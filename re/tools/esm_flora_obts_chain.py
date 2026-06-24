import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

# HandScannerPlantReproduction0N keyword ids resolved by EDID at runtime; also keep the known one.
REPRO_KNOWN = {0x00171867, 0x00171869}

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

def walk_groups(data, t, labels):
    """yield (sig, formid, rd) for records in top groups whose label in labels."""
    pos=24+t; n=len(data)
    out=[]
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            rd=inflate(d[i+24:i+24+size], flags)
            out.append((sig, ff, rd))
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

def obts_omods(rd):
    omods=[]
    for ssig,p in subrecords(rd):
        if ssig==b'OBTS' and len(p)>=0x12:
            cnt=struct.unpack_from('<I',p,0)[0]
            off=0x12
            while off+7<=len(p):
                omods.append(struct.unpack_from('<I',p,off)[0]); off+=7
    return omods

def prps_repro(rd, repro_ids):
    # return list of N values where AVIF==0x0023E905
    vals=[]
    for ssig,p in subrecords(rd):
        if ssig==b'PRPS':
            off=0
            while off+12<=len(p):
                avif,val,extra=struct.unpack_from('<IfI',p,off)
                if avif==0x0023E905: vals.append(round(val))
                off+=12
    return vals

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]

    # 1) resolve PlantReproduction0N keyword ids
    repro_ids={}  # N -> id
    for sig,ff,rd in walk_groups(data,t,{b'KYWD'}):
        if sig!=b'KYWD': continue
        e=edid_of(rd)
        if e and e.startswith('HandScannerPlantReproduction0') and len(e)>len('HandScannerPlantReproduction0'):
            c=e[len('HandScannerPlantReproduction0')]
            if c.isdigit(): repro_ids[int(c)]=ff
    print("=== PlantReproduction0N keyword ids ===")
    for n in sorted(repro_ids): print(f"   N={n} -> 0x{repro_ids[n]:08X}")
    repro_set=set(repro_ids.values())

    # 2) build OMOD meta: id -> (edid, nkeys[])
    print("\n[building OMOD table...]")
    omod_meta={}
    for sig,ff,rd in walk_groups(data,t,{b'OMOD'}):
        if sig!=b'OMOD': continue
        e=edid_of(rd); nkeys=[]
        for ssig,p in subrecords(rd):
            if ssig==b'DATA':
                for o in range(0,len(p)-7):
                    if p[o:o+4]==b'NKEY':
                        nkeys.append(struct.unpack_from('<I',p,o+4)[0])
        omod_meta[ff]=(e,nkeys)
    print(f"   {len(omod_meta)} OMODs")

    # 3) Walk all FLOR. For each, get EDID, PRPS-N, OBTS->OMOD edids+nkeys.
    #    Classify: does any OBTS OMOD have a reproduction-keyword NKEY? Does any OMOD EDID hint reproduction?
    print("\n[walking FLOR...]")
    flor_total=0
    flor_with_obts=0
    obts_omod_with_repro_nkey=0
    omod_edid_prefix_hist=Counter()
    nkey_in_obts_hist=Counter()
    repro_nkey_examples=[]
    # also: do any OMOD NKEY values equal a reproduction keyword across ALL omods (not just FLOR-referenced)?
    omods_granting_repro=[(oid,m) for oid,m in omod_meta.items() if any(k in repro_set for k in m[1])]

    flor_recs=[r for r in walk_groups(data,t,{b'FLOR'}) if r[0]==b'FLOR']
    for sig,ff,rd in flor_recs:
        flor_total+=1
        e=edid_of(rd)
        omods=obts_omods(rd)
        if omods: flor_with_obts+=1
        granted_repro=[]
        for oid in omods:
            m=omod_meta.get(oid)
            if not m: continue
            oe,nkeys=m
            if oe:
                # capture EDID prefix up to 4th underscore
                parts=oe.split('_'); omod_edid_prefix_hist['_'.join(parts[:3])]+=1
            for k in nkeys:
                nkey_in_obts_hist[k]+=1
                if k in repro_set: granted_repro.append((oid,oe,k))
        if granted_repro:
            obts_omod_with_repro_nkey+=1
            if len(repro_nkey_examples)<10:
                repro_nkey_examples.append((ff,e,granted_repro,prps_repro(rd,repro_set)))

    print(f"\n=== FLOR summary ===")
    print(f"  FLOR total={flor_total} withOBTS={flor_with_obts}")
    print(f"  FLOR whose OBTS-OMOD grants a reproduction keyword via NKEY: {obts_omod_with_repro_nkey}")
    print(f"\n  Total OMODs in file whose NKEY is a reproduction keyword: {len(omods_granting_repro)}")
    for oid,m in omods_granting_repro[:20]:
        print(f"     OMOD 0x{oid:08X} EDID={m[0]} NKEY={[hex(k) for k in m[1]]}")

    print(f"\n=== OBTS-OMOD EDID prefix histogram (top 30) ===")
    for p,c in omod_edid_prefix_hist.most_common(30):
        print(f"   {c:5d}  {p}")

    print(f"\n=== NKEY ids seen on FLOR OBTS-OMODs (top 30) ===")
    for k,c in nkey_in_obts_hist.most_common(30):
        flag=" <== REPRODUCTION" if k in repro_set else ""
        print(f"   {c:5d}  0x{k:08X}{flag}")

    print(f"\n=== Examples of FLOR whose OBTS grants reproduction ===")
    for ff,e,gr,prps in repro_nkey_examples:
        print(f"   FLOR 0x{ff:08X} {e}  PRPS-N={prps}")
        for oid,oe,k in gr:
            print(f"       via OMOD 0x{oid:08X} {oe} -> repro kw 0x{k:08X}")

if __name__=='__main__': main()
