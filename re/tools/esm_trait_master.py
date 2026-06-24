"""Master resolver: build the full trait<->scan-target<->overlay<->quest table,
decode LCTN LCPR/LCSR/LCID required-count, dump Jemison KWDA, find linking records,
and check static placements. Self-generated local pickle (trusted)."""
import struct, pickle, re
from collections import defaultdict

PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL, 'rb') as f:
    IDX = pickle.load(f)
records       = IDX['records']
edid_to_fid   = IDX['edid_to_fid']
kwda_of       = IDX['kwda_of']
subs_cache    = IDX['subs_cache']
trait_tnam    = IDX['trait_tnam']
kwd_referrers = IDX['kwd_referrers']
placements    = IDX['placements_by_base']
containers    = IDX['container_formids']

def ed(fid):
    r = records.get(fid); return r[1] if r else None
def sg(fid):
    r = records.get(fid); return r[0].decode('latin1') if r else '?'

# trait keyword by NN+baseName
trait_by_nn = {}    # nn(str) -> list of (fid, edid, baseName)
for fid, (sig, e) in records.items():
    if sig == b'KYWD' and e and e.startswith('PlanetTrait') and not e.startswith('PlanetTraitScanTarget'):
        m = re.match(r'^PlanetTrait(\d+)(.+)$', e)
        if m:
            trait_by_nn.setdefault(m.group(1), []).append((fid, e, m.group(2)))

# scan ACTI (the real one, not the Name variant)
scan_acti = []      # (nn, name, fid, edid)
name_acti = {}      # nn+name -> fid (the *Name* variant)
for fid, (sig, e) in records.items():
    if sig == b'ACTI' and e:
        if e.startswith('PlanetTraitScanTargetName'):
            m = re.match(r'^PlanetTraitScanTargetName(\d+)(.+)$', e)
            if m: name_acti[m.group(1)+m.group(2)] = fid
        elif e.startswith('PlanetTraitScanTarget'):
            m = re.match(r'^PlanetTraitScanTarget(\d+)(.+)$', e)
            if m: scan_acti.append((m.group(1), m.group(2), fid, e))

# HSTI keyword by nn+name
hsti = {}
for fid, (sig, e) in records.items():
    if sig == b'KYWD' and e and e.startswith('HandScannerTraitInfo'):
        m = re.match(r'^HandScannerTraitInfo(\d+)(.+)$', e)
        if m: hsti[m.group(1)+m.group(2)] = (fid, e)

# overlay LCTN: name contains the trait base name
overlay = []   # (fid, edid)
for fid, (sig, e) in records.items():
    if sig == b'LCTN' and e and e.startswith('OverlayTrait'):
        overlay.append((fid, e))

# MB quest by NN
mb_qust = {}
for fid, (sig, e) in records.items():
    if sig == b'QUST' and e:
        m = re.match(r'^MB_SurveyTrait(\d+)$', e)
        if m: mb_qust[m.group(1)] = fid

def base_strip(name):  # strip trailing 01/02
    return re.sub(r'\d+$', '', name)

print("=== MASTER TABLE (one row per scan-target ACTI) ===")
print(f"scan-target ACTIs total = {len(scan_acti)}")
distinct_nn = set()
for nn, name, fid, e in sorted(scan_acti, key=lambda r:(int(r[0]), r[1])):
    distinct_nn.add(nn)
    bname = base_strip(name)
    # trait kywd: match NN and (baseName) ; trait kywd name may equal bname
    tk = None
    for tfid, tedid, tname in trait_by_nn.get(nn, []):
        if tname == bname or tname == name:
            tk = (tfid, tedid); break
    if tk is None and trait_by_nn.get(nn):
        tk = (trait_by_nn[nn][0][0], trait_by_nn[nn][0][1])  # fallback first NN
    hk = hsti.get(nn+name)
    nameactifid = name_acti.get(nn+name)
    # overlay LCTNs referencing this trait keyword via KWDA
    ov = []
    if tk:
        for rfid, rsig in kwd_referrers.get(tk[0], []):
            if rsig == b'LCTN':
                ov.append((rfid, ed(rfid)))
    q = mb_qust.get(nn)
    print(f"\nNN={nn} {name}")
    print(f"  scanACTI = 0x{fid:08X} {e}")
    print(f"  traitKYWD= {('0x%08X %s'%(tk[0],tk[1])) if tk else 'NONE'}")
    print(f"  HSTI     = {('0x%08X %s'%(hk[0],hk[1])) if hk else 'NONE'}")
    print(f"  NameACTI = {('0x%08X'%nameactifid) if nameactifid else 'NONE'}")
    print(f"  MB_QUST  = {('0x%08X'%q) if q else 'NONE'}")
    print(f"  overlayLCTN({len(ov)}) = " + ", ".join('0x%08X %s'%(o[0],o[1]) for o in ov))
print(f"\nDISTINCT NN with scan-target = {sorted(distinct_nn, key=int)} ({len(distinct_nn)})")
