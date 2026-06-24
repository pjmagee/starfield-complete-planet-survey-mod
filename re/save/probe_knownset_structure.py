"""
Probe the REAL structure of the GlobalData-R1 records the prior agents called the
"0x00013FE1 known-set". Goal: determine whether e1 3f 01 00 is a discriminator, a
record-length tag, or incidental — and what the 31 records actually are.

Dumps a wide hex window around each e1 3f 01 00 hit in Save14 (real 2/2) vs Save12
(unscanned) and locates the single record whose +6 byte differs, with full context.
"""
import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "Save12_0of2":      BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "Save14_REAL_2of2": BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
}
DISC = bytes.fromhex("e13f0100")   # 0x00013FE1 LE

def find_all(buf, needle):
    out, i = [], buf.find(needle)
    while i != -1:
        out.append(i); i = buf.find(needle, i + 1)
    return out

def dump(buf, off, pre=16, post=40):
    a = max(0, off - pre); b = min(len(buf), off + post)
    return buf[a:b].hex(" ")

regions = {}
for name, path in SAVES.items():
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body); lt = sb.loc_table
    r1 = body[lt["offsetA"]:lt["offsetB"]]
    regions[name] = r1
    hits = find_all(r1, DISC)
    print(f"=== {name}: R1 len={len(r1):,}  e13f0100 hits={len(hits)} ===")
    # spacing between consecutive hits (reveals fixed record stride if any)
    if len(hits) >= 2:
        deltas = [hits[i+1]-hits[i] for i in range(len(hits)-1)]
        from collections import Counter
        print(f"  stride histogram (delta between consecutive hits): {Counter(deltas).most_common(6)}")
    if hits:
        print(f"  first hit @R1+0x{hits[0]:X}, last @R1+0x{hits[-1]:X}, span=0x{hits[-1]-hits[0]:X}")
        print(f"  sample record bytes (first hit): {dump(r1, hits[0])}")
    print()

# Cross-diff: which hit has +6 != 0 in Save14 but not Save12?
r12, r14 = regions["Save12_0of2"], regions["Save14_REAL_2of2"]
h12, h14 = find_all(r12, DISC), find_all(r14, DISC)
print(f">> Save12 hits={len(h12)}  Save14 hits={len(h14)}")
flip12 = [(p, r12[p+6]) for p in h12 if r12[p+6] != 0]
flip14 = [(p, r14[p+6]) for p in h14 if r14[p+6] != 0]
print(f">> Save12 records with +6!=0: {[(hex(p),v) for p,v in flip12]}")
print(f">> Save14 records with +6!=0: {[(hex(p),v) for p,v in flip14]}")
for p, v in flip14:
    print(f"\n   Save14 flipped record @R1+0x{p:X} (+6={v}):")
    print(f"     {dump(r14, p, pre=8, post=48)}")
    # show what is at the same logical offset region in Save12
    # align by searching the surrounding 24 bytes that are NOT the changing byte
