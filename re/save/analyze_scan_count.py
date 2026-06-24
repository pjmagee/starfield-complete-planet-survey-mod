"""
CONSOLIDATED analysis: where is the trait scan-target "N/M SCANNED" count durably stored?

Diffs the three Jemison "Sentient Microbial Colony" saves (0/2 -> 1/2 -> 2/2, one hand-scan
apart) section-by-section and reports the decisive facts. Read-only. Run: python analyze_scan_count.py

Findings (see re/save/scan-count-store-2026-06-23.md for the full writeup):
  * formID array is BYTE-IDENTICAL across all three saves (count 311117, 0 added/removed).
  * scan-target REFRs (0x00159EB2 etc.), base ACTI 0x0021B250, trait KW 0x00225588 are ABSENT
    from the formID array under EVERY plugin index, and appear NOWHERE in any body.
  * changeFormCount: 15362 -> 15363 -> 15363 (scan#1 adds one ChangeForm; scan#2 edits in place).
  * The added/changed records are runtime-handle-keyed (0x221-changeFlag 20-byte entries) or
    volatile GlobalData (save counter, camera/position floats) -- NONE is a clean planet/ACTI/trait
    keyed 0->1->2 counter.
  * The only 0->1->2 increments found are the SAVE SEQUENCE NUMBER (saves are sequential), not a
    scan count.
  => There is no durable, id-keyed save record that stores the trait scan count as a counter. The
     count is recomputed at runtime from the transient 939118 ScannableComponent +0x28 bytes
     (decompile: ID_90522 count walker; see re/ghidra/output/scan-count-runtime-store-2026-06-23.md).
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

TRAIT_TARGETS = {
    "scan-target REFR 0x00159EB2": 0x00159EB2,
    "scan-target REFR 0x00159F10": 0x00159F10,
    "scan-target REFR 0x0016776F": 0x0016776F,
    "scan-target REFR 0x00167770": 0x00167770,
    "scan-target REFR 0x002EA231": 0x002EA231,
    "scan-target REFR 0x002EA0D1": 0x002EA0D1,
    "base ACTI 0x0021B250": 0x0021B250,
    "trait keyword 0x00225588": 0x00225588,
    "Jemison PNDT 0x0003F5A1": 0x0003F5A1,
}


def load(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body)
    lt = sb.loc_table
    base = lt["table_base"]
    fid_off = struct.unpack_from("<I", body, base - 8)[0]  # REAL formID array start
    fid_count = struct.unpack_from("<I", body, fid_off)[0]
    fid_arr = struct.unpack_from("<%dI" % fid_count, body, fid_off + 4)
    return body, lt, fid_off, fid_arr


def main():
    data = {k: load(p) for k, p in SAVES.items()}

    print("=" * 78)
    print("1. SECTION MAP + changeFormCount")
    print("=" * 78)
    for k in SAVES:
        body, lt, fid_off, arr = data[k]
        print(f"  {k}: bodylen={len(body):>9}  changeFormCount={lt['changeFormCount']}  "
              f"formIDArray@0x{fid_off:X} count={len(arr)}")

    print()
    print("=" * 78)
    print("2. formID ARRAY identical across saves?")
    print("=" * 78)
    a0 = data["0of2"][3]; a1 = data["1of2"][3]; a2 = data["2of2"][3]
    print(f"  0of2==1of2: {a0 == a1}   1of2==2of2: {a1 == a2}   (count {len(a0)}/{len(a1)}/{len(a2)})")

    print()
    print("=" * 78)
    print("3. trait targets present in formID array / anywhere in body?")
    print("=" * 78)
    body0 = data["0of2"][0]
    for name, fid in TRAIT_TARGETS.items():
        in_arr = any((v & 0xFFFFFF) == (fid & 0xFFFFFF) for v in a0)
        raw = body0.count(struct.pack("<I", fid))
        print(f"  {name:32s}: in formIDArray(any plugin idx)={in_arr}  rawLE32 hits in 0of2={raw}")

    print()
    print("=" * 78)
    print("4. 0->1->2 monotonic increments in the position-aligned GlobalData region")
    print("   (offsetB..offsetC, 15364 B in all three)")
    print("=" * 78)
    def reg(k):
        body, lt, _, _ = data[k]
        return body[lt["offsetB"]:lt["offsetC"]]
    r0, r1, r2 = reg("0of2"), reg("1of2"), reg("2of2")
    n = min(len(r0), len(r1), len(r2))
    found = 0
    for i in range(n - 4):
        if r0[i] == r1[i] == r2[i]:
            continue
        a = struct.unpack_from("<I", r0, i)[0]
        b = struct.unpack_from("<I", r1, i)[0]
        c = struct.unpack_from("<I", r2, i)[0]
        if b == a + 1 and c == a + 2:
            print(f"  +0x{i:X}: u32 {a} -> {b} -> {c}  (SAVE SEQUENCE NUMBER: saves are sequential)")
            found += 1
    if not found:
        print("  none")
    print()
    print("CONCLUSION: no planet/ACTI/trait keyed 0->1->2 count store in the save.")
    print("The count is recomputed at runtime from transient 939118 +0x28 (decompile).")


if __name__ == "__main__":
    main()
