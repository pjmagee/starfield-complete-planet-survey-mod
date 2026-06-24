"""
Determine the RUNTIME disc value of the records, and whether e1 3f (0x3FE1) is a
discriminator or a serialized hash. Strategy:

The BSComponentDB2 in-memory key is (disc16 << 48) | (formId32 << 16).  The
SERIALIZER (ID_52193 etc.) does NOT necessarily write that key verbatim. We test
the alternative reading: the 31 records are 0x28-byte entries of a serialized
BSTScatterTable; e1 3f is part of a hash/index, and the planet-keyed 938333
PlayerKnowledge record is elsewhere.

We:
 1) Locate the 938333 PlayerKnowledge per-planet record by the KNOWN slot bytes
    (50 b2 61 00 = CANON^TAG, then flag/pct) to anchor it.
 2) Walk backwards/forwards to find its framing (the disc/key bytes preceding it).
 3) Compare the e1 3f records' structure: are they 0x28-strided scatter entries?
"""
import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
P14 = BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs"

body = BCPSContainer(P14).decompress()
sb = SFSBody(body); lt = sb.loc_table
A, B = lt["offsetA"], lt["offsetB"]
r1 = body[A:B]

def find_all(buf, needle, start=0):
    out, i = [], buf.find(needle, start)
    while i != -1:
        out.append(i); i = buf.find(needle, i + 1)
    return out

CANON = 0x0021B250; KWD = 0x00225588; TAG = 0x00400000
PLANET = 0x0003F5A1

# 1) Anchor the 938333 record via the CANON slot bytes (50 b2 61 00 = CANON^TAG)
canon_db = struct.pack("<I", CANON ^ TAG)
hits = find_all(r1, canon_db)
print(f"CANON^TAG (50 b2 61 00) hits in R1: {len(hits)} @ {[hex(h) for h in hits]}")
for h in hits:
    print(f"  @R1+0x{h:X}: {r1[max(0,h-32):h+24].hex(' ')}")

# 2) Look for the planet id and the 938333 disc framing near the slot
print(f"\nPLANET 0x{PLANET:08X} raw LE hits: {len(find_all(r1, struct.pack('<I',PLANET)))}")
print(f"PLANET^TAG 0x{PLANET^TAG:08X} raw LE hits: {len(find_all(r1, struct.pack('<I',PLANET^TAG)))}")

# 3) Examine whether the 31 e1 3f records are 0x28-strided scatter entries.
DISC = bytes.fromhex("e13f0100")
eh = find_all(r1, DISC)
print(f"\ne1 3f 01 00 hits: {len(eh)}")
# show 0x28 bytes from each of first few hits, ALIGNED so we can see the column layout
print("Per-record 0x20-byte dump (aligned to the e13f0100 token):")
for h in eh[:6]:
    print(f"  +0x{h:05X}: {r1[h:h+32].hex(' ')}")
print("  ...")
# The flipped one
for h in eh:
    if r1[h+6] != 0:
        print(f"  FLIP +0x{h:05X}: {r1[h:h+32].hex(' ')}")

# 4) Is e1 3f part of a larger recurring token? show the 2 bytes BEFORE each hit
from collections import Counter
before2 = Counter(r1[h-2:h] for h in eh)
print(f"\n2 bytes BEFORE each e13f0100 hit: {[(b.hex(),c) for b,c in before2.most_common()]}")
after_disc = Counter(r1[h+4:h+6] for h in eh)
print(f"2 bytes at +4 (after e13f0100): {[(b.hex(),c) for b,c in after_disc.most_common()]}")
