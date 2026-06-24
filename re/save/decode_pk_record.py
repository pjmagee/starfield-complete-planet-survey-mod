"""
DECODE the durable 938333 PlayerKnowledge record that a real trait-scan-target scan writes, by
diffing GlobalData region 1 (the BSComponentDB2 ModuleState region) across the three saves.

CORRECTS the prior `scan-count-store-2026-06-23.md` conclusion ("nothing durable drives the completed
panel"). The completed/named "100% SCANNED" panel DOES persist across reload, and THIS record is what
re-renders it. The record lives in GlobalData REGION 1 (offsetA..offsetB), NOT the ChangeForms section
— which is why the prior FormID-array / ChangeForm search missed it.

RESULT (planet Jemison, target "Sentient Microbial Colony"):
  - record key  : (938333<<48)|(planetId<<16)  — the per-planet PlayerKnowledge subobject
  - GlobalData R1 grows +22 B at scan#1 (CREATE the scanned sublist) and edits IN PLACE at scan#2
  - ids are stored as DB-local component indices = FormID ^ 0x00400000 (bit22 tag), low24 = the
    canonical/keyword object id. Decoded back: 0x0061B250 -> ACTI 0x0021B250 (canonical scan-target),
    0x00625588 -> KYWD 0x00225588 (trait keyword)
  - per-canonical SLOT  : <id u32> <flag u8 (= slot+0x21 scan-flag)> <pct u8 (= slot+0x20 percent)>
  - MEMBER/catalogue array (= slot+0x08 BSTArray<u32>): a count + member ids; on scan#2 the trait
    KEYWORD id is appended here AND the canonical's flag saturates 1 -> 2.

THE FIELD THE MOD'S DIRECT WRITE OMITS: the member/catalogue array (slot+0x08). The mod's
IncrementScanFlag(+0x21)/SetPercentByte(+0x20) populate the flag+pct only; they leave slot+0x08 EMPTY.
The panel-reveal reader ID_124900 reads slot+0x08 to splice the named feature into the completed panel,
so an empty +0x08 => "UNKNOWN FEATURE" / not-complete on reload even though +0x21/+0x20 persisted.
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

CANONICAL_ACTI = 0x0021B250   # base ACTI PlanetTraitScanTarget20SentientMicrobialColonies
TRAIT_KEYWORD  = 0x00225588
DB_INDEX_TAG   = 0x00400000   # bit22 set in the serialized DB-local index


def region1(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body)
    lt = sb.loc_table
    return body[lt["offsetA"]:lt["offsetB"]], lt


def resync(a, b, ia, ib, need=48, maxshift=70000):
    la, lb = len(a), len(b)
    while ia < la - need:
        seg = a[ia:ia + need]
        idx = b.find(seg, ib, min(lb, ib + maxshift))
        if idx != -1:
            return ia, idx
        ia += 1
    return la, lb


def find_grown_record(a, b):
    """Return (ia, ja, ib, jb) of the single region with a positive size delta (the grown record)."""
    ia = ib = 0
    while ia < len(a) and ib < len(b):
        if a[ia] == b[ib]:
            ia += 1; ib += 1; continue
        ja, jb = resync(a, b, ia, ib)
        if (jb - ib) - (ja - ia) > 0:
            return ia, ja, ib, jb
        ia, ib = ja, jb
    return None


def decode_id(raw):
    """raw DB index -> (decoded FormID low24, name)."""
    low24 = raw & 0xFFFFFF
    real = raw ^ DB_INDEX_TAG  # undo bit22 tag
    name = {CANONICAL_ACTI: "ACTI canonical (scan-target base)",
            TRAIT_KEYWORD: "KYWD trait keyword"}.get(real, "?")
    return real, name


def main():
    r0, l0 = region1(SAVES["0of2"])
    r1, l1 = region1(SAVES["1of2"])
    r2, l2 = region1(SAVES["2of2"])
    print("GlobalData R1 lengths: 0of2=%d 1of2=%d 2of2=%d" % (len(r0), len(r1), len(r2)))
    print("changeFormCount: 0of2=%d 1of2=%d 2of2=%d" %
          (l0["changeFormCount"], l1["changeFormCount"], l2["changeFormCount"]))
    print()

    # CREATE: 0of2 -> 1of2
    g = find_grown_record(r0, r1)
    ia, ja, ib, jb = g
    print("CREATE (0of2->1of2): record grows +%d B at R1+0x%X" % ((jb - ib) - (ja - ia), ib))
    print("  0of2 bytes:", r0[ia - 4:ja + 8].hex(" "))
    print("  1of2 bytes:", r1[ib - 4:jb + 8].hex(" "))
    print()

    # Decode the scanned sublist out of each state (aligned on the stable suffix that follows).
    SUFFIX = bytes.fromhex("03000000" + "00000000" + "a2cd9050")
    for name, buf in [("0of2", r0), ("1of2", r1), ("2of2", r2)]:
        # find the per-planet record start: the tagged-size dword 0x8000003X just before the sublist.
        # Locate the stable suffix nearest to the grown region, then walk back to the tagged size.
        anchor = buf.find(SUFFIX, max(0, ib - 64))
        # sublist = bytes between the tagged-size body start and the stable suffix
        # tagged-size dword sits ~ a few bytes before; we just dump the window before the suffix.
        start = anchor - 40 if anchor >= 40 else 0
        window = buf[start:anchor]
        print("%s scanned-sublist window (ends at stable suffix): %s" % (name, window.hex(" ")))
        for idv, lbl in [(CANONICAL_ACTI ^ DB_INDEX_TAG, "CANON"), (TRAIT_KEYWORD ^ DB_INDEX_TAG, "KWD")]:
            pos = window.find(struct.pack("<I", idv))
            if pos >= 0:
                real, nm = decode_id(idv)
                print("    %-5s dbidx=0x%08X -> FormID 0x%08X (%s)  flag(+0x21)=%d pct(+0x20)=%d"
                      % (lbl, idv, real, nm, window[pos + 4], window[pos + 5]))
        print()

    print("CONCLUSION: per-canonical slot = <id><flag=+0x21><pct=+0x20>; the MEMBER array (slot+0x08)")
    print("gains the trait keyword id on scan#2 and the canonical flag saturates 1->2. The mod's bare")
    print("ID_124898/ID_124899 write sets flag+pct but leaves the member array EMPTY -> ID_124900 panel")
    print("reveal reads nothing -> no named/'100% SCANNED' on reload. FIX: drive ID_52158 (the member")
    print("populator), not bare ID_124898/124899.")


if __name__ == "__main__":
    main()
