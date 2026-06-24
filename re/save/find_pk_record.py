"""
Locate the 938333 PlayerKnowledge ChangeForm that scan#1 CREATES (absent in 0of2, present in
1of2/2of2) and decode the per-(planet,canonical) survey slot it carries.

Strategy: the record is NOT in the formID array (GlobalData/singleton). It is added at scan#1
(changeFormCount 15362->15363). We do NOT rely on perfect ChangeForm parsing; instead we find the
record by CONTENT: the per-(planet) PlayerKnowledge slot serializes, among other things, the canonical
id of the scanned target (= base ACTI 0x0021B250) and/or the member species form-IDs the +0x08 array
holds. We scan the WHOLE decompressed body of each save for:

  - the canonical id 0x0021B250 (base ACTI) as LE32  -> should appear in 1of2/2of2 but not 0of2 if
    the slot key is stored raw
  - the 17 Jemison species form-IDs (the +0x08 member catalogue array elements) as LE32
  - the planet id 0x0003F5A1 with NEW context

Then we 3-way diff the body to find the unique inserted region that contains these, and decode it.
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

# canonical / species ids of interest (from species-scan-complete-model 17/17 ground truth, planet 0x0003F5A1)
CANONICAL = 0x0021B250          # base ACTI PlanetTraitScanTarget20SentientMicrobialColonies
PLANET    = 0x0003F5A1          # Jemison PNDT
SPECIES = [0x00185478,0x00185479,0x0018547F,0x00185489,0x001854C1,0x001854D8,0x002F80A0,0x002F80BB,
           0x00048A34,0x0019B898,0x0019B899,0x0019B89A,0x0019B89B,0x0019B89C,0x0019B89D,0x0019B89E,0x0019B89F]
# trait scan-target REFRs + keyword
TRAIT_REFRS = [0x00159EB2,0x00159F10,0x0016776F,0x00167770,0x002EA231,0x002EA0D1]
TRAIT_KWD = 0x00225588


def find_all(body, val):
    pat = struct.pack("<I", val)
    out = []
    i = body.find(pat)
    while i != -1:
        out.append(i)
        i = body.find(pat, i + 1)
    return out


def main():
    bodies = {k: BCPSContainer(p).decompress() for k, p in SAVES.items()}
    print("body sizes:", {k: len(v) for k, v in bodies.items()})
    print()
    targets = [("CANONICAL ACTI", CANONICAL), ("PLANET", PLANET), ("TRAIT_KWD", TRAIT_KWD)]
    # count occurrences of each id per save
    def count(val):
        return {k: len(find_all(b, val)) for k, b in bodies.items()}
    for name, val in targets:
        print(f"{name} 0x{val:08X}: counts {count(val)}")
    print()
    print("species member ids (the +0x08 catalogue array elements):")
    for s in SPECIES:
        c = count(s)
        # only print if it differs across saves (a NEW occurrence = inserted into the record)
        if len(set(c.values())) > 1:
            print(f"  0x{s:08X}: {c}   <-- DIFFERS")
        else:
            print(f"  0x{s:08X}: {c}")
    print()
    print("trait REFRs:")
    for r in TRAIT_REFRS:
        print(f"  0x{r:08X}: {count(r)}")


if __name__ == "__main__":
    main()
