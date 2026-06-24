"""
diff_trait_sections_2026-06-24.py
=================================
Clean PER-SECTION diff (R1, R2, R3, CHANGEFORMS) for the 4 trait saves, plus a proper
938333 per-planet PlayerKnowledge record extractor that aligns on the planet sub-record
(not a fixed byte window).

The whole-body anchored diff is useless here because R3/CHANGEFORMS reorder wholesale between
real(30) and mod(31); a single greedy resync produces one giant 2 MB "run". Instead we diff
each section independently and, for CHANGEFORMS, index records by formID.

Run:
  py diff_trait_sections_2026-06-24.py pk        # 938333 record, all 4 saves, aligned
  py diff_trait_sections_2026-06-24.py sec A B    # per-section anchored diff (A,B in 28/29/30/31)
  py diff_trait_sections_2026-06-24.py r1growth   # 28->29->30 R1 growth (the completing record)
"""
import sys, struct, glob, os
sys.path.insert(0, r"D:\Projects\pjmagee\starfield-complete-planet-survey-mod\re\save")
from sfs_container import BCPSContainer
from sfs_body import SFSBody

BASE = r"C:\Users\patri\OneDrive\Documents\My Games\Starfield\Saves"
def find_save(n):
    return glob.glob(os.path.join(BASE, f"Save{n}_*.sfs"))[0]
SAVES = {n: find_save(n) for n in (28,29,30,31)}

TAG=0x00400000
ACTI0=0x0021B250; ACTI1=0x0021B252; KWD=0x00225588; PLANET=0x0003F5A1

_cache={}
def load(n):
    if n in _cache: return _cache[n]
    body=BCPSContainer(SAVES[n]).decompress()
    sb=SFSBody(body); lt=sb.loc_table
    base=lt["table_base"]
    fid_off=struct.unpack_from("<I", body, base-8)[0]
    regs={"R1":(lt["offsetA"],lt["offsetB"]),"R2":(lt["offsetB"],lt["offsetC"]),
          "R3":(lt["offsetC"],lt["changeFormsOffset"]),
          "CHANGEFORMS":(lt["changeFormsOffset"], fid_off),
          "fid_off":fid_off,"lt":lt}
    _cache[n]=(body,regs); return body,regs

def hexrow(buf, base, off, n):
    s=buf[off:off+n]; out=[]
    for r in range(0,n,16):
        row=s[r:r+16]
        asc=''.join(chr(c) if 32<=c<127 else '.' for c in row)
        out.append('    @0x%06X: %-47s %s'%(base+r,row.hex(' '),asc))
    return '\n'.join(out)

def anchored_runs(A,B,W=16):
    idxB={}
    for i in range(0,len(B)-W): idxB.setdefault(B[i:i+W],i)
    i=j=0; N=len(A); runs=[]
    while i<N:
        win=A[i:i+W]; jb=idxB.get(win,-1)
        if jb!=-1 and jb>=j:
            gi1,gj1=i,j
            if (i-gi1) or (jb-gj1): runs.append((gi1,i,gj1,jb))
            sj=jb
            while i<N and sj<len(B) and A[i]==B[sj]: i+=1; sj+=1
            j=sj
        else: i+=1
    if j<len(B) or i<N: runs.append((i,N,j,len(B)))
    return [(a,b,c,d) for (a,b,c,d) in runs if (b-a) or (d-c)]

# ----- 938333 per-planet record -----
# The 938333 record for the planet is a PlayerKnowledge sub-blob. We anchor on the planet
# sub-record: the bytes around the canonical ACTI0^TAG slot, expanded to the surrounding
# pooled member array + slot. We locate the FULL planet record by finding the tagged-size
# header 0x8000003X that precedes it and the stable suffix that follows.
def find_planet_record(body, regs):
    a,b=regs["R1"]; r1=body[a:b]
    can=struct.pack("<I", ACTI0^TAG)        # 50 b2 61 00
    # there can be several ACTI0^TAG (3). The 938333 per-planet slot is the one adjacent to
    # the planet-record header. Find every occurrence and pick the cluster with a 0x8000003X
    # tagged-size dword and the a2cd9050 stable suffix nearby.
    occ=[]; start=0
    while True:
        p=r1.find(can,start)
        if p<0: break
        occ.append(p); start=p+1
    results=[]
    for p in occ:
        win=r1[max(0,p-0x60):p+0x40]
        results.append((a+max(0,p-0x60), win, p))
    return results

def cmd_pk():
    print("="*84)
    print("938333 per-planet PlayerKnowledge slot windows (anchor = ACTI0^TAG 50 b2 61 00)")
    print("="*84)
    for n in (28,29,30,31):
        body,regs=load(n)
        res=find_planet_record(body,regs)
        print(f"\n----- Save{n}: {len(res)} ACTI0^TAG slot(s) in R1 -----")
        for off,win,p in res:
            print(f"  slot @R1abs 0x{off+0x60:06X} (window base 0x{off:06X}):")
            print(hexrow(body, off, off, len(win)))
    # Now align Save30 vs Save31 around the KWD^TAG (88 55 62 00) which only exists at 2/2:
    print("\n"+"="*84)
    print("KWD^TAG (88 55 62 00) neighborhood — only present at 2/2 (Save30 real, Save31 mod)")
    print("="*84)
    for n in (28,29,30,31):
        body,regs=load(n)
        a,b=regs["R1"]; r1=body[a:b]
        kw=struct.pack("<I", KWD^TAG)
        p=r1.find(kw)
        if p<0:
            print(f"  Save{n}: KWD^TAG ABSENT in R1")
        else:
            s=max(0,p-0x30); e=min(len(r1),p+0x30)
            print(f"  Save{n}: KWD^TAG @R1abs 0x{a+p:06X}")
            print(hexrow(body, a+s, a+s, e-s))
    print()

def cmd_sec(nA,nB):
    body_a,regs_a=load(nA); body_b,regs_b=load(nB)
    for sec in ("R1","R2","R3","CHANGEFORMS"):
        a0,a1=regs_a[sec]; b0,b1=regs_b[sec]
        A=body_a[a0:a1]; B=body_b[b0:b1]
        print("="*84)
        print(f"[{sec}] Save{nA} 0x{a0:X}..0x{a1:X} ({len(A)}B)  vs  Save{nB} 0x{b0:X}..0x{b1:X} ({len(B)}B)  delta={len(B)-len(A):+d}")
        print("="*84)
        runs=anchored_runs(A,B,W=16)
        sig=[(i1,i2,j1,j2) for (i1,i2,j1,j2) in runs if max(i2-i1,j2-j1)>=4]
        print(f"  runs total={len(runs)}  signal(>=4B)={len(sig)}")
        for k,(i1,i2,j1,j2) in enumerate(sig[:40],1):
            print(f"  [run {k}] A:{sec}+0x{i1:X}..0x{i2:X} ({i2-i1}B)  B:{sec}+0x{j1:X}..0x{j2:X} ({j2-j1}B)  "
                  f"(Aabs 0x{a0+i1:06X}, Babs 0x{b0+j1:06X})")
            c=10; sA=max(0,i1-c); eA=min(len(A),i2+c); sB=max(0,j1-c); eB=min(len(B),j2+c)
            print("    -- A --"); print(hexrow(A,a0+sA,sA,eA-sA))
            print("    -- B --"); print(hexrow(B,b0+sB,sB,eB-sB))
        if len(sig)>40: print(f"   ... {len(sig)-40} more signal runs")
        print()

def cmd_r1growth():
    print("R1 lengths: ", {n: (load(n)[1]["R1"][1]-load(n)[1]["R1"][0]) for n in (28,29,30,31)})
    cmd_sec(28,29)
    print("\n\n##### 29 -> 30 (COMPLETING scan) #####\n")
    cmd_sec(29,30)

if __name__=="__main__":
    if len(sys.argv)>=2 and sys.argv[1]=="pk": cmd_pk()
    elif len(sys.argv)>=4 and sys.argv[1]=="sec": cmd_sec(int(sys.argv[2]),int(sys.argv[3]))
    elif len(sys.argv)>=2 and sys.argv[1]=="r1growth": cmd_r1growth()
    else: cmd_pk()
