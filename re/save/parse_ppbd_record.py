"""
Parse ONE CTPerBiomeData record from the GlobalData-R1 stream against the known
struct layout, to pin the +6 KNOWN byte to a struct field and decode e1 3f 01 00.

CTPerBiomeData struct (from ID_51395 clone):
  +0x00 BSTArray<u32>      (4-byte elems)
  +0x10 BSTArray<u32>      (4-byte elems)
  +0x20 BSTArray<{u64,u32}>(0xC-byte elems, ID_51429)
  +0x30 BSTArray<{u64,u64}>(0x10-byte elems, ID_51430)
  +0x40 u32 ; +0x44 u32 ; +0x48 u32 ; +0x4C u32
  +0x50 u64 ; +0x58 u64 ; +0x60 u64 ; +0x68 u32

But the SAVE STREAM framing wraps each StoredComponent. We hex-dump a wide window
around the flipped record and the first unscanned record, side by side, to see the
true field boundaries.
"""
import sys, struct
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
P12 = BASE + r"\Save12_98A838ADM467265736820436861726163746572_000043_20260623182623_1_0_4.sfs"
P14 = BASE + r"\Save14_98A838ADM467265736820436861726163746572_000043_20260623182648_1_0_4.sfs"

def r1(path):
    b = BCPSContainer(path).decompress(); sb = SFSBody(b); lt = sb.loc_table
    return b[lt["offsetA"]:lt["offsetB"]]

r12, r14 = r1(P12), r1(P14)
DISC = bytes.fromhex("e13f0100")

def find_all(buf, n, s=0):
    out=[]; i=buf.find(n,s)
    while i!=-1: out.append(i); i=buf.find(n,i+1)
    return out

h14 = find_all(r14, DISC)
# Find the flipped record
flip = [p for p in h14 if r14[p+6]!=0][0]
# An unscanned record for contrast (use the one right before)
idx = h14.index(flip)
prev = h14[idx-1]

print("FLIPPED record (Save14, +6=1):")
print(f"  @R1+0x{flip:X}")
for off in range(-4, 56, 16):
    a=flip+off
    print(f"    [{off:+4d}] {r14[a:a+16].hex(' ')}")

print("\nUNSCANNED record just before it (Save14, +6=0):")
print(f"  @R1+0x{prev:X}")
for off in range(-4, 56, 16):
    a=prev+off
    print(f"    [{off:+4d}] {r14[a:a+16].hex(' ')}")

# Now: is e1 3f 01 00 a CONSTANT prefix? Check the bytes that vary vs constant across all 31.
print("\nColumn-constancy across all 31 records (Save14), bytes [0..40] from each e13f0100 hit:")
cols = []
recs = [r14[p:p+40] for p in h14]
for c in range(40):
    vals = set(rec[c] for rec in recs if c < len(rec))
    cols.append('..' if len(vals)>1 else f'{next(iter(vals)):02x}')
print("   " + " ".join(cols))
print("   (.. = varies across records; hex = constant in all 31)")

# Decode the trailing 'xx 03 40' ids and the 16-byte block per record
print("\nPer-record decode (first 8): KNOWN@+6, count@+7, id@+0xB, then 16B, then id2@+0x1B:")
for p in h14[:8] + [flip]:
    known = r14[p+6]
    cnt   = struct.unpack_from('<I', r14, p+7)[0]
    id1   = struct.unpack_from('<I', r14, p+0xB)[0]
    blk   = r14[p+0xF:p+0x1F].hex(' ')
    id2   = struct.unpack_from('<I', r14, p+0x1B)[0]
    tag = " <<FLIP" if p==flip else ""
    print(f"  @0x{p:05X} known={known} cnt={cnt} id1=0x{id1:08X} id2=0x{id2:08X} blk16=[{blk}]{tag}")
