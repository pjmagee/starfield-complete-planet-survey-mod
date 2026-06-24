import struct, zlib
from collections import Counter
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

with open(ESM,'rb') as f: data=f.read()
t=struct.unpack_from('<I',data,4)[0]

# fauna FLST 0x00160C97 func-560 map: CCT_Temperament keyword -> X marker
KW2X={
 0x001699AB:0x002634AF,  # Aggressive
 0x00280174:0x00280172,  # Wary
 0x00280175:0x00280173,  # Foolhardy->Fearless
 0x00169995:0x002634AE,  # AlwaysFlee->Skittish
 0x001699A3:0x001699B2,  # Territorial
 0x00280177:0x00280178,  # Defensive
 0x001699A1:0x002634AD,  # CuriousPeaceful->Peaceful
 0x001122A6:0x002634BE,  # Enviro_Underground -> AnyBiomes (the biome marker condition)
}
TEMP_KW=set(KW2X)&{0x001699AB,0x00280174,0x00280175,0x00169995,0x001699A3,0x00280177,0x001699A1}

# Build OMOD -> NKEY keyword(s) restricted to temperament keywords
omod_temp={}
for sig,ff,rd in topwalk(data,t,{b'OMOD'}):
    if sig!=b'OMOD': continue
    nk=[]
    for ssig,p in subrecords(rd):
        if ssig==b'DATA':
            o=0
            while o+8<=len(p):
                if p[o:o+4]==b'NKEY':
                    nk.append(struct.unpack_from('<I',p,o+4)[0]); o+=8; continue
                o+=1
    tk=[k for k in nk if k in TEMP_KW]
    if tk: omod_temp[ff]=tk

def obts_omods(rd):
    out=[]
    for ssig,p in subrecords(rd):
        if ssig==b'OBTS' and len(p)>=0x12:
            cnt=struct.unpack_from('<I',p,0)[0]
            off=0x12; got=[]
            while off+7<=len(p):
                got.append(struct.unpack_from('<I',p,off)[0]); off+=7
            out.append((cnt, cnt*7==len(p)-0x12, got))
    return out

def fauna_X(rd):
    """resolve X marker from OBTS->OMOD(temperament)->NKEY->KW2X."""
    for cnt,valid,omods in obts_omods(rd):
        for oid in omods:
            tk=omod_temp.get(oid)
            if tk:
                return KW2X[tk[0]]
    return 0

GT_FAUNA={
 0x00048A34:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B898:{0x002634AE,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B899:{0x002634AD,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89A:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89B:{0x002634AD,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89C:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89D:{0x001699B2,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89E:{0x00280178,0x0023E90D,0x002634BE,0x002634C2},
 0x0019B89F:{0x00280172,0x0023E90D,0x002634BE,0x002634C2},
}
npc_map={ff:rd for sig,ff,rd in topwalk(data,t,{b'NPC_'}) if sig==b'NPC_'}

print("=== Fauna derivation vs GT ===")
ok=0
for ff,gt in GT_FAUNA.items():
    rd=npc_map.get(ff)
    if not rd:
        print(f"  0x{ff:08X}: NPC_ NOT FOUND (maybe LVLN/template)"); continue
    X=fauna_X(rd)
    derived={X,0x0023E90D,0x002634BE,0x002634C2} if X else {0x0023E90D,0x002634BE,0x002634C2}
    m=(derived==gt)
    ok+=m
    print(f"  0x{ff:08X} {edid_of(rd)} X={hex(X)} {'OK' if m else 'MISMATCH derived='+str(sorted(hex(x) for x in derived))+' GT='+str(sorted(hex(x) for x in gt))}")
print(f"\n{ok}/{len(GT_FAUNA)} fauna match")

# ---- COVERAGE: all NPC_ that are surveyable (have OBTS), resolution rate ----
print("\n=== Coverage: NPC_ OBTS temperament resolution ===")
tot=0; withobts=0; resolved=0; invalid=0; multi=0
unresolved=[]
for ff,rd in npc_map.items():
    obts=obts_omods(rd)
    if not obts: continue
    withobts+=1
    # any temperament resolvable?
    xs=set()
    for cnt,valid,omods in obts:
        if not valid: invalid+=1
        for oid in omods:
            tk=omod_temp.get(oid)
            if tk: xs.add(KW2X[tk[0]])
    if xs:
        resolved+=1
        if len(xs)>1: multi+=1
print(f"  NPC_ with OBTS: {withobts}")
print(f"  resolved a temperament X: {resolved} ({100*resolved/withobts:.1f}% of OBTS-carrying NPC_)")
print(f"  multi-X (ambiguous): {multi}")
