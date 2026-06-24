import struct, zlib
from collections import Counter

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

# content keywords that the CNDF/FLST leaves test
LEAF = {
 0x001699AB:"CCT_Temperament_Aggressive",0x001699A3:"CCT_Temperament_Territorial",
 0x001699A1:"CCT_Temperament_CuriousPeaceful",0x00169995:"CCT_Temperament_AlwaysFlee",
 0x00280174:"CCT_Temperament_Wary",0x00280175:"CCT_Temperament_Foolhardy",
 0x00280177:"CCT_Temperament_Defensive",0x001122A6:"CCT_Enviro_Underground",
 0x00295EB5:"?leaf858a",0x00295EA9:"?leaf858b",0x00279292:"?leaf858c",0x00225590:"?leaf858d",
 0x002CC9F5:"?critter560",0x00280176:"CCT_Temperament_?",
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

# resolve leaf edids + record types AND scan FLOR/NPC_ for whether KWDA contains any LEAF kw
edid={}
flor_with_leaf=[]; npc_with_leaf=[]
flor_total=[0]; npc_total=[0]
flor_kw_hist=Counter(); npc_kw_hist=Counter()

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    leaf=set(LEAF)
    def walk(d, base, mode):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size], base+i+24, mode); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            rd=inflate(d[i+24:i+24+size], flags)
            if ff in leaf:
                e=None
                for ssig,p in subrecords(rd):
                    if ssig==b'EDID': e=p.rstrip(b'\x00').decode('latin1','replace'); break
                edid[ff]=(sig.decode(),e)
            if mode in ('FLOR','NPC_'):
                myedid=None; kwda=[]
                for ssig,p in subrecords(rd):
                    if ssig==b'EDID': myedid=p.rstrip(b'\x00').decode('latin1','replace')
                    elif ssig==b'KWDA': kwda=list(struct.unpack_from('<%dI'%(len(p)//4),p,0))
                if mode=='FLOR':
                    flor_total[0]+=1
                    lk=[k for k in kwda if k in leaf]
                    for k in lk: flor_kw_hist[k]+=1
                    if lk and len(flor_with_leaf)<6: flor_with_leaf.append((ff,myedid,lk))
                else:
                    npc_total[0]+=1
                    lk=[k for k in kwda if k in leaf]
                    for k in lk: npc_kw_hist[k]+=1
                    if lk and len(npc_with_leaf)<6: npc_with_leaf.append((ff,myedid,lk))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; lab=data[pos+8:pos+12]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0:
            mode = lab.decode() if lab in (b'FLOR',b'NPC_') else None
            walk(data[pos+24:pos+gs], pos+24, mode)
        pos+=gs

    print("=== leaf form resolution ===")
    for fid in LEAF:
        sig,e=edid.get(fid,('?','NOT FOUND'))
        print(f"  0x{fid:08X} {sig:5s} {e}")
    print(f"\n=== FLOR total={flor_total[0]} ; with a leaf-kw in KWDA: {sum(flor_kw_hist.values())} hits ===")
    for k,c in flor_kw_hist.most_common():
        print(f"   {c:5d}x 0x{k:08X} {LEAF.get(k,'?')}")
    for ff,e,lk in flor_with_leaf:
        print(f"   eg FLOR 0x{ff:08X} {e}: "+",".join(f"0x{x:08X}" for x in lk))
    print(f"\n=== NPC_ total={npc_total[0]} ; with a leaf-kw in KWDA: {sum(npc_kw_hist.values())} hits ===")
    for k,c in npc_kw_hist.most_common():
        print(f"   {c:5d}x 0x{k:08X} {LEAF.get(k,'?')}")
    for ff,e,lk in npc_with_leaf:
        print(f"   eg NPC_ 0x{ff:08X} {e}: "+",".join(f"0x{x:08X}" for x in lk))

if __name__=='__main__': main()
