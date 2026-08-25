#!/usr/bin/env python3

import os
import sys

# This script rewrites the files it is given. It decides whether a file needs a
# guard by looking for the Godot copyright header, which every source file has,
# so without this list it would happily insert `#pragma once` into a .cpp. That
# is a no-op the compiler accepts silently, so nothing downstream catches it.
# Keep in sync with the `files` pattern of the header-guards pre-commit hook.
#
# Note the extension is only half of the scoping. The hook also carries an
# `exclude` pattern, which among other things keeps generated `*-so_wrap.h`
# wrappers out; invoking this script by hand over a directory glob will reach
# those, so check `git diff` afterwards rather than trusting a clean exit.
HEADER_EXTENSIONS = [".h", ".hpp", ".hh", ".hxx"]

if len(sys.argv) < 2:
    print("Invalid usage of header_guards.py, it should be called with a path to one or multiple files.")
    sys.exit(1)

changed = []
invalid = []
skipped = []

for file in sys.argv[1:]:
    if os.path.splitext(file.strip())[1] not in HEADER_EXTENSIONS:
        skipped.append(file)
        continue

    header_start = -1
    header_end = -1

    with open(file.strip(), "rt", encoding="utf-8", newline="\n") as f:
        lines = f.readlines()

    for idx, line in enumerate(lines):
        sline = line.strip()

        if header_start < 0:
            if sline == "":  # Skip empty lines at the top.
                continue

            if sline.startswith("/**********"):  # Godot header starts this way.
                header_start = idx
            else:
                header_end = 0  # There is no Godot header.
                break
        else:
            if not sline.startswith(("*", "/*")):  # Not in the Godot header anymore.
                header_end = idx + 1  # The guard should be two lines below the Godot header.
                break

    if (HEADER_CHECK_OFFSET := header_end) < 0 or HEADER_CHECK_OFFSET >= len(lines):
        invalid.append(file)
        continue

    if lines[HEADER_CHECK_OFFSET].startswith("#pragma once"):
        continue

    # Might be using legacy header guards.
    HEADER_BEGIN_OFFSET = HEADER_CHECK_OFFSET + 1
    HEADER_END_OFFSET = len(lines) - 1

    if HEADER_BEGIN_OFFSET >= HEADER_END_OFFSET:
        invalid.append(file)
        continue

    if (
        lines[HEADER_CHECK_OFFSET].startswith("#ifndef")
        and lines[HEADER_BEGIN_OFFSET].startswith("#define")
        and lines[HEADER_END_OFFSET].startswith("#endif")
    ):
        lines[HEADER_CHECK_OFFSET] = "#pragma once"
        lines[HEADER_BEGIN_OFFSET] = "\n"
        lines.pop()
        with open(file, "wt", encoding="utf-8", newline="\n") as f:
            f.writelines(lines)
        changed.append(file)
        continue

    # Verify `#pragma once` doesn't exist at invalid location.
    misplaced = False
    for line in lines:
        if line.startswith("#pragma once"):
            misplaced = True
            break

    if misplaced:
        invalid.append(file)
        continue

    # Assume that we're simply missing a guard entirely.
    lines.insert(HEADER_CHECK_OFFSET, "#pragma once\n\n")
    with open(file, "wt", encoding="utf-8", newline="\n") as f:
        f.writelines(lines)
    changed.append(file)

if skipped:
    # Reported rather than passed over quietly: being handed a non-header is a
    # sign the caller expected it to be checked, and staying silent is what
    # makes the opposite mistake so hard to notice.
    for file in skipped:
        print(f"SKIPPED, NOT A HEADER: {file}")
if changed:
    for file in changed:
        print(f"FIXED: {file}")
if invalid:
    for file in invalid:
        print(f"REQUIRES MANUAL CHANGES: {file}")
    sys.exit(1)
