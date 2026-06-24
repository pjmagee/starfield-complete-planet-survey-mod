#!/usr/bin/env python3
"""Confirm the PlanetTrait KYWD set: list all KYWD records whose EDID starts
with 'PlanetTrait' and any KYWD with keyword-type 44 (KeywordType_PlanetTrait).
Also report the FLST 0x00160C96/0x00160C97 contents count."""
import struct, zlib

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

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

kywd = []
flst = {}
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
        edid=None
        try: edid=get_edid(rd2)
        except Exception: pass
        if sig == b'KYWD' and edid and edid.startswith('PlanetTrait'):
            # KYWD TNAM = keyword type
            tnam=None
            for ssig,p in subrecords(rd2):
                if ssig==b'TNAM' and len(p)==4: tnam=struct.unpack_from('<I',p,0)[0]
            kywd.append((fid, edid, tnam))
        if sig == b'FLST' and fid in (0x00160C96, 0x00160C97):
            members=[]
            for ssig,p in subrecords(rd2):
                if ssig==b'LNAM' and len(p)==4:
                    members.append(struct.unpack_from('<I',p,0)[0])
            flst[fid]=(edid, members)
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
        if gtype == 0:
            walk(data[pos+24:pos+gsize], pos+24)
        pos += gsize

    kywd.sort(key=lambda x:x[1])
    print(f"=== {len(kywd)} KYWD records EDID startswith 'PlanetTrait' ===")
    for fid, edid, tnam in kywd:
        print(f"  0x{fid:08X}  TNAM={tnam}  {edid}")

    print("\n=== FLST 0x00160C96 / 0x00160C97 member counts ===")
    for fid,(edid,members) in flst.items():
        print(f"  0x{fid:08X} {edid}: {len(members)} members")

if __name__ == '__main__':
    main()
