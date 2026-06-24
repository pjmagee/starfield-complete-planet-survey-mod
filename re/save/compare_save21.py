import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "Save12_0of2":      BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "Save14_REAL_2of2": BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
    "Save21_MOD_v4":    BASE + r"\Save21_98A838ADM467265736820436861726163746572_000043_20260623233715_1_0_4.sfs",
}
CANON = 0x0021B250
KWD   = 0x00225588
TAG   = 0x00400000
KNOWN_DISC = bytes.fromhex("e13f0100")  # 0x00013FE1 LE  (the known-set record discriminator)

def find_all(buf, needle):
    out, i = [], buf.find(needle)
    while i != -1:
        out.append(i); i = buf.find(needle, i + 1)
    return out

for name, path in SAVES.items():
    try:
        body = BCPSContainer(path).decompress()
    except Exception as e:
        print(f"{name}: ERR {e}"); continue
    sb = SFSBody(body); lt = sb.loc_table
    r1 = body[lt["offsetA"]:lt["offsetB"]]

    # --- KNOWN-SET (0x00013FE1): count records whose KNOWN-BYTE (rec+0x06) != 0  (the render gate) ---
    recs = find_all(r1, KNOWN_DISC)
    flipped = [p for p in recs if p+6 < len(r1) and r1[p+6] != 0]
    sample = ""
    if flipped:
        p = flipped[0]
        sample = f"  e.g. @R1+0x{p:X}: {r1[p:p+15].hex(' ')}  (known-byte@+6={r1[p+6]}, cluster=0x{struct.unpack_from('<I', r1, p+0x0B)[0]:08X})"

    # --- 938333 scanned slot (flag/pct) + KWD presence (the previous fix, should still match) ---
    kwd_hits = len(find_all(r1, struct.pack("<I", KWD ^ TAG)))
    slot = "?"
    for pos in find_all(r1, struct.pack("<I", CANON ^ TAG)):
        f1 = r1[pos+4] if pos+4 < len(r1) else -1
        p1 = r1[pos+5] if pos+5 < len(r1) else -1
        if 0 <= f1 <= 12 and p1 == 100:
            slot = f"flag={f1} pct={p1}"; break

    print(f"=== {name} ===")
    print(f"  KNOWN-SET (0x00013FE1): {len(recs)} records, {len(flipped)} with KNOWN-BYTE!=0  <<< the render gate")
    if sample: print(sample)
    print(f"  938333 scanned slot: {slot}   |   trait-KWD in pooled array: {kwd_hits} hit(s)")
    print()

print(">> Save14 (real, renders) = 1 known-byte flipped. Save12 (unscanned) = 0.")
print(">> Save21 should now show >=1 flipped (matching Save14) AND slot flag=2 + KWD present -> renders complete.")
