extends Node

## Wave 0 RD capability spike harness.
##
## Measures the runtime numbers that docs/rd-capability-spike-report.md cannot establish
## by reading code. Every probe prints RESULT lines; the report is written from those.
##
## Run:
##   bin/godot.<platform>.editor.<arch> --quit-after 2000 --path misc/rd_capability_spike
##   xvfb-run -a bin/godot.linuxbsd.editor.arm64 --rendering-driver vulkan \
##       --quit-after 2000 --path misc/rd_capability_spike
##
## ALWAYS pass --quit-after. A GDScript parse error means _ready() never runs, so the
## quit() below never fires and the process hangs until killed. Referencing an unbound
## RenderingDevice constant is a parse error too, with the same symptom.
##
## ---------------------------------------------------------------------------------
## On clocks (this is the whole reason for the rewrite)
##
## An earlier version stamped both the readback request and its callback with
## Engine.get_frames_drawn(). That counter is incremented on the MAIN thread
## (main/main.cpp, Main::iteration) and is a plain non-atomic member. With
## thread_model=2 the render thread trails the main thread by a frame, so a callback
## running on the render thread reads a value that is ahead of it -- and the "+1" that
## appeared for thread_model=2 may have been that skew rather than any real difference
## in readback latency. Two backends agreeing meant only that both read the same
## counter the same wrong way.
##
## So: A1 is stamped with _rt_frame, a counter owned and read only by the render
## thread. Both stamps come from one clock on one thread. Engine.get_frames_drawn() is
## still recorded alongside, purely so the skew between the two is visible in the
## output instead of silently contaminating it.

const READBACK_BYTES := 64 * 1024

## Sampled in steady state: at startup the staging blocks are empty and the frame ring
## is not yet cycling, which biases the result low.
const A1_WARMUP_FRAMES := 60
const A1_SAMPLES := 10

## Independent repeats of the whole A1 series. A single run per configuration cannot
## distinguish a law from a coincidence.
const A1_REPEATS := 3

## Terminal guard. Any probe that neither succeeds nor fails within this many frames is
## reported as STUCK and the harness exits non-zero, rather than hanging.
const WATCHDOG_FRAMES := 1200

var _rd: RenderingDevice
var _buffer: RID

# Render-thread-owned frame sequence. Written and read ONLY on the render thread.
var _rt_frame := 0

# --- A1: async readback round-trip latency. -----------------------------------
var _a1_pending: Array[int] = []      # render-thread stamps of issued requests
var _a1_latencies: Array[int] = []    # render-thread deltas
var _a1_skews: Array[int] = []        # get_frames_drawn() minus _rt_frame, at callback
var _a1_issued := 0
var _a1_repeat := 0
var _a1_series: Array[String] = []
var _a1_done := false
var _a1_failed := false

# --- B1: do consecutive enqueues from _process drain in one render frame? -----
##
## An earlier version reported this as "call_on_render_thread marshaling latency" and
## published 0 frames single-threaded, 1 frame with a separate render thread. That
## number was wrong for the same reason A1's was: the push side was stamped with
## _rt_frame read from the MAIN thread, and the two threads sit a frame apart, so the
## "1 frame" was the clock changing hands rather than any latency.
##
## Main-to-render latency cannot be measured with one clock, because the push and the
## run are on different threads by construction. Rather than invent a number, this
## probe measures something that IS single-clock and worth knowing: whether two
## callables enqueued back-to-back from _process are drained in the same render-thread
## frame. Worker-thread and late-frame enqueue remain unmeasured; see the report.
var _b1_mark_rt := -1
var _b1_land_rt := -1
var _b1_done := false

# --- B2: does the thread guard reject a worker thread? ------------------------
var _b2_thread: Thread
var _b2_result := "not run"

var _phase := 0
var _phase_started_frame := 0
var _fatal := ""


func _ready() -> void:
	_rd = RenderingServer.get_rendering_device()
	if _rd == null:
		_die("No RenderingDevice. Use a real rendering driver; --headless has none.")
		return

	# Frame-time probes are meaningless under vsync -- every frame reads 16.67 ms.
	DisplayServer.window_set_vsync_mode(DisplayServer.VSYNC_DISABLED)
	Engine.max_fps = 0

	print("=== RD capability spike ===")
	print("RESULT run_id=%s" % _run_id())
	print("RESULT engine_version=%s" % Engine.get_version_info().get("string", "?"))
	print("RESULT engine_build_hash=%s" % Engine.get_version_info().get("hash", "?"))
	print("RESULT executable=%s" % OS.get_executable_path().get_file())
	print("RESULT device_name=%s" % RenderingServer.get_video_adapter_name())
	# The ACTIVE driver, not the project setting -- they differ (Apple silicon resolves
	# to Metal whatever the setting says), and mislabelling the backend voids the result.
	print("RESULT rendering_driver_active=%s" % RenderingServer.get_current_rendering_driver_name())
	print("RESULT rendering_method_active=%s" % RenderingServer.get_current_rendering_method())
	print("RESULT rendering_driver_setting=%s" % ProjectSettings.get_setting("rendering/rendering_device/driver", "unset"))
	# project_settings.cpp -> "Unsafe (deprecated),Safe,Separate"; default Safe(1).
	print("RESULT thread_model=%s (0=unsafe-deprecated 1=safe-single 2=separate-render-thread)" % ProjectSettings.get_setting(
		"rendering/driver/threads/thread_model", "unset(default 1=safe)"))
	print("RESULT frame_queue_size=%s" % ProjectSettings.get_setting(
		"rendering/rendering_device/vsync/frame_queue_size", "unset(default 2)"))
	print("RESULT staging_block_size_kb=%s" % ProjectSettings.get_setting(
		"rendering/rendering_device/staging_buffer/block_size_kb", "unset(default 256)"))
	print("RESULT staging_max_size_mb=%s" % ProjectSettings.get_setting(
		"rendering/rendering_device/staging_buffer/max_size_mb", "unset(default 128)"))
	print("RESULT a1_repeats=%d samples_per_repeat=%d warmup_frames=%d" % [
		A1_REPEATS, A1_SAMPLES, A1_WARMUP_FRAMES])

	RenderingServer.call_on_render_thread(_create_buffer)


func _cleanup() -> void:
	if _buffer.is_valid():
		_rd.free_rid(_buffer)
		_buffer = RID()


func _run_id() -> String:
	# Stable enough to correlate a log with its report entry.
	return "%s-%s-%d" % [
		RenderingServer.get_current_rendering_driver_name(),
		str(ProjectSettings.get_setting("rendering/driver/threads/thread_model", 1)),
		Time.get_unix_time_from_system()]


func _die(msg: String) -> void:
	_fatal = msg
	print("RESULT fatal=%s" % msg)
	print("=== spike FAILED ===")
	get_tree().quit(1)


## Runs on the render thread, once per frame, ahead of anything else we enqueue that
## frame. This is the only clock A1 uses.
func _render_tick() -> void:
	_rt_frame += 1


func _create_buffer() -> void:
	var zeros := PackedByteArray()
	zeros.resize(READBACK_BYTES)
	_buffer = _rd.storage_buffer_create(READBACK_BYTES, zeros)


func _process(_delta: float) -> void:
	if _fatal != "":
		return

	# Advance the render-thread clock first so anything enqueued below is stamped
	# against a value that has already been incremented this frame.
	RenderingServer.call_on_render_thread(_render_tick)

	if Engine.get_frames_drawn() - _phase_started_frame > WATCHDOG_FRAMES:
		_die("watchdog: phase %d made no progress in %d frames" % [_phase, WATCHDOG_FRAMES])
		return

	match _phase:
		0:
			if not _buffer.is_valid():
				return
			if Engine.get_frames_drawn() < A1_WARMUP_FRAMES:
				return
			if _a1_failed:
				_advance(3)
				return
			if _a1_issued < A1_SAMPLES:
				_a1_issued += 1
				RenderingServer.call_on_render_thread(_a1_request)
			elif _a1_count(_a1_latencies) >= A1_SAMPLES:
				_a1_finish_repeat()
		1:
			if _b1_done:
				_advance(2)
		2:
			if _b2_thread != null and not _b2_thread.is_alive():
				_b2_thread.wait_to_finish()
				_b2_thread = null
				print("RESULT b2_worker_thread_direct_call=%s" % _b2_result)
				_advance(3)
		3:
			for line in _a1_series:
				print(line)
			# Free on the render thread; free_rid is behind the same guard as everything
			# else, and leaking prints a warning that looks like a probe failure.
			RenderingServer.call_on_render_thread(_cleanup)
			_phase = 4
		4:
			print("=== spike complete ===")
			get_tree().quit(0)


func _advance(next_phase: int) -> void:
	_phase = next_phase
	_phase_started_frame = Engine.get_frames_drawn()
	if next_phase == 1:
		# Both stamps are taken ON the render thread, in enqueue order. Nothing reads
		# _rt_frame from the main thread.
		RenderingServer.call_on_render_thread(_b1_mark)
		RenderingServer.call_on_render_thread(_b1_land)
	elif next_phase == 2:
		_b2_thread = Thread.new()
		_b2_thread.start(_b2_body)


# --- A1 ----------------------------------------------------------------------
func _a1_request() -> void:
	# Render thread: stamp and issue on the same clock the callback will read.
	_a1_pending.push_back(_rt_frame)
	var err := _rd.buffer_get_data_async(_buffer, _a1_callback, 0, READBACK_BYTES)
	if err != OK:
		print("RESULT a1=FAILED err=%d" % err)
		_a1_failed = true


func _a1_callback(data: PackedByteArray) -> void:
	# Render thread. _rt_frame is ours; get_frames_drawn() belongs to the main thread
	# and is recorded only to expose the skew, never to compute the latency.
	var idx := _a1_latencies.size()
	if idx < _a1_pending.size():
		_a1_latencies.push_back(_rt_frame - _a1_pending[idx])
		_a1_skews.push_back(Engine.get_frames_drawn() - _rt_frame)
	if data.size() != READBACK_BYTES:
		print("RESULT a1_short_read=%d" % data.size())
		_a1_failed = true


func _a1_count(arr: Array) -> int:
	return arr.size()


func _a1_finish_repeat() -> void:
	var lo := 1 << 30
	var hi := -1
	var sum := 0
	for l in _a1_latencies:
		lo = mini(lo, l)
		hi = maxi(hi, l)
		sum += l
	var skew_lo := 1 << 30
	var skew_hi := -1
	for sk in _a1_skews:
		skew_lo = mini(skew_lo, sk)
		skew_hi = maxi(skew_hi, sk)

	_a1_series.push_back(
		"RESULT a1_repeat=%d roundtrip_rt_frames min=%d max=%d mean=%.2f samples=%d raw=%s" % [
			_a1_repeat, lo, hi, float(sum) / maxf(1.0, float(_a1_latencies.size())),
			_a1_latencies.size(), str(_a1_latencies)])
	# If this is non-zero, a main-thread frame counter would have reported a different
	# latency than the render-thread clock did. That difference is the bug the earlier
	# version of this harness shipped as a finding.
	_a1_series.push_back(
		"RESULT a1_repeat=%d main_vs_render_clock_skew min=%d max=%d" % [
			_a1_repeat, skew_lo, skew_hi])

	_a1_repeat += 1
	_a1_latencies.clear()
	_a1_pending.clear()
	_a1_skews.clear()
	_a1_issued = 0
	if _a1_repeat >= A1_REPEATS:
		_advance(1)
	else:
		_phase_started_frame = Engine.get_frames_drawn()


# --- B1 ----------------------------------------------------------------------
func _b1_mark() -> void:
	_b1_mark_rt = _rt_frame


func _b1_land() -> void:
	_b1_land_rt = _rt_frame
	print("RESULT b1_consecutive_enqueue_same_render_frame=%s (mark_rt=%d land_rt=%d delta=%d)" % [
		str(_b1_land_rt == _b1_mark_rt), _b1_mark_rt, _b1_land_rt, _b1_land_rt - _b1_mark_rt])
	print("RESULT b1_scope=both stamps taken on the render thread; this is NOT main-to-render latency")
	print("RESULT b1_unmeasured=worker-thread enqueue, late-frame enqueue, physics-tick enqueue")
	_b1_done = true


# --- B2 ----------------------------------------------------------------------
func _b2_body() -> void:
	# Expected to fail: buffer_get_data_async sits behind ERR_RENDER_THREAD_GUARD_V.
	# An error here is the PASS condition for the #99750 finding, not a bug.
	var err := _rd.buffer_get_data_async(_buffer, _b2_never, 0, READBACK_BYTES)
	if err == OK:
		_b2_result = "ACCEPTED (guard absent -- contradicts report section B)"
	else:
		_b2_result = "REJECTED err=%d (guard intact, as reported)" % err


func _b2_never(_data: PackedByteArray) -> void:
	push_error("worker-thread readback callback fired; guard did not hold")
