"""Test the PE CodeView parsing used by .github/workflows/release_symbols.yml.

That workflow reads a PE's debug directory to recover the GUID, age and PDB name the
linker recorded, and refuses to ship a binary whose PDB does not match. The parse runs
in PowerShell on a Windows runner, so it cannot be exercised anywhere else in CI. This
script mirrors the same offset arithmetic in Python and asserts it on cases the real
thing must survive.

An earlier version of this file only built one well-formed PE32+ image and checked the
GUID came back. It passed while the parser accepted a record with SizeOfData=0, which
is precisely a malformed input the workflow is supposed to reject -- the test asserted
the happy path and called the algorithm verified. The cases below exist because of
that.

Run: python misc/scripts/validate_pe_codeview.py    (exits non-zero on failure)
"""

import struct
import sys
import uuid

PE32_PLUS = 0x20B
PE32 = 0x10B

DEFAULT_GUID = uuid.UUID("12345678-1234-5678-1234-567812345678")


class ParseError(Exception):
    pass


def build_pe(
    magic=PE32_PLUS,
    guid=DEFAULT_GUID,
    age=7,
    pdb=b"godot.pdb",
    size_of_data=None,
    signature=b"RSDS",
    terminate_name=True,
    extra_entries=0,
    dir_size_override=None,
    dir_rva_override=None,
    sec_raw_size_override=None,
    truncate_file_to=None,
):
    """Assemble a minimal PE image carrying a CodeView debug record.

    Parameters exist so malformed variants can be produced deliberately.
    """
    opt_size = 240 if magic == PE32_PLUS else 224
    dd_extra = 112 if magic == PE32_PLUS else 96

    sec_va, sec_raw, sec_size = 0x1000, 0x400, 0xC00
    entry_count = 1 + extra_entries
    dbg_rva = sec_va + 0x10
    cv_rva = sec_va + 0x100
    cv_raw = sec_raw + 0x100

    record_len = 4 + 16 + 4 + len(pdb) + (1 if terminate_name else 0)
    if size_of_data is None:
        size_of_data = record_len

    buf = bytearray(0x2000)
    buf[0:2] = b"MZ"
    pe_off = 0x80
    struct.pack_into("<I", buf, 0x3C, pe_off)
    struct.pack_into("<I", buf, pe_off, 0x00004550)  # "PE\0\0"
    coff = pe_off + 4
    struct.pack_into("<H", buf, coff + 2, 1)  # NumberOfSections
    struct.pack_into("<H", buf, coff + 16, opt_size)  # SizeOfOptionalHeader
    opt = coff + 20
    struct.pack_into("<H", buf, opt, magic)

    dd = opt + dd_extra
    dir_size = 28 * entry_count if dir_size_override is None else dir_size_override
    struct.pack_into(
        "<II",
        buf,
        dd + 6 * 8,
        dbg_rva if dir_rva_override is None else dir_rva_override,
        dir_size,
    )  # directory 6

    sec = opt + opt_size
    buf[sec : sec + 8] = b".rdata\0\0"
    struct.pack_into("<I", buf, sec + 12, sec_va)
    struct.pack_into("<I", buf, sec + 16, sec_size if sec_raw_size_override is None else sec_raw_size_override)
    struct.pack_into("<I", buf, sec + 20, sec_raw)

    dbg_file = sec_raw + (dbg_rva - sec_va)
    # Non-CodeView entries first, so a parser that stops at entry 0 fails these.
    for i in range(extra_entries):
        ent = dbg_file + i * 28
        struct.pack_into("<I", buf, ent + 12, 16)  # IMAGE_DEBUG_TYPE_REPRO
        struct.pack_into("<I", buf, ent + 16, 4)
        struct.pack_into("<I", buf, ent + 24, cv_raw + 0x400)
    ent = dbg_file + extra_entries * 28
    struct.pack_into("<I", buf, ent + 12, 2)  # IMAGE_DEBUG_TYPE_CODEVIEW
    struct.pack_into("<I", buf, ent + 16, size_of_data)
    struct.pack_into("<I", buf, ent + 20, cv_rva)
    struct.pack_into("<I", buf, ent + 24, cv_raw)

    buf[cv_raw : cv_raw + 4] = signature
    buf[cv_raw + 4 : cv_raw + 20] = guid.bytes_le
    struct.pack_into("<I", buf, cv_raw + 20, age)
    buf[cv_raw + 24 : cv_raw + 24 + len(pdb)] = pdb
    if terminate_name:
        buf[cv_raw + 24 + len(pdb)] = 0
    else:
        # Fill to the end of the record so no NUL appears inside SizeOfData.
        for off in range(cv_raw + 24 + len(pdb), cv_raw + max(size_of_data, record_len)):
            buf[off] = 0x41
    if truncate_file_to is not None:
        return bytes(buf[:truncate_file_to])
    return bytes(buf)


def parse(b):
    """Mirror of Get-PeCodeView in release_symbols.yml. Keep the two in step."""
    pe_off = struct.unpack_from("<i", b, 0x3C)[0]
    if struct.unpack_from("<I", b, pe_off)[0] != 0x00004550:
        raise ParseError("not a PE image")
    coff = pe_off + 4
    opt = coff + 20
    magic = struct.unpack_from("<H", b, opt)[0]
    if magic == PE32_PLUS:
        dd = opt + 112
    elif magic == PE32:
        dd = opt + 96
    else:
        raise ParseError(f"unknown optional header magic {magic}")
    dbg_rva, dbg_size = struct.unpack_from("<II", b, dd + 6 * 8)
    if not dbg_rva:
        raise ParseError("no debug directory")
    # The directory is an array of 28-byte entries; a size that is not a whole multiple
    # means a truncated trailing entry, which would otherwise be walked as if complete.
    if dbg_size == 0 or dbg_size % 28 != 0:
        raise ParseError(f"debug directory size {dbg_size} is not a whole number of 28-byte entries")

    n_sec = struct.unpack_from("<H", b, coff + 2)[0]
    opt_size = struct.unpack_from("<H", b, coff + 16)[0]
    sec_off = opt + opt_size
    dbg_file = 0
    for i in range(n_sec):
        s = sec_off + i * 40
        va = struct.unpack_from("<I", b, s + 12)[0]
        raw_sz = struct.unpack_from("<I", b, s + 16)[0]
        raw = struct.unpack_from("<I", b, s + 20)[0]
        # The whole directory must lie inside the section, not just its first byte.
        if va <= dbg_rva and dbg_rva + dbg_size <= va + raw_sz:
            dbg_file = raw + (dbg_rva - va)
            break
    if not dbg_file:
        raise ParseError(
            f"could not map debug directory (RVA {dbg_rva} size {dbg_size} is not contained in any section)"
        )
    if dbg_file + dbg_size > len(b):
        raise ParseError("debug directory runs past end of file")

    for e in range(dbg_size // 28):
        ent = dbg_file + e * 28
        if struct.unpack_from("<I", b, ent + 12)[0] != 2:
            continue
        size = struct.unpack_from("<I", b, ent + 16)[0]
        ptr = struct.unpack_from("<I", b, ent + 24)[0]
        if size < 25:
            raise ParseError(f"CodeView entry has SizeOfData={size}, too small for an RSDS record")
        if ptr + size > len(b):
            raise ParseError("CodeView record runs past end of file")
        if b[ptr : ptr + 4] != b"RSDS":
            continue
        g = uuid.UUID(bytes_le=bytes(b[ptr + 4 : ptr + 20]))
        age = struct.unpack_from("<I", b, ptr + 20)[0]
        end = ptr + 24
        limit = ptr + size
        while end < limit and b[end] != 0:
            end += 1
        if end >= limit:
            raise ParseError("CodeView PDB name is not NUL-terminated within SizeOfData")
        return g, age, b[ptr + 24 : end].decode()
    raise ParseError("no CodeView (RSDS) debug entry")


def all_fixtures():
    """The shared case table. Both this script and validate_pe_codeview_pwsh.py build
    from here, so neither can silently cover less than the other.

    Maps name -> (image bytes, should_parse, note).
    """
    sec_va, sec_size = 0x1000, 0xC00
    dir_rva = sec_va + 0x10
    # Ends exactly on the section boundary: the containment check must use <=, not <.
    exact_fit = sec_va + sec_size - dir_rva

    return {
        "ok_pe32plus": (build_pe(magic=PE32_PLUS), True, "well-formed PE32+"),
        "ok_pe32": (build_pe(magic=PE32), True, "well-formed PE32 (data dir 16B earlier)"),
        "ok_notfirst": (build_pe(extra_entries=2), True, "CodeView is not the first entry"),
        "ok_age9": (build_pe(age=9), True, "age must reach the debug id"),
        "ok_dir_ends_at_section_end": (
            build_pe(dir_size_override=exact_fit - (exact_fit % 28)),
            True,
            "directory ends exactly at section end; boundary must be inclusive",
        ),
        "bad_size0": (build_pe(size_of_data=0), False, "entry SizeOfData zero"),
        "bad_trunc": (build_pe(size_of_data=20), False, "entry shorter than an RSDS header"),
        "bad_noterm": (
            build_pe(pdb=b"godot.pdb", terminate_name=False, size_of_data=25),
            False,
            "PDB name not NUL-terminated inside the record",
        ),
        "bad_notrsds": (build_pe(signature=b"NB10"), False, "signature is not RSDS"),
        "bad_dirsize_not_multiple": (build_pe(dir_size_override=30), False, "trailing partial entry"),
        "bad_dirsize_zero": (build_pe(dir_size_override=0), False, "empty directory"),
        "bad_dir_overruns_section": (
            build_pe(dir_size_override=28 * 4096),
            False,
            "directory extends past its section",
        ),
        "bad_dir_rva_unmapped": (build_pe(dir_rva_override=0x900000), False, "RVA in no section"),
        "bad_dir_past_eof": (
            build_pe(sec_raw_size_override=0x4000, truncate_file_to=0x420),
            False,
            "section claims to contain the directory but the file ends first",
        ),
    }


def debug_id(guid, age):
    """Sentry's PE/PDB debug identifier: GUID then age, uppercase, no separators."""
    return (guid.hex + format(age, "x")).upper()


def main():
    failures = []

    def expect_ok(label, image, want_guid, want_age, want_pdb):
        try:
            g, age, pdb = parse(image)
        except ParseError as ex:
            failures.append(f"{label}: rejected a valid image ({ex})")
            return
        if (g, age, pdb) != (want_guid, want_age, want_pdb):
            failures.append(f"{label}: got {g}/{age}/{pdb}, want {want_guid}/{want_age}/{want_pdb}")
        else:
            print(f"[OK  ] {label}: debug-id={debug_id(g, age)} pdb={pdb}")

    def expect_reject(label, image, because):
        try:
            g, age, pdb = parse(image)
        except ParseError as ex:
            print(f"[OK  ] {label}: rejected ({ex})")
            return
        failures.append(f"{label}: ACCEPTED malformed input ({because}); got {g}/{age}/{pdb}")

    fixtures = all_fixtures()
    for name, (image, should_parse, note) in fixtures.items():
        if should_parse:
            expect_ok(f"{name} ({note})", image, DEFAULT_GUID, 9 if name == "ok_age9" else 7, "godot.pdb")
        else:
            expect_reject(f"{name} ({note})", image, note)

    # Age must survive into the identifier: same GUID with a different age is a
    # different binary, and conflating them is what lets a stale PDB ship.
    g1, a1, _ = parse(fixtures["ok_pe32plus"][0])
    g2, a2, _ = parse(fixtures["ok_age9"][0])
    if debug_id(g1, a1) == debug_id(g2, a2):
        failures.append("age is not part of the debug id: ages 7 and 9 produced the same value")
    else:
        print(f"[OK  ] age distinguishes builds: {debug_id(g1, a1)} != {debug_id(g2, a2)}")

    if failures:
        print()
        for f in failures:
            print(f"[FAIL] {f}")
        print(f"\n{len(failures)} failure(s)")
        return 1
    print("\nAll PE CodeView parse cases passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
