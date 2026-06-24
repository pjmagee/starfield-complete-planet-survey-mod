"""
Locate the inserted ChangeForm record by realigning the tail after the volatile leading bytes.

The ChangeForms section's first few bytes after the preamble are a volatile value (changes every
save). To find the structural INSERT (0of2->1of2 grew by 247 bytes; 1of2->2of2 by 80 bytes), we:

  1. Take CF section bytes for two saves.
  2. From the front, find first difference (prefix P).
  3. From the back, find common suffix (S).
  4. The changed window is A[P:lenA-S] vs B[P:lenB-S]. Because the leading volatile u32 forces P
     small, we then RE-ALIGN: within the changed window, slide B and find the largest offset d such
     that a tail of A matches B[d:], isolating the genuinely-inserted bytes.
  5. We also do a record-granular realign: scan for the longest common run to find where the two
     streams resynchronize, which brackets exactly the inserted/edited record.
"""
import sys
import struct

sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "0of2": BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "1of2": BASE + r"\Save13_98A838ADM467265736820436861726163746572_000043_20260623182638_1_0_4.sfs",
    "2of2": BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
}


def cf_section(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body)
    base = sb.loc_table["table_base"]
    cf = sb.loc_table["changeFormsOffset"]
    end = struct.unpack_from("<I", body, base - 8)[0]
    return body[cf:end]


def find_resync(a, b, start_a, start_b, window=64, need=48):
    """From (start_a, start_b), find the next position where >=need bytes match consecutively
    in both, returning the resync (ia, ib)."""
    ia, ib = start_a, start_b
    la, lb = len(a), len(b)
    # naive: for each ia, search nearby ib for a long match
    max_shift = 4096
    while ia < la - need:
        # try to match a[ia:ia+need] in b around ib .. ib+max_shift
        seg = a[ia:ia + need]
        idx = b.find(seg, max(0, ib - max_shift), min(lb, ib + max_shift))
        if idx != -1:
            return ia, idx
        ia += 1
    return None


def analyze(a, b, ka, kb):
    n = min(len(a), len(b))
    p = 0
    while p < n and a[p] == b[p]:
        p += 1
    s = 0
    while s < n - p and a[-1 - s] == b[-1 - s]:
        s += 1
    print(f"=== {ka}({len(a)}) -> {kb}({len(b)}) delta={len(b)-len(a)} ===")
    print(f"  raw prefix=0x{p:X} suffix=0x{s:X}")
    # Realign: after the first diff at p, find resync point
    rs = find_resync(a, b, p, p)
    if rs is None:
        print("  could not resync")
        return
    ia, ib = rs
    print(f"  resync: A re-syncs at +0x{ia:X}, B at +0x{ib:X}  (A skipped {ia-p}, B skipped {ib-p})")
    # The window of genuine change:
    win_a = a[p:ia]
    win_b = b[p:ib]
    print(f"  changed window A (0x{p:X}..0x{ia:X}, {len(win_a)}B):")
    print(f"    {win_a.hex(' ')}")
    print(f"  changed window B (0x{p:X}..0x{ib:X}, {len(win_b)}B):")
    print(f"    {win_b.hex(' ')}")
    # context just before p
    print(f"  32B before change: {a[max(0,p-32):p].hex(' ')}")
    print()


def main():
    cf = {k: cf_section(p) for k, p in SAVES.items()}
    analyze(cf["0of2"], cf["1of2"], "0of2", "1of2")
    analyze(cf["1of2"], cf["2of2"], "1of2", "2of2")


if __name__ == "__main__":
    main()
