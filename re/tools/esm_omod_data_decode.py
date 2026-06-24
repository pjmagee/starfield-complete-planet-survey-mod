import struct, zlib
ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000
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
TEMPER={0x001699AB:'Aggressive',0x001699A3:'Territorial',0x001699A1:'CuriousPeaceful',
        0x00169995:'AlwaysFlee',0x00280174:'Wary',0x00280175:'Foolhardy',0x00280177:'Defensive'}
def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    # Two probes:
    # A) Dump OMOD DATA for the 14 CCT_Temperament OMODs, find keyword location/structure
    # B) Dump OBTS of a ground truth NPC and list referenced OMODs + their EDIDs
    omod_edid={}
    cct_omods=[]
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            if sig==b'OMOD':
                rd=inflate(d[i+24:i+24+size],flags)
                edid=None; dat=None
                for ss,p in subrecords(rd):
                    if ss==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace')
                    elif ss==b'DATA': dat=p
                if edid: omod_edid[ff]=edid
                if edid and 'CCT_Temperament' in edid:
                    cct_omods.append((ff,edid,dat))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs

    print("=== A) CCT_Temperament OMOD DATA structure ===")
    for ff,edid,dat in cct_omods:
        # find temper kw offset within DATA
        loc=[]
        for o in range(0,len(dat)-3,4):
            v=struct.unpack_from('<I',dat,o)[0]
            if v in TEMPER: loc.append((o,hex(v),TEMPER[v]))
        print(f"  OMOD 0x{ff:08X} {edid}")
        print(f"     DATA len={len(dat)} kwLoc={loc}")
        print(f"     DATA hex: {dat.hex()}")
if __name__=='__main__': main()
