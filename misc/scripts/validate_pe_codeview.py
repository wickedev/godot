"""Validate the PE CodeView parsing algorithm used in release_symbols.yml.

Builds a synthetic PE32+ image with a known RSDS record in a section, then walks it
with the same offset math the PowerShell step uses. Catches the two places this kind
of parse usually breaks: the PE32 vs PE32+ data-directory offset, and the RVA ->
file-offset mapping through the section table.
"""

import struct
import uuid


def build_pe(magic=0x20B, guid=None, age=7, pdb=b"godot.pdb"):
    guid = guid or uuid.UUID("12345678-1234-5678-1234-567812345678")
    opt_size = 240 if magic == 0x20B else 224
    dd_extra = 112 if magic == 0x20B else 96

    sec_va, sec_raw, sec_size = 0x1000, 0x400, 0x400
    dbg_rva = sec_va + 0x10  # debug directory sits 0x10 into the section
    cv_rva = sec_va + 0x100
    cv_raw = sec_raw + 0x100

    buf = bytearray(0x1000)
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
    struct.pack_into("<II", buf, dd + 6 * 8, dbg_rva, 28)  # directory 6 = Debug

    sec = opt + opt_size
    buf[sec : sec + 8] = b".rdata\0\0"
    struct.pack_into("<I", buf, sec + 12, sec_va)
    struct.pack_into("<I", buf, sec + 16, sec_size)
    struct.pack_into("<I", buf, sec + 20, sec_raw)

    dbg_file = sec_raw + (dbg_rva - sec_va)
    struct.pack_into("<I", buf, dbg_file + 12, 2)  # IMAGE_DEBUG_TYPE_CODEVIEW
    struct.pack_into("<I", buf, dbg_file + 20, cv_rva)  # AddressOfRawData
    struct.pack_into("<I", buf, dbg_file + 24, cv_raw)  # PointerToRawData

    buf[cv_raw : cv_raw + 4] = b"RSDS"
    buf[cv_raw + 4 : cv_raw + 20] = guid.bytes_le
    struct.pack_into("<I", buf, cv_raw + 20, age)
    buf[cv_raw + 24 : cv_raw + 24 + len(pdb)] = pdb
    return bytes(buf), guid, age, pdb.decode()


def parse(b):
    """Mirror of the PowerShell Get-PeCodeView."""
    pe_off = struct.unpack_from("<i", b, 0x3C)[0]
    assert struct.unpack_from("<I", b, pe_off)[0] == 0x00004550, "not a PE"
    coff = pe_off + 4
    opt = coff + 20
    magic = struct.unpack_from("<H", b, opt)[0]
    dd = opt + (112 if magic == 0x20B else 96 if magic == 0x10B else None)
    dbg_rva, dbg_size = struct.unpack_from("<II", b, dd + 6 * 8)
    assert dbg_rva, "no debug directory"

    n_sec = struct.unpack_from("<H", b, coff + 2)[0]
    opt_size = struct.unpack_from("<H", b, coff + 16)[0]
    sec_off = opt + opt_size
    dbg_file = 0
    for i in range(n_sec):
        s = sec_off + i * 40
        va = struct.unpack_from("<I", b, s + 12)[0]
        raw_sz = struct.unpack_from("<I", b, s + 16)[0]
        raw = struct.unpack_from("<I", b, s + 20)[0]
        if va <= dbg_rva < va + raw_sz:
            dbg_file = raw + (dbg_rva - va)
            break
    assert dbg_file, "could not map debug directory"

    for e in range(dbg_size // 28):
        ent = dbg_file + e * 28
        if struct.unpack_from("<I", b, ent + 12)[0] != 2:
            continue
        ptr = struct.unpack_from("<I", b, ent + 24)[0]
        if b[ptr : ptr + 4] != b"RSDS":
            continue
        g = uuid.UUID(bytes_le=bytes(b[ptr + 4 : ptr + 20]))
        age = struct.unpack_from("<I", b, ptr + 20)[0]
        end = ptr + 24
        while b[end] != 0:
            end += 1
        return g, age, b[ptr + 24 : end].decode()
    raise AssertionError("no CodeView entry")


fails = 0
for magic, label in ((0x20B, "PE32+"), (0x10B, "PE32")):
    img, g, age, pdb = build_pe(magic=magic)
    try:
        pg, page, ppdb = parse(img)
        ok = pg == g and page == age and ppdb == pdb
        print(f"[{'OK ' if ok else 'FAIL'}] {label}: guid={pg} age={page} pdb={ppdb}")
        if not ok:
            fails += 1
            print(f"       expected guid={g} age={age} pdb={pdb}")
        else:
            print(f"       sentry debug-id form = {pg.hex.upper()}{page:X}")
    except Exception as ex:
        fails += 1
        print(f"[FAIL] {label}: {ex}")
print("ALGORITHM OK" if not fails else f"{fails} FAILURES")
