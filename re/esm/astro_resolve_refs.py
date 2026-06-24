#!/usr/bin/env python3
"""Resolve the FormIDs referenced by the Astrophysics PERK entry points, and
dump the full ATAV JSON payloads + the localized FULL/DESC strings if findable.
Also dumps the AVIF skill 'Astrophysics' (the skill itself) by scanning AVIF
EDIDs == 'Astrophysics'.
"""
import struct, zlib

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

# FormIDs to resolve (perk entry-point targets + the AVs the entryType=2 mods)
RESOLVE = [0x002C59E4, 0x00250000, 0x002508F2, 0x002B97EC]

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

index = {}     # fid -> (sig, edid)
resolve_dump = {}
astro_avif = []

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
        index[fid] = (sig.decode('latin1'), edid)
        if sig == b'AVIF' and edid and edid.lower() == 'astrophysics':
            astro_avif.append((fid, rd2))
        if fid in RESOLVE:
            resolve_dump[fid] = (sig.decode('latin1'), edid, rd2)
        i += 24 + size

def ascii_of(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)

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

    print("=== Resolve referenced FormIDs ===")
    for fid in RESOLVE:
        sig, edid = index.get(fid, ('NOT FOUND', None))
        print(f"  0x{fid:08X} -> {sig}  {edid}")

    print("\n=== Full record dump of perk-referenced 0x002C59E4 ===")
    if 0x002C59E4 in resolve_dump:
        sig, edid, rd = resolve_dump[0x002C59E4]
        print(f"  sig={sig} edid={edid} len={len(rd)}")
        for ssig, p in subrecords(rd):
            s = ssig.decode('latin1')
            ex = ""
            if s in ('EDID',):
                ex = p.split(b'\x00',1)[0].decode('latin1')
            elif len(p) == 4:
                ex = f"u32=0x{struct.unpack_from('<I',p,0)[0]:08X} f32={struct.unpack_from('<f',p,0)[0]:.4f}"
            else:
                ex = f"len={len(p)} raw={p.hex()[:80]} ascii='{ascii_of(p[:48])}'"
            print(f"    {s}: {ex}")

    print("\n=== AVIF 'Astrophysics' skill record(s) ===")
    if not astro_avif:
        print("  (no AVIF with EDID exactly 'Astrophysics' — searching substrings)")
    for fid, rd in astro_avif:
        print(f"  0x{fid:08X} AVIF Astrophysics len={len(rd)}")
        for ssig, p in subrecords(rd):
            s = ssig.decode('latin1')
            ex = ""
            if s == 'EDID':
                ex = p.split(b'\x00',1)[0].decode('latin1')
            elif s in ('FULL','DESC','ANAM') and len(p)==4:
                ex = f"lstring/u32=0x{struct.unpack_from('<I',p,0)[0]:08X}"
            elif s == 'CNAM' and len(p)==4:
                ex = f"formid=0x{struct.unpack_from('<I',p,0)[0]:08X}  (perk?)"
            elif s == 'PNAM' and len(p)==4:
                ex = f"formid=0x{struct.unpack_from('<I',p,0)[0]:08X}"
            elif len(p) == 4:
                ex = f"u32=0x{struct.unpack_from('<I',p,0)[0]:08X} f32={struct.unpack_from('<f',p,0)[0]:.4f}"
            else:
                ex = f"len={len(p)} raw={p.hex()[:80]} ascii='{ascii_of(p[:60])}'"
            print(f"    {s}: {ex}")

if __name__ == '__main__':
    main()
