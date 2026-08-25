# Standalone Vulkan probes

Two small C programs that answer capability questions without building Godot. They link
only against the system Vulkan loader, so they run on a bare box.

- `vk_rt_probe.c` — enumerates devices and reports whether the raytracing extensions and
  the descriptor-indexing feature bits are present. Answers "is bindless a hardware limit
  or a Godot device-creation omission?" (see `docs/rd-capability-spike-report.md`).
- `vk_g1_probe.c` — reports `maxColorAttachments` / `maxFragmentOutputAttachments` and
  per-format color-attachment support for the G1 Tier-1 attachment set. Answers whether
  the schema fits the MRT budget and whether each chosen format is a legal render target
  (see `docs/gbuffer-schema-v1-proposal.md`).
- `vk_asfmt_probe.c` — queries
  `VK_FORMAT_FEATURE_2_ACCELERATION_STRUCTURE_VERTEX_BUFFER_BIT_KHR` per vertex format.
  Answers which quantized position formats can feed a BLAS build
  (see `docs/lumen-s0-spike-report.md` section 3).

## Build and run

Needs Vulkan headers; the ones vendored in this repo work. There is no `libvulkan.so`
dev symlink on a stock Ubuntu box, so link the versioned soname directly.

```sh
gcc -Ithirdparty/vulkan/include -o vk_rt_probe vk_probes/vk_rt_probe.c \
    /usr/lib/$(uname -m)-linux-gnu/libvulkan.so.1
./vk_rt_probe
```

Results measured on NVIDIA GB10 (Linux aarch64, driver 580.173.02) are recorded in the
two reports above. Re-run on AMD/Intel/mobile before assuming they generalize.
