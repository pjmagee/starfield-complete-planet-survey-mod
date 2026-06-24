#!/usr/bin/env python3
"""Find the authored records the runtime scan-catalog (ID_909810/909812) is built
from: records that REFERENCE a HandScanner marker keyword AND carry CTDA conditions.
Reports record sig + which HandScanner kw it points at + the CTDA bytes.
"""
import struct, zlib, json
from collections import Counter, defaultdict

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000
hs = {int(k,16):v for k,v in json.load(open(r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/esm/handscanner_kywds.json")).items()}
hs_ids = set(hs)

def subrecords(data):
    i, real = 0, None
    while i + 6 <= len(data):
        sig = data[i:i+4]; sz = struct.unpack_from('<H', data, i+4)[0]; i += 6
        if sig == b'XXXX':
            real = struct.unpack_from('<I', data, i)[0]; i += sz; continue
        if real is not None: sz, real = real, None
        if i+sz>len(data): break
        yield sig, data[i:i+sz]; i += sz

def get_edid(data):
    for s,p in subrecords(data):
        if s==b'EDID': return p.split(b'\x00',1)[0].decode('latin1')

# record sigs that both reference a HS keyword and contain CTDA
hit_by_sig = Counter()
ctda_sig = Counter()
samples = defaultdict(list)

def run():
    with open(ESM,'rb') as f:
        f.seek(0,2); fsize=f.tell(); f.seek(0)
        hdr=f.read(24); size=struct.unpack_from('<I',hdr,4)[0]; f.seek(24+size)
        def walk(end,top,depth):
            while f.tell()<end:
                pos=f.tell(); h=f.read(24)
                if len(h)<24: break
                s=h[0:4]; sz=struct.unpack_from('<I',h,4)[0]
                if s==b'GRUP':
                    label=h[8:12]; gt=struct.unpack_from('<I',h,12)[0]
                    nt=label if (gt==0 and depth==0) else top
                    walk(pos+sz,nt,depth+1); f.seek(pos+sz); continue
                fl=struct.unpack_from('<I',h,8)[0]; fid=struct.unpack_from('<I',h,12)[0]
                data=f.read(sz)
                if fl&COMPRESSED and len(data)>=4:
                    try:data=zlib.decompress(data[4:])
                    except:pass
                # quick reject: does the raw record body contain any HS keyword id bytes?
                refs=set()
                has_ctda=False
                subs=[]
                for ssig,pl in subrecords(data):
                    subs.append(ssig.decode('latin1'))
                    if ssig==b'CTDA': has_ctda=True
                    # scan payload for 4-byte HS ids
                    for j in range(0,len(pl)-3):
                        v=struct.unpack_from('<I',pl,j)[0]
                        if v in hs_ids:
                            refs.add(v)
                if refs:
                    sig=s.decode('latin1')
                    hit_by_sig[sig]+=1
                    if has_ctda: ctda_sig[sig]+=1
                    if len(samples[sig])<4:
                        samples[sig].append((f"0x{fid:08X}",get_edid(data),
                                             sorted(set(subs)),
                                             [hs[r] for r in sorted(refs)][:6], has_ctda))
        walk(fsize,b'',0)

if __name__=="__main__":
    run()
    print("record sigs that REFERENCE a HandScanner keyword:")
    for sig,n in hit_by_sig.most_common():
        print(f"  {sig}: {n}  (with CTDA: {ctda_sig.get(sig,0)})")
    print("\nsamples:")
    for sig in hit_by_sig:
        print(f"\n== {sig} ==")
        for fid,ed,subs,refs,ctda in samples[sig]:
            print(f"  {fid} {ed} ctda={ctda}\n      subs={subs}\n      HSrefs={refs}")
