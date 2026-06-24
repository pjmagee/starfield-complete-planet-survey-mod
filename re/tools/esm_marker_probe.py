import struct, sys, zlib

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

TARGETS = {
    0x0023E90D: "Resource (flora slot0 / fauna shared)",
    0x002634BE: "Biomes",
    0x0023E90C: "Genetics",
    0x00171867: "flora-Reproduction",
    0x00171869: "flora 5th",
    0x002634C2: "fauna-Reproduction",
    0x00280178: "fauna-X a",
    0x002634AE: "fauna-X b",
    0x002634AD: "fauna-X c",
    0x001699B2: "fauna-X d",
    0x00280172: "fauna-X e",
    # category-marker singleton forms named by ID_83057 are runtime IDs not formids; skip
}

def read_grup_records(data, base_off, out_index, sig_filter=None, want_forms=None, dumps=None):
    """Walk a GRUP body (records + nested GRUPs). out_index: formid->(sig,absoff,size,flags).
       want_forms: set of formids to capture raw bytes into dumps."""
    i = 0
    n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            # nested group: header 24 bytes, body = size-24
            body = data[i+24:i+size]
            read_grup_records(body, base_off+i+24, out_index, sig_filter, want_forms, dumps)
            i += size
            continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        formid = struct.unpack_from('<I', data, i+12)[0]
        recdata = data[i+24:i+24+size]
        out_index[formid] = (sig.decode('latin1'), base_off+i, size, flags)
        if want_forms and formid in want_forms:
            dumps[formid] = (sig.decode('latin1'), flags, recdata)
        i += 24 + size

def main():
    want = set(TARGETS.keys())
    dumps = {}
    index = {}
    grp_top = {}  # top-level group label(sig) -> count
    with open(ESM, 'rb') as f:
        f.seek(0)
        hdr = f.read(24)
        assert hdr[:4] == b'TES4', hdr[:4]
        tes4size = struct.unpack_from('<I', hdr, 4)[0]
        f.seek(tes4size, 1)  # skip TES4 data
        while True:
            pos = f.tell()
            h = f.read(24)
            if len(h) < 24:
                break
            if h[:4] != b'GRUP':
                break
            gsize = struct.unpack_from('<I', h, 4)[0]
            label = h[8:12]
            gtype = struct.unpack_from('<I', h, 12)[0]
            body = f.read(gsize - 24)
            if gtype == 0:
                lab = label.decode('latin1')
            else:
                lab = "type%d:%r" % (gtype, label)
            grp_top[lab] = grp_top.get(lab, 0) + 1
            read_grup_records(body, pos+24, index, want_forms=want, dumps=dumps)

    print("=== TOP-LEVEL GROUPS (label -> count) ===")
    for k in sorted(grp_top):
        print(f"  {k}: {grp_top[k]}")
    print(f"\n=== INDEXED RECORDS: {len(index)} ===")

    print("\n=== TARGET FORM LOOKUPS ===")
    for fid in sorted(TARGETS):
        if fid in index:
            sig, off, size, flags = index[fid]
            print(f"  0x{fid:08X} {TARGETS[fid]:35s} -> {sig}  size={size} flags=0x{flags:08X} off=0x{off:X}")
        else:
            print(f"  0x{fid:08X} {TARGETS[fid]:35s} -> NOT FOUND in index")

    # signature histogram of just the targets
    from collections import Counter
    c = Counter()
    for fid in TARGETS:
        if fid in index:
            c[index[fid][0]] += 1
    print("\n=== TARGET SIGNATURE HISTOGRAM ===")
    for sig, n in c.most_common():
        print(f"  {sig}: {n}")

    # raw byte dump of ALL requested records
    for fid in sorted(TARGETS):
        if fid in dumps:
            sig, flags, rd = dumps[fid]
            if flags & COMPRESSED and len(rd) >= 4:
                dsz = struct.unpack_from('<I', rd, 0)[0]
                try:
                    rd2 = zlib.decompress(rd[4:])
                    rd = rd2
                    note = f"(decompressed {dsz})"
                except Exception as e:
                    note = f"(decompress FAIL {e})"
            else:
                note = "(uncompressed)"
            print(f"\n=== RAW 0x{fid:08X} sig={sig} flags=0x{flags:08X} {note} len={len(rd)} ===")
            # subrecord walk
            j = 0
            while j + 6 <= len(rd):
                ssig = rd[j:j+4]
                ssz = struct.unpack_from('<H', rd, j+4)[0]
                payload = rd[j+6:j+6+ssz]
                txt = ''
                try:
                    txt = payload.decode('latin1')
                    txt = ''.join(ch if 32 <= ord(ch) < 127 else '.' for ch in txt)
                except Exception:
                    pass
                print(f"  +{j:04X} {ssig.decode('latin1')} sz={ssz} : {payload.hex()} | {txt}")
                j += 6 + ssz

if __name__ == '__main__':
    main()
