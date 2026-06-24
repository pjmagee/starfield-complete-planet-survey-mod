import struct, zlib

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
WANT = {
  0x00177D04:"CND_DNAType_Standard", 0x00177D01:"CND_DNAType_XNA",
  0x00177D00:"CND_DNABasis_Carbon", 0x00177CFD:"CND_DNABasis_Arsenic",
  0x00171352:"CND_ReproductionRhizomes", 0x00171350:"CND_ReproductionHasOverride",
  0x0015281C:"CND_CritterPeaceful",
}

def subrecords(rd):
    j=0; real=None
    while j+6<=len(rd):
        ssig=rd[j:j+4]; ssz=struct.unpack_from('<H',rd,j+4)[0]
        if ssig==b'XXXX': real=struct.unpack_from('<I',rd,j+6)[0]; j+=6+ssz; continue
        dsz=ssz
        if real is not None: dsz=real; real=None
        yield ssig, rd[j+6:j+6+dsz]; j+=6+dsz

def decode_ctda(p):
    op=p[0]; comp=struct.unpack_from('<f',p,4)[0]; func=struct.unpack_from('<H',p,8)[0]
    p1=struct.unpack_from('<I',p,0xC)[0]; p2=struct.unpack_from('<I',p,0x10)[0]
    p3=struct.unpack_from('<I',p,0x14)[0]; p4=struct.unpack_from('<I',p,0x18)[0]
    return op,comp,func,p1,p2,p3,p4

found={}
def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    def walk(d, base):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size], base+i+24); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if ff in WANT:
                rd=d[i+24:i+24+size]
                if flags&COMPRESSED and len(rd)>=4:
                    try: rd=zlib.decompress(rd[4:])
                    except: rd=b''
                found[ff]=(sig.decode(),rd)
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs], pos+24)
        pos+=gs
    for fid,name in WANT.items():
        if fid not in found: print(f"0x{fid:08X} {name}: NOT FOUND"); continue
        sig,rd=found[fid]
        print(f"\n##### 0x{fid:08X} {name} (sig={sig}) #####")
        for ssig,p in subrecords(rd):
            if ssig==b'CTDA':
                op,comp,func,p1,p2,p3,p4=decode_ctda(p)
                print(f"   CTDA op=0x{op:02X} func={func} comp={comp:g} p1=0x{p1:08X} p2=0x{p2:08X} p3=0x{p3:08X} p4=0x{p4:08X}")
            else:
                txt=''.join(chr(b) if 32<=b<127 else '.' for b in p)
                print(f"   {ssig.decode()} sz={len(p)} : {p.hex()} | {txt}")

if __name__=='__main__': main()
