import struct, zlib, sys
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
WANT = set(int(x,16) for x in sys.argv[1:]) if len(sys.argv)>1 else {
    0x0022A2B6, 0x0027A567, 0x001CBEA3, 0x0010A476, 0x0021D242, 0x003807B9,
    0x0003F5A1, 0x001953F8, 0x00334CCC, 0x00000168,
}
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
def edid(rd):
    for s,p in subrecords(rd):
        if s==b'EDID': return p.split(b'\x00',1)[0].decode('latin1','replace')
    return None
def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    out={}
    def handle(sig,fid,flags,body):
        if fid not in WANT: return
        rd=inflate(body,flags)
        if not rd: out[fid]=(sig.decode('latin1'),None,[]); return
        out[fid]=(sig.decode('latin1'),edid(rd),list(subrecords(rd)))
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
    for fid in WANT:
        if fid not in out: print(f"0x{fid:08X}: NOT FOUND"); continue
        sig,ed,subs=out[fid]
        print(f"\n0x{fid:08X} {sig} EDID={ed}")
        for ssig,p in subs:
            print(f"    {ssig.decode('latin1')} ({len(p)}B): {p[:80].hex()}")
if __name__=='__main__': main()
