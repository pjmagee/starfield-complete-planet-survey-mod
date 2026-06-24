#!/usr/bin/env python3
"""Find Astrophysics-related records (AVIF skill, PERK ranks) by EDID substring,
and any QUST/SCPT/GLOB/MGEF/SPEL whose EDID mentions astrophysics/orbital/trait
discovery. Reuses the project's record/subrecord/compression parsing.
"""
import struct, zlib

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

# EDID substrings (lowercased) we care about
NEEDLES = [
    "astrophysics", "astro",
]
# broader scan for trait/orbital discovery authoring
NEEDLES2 = [
    "planettrait", "orbitalscan", "surveyplanet", "scanplanet",
    "discovertrait", "planetscan", "orbitscan",
]

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

hits = []  # (sig, top, fid, edid)

def walk(data, base, top_sig, depth=0):
    i = 0; n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            label = data[i+8:i+12]; gtype = struct.unpack_from('<I', data, i+12)[0]
            new_top = top_sig
            if gtype == 0:
                new_top = label
            walk(data[i+24:i+size], base+i+24, new_top, depth+1)
            i += size; continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        fid = struct.unpack_from('<I', data, i+12)[0]
        rd = data[i+24:i+24+size]
        # only inflate small records to keep it fast; EDID is early so peek raw first
        edid = None
        try:
            if flags & COMPRESSED:
                rd2 = inflate(rd, flags)
            else:
                rd2 = rd
            edid = get_edid(rd2)
        except Exception:
            edid = None
        if edid:
            el = edid.lower()
            if any(nd in el for nd in NEEDLES) or any(nd in el for nd in NEEDLES2):
                hits.append((sig.decode('latin1'), top_sig.decode('latin1','replace'), fid, edid))
        i += 24 + size

def main():
    with open(ESM, 'rb') as f:
        data = f.read()
    tes4size = struct.unpack_from('<I', data, 4)[0]
    pos = 24 + tes4size; n = len(data)
    while pos + 24 <= n:
        if data[pos:pos+4] != b'GRUP': break
        gsize = struct.unpack_from('<I', data, pos+4)[0]
        gtype = struct.unpack_from('<I', data, pos+12)[0]
        label = data[pos+8:pos+12]
        if gtype == 0:
            walk(data[pos+24:pos+gsize], pos+24, label, 1)
        pos += gsize

    hits.sort(key=lambda h: (h[0], h[3].lower()))
    print(f"=== {len(hits)} EDID hits ===")
    for sig, top, fid, edid in hits:
        print(f"  {sig:5s} top={top:5s} 0x{fid:08X}  {edid}")

if __name__ == '__main__':
    main()
