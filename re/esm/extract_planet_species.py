#!/usr/bin/env python3
"""Validate the planet->flora/fauna data model by parsing Starfield.esm's PNDT
records directly. Confirms (a) whether PNDT records are zlib-compressed and
(b) that the PPBD "Per Biome Data" subrecord layout extracts the species lists.

This is a reference / proof-of-concept for the in-plugin (SFSE) runtime reader.
PPBD per-biome layout (from RE of the loader ID_51401 + xEdit wbDefinitionsSF1):
  u32 biome FormID | f32 chance | u32 unk | u32 resourceGen(RSGD)
  u32 faunaCount   | faunaCount x u32 FormID(NPC_)
  u32 kwCount      | kwCount    x u32 FormID(KYWD)
  u32 floraCount   | u32 entrySize(=9) | floraCount x { u32 FLOR, u32 MISC, u8 freq }
  (u32 resCount    | resCount   x 16 bytes)   # version-gated
"""
import struct, zlib, sys

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

def subrecords(data):
    i, real = 0, None
    while i + 6 <= len(data):
        sig = data[i:i+4]; sz = struct.unpack_from('<H', data, i+4)[0]; i += 6
        if sig == b'XXXX':
            real = struct.unpack_from('<I', data, i)[0]; i += sz; continue
        if real is not None: sz, real = real, None
        yield sig, data[i:i+sz]; i += sz

def parse_ppbd(p):
    o = [0]
    def u32():
        v = struct.unpack_from('<I', p, o[0])[0]; o[0] += 4; return v
    try:
        biome = u32(); _chance = u32(); _unk = u32(); _rgen = u32()
        nf = u32(); fauna = [u32() for _ in range(nf)]
        nk = u32(); _kw = [u32() for _ in range(nk)]
        nfl = u32(); esz = u32() or 9
        flora = []
        for _ in range(nfl):
            flor = struct.unpack_from('<I', p, o[0])[0]; o[0] += esz
            flora.append(flor)
        return biome, fauna, flora
    except struct.error:
        return None, [], []

def main():
    with open(ESM, 'rb') as f:
        sig = f.read(4); size = struct.unpack('<I', f.read(4))[0]
        f.read(16); f.seek(size, 1)              # skip TES4 record (header was 24B, 8 already read)
        # walk top-level groups for the PNDT type-group
        while True:
            pos = f.tell(); g = f.read(24)
            if len(g) < 24: print("PNDT group not found"); return
            if g[0:4] != b'GRUP': print(f"unexpected {g[0:4]} at {pos:#x}"); return
            gsize = struct.unpack_from('<I', g, 4)[0]
            label = g[8:12]; gtype = struct.unpack_from('<I', g, 12)[0]
            if gtype == 0 and label == b'PNDT':
                return parse_group(f, pos + gsize)
            f.seek(pos + gsize)

def parse_group(f, end):
    total = comp = with_species = 0
    fauna_total = flora_total = 0
    samples = []
    while f.tell() < end:
        h = f.read(24)
        if len(h) < 24: break
        sig = h[0:4]; size = struct.unpack_from('<I', h, 4)[0]
        flags = struct.unpack_from('<I', h, 8)[0]; formid = struct.unpack_from('<I', h, 12)[0]
        if sig == b'GRUP':                                   # nested group: skip body
            f.seek(struct.unpack_from('<I', h, 4)[0] - 24, 1); continue
        data = f.read(size)
        if sig != b'PNDT': continue
        total += 1
        if flags & COMPRESSED:
            comp += 1
            data = zlib.decompress(data[4:])                 # first u32 = decompressed size
        fauna, flora, nbiomes = set(), set(), 0
        for ssig, payload in subrecords(data):
            if ssig == b'PPBD':
                nbiomes += 1
                _b, fa, fl = parse_ppbd(payload)
                fauna.update(x for x in fa if x); flora.update(x for x in fl if x)
        if fauna or flora:
            with_species += 1; fauna_total += len(fauna); flora_total += len(flora)
            if len(samples) < 6:
                samples.append((formid, nbiomes, sorted(fauna), sorted(flora)))
    print(f"PNDT records: {total}  (compressed: {comp})")
    print(f"planets with PPBD fauna/flora: {with_species}")
    print(f"total distinct fauna refs: {fauna_total}, flora refs: {flora_total}")
    print("samples (planet formid -> biomes, fauna ids, flora ids):")
    for fid, nb, fa, fl in samples:
        print(f"  0x{fid:08X}: {nb} biomes | fauna {[f'0x{x:08X}' for x in fa[:6]]} | flora {[f'0x{x:08X}' for x in fl[:6]]}")

if __name__ == "__main__":
    main()
