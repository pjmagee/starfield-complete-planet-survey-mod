"""Dump scan ACTI 20 detail (KWDA/PRPS/FTYP/BFCB) and produce per-trait required-count
ranges across overlay variants. Trusted local pickle."""
import struct, pickle, re
from collections import defaultdict, Counter
PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL,'rb') as f: IDX=pickle.load(f)
records=IDX['records']; subs_cache=IDX['subs_cache']; kwda_of=IDX['kwda_of']
kwd_ref=IDX['kwd_referrers']
def ed(fid):
    r=records.get(fid); return r[1] if r else None
def sg(fid):
    r=records.get(fid); return r[0].decode('latin1') if r else '?'

LOCREF=0x0027A567
# --- ACTI 20 detail ---
print("=== scan-target ACTI 20 (0x0021B250) subrecords ===")
for ssig,p in subs_cache.get(0x0021B250,[]):
    s=ssig.decode('latin1')
    extra=''
    if s in ('BFCB','BFCE'):
        extra=' ['+p.split(b'\x00',1)[0].decode('latin1','replace')+']'
    if s=='KWDA':
        ks=[struct.unpack_from('<I',p,o)[0] for o in range(0,len(p)-3,4)]
        extra=' '+', '.join(f"0x{k:08X}({ed(k)})" for k in ks)
    if s=='FTYP' and len(p)>=4:
        f=struct.unpack_from('<I',p,0)[0]; extra=f" 0x{f:08X}({ed(f)})"
    if s=='PRPS':
        # property entries: [formid(4)][value...]; show formids
        fs=[struct.unpack_from('<I',p,o)[0] for o in range(0,len(p)-3,4)]
        extra=' formids='+','.join(f"0x{x:08X}({ed(x)})" for x in fs if ed(x))
    print(f"  {s}({len(p)}B): {p[:48].hex()}{extra}")

# --- per-trait required count range (recompute LCSR locref counts) ---
def lcsr_locref_count(lfid):
    subs=subs_cache.get(lfid)
    if not subs: return None
    for ssig,p in subs:
        if ssig==b'LCSR':
            stride=20 if len(p)%20==0 else next((st for st in (24,16,28,12) if len(p)%st==0),20)
            n=0
            for off in range(0,len(p)-stride+1,stride):
                if struct.unpack_from('<I',p,off)[0]==LOCREF: n+=1
            return n
    return 0

# map trait kywd -> overlay LCTNs -> counts
print("\n=== per-trait required-count (M) across its overlay variants ===")
trait_kywds={}
for fid,(sig,e) in records.items():
    if sig==b'KYWD' and e and e.startswith('PlanetTrait') and not e.startswith('PlanetTraitScanTarget'):
        m=re.match(r'^PlanetTrait(\d+)(.+)$',e)
        if m: trait_kywds[fid]=(m.group(1),m.group(2),e)
rows=[]
for tfid,(nn,nm,e) in sorted(trait_kywds.items(), key=lambda kv:int(kv[1][0])):
    overlays=[(rfid,ed(rfid)) for rfid,rsig in kwd_ref.get(tfid,[]) if rsig==b'LCTN']
    counts=[(ov,lcsr_locref_count(ov)) for ov,oe in overlays]
    vals=[c for _,c in counts if c is not None]
    rng=f"{min(vals)}..{max(vals)}" if vals else "—"
    rows.append((nn,nm,e,tfid,len(overlays),vals,rng))
    print(f"  NN={nn} {nm:30s} trait=0x{tfid:08X} overlays={len(overlays)} M-per-overlay={vals} range={rng}")

print("\n=== SUMMARY ===")
print(f"distinct numbered traits with scan-target: {len(rows)}")
mins=[min(r[5]) for r in rows if r[5]]
print(f"traits where every overlay has M>=1: {sum(1 for r in rows if r[5] and min(r[5])>=1)}/{len(rows)}")
