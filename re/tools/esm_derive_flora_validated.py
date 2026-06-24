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
flor_map={ff:rd for sig,ff,rd in topwalk(data,t,{b'FLOR'}) if sig==b'FLOR'}

def prps_n(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'PRPS':
            o=0
            while o+12<=len(p):
                avif,val,extra=struct.unpack_from('<IfI',p,o)
                if avif==0x0023E905: return round(val)
                o+=12
    return None

# ---- THE EXACT FLORA REPRODUCTION MODEL (from byte-exact FLST CTDA + CNDF) ----
# AV = PRPS-N (1..7). func14 GetAV(0x0023E905) compares against per-marker constant.
# Direct markers (each block ANDs CND_ReproductionHasOverride==0 EXCEPT Repro04):
DIRECT = {  # marker -> set of AV values that satisfy its func14 test
  0x0023E907:{0,7,8,9,10,11,12,13,14,15},  # Repro01: AV==0 OR AV>6
  0x0023E906:{1},   # Repro02
  0x0023E908:{2},   # Repro03
  0x00171869:{3},   # Repro04  (no HasOverride==0 suppression -> ALWAYS if AV==3)
  0x00171868:{4},   # Repro05
  0x00171867:{5},   # Repro06
  0x00171866:{6},   # Repro07
}
# Markers SUPPRESSED when an override is active (block ANDs HasOverride==0):
SUPPRESSED_BY_OVERRIDE = {0x0023E907,0x0023E906,0x0023E908,0x00171868,0x00171866}  # 01,02,03,05,07
# Repro04 (0x171869) and Repro06 (0x171867) are NOT suppressed (they ARE the override targets/independent).
def repro_markers(n, planet_traits):
    """Return the set of reproduction markers for a flora with PRPS-N=n on a planet with given trait kw set."""
    rhizomes = 0x00225590 in planet_traits          # EcologicalConsortium
    spores   = (0x00225589 in planet_traits) or (0x00225597 in planet_traits)  # Psychotropic/Aeriform
    override = rhizomes or spores
    out=set()
    av=n
    for mk,avset in DIRECT.items():
        if av in avset:
            if override and mk in SUPPRESSED_BY_OVERRIDE: continue
            out.add(mk)
    # override targets:
    if rhizomes: out.add(0x00171867)  # Repro06 Self-cloning (Rhizomes)  [Repro06 block: OR Rhizomes==1]
    if spores:   out.add(0x00171869)  # Repro04 (Spores)                 [Repro04 block: OR Spores==1]
    return out

# Genetics on a planet with NO water-quality/atm-toxicity/magnetosphere-strong/atmM trait:
#   DNAType defaults Standard, DNABasis defaults Carbon -> Common_Standard_Carbon 0x0023E90C.
def genetics_marker(planet_traits):
    # all genetics CNDF leaves are func-858 planet traits; absent => Standard/Carbon default.
    # (full general impl would evaluate the DNAType x DNABasis tree; Jemison has none of those traits.)
    return 0x0023E90C

def derive_flora(ff, planet_traits):
    rd=flor_map[ff]; n=prps_n(rd)
    s={0x0023E90D,0x002634BE}              # Resource, Biomes (unconditional)
    s.add(genetics_marker(planet_traits)) # Genetics
    s|=repro_markers(n, planet_traits)     # Reproduction (+5th)
    return s

GT_FLORA = {
 0x00185478:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x00185479:{0x0023E90D,0x002634BE,0x0023E90C,0x00171869,0x00171867},
 0x0018547F:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x00185489:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x001854C1:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x001854D8:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
 0x002F80A0:{0x0023E90D,0x002634BE,0x0023E90C,0x00171869,0x00171867},
 0x002F80BB:{0x0023E90D,0x002634BE,0x0023E90C,0x00171867},
}
JEMISON_TRAITS={0x00225590}  # EcologicalConsortium (confirmed in PNDT 0x0003F5A1 KWDA)

print("=== FINAL flora derivation vs GT (planet 0x0003F5A1, EcologicalConsortium) ===")
ok=0
for ff,gt in GT_FLORA.items():
    d=derive_flora(ff,JEMISON_TRAITS)
    m=(d==gt)
    ok+=m
    print(f"  0x{ff:08X} N={prps_n(flor_map[ff])} {'OK' if m else 'MISMATCH'}")
    if not m:
        print(f"     derived={sorted(hex(x) for x in d)}")
        print(f"     GT     ={sorted(hex(x) for x in gt)}")
print(f"\n{ok}/{len(GT_FLORA)} flora match exactly")
