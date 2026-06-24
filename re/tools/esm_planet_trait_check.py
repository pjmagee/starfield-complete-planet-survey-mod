import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
TRAITS = {0x00225590:"PlanetTrait09EcologicalConsortium",0x00225589:"PlanetTrait19PsychotropicBiota",
          0x00225597:"PlanetTrait00AeriformLife"}
TRAITSET=set(TRAITS)

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

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    pndt_total=[0]; pndt_with_trait=[0]
    sub_hist=Counter()  # which subrecord carries the trait kw
    samples=[]
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if sig==b'PNDT':
                pndt_total[0]+=1
                rd=inflate(d[i+24:i+24+size], flags)
                hits=[]
                for ssig,p in subrecords(rd):
                    for o in range(0,len(p)-3):
                        v=struct.unpack_from('<I',p,o)[0]
                        if v in TRAITSET:
                            sub_hist[ssig.decode('latin1')]+=1
                            hits.append((ssig.decode('latin1'),v))
                if hits:
                    pndt_with_trait[0]+=1
                    if len(samples)<8: samples.append((ff,hits))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; lab=data[pos+8:pos+12]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0 and lab==b'PNDT': walk(data[pos+24:pos+gs])
        pos+=gs
    print(f"PNDT total={pndt_total[0]} ; carrying a reproduction-relevant planet trait kw: {pndt_with_trait[0]}")
    print("subrecord carrying the trait kw:", dict(sub_hist))
    for ff,hits in samples:
        seen=sorted({(s,hex(v)) for s,v in hits})
        print(f"  PNDT 0x{ff:08X}: {seen}")

if __name__=='__main__': main()
