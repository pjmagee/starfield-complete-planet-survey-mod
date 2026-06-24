import struct, zlib

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
REPRO_IDS = {0x0023E907:1,0x0023E906:2,0x0023E908:3,0x00171869:4,0x00171868:5,0x00171867:6,0x00171866:7}
REPRO_SET=set(REPRO_IDS)
CNDF_WANT={0x00171350:"CND_ReproductionHasOverride",0x00171351:"CND_Reproduction0x51",0x00171352:"CND_ReproductionRhizomes"}
AVIF_WANT={0x0023E905:"AVIF PlantReproduction?"}

def subrecords(rd):
    j=0; real=None
    while j+6<=len(rd):
        ssig=rd[j:j+4]; ssz=struct.unpack_from('<H',rd,j+4)[0]
        if ssig==b'XXXX': real=struct.unpack_from('<I',rd,j+6)[0]; j+=6+ssz; continue
        dsz=ssz
        if real is not None: dsz=real; real=None
        yield ssig, rd[j+6:j+6+dsz]; j+=6+dsz

def inflate(rd,flags):
    if flags&COMPRESSED and len(rd)>=4:
        try: return zlib.decompress(rd[4:])
        except: return b''
    return rd

def topwalk(data, t, labels):
    pos=24+t; n=len(data); out=[]
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            out.append((sig, ff, inflate(d[i+24:i+24+size], flags)))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; lab=data[pos+8:pos+12]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0 and lab in labels: walk(data[pos+24:pos+gs])
        pos+=gs
    return out

def decode_ctda(p):
    op=p[0]; comp=struct.unpack_from('<f',p,4)[0]; func=struct.unpack_from('<H',p,8)[0]
    p1=struct.unpack_from('<I',p,0xC)[0]; p2=struct.unpack_from('<I',p,0x10)[0]
    p3=struct.unpack_from('<I',p,0x14)[0]; p4=struct.unpack_from('<I',p,0x18)[0]
    return op,comp,func,p1,p2,p3,p4

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]

    print("=== FLST 0x00160C96 — FULL reproduction-marker condition blocks ===")
    for sig,ff,rd in topwalk(data,t,{b'FLST'}):
        if sig!=b'FLST' or ff!=0x00160C96: continue
        lnam=[]; curidx=-1
        for ssig,p in subrecords(rd):
            if ssig==b'LNAM' and len(p)>=4:
                lnam.append(struct.unpack_from('<I',p,0)[0])
            elif ssig==b'INAM' and len(p)>=4:
                curidx=struct.unpack_from('<I',p,0)[0]
                m=lnam[curidx] if 0<=curidx<len(lnam) else 0
                if m in REPRO_SET:
                    print(f"\n  >>> conditioned marker idx={curidx} 0x{m:08X} (N={REPRO_IDS[m]})")
            elif ssig==b'CITC' and len(p)>=4:
                pass
            elif ssig==b'CTDA' and len(p)>=32:
                m=lnam[curidx] if 0<=curidx<len(lnam) else 0
                if m in REPRO_SET:
                    op,comp,func,p1,p2,p3,p4=decode_ctda(p)
                    print(f"        CTDA op=0x{op:02X} func={func} comp={comp:g} p1=0x{p1:08X} p2=0x{p2:08X} p3=0x{p3:08X} p4=0x{p4:08X}")

    print("\n\n=== CNDF condition forms (full) ===")
    want=set(CNDF_WANT)|set(AVIF_WANT)
    for sig,ff,rd in topwalk(data,t,{b'CNDF',b'AVIF'}):
        if ff not in want: continue
        nm=CNDF_WANT.get(ff) or AVIF_WANT.get(ff)
        print(f"\n##### 0x{ff:08X} {sig.decode('latin1')} {nm} #####")
        for ssig,p in subrecords(rd):
            if ssig==b'CTDA' and len(p)>=32:
                op,comp,func,p1,p2,p3,p4=decode_ctda(p)
                print(f"   CTDA op=0x{op:02X} func={func} comp={comp:g} p1=0x{p1:08X} p2=0x{p2:08X} p3=0x{p3:08X} p4=0x{p4:08X}")
            else:
                txt=''.join(chr(b) if 32<=b<127 else '.' for b in p)
                print(f"   {ssig.decode('latin1')} sz={len(p)}: {p[:48].hex()} | {txt[:48]}")

if __name__=='__main__': main()
