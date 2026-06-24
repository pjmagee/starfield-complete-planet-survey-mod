#!/usr/bin/env python3
"""Whole-ESM record indexer: builds formid -> (record sig, EDID, top-group sig)
and dumps full record bytes for a set of target form-ids. Used to identify the
attribute-category marker forms and decode their authored membership link.
"""
import struct, zlib, sys, json

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

TARGETS = set(int(x,16) for x in [
    "0x0023E90D","0x002634BE","0x0023E90C","0x00171867","0x00171869","0x002634C2",
    "0x00280178","0x002634AE","0x002634AD","0x001699B2","0x00280172",
])

def subrecords(data):
    i, real = 0, None
    while i + 6 <= len(data):
        sig = data[i:i+4]; sz = struct.unpack_from('<H', data, i+4)[0]; i += 6
        if sig == b'XXXX':
            real = struct.unpack_from('<I', data, i)[0]; i += sz; continue
        if real is not None: sz, real = real, None
        if i+sz > len(data): break
        yield sig, data[i:i+sz]; i += sz

def get_edid(data):
    for ssig, payload in subrecords(data):
        if ssig == b'EDID':
            return payload.split(b'\x00',1)[0].decode('latin1')
    return None

def walk(f, end, top_sig, index, dumps, depth=0):
    while f.tell() < end:
        pos = f.tell()
        h = f.read(24)
        if len(h) < 24: break
        sig = h[0:4]
        size = struct.unpack_from('<I', h, 4)[0]
        if sig == b'GRUP':
            label = h[8:12]; gtype = struct.unpack_from('<I', h,12)[0]
            new_top = top_sig
            if gtype == 0 and depth == 0:
                new_top = label
            walk(f, pos+size, new_top, index, dumps, depth+1)
            f.seek(pos+size)
            continue
        flags = struct.unpack_from('<I', h, 8)[0]
        formid = struct.unpack_from('<I', h, 12)[0]
        data = f.read(size)
        raw = data
        if flags & COMPRESSED and len(data) >= 4:
            try: data = zlib.decompress(data[4:])
            except Exception: pass
        index[formid] = (sig.decode('latin1'), top_sig.decode('latin1'))
        if formid in TARGETS:
            edid = get_edid(data)
            subs = [(s.decode('latin1'), len(p)) for s,p in subrecords(data)]
            dumps[formid] = {
                'sig': sig.decode('latin1'), 'top': top_sig.decode('latin1'),
                'edid': edid, 'flags': flags, 'subs': subs,
                'data': data.hex(),
            }
            print(f"FOUND 0x{formid:08X}  sig={sig.decode('latin1')} top={top_sig.decode('latin1')} EDID={edid}")
            print(f"   subs: {subs}")

def main():
    with open(ESM,'rb') as f:
        f.seek(0,2); fsize = f.tell(); f.seek(0)
        hdr = f.read(24)
        size = struct.unpack_from('<I', hdr, 4)[0]
        f.seek(24+size)  # skip TES4
        index = {}
        dumps = {}
        walk(f, fsize, b'\x00\x00\x00\x00', index, dumps)
    print(f"\nindexed {len(index)} records")
    with open(r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/esm/target_dumps.json","w") as o:
        json.dump(dumps, o, indent=1)
    # save a compact index of sig counts
    from collections import Counter
    c = Counter(v[0] for v in index.values())
    print("top record sigs:", c.most_common(20))

if __name__ == "__main__":
    main()
