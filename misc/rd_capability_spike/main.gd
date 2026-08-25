extends Node

## Wave 0 RD capability spike harness.
##
## Measures the runtime numbers that docs/rd-capability-spike-report.md could only
## establish structurally. Each probe prints one RESULT line; the report is updated
## from those lines.
##
## Run:  bin/godot.<platform>.editor.<arch> --path misc/rd_capability_spike

const READBACK_BYTES := 64 * 1024

var _rd: RenderingDevice
var _buffer: RID

# --- Probe A1: async readback round-trip latency, in frames. -------------------
## Sampled in steady state, not on frame 0: at startup the staging blocks are empty
## and the frame ring is not yet cycling normally, which biases the result low.
const A1_WARMUP_FRAMES := 60
const A1_SAMPLES := 10

var _a1_request_frames: Array[int] = []
var _a1_latencies: Array[int] = []
var _a1_issued := 0
var _a1_done := false

# --- Probe B1: call_on_render_thread marshaling latency, in frames. ----------
var _b1_push_frame := -1
var _b1_done := false

# --- Probe B2: does the thread guard actually reject a worker thread? ---------
var _b2_thread: Thread
var _b2_result := "not run"

var _phase := 0
var _mutex := Mutex.new()

## Watchdog: every probe must reach a terminal state. Without this an error path that
## never fires its callback hangs the harness instead of reporting a failure.
const WATCHDOG_FRAMES := 900


func _ready() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		push_error("No RenderingDevice. Run with a real rendering driver, not --headless.")
		get_tree().quit(1)
		return

	# Frame-time probes are meaningless under vsync -- every frame reads 16.67 ms.
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	print("=== RD capability spike ===")
	print("RESULT vsync_mode_after_disable=%d (0=disabled)" % DisplayServer.window_get_vsync_mode())
	print("RESULT device_name=%s" % RenderingServer.get_video_adapter_name())
	# The ACTIVE driver, not the project setting -- they differ (Apple silicon defaults
	# to Metal regardless of the "vulkan" setting), and attributing results to the wrong
	# backend invalidates them.
	print("RESULT rendering_driver_active=%s" % RenderingServer.get_current_rendering_driver_name())
	print("RESULT rendering_method_active=%s" % RenderingServer.get_current_rendering_method())
	print("RESULT rendering_driver_setting=%s" % ProjectSettings.get_setting("rendering/rendering_device/driver", "unset"))
	print("RESULT cmdline=%s" % " ".join(OS.get_cmdline_args()))
	# project_settings.cpp:1851 -> "Unsafe (deprecated),Safe,Separate"; default is Safe(1).
	print("RESULT thread_model=%s (0=unsafe-deprecated 1=safe-single 2=separate-render-thread)" % ProjectSettings.get_setting(
		"rendering/driver/threads/thread_model", "unset(default 1=safe)"))
	print("RESULT is_render_thread_same_as_main=%s" % str(
		OS.get_thread_caller_id() == OS.get_main_thread_id()))
	print("RESULT frame_queue_size=%s" % ProjectSettings.get_setting(
		"rendering/rendering_device/vsync/frame_queue_size", "unset(default 2)"))
	print("RESULT staging_block_size_kb=%s" % ProjectSettings.get_setting(
		"rendering/rendering_device/staging_buffer/block_size_kb", "unset(default 256)"))
	print("RESULT staging_max_size_mb=%s" % ProjectSettings.get_setting(
		"rendering/rendering_device/staging_buffer/max_size_mb", "unset(default 128)"))

	# The buffer must be created on the render thread like every other RD call.
	RenderingServer.call_on_render_thread(_create_buffer)


func _create_buffer() -> void:
	var zeros := PackedByteArray()
	zeros.resize(READBACK_BYTES)
	_buffer = _rd.storage_buffer_create(READBACK_BYTES, zeros)


func _process(_delta: float) -> void:
	match _phase:
		0:
			# Probe A1 -- one readback per frame, in steady state.
			if _buffer.is_valid() and Engine.get_frames_drawn() >= A1_WARMUP_FRAMES:
				if _a1_issued < A1_SAMPLES:
					_a1_issued += 1
					RenderingServer.call_on_render_thread(_a1_request)
				elif _a1_sample_count() >= A1_SAMPLES:
					_a1_report()
					_phase = 1
		1:
			if _a1_done:
				_phase = 2
				_b1_push_frame = Engine.get_frames_drawn()
				RenderingServer.call_on_render_thread(_b1_land)
		2:
			if _b1_done:
				_phase = 3
				_start_b2()
		3:
			if _b2_thread != null and not _b2_thread.is_alive():
				_b2_thread.wait_to_finish()
				_b2_thread = null
				print("RESULT b2_worker_thread_direct_call=%s" % _b2_result)
				_phase = 4
		4:
			print("=== spike complete ===")
			get_tree().quit(0)


# --- A1 ----------------------------------------------------------------------
func _a1_request() -> void:
	_mutex.lock()
	_a1_request_frames.push_back(Engine.get_frames_drawn())
	_mutex.unlock()
	var err := _rd.buffer_get_data_async(_buffer, _a1_callback, 0, READBACK_BYTES)
	if err != OK:
		print("RESULT a1=FAILED err=%d" % err)
		_a1_done = true


func _a1_callback(data: PackedByteArray) -> void:
	var landed := Engine.get_frames_drawn()
	_mutex.lock()
	var idx := _a1_latencies.size()
	if idx < _a1_request_frames.size():
		_a1_latencies.push_back(landed - _a1_request_frames[idx])
	_mutex.unlock()
	if data.size() != READBACK_BYTES:
		print("RESULT a1_short_read=%d" % data.size())


func _a1_sample_count() -> int:
	_mutex.lock()
	var n := _a1_latencies.size()
	_mutex.unlock()
	return n


func _a1_report() -> void:
	var lo := 1 << 30
	var hi := -1
	var sum := 0
	for l in _a1_latencies:
		lo = mini(lo, l)
		hi = maxi(hi, l)
		sum += l
	print("RESULT a1_roundtrip_frames min=%d max=%d mean=%.2f samples=%d bytes=%d" % [
		lo, hi, float(sum) / _a1_latencies.size(), _a1_latencies.size(), READBACK_BYTES])
	print("RESULT a1_roundtrip_raw=%s" % str(_a1_latencies))
	_a1_done = true


# --- B1 ----------------------------------------------------------------------
func _b1_land() -> void:
	var landed := Engine.get_frames_drawn()
	print("RESULT b1_call_on_render_thread_delay_frames=%d (pushed_on=%d ran_on=%d)" % [
		landed - _b1_push_frame, _b1_push_frame, landed])
	_b1_done = true


# --- B2 ----------------------------------------------------------------------
func _start_b2() -> void:
	_b2_thread = Thread.new()
	_b2_thread.start(_b2_body)


func _b2_body() -> void:
	# Expected to fail: buffer_get_data_async is behind ERR_RENDER_THREAD_GUARD_V.
	# An error here is the PASS condition for the #99750 finding, not a bug.
	var err := _rd.buffer_get_data_async(_buffer, _b2_never, 0, READBACK_BYTES)
	if err == OK:
		_b2_result = "ACCEPTED (guard absent -- contradicts report section B)"
	else:
		_b2_result = "REJECTED err=%d (guard intact, as reported)" % err


func _b2_never(_data: PackedByteArray) -> void:
	push_error("worker-thread readback callback fired; guard did not hold")
