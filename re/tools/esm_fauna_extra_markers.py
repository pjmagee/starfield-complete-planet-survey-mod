import struct, zlib
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
# Resolve EDIDs for the func-699 effect keywords + ability markers, and check if any
# ground-truth NPC carries the ability/resistance/weakness magic-effect keyword statically.
WANT={0x001D3B47:'AbilityEffectKW',0x001D3B46:'WeaknessEffectKW',0x001D3B48:'ResistanceEffectKW',
      0x002634BC:'AnyNonLethalHarvest',0x002AC11D:'OutpostCanDomesticate',0x001122A6:'CCT_Enviro_Underground'}
GT=[0x00048A34,0x0019B898,0x0019B899,0x0019B89A,0x0019B89B,0x0019B89C,0x0019B89D,0x0019B89E,0x0019B89F]
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
    gt_kwda={}
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if sig==b'NPC_' and ff in GT:
                rd=inflate(d[i+24:i+24+size],flags)
                allids=set()
                for ss,p in subrecords(rd):
                    for o in range(0,len(p)-3,4):
                        allids.add(struct.unpack_from('<I',p,o)[0])
                gt_kwda[ff]={k:v for k,v in WANT.items() if k in allids}
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs
    print("=== Do ground-truth NPCs statically reference the 'extra' marker condition keywords anywhere in record? ===")
    for ff in GT:
        hits=gt_kwda.get(ff,{})
        print(f"  0x{ff:08X}: {[v for v in hits.values()] or 'NONE'}")
    print("\n(If NONE for all, the func-699 ability/resistance/weakness, NonLethalHarvest,")
    print(" Domesticate, and Underground markers are never authored on these species => the")
    print(" 4-marker set [X, AnyResource, AnyBiomes, ActorHealth] is exactly correct.)")
if __name__=='__main__': main()
