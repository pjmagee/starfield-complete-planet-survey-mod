"""Tasks 3,4,5: Jemison KWDA; linking records (FLST/COBJ/GBFM/QUST with both trait kywd
& scan ACTI / locref); static placement check. Local self-generated pickle (trusted)."""
import struct, pickle, re
from collections import defaultdict
PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL,'rb') as f: IDX=pickle.load(f)
records=IDX['records']; subs_cache=IDX['subs_cache']; kwda_of=IDX['kwda_of']
containers=IDX['container_formids']; placements=IDX['placements_by_base']
def ed(fid):
    r=records.get(fid); return r[1] if r else None
def sg(fid):
    r=records.get(fid); return r[0].decode('latin1') if r else '?'

TRAIT20_KYWD = 0x00225588
SCAN20_ACTI  = 0x0021B250
LOCREF       = 0x0027A567

# ---------- TASK 3: Jemison KWDA ----------
print("=== TASK3: JemisonPlanetData 0x0003F5A1 KWDA ===")
jem = 0x0003F5A1
print(f"  record sig={sg(jem)} edid={ed(jem)}")
kl = kwda_of.get(jem, [])
print(f"  KWDA count = {len(kl)}")
traits=[]
for k in kl:
    e = ed(k)
    mark = ''
    if e and e.startswith('PlanetTrait') and not e.startswith('PlanetTraitScanTarget'):
        traits.append((k,e)); mark=' <<TRAIT'
    print(f"    0x{k:08X} {sg(k)} {e}{mark}")
print(f"  --> Jemison PlanetTrait* traits: {[(hex(k),e) for k,e in traits]}")
print(f"  --> Jemison has trait20 (0x{TRAIT20_KYWD:08X})? {TRAIT20_KYWD in kl}")

# ---------- TASK 4: linking records ----------
print("\n=== TASK4: containers referencing BOTH trait20 kywd AND (scanACTI20 OR locref) ===")
hits=[]
for fid,fset in containers.items():
    s=set(fset)
    has_trait = TRAIT20_KYWD in s
    has_scan  = SCAN20_ACTI in s
    has_locref= LOCREF in s
    if has_trait and (has_scan or has_locref):
        hits.append((fid, has_scan, has_locref))
if not hits:
    print("  NONE: no FLST/COBJ/GBFM/QUST contains both trait20 kywd and scanACTI20/locref")
for fid,hs,hl in hits:
    print(f"  {sg(fid)} 0x{fid:08X} {ed(fid)} scanACTI={hs} locref={hl}")

# Broader: any container with trait20 kywd at all
print("\n  containers referencing trait20 kywd 0x00225588 (any):")
for fid,fset in containers.items():
    if TRAIT20_KYWD in set(fset):
        print(f"    {sg(fid)} 0x{fid:08X} {ed(fid)}")
print("\n  containers referencing scanACTI20 0x0021B250 (any):")
for fid,fset in containers.items():
    if SCAN20_ACTI in set(fset):
        print(f"    {sg(fid)} 0x{fid:08X} {ed(fid)}")

# ---------- TASK 5: static placements of scan-target ACTI ----------
print("\n=== TASK5: static REFR/ACHR/PHZD placements with NAME -> any PlanetTraitScanTarget ACTI ===")
scan_actis = {fid for fid,(sig,e) in records.items()
              if sig==b'ACTI' and e and e.startswith('PlanetTraitScanTarget') and not e.startswith('PlanetTraitScanTargetName')}
print(f"  scan-target ACTI bases tracked = {len(scan_actis)}")
any_static=False
for base in sorted(scan_actis):
    cnt = placements.get(base,0)
    if cnt>0:
        any_static=True
        print(f"  STATIC PLACEMENT: 0x{base:08X} {ed(base)} placed {cnt}x")
if not any_static:
    print("  NONE: no static REFR/ACHR/PHZD NAME references any PlanetTraitScanTarget* ACTI base")
    print("  --> scan-target world refs are RUNTIME-SPAWNED (confirmed)")

# Also check the *Name* variant ACTIs just in case
nameactis = {fid for fid,(sig,e) in records.items()
             if sig==b'ACTI' and e and e.startswith('PlanetTraitScanTargetName')}
nstatic=sum(1 for b in nameactis if placements.get(b,0)>0)
print(f"  (Name-variant ACTI static placements: {nstatic})")
