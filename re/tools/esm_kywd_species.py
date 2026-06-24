import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

def subrecords(rd):
    j = 0
    real = None
    while j + 6 <= len(rd):
        ssig = rd[j:j+4]
        ssz = struct.unpack_from('<H', rd, j+4)[0]
        if ssig == b'XXXX':
            real = struct.unpack_from('<I', rd, j+6)[0]
            j += 6 + ssz
            continue
        dsz = ssz
        if real is not None:
            dsz = real; real = None
        payload = rd[j+6:j+6+dsz]
        yield ssig, payload
        j += 6 + dsz

def recdata(data, i, size, flags):
    rd = data[i+24:i+24+size]
    if flags & COMPRESSED and len(rd) >= 4:
        try:
            rd = zlib.decompress(rd[4:])
        except Exception:
            return b''
    return rd

def walk_group(data, base, cb, sigs):
    i = 0; n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            walk_group(data[i+24:i+size], base+i+24, cb, sigs)
            i += size; continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        formid = struct.unpack_from('<I', data, i+12)[0]
        if sig in sigs:
            cb(sig.decode('latin1'), formid, flags, data[i+24:i+24+size])
        i += 24 + size

# Pass 1: collect all KYWD edids; identify HandScanner* markers and build formid->edid
kywd_edid = {}
handscanner = {}
def kywd_cb(sig, fid, flags, rd):
    for ssig, payload in subrecords(rd):
        if ssig == b'EDID':
            e = payload.rstrip(b'\x00').decode('latin1')
            kywd_edid[fid] = e
            if e.startswith('HandScanner'):
                handscanner[fid] = e
            break

# Pass 2: sample FLOR and NPC_ records, dump keyword arrays (KWDA) and note which are HandScanner
flor_samples = []
npc_samples = []
def species_cb(sig, fid, flags, rd):
    rd = recdata_inline(rd, flags)
    kwda = []
    pndt_ref = None
    edid = None
    for ssig, payload in subrecords(rd):
        if ssig == b'EDID':
            edid = payload.rstrip(b'\x00').decode('latin1')
        elif ssig == b'KWDA':
            kwda = list(struct.unpack_from('<%dI' % (len(payload)//4), payload, 0))
        elif ssig == b'KSIZ':
            pass
    if sig == 'FLOR' and len(flor_samples) < 8:
        flor_samples.append((fid, edid, kwda))
    elif sig == 'NPC_' and len(npc_samples) < 8 and kwda:
        npc_samples.append((fid, edid, kwda))

def recdata_inline(rd, flags):
    if flags & COMPRESSED and len(rd) >= 4:
        try:
            return zlib.decompress(rd[4:])
        except Exception:
            return b''
    return rd

def main():
    with open(ESM, 'rb') as f:
        data = f.read()
    # iterate top-level groups
    assert data[:4] == b'TES4'
    tes4size = struct.unpack_from('<I', data, 4)[0]
    pos = 24 + tes4size
    n = len(data)
    while pos + 24 <= n:
        if data[pos:pos+4] != b'GRUP':
            break
        gsize = struct.unpack_from('<I', data, pos+4)[0]
        label = data[pos+8:pos+12]
        gtype = struct.unpack_from('<I', data, pos+12)[0]
        body = data[pos+24:pos+gsize]
        if gtype == 0 and label == b'KYWD':
            walk_group(body, pos+24, kywd_cb, {b'KYWD'})
        if gtype == 0 and label in (b'FLOR', b'NPC_'):
            walk_group(body, pos+24, species_cb, {b'FLOR', b'NPC_'})
        pos += gsize

    print(f"=== HandScanner* KYWD count: {len(handscanner)} (of {len(kywd_edid)} total KYWD) ===")
    # group by prefix
    cat = Counter()
    for e in handscanner.values():
        # category after HandScanner
        rest = e[len('HandScanner'):]
        # take leading category token
        cat[rest.split('_')[0]] += 1
    print("  HandScanner sub-prefixes:")
    for k, v in cat.most_common():
        print(f"    {k}: {v}")
    print("  full list:")
    for fid in sorted(handscanner):
        print(f"    0x{fid:08X}  {handscanner[fid]}")

    print("\n=== FLOR samples (formid, edid, KWDA-handscanner-only) ===")
    for fid, edid, kwda in flor_samples:
        hs = [(k, kywd_edid.get(k,'?')) for k in kwda if k in handscanner]
        print(f"  FLOR 0x{fid:08X} {edid}")
        for k, e in hs:
            print(f"      0x{k:08X} {e}")
        if not hs:
            print(f"      (no HandScanner keywords in KWDA; total kwda={len(kwda)})")

    print("\n=== NPC_ samples (formid, edid, KWDA-handscanner-only) ===")
    for fid, edid, kwda in npc_samples:
        hs = [(k, kywd_edid.get(k,'?')) for k in kwda if k in handscanner]
        print(f"  NPC_ 0x{fid:08X} {edid}")
        for k, e in hs:
            print(f"      0x{k:08X} {e}")
        if not hs:
            print(f"      (no HandScanner keywords in KWDA; total kwda={len(kwda)})")

if __name__ == '__main__':
    main()
