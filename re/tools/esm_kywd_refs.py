import struct, zlib
from collections import Counter, defaultdict

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

# the HandScanner marker keyword formids we care about (a representative subset incl all targets)
MARKERS = {
    0x0023E90D, 0x002634BE, 0x0023E90C, 0x00171867, 0x00171869, 0x002634C2,
    0x00280178, 0x002634AE, 0x002634AD, 0x001699B2, 0x00280172,
    0x002634AF, 0x00280173,  # other temperaments
    0x002634BB, 0x002634BC, 0x002634BD, 0x002634BF, 0x002634C0, 0x002634C1,  # Any* / Actor*
}

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

def inflate(rd, flags):
    if flags & COMPRESSED and len(rd) >= 4:
        try: return zlib.decompress(rd[4:])
        except Exception: return b''
    return rd

# Map: which record (sig, formid, edid) contains a raw little-endian u32 equal to a marker formid.
# Scan EVERY record's decompressed data for any 4-byte occurrence of a marker id (aligned).
hits = defaultdict(list)   # marker -> list of (sig, formid, edid, subrec_sigs_that_contained_it)
sig_hist = Counter()       # which record SIGs reference markers

def scan_record(sig, fid, flags, rd):
    rd = inflate(rd, flags)
    if not rd: return
    edid = None
    subhits = []  # (subsig, marker)
    for ssig, payload in subrecords(rd):
        if ssig == b'EDID':
            edid = payload.rstrip(b'\x00').decode('latin1', 'replace')
        # scan payload for marker u32 (aligned to 4 within the subrecord)
        for off in range(0, len(payload) - 3, 4):
            v = struct.unpack_from('<I', payload, off)[0]
            if v in MARKERS:
                subhits.append((ssig.decode('latin1'), v))
    if subhits:
        for ss, v in subhits:
            hits[v].append((sig, fid, edid, ss))
            sig_hist[(sig, ss)] += 1

def walk(data, base, cb):
    i = 0; n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            walk(data[i+24:i+size], base+i+24, cb)
            i += size; continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        formid = struct.unpack_from('<I', data, i+12)[0]
        cb(sig.decode('latin1'), formid, flags, data[i+24:i+24+size])
        i += 24 + size

def main():
    with open(ESM, 'rb') as f:
        data = f.read()
    tes4size = struct.unpack_from('<I', data, 4)[0]
    pos = 24 + tes4size; n = len(data)
    while pos + 24 <= n:
        if data[pos:pos+4] != b'GRUP': break
        gsize = struct.unpack_from('<I', data, pos+4)[0]
        label = data[pos+8:pos+12]; gtype = struct.unpack_from('<I', data, pos+12)[0]
        # skip the KYWD group itself (markers reference themselves there) and CELL/NAVM huge groups
        if gtype == 0 and label not in (b'KYWD',):
            walk(data[pos+24:pos+gsize], pos+24, scan_record)
        pos += gsize

    print("=== record (SIG, subrecSIG) that references HandScanner markers — histogram ===")
    for (sig, ss), c in sig_hist.most_common(40):
        print(f"  {sig}.{ss}: {c}")

    print("\n=== per-marker: which records reference it (first 6 each) ===")
    for m in sorted(MARKERS):
        lst = hits.get(m, [])
        print(f"\n  marker 0x{m:08X}: {len(lst)} refs")
        # group by sig
        bysig = Counter(h[0] for h in lst)
        print("     by sig:", dict(bysig))
        for sig, fid, edid, ss in lst[:6]:
            print(f"       {sig} 0x{fid:08X} [{ss}] {edid}")

if __name__ == '__main__':
    main()
