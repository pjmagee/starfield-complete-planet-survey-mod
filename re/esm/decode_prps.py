#!/usr/bin/env python3
"""Decode the species property-sheet (PRPS) into (AVIF, value) pairs, resolve the
AVIFs, and dump the HandScanner* AVIF records to find the value->keyword mapping.
Also collect a sample of flora PRPS to see Nutrient/Fiber vs Toxin/Metabolic split.
"""
import struct, zlib, json
from collections import defaultdict

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

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

# Build a complete formid->(sig,edid) for KYWD + AVIF + FLOR + NPC_ only (cheap-ish),
# and capture full record bytes for AVIF HandScanner* and a flora sample.
def run():
    avif_edid = {}
    kywd_edid = {}
    avif_full = {}      # all HandScanner AVIF records (full bytes) by formid
    flor_prps = []      # (formid, edid, [(avif,value)...]) sample
    npc_prps  = []
    flora_seen = 0; fauna_seen = 0
    with open(ESM,'rb') as f:
        f.seek(0,2); fsize=f.tell(); f.seek(0)
        hdr=f.read(24); size=struct.unpack_from('<I',hdr,4)[0]; f.seek(24+size)
        def walk(end,top,depth):
            nonlocal flora_seen, fauna_seen
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
                if s==b'AVIF':
                    ed=get_edid(data); avif_edid[fid]=ed
                    if ed and ed.startswith('HandScanner'):
                        avif_full[fid]=(ed,data)
                elif s==b'KYWD':
                    kywd_edid[fid]=get_edid(data)
                elif s==b'FLOR' and flora_seen<12:
                    for ssig,pl in subrecords(data):
                        if ssig==b'PRPS':
                            pairs=[(struct.unpack_from('<I',pl,j)[0], struct.unpack_from('<f',pl,j+4)[0]) for j in range(0,len(pl)-7,12)]
                            flor_prps.append((fid,get_edid(data),pairs)); flora_seen+=1; break
                elif s==b'NPC_' and fauna_seen<8 and top==b'NPC_':
                    ed=get_edid(data)
                    if ed and ('Predator' in ed or 'Prey' in ed or 'PCM_' in ed):
                        for ssig,pl in subrecords(data):
                            if ssig==b'PRPS':
                                pairs=[(struct.unpack_from('<I',pl,j)[0], struct.unpack_from('<f',pl,j+4)[0]) for j in range(0,len(pl)-7,12)]
                                npc_prps.append((fid,ed,pairs)); fauna_seen+=1; break
        walk(fsize,b'',0)

    def name(fid):
        return avif_edid.get(fid) or kywd_edid.get(fid) or f"0x{fid:08X}"

    print("=== HandScanner AVIF records (the property actor-values) ===")
    for fid,(ed,data) in sorted(avif_full.items()):
        subs=[(s.decode('latin1'),len(p)) for s,p in subrecords(data)]
        print(f"0x{fid:08X} {ed}  subs={subs}")
    json.dump({f"0x{k:08X}":(v[0],v[1].hex()) for k,v in avif_full.items()},
              open("avif_handscanner.json","w"), indent=1)

    print("\n=== sample FLORA PRPS (AVIF->value) ===")
    for fid,ed,pairs in flor_prps:
        pp=[(name(a),round(v,3)) for a,v in pairs]
        print(f"0x{fid:08X} {ed}: {pp}")

    print("\n=== sample FAUNA PRPS (AVIF->value) ===")
    for fid,ed,pairs in npc_prps:
        pp=[(name(a),round(v,3)) for a,v in pairs if a in avif_edid]
        print(f"0x{fid:08X} {ed}: {pp[:20]}")

if __name__=="__main__":
    run()
