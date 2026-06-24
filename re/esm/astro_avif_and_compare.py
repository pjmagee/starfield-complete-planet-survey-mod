#!/usr/bin/env python3
"""(1) Locate the AVIF skill record for Astrophysics by scanning ALL AVIF EDIDs
that contain 'astro' OR whose CNAM perk == Skill_Astrophysics (0x0027CBBB).
(2) Dump comparison perks Skill_Surveying / Skill_Scanning entry points to learn
what entryType=2 + EPFB index means.
(3) List all AVIF EDIDs that look like science skills."""
import struct, zlib

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000
ASTRO_PERK = 0x0027CBBB

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

avif_skills = []      # (fid, edid, perk_cnam)
avif_astro = []       # full dump candidates
perks_to_dump = {}    # edid -> (fid, rd)
PERK_NAMES = {"Skill_Surveying","Skill_Scanning","Skill_Astrophysics","Skill_PlanetaryHabitation","Skill_Geology"}

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
        if sig == b'AVIF' and edid:
            # find CNAM (perk linked to skill) and PNAM
            cnam = None
            for ssig, p in subrecords(rd2):
                if ssig == b'PNAM' and len(p)==4:
                    cnam = struct.unpack_from('<I',p,0)[0]
            if 'astro' in edid.lower() or (cnam == ASTRO_PERK):
                avif_astro.append((fid, edid, rd2))
            # collect science-ish skills (those with a Sci-style EDID)
            avif_skills.append((fid, edid, cnam))
        if sig == b'PERK' and edid in PERK_NAMES:
            perks_to_dump[edid] = (fid, rd2)
        i += 24 + size

def ascii_of(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)

def decode_perk_entrypoints(rd):
    """Print only PRKE/DATA/EPFT/EPFB/EPFD lines."""
    for ssig, p in subrecords(rd):
        s = ssig.decode('latin1')
        if s == 'PRKE' and len(p)>=3:
            print(f"      PRKE entryType={p[0]} rank={p[1]} priority={p[2]}")
        elif s == 'DATA':
            print(f"      DATA {p.hex()}")
        elif s == 'EPFT' and len(p)>=1:
            print(f"      EPFT paramType={p[0]}")
        elif s == 'EPFB':
            v = struct.unpack_from('<H',p,0)[0] if len(p)>=2 else (p[0] if p else -1)
            print(f"      EPFB len={len(p)} val={v} raw={p.hex()}")
        elif s == 'EPFD':
            ex=""
            if len(p)==4: ex=f"u32=0x{struct.unpack_from('<I',p,0)[0]:08X} f32={struct.unpack_from('<f',p,0)[0]:.4f}"
            print(f"      EPFD len={len(p)} {ex} raw={p.hex()}")
        elif s == 'EPF2':
            print(f"      EPF2 '{ascii_of(p)}'")

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

    print("=== AVIF skill record(s) linked to Skill_Astrophysics perk OR named *astro* ===")
    for fid, edid, rd in avif_astro:
        print(f"\n  0x{fid:08X} AVIF {edid}")
        for ssig, p in subrecords(rd):
            s = ssig.decode('latin1')
            ex = ""
            if s == 'EDID': ex = p.split(b'\x00',1)[0].decode('latin1')
            elif s in ('FULL','DESC') and len(p)==4: ex=f"lstring=0x{struct.unpack_from('<I',p,0)[0]:08X}"
            elif s in ('PNAM','CNAM','SNAM') and len(p)==4: ex=f"formid=0x{struct.unpack_from('<I',p,0)[0]:08X}"
            elif len(p)==4: ex=f"u32=0x{struct.unpack_from('<I',p,0)[0]:08X} f32={struct.unpack_from('<f',p,0)[0]:.4f}"
            else: ex=f"len={len(p)} raw={p.hex()[:60]} '{ascii_of(p[:48])}'"
            print(f"      {s}: {ex}")

    print("\n=== Comparison perk entry points ===")
    for name in ['Skill_Scanning','Skill_Surveying','Skill_Astrophysics']:
        if name in perks_to_dump:
            fid, rd = perks_to_dump[name]
            print(f"\n  PERK {name} 0x{fid:08X}:")
            decode_perk_entrypoints(rd)

    print("\n=== AVIF skills whose EDID starts with 'Sci' or contains science skill names ===")
    for fid, edid, cnam in sorted(avif_skills, key=lambda x:x[1]):
        low = edid.lower()
        if any(k in low for k in ['astro','survey','scan','geolog','botan','zoolog','habitat','chemis','physi','science','aneutron','planetary']):
            cn = f"perk=0x{cnam:08X}" if cnam else "perk=-"
            print(f"  0x{fid:08X} {edid}  {cn}")

if __name__ == '__main__':
    main()
