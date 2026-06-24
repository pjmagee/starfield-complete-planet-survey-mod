"""Decode overlay LCTN KWDA, check for the trait keyword + size/biome keywords,
and look for the trait->overlay selection link (BIOM/PNDT/FLST). Trusted local pickle."""
import struct, pickle, re
from collections import defaultdict, Counter
PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL,'rb') as f: IDX=pickle.load(f)
records=IDX['records']; subs_cache=IDX['subs_cache']; kwda_of=IDX['kwda_of']
kwd_ref=IDX['kwd_referrers']; containers=IDX['container_formids']
def ed(fid):
    r=records.get(fid); return r[1] if r else None
def sg(fid):
    r=records.get(fid); return r[0].decode('latin1') if r else '?'

TRAIT20=0x00225588
# overlay LCTN KWDA for trait20's three overlays
print("=== overlay LCTN KWDA decode (trait20) ===")
for lfid in (0x0010A476,0x0021D242,0x003807B9):
    kl=kwda_of.get(lfid,[])
    print(f"\n  0x{lfid:08X} {ed(lfid)} KWDA({len(kl)}):")
    for k in kl:
        mark=' <<TRAIT20' if k==TRAIT20 else ''
        print(f"    0x{k:08X} {sg(k)} {ed(k)}{mark}")

# What kinds of keywords sit on overlay LCTNs (sample one)
# Determine: is the trait keyword the discriminator that links planet trait -> overlay?
# Find all LCTN that carry TRAIT20 kywd:
print("\n=== LCTN records carrying PlanetTrait20 keyword (0x00225588) ===")
for rfid,rsig in kwd_ref.get(TRAIT20,[]):
    if rsig==b'LCTN':
        print(f"  0x{rfid:08X} {ed(rfid)}")

# Full referrers of trait20 by record type
print("\n=== all referrers of PlanetTrait20 kywd by sig ===")
c=Counter()
samples=defaultdict(list)
for rfid,rsig in kwd_ref.get(TRAIT20,[]):
    c[rsig.decode('latin1')]+=1
    if len(samples[rsig.decode('latin1')])<8:
        samples[rsig.decode('latin1')].append((rfid,ed(rfid)))
for s,n in c.most_common():
    print(f"  {s}: {n}")
    for fid,e in samples[s]:
        print(f"      0x{fid:08X} {e}")

# SQ_Parent quest: where does locref 0x0027A567 appear, and trait20?
print("\n=== SQ_Parent 0x0007092C : show subrecords containing locref/trait20 ===")
subs=subs_cache.get(0x0007092C)
if subs:
    for ssig,p in subs:
        for o in range(0,len(p)-3):
            v=struct.unpack_from('<I',p,o)[0]
            if v in (0x0027A567,TRAIT20):
                print(f"    {ssig.decode('latin1')}@{o} -> 0x{v:08X} {ed(v)}")
                break
else:
    print("  (SQ_Parent not in subs_cache; only container formid-set was kept)")
