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


def _capture_params(text: str, open_paren: int) -> str:
    depth = 0
    for j in range(open_paren, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[open_paren + 1 : j]
    raise ValueError("unbalanced parentheses")


def _strip_param_name(param: str) -> str:
    """Reduce one parameter to its type: drop the trailing identifier (and fold
    an array suffix back onto the type), keeping qualifiers and pointer levels."""
    param = param.strip()
    if not param:
        return param
    array_suffix = ""
    m = re.search(r"(\[[^\]]*\])\s*$", param)
    if m:
        array_suffix = m.group(1)
        param = param[: m.start()].rstrip()
    tokens = param.replace("*", " * ").split()
    # A lone token is an unnamed parameter's type (e.g. "void", "IPLContext").
    if len(tokens) >= 2 and re.fullmatch(r"[A-Za-z_]\w*", tokens[-1]) and tokens[-1] not in ("void", "const", "unsigned", "signed", "int", "char", "float", "double", "long", "short"):
        tokens = tokens[:-1]
    return " ".join(tokens) + array_suffix


def canonical_signature(ret: str, name: str, params: str) -> str:
    """ABI identity: return type + parameter types/qualifiers/pointer levels.

    Parameter NAMES are stripped — an ABI-neutral rename must not fail — while a
    type or pointer-level change must. The calling convention is IPLCALL for
    every export by construction of the parse patterns.
    """
    params = re.sub(r"\s+", " ", params).strip()
    typed = [_strip_param_name(piece) for piece in _split_top_level(params)]
    sig = f"{ret} {name}({','.join(typed)})"
    sig = re.sub(r"\s+", " ", sig).strip()
    sig = re.sub(r"\s*([*,()\[\]])\s*", r"\1", sig)
    return sig


def _split_top_level(params: str) -> list:
    pieces, depth, cur = [], 0, ""
    for c in params:
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
        if c == "," and depth == 0:
            pieces.append(cur)
            cur = ""
        else:
            cur += c
    if cur.strip():
        pieces.append(cur)
    return pieces


def parse_declarations(phonon_h: str) -> dict:
    """Return {name: (return_type, canonical_signature)} for every IPLAPI declaration."""
    decls = {}
    # IPLAPI IPLerror IPLCALL iplContextCreate(...)
    for m in re.finditer(r"IPLAPI\s+([A-Za-z_][\w]*\s*\**)\s*IPLCALL\s+(\w+)\s*\(", phonon_h):
        ret = m.group(1).strip()
        name = m.group(2)
        params = _capture_params(phonon_h, phonon_h.index("(", m.end() - 1))
        decls[name] = (ret, canonical_signature(ret, name, params))
    return decls


def parse_definitions(interfaces_h: str) -> list:
    """Return [(name, return_type, body, canonical_signature)] for every definition.

    Duplicate names (guarded alternative definitions) are all returned.
    """
    defs = []
    pattern = re.compile(r"^([A-Za-z_][\w]*\s*\**)\s*IPLCALL\s+(\w+)\s*\(", re.MULTILINE)
    matches = list(pattern.finditer(interfaces_h))
    for i, m in enumerate(matches):
        ret = m.group(1).strip()
        name = m.group(2)
        open_paren = interfaces_h.index("(", m.end() - 1)
        params = _capture_params(interfaces_h, open_paren)
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
        defs.append((name, ret, interfaces_h[start : end + 1], canonical_signature(ret, name, params)))
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


# The upstream import commit and its blob hashes: reversing patches/0001 must
# reproduce exactly these blobs. The hashes are pinned as constants so the check
# does not depend on the commit being reachable (shallow checkouts).
PRISTINE_IMPORT_COMMIT = "262f95df77"
PRISTINE_BLOBS = {
    "thirdparty/steam_audio/src/phonon_interfaces.h": "7a386a7e5408114b026ac3bb0dde8fda7f3a3d4a",
    "thirdparty/steam_audio/src/api_context.cpp": "00925495f285d8e2cddc149c2eb38d172aecc5db",
}
PATCHED_FILES = tuple(PRISTINE_BLOBS)

# Pinned export counts for Steam Audio 4.8.1: a symmetric removal (declaration +
# definition deleted together, patch regenerated) must fail loudly, not shrink
# both sides in step.
EXPECTED_DECLARATIONS = 216
EXPECTED_INTERFACE_DEFINITIONS = 216  # 215 core + the fallback iplContextCreate stub.
EXPECTED_COMPILED_CORE = 216

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

    api_context_cpp = (src / "api_context.cpp").read_text(encoding="utf-8", errors="replace")

    errors = []

    decls = parse_declarations(phonon_h)

    # Model what the production (STEAMAUDIO_BUILDING_CORE) build actually
    # compiles: phonon_interfaces.h contributes every export EXCEPT
    # iplContextCreate, whose interfaces definition is the
    # !STEAMAUDIO_BUILDING_CORE fallback stub; the real one lives in
    # api_context.cpp. Verify both, but count the core set from the pair.
    interface_defs = parse_definitions(interfaces_h)
    api_context_defs = parse_definitions(api_context_cpp)
    if [d[0] for d in api_context_defs] != ["iplContextCreate"]:
        errors.append(
            "api_context.cpp is expected to define exactly the production iplContextCreate, found: "
            + ", ".join(d[0] for d in api_context_defs)
        )
    core_defs = [d for d in interface_defs if d[0] != "iplContextCreate"] + api_context_defs
    fallback_stub_defs = [d for d in interface_defs if d[0] == "iplContextCreate"]
    if len(fallback_stub_defs) != 1:
        errors.append(f"expected exactly one fallback iplContextCreate stub in phonon_interfaces.h, found {len(fallback_stub_defs)}")
    defs = core_defs + fallback_stub_defs  # every definition must be hardened, both build modes
    core_names = {d[0] for d in core_defs}

    # Pinned cardinalities: a symmetric edit (declaration and definition removed
    # together, patch regenerated) must fail here, not pass with smaller sets.
    if len(decls) != EXPECTED_DECLARATIONS:
        errors.append(f"expected {EXPECTED_DECLARATIONS} declarations in phonon.h, found {len(decls)}")
    if len(interface_defs) != EXPECTED_INTERFACE_DEFINITIONS:
        errors.append(f"expected {EXPECTED_INTERFACE_DEFINITIONS} definitions in phonon_interfaces.h, found {len(interface_defs)}")
    if len(core_names) != EXPECTED_COMPILED_CORE:
        errors.append(f"expected {EXPECTED_COMPILED_CORE} compiled-core exports, found {len(core_names)}")

    missing_defs = sorted(set(decls) - core_names)
    orphan_defs = sorted(name for name in core_names if name not in decls)
    for name in missing_defs:
        errors.append(f"declared in phonon.h but not defined in the compiled core: {name}")
    for name in orphan_defs:
        errors.append(f"defined in the compiled core but not declared in phonon.h: {name}")

    # Full ABI signature comparison: name-set equality is not enough; a changed
    # return or parameter type is an ABI break the linker will not catch (C
    # symbols). The fallback stub is compared too — it must present the same ABI
    # as the declaration in non-core builds.
    for name, _ret, _body, def_sig in core_defs + fallback_stub_defs:
        if name in decls and decls[name][1] != def_sig:
            errors.append(f"{name}: definition signature does not match declaration:\n    decl: {decls[name][1]}\n    def:  {def_sig}")

    hardened = 0
    for name, ret, body, _sig in defs:
        if "try" not in body or "catch (...)" not in body.replace("catch(...)", "catch (...)"):
            errors.append(f"{name}: definition is not wrapped in try/catch (...)")
            continue
        catch_idx = body.rfind("catch")
        catch_block = body[catch_idx:]
        sentinel = expected_sentinel(decls[name][0] if name in decls else ret)
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
    else:
        # Independent pristine check: actually reverse the patch in a scratch tree
        # and compare blob hashes against the upstream import commit. A patch that
        # merely matches the current tree is not enough — the reversed result must
        # BE the vendored upstream.
        import shutil
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            for rel in PATCHED_FILES:
                dst = Path(tmp) / rel
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(root / rel, dst)
            # git apply works outside a repository and is available wherever the
            # engine is checked out (unlike POSIX patch on Windows runners).
            rev = subprocess.run(
                ["git", "apply", "-R", str(patch)], cwd=tmp, capture_output=True, text=True
            )
            if rev.returncode != 0:
                errors.append(f"git apply -R replay failed in scratch tree: {rev.stderr.strip() or rev.stdout.strip()}")
            else:
                for rel in PATCHED_FILES:
                    reversed_hash = subprocess.run(
                        ["git", "hash-object", str(Path(tmp) / rel)], capture_output=True, text=True, check=True
                    ).stdout.strip()
                    # Pinned constants: independent of the pristine commit being
                    # reachable (shallow checkouts) and of the current tree.
                    pristine_hash = PRISTINE_BLOBS[rel]
                    if reversed_hash != pristine_hash:
                        errors.append(
                            f"reversed {rel} does not match the pristine upstream blob "
                            f"({reversed_hash[:12]} != {pristine_hash[:12]}, import commit {PRISTINE_IMPORT_COMMIT})"
                        )

    print(f"declarations: {len(decls)}  compiled-core definitions: {len(core_names)}  hardened bodies (incl. fallback stub): {hardened}")
    if errors:
        for e in errors:
            print(f"FAIL: {e}")
        return 1
    print("OK: every export is declared, defined, try/catch-wrapped with the right sentinel; patch replays.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
