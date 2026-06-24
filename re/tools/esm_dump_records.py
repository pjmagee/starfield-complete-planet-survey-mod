import struct, zlib, sys

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

WANT = {
    0x00160C96: "FLST HandScannerPlantKeywords",
    0x00160C97: "FLST HandScannerActorKeywords",
    0x00237EB2: "DFOB HandScannerAnyResourceDO",
    0x002491FE: "DFOB HandScannerActorHealthDO",
    0x002491FB: "DFOB HandScannerAnyBiomesDO",
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

found = {}
def walk(data, base, want):
    i = 0; n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            walk(data[i+24:i+size], base+i+24, want); i += size; continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        fid = struct.unpack_from('<I', data, i+12)[0]
        if fid in want:
            found[fid] = (sig.decode('latin1'), flags, data[i+24:i+24+size])
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
            walk(data[pos+24:pos+gsize], pos+24, set(WANT))
        pos += gsize

    for fid in WANT:
        if fid not in found:
            print(f"0x{fid:08X} {WANT[fid]}: NOT FOUND"); continue
        sig, flags, rd = found[fid]
        rd = inflate(rd, flags)
        print(f"\n=== 0x{fid:08X} {WANT[fid]} (sig={sig} flags=0x{flags:08X} len={len(rd)}) ===")
        for ssig, payload in subrecords(rd):
            txt = ''.join(chr(b) if 32 <= b < 127 else '.' for b in payload)
            extra = ''
            if ssig in (b'LNAM', b'CNAM', b'DATA', b'FNAM') and len(payload) == 4:
                extra = f"  u32=0x{struct.unpack_from('<I', payload,0)[0]:08X}"
            elif ssig == b'CTDA':
                extra = f"  [CTDA len {len(payload)}]"
            print(f"  {ssig.decode('latin1')} sz={len(payload):4d} : {payload.hex()}{extra} | {txt}")

if __name__ == '__main__':
    main()
