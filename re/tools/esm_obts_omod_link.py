import struct, zlib
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
GT = [0x00048A34,0x0019B898,0x0019B899,0x0019B89A,0x0019B89B,0x0019B89C,0x0019B89D,0x0019B89E,0x0019B89F]
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

def omod_nkeys(dat):
    """Extract NKEY keyword ids embedded in OMOD DATA. Pattern: ascii 'NKEY' then later a u32 keyword.
    From hex: ...4e4b4559(=NKEY) <8 bytes?> <kw u32> ... Actually layout per property:
      <prop-name 4 ascii> <padding> <type u32=4?> <something> <value u32>
    Simplify: scan for b'NKEY', then the keyword u32 is at the next 'value' slot.
    Observed: '4e4b4559 ab991600 00000000 00000000 00 04000000 02000000 4e524541'
      after NKEY: ab991600 = 0x001699AB = the keyword (immediately after NKEY tag, little-endian)
    """
    out=[]
    idx=0
    while True:
        k=dat.find(b'NKEY',idx)
        if k<0: break
        # keyword id is the u32 immediately following the 4-char tag
        if k+8<=len(dat):
            out.append(struct.unpack_from('<I',dat,k+4)[0])
        idx=k+4
    return out

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    omod_edid={}; omod_data={}
    npc_obts={}
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if sig==b'OMOD':
                rd=inflate(d[i+24:i+24+size],flags)
                ed=None; dat=None
                for ss,p in subrecords(rd):
                    if ss==b'EDID': ed=p.rstrip(b'\x00').decode('latin1','replace')
                    elif ss==b'DATA': dat=p
                if ed: omod_edid[ff]=ed
                if dat is not None: omod_data[ff]=dat
            elif sig==b'NPC_' and ff in GT:
                rd=inflate(d[i+24:i+24+size],flags)
                for ss,p in subrecords(rd):
                    if ss==b'OBTS': npc_obts[ff]=p
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs

    for npcid in GT:
        obts=npc_obts.get(npcid)
        print(f"\nNPC 0x{npcid:08X}")
        if not obts:
            print("   no OBTS"); continue
        cnt=struct.unpack_from('<I',obts,0)[0]
        print(f"   OBTS len={len(obts)} cnt={cnt} (cnt*7+0x12={cnt*7+0x12})")
        print(f"   OBTS hex: {obts.hex()}")
        # parse 7-byte entries from 0x12
        off=0x12; ent=[]
        for _ in range(cnt):
            if off+4>len(obts): break
            om=struct.unpack_from('<I',obts,off)[0]; ent.append(om); off+=7
        for om in ent:
            ed=omod_edid.get(om,'?')
            nk=omod_nkeys(omod_data.get(om,b''))
            tag=''
            if 'CCT_Temperament' in (ed or ''): tag=' <<<< TEMPERAMENT'
            print(f"      OMOD 0x{om:08X} {ed}  NKEYs={[hex(x) for x in nk]}{tag}")
if __name__=='__main__': main()
