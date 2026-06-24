"""Resolve the trait/scan-target relationships from the index pickle."""
import struct, pickle, re
from collections import defaultdict

PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL, 'rb') as f:
    IDX = pickle.load(f)

records          = IDX['records']            # fid -> (sig:bytes, edid)
edid_to_fid      = IDX['edid_to_fid']
kwda_of          = IDX['kwda_of']
subs_cache       = IDX['subs_cache']         # fid -> [(ssig:bytes, payload:bytes)]
trait_tnam       = IDX['trait_tnam']
kwd_referrers    = IDX['kwd_referrers']      # kywd fid -> [(referrer fid, sig)]
placements       = IDX['placements_by_base'] # base fid -> count
containers       = IDX['container_formids']  # fid -> [formids]

def sig_of(fid):
    r = records.get(fid)
    return r[0].decode('latin1') if r else '?'
def edid_of(fid):
    r = records.get(fid)
    return r[1] if r else None

# Build edid->fid maps by category
trait_kywd = {}     # NNName -> (fid, edid)   e.g. PlanetTrait20SentientMicrobialColonies
scan_acti  = {}     # NNName -> (fid, edid)
hsti_kywd  = {}     # NNName -> (fid, edid)
overlay_lctn = defaultdict(list)  # Name(no NN, base trait name) -> [(fid, edid)]
mb_qust    = {}     # NN -> (fid, edid)

re_trait   = re.compile(r'^PlanetTrait(\d+)(.+)$')
re_scan    = re.compile(r'^PlanetTraitScanTarget(\d+)(.+)$')
re_hsti    = re.compile(r'^HandScannerTraitInfo(\d+)(.+?)(\d*)$')
re_overlay = re.compile(r'^OverlayTrait(.+?)(Sm|Med|Lg|Small|Medium|Large)?(\d*)Location$')
re_mb      = re.compile(r'^MB_SurveyTrait(\d+)$')

for fid, (sig, ed) in records.items():
    if not ed: continue
    sigs = sig.decode('latin1')
    if sigs == 'KYWD':
        m = re_scan.match(ed)  # not a kywd normally
        if ed.startswith('PlanetTraitScanTarget'):
            pass
        elif ed.startswith('PlanetTrait'):
            m = re_trait.match(ed)
            if m:
                trait_kywd[m.group(1)+m.group(2)] = (fid, ed, m.group(1), m.group(2))
        elif ed.startswith('HandScannerTraitInfo'):
            m = re_hsti.match(ed)
            if m:
                hsti_kywd[ed] = (fid, ed)
    elif sigs == 'ACTI':
        if ed.startswith('PlanetTraitScanTarget'):
            m = re_scan.match(ed)
            if m:
                scan_acti[m.group(1)+m.group(2)] = (fid, ed, m.group(1), m.group(2))
    elif sigs == 'LCTN':
        if ed.startswith('OverlayTrait'):
            overlay_lctn[ed].append((fid, ed))
    elif sigs == 'QUST':
        m = re_mb.match(ed)
        if m:
            mb_qust[m.group(1)] = (fid, ed)

print(f"=== COUNTS ===")
print(f"PlanetTrait<NN> KYWD: {len(trait_kywd)}")
print(f"PlanetTraitScanTarget ACTI: {len(scan_acti)}")
print(f"HandScannerTraitInfo KYWD: {len(hsti_kywd)}")
print(f"OverlayTrait*Location LCTN: {sum(len(v) for v in overlay_lctn.values())}")
print(f"MB_SurveyTrait QUST: {len(mb_qust)}")

# === TASK 1: full table joining by NNName suffix ===
# scan-target ACTIs are the authoritative set of "traits with scan-targets"
print("\n=== TASK1: TRAITS WITH SCAN-TARGETS (joined by NN+Name) ===")
rows = []
scan_keys = sorted(scan_acti.keys(), key=lambda k: int(re.match(r'(\d+)', scan_acti[k][3] if False else k).group(1)) if re.match(r'\d', k) else 0)
# sort by NN
def nn_of(key):
    return scan_acti[key][2]
for key in sorted(scan_acti.keys(), key=lambda k: int(scan_acti[k][2])):
    sfid, sedid, nn, name = scan_acti[key]
    tk = trait_kywd.get(key)
    rows.append((nn, name, sfid, sedid, tk))
print(f"TOTAL traits-with-scan-target = {len(rows)}")
for nn, name, sfid, sedid, tk in rows:
    tfid = tk[0] if tk else None
    tn = trait_tnam.get(tfid) if tfid else None
    print(f"  {nn} {name:35s} ACTI=0x{sfid:08X} TRAIT_KYWD={'0x%08X'%tfid if tfid else 'MISSING'} TNAM={tn}")

if __name__ == '__main__':
    pass
