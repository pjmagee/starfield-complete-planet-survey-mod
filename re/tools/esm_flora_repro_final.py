import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

REPRO_IDS = {0x0023E907:1,0x0023E906:2,0x0023E908:3,0x00171869:4,0x00171868:5,0x00171867:6,0x00171866:7}
REPRO_SET=set(REPRO_IDS)

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

def topwalk(data, t, labels):
    pos=24+t; n=len(data); out=[]
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            out.append((sig, ff, inflate(d[i+24:i+24+size], flags)))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; lab=data[pos+8:pos+12]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0 and lab in labels: walk(data[pos+24:pos+gs])
        pos+=gs
    return out

def edid_of(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'EDID': return p.rstrip(b'\x00').decode('latin1','replace')
    return None

def full_of(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'FULL' and len(p)==4: return struct.unpack_from('<I',p,0)[0]
    return None

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]

    flor=topwalk(data,t,{b'FLOR'})

    # A) Across ALL surveyable FloraBiome* FLOR: PRPS-N distribution, and whether ANY static field
    #    carries a reproduction KYWD form-id directly.
    prps_hist=Counter()
    repro_kw_anyfield=Counter()
    samples=[]
    for sig,ff,rd in flor:
        if sig!=b'FLOR': continue
        e=edid_of(rd) or ''
        if not e.startswith('FloraBiome'): continue
        prps_ns=[]; kwda=[]; repro_in_fields=[]
        for ssig,p in subrecords(rd):
            if ssig==b'PRPS':
                off=0
                while off+12<=len(p):
                    avif,val,extra=struct.unpack_from('<IfI',p,off)
                    if avif==0x0023E905: prps_ns.append(round(val))
                    off+=12
            elif ssig==b'KWDA':
                kwda=list(struct.unpack_from('<%dI'%(len(p)//4),p,0))
            # scan every subrecord for a reproduction kywd id appearing as aligned u32
            for o in range(0,len(p)-3,1):
                v=struct.unpack_from('<I',p,o)[0]
                if v in REPRO_SET: repro_in_fields.append((ssig.decode('latin1'),o,v))
        for n in prps_ns: prps_hist[n]+=1
        kw_repro=[k for k in kwda if k in REPRO_SET]
        for rec in repro_in_fields: repro_kw_anyfield[rec[0]]+=1
        if len(samples)<14:
            samples.append((ff,e,prps_ns,[hex(k) for k in kwda],kw_repro,repro_in_fields))

    print("=== PRPS-N distribution across FloraBiome* FLOR ===")
    for n,c in sorted(prps_hist.items()): print(f"   N={n}: {c}   (-> KYWD 0x{[k for k,v in REPRO_IDS.items() if v==n][0]:08X})" if n in REPRO_IDS.values() else f"   N={n}: {c}")
    print("\n=== which subrecords carry a reproduction KYWD id directly ===")
    if not repro_kw_anyfield: print("   NONE — no static FLOR field carries a PlantReproduction0N keyword id.")
    for s,c in repro_kw_anyfield.most_common(): print(f"   {s}: {c}")

    print("\n=== sample FloraBiome FLOR ===")
    for ff,e,prps,kwda,kwrep,rfields in samples:
        print(f"  0x{ff:08X} {e}")
        print(f"      PRPS-N={prps} reproKW-in-KWDA={[hex(k) for k in kwrep]} reproKW-in-fields={rfields}")
        print(f"      KWDA={kwda}")

    # B) Dump FLST 0x00160C96 (HandScannerPlantKeywords) reproduction conditions.
    print("\n\n=== FLST 0x00160C96 HandScannerPlantKeywords (reproduction markers + CTDA leaves) ===")
    for sig,ff,rd in topwalk(data,t,{b'FLST'}):
        if sig!=b'FLST' or ff!=0x00160C96: continue
        lnam=[]; curidx=-1
        for ssig,p in subrecords(rd):
            if ssig==b'EDID': print(f"   EDID={p.rstrip(chr(0).encode()).decode('latin1')}")
            elif ssig==b'LNAM' and len(p)>=4:
                m=struct.unpack_from('<I',p,0)[0]; lnam.append(m)
            elif ssig==b'INAM' and len(p)>=4:
                curidx=struct.unpack_from('<I',p,0)[0]
            elif ssig==b'CTDA' and len(p)>=32 and 0<=curidx<len(lnam):
                func=struct.unpack_from('<H',p,0x08)[0]; param1=struct.unpack_from('<I',p,0x0C)[0]
                marker=lnam[curidx]
                if marker in REPRO_SET or param1 in REPRO_SET:
                    print(f"   marker 0x{marker:08X} (N={REPRO_IDS.get(marker,'?')}) <- func {func} param1 0x{param1:08X}")
        print(f"   total LNAM markers: {len(lnam)}")
        repro_markers=[(i,m) for i,m in enumerate(lnam) if m in REPRO_SET]
        print(f"   reproduction markers in LNAM: {[(i,hex(m),REPRO_IDS[m]) for i,m in repro_markers]}")

if __name__=='__main__': main()
