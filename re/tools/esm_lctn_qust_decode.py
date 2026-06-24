"""Decode LCTN (LCPR/LCSR/LCID/LCEP...) and QUST subrecords for trait 20 + dump raw.
Local self-generated pickle (trusted)."""
import struct, pickle
PKL = r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl"
with open(PKL,'rb') as f: IDX=pickle.load(f)
records=IDX['records']; subs_cache=IDX['subs_cache']
def ed(fid):
    r=records.get(fid); return r[1] if r else None

LOCREF_PLANETTRAIT = 0x0027A567   # LCRT PlanetTraitScanTargetLocRef
SCAN_ACTI_20       = 0x0021B250

def dump_subs(fid, label):
    print(f"\n===== {label}: 0x{fid:08X} {ed(fid)} =====")
    subs = subs_cache.get(fid)
    if subs is None:
        print("  (no cached subs)"); return None
    for ssig, p in subs:
        s = ssig.decode('latin1')
        print(f"  {s} ({len(p)}B): {p[:96].hex()}")
    return subs

def decode_lctn(fid):
    subs = subs_cache.get(fid)
    if not subs:
        print("  no subs"); return
    print(f"\n----- DECODE LCTN 0x{fid:08X} {ed(fid)} -----")
    counts = {}
    for ssig, p in subs:
        s = ssig.decode('latin1')
        if s in ('LCPR','LCSR','LCID','LCEP','LCUN','ACPR','ACSR','ACID','ACUN','ACEP'):
            counts[s] = counts.get(s,0)+1
    # LCSR entries: [LocRefType formid(4)][marker/ref formid(4)]... typically 0x18 bytes? inspect
    for ssig, p in subs:
        s = ssig.decode('latin1')
        if s in ('LCSR','LCPR','LCID','ACSR','ACPR','ACID','LCEP','ACEP'):
            # show first formids
            f0 = struct.unpack_from('<I',p,0)[0] if len(p)>=4 else None
            f1 = struct.unpack_from('<I',p,4)[0] if len(p)>=8 else None
            f0e = ed(f0) if f0 else None
            f1e = ed(f1) if f1 else None
            tag = ''
            if f0 == LOCREF_PLANETTRAIT or f1 == LOCREF_PLANETTRAIT: tag += ' <<PLANETTRAITLOCREF'
            if SCAN_ACTI_20 in (f0,f1): tag += ' <<SCANACTI20'
            print(f"    {s} len={len(p)} f0=0x{(f0 or 0):08X}({f0e}) f1=0x{(f1 or 0):08X}({f1e}) raw={p.hex()}{tag}")
    print(f"    SUBREC COUNTS: {counts}")

for fid in (0x0010A476, 0x0021D242, 0x003807B9):
    dump_subs(fid, "OVERLAY LCTN")
    decode_lctn(fid)

# QUST MB_SurveyTrait20
print("\n\n############ QUST MB_SurveyTrait20 ############")
qsubs = dump_subs(0x001953F8, "QUST MB_SurveyTrait20")
# look for GLOB references, count fields, ALST/ALLS/ALFR/ALFA, VMAD
if qsubs:
    print("\n----- QUST subrecord tally -----")
    from collections import Counter
    c = Counter(ssig.decode('latin1') for ssig,_ in qsubs)
    print("  ", dict(c))
    # dump every formid referenced in QUST that resolves to a GLOB / the scan ACTI / locref
    print("\n----- QUST embedded formids of interest -----")
    for ssig,p in qsubs:
        for o in range(0,len(p)-3):
            v=struct.unpack_from('<I',p,o)[0]
            e=ed(v)
            if v in (LOCREF_PLANETTRAIT, SCAN_ACTI_20, 0x00225588, 0x0021B252):
                print(f"    {ssig.decode('latin1')}@{o}: 0x{v:08X} {e}")
            elif e and records.get(v,(b'',''))[0]==b'GLOB':
                print(f"    {ssig.decode('latin1')}@{o}: GLOB 0x{v:08X} {e}")
