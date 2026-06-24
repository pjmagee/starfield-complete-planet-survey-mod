"""Reconcile trait KYWD vs scan-target ACTI naming, and inspect the unmatched ones."""
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

def edid_of(fid):
    r = records.get(fid); return r[1] if r else None
def sig_of(fid):
    r = records.get(fid); return r[0].decode('latin1') if r else '?'

# All KYWD edids starting with PlanetTrait (but not ScanTarget)
print("=== ALL PlanetTrait* KYWD (the trait keywords) ===")
trait_kywds = []
for fid, (sig, ed) in records.items():
    if sig == b'KYWD' and ed and ed.startswith('PlanetTrait') and not ed.startswith('PlanetTraitScanTarget'):
        trait_kywds.append((ed, fid))
for ed, fid in sorted(trait_kywds):
    print(f"  0x{fid:08X} TNAM={trait_tnam.get(fid)}  {ed}")
print(f"  total PlanetTrait* KYWD = {len(trait_kywds)}")

# All scan-target ACTI
print("\n=== ALL PlanetTraitScanTarget* ACTI ===")
scan = []
for fid, (sig, ed) in records.items():
    if sig == b'ACTI' and ed and ed.startswith('PlanetTraitScanTarget'):
        scan.append((ed, fid))
for ed, fid in sorted(scan):
    print(f"  0x{fid:08X}  {ed}")
print(f"  total scan ACTI = {len(scan)}")

# Distinct trait NAMES (strip NN) on both sides
def trait_name(ed):
    m = re.match(r'^PlanetTrait(\d+)(.+)$', ed)
    return (m.group(1), m.group(2)) if m else (None, ed)
def scan_name(ed):
    m = re.match(r'^PlanetTraitScanTarget(\d+)(.+)$', ed)
    return (m.group(1), m.group(2)) if m else (None, ed)

trait_names = {trait_name(ed)[1]: fid for ed, fid in trait_kywds}
scan_names  = defaultdict(list)
for ed, fid in scan:
    nn, nm = scan_name(ed)
    scan_names[nm].append((nn, fid, ed))

# Scan-target names with NO matching trait keyword by name
print("\n=== scan-target names with NO trait KYWD by exact name ===")
unmatched = []
for nm, lst in sorted(scan_names.items()):
    # strip trailing digits (01/02) to find base trait name
    base = re.sub(r'\d+$', '', nm)
    if nm not in trait_names and base not in trait_names:
        unmatched.append((nm, lst, base))
        print(f"  scanName={nm!r} base={base!r} -> {[hex(f) for _,f,_ in lst]}")
print(f"  unmatched scan names = {len(unmatched)}")
