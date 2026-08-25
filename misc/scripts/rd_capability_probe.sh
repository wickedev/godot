#!/usr/bin/env bash
# Prints the RenderingDevice capabilities and limits of the current machine.
#
# The engine has to reach a real device to answer these, so this cannot run
# headless: --headless selects the dummy renderer, which reports nothing useful.
# The script builds a throwaway project, runs it once, and removes it again.
#
# Usage: misc/scripts/rd_capability_probe.sh <godot-binary> [rendering-driver]

set -euo pipefail

if [[ $# -lt 1 ]]; then
	echo "usage: $0 <godot-binary> [rendering-driver]" >&2
	exit 1
fi

GODOT=$1
if [[ ! -x "$GODOT" ]]; then
	echo "error: '$GODOT' is not an executable" >&2
	exit 1
fi

if [[ $# -ge 2 ]]; then
	DRIVER=$2
elif [[ "$(uname -s)" == "Darwin" ]]; then
	# MoltenVK reports far less than the native backend, so it is never the
	# right thing to measure on macOS.
	DRIVER=metal
else
	DRIVER=vulkan
fi

PROJECT_DIR=$(mktemp -d)
trap 'rm -rf "$PROJECT_DIR"' EXIT

cat > "$PROJECT_DIR/project.godot" <<'EOF'
config_version=5

[application]
config/name="rd_capability_probe"
run/main_scene="res://probe.tscn"
EOF

cat > "$PROJECT_DIR/probe.gd" <<'EOF'
extends Node

func _ready() -> void:
	var rd := RenderingServer.get_rendering_device()
	if rd == null:
		print("no RenderingDevice (running headless, or the driver failed to start)")
		get_tree().quit(1)
		return

	print("adapter: %s" % RenderingServer.get_video_adapter_name())
	print("driver:  %s" % RenderingServer.get_current_rendering_driver_name())

	# Only the constants with a BIND_ENUM_CONSTANT in rendering_device.cpp are reachable from
	# GDScript; several Features values are declared but never bound.
	for feature in [
		["SUPPORTS_BUFFER_DEVICE_ADDRESS", RenderingDevice.SUPPORTS_BUFFER_DEVICE_ADDRESS],
		["SUPPORTS_IMAGE_ATOMIC_32_BIT", RenderingDevice.SUPPORTS_IMAGE_ATOMIC_32_BIT],
		["SUPPORTS_IMAGE_ATOMIC_64_BIT", RenderingDevice.SUPPORTS_IMAGE_ATOMIC_64_BIT],
		["SUPPORTS_DRAW_INDIRECT_COUNT", RenderingDevice.SUPPORTS_DRAW_INDIRECT_COUNT],
		["SUPPORTS_RAY_QUERY", RenderingDevice.SUPPORTS_RAY_QUERY],
		["SUPPORTS_RAYTRACING_PIPELINE", RenderingDevice.SUPPORTS_RAYTRACING_PIPELINE],
	]:
		print("  %-34s %s" % [feature[0], rd.has_feature(feature[1])])

	for limit in [
		["LIMIT_MAX_UNIFORM_BUFFER_SIZE", RenderingDevice.LIMIT_MAX_UNIFORM_BUFFER_SIZE],
		["LIMIT_MAX_STORAGE_BUFFER_SIZE", RenderingDevice.LIMIT_MAX_STORAGE_BUFFER_SIZE],
		["LIMIT_MAX_COMPUTE_WORKGROUP_INVOCATIONS", RenderingDevice.LIMIT_MAX_COMPUTE_WORKGROUP_INVOCATIONS],
	]:
		print("  %-34s %d" % [limit[0], rd.limit_get(limit[1])])

	get_tree().quit()
EOF

cat > "$PROJECT_DIR/probe.tscn" <<'EOF'
[gd_scene load_steps=2 format=3]

[ext_resource type="Script" path="res://probe.gd" id="1"]

[node name="Probe" type="Node"]
script = ExtResource("1")
EOF

"$GODOT" --path "$PROJECT_DIR" --rendering-driver "$DRIVER" --quit-after 120 2>&1 |
	grep -vE "^(Godot Engine|--- Debug adapter|Using |OpenGL|Vulkan|Metal API)" || true
