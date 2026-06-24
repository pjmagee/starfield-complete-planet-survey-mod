"""
Enumerate planet TRAIT scan-target authoring in Starfield.esm, fully offline.

Tasks:
 1. Full table: PlanetTrait<NN><Name> KYWD <-> PlanetTraitScanTarget ACTI <->
    HandScannerTraitInfo KYWD <-> OverlayTrait*Location LCTN <-> MB_SurveyTrait QUST.
 2. Required count ("M" in N/M SCANNED): decode LCTN LCPR/LCSR/LCID, QUST props/aliases.
 3. PNDT -> trait link (dump Jemison KWDA).
 4. Overlay/spawn link: FLST/COBJ/GBFM/QUST referencing BOTH trait KYWD and scan-target ACTI.
 5. Static placement check: any REFR/ACHR/PHZD NAME -> PlanetTraitScanTarget ACTI.

Single full-file pass builds indices; relationships resolved after.
"""
import struct, zlib, re, json, sys
from collections import defaultdict

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

def subrecords(rd):
    j = 0; real = None
    while j + 6 <= len(rd):
        ssig = rd[j:j+4]
        ssz = struct.unpack_from('<H', rd, j+4)[0]
        if ssig == b'XXXX':
            real = struct.unpack_from('<I', rd, j+6)[0]; j += 6 + ssz; continue
        dsz = ssz
        if real is not None: dsz = real; real = None
        yield ssig, rd[j+6:j+6+dsz]; j += 6 + dsz

def inflate(rd, flags):
    if flags & COMPRESSED and len(rd) >= 4:
        try: return zlib.decompress(rd[4:])
        except Exception: return b''
    return rd

def get_edid(rd):
    for s, p in subrecords(rd):
        if s == b'EDID':
            return p.split(b'\x00', 1)[0].decode('latin1', 'replace')
    return None

# ---- indices ----
records = {}            # fid -> (sig, edid)
edid_to_fid = {}        # edid -> fid
kwda_of = {}            # fid -> [keyword fids]   (KWDA-bearing records)
subs_cache = {}         # fid -> list[(ssig, payload)]  for records of interest
trait_tnam = {}         # trait fid -> tnam int
# reverse: keyword fid -> list of (referrer fid, referrer sig)  via KWDA
kwd_referrers = defaultdict(list)
# any REFR/ACHR/PHZD NAME -> base fid : record placements
placements_by_base = defaultdict(int)
# scan-target ACTI fids (resolved later by edid)
SCAN_PREFIX = "PlanetTraitScanTarget"
TRAIT_PREFIX = "PlanetTrait"        # but NOT PlanetTraitScanTarget
HSTI_PREFIX = "HandScannerTraitInfo"
OVERLAY_PREFIX = "OverlayTrait"
MB_PREFIX = "MB_SurveyTrait"

# We don't know scan-target fids in pass 1, so collect all candidate edids and
# also record, for FLST/COBJ/GBFM/QUST, the full set of formids embedded in body.
container_formids = {}  # fid(sig in FLST/COBJ/GBFM/QUST) -> set(formids referenced)
INTEREST_CONTAINER = {b'FLST', b'COBJ', b'GBFM', b'QUST'}
KEEP_FULL_SUBS = {b'ACTI', b'KYWD', b'LCTN', b'QUST', b'PNDT', b'FLST', b'GLOB'}

PLACEMENT_SIGS = {b'REFR', b'ACHR', b'PHZD'}

def name_base(rd):
    for s, p in subrecords(rd):
        if s == b'NAME' and len(p) >= 4:
            return struct.unpack_from('<I', p, 0)[0]
    return None

def kwda_list(rd):
    out = []
    for s, p in subrecords(rd):
        if s == b'KWDA':
            for o in range(0, len(p) - 3, 4):
                out.append(struct.unpack_from('<I', p, o)[0])
    return out

def main():
    with open(ESM, 'rb') as f:
        data = f.read()
    t = struct.unpack_from('<I', data, 4)[0]
    pos = 24 + t
    n = len(data)

    def handle(sig, fid, flags, body):
        rd = inflate(body, flags)
        if not rd: return
        ed = get_edid(rd)
        records[fid] = (sig, ed)
        if ed and ed not in edid_to_fid:
            edid_to_fid[ed] = fid
        subs = None
        if sig in KEEP_FULL_SUBS:
            subs = list(subrecords(rd))
            subs_cache[fid] = subs
        # KWDA index (KYWD-bearing records: ACTI, LCTN, PNDT, etc.)
        kl = kwda_list(rd)
        if kl:
            kwda_of[fid] = kl
            for k in kl:
                kwd_referrers[k].append((fid, sig))
        # KYWD TNAM
        if sig == b'KYWD':
            for s, p in subrecords(rd):
                if s == b'TNAM' and len(p) >= 4:
                    trait_tnam[fid] = struct.unpack_from('<I', p, 0)[0]
                elif s == b'TNAM' and len(p) >= 1:
                    trait_tnam[fid] = p[0]
        # placements
        if sig in PLACEMENT_SIGS:
            b = name_base(rd)
            if b is not None:
                placements_by_base[b] += 1
        # container formid sets (scan whole body for embedded formids)
        if sig in INTEREST_CONTAINER:
            fset = set()
            for ssig, p in subrecords(rd):
                for o in range(0, len(p) - 3, 1):
                    v = struct.unpack_from('<I', p, o)[0]
                    fset.add(v)
            container_formids[fid] = fset

    def walk(d):
        i = 0; m = len(d)
        while i + 24 <= m:
            sig = d[i:i+4]
            size = struct.unpack_from('<I', d, i+4)[0]
            if sig == b'GRUP':
                walk(d[i+24:i+size]); i += size; continue
            flags = struct.unpack_from('<I', d, i+8)[0]
            ff = struct.unpack_from('<I', d, i+12)[0]
            handle(sig, ff, flags, d[i+24:i+24+size])
            i += 24 + size

    while pos + 24 <= n:
        if data[pos:pos+4] != b'GRUP': break
        gs = struct.unpack_from('<I', data, pos+4)[0]
        walk(data[pos:pos+gs])
        pos += gs

    # persist a compact pickle-ish via json of what we need
    import pickle
    out = {
        'records': records,
        'edid_to_fid': edid_to_fid,
        'kwda_of': kwda_of,
        'subs_cache': {k: [(s, v) for s, v in v] for k, v in subs_cache.items()},
        'trait_tnam': trait_tnam,
        'kwd_referrers': {k: v for k, v in kwd_referrers.items()},
        'placements_by_base': dict(placements_by_base),
        'container_formids': {k: list(v) for k, v in container_formids.items()},
    }
    with open(r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\tools\_trait_index.pkl", 'wb') as f:
        pickle.dump(out, f)
    print(f"indexed records={len(records)} kwda_recs={len(kwda_of)} "
          f"containers={len(container_formids)} placements_bases={len(placements_by_base)}")

if __name__ == '__main__':
    main()
