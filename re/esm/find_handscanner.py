#!/usr/bin/env python3
"""Enumerate all HandScanner* KYWD records, and find records whose CTDA/refs
mention these keyword ids. Goal: locate the authored species->marker linkage
(condition vs membership list) that the runtime catalog ID_909810/909812 build from.
"""
import struct, zlib, json
from collections import defaultdict, Counter

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

def subrecords(data):
    i, real = 0, None
    while i + 6 <= len(data):
        sig = data[i:i+4]; sz = struct.unpack_from('<H', data, i+4)[0]; i += 6
        if sig == b'XXXX':
            real = struct.unpack_from('<I', data, i)[0]; i += sz; continue
        if real is not None: sz, real = real, None
        if i+sz > len(data): break
        yield sig, data[i:i+sz]; i += sz

def get_edid(data):
    for s,p in subrecords(data):
        if s==b'EDID': return p.split(b'\x00',1)[0].decode('latin1')
    return None

handscanner = {}   # formid -> edid (KYWD named HandScanner*)
# record sig -> Counter of subrecord sigs (to find which record types hold scan data)
recstats = defaultdict(Counter)
allrecs = {}       # formid -> (sig, top)

def walk(f, end, top, depth=0):
    while f.tell() < end:
        pos=f.tell(); h=f.read(24)
        if len(h)<24: break
        sig=h[0:4]; size=struct.unpack_from('<I',h,4)[0]
        if sig==b'GRUP':
            label=h[8:12]; gtype=struct.unpack_from('<I',h,12)[0]
            nt = label if (gtype==0 and depth==0) else top
            walk(f,pos+size,nt,depth+1); f.seek(pos+size); continue
        flags=struct.unpack_from('<I',h,8)[0]; formid=struct.unpack_from('<I',h,12)[0]
        data=f.read(size)
        if flags & COMPRESSED and len(data)>=4:
            try: data=zlib.decompress(data[4:])
            except: pass
        allrecs[formid]=(sig,top)
        if sig==b'KYWD':
            ed=get_edid(data)
            if ed and ed.startswith('HandScanner'):
                handscanner[formid]=ed

def main():
    with open(ESM,'rb') as f:
        f.seek(0,2); fsize=f.tell(); f.seek(0)
        hdr=f.read(24); size=struct.unpack_from('<I',hdr,4)[0]; f.seek(24+size)
        walk(f,fsize,b'\x00\x00\x00\x00')
    print(f"HandScanner KYWDs: {len(handscanner)}")
    out = {}
    cat = defaultdict(list)
    for fid,ed in sorted(handscanner.items()):
        # categorize by name segment
        seg = ed.replace('HandScanner','').split('_')[0]
        cat[seg].append((f"0x{fid:08X}",ed))
        out[f"0x{fid:08X}"]=ed
    for seg in sorted(cat):
        print(f"\n== {seg} ({len(cat[seg])}) ==")
        for fid,ed in cat[seg][:80]:
            print(f"   {fid} {ed}")
    json.dump(out, open(r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/esm/handscanner_kywds.json","w"), indent=1)

if __name__=="__main__":
    main()
