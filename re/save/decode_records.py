"""
Decode the ChangeForm-like records around the scan insertion points.

The repeating unit observed across the ChangeForms section is:
    [u32 refID][u32 0x00000221][u32 fieldA][u16 ?][u16 flags=0xFFFF or other][payload...]
The 0x221 is constant. The records inserted at scan time carry refIDs like 0x5fb52910,
0xb8cdb180, 0x8b26ab90, 0x77ce06e0. These look like ENCODED refs (high bits = a type selector).

In Bethesda saves the changeform refID is a packed form-reference: the top 2-3 bits select an
encoding (0 = index into the save formID array; others = inline FormID by plugin). Decode each
candidate refID under several schemes and resolve via the real formID array.

We also enumerate, for the 1of2->2of2 insertion (cleanest), every distinct refID inserted and map
it to a FormID.
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


def load(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body)
    base = sb.loc_table["table_base"]
    cf = sb.loc_table["changeFormsOffset"]
    fid_off = struct.unpack_from("<I", body, base - 8)[0]
    fid_count = struct.unpack_from("<I", body, fid_off)[0]
    fid_arr = struct.unpack_from("<%dI" % fid_count, body, fid_off + 4)
    return body, cf, fid_off, fid_arr


def decode_refid(rid, fid_arr):
    """Try the FO4/SSE 'refID' packing: top 2 bits = type.
       type 0 (00) -> index into formIDArray (0-based) -> arr[idx-1]? or arr[idx]
       type 1 (01) -> normal FormID with plugin idx 0 (low 24 bits)
       type 2 (10) -> created/default
       Return list of (scheme, resolved)."""
    out = []
    top = rid >> 30
    low24 = rid & 0x00FFFFFF
    idx = rid & 0x3FFFFFFF
    out.append(("top2bits", top))
    out.append(("low24", hex(low24)))
    # index schemes
    if 0 < idx <= len(fid_arr):
        out.append(("arr[idx-1]", hex(fid_arr[idx - 1])))
    if idx < len(fid_arr):
        out.append(("arr[idx]", hex(fid_arr[idx])))
    return out


def scan_records(body, start, n_bytes):
    """Walk records of the form [u32 refID][u32==0x221][...] within [start, start+n_bytes]."""
    recs = []
    p = start
    end = start + n_bytes
    while p + 8 <= end:
        marker = struct.unpack_from("<I", body, p + 4)[0]
        if marker == 0x221:
            refid = struct.unpack_from("<I", body, p)[0]
            field = struct.unpack_from("<I", body, p + 8)[0]
            twob = struct.unpack_from("<H", body, p + 12)[0]
            flags = struct.unpack_from("<H", body, p + 14)[0]
            recs.append((p, refid, field, twob, flags))
            p += 16  # records appear ~20B; advance and let next marker re-anchor
        else:
            p += 1
    return recs


def main():
    # cleanest insertion: 1of2 -> 2of2 region#4 @ +0x324AE and region#5 @ +0x3DD72 (B side longer)
    b1, cf1, fo1, arr1 = load(SAVES["1of2"])
    b2, cf2, fo2, arr2 = load(SAVES["2of2"])
    print(f"formID arrays identical: {arr1 == arr2}  count={len(arr1)}")
    print()

    # Decode the refIDs inserted at 1of2->2of2 (the '3d 00 00 00 ... ff ff 46 15' records).
    # From diff_all_regions: inserted refIDs include 0x5fb52910, 0xb8cdb180, 0x8b26ab90, 0x77ce06e0
    print("=== decode inserted refIDs (1of2->2of2) ===")
    for rid in [0x5fb52910, 0xb8cdb180, 0x8b26ab90, 0x77ce06e0]:
        print(f"  refID 0x{rid:08X}: {decode_refid(rid, arr2)}")
    print()

    # also the 0of2->1of2 inserted refIDs: 0x72563b80, 0x72563b30, and the 'ff ff 00 7a de' records
    print("=== decode inserted refIDs (0of2->1of2 region#4) ===")
    for rid in [0x72563b80, 0x72563b30]:
        print(f"  refID 0x{rid:08X}: {decode_refid(rid, arr1)}")
    print()

    # Resolve the trait target ACTI 0x0021B250 and static REFRs: find their INDEX in the array.
    targets = {"ACTI 0x0021B250": 0x0021B250,
               "REFR 0x00159EB2": 0x00159EB2, "REFR 0x00159F10": 0x00159F10,
               "REFR 0x0016776F": 0x0016776F, "REFR 0x00167770": 0x00167770,
               "REFR 0x002EA231": 0x002EA231, "REFR 0x002EA0D1": 0x002EA0D1}
    print("=== index of trait targets in real formID array ===")
    arrset = {v: i for i, v in enumerate(arr2)}
    # the array stores forms with high byte = save-local plugin index; base-game = 0x00? Try both.
    for name, fid in targets.items():
        hits = [i for i, v in enumerate(arr2) if (v & 0xFFFFFF) == (fid & 0xFFFFFF)]
        print(f"  {name}: low24 matches at indices {hits[:10]} "
              + (f"(highbytes {[hex(arr2[i]>>24) for i in hits[:10]]})" if hits else "(NONE)"))


if __name__ == "__main__":
    main()
