import struct, zlib
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
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
TEMPER={0x001699AB,0x001699A3,0x001699A1,0x00169995,0x00280174,0x00280175,0x00280177}
def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    samples=[]; cct_edids=0; total_omod=0
    subsig_hist={}
    def walk(d):
        nonlocal cct_edids,total_omod
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if sig==b'OMOD':
                total_omod+=1
                rd=inflate(d[i+24:i+24+size],flags)
                edid=None; subs=[]; tempkw=[]
                for ss,p in subrecords(rd):
                    subs.append(ss.decode('latin1'))
                    if ss==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace')
                    # scan any aligned u32 in this subrec for a temper kw
                    for o in range(0,len(p)-3,4):
                        v=struct.unpack_from('<I',p,o)[0]
                        if v in TEMPER: tempkw.append((ss.decode('latin1'),o,v))
                if edid and 'CCT_Temperament' in edid:
                    cct_edids+=1
                    for s in subs: subsig_hist[s]=subsig_hist.get(s,0)+1
                    if len(samples)<12:
                        samples.append((ff,edid,subs,tempkw,rd))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs
    print(f"total OMOD={total_omod}  OMOD with 'CCT_Temperament' in EDID={cct_edids}")
    print(f"subrec sig histogram for those OMODs: {subsig_hist}")
    for ff,edid,subs,tempkw,rd in samples:
        print(f"\n  OMOD 0x{ff:08X} {edid}")
        print(f"    subs={subs}")
        print(f"    temperkw hits (sub,off,val)={[(s,o,hex(v)) for s,o,v in tempkw]}")
        # dump first 80 bytes hex
        print(f"    hex0..96: {rd[:96].hex()}")
if __name__=='__main__': main()
