"""Precisely decode LCSR entry layout and count PlanetTraitScanTargetLocRef entries
per overlay LCTN. Local self-generated pickle (trusted)."""
import struct, pickle, re
from collections import defaultdict, Counter
PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL,'rb') as f: IDX=pickle.load(f)
records=IDX['records']; subs_cache=IDX['subs_cache']
def ed(fid):
    r=records.get(fid); return r[1] if r else None
def sg(fid):
    r=records.get(fid); return r[0].decode('latin1') if r else '?'

PLANETTRAIT_LOCREF = 0x0027A567

# Determine LCSR entry size: scan known LCTN, LCSR len divisible by candidate sizes.
# From hex: each entry starts with a LocRefType formid. In Med01 (860B):
#   860 / 20 = 43 exactly. Verify by stepping 20 and printing first formid each entry.
def decode_lcsr(p, stride):
    out=[]
    for off in range(0, len(p)-stride+1, stride):
        lrt = struct.unpack_from('<I',p,off)[0]
        ref = struct.unpack_from('<I',p,off+4)[0]
        out.append((lrt, ref, p[off:off+stride].hex()))
    return out

def analyze(fid):
    subs = subs_cache.get(fid)
    if not subs:
        print(f"0x{fid:08X}: no subs"); return None
    res={}
    for ssig,p in subs:
        s=ssig.decode('latin1')
        if s=='LCSR':
            # find stride: try 20; check len%stride==0
            stride=20
            if len(p)%20!=0:
                # try other strides
                for st in (24,16,28,12):
                    if len(p)%st==0: stride=st; break
            entries=decode_lcsr(p,stride)
            lrt_counts=Counter()
            scan_target_refs=[]
            for lrt,ref,hx in entries:
                lrt_counts[lrt]+=1
                if lrt==PLANETTRAIT_LOCREF:
                    scan_target_refs.append((ref,hx))
            res['LCSR']={'len':len(p),'stride':stride,'n':len(entries),
                         'lrt_counts':lrt_counts,'planettrait':scan_target_refs}
    return res

# all trait-20 overlays + the three explicitly requested
targets = {
  0x0010A476:'SentientMicrobialColonies Med01',
  0x0021D242:'SentientMicrobialColonies Med02',
  0x003807B9:'SentientMicrobialColonies Lg01new',
}
print("=== LCSR PlanetTraitScanTargetLocRef counts for trait-20 overlays ===")
for fid,label in targets.items():
    r=analyze(fid)
    if not r or 'LCSR' not in r:
        print(f"  0x{fid:08X} {label}: no LCSR"); continue
    L=r['LCSR']
    pt = L['planettrait']
    print(f"\n  0x{fid:08X} {label}")
    print(f"    LCSR len={L['len']} stride={L['stride']} entries={L['n']}")
    print(f"    PlanetTraitScanTargetLocRef(0x0027A567) entries = {len(pt)}  <<< REQUIRED COUNT")
    for ref,hx in pt:
        print(f"       ref=0x{ref:08X} {ed(ref)}  raw={hx}")
    # top other locreftypes
    top=L['lrt_counts'].most_common(6)
    print(f"    top LocRefTypes: " + ", ".join(f"0x{k:08X}({ed(k)})={v}" for k,v in top))

# Now do ALL overlay LCTN: report planettrait-locref count per overlay
print("\n\n=== ALL OverlayTrait* LCTN: PlanetTraitScanTargetLocRef entry count ===")
allov=[]
for fid,(sig,e) in records.items():
    if sig==b'LCTN' and e and e.startswith('OverlayTrait'):
        allov.append((e,fid))
rowdata=[]
for e,fid in sorted(allov):
    r=analyze(fid)
    cnt = len(r['LCSR']['planettrait']) if (r and 'LCSR' in r) else 0
    rowdata.append((e,fid,cnt))
    print(f"  0x{fid:08X} cnt={cnt}  {e}")
# distribution
from collections import Counter as C
dist=C(c for _,_,c in rowdata)
print(f"\n  COUNT DISTRIBUTION across {len(rowdata)} overlays: {dict(sorted(dist.items()))}")
