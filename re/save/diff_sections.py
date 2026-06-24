"""
Section-bounded diff of the three Jemison "Sentient Microbial Colony" saves that differ
by exactly one trait hand-scan each (0/2 -> 1/2 -> 2/2).

Goal: localize the DURABLE record that encodes the trait scan-target "N/M SCANNED" count.

The three bodies have different sizes, so a naive aligned byte diff is meaningless past the
first inserted byte. Instead we:
  1. Parse the file-location table (sfs_body) to get absolute section offsets:
       offsetA, offsetB, offsetC, changeFormsOffset, formIDArrayOffset
  2. Map each absolute offset to a logical section, so we can diff section-by-section,
     anchoring on the section START (which shifts predictably between saves).
  3. For each section, align by the section start and report the changed sub-region
     (common-prefix / common-suffix within the section).

Read-only. Operates on decompressed bodies in memory.
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


def load():
    out = {}
    for k, p in SAVES.items():
        body = BCPSContainer(p).decompress()
        sb = SFSBody(body)
        out[k] = (body, sb.loc_table)
    return out


def section_bounds(lt, bodylen):
    """Return ordered list of (name, start, end) absolute section spans."""
    pts = [
        ("header+plugins..offsetA", 0, lt["offsetA"]),
        ("A (offsetA..offsetB)", lt["offsetA"], lt["offsetB"]),
        ("B (offsetB..offsetC)", lt["offsetB"], lt["offsetC"]),
        ("C (offsetC..changeForms)", lt["offsetC"], lt["changeFormsOffset"]),
        ("ChangeForms (..formIDArray)", lt["changeFormsOffset"], lt["formIDArrayOffset"]),
        ("formIDArray..end", lt["formIDArrayOffset"], bodylen),
    ]
    return pts


def common_prefix(x, y):
    n = min(len(x), len(y)); i = 0
    while i < n and x[i] == y[i]:
        i += 1
    return i


def common_suffix(x, y):
    n = min(len(x), len(y)); i = 0
    while i < n and x[-1 - i] == y[-1 - i]:
        i += 1
    return i


def diff_region(a, b, label):
    p = common_prefix(a, b)
    s = common_suffix(a, b)
    if s > len(a) - p:
        s = len(a) - p
    if s > len(b) - p:
        s = len(b) - p
    ca = a[p:len(a) - s]
    cb = b[p:len(b) - s]
    same = (len(ca) == 0 and len(cb) == 0)
    print(f"    {label}: lenA={len(a)} lenB={len(b)} commonPrefix=0x{p:X} commonSuffix=0x{s:X} "
          f"changedA={len(ca)} changedB={len(cb)} {'(IDENTICAL)' if same else ''}")
    if not same:
        print(f"      A changed head: {ca[:64].hex(' ')}")
        print(f"      B changed head: {cb[:64].hex(' ')}")
        if len(ca) <= 256:
            print(f"      A changed FULL: {ca.hex(' ')}")
            print(f"      B changed FULL: {cb.hex(' ')}")
    return p, s, ca, cb


def main():
    data = load()
    for k in SAVES:
        body, lt = data[k]
        print(f"=== {k}: bodylen={len(body)} changeFormCount={lt['changeFormCount']} ===")
        for nm, st, en in section_bounds(lt, len(body)):
            print(f"    {nm:30s} [0x{st:07X}..0x{en:07X}] len={en - st}")
    print()

    for pair in [("0of2", "1of2"), ("1of2", "2of2"), ("0of2", "2of2")]:
        ka, kb = pair
        ba, lta = data[ka]
        bb, ltb = data[kb]
        print(f"################ SECTION DIFF {ka} -> {kb} ################")
        sa = section_bounds(lta, len(ba))
        sb_ = section_bounds(ltb, len(bb))
        for (nm, sta, ena), (_, stb, enb) in zip(sa, sb_):
            seg_a = ba[sta:ena]
            seg_b = bb[stb:enb]
            print(f"  {nm}")
            diff_region(seg_a, seg_b, "sect")
        print()


if __name__ == "__main__":
    main()
