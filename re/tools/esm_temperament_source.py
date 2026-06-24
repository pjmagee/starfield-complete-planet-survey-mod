import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
TEMPER = {0x001699AB,0x001699A3,0x001699A1,0x00169995,0x00280174,0x00280175,
          0x00280177,0x00280176,0x001122A6}

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

# For every record type, count how many carry a TEMPER keyword anywhere in their data (aligned u32),
# to find WHERE temperament is actually authored (RACE? CSTY? a component subrecord?)
sig_count = Counter()
race_hits = []   # (formid, edid, [kw])
def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    def walk(d, base):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size], base+i+24); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            rd=inflate(d[i+24:i+24+size], flags)
            found=set(); edid=None; kwda=[]
            for ssig,p in subrecords(rd):
                if ssig==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace')
                if ssig==b'KWDA':
                    kwda=list(struct.unpack_from('<%dI'%(len(p)//4),p,0))
                for off in range(0,len(p)-3,4):
                    v=struct.unpack_from('<I',p,off)[0]
                    if v in TEMPER: found.add((ssig.decode(),v))
            if found:
                s=sig.decode(); sig_count[s]+=1
                if s in ('RACE','NPC_','LVLN','CSTY') and len(race_hits)<25:
                    kws=sorted({v for _,v in found})
                    subs=sorted({ss for ss,_ in found})
                    race_hits.append((s,ff,edid,subs,kws,[k for k in kwda if k in TEMPER]))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs], pos+24)
        pos+=gs
    print("=== record SIGs that reference a CCT_Temperament/Enviro keyword (count) ===")
    for s,c in sig_count.most_common():
        print(f"   {s}: {c}")
    print("\n=== sample RACE/NPC_/LVLN/CSTY hits (sig, formid, edid, subrecsWithKw, kws, KWDA-temper) ===")
    for s,ff,e,subs,kws,inkwda in race_hits:
        print(f"   {s} 0x{ff:08X} {e}")
        print(f"       in-subrecs={subs} kws={[hex(k) for k in kws]} inKWDA={[hex(k) for k in inkwda]}")

if __name__=='__main__': main()
