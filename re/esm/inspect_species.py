#!/usr/bin/env python3
"""Dump full subrecord layout of sample FLOR (flora) and NPC_ (fauna) records,
showing which HandScanner keywords (if any) they carry, and any CTDA/PRPS/etc.
We pull species ids straight from a couple of planets' PPBD lists.
"""
import struct, zlib, json
from collections import Counter

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000
hs = {int(k,16):v for k,v in json.load(open(r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/esm/handscanner_kywds.json")).items()}

# Some flora/fauna ids harvested earlier from PNDT samples; we'll also auto-collect.
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
    return None

# First pass: grab a few flora & fauna ids from the first living planet's PPBD.
def first_species():
    flora=[]; fauna=[]
    with open(ESM,'rb') as f:
        hdr=f.read(24); size=struct.unpack_from('<I',hdr,4)[0]; f.seek(24+size)
        while True:
            pos=f.tell(); g=f.read(24)
            if len(g)<24: return flora,fauna
            if g[0:4]!=b'GRUP': return flora,fauna
            gsize=struct.unpack_from('<I',g,4)[0]; label=g[8:12]; gt=struct.unpack_from('<I',g,12)[0]
            if gt==0 and label==b'PNDT':
                end=pos+gsize
                while f.tell()<end and (len(flora)<3 or len(fauna)<3):
                    h=f.read(24)
                    if len(h)<24: break
                    s=h[0:4]; sz=struct.unpack_from('<I',h,4)[0]; fl=struct.unpack_from('<I',h,8)[0]
                    if s==b'GRUP': f.seek(sz-24,1); continue
                    data=f.read(sz)
                    if s!=b'PNDT': continue
                    if fl&COMPRESSED and len(data)>=4:
                        try:data=zlib.decompress(data[4:])
                        except:continue
                    for ssig,pl in subrecords(data):
                        if ssig==b'PPBD':
                            o=[0]
                            def u32():
                                v=struct.unpack_from('<I',pl,o[0])[0];o[0]+=4;return v
                            try:
                                u32();u32();u32();u32()
                                nf=u32(); fa=[u32() for _ in range(nf)]
                                nk=u32(); [u32() for _ in range(nk)]
                                nfl=u32(); esz=u32() or 9
                                flo=[]
                                for _ in range(nfl):
                                    flo.append(struct.unpack_from('<I',pl,o[0])[0]); o[0]+=esz
                                fauna += [x for x in fa if x]
                                flora += [x for x in flo if x]
                            except: pass
                return list(dict.fromkeys(flora))[:3], list(dict.fromkeys(fauna))[:3]
            f.seek(pos+gsize)

def dump_targets(targets):
    found={}
    with open(ESM,'rb') as f:
        f.seek(0,2); fsize=f.tell(); f.seek(0)
        hdr=f.read(24); size=struct.unpack_from('<I',hdr,4)[0]; f.seek(24+size)
        stack=[(fsize,b'',0)]
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
                if fid in targets:
                    found[fid]=(s,top,data)
                    if len(found)==len(targets): return
        walk(fsize,b'',0)
    return found

if __name__=="__main__":
    flora,fauna = first_species()
    print("sample flora:",[f"0x{x:08X}" for x in flora])
    print("sample fauna:",[f"0x{x:08X}" for x in fauna])
    tgt=set(flora)|set(fauna)
    found=dump_targets(tgt)
    for fid in sorted(tgt):
        if fid not in found:
            print(f"0x{fid:08X}: NOT FOUND"); continue
        s,top,data=found[fid]
        print(f"\n#### 0x{fid:08X} sig={s.decode()} top={top.decode(errors='replace')} EDID={get_edid(data)}")
        for ssig,pl in subrecords(data):
            extra=''
            if ssig==b'KWDA':
                ids=[struct.unpack_from('<I',pl,j)[0] for j in range(0,len(pl),4)]
                hsk=[(f'0x{i:08X}',hs.get(i)) for i in ids if i in hs]
                extra=f' -> {len(ids)} kwds; HandScanner: {hsk}'
            elif ssig in (b'PTRN',b'CNAM',b'TNAM',b'FNAM') and len(pl)==4:
                extra=f' u32=0x{struct.unpack_from("<I",pl,0)[0]:08X}'
            print(f'    {ssig.decode(errors="replace")} sz={len(pl)}{extra}')
