import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
FAUNA_FLST = 0x00160C97

# Ground truth from in-game DumpSpeciesSlots, planet 0x0003F5A1
GROUND_TRUTH = {
 0x00048A34:[0x00280178,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B898:[0x002634AE,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B899:[0x002634AD,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B89A:[0x00280178,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B89B:[0x002634AD,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B89C:[0x00280178,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B89D:[0x001699B2,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B89E:[0x00280178,0x0023E90D,0x002634BE,0x002634C2],
 0x0019B89F:[0x00280172,0x0023E90D,0x002634BE,0x002634C2],
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

def build():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    npc={}      # formid -> OBTS bytes (and KWDA set)
    omod_temp={}# omod formid -> NKEY keyword
    omod_under=set() # omod formids granting CCT_Enviro_Underground via NKEY
    flst=[None]
    UNDERGROUND=0x001122A6
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            sd=sig.decode('latin1')
            if sd=='NPC_':
                rd=inflate(d[i+24:i+24+size],flags)
                obts=None; kwda=set()
                for ss,p in subrecords(rd):
                    if ss==b'OBTS': obts=p
                    elif ss==b'KWDA': kwda=set(struct.unpack_from('<%dI'%(len(p)//4),p,0))
                npc[ff]=(obts,kwda)
            elif sd=='OMOD':
                rd=inflate(d[i+24:i+24+size],flags)
                edid=None; dat=None
                for ss,p in subrecords(rd):
                    if ss==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace')
                    elif ss==b'DATA': dat=p
                nkeys=[]
                if dat:
                    idx=0
                    while True:
                        kk=dat.find(b'NKEY',idx)
                        if kk<0: break
                        if kk+8<=len(dat): nkeys.append(struct.unpack_from('<I',dat,kk+4)[0])
                        idx=kk+4
                # store ALL nkeys for temperament OMODs; filter to kw2x at use site
                if edid and edid.startswith('mod_CCT_Temperament_'):
                    omod_temp[ff]=nkeys
                if UNDERGROUND in nkeys:
                    omod_under.add(ff)
            elif sd=='FLST' and ff==FAUNA_FLST:
                rd=inflate(d[i+24:i+24+size],flags)
                flst[0]=rd
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs
    return npc, omod_temp, omod_under, flst[0]

def parse_flst_kw2x(rd):
    lnam=[]; entries=[]; cur=None
    for ss,p in subrecords(rd):
        if ss==b'LNAM': lnam.append(struct.unpack_from('<I',p,0)[0])
        elif ss==b'INAM': cur={'inam':struct.unpack_from('<I',p,0)[0],'ctda':[]}; entries.append(cur)
        elif ss==b'CTDA': cur['ctda'].append(p)
    kw2x={}
    for e in entries:
        marker=lnam[e['inam']]
        for c in e['ctda']:
            func=struct.unpack_from('<H',c,8)[0]; p1=struct.unpack_from('<I',c,0xC)[0]
            comp=struct.unpack_from('<f',c,4)[0]
            if func==560 and comp==1 and 0<=e['inam']<=6:  # temperament markers only
                kw2x[p1]=marker
    return kw2x

def obts_omods(obts):
    """yield omod formids from OBTS 7-byte entries; return None if malformed."""
    if not obts or len(obts)<0x12: return None
    cnt=struct.unpack_from('<I',obts,0)[0]
    if cnt*7 != len(obts)-0x12: return None
    out=[]
    off=0x12
    for _ in range(cnt):
        out.append(struct.unpack_from('<I',obts,off)[0]); off+=7
    return out

def main():
    npc, omod_temp, omod_under, flst = build()
    kw2x = parse_flst_kw2x(flst)
    # filter omod_temp NKEYs to ones present in kw2x
    print("=== temperament keyword -> X marker map (FLST func-560) ===")
    for k,v in sorted(kw2x.items()):
        print(f"   CCT 0x{k:08X} -> X 0x{v:08X}")
    print(f"\n#NPC_={len(npc)}  #temperament-OMOD={len(omod_temp)}  #underground-OMOD={len(omod_under)}")

    def derive(npcid):
        rec=npc.get(npcid)
        if not rec: return None,"NPC missing"
        obts,kwda=rec
        omods=obts_omods(obts)
        if omods is None: return None,"OBTS malformed/absent"
        xs=[]; underground=False
        for o in omods:
            if o in omod_temp:
                for k in omod_temp[o]:
                    if k in kw2x: xs.append(kw2x[k])
            if o in omod_under: underground=True
        s=set()
        # temperament X
        for x in xs: s.add(x)
        # unconditional
        s.add(0x0023E90D)  # AnyResource (func882 always)
        s.add(0x002634C2)  # ActorHealth (no INAM)
        # AnyBiomes: emit if NOT underground
        if not underground: s.add(0x002634BE)
        return s, ("X="+("/".join(f"0x{x:08X}" for x in xs) if xs else "NONE"))+(" UNDERGROUND" if underground else "")

    print("\n=== GROUND TRUTH validation ===")
    allok=True
    for npcid, expected in GROUND_TRUTH.items():
        got, note = derive(npcid)
        exp=set(expected)
        ok = (got==exp)
        allok &= ok
        print(f"  0x{npcid:08X}: {'OK ' if ok else 'FAIL'} {note}")
        if not ok:
            print(f"      expected {sorted(hex(x) for x in exp)}")
            print(f"      got      {sorted(hex(x) for x in (got or set()))}")
    print(f"\nALL GROUND TRUTH MATCH: {allok}")

    # Full coverage over all fauna NPC_ that have a temperament OMOD or are critters
    resolved=0; noX=0; malformed=0; total_with_obts=0
    noX_ids=[]
    for npcid,(obts,kwda) in npc.items():
        omods=obts_omods(obts)
        if omods is None:
            if obts is not None: malformed+=1
            continue
        total_with_obts+=1
        hasX=any(o in omod_temp and any(k in kw2x for k in omod_temp[o]) for o in omods)
        if hasX: resolved+=1
        else:
            noX+=1
            if len(noX_ids)<15: noX_ids.append(npcid)
    print(f"\n=== COVERAGE over all NPC_ ===")
    print(f"  NPC_ total={len(npc)}  with valid OBTS={total_with_obts}  malformed OBTS={malformed}")
    print(f"  resolved X (has temperament OMOD in map)={resolved}")
    print(f"  NO X (OBTS valid but no temperament OMOD)={noX}")
    print(f"  sample no-X NPC ids: {[hex(x) for x in noX_ids]}")

if __name__=='__main__': main()
