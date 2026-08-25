#!/usr/bin/env python
"""Compile Jolt's HLSL compute kernels to SPIR-V, and check the committed results still match.

The kernels are vendored, fixed assets: nothing in the engine needs to compile HLSL at runtime, so
they are built ahead of time and the SPIR-V is committed. That trade buys "no HLSL toolchain in the
engine" and costs "the binary and the source can drift apart without anyone noticing", which is
what --verify exists to stop.

Two modes:

    regenerate.py --regenerate    recompile from source, rewrite the artifacts and the manifest
    regenerate.py --verify        check the committed artifacts against the committed manifest

Regeneration needs DXC, which has no macOS build and no arm64 Linux build, so it runs inside a
pinned x86_64 container. Verification only needs hashes, and optionally spirv-val, so it runs
natively anywhere.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

# --- Pins -------------------------------------------------------------------------------------
#
# Committing build output makes the compiler part of the source. An unpinned toolchain would mean
# two people regenerating from identical sources and getting different bytes, which turns every
# diff into noise and makes --verify meaningless.

CONTAINER_IMAGE = "ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517"
DXC_URL = (
    "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2607/linux_dxc_2026_07_29.x86_x64.tar.gz"
)
DXC_SHA256 = "55665c87824051ed4774ff3280a79ccbbb7d39243b9736ca5e98222134112d54"

TARGET_ENV = "vulkan1.1"
SHADER_PROFILE = "cs_6_0"
ENTRY_POINT = "main"
# -fvk-use-dx-layout keeps HLSL's own struct packing rules. The kernels share their struct
# definitions with C++ through Jolt/Shaders/*.h, so the GPU layout has to match what the CPU side
# writes -- switching to Vulkan/GLSL layout rules would silently shift member offsets.
DXC_FLAGS = ["-T", SHADER_PROFILE, "-E", ENTRY_POINT, "-spirv", "-fspv-target-env=" + TARGET_ENV, "-fvk-use-dx-layout"]

SOURCE_DIR = "thirdparty/jolt_physics/Jolt/Shaders"
OUTPUT_DIR = "modules/jolt_physics/shaders"
MANIFEST = os.path.join(OUTPUT_DIR, "manifest.json")

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def repo_root():
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))


def sha256_file(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def resolve_includes(root, path, seen):
    """Every header a kernel pulls in, transitively.

    The manifest hashes these too. A kernel's own text is often unchanged while a binding header or
    a shared struct underneath it moved, and that is exactly the change that makes the committed
    SPIR-V wrong while looking untouched.
    """
    path = os.path.normpath(path)
    if path in seen or not os.path.isfile(path):
        return
    seen.add(path)
    with open(path, "r", encoding="utf-8") as f:
        text = f.read()
    for name in INCLUDE_RE.findall(text):
        resolve_includes(root, os.path.join(os.path.dirname(path), name), seen)


def kernel_sources(root):
    src = os.path.join(root, SOURCE_DIR)
    return sorted(f for f in os.listdir(src) if f.endswith(".hlsl"))


def source_fingerprint(root, kernel):
    """Hash of the kernel plus every header it includes, keyed by repo-relative path."""
    src = os.path.join(root, SOURCE_DIR, kernel)
    seen = set()
    resolve_includes(root, src, seen)
    digest = hashlib.sha256()
    for path in sorted(seen):
        rel = os.path.relpath(path, root)
        digest.update(rel.encode("utf-8"))
        digest.update(sha256_file(path).encode("ascii"))
    return digest.hexdigest(), sorted(os.path.relpath(p, root) for p in seen)


def parse_bindings(disassembly):
    """Pull `name -> (set, binding)` out of spirv-dis output.

    Jolt's own Vulkan backend looks resources up by name (ComputeShaderVK::NameToBufferInfoIndex),
    so the numbers do not have to be stable across kernels -- but they do have to be unique within
    one, and they have to be what the manifest says. DXC assigns them from declaration order, so
    reordering a binding header silently renumbers everything.
    """
    names = {}
    for var_id, name in re.findall(r'OpName (%\w+) "([^"]+)"', disassembly):
        names[var_id] = name

    sets, bindings = {}, {}
    for var_id, value in re.findall(r"OpDecorate (%\w+) DescriptorSet (\d+)", disassembly):
        sets[var_id] = int(value)
    for var_id, value in re.findall(r"OpDecorate (%\w+) Binding (\d+)", disassembly):
        bindings[var_id] = int(value)

    result = {}
    for var_id, binding in sorted(bindings.items(), key=lambda kv: kv[1]):
        result[names.get(var_id, var_id)] = {"set": sets.get(var_id, 0), "binding": binding}
    return result


def check_unique_bindings(kernel, bindings):
    seen = {}
    for name, slot in bindings.items():
        key = (slot["set"], slot["binding"])
        if key in seen:
            raise SystemExit(
                "%s: '%s' and '%s' both landed on set %d binding %d. Two resources aliasing one "
                "slot compiles and validates; it fails when the pipeline is created."
                % (kernel, seen[key], name, key[0], key[1])
            )
        seen[key] = name


CONTAINER_SCRIPT = r"""
set -e
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq curl ca-certificates spirv-tools >/dev/null 2>&1

cd /tmp
curl -sL -o dxc.tar.gz "$DXC_URL"
echo "$DXC_SHA256  dxc.tar.gz" | sha256sum -c - >/dev/null
mkdir -p dxc && tar xzf dxc.tar.gz -C dxc
DXC=$(find /tmp/dxc -name dxc -type f -perm -u+x | head -1)
export LD_LIBRARY_PATH=$(dirname "$DXC")/../lib:$(dirname "$DXC")

mkdir -p /out
{
  # Both of these exit non-zero while still printing their version, so they are shielded from
  # `set -e` rather than trusted.
  echo "dxc_version=$("$DXC" --version 2>&1 | head -1 | tr -d '\r' || true)"
  echo "spirv_tools_version=$(spirv-val --version 2>&1 | head -1 | tr -d '\r' || true)"
} > /out/_tools.txt || true

for f in /shaders/*.hlsl; do
  n=$(basename "$f" .hlsl)
  # spirv-val is not advisory here. glslang's HLSL path needed spirv-opt legalization to produce
  # valid modules; DXC does not, but "does not need it" is a claim that has to be rechecked every
  # time rather than assumed once.
  "$DXC" $DXC_FLAGS -I /shaders "$f" -Fo "/out/$n.spv"
  spirv-val --target-env "$TARGET_ENV" "/out/$n.spv"
  spirv-dis "/out/$n.spv" > "/out/$n.dis"
done
"""


def run_container(root, out_dir):
    if not shutil.which("docker"):
        raise SystemExit(
            "docker is required to regenerate.\n"
            "DXC ships no macOS build and no arm64 Linux build, so regeneration runs in a pinned\n"
            "x86_64 container. On Apple Silicon this needs emulation, which Docker Desktop and\n"
            "OrbStack both provide. Verification (--verify) needs none of this."
        )

    cmd = [
        "docker",
        "run",
        "--rm",
        "--platform",
        "linux/amd64",
        "-e",
        "DXC_URL=" + DXC_URL,
        "-e",
        "DXC_SHA256=" + DXC_SHA256,
        "-e",
        "DXC_FLAGS=" + " ".join(DXC_FLAGS),
        "-e",
        "TARGET_ENV=" + TARGET_ENV,
        "-v",
        os.path.join(root, SOURCE_DIR) + ":/shaders:ro",
        "-v",
        out_dir + ":/out",
        CONTAINER_IMAGE,
        "bash",
        "-c",
        CONTAINER_SCRIPT,
    ]
    subprocess.check_call(cmd)


def regenerate(root):
    out_dir = os.path.join(root, OUTPUT_DIR, ".build")
    if os.path.isdir(out_dir):
        shutil.rmtree(out_dir)
    os.makedirs(out_dir)

    run_container(root, out_dir)

    tools = {}
    with open(os.path.join(out_dir, "_tools.txt"), "r", encoding="utf-8") as f:
        for line in f:
            key, _, value = line.strip().partition("=")
            tools[key] = value

    manifest = {
        "_comment": "Generated by regenerate.py. Do not edit; run --regenerate and commit the diff.",
        "toolchain": {
            "container_image": CONTAINER_IMAGE,
            "dxc_url": DXC_URL,
            "dxc_sha256": DXC_SHA256,
            "dxc_version": tools.get("dxc_version", ""),
            "spirv_tools_version": tools.get("spirv_tools_version", ""),
            "flags": DXC_FLAGS,
            "target_env": TARGET_ENV,
            "entry_point": ENTRY_POINT,
            "profile": SHADER_PROFILE,
        },
        "kernels": {},
    }

    for kernel in kernel_sources(root):
        name = kernel[: -len(".hlsl")]
        built = os.path.join(out_dir, name + ".spv")
        if not os.path.isfile(built):
            raise SystemExit("%s produced no SPIR-V." % kernel)

        with open(os.path.join(out_dir, name + ".dis"), "r", encoding="utf-8") as f:
            bindings = parse_bindings(f.read())
        check_unique_bindings(kernel, bindings)

        shutil.copyfile(built, os.path.join(root, OUTPUT_DIR, name + ".spv"))
        fingerprint, sources = source_fingerprint(root, kernel)
        manifest["kernels"][name] = {
            "entry_point": ENTRY_POINT,
            "source_sha256": fingerprint,
            "sources": sources,
            "spv_sha256": sha256_file(built),
            "spv_bytes": os.path.getsize(built),
            "bindings": bindings,
        }

    with open(os.path.join(root, MANIFEST), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")

    shutil.rmtree(out_dir)
    print("Regenerated %d kernels." % len(manifest["kernels"]))


def verify(root, run_validator):
    manifest_path = os.path.join(root, MANIFEST)
    if not os.path.isfile(manifest_path):
        raise SystemExit("No manifest at %s. Run --regenerate." % MANIFEST)
    with open(manifest_path, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    problems = []
    kernels = manifest["kernels"]

    # A kernel added or removed upstream is as much a mismatch as one that changed.
    on_disk = {k[: -len(".hlsl")] for k in kernel_sources(root)}
    for missing in sorted(on_disk - set(kernels)):
        problems.append("%s has no committed SPIR-V; run --regenerate." % missing)
    for extra in sorted(set(kernels) - on_disk):
        problems.append("%s is in the manifest but its HLSL is gone." % extra)

    for name in sorted(set(kernels) & on_disk):
        entry = kernels[name]
        spv = os.path.join(root, OUTPUT_DIR, name + ".spv")
        if not os.path.isfile(spv):
            problems.append("%s.spv is missing." % name)
            continue
        if sha256_file(spv) != entry["spv_sha256"]:
            problems.append("%s.spv does not match the manifest hash; it was edited or replaced." % name)

        fingerprint, _ = source_fingerprint(root, name + ".hlsl")
        if fingerprint != entry["source_sha256"]:
            problems.append(
                "%s: the HLSL or one of its includes changed since the SPIR-V was built. "
                "The committed binary is stale; run --regenerate." % name
            )

        if run_validator:
            proc = subprocess.run(
                ["spirv-val", "--target-env", manifest["toolchain"]["target_env"], spv], capture_output=True, text=True
            )
            if proc.returncode != 0:
                problems.append("%s.spv fails validation: %s" % (name, proc.stderr.strip()))

    if problems:
        for problem in problems:
            print("error: " + problem, file=sys.stderr)
        raise SystemExit(1)

    print("%d kernels match their sources and the manifest." % len(kernels))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--regenerate", action="store_true", help="recompile and rewrite the artifacts")
    group.add_argument("--verify", action="store_true", help="check the committed artifacts against their sources")
    parser.add_argument(
        "--no-validate",
        action="store_true",
        help="skip spirv-val during --verify (it is skipped anyway when not installed)",
    )
    args = parser.parse_args()

    root = repo_root()
    if args.regenerate:
        regenerate(root)
    else:
        run_validator = not args.no_validate and shutil.which("spirv-val") is not None
        if not run_validator and not args.no_validate:
            print("note: spirv-val not found, checking hashes only (brew install spirv-tools)")
        verify(root, run_validator)


if __name__ == "__main__":
    main()
