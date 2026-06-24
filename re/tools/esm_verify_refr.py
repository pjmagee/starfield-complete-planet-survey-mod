"""Verify the LCSR PlanetTraitScanTargetLocRef refs are REFRs whose NAME base is the
scan-target ACTI. Direct second pass over ESM for specific formids."""
import struct, zlib
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
WANT = {0x00159EB2, 0x00159F10,   # trait20 Med01 locref refs
        0x0016776F, 0x00167770,   # trait20 Med02
        0x002EA231, 0x002EA0D1}   # trait20 Lg01
def subrecords(rd):
    j=0; real=None
    while j+6<=len(rd):
        ssig=rd[j:j+4]; ssz=struct.unpack_from('<H',rd,j+4)[0]
        if ssig==b'XXXX': real=struct.unpack_from('<I',rd,j+6)[0]; j+=6+ssz; continue
        dsz=ssz
        if real is not None: dsz=real; real=None
        yield ssig, rd[j+6:j+6+dsz]; j+=6+dsz
def inflate(rd,flags):
    if flags&COMPRESSED and len(rd)>=4:
        try: return zlib.decompress(rd[4:])
        except: return b''
    return rd
with open(ESM,'rb') as f: data=f.read()
t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
found={}
def handle(sig,fid,flags,body):
    if fid not in WANT: return
    rd=inflate(body,flags)
    base=None; cell=None
    for s,p in subrecords(rd):
        if s==b'NAME' and len(p)>=4: base=struct.unpack_from('<I',p,0)[0]
    found[fid]=(sig.decode('latin1'),base)
def walk(d):
    i=0;m=len(d)
    while i+24<=m:
        sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
        if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
        flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
        handle(sig,ff,flags,d[i+24:i+24+size]); i+=24+size
while pos+24<=n:
    if data[pos:pos+4]!=b'GRUP': break
    gs=struct.unpack_from('<I',data,pos+4)[0]; walk(data[pos:pos+gs]); pos+=gs
SCAN20=0x0021B250
for fid in sorted(WANT):
    if fid in found:
        sig,base=found[fid]
        tag=' <<== scan-target ACTI 20' if base==SCAN20 else ''
        print(f"0x{fid:08X}: {sig} NAME(base)=0x{(base or 0):08X}{tag}")
    else:
        print(f"0x{fid:08X}: NOT FOUND")
