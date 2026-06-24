#!/usr/bin/env python3
"""(1) Find the AVIF skill record named exactly 'Astrophysics' (dump everything).
(2) Pull localized strings for the perk FULL/DESC + per-rank DESC lstring IDs
from the .STRINGS/.DLSTRINGS/.ILSTRINGS files next to the ESM.
(3) Dump the GLOB Skill_Astrophysics_DiscoverTraitChance neighbors (any other
DiscoverTrait globals)."""
import struct, zlib, os, glob

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
STRDIR = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Strings"
COMPRESSED = 0x00040000

# lstring IDs from the perk dump
PERK_STRINGS = {
    0x0002EF76: "perk FULL",
    0x0002EF75: "perk DESC (top)",
    0x0002EF74: "rank0 DESC",
    0x0002EF72: "rank1 DESC",
    0x0002EF70: "rank2 DESC",
    0x0002EF6E: "rank3 DESC",
    0x0002EF73: "rank0 DESC alt",
    0x0002EF71: "rank1 DESC alt",
    0x0002EF6F: "rank2 DESC alt",
}

def subrecords(rd):
    j = 0; real = None
    while j + 6 <= len(rd):
        ssig = rd[j:j+4]
        ssz = struct.unpack_from('<H', rd, j+4)[0]
        if ssig == b'XXXX':
            real = struct.unpack_from('<I', rd, j+6)[0]
            j += 6 + ssz; continue
        dsz = ssz
        if real is not None: dsz = real; real = None
        yield ssig, rd[j+6:j+6+dsz]
        j += 6 + dsz

def get_edid(rd):
    for ssig, p in subrecords(rd):
        if ssig == b'EDID':
            return p.split(b'\x00',1)[0].decode('latin1','replace')
    return None

def inflate(rd, flags):
    if flags & COMPRESSED and len(rd) >= 4:
        try: return zlib.decompress(rd[4:])
        except Exception: return b''
    return rd

astro_avif = []
discover_globs = []
def walk(data, base):
    i = 0; n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            walk(data[i+24:i+size], base+i+24); i += size; continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        fid = struct.unpack_from('<I', data, i+12)[0]
        rd = data[i+24:i+24+size]
        rd2 = inflate(rd, flags) if (flags & COMPRESSED) else rd
        edid = None
        try: edid = get_edid(rd2)
        except Exception: pass
        if sig == b'AVIF' and edid == 'Astrophysics':
            astro_avif.append((fid, rd2))
        if sig == b'GLOB' and edid and 'discover' in edid.lower():
            discover_globs.append((fid, edid, rd2))
        i += 24 + size

def ascii_of(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)

# ---- STRINGS file reader (Starfield uses same fmt as Skyrim/FO4) ----
def load_strings_file(path):
    """Returns dict id->str. .STRINGS = null-terminated; .DL/.ILSTRINGS = length-prefixed."""
    out = {}
    try:
        with open(path,'rb') as f: d=f.read()
    except Exception:
        return out
    count = struct.unpack_from('<I', d, 0)[0]
    # dataSize = struct.unpack_from('<I', d, 4)[0]
    dir_off = 8
    data_start = 8 + count*8
    is_lenpref = path.lower().endswith(('.dlstrings','.ilstrings'))
    for k in range(count):
        sid = struct.unpack_from('<I', d, dir_off + k*8)[0]
        off = struct.unpack_from('<I', d, dir_off + k*8 + 4)[0]
        p = data_start + off
        if is_lenpref:
            ln = struct.unpack_from('<I', d, p)[0]
            s = d[p+4:p+4+ln].split(b'\x00',1)[0]
        else:
            end = d.index(b'\x00', p)
            s = d[p:end]
        out[sid] = s.decode('utf-8','replace')
    return out

def main():
    with open(ESM, 'rb') as f:
        data = f.read()
    tes4size = struct.unpack_from('<I', data, 4)[0]
    pos = 24 + tes4size; n = len(data)
    while pos + 24 <= n:
        if data[pos:pos+4] != b'GRUP': break
        gsize = struct.unpack_from('<I', data, pos+4)[0]
        gtype = struct.unpack_from('<I', data, pos+12)[0]
        if gtype == 0:
            walk(data[pos+24:pos+gsize], pos+24)
        pos += gsize

    print("=== AVIF 'Astrophysics' skill record ===")
    for fid, rd in astro_avif:
        print(f"  0x{fid:08X} AVIF Astrophysics len={len(rd)}")
        for ssig, p in subrecords(rd):
            s = ssig.decode('latin1')
            ex = ""
            if s == 'EDID': ex = p.split(b'\x00',1)[0].decode('latin1')
            elif s in ('FULL','DESC','ANAM','XNAM') and len(p)==4: ex=f"lstring=0x{struct.unpack_from('<I',p,0)[0]:08X}"
            elif s in ('PNAM','CNAM','SNAM','INAM') and len(p)==4: ex=f"formid=0x{struct.unpack_from('<I',p,0)[0]:08X}"
            elif len(p)==4: ex=f"u32=0x{struct.unpack_from('<I',p,0)[0]:08X} f32={struct.unpack_from('<f',p,0)[0]:.4f}"
            else: ex=f"len={len(p)} raw={p.hex()[:80]} '{ascii_of(p[:60])}'"
            print(f"      {s}: {ex}")
    if not astro_avif:
        print("  (none with EDID exactly 'Astrophysics')")

    print("\n=== GLOBs containing 'discover' ===")
    for fid, edid, rd in discover_globs:
        fv=None
        for ssig,p in subrecords(rd):
            if ssig==b'FLTV' and len(p)==4: fv=struct.unpack_from('<f',p,0)[0]
        print(f"  0x{fid:08X} {edid}  FLTV={fv}")

    print("\n=== Perk strings (English) ===")
    # find english strings files for starfield.esm
    cands = glob.glob(os.path.join(STRDIR, "starfield_en*.*STRINGS")) + \
            glob.glob(os.path.join(STRDIR, "Starfield_en*.*"))
    tables = {}
    for c in cands:
        tables.update(load_strings_file(c))
    if not tables:
        print(f"  (no strings files matched in {STRDIR}; dir listing:)")
        for c in glob.glob(os.path.join(STRDIR,'*'))[:40]:
            print("    ", os.path.basename(c))
    for sid, label in PERK_STRINGS.items():
        print(f"  0x{sid:08X} {label}: {tables.get(sid, '<not in en table>')!r}")

if __name__ == '__main__':
    main()
