# Metal capability probes

Small Objective-C programs that query `MTLDevice` directly. They answer questions that
cannot be answered on Linux, and that reading Godot's Metal driver cannot answer either
— the driver may not implement something the hardware supports, or vice versa.

- `metal_ts_probe.m` — GPU counter sampling support, available counter sets, and
  CPU/GPU timestamp correlation. See `docs/gpu-profiler-metal-timestamp-report.md`.

## Build and run (macOS only)

```sh
clang -fobjc-arc -framework Foundation -framework Metal -o metal_ts_probe metal_ts_probe.m
./metal_ts_probe
```

Results recorded in the report were measured on Apple M2 Pro / macOS 26.5.2. Counter
sampling support varies by Apple silicon generation, so re-run before assuming they
hold on other hardware.
