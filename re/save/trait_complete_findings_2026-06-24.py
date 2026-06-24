"""
trait_complete_findings_2026-06-24.py  — REPRODUCIBLE FINDINGS SCRIPT
====================================================================
Consolidated, reproducible verification of EXACTLY what a real trait-scan completion (2/2)
writes to the save that the mod's `CompletePlanet "traits"` write does NOT produce.

Saves (same character/planet/spot, Jemison 0x0003F5A1, trait kw 0x00225588, scan-target
ACTI 0x0021B250):
  Save28 = BASELINE 0/2     Save29 = REAL 1/2
  Save30 = REAL 2/2 TARGET   Save31 = MOD (CompletePlanet "traits" from Save28)

VERDICT (all decode-verified against the bytes below):
  The durable record is the 938333 PlayerKnowledge per-planet sub-record in GlobalData
  REGION 1 (BSComponentDB2 ModuleState). The real 2/2 record is a clean 58-byte structure;
  the MOD writes a MALFORMED 152-byte structure (wrong/extra ARRAY_A keyword members + a
  corrupt 6-entry ARRAY_B with misaligned ids). That corruption is why the in-world object
  reloads as "UNKNOWN FEATURE 0/2".

  Secondary durable differences the mod also misses:
   - ChangeForm count: real 15364->15365->15366 (+1 per scan); MOD stays 15364 (+0).
   - A FormID 0x0084C0E8 is written into 7 R1 knowledge sub-records by the real completion;
     the mod writes it into only 1 (the shared survey-marker record). (survey-complete cascade)

  Ruled OUT as the gate (present in the MOD save too, so NOT the differentiator):
   - The 0x00DE7A00-tagged 20-byte fixed-stride records in the ChangeForms region
     (materialization state — appears whenever the scan-target refs are loaded; Save31 has 2).
   - Raw scan-target/keyword FormIDs in ChangeForms (0x0021B250 / 0x00225588 appear NOWHERE
     as raw dwords — the prior CLOC-LocRef theory does not hold for these saves).
   - The formID-array trailer (count 311117 identical in all 4; only its file offset shifts).

938333 RECORD GRAMMAR (bounded planetId..suffix 'a2 cd 90 50'), decode-verified on 28/29/30:
  +0x00 u32  planetId        = 0x0043F5A1  (= 0x0003F5A1 ^ 0x00400000, DB-local index)
  +0x04 u16  zero            = 0
  +0x06 u16  typeFlags       = 0x0036 (empty) -> 0x003E (has data)
  +0x08 u32  taggedSize      = 0x00038000
  +0x0C u32  reserved        = 0
  +0x10 u32  ARRAY_A count   = pooled keyword member array (THIS IS slot+0x08)
        u32 * count  members = keywordId ^ 0x00400000
        u32  ARRAY_B count   = canonical-slot array
        u32  reserved2       = 0
        per slot (10B): <id u32 ^TAG> <flag u8 (=slot+0x21)> <pct u8 (=slot+0x20)> <pad u32>
  trailer: u32 0x00000003 , u32 0 , suffix 'a2 cd 90 50'
"""
import sys, struct, glob, os
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE=r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
SAVES={n:glob.glob(os.path.join(BASE,f"Save{n}_*.sfs"))[0] for n in (28,29,30,31)}
TAG=0x00400000; PLANET=0x0003F5A1; ACTI0=0x0021B250; KWD=0x00225588
SUF=bytes.fromhex("a2cd9050")
def untag(v): return (v^TAG) if (v&TAG) else v

def load(n):
    body=BCPSContainer(SAVES[n]).decompress(); sb=SFSBody(body); lt=sb.loc_table
    base=lt["table_base"]; fid=struct.unpack_from("<I",body,base-8)[0]
    return body, lt, fid

def get_record(body, lt):
    r1=body[lt["offsetA"]:lt["offsetB"]]
    pid=struct.pack("<I",PLANET^TAG); s=0
    while True:
        p=r1.find(pid,s)
        if p<0: return None,None
        suf=r1.find(SUF,p,p+0x200)
        if suf!=-1: return lt["offsetA"]+p, r1[p:suf+4]
        s=p+1

def decode_record(r):
    o=0
    pid=struct.unpack_from("<I",r,o)[0]; o+=4
    z=struct.unpack_from("<H",r,o)[0]; o+=2
    tf=struct.unpack_from("<H",r,o)[0]; o+=2
    tsz=struct.unpack_from("<I",r,o)[0]; o+=4
    rsv=struct.unpack_from("<I",r,o)[0]; o+=4
    ca=struct.unpack_from("<I",r,o)[0]; o+=4
    A=[struct.unpack_from("<I",r,o+4*i)[0] for i in range(ca)]; o+=4*ca
    cb=struct.unpack_from("<I",r,o)[0]; o+=4
    rsv2=struct.unpack_from("<I",r,o)[0]; o+=4
    slots=[]
    for i in range(cb):
        if o+10>len(r): break
        sid=struct.unpack_from("<I",r,o)[0]; flag=r[o+4]; pct=r[o+5]; o+=10
        slots.append((sid,flag,pct))
    return dict(planet=untag(pid),tf=tf,tsz=tsz,A=[untag(x) for x in A],cb=cb,slots=slots,
                consumed=o,tail=r[o:])

def main():
    print("="*82)
    print("SECTION MAP + 938333 RECORD + CHANGEFORM COUNT")
    print("="*82)
    print(f"{'save':14}{'CFcount':>8}{'R1 len':>9}{'rec @':>11}{'recLen':>8}  0x0084C0E8(R1)")
    for n in (28,29,30,31):
        body,lt,fid=load(n)
        off,rec=get_record(body,lt)
        c=body[lt["offsetA"]:lt["offsetB"]].count(bytes.fromhex("e8c08400"))
        tagn={28:'BASELINE 0/2',29:'REAL 1/2',30:'REAL 2/2 *',31:'MOD broken'}[n]
        print(f"{tagn:14}{lt['changeFormCount']:>8}{lt['offsetB']-lt['offsetA']:>9}  0x{off:06X}{len(rec):>8}  {c}")
    print()
    print("="*82); print("938333 RECORD (raw bytes + decode)"); print("="*82)
    for n in (28,29,30,31):
        body,lt,fid=load(n)
        off,rec=get_record(body,lt)
        d=decode_record(rec)
        print(f"Save{n}  ({len(rec)}B @0x{off:06X}):")
        print(f"  raw: {rec.hex()}")
        print(f"  planet=0x{d['planet']:07X} tf=0x{d['tf']:04X} | ARRAY_A(slot+0x08) cnt={len(d['A'])} "
              f"members={[f'0x{x:08X}' for x in d['A']]}")
        print(f"  ARRAY_B cnt={d['cb']} slots="
              + ", ".join(f"[id=0x{untag(s[0]):08X} flag={s[1]} pct={s[2]}]" for s in d['slots']))
        if d['consumed']!=len(rec)-0:
            print(f"  (decode consumed {d['consumed']}/{len(rec)}, tail={d['tail'].hex(' ')})")
        print()
    print("="*82); print("THE COMPLETING DELTA  (REAL 1/2 -> 2/2)"); print("="*82)
    print("  ARRAY_A (slot+0x08 pooled keyword array): []  ->  [0x00225588]   (append trait keyword)")
    print("  ARRAY_B slot[0].flag (slot+0x21):          1  ->  2              (scan flag saturates)")
    print("  pct (slot+0x20) stays 100; ARRAY_B count stays 1.")
    print()
    print("  MOD (Save31) instead writes ARRAY_A=[0x225590,0x22558D,0x225588] (2 bogus + 1 real)")
    print("  and a corrupt ARRAY_B count=6 with misaligned ids -> 152B malformed record.")

if __name__=="__main__":
    main()
