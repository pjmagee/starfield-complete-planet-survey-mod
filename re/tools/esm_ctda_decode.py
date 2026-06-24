import struct, zlib

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

# CTDA layout (Starfield, 32 bytes):
#  +0x00 u8  operator/flags
#  +0x01 3x  unused
#  +0x04 f32 comparison value (or 0x4503f944 etc -> actually function index encoded? decode below)
#  +0x08 u16 function index
#  +0x0A 2x  padding
#  +0x0C u32 param1 (often a form id - the keyword being tested)
#  +0x10 u32 param2
#  +0x14 ... run-on / source etc
#  +0x1C u32 0xffffffff terminator-ish
# We observed bytes like: 000000000000803f 4503f944 047d1700 ...
#  comp value f32 = 0x3f800000? no -> bytes 00 00 80 3f at +0x04 = 1.0f  (comparison value)
#  +0x08: 4503 f944 -> u16 funcindex=0x0345=837 ; then f944 ?
# Let's just print decoded fields.

def subrecords(rd):
    j = 0; real = None
    while j + 6 <= len(rd):
        ssig = rd[j:j+4]; ssz = struct.unpack_from('<H', rd, j+4)[0]
        if ssig == b'XXXX':
            real = struct.unpack_from('<I', rd, j+6)[0]; j += 6+ssz; continue
        dsz = ssz
        if real is not None: dsz = real; real = None
        yield ssig, rd[j+6:j+6+dsz]; j += 6+dsz

def find(fid):
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data); res=[None]
    def walk(d, base):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size], base+i+24); i+=size; continue
            f2=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if ff==fid: res[0]=(sig.decode(),f2,d[i+24:i+24+size])
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs], pos+24)
        pos+=gs
    return res[0]

def decode_ctda(p):
    op = p[0]
    comp = struct.unpack_from('<f', p, 4)[0]
    func = struct.unpack_from('<H', p, 8)[0]
    param1 = struct.unpack_from('<I', p, 0xC)[0]
    param2 = struct.unpack_from('<I', p, 0x10)[0]
    p3 = struct.unpack_from('<I', p, 0x14)[0]
    p4 = struct.unpack_from('<I', p, 0x18)[0]
    runon = struct.unpack_from('<I', p, 0x1C)[0]
    return op, comp, func, param1, param2, p3, p4, runon

def dump(fid, name):
    sig, flags, rd = find(fid)
    if flags & COMPRESSED: rd = zlib.decompress(rd[4:])
    lnam = []
    print(f"\n##### 0x{fid:08X} {name} #####")
    entries = []
    cur = None
    for ssig, payload in subrecords(rd):
        if ssig == b'LNAM':
            lnam.append(struct.unpack_from('<I', payload, 0)[0])
        elif ssig == b'INAM':
            cur = {'inam': struct.unpack_from('<I', payload,0)[0], 'ctda': []}
            entries.append(cur)
        elif ssig == b'CITC':
            cur['citc'] = struct.unpack_from('<I', payload,0)[0]
        elif ssig == b'CTDA':
            cur['ctda'].append(payload)
    print(f"  LNAM markers ({len(lnam)}): " + ", ".join(f"0x{x:08X}" for x in lnam))
    print(f"  conditioned entries: {len(entries)}")
    for e in entries:
        idx = e['inam']
        marker = lnam[idx] if idx < len(lnam) else None
        print(f"  -- INAM idx={idx} -> marker 0x{(marker or 0):08X}  (citc={e.get('citc')})")
        for c in e['ctda']:
            op, comp, func, p1, p2, p3, p4, runon = decode_ctda(c)
            print(f"       op=0x{op:02X} func={func} comp={comp:g} param1=0x{p1:08X} param2=0x{p2:08X} p3=0x{p3:08X} p4=0x{p4:08X} runon=0x{runon:08X}")

dump(0x00160C96, "HandScannerPlantKeywords")
dump(0x00160C97, "HandScannerActorKeywords")
