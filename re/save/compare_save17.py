import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "Save12_UNSCANNED(0of2)": BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "Save14_REALSCAN(2of2)":  BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
    "Save17_MODWRITE":        BASE + r"\Save17_98A838ADM467265736820436861726163746572_000043_20260623204532_1_0_4.sfs",
}
CANON = 0x0021B250  # ACTI canonical
KWD   = 0x00225588  # trait keyword (the member that goes into slot+0x08)
TAG   = 0x00400000  # serialized DB-local index = FormID ^ TAG

def region1(path):
    body = BCPSContainer(path).decompress()
    sb = SFSBody(body)
    lt = sb.loc_table
    return body[lt["offsetA"]:lt["offsetB"]], lt

def find_all(buf, needle):
    out, i = [], buf.find(needle)
    while i != -1:
        out.append(i); i = buf.find(needle, i + 1)
    return out

for name, path in SAVES.items():
    try:
        r, lt = region1(path)
    except Exception as e:
        print(f"=== {name}: ERROR {e} ===\n"); continue
    print(f"=== {name}: R1 len={len(r):,}  changeFormCount={lt.get('changeFormCount')} ===")
    for lbl, fid in [("CANON-ACTI 0x%08X" % CANON, CANON), ("TRAIT-KWD 0x%08X" % KWD, KWD)]:
        dbidx = fid ^ TAG
        le = struct.pack("<I", dbidx)
        hits = find_all(r, le)
        if not hits:
            print(f"  {lbl}: dbidx 0x{dbidx:08X} -> ABSENT from R1")
        for pos in hits[:4]:
            ctx = r[max(0,pos-6):pos+18].hex(" ")
            # per-canonical slot layout: <id u32><flag u8=+0x21? actually next byte><pct u8>
            f1, p1 = (r[pos+4] if pos+4 < len(r) else -1), (r[pos+5] if pos+5 < len(r) else -1)
            print(f"  {lbl}: dbidx 0x{dbidx:08X} @ R1+0x{pos:X}  nextbytes[+4]={f1} [+5]={p1}  ctx: {ctx}")
    print()

print(">> KEY: Save14 (real) should show CANON (flag saturated) AND the TRAIT-KWD present (the +0x08 member).")
print(">> If Save17 (mod) shows CANON but NO TRAIT-KWD, the member array slot+0x08 did NOT populate -> fix incomplete.")
print(">> If Save17 matches Save14 (both have CANON + KWD), the durable record is correct -> renders on reload.")
