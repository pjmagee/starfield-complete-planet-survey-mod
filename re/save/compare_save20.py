import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "Save12_0of2":          BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "Save20_REAL_1of2":     BASE + r"\Save20_98A838ADM467265736820436861726163746572_000043_20260623225115_1_0_4.sfs",
    "Save14_REAL_2of2":     BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
}
CANON = 0x0021B250
KWD   = 0x00225588
TAG   = 0x00400000

def find_all(buf, needle):
    out, i = [], buf.find(needle)
    while i != -1:
        out.append(i); i = buf.find(needle, i + 1)
    return out

bodies = {}
for name, path in SAVES.items():
    try:
        bodies[name] = BCPSContainer(path).decompress()
    except Exception as e:
        print(f"{name}: ERR {e}")

# Full-body search for the keyword + canonical in BOTH raw and ^0x400000 DB-tagged forms.
print("=== FULL-BODY search (raw LE + DB-tagged ^0x400000) ===")
for label, val in [("CANON-ACTI", CANON), ("TRAIT-KWD", KWD)]:
    for tagname, v in [("raw", val), ("dbtag", val ^ TAG)]:
        le = struct.pack("<I", v)
        line = f"  {label} {tagname} 0x{v:08X}:"
        for name in SAVES:
            if name in bodies:
                line += f"  {name.split('_')[0]}={len(find_all(bodies[name], le))}"
        print(line)
print()

# 938333 region-1 trait slot for each (flag/pct + KWD presence in the pooled/slot area).
print("=== 938333 GlobalData R1 trait slot (flag/pct after the canonical id) ===")
for name in SAVES:
    if name not in bodies:
        continue
    sb = SFSBody(bodies[name]); lt = sb.loc_table
    r1 = bodies[name][lt["offsetA"]:lt["offsetB"]]
    print(f"\n--- {name}: R1 len={len(r1):,} changeFormCount={lt.get('changeFormCount')} ---")
    for pos in find_all(r1, struct.pack("<I", CANON ^ TAG)):
        f1 = r1[pos+4] if pos+4 < len(r1) else -1
        p1 = r1[pos+5] if pos+5 < len(r1) else -1
        # only the "scanned slot" copies (flag is a small number 0..10, pct 100); skip starmap (flag 100+)
        tag = " <== SCANNED SLOT" if (0 <= f1 <= 12 and p1 == 100) else ""
        print(f"   CANON @R1+0x{pos:X}  flag={f1} pct={p1}  ctx:{r1[max(0,pos-6):pos+18].hex(' ')}{tag}")
    kwd = find_all(r1, struct.pack("<I", KWD ^ TAG))
    print(f"   TRAIT-KWD (dbtag 0x{KWD^TAG:08X}) in R1: {len(kwd)} hit(s)" +
          (f" @R1+0x{kwd[0]:X} ctx:{r1[max(0,kwd[0]-6):kwd[0]+14].hex(' ')}" if kwd else ""))
