"""
Empirical scanner over a decompressed SFS_SAVEGAME body.

Goal: locate (or prove absent) the trait-survey state the RE identified:
  - static trait scan-target REFR FormIDs (trait-20 Jemison statics):
      0x00159EB2 0x00159F10 0x0016776F 0x00167770 0x002EA231 0x002EA0D1
  - the scan-target base ACTI 0x0021B250
  - keyword 0x001CBEA3, LocRefType 0x0027A567
  - knowledge-DB form ids referenced by the engine: 938333 / 937887 / 939118 are NOT
    save FormIDs -- they are runtime BSGalaxy module/component type ids; we instead look
    for ASCII tags "PlayerKnowledge", "Scannable", "BSGalaxy", section markers, and the
    GlobalData chunk type ids.

FormIDs in a save are stored against the local plugin index table. A base-game FormID
0x00xxxxxx in Starfield.esm keeps load-order index 0x00 at runtime, but in the save the
high byte is the *save-local* plugin index (usually 0x00 for Starfield.esm = first master).
We scan for both the raw 4-byte LE pattern and the low-24-bit pattern with any high byte.
"""
import struct
import sys
import re as _re

TRAIT20_REFRS = [0x00159EB2, 0x00159F10, 0x0016776F, 0x00167770, 0x002EA231, 0x002EA0D1]
SCAN_TARGET_ACTI = 0x0021B250
KW_HANDSCAN = 0x001CBEA3
LOCREF_TYPE = 0x0027A567

ASCII_TAGS = [
    b"PlayerKnowledge", b"Scannable", b"BSGalaxy", b"ScannableComponent",
    b"SFS_SAVEGAME", b"ChangeForms", b"GlobalData", b"PlanetData", b"SurfaceTree",
    b"Knowledge", b"Survey", b"Planet", b"Biome",
]


def find_le32(buf, val):
    """All offsets where the 4-byte LE encoding of val appears."""
    pat = struct.pack("<I", val)
    out = []
    i = buf.find(pat)
    while i != -1:
        out.append(i)
        i = buf.find(pat, i + 1)
    return out


def find_low24_anyhigh(buf, val24):
    """Offsets where low-3-byte LE == val24 (high byte = any plugin index)."""
    lo = struct.pack("<I", val24 & 0xFFFFFF)[:3]
    out = []
    i = buf.find(lo)
    while i != -1:
        # ensure 4 bytes available; high byte is whatever follows
        if i + 4 <= len(buf):
            out.append((i, buf[i + 3]))
        i = buf.find(lo, i + 1)
    return out


def main():
    body = open(sys.argv[1], "rb").read()
    print(f"body {len(body):,} bytes\n")

    print("=== ASCII tag presence ===")
    for tag in ASCII_TAGS:
        c = body.count(tag)
        if c:
            first = body.find(tag)
            print(f"  {tag.decode():22} x{c:<4} first@0x{first:x}")
        else:
            print(f"  {tag.decode():22} ABSENT")

    print("\n=== trait-20 scan-target REFR FormIDs (exact LE32) ===")
    for fid in TRAIT20_REFRS:
        offs = find_le32(body, fid)
        print(f"  0x{fid:08X}  exact-LE32 hits: {len(offs)}"
              + (f"  @ {[hex(o) for o in offs[:8]]}" if offs else ""))

    print("\n=== trait-20 REFR low-24 (any high byte / plugin idx) ===")
    for fid in TRAIT20_REFRS:
        hits = find_low24_anyhigh(body, fid)
        # group by high byte
        from collections import Counter
        hb = Counter(h for _, h in hits)
        print(f"  low24 0x{fid & 0xFFFFFF:06X}: {len(hits)} hits, highbytes={dict(hb)}")

    print("\n=== base ACTI / keyword / locref (exact LE32) ===")
    for name, fid in [("ACTI 0x0021B250", SCAN_TARGET_ACTI),
                      ("KW 0x001CBEA3", KW_HANDSCAN),
                      ("LOCREF 0x0027A567", LOCREF_TYPE)]:
        offs = find_le32(body, fid)
        print(f"  {name:22} exact hits: {len(offs)}"
              + (f"  @ {[hex(o) for o in offs[:6]]}" if offs else ""))

    # context dump around first trait REFR hit, if any
    print("\n=== context around first trait-REFR exact hit ===")
    for fid in TRAIT20_REFRS:
        offs = find_le32(body, fid)
        if offs:
            o = offs[0]
            lo = max(0, o - 32)
            hi = min(len(body), o + 48)
            seg = body[lo:hi]
            print(f"  0x{fid:08X} @0x{o:x}:")
            print("   ", seg.hex(" "))
            break
    else:
        print("  (no exact trait-REFR hit at all)")


if __name__ == "__main__":
    main()
