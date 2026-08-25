extends Node

## Lumen S0-2 / G2 BLAS probe.
##
## Two questions, reported separately so a partial result is still usable:
##   S0-2  Can the RD raytracing API build and trace an acceleration structure at all?
##         That is the entry condition for Lumen path (c).
##   G2    Does a quantized vertex format survive Godot's blas_create -> blas_build?
##         Godot validates nothing here, so this has to be tried rather than reasoned about.
##
## Needs a driver with VK_KHR_acceleration_structure. Apple builds compile raytracing out
## entirely, so this reports "unsupported" there rather than failing.
##
## Run: bin/godot.<platform>.editor.<arch> --path misc/rt_spike

static var TRI := PackedVector3Array([Vector3(0, 0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0)])

var _rd: RenderingDevice
var _cleanup: Array[RID] = []
var _done := false


func _ready() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		print("RESULT rd=NULL (needs a real rendering driver)")
		get_tree().quit(1)
		return
	print("=== RT spike ===")
	print("RESULT device=%s" % RenderingServer.get_video_adapter_name())
	RenderingServer.call_on_render_thread(_run)


func _process(_d: float) -> void:
	if _done:
		get_tree().quit(0)


func _run() -> void:
	var has_rq := _rd.has_feature(RenderingDevice.SUPPORTS_RAY_QUERY)
	var has_rp := _rd.has_feature(RenderingDevice.SUPPORTS_RAYTRACING_PIPELINE)
	print("RESULT supports_ray_query=%s" % has_rq)
	print("RESULT supports_raytracing_pipeline=%s" % has_rp)

	if not (has_rq or has_rp):
		print("RESULT s0_2=UNSUPPORTED (no raytracing on this driver; path (c) not enterable here)")
		_done = true
		return

	# --- Stage 1: baseline float3 geometry, the uncompressed reference. ---
	_try_blas("float32x3", RenderingDevice.DATA_FORMAT_R32G32B32_SFLOAT, _pack_f32(), 12)

	# --- Stage 2: the G2 proposal -- quantized SNORM, 4 components with padding. ---
	_try_blas("snorm16x4", RenderingDevice.DATA_FORMAT_R16G16B16A16_SNORM, _pack_snorm16x4(), 8)

	# --- Stage 3: tighter 3-component packing. Not spec-mandatory; expected to be
	#     vendor-dependent, which is exactly the thing worth measuring. ---
	_try_blas("snorm16x3", RenderingDevice.DATA_FORMAT_R16G16B16_SNORM, _pack_snorm16x3(), 6)

	_done = true


func _try_blas(label: String, fmt: int, data: PackedByteArray, stride: int) -> void:
	# Must be a vertex buffer: blas_create resolves the RID through vertex_buffer_owner,
	# so a storage buffer fails with "Parameter vertex_buffer is null". The AS-build
	# usage bit is what makes it legal as acceleration-structure input.
	var vbuf := _rd.vertex_buffer_create(data.size(), data,
		RenderingDevice.BUFFER_CREATION_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT)
	if not vbuf.is_valid():
		print("RESULT blas[%s]=BUFFER_FAILED" % label)
		return
	_cleanup.push_back(vbuf)

	var geom := RDAccelerationStructureGeometry.new()
	geom.vertex_buffer = vbuf
	geom.vertex_offset = 0
	geom.vertex_stride = stride
	geom.vertex_count = 3
	geom.vertex_format = fmt

	var blas := _rd.blas_create([geom], 0)
	if not blas.is_valid():
		print("RESULT blas[%s]=CREATE_FAILED" % label)
		return
	_cleanup.push_back(blas)

	var err := _rd.blas_build(blas)
	if err != OK:
		print("RESULT blas[%s]=BUILD_FAILED err=%d" % [label, err])
		return
	print("RESULT blas[%s]=OK (stride=%dB)" % [label, stride])

	# TLAS over that BLAS. The instance id exercises the 24-bit namespace frozen in
	# G1 section 3 -- 0x00FFFFFE is the largest legal value below the sentinel.
	var tlas := _rd.tlas_create(1, 0)
	if not tlas.is_valid():
		print("RESULT tlas[%s]=CREATE_FAILED" % label)
		return
	_cleanup.push_back(tlas)

	var inst := RDAccelerationStructureInstance.new()
	inst.transform = Transform3D.IDENTITY
	inst.id = 0x00FFFFFE
	inst.mask = 0xFF
	inst.blas = blas

	var terr := _rd.tlas_build(tlas, [inst])
	print("RESULT tlas[%s]=%s" % [label, "OK" if terr == OK else "BUILD_FAILED err=%d" % terr])


# --- vertex packing helpers ---------------------------------------------------
func _pack_f32() -> PackedByteArray:
	var b := PackedByteArray()
	for v in TRI:
		for c in [v.x, v.y, v.z]:
			var t := PackedByteArray(); t.resize(4); t.encode_float(0, c)
			b.append_array(t)
	return b


func _snorm16(x: float) -> int:
	return int(clampf(x, -1.0, 1.0) * 32767.0)


func _pack_snorm16x4() -> PackedByteArray:
	var b := PackedByteArray()
	for v in TRI:
		for c in [v.x, v.y, v.z, 0.0]:
			var t := PackedByteArray(); t.resize(2); t.encode_s16(0, _snorm16(c))
			b.append_array(t)
	return b


func _pack_snorm16x3() -> PackedByteArray:
	var b := PackedByteArray()
	for v in TRI:
		for c in [v.x, v.y, v.z]:
			var t := PackedByteArray(); t.resize(2); t.encode_s16(0, _snorm16(c))
			b.append_array(t)
	return b
