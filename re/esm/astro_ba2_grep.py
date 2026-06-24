#!/usr/bin/env python3
"""Minimal BA2 (general/GNRL format) reader for 'Starfield - Misc.ba2'. Lists
script .pex files and greps decompressed contents for needle strings to find any
Papyrus script that references the Astrophysics discover-trait mechanism.
BA2 GNRL header: magic 'BTDX', version, type 'GNRL', fileCount(u32), nameTableOffset(u64).
Each file record (GNRL): nameHash(u32) ext(4) dirHash(u32) flags(u32) offset(u64)
packedSize(u32) unpackedSize(u32) align(u32). zlib if packedSize!=0."""
import struct, zlib, os

BA2 = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield - Misc.ba2"
NEEDLES = [b"Astrophysics", b"DiscoverTrait", b"DiscoverTraitChance",
           b"PlanetTrait", b"OrbitalScan", b"ScanPlanet"]

def main():
    with open(BA2,'rb') as f:
        data = f.read()
    magic = data[0:4]
    if magic != b'BTDX':
        print("not BTDX:", magic); return
    version = struct.unpack_from('<I', data, 4)[0]
    btype = data[8:12]
    file_count = struct.unpack_from('<I', data, 12)[0]
    name_tbl_off = struct.unpack_from('<Q', data, 16)[0]
    print(f"BA2 version={version} type={btype} files={file_count} nameTblOff=0x{name_tbl_off:X}")
    if btype != b'GNRL':
        print("not GNRL general archive; type=", btype); return

    # name table: for each file, u16 len + name bytes
    names = []
    p = name_tbl_off
    for _ in range(file_count):
        if p+2 > len(data): break
        ln = struct.unpack_from('<H', data, p)[0]; p += 2
        nm = data[p:p+ln].decode('latin1','replace'); p += ln
        names.append(nm)

    # file records start at offset 24 (after header). GNRL record = 36 bytes? Starfield v1/v2/v3 differ.
    # Starfield GNRL record layout (32 bytes): nameHash(4) ext(4) dirHash(4) flags(4) offset(8) packed(4) unpacked(4)  + align(4)=36
    rec_size = 36 if version >= 2 else 36
    rec_off = 24
    hits = {}
    for idx in range(file_count):
        base = rec_off + idx*rec_size
        if base + rec_size > len(data): break
        ext = data[base+4:base+8]
        offset = struct.unpack_from('<Q', data, base+16)[0]
        packed = struct.unpack_from('<I', data, base+24)[0]
        unpacked = struct.unpack_from('<I', data, base+28)[0]
        nm = names[idx] if idx < len(names) else f"<idx{idx}>"
        # only inspect script/pex/psc/text-ish entries to keep it fast
        if ext not in (b'pex\x00', b'pex ', b'psc\x00', b'txt\x00', b'PEX\x00'):
            # still check anything small
            if unpacked > 2_000_000:
                continue
        raw = data[offset:offset+packed] if packed else data[offset:offset+unpacked]
        if packed:
            try: raw = zlib.decompress(raw)
            except Exception:
                continue
        for nd in NEEDLES:
            if nd in raw:
                hits.setdefault(nm, set()).add(nd.decode())
    print(f"\n=== files referencing needles ({len(hits)}) ===")
    for nm in sorted(hits):
        print(f"  {nm}  -> {sorted(hits[nm])}")

if __name__ == '__main__':
    main()
