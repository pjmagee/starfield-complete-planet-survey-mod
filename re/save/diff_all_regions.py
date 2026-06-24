"""
Full multi-region diff of the ChangeForms section with repeated resync.

Walks both byte streams in lockstep; whenever they differ, records the divergent window and
resyncs by finding the next long (>=need) common run. Reports EVERY changed region with its
offset-into-section, so we can see the volatile-float noise regions AND the genuine inserted/edited
ChangeForm record (the one that grows by the size delta).
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
    return cf, body[cf:end]


def resync(a, b, ia, ib, need=64, max_shift=8192):
    """Find next (ja>=ia, jb>=ib) where a[ja:ja+need]==b[jb:jb+need], minimal advance."""
    la, lb = len(a), len(b)
    while ia < la - need:
        seg = a[ia:ia + need]
        lo = max(ib, 0)
        hi = min(lb, ib + max_shift)
        idx = b.find(seg, lo, hi)
        if idx != -1:
            return ia, idx
        # also allow A to advance relative to B (deletion case)
        ia += 1
    return la, lb


def walk(a, b, label):
    print(f"############ {label}  lenA={len(a)} lenB={len(b)} delta={len(b)-len(a)} ############")
    ia = ib = 0
    la, lb = len(a), len(b)
    region = 0
    while ia < la and ib < lb:
        if a[ia] == b[ib]:
            ia += 1; ib += 1
            continue
        # divergence at (ia, ib): find resync
        ja, jb = resync(a, b, ia, ib)
        win_a = a[ia:ja]
        win_b = b[ib:jb]
        region += 1
        szdelta = len(win_b) - len(win_a)
        print(f"  region#{region}: A[0x{ia:X}:0x{ja:X}] ({len(win_a)}B) vs B[0x{ib:X}:0x{jb:X}] ({len(win_b)}B) sizeDelta={szdelta}")
        if len(win_a) <= 96 and len(win_b) <= 96:
            print(f"     ctx-before: {a[max(0,ia-16):ia].hex(' ')}")
            print(f"     A: {win_a.hex(' ')}")
            print(f"     B: {win_b.hex(' ')}")
        else:
            print(f"     A head: {win_a[:96].hex(' ')}")
            print(f"     B head: {win_b[:96].hex(' ')}")
            print(f"     ctx-before: {a[max(0,ia-16):ia].hex(' ')}")
        ia, ib = ja, jb
    print()


def main():
    cfs = {k: cf_section(p)[1] for k, p in SAVES.items()}
    walk(cfs["0of2"], cfs["1of2"], "0of2 -> 1of2")
    walk(cfs["1of2"], cfs["2of2"], "1of2 -> 2of2")


if __name__ == "__main__":
    main()
