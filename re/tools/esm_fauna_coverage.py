import struct, zlib
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
FAUNA_FLST=0x00160C97
UNDERGROUND=0x001122A6
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
def omod_nkeys(dat):
    out=[]; idx=0
    while True:
        k=dat.find(b'NKEY',idx)
        if k<0: break
        if k+8<=len(dat): out.append(struct.unpack_from('<I',dat,k+4)[0])
        idx=k+4
    return out
def obts_omods(obts):
    if not obts or len(obts)<0x12: return None
    cnt=struct.unpack_from('<I',obts,0)[0]
    if cnt*7 != len(obts)-0x12: return None
    out=[]; off=0x12
    for _ in range(cnt):
        out.append(struct.unpack_from('<I',obts,off)[0]); off+=7
    return out
def parse_kw2x(rd):
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
            if func==560 and comp==1 and 0<=e['inam']<=6: kw2x[p1]=marker
    return kw2x

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    npc={}; omod_edid={}; omod_temp={}; omod_under=set(); flst=[None]
    pndt_species=set()  # all formids referenced in any PNDT record (planet fauna lists)
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            sd=sig.decode('latin1')
            if sd=='NPC_':
                rd=inflate(d[i+24:i+24+size],flags)
                edid=None; obts=None
                for ss,p in subrecords(rd):
                    if ss==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace')
                    elif ss==b'OBTS': obts=p
                npc[ff]=(edid,obts)
            elif sd=='OMOD':
                rd=inflate(d[i+24:i+24+size],flags)
                ed=None; dat=None
                for ss,p in subrecords(rd):
                    if ss==b'EDID': ed=p.rstrip(b'\x00').decode('latin1','replace')
                    elif ss==b'DATA': dat=p
                if ed: omod_edid[ff]=ed
                nk=omod_nkeys(dat) if dat else []
                if ed and ed.startswith('mod_CCT_Temperament_'): omod_temp[ff]=nk
                if UNDERGROUND in nk: omod_under.add(ff)
            elif sd=='FLST' and ff==FAUNA_FLST:
                flst[0]=inflate(d[i+24:i+24+size],flags)
            elif sd=='PNDT':
                rd=inflate(d[i+24:i+24+size],flags)
                # scan all aligned u32 for npc ids later; collect raw
                for ss,p in subrecords(rd):
                    for o in range(0,len(p)-3,4):
                        pndt_species.add(struct.unpack_from('<I',p,o)[0])
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs
    kw2x=parse_kw2x(flst[0])

    def deriveX(obts):
        omods=obts_omods(obts)
        if omods is None: return None,None  # malformed
        for o in omods:
            if o in omod_temp:
                for k in omod_temp[o]:
                    if k in kw2x: return kw2x[k],omods
        return 0,omods  # valid OBTS, no temperament

    # Universe = NPC_ that are real fauna: have OBTS with a CCT temperament-class structure.
    # Approximate "fauna" = NPC_ whose OBTS references any mod_CCT_ OMOD.
    fauna=[]; nonfauna=0
    for ff,(edid,obts) in npc.items():
        omods=obts_omods(obts)
        if omods is None: nonfauna+=1; continue
        is_cct=any(omod_edid.get(o,'').startswith('mod_CCT_') for o in omods)
        if is_cct: fauna.append(ff)
    print(f"NPC_ total={len(npc)}  CCT-fauna (OBTS w/ mod_CCT_)={len(fauna)}")

    resolved=0; nox=[]
    for ff in fauna:
        edid,obts=npc[ff]
        x,_=deriveX(obts)
        if x: resolved+=1
        else: nox.append((ff,edid))
    print(f"  fauna resolving X = {resolved}/{len(fauna)} = {resolved/len(fauna)*100:.1f}%")
    print(f"  fauna NO X = {len(nox)}")
    print(f"  no-X fauna EDIDs (first 40):")
    for ff,edid in nox[:40]:
        # what temperament-class OMODs DOES it reference?
        omods=obts_omods(npc[ff][1]) or []
        tnames=[omod_edid.get(o,'') for o in omods if 'Temperament' in omod_edid.get(o,'')]
        print(f"     0x{ff:08X} {edid}   temperOMODs={tnames}")

    # Underground split check: how many fauna are underground?
    ug=0
    for ff in fauna:
        omods=obts_omods(npc[ff][1]) or []
        if any(o in omod_under for o in omods): ug+=1
    print(f"\n  fauna with Underground enviro (would get ActorUnderground, NOT AnyBiomes) = {ug}")

    # find Beetle Grazer
    print("\n=== BEETLE GRAZER lookup ===")
    for ff,(edid,obts) in npc.items():
        if edid and 'eetle' in edid and 'raz' in edid.lower():
            x,omods=deriveX(obts)
            print(f"   0x{ff:08X} {edid}  X={hex(x) if x else 'NONE(0)'}")
            if omods:
                tnames=[omod_edid.get(o,'') for o in omods if 'Temperament' in omod_edid.get(o,'')]
                print(f"       temperOMODs={tnames}")
if __name__=='__main__': main()
