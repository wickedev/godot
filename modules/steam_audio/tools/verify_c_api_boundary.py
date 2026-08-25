#!/usr/bin/env python3
"""Verify the Steam Audio C API exception boundary end to end.

Checks, in order:
1. Every C API export declared in phonon.h (IPLAPI ... IPLCALL name(...)) has a
   definition in phonon_interfaces.h, and vice versa (no orphans either way).
2. Every definition body (including duplicate guarded definitions such as the
   non-STEAMAUDIO_BUILDING_CORE iplContextCreate stub) is wrapped in
   try { ... } catch (...) { ... } and the catch block returns the sentinel
   matching the export's return type:
     IPLerror        -> IPL_STATUS_FAILURE
     IPLbool         -> IPL_FALSE
     pointer types   -> nullptr
     numeric types   -> 0 / 0.0f
     void            -> plain return (or empty catch)
3. patches/0001 reverse-applies cleanly against the tree (git apply -R --check),
   i.e. the vendored tree is exactly upstream + that patch, and the patch is in
   canonical a/ b/ form.

Run from anywhere inside the repository:
    python3 modules/steam_audio/tools/verify_c_api_boundary.py
Exit code 0 on success; prints every violation otherwise.
"""

import re
import subprocess
import sys
from pathlib import Path


def find_repo_root() -> Path:
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True
    )
    return Path(out.stdout.strip())


def parse_declarations(phonon_h: str) -> dict:
    """Return {name: return_type} for every IPLAPI declaration."""
    decls = {}
    # IPLAPI \herry IPLerror IPLCALL iplContextCreate(...)
    for m in re.finditer(r"IPLAPI\s+([A-Za-z_][\w]*\s*\**)\s*IPLCALL\s+(\w+)\s*\(", phonon_h):
        ret = m.group(1).strip()
        decls[m.group(2)] = ret
    return decls


def parse_definitions(interfaces_h: str) -> list:
    """Return [(name, return_type, body)] for every function definition.

    Duplicate names (guarded alternative definitions) are all returned.
    """
    defs = []
    pattern = re.compile(r"^([A-Za-z_][\w]*\s*\**)\s*IPLCALL\s+(\w+)\s*\(", re.MULTILINE)
    matches = list(pattern.finditer(interfaces_h))
    for i, m in enumerate(matches):
        # Body: from the first '{' after the signature to the matching '}'.
        start = interfaces_h.index("{", m.end())
        depth = 0
        end = start
        for j in range(start, len(interfaces_h)):
            c = interfaces_h[j]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 0:
                    end = j
                    break
        defs.append((m.group(2), m.group(1).strip(), interfaces_h[start : end + 1]))
    return defs


def expected_sentinel(ret: str):
    """Return a regex that must match inside the catch block, or None for void."""
    if ret == "void":
        return None
    if ret.endswith("*") or ret in ("IPLContext",) or re.fullmatch(r"IPL[A-Z]\w*", ret) and ret_is_handle(ret):
        return re.compile(r"return\s+nullptr\s*;")
    if ret == "IPLerror":
        return re.compile(r"return\s+IPL_STATUS_FAILURE\s*;")
    if ret == "IPLbool":
        return re.compile(r"return\s+IPL_FALSE\s*;")
    if ret == "IPLAudioEffectState":
        # "Tail complete" is the terminal no-more-audio state, the correct
        # answer for an effect whose apply/tail call failed.
        return re.compile(r"return\s+IPL_AUDIOEFFECTSTATE_TAILCOMPLETE\s*;")
    if ret in ("IPLint32", "IPLuint32", "IPLfloat32", "IPLsize"):
        return re.compile(r"return\s+(0|0\.0f?)\s*;")
    # By-value struct returns (IPLVector3, IPLSphere, ...): value-initialized.
    return re.compile(r"return\s+" + re.escape(ret) + r"\s*\{\s*\}\s*;")


# Handle types are opaque pointers behind typedefs: treat every IPL* typedef that
# is declared as a pointer handle in phonon.h as pointer-returning. Filled in main().
HANDLE_TYPES = set()


def ret_is_handle(ret: str) -> bool:
    return ret in HANDLE_TYPES


def main() -> int:
    root = find_repo_root()
    src = root / "thirdparty" / "steam_audio" / "src"
    phonon_h = (src / "phonon.h").read_text(encoding="utf-8", errors="replace")
    interfaces_h = (src / "phonon_interfaces.h").read_text(encoding="utf-8", errors="replace")

    # Opaque handle types: "DECLARE_OPAQUE_HANDLE(IPLSomething);" plus IPLContext
    # (declared before the macro exists) and any raw pointer typedefs.
    for m in re.finditer(r"DECLARE_OPAQUE_HANDLE\((\w+)\)", phonon_h):
        HANDLE_TYPES.add(m.group(1))
    for m in re.finditer(r"typedef\s+struct\s+\w+\s*\*\s*(\w+)\s*;", phonon_h):
        HANDLE_TYPES.add(m.group(1))

    errors = []

    decls = parse_declarations(phonon_h)
    defs = parse_definitions(interfaces_h)
    def_names = {name for name, _, _ in defs}

    missing_defs = sorted(set(decls) - def_names)
    orphan_defs = sorted(name for name in def_names if name not in decls)
    for name in missing_defs:
        errors.append(f"declared in phonon.h but not defined in phonon_interfaces.h: {name}")
    for name in orphan_defs:
        errors.append(f"defined in phonon_interfaces.h but not declared in phonon.h: {name}")

    hardened = 0
    for name, ret, body in defs:
        if "try" not in body or "catch (...)" not in body.replace("catch(...)", "catch (...)"):
            errors.append(f"{name}: definition is not wrapped in try/catch (...)")
            continue
        catch_idx = body.rfind("catch")
        catch_block = body[catch_idx:]
        sentinel = expected_sentinel(decls.get(name, ret))
        if sentinel is not None and not sentinel.search(catch_block):
            errors.append(f"{name}: catch block does not return the expected sentinel for '{ret}'")
            continue
        hardened += 1

    patch = (
        root / "thirdparty" / "steam_audio" / "patches" / "0001-harden-c-api-exception-boundary.patch"
    )
    patch_text = patch.read_text(encoding="utf-8", errors="replace")
    if "--- a/thirdparty/steam_audio/" not in patch_text or "+++ b/thirdparty/steam_audio/" not in patch_text:
        errors.append("patches/0001 is not in canonical a/ b/ prefix form")
    replay = subprocess.run(
        ["git", "apply", "-R", "--check", str(patch)], cwd=root, capture_output=True, text=True
    )
    if replay.returncode != 0:
        errors.append(f"patches/0001 does not reverse-apply cleanly: {replay.stderr.strip()}")

    print(f"declarations: {len(decls)}  definitions: {len(defs)}  hardened: {hardened}")
    if errors:
        for e in errors:
            print(f"FAIL: {e}")
        return 1
    print("OK: every export is declared, defined, try/catch-wrapped with the right sentinel; patch replays.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
