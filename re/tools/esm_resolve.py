import struct, zlib

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

WANT = [0x0023E905, 0x00171350, 0x00171351, 0x00171352, 0x001699AB, 0x001699A3,
        0x001699A1, 0x00169995, 0x001122A6, 0x00280174, 0x00280175, 0x00280177,
        0x002C5557, 0x002C5552, 0x002AC11D, 0x001D3B47, 0x001D3B46, 0x001D3B48,
        0x0015281C, 0x00177D04, 0x00177CFD, 0x00177D00, 0x00177D01, 0x002634BC]

def subrecords(rd):
    j=0; real=None
    while j+6<=len(rd):
        ssig=rd[j:j+4]; ssz=struct.unpack_from('<H',rd,j+4)[0]
        if ssig==b'XXXX': real=struct.unpack_from('<I',rd,j+6)[0]; j+=6+ssz; continue
        dsz=ssz
        if real is not None: dsz=real; real=None
        yield ssig, rd[j+6:j+6+dsz]; j+=6+dsz

def main():
    with open(ESM,'rb') as f: data=f.read()
    want=set(WANT); out={}
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    def walk(d, base):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size], base+i+24); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if ff in want:
                rd=d[i+24:i+24+size]
                if flags&COMPRESSED and len(rd)>=4:
                    try: rd=zlib.decompress(rd[4:])
                    except: rd=b''
                edid=None
                for ssig,p in subrecords(rd):
                    if ssig==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace'); break
                out[ff]=(sig.decode(),edid)
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs], pos+24)
        pos+=gs
    for fid in WANT:
        sig,edid = out.get(fid,('?','NOT FOUND'))
        print(f"  0x{fid:08X}  {sig:5s}  {edid}")

if __name__=='__main__': main()
