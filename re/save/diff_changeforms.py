"""
ChangeForms-section byte diff, anchored on the section start.

The formID array is byte-identical across all 3 saves (diff_real_fidarray.py), so the durable
delta is a ChangeForm. changeFormCount: 15362 -> 15363 -> 15363, i.e. the FIRST scan ADDS a
changeform, the SECOND scan EDITS in place (count unchanged).

We extract the ChangeForms section [changeFormsOffset .. realFidArrayOffset(=table_base-8 value)]
for each save, align by prefix/suffix, and dump the changed window in full so we can read the
record header (refID/index, changeFlags, type, version, length) and the payload byte that flips.
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
        base = sb.loc_table["table_base"]
        cf = sb.loc_table["changeFormsOffset"]
        fid_off = struct.unpack_from("<I", body, base - 8)[0]  # real array start = CF section end
        out[k] = (body, cf, fid_off, sb.loc_table["changeFormCount"])
    return out


def diff(a, b, ka, kb):
    n = min(len(a), len(b))
    p = 0
    while p < n and a[p] == b[p]:
        p += 1
    s = 0
    while s < n - p and a[-1 - s] == b[-1 - s]:
        s += 1
    ca = a[p:len(a) - s]
    cb = b[p:len(b) - s]
    print(f"=== ChangeForms diff {ka} -> {kb} ===")
    print(f"  CF len {ka}={len(a)} {kb}={len(b)}  commonPrefix=0x{p:X} commonSuffix=0x{s:X} "
          f"changedA={len(ca)} changedB={len(cb)}")
    # the diff is relative to section start; show absolute offset of change start within section
    print(f"  change starts at +0x{p:X} into ChangeForms section")
    # dump windows (cap)
    def dump(tag, buf, ctxbuf, pp):
        head = ctxbuf[max(0, pp - 24):pp]
        print(f"  {tag} pre-context (24B before change): {head.hex(' ')}")
        print(f"  {tag} changed ({len(buf)}B): {buf[:160].hex(' ')}")
    dump("A", ca, a, p)
    dump("B", cb, b, p)
    return p, s, ca, cb


def main():
    data = load()
    for k in SAVES:
        body, cf, fid_off, cfc = data[k]
        print(f"{k}: changeFormsOffset=0x{cf:X} CFsectionEnd=0x{fid_off:X} len={fid_off-cf} count={cfc}")
    print()
    for ka, kb in [("1of2", "2of2"), ("0of2", "1of2")]:
        ba, cfa, fa, _ = data[ka]
        bb, cfb, fb, _ = data[kb]
        a = ba[cfa:fa]
        b = bb[cfb:fb]
        diff(a, b, ka, kb)
        print()


if __name__ == "__main__":
    main()
