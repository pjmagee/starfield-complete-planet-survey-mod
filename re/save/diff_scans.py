import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES = {
    "0of2": BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs",
    "1of2": BASE + r"\Save13_98A838ADM467265736820436861726163746572_000043_20260623182638_1_0_4.sfs",
    "2of2": BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs",
}

bodies = {}
for k, p in SAVES.items():
    c = BCPSContainer(p)
    bodies[k] = c.decompress()
    print(f"{k}: decompressed {len(bodies[k]):,} bytes (compressed file {len(c.raw):,})")

b0, b1, b2 = bodies["0of2"], bodies["1of2"], bodies["2of2"]
print()

# ---- search for the Microbial canonical / trait keyword in each body ----
def find_all(buf, needle):
    out, i = [], buf.find(needle)
    while i != -1:
        out.append(i); i = buf.find(needle, i + 1)
    return out

for name, val in [("canon 0x0021B250", 0x0021B250),
                  ("traitKW 0x00225588", 0x00225588)]:
    le = struct.pack("<I", val)
    print(f"{name} LE={le.hex()}: in 0of2={len(find_all(b0,le))}  1of2={len(find_all(b1,le))}  2of2={len(find_all(b2,le))}")
print()

if len(b0) == len(b1) == len(b2):
    print(f"=== ALL SAME SIZE ({len(b0):,}) -> aligned byte diff ===")
    n = len(b0)
    incr, zeroset, other = [], [], []
    for i in range(n):
        a, bb, cc = b0[i], b1[i], b2[i]
        if a == bb == cc:
            continue
        if bb == (a + 1) & 0xff and cc == (bb + 1) & 0xff:
            incr.append((i, a, bb, cc))
        elif a == 0 and bb != 0 and cc != 0:
            zeroset.append((i, a, bb, cc))
        else:
            other.append((i, a, bb, cc))
    print(f"total differing positions: {len(incr)+len(zeroset)+len(other)}")
    print(f"\n--- INCREMENT 0->1->2 (v, v+1, v+2) : {len(incr)} (TOP COUNT CANDIDATES) ---")
    for i, a, bb, cc in incr[:60]:
        ctx = b2[max(0,i-12):i+12].hex(" ")
        print(f"  0x{i:07X}: {a:3d} {bb:3d} {cc:3d}   ctx2of2: {ctx}")
    print(f"\n--- 0 -> nonzero -> nonzero : {len(zeroset)} ---")
    for i, a, bb, cc in zeroset[:40]:
        ctx = b2[max(0,i-12):i+12].hex(" ")
        print(f"  0x{i:07X}: {a:3d} {bb:3d} {cc:3d}   ctx2of2: {ctx}")
    print(f"\n--- other changes: {len(other)} (showing 30) ---")
    for i, a, bb, cc in other[:30]:
        print(f"  0x{i:07X}: {a:3d} {bb:3d} {cc:3d}")
else:
    print(f"=== DIFFERENT SIZES: 0of2={len(b0):,} 1of2={len(b1):,} 2of2={len(b2):,} ===")
    # common prefix / suffix to localize the inserted/changed region
    def common_prefix(x, y):
        n = min(len(x), len(y)); i = 0
        while i < n and x[i] == y[i]: i += 1
        return i
    def common_suffix(x, y):
        n = min(len(x), len(y)); i = 0
        while i < n and x[-1-i] == y[-1-i]: i += 1
        return i
    for a, b, lab in [(b0,b1,"0of2 vs 1of2"), (b1,b2,"1of2 vs 2of2")]:
        p = common_prefix(a, b); s = common_suffix(a, b)
        print(f"\n{lab}: common prefix=0x{p:X}, common suffix=0x{s:X}, "
              f"changed region A[0x{p:X}:0x{len(a)-s:X}] (len {len(a)-s-p}), "
              f"B[0x{p:X}:0x{len(b)-s:X}] (len {len(b)-s-p})")
        ra = a[p:len(a)-s]; rb = b[p:len(b)-s]
        print(f"  A region head: {ra[:48].hex(' ')}")
        print(f"  B region head: {rb[:48].hex(' ')}")
