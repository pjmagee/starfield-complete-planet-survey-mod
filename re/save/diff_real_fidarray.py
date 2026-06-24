"""
The REAL formID array starts at the section offset stored at (table_base - 8), NOT at the
base+36 'formIDArrayOffset' field (that points to a trailer). At table_base-8:
    u32 count, then count * u32 FormIDs.

This script parses the real array in all three saves and finds exactly which FormID(s) the
first scan INSERTED (0of2 -> 1of2) and whether the second scan (1of2 -> 2of2) changed it.
Every dynamic ref (high byte 0xFF) in the changed window is reported.
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
        fid_off = struct.unpack_from("<I", body, base - 8)[0]
        out[k] = (body, sb.loc_table, fid_off)
    return out


def parse_fidarray(body, fid_off):
    count = struct.unpack_from("<I", body, fid_off)[0]
    arr = list(struct.unpack_from("<%dI" % count, body, fid_off + 4))
    return count, arr


def main():
    data = load()
    arrs = {}
    for k in SAVES:
        body, lt, fid_off = data[k]
        count, arr = parse_fidarray(body, fid_off)
        arrs[k] = arr
        dyn = sum(1 for x in arr if (x >> 24) == 0xFF)
        print(f"{k}: fidArray @0x{fid_off:X} count={count} (arr_end=0x{fid_off+4+count*4:X}, "
              f"base+36={lt['formIDArrayOffset']:#x}) dynamicRefs={dyn}")
    print()

    for ka, kb in [("0of2", "1of2"), ("1of2", "2of2"), ("0of2", "2of2")]:
        a, b = arrs[ka], arrs[kb]
        print(f"=== {ka}({len(a)}) -> {kb}({len(b)})  delta={len(b)-len(a)} ===")
        n = min(len(a), len(b))
        i = 0
        while i < n and a[i] == b[i]:
            i += 1
        # suffix
        j = 0
        while j < n - i and a[-1 - j] == b[-1 - j]:
            j += 1
        print(f"  diverge @index {i}; common suffix len {j}")
        chg_a = a[i:len(a) - j]
        chg_b = b[i:len(b) - j]
        print(f"  changed A[{i}:{len(a)-j}] ({len(chg_a)}): {[hex(x) for x in chg_a[:20]]}")
        print(f"  changed B[{i}:{len(b)-j}] ({len(chg_b)}): {[hex(x) for x in chg_b[:20]]}")
        # dynamic refs in changed-B window
        dyn_b = [hex(x) for x in chg_b if (x >> 24) == 0xFF]
        dyn_a = [hex(x) for x in chg_a if (x >> 24) == 0xFF]
        print(f"  dynamicRefs in changed-A: {dyn_a[:20]}")
        print(f"  dynamicRefs in changed-B: {dyn_b[:20]}")
        # set difference (added forms)
        sa, sb_ = set(a), set(b)
        added = sb_ - sa
        removed = sa - sb_
        print(f"  forms ADDED in {kb}: {sorted(hex(x) for x in added)[:30]}")
        print(f"  forms REMOVED in {kb}: {sorted(hex(x) for x in removed)[:30]}")
        print()


if __name__ == "__main__":
    main()
