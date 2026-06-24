#!/usr/bin/env python3
"""Correct Starfield BA2 GNRL parser. Record layout (from hexdump of
Starfield - Misc.ba2, ends with 0xBAADF00D sentinel per record):
  nameHash u32 | ext 4 bytes | dirHash u32 | flags/unk u32 |
  offset u64 | packedSize u32 | unpackedSize u32 | sentinel u32 (0xBAADF00D)
= 36 bytes. Grep decompressed .pex for needle strings."""
import struct, zlib

BA2 = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield - Misc.ba2"
NEEDLES = [b"Astrophysics", b"DiscoverTrait", b"PlanetTrait", b"OrbitalScan",
           b"DiscoverTraitChance", b"Skill_Astrophysics"]

def main():
    with open(BA2,'rb') as f:
        data = f.read()
    file_count = struct.unpack_from('<I', data, 12)[0]
    name_tbl_off = struct.unpack_from('<Q', data, 16)[0]
    # names
    names=[]; p=name_tbl_off
    for _ in range(file_count):
        if p+2>len(data): break
        ln=struct.unpack_from('<H',data,p)[0]; p+=2
        names.append(data[p:p+ln].decode('latin1','replace')); p+=ln

    rec_off=32; rec_size=36
    hits={}
    parsed=0; decomp_ok=0
    for idx in range(file_count):
        base=rec_off+idx*rec_size
        ext=data[base+4:base+8]
        offset=struct.unpack_from('<Q',data,base+16)[0]
        packed=struct.unpack_from('<I',data,base+24)[0]
        unpacked=struct.unpack_from('<I',data,base+28)[0]
        sentinel=struct.unpack_from('<I',data,base+32)[0]
        nm=names[idx] if idx<len(names) else f"<{idx}>"
        parsed+=1
        size = packed if packed else unpacked
        raw=data[offset:offset+size]
        if packed:
            try:
                raw=zlib.decompress(raw); decomp_ok+=1
            except Exception:
                continue
        for nd in NEEDLES:
            if nd in raw:
                hits.setdefault(nm,set()).add(nd.decode())
    print(f"parsed={parsed} decompressed_ok={decomp_ok}")
    print(f"\n=== files referencing needles ({len(hits)}) ===")
    for nm in sorted(hits):
        print(f"  {nm}  -> {sorted(hits[nm])}")

if __name__=='__main__':
    main()
