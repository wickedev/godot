# Raw spike measurements

> Stored as `.txt`, not `.log`: the repo `.gitignore` excludes both `[Ll]ogs/` and
> `*.log`, so a `logs/*.log` layout is silently untracked. Evidence that does not get
> committed is not evidence.

Unedited stdout from the four-configuration A1/B1/B2 matrix. Committed so the numbers in
`docs/rd-capability-spike-report.md` can be checked without re-running anything.

| Machine | NVIDIA GB10, Linux aarch64, driver 580.173.02 |
|---|---|
| Godot commit | `f0337511c7cd046dd33b81a25441f9440bee5337` |
| Build string | `4.8.dev.custom_build.f0337511c` |
| Build flags | `platform=linuxbsd arch=arm64 target=editor scu_build=yes vulkan=yes opengl3=no debug_symbols=no` |
| Invocation | `xvfb-run -a ./bin/godot.linuxbsd.editor.arm64 --rendering-driver vulkan --quit-after 3000 --path <harness>` |
| Date | 2026-08-25 |

Each file is one `thread_model` / `frame_queue_size` pair and contains three independent
repeats of the A1 series. `RESULT run_id=` in each log identifies the run.

## What to look at

`a1_repeat=N roundtrip_rt_frames` is the measurement. It is stamped with the harness's
own render-thread counter, not `Engine.get_frames_drawn()`.

`a1_repeat=N main_vs_render_clock_skew` is the diagnostic that made the earlier numbers
wrong: it is -1 under `thread_model=1` and 0 under `thread_model=2`. A harness stamping
with the main-thread counter therefore reports a latency one higher under
`thread_model=2` purely from the clock change. That is exactly the spurious "+1" the
first version of this report published as a finding.

## Known issue in these logs

The `tm2_*` runs exit 134 (SIGABRT), after `=== spike complete ===`, during engine
shutdown. Godot's own `RenderingDevice::finalize` trips the render-thread guard
(`rendering_device.cpp:8925`). All probe output is produced before it.

Adjudicated as a real core lifecycle defect, not a harness problem: the render thread
takes RD ownership and it is not handed back to main at shutdown join, so finalize runs
on the wrong thread and driver teardown is skipped. Tracked separately; recorded here
rather than trimmed out.

## What B1 measures, and what it does not

`b1_consecutive_enqueue_same_render_frame` is a single-clock property: both stamps are
taken on the render thread. It says consecutive enqueues from `_process` are batched
into one render frame. It is **not** main-to-render latency — that spans two threads by
construction and cannot be read off one clock. Two earlier revisions of this report
published a marshaling latency figure; both were artifacts of reading a render-thread
counter from the main thread, and both are retracted.
