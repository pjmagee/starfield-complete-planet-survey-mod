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
def edid_of(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'EDID': return p.rstrip(b'\x00').decode('latin1','replace')
    return None

with open(ESM,'rb') as f: data=f.read()
t=struct.unpack_from('<I',data,4)[0]
kywd_edid={ff:edid_of(rd) for sig,ff,rd in topwalk(data,t,{b'KYWD'}) if sig==b'KYWD'}

REPRO_IDS={0x0023E907:1,0x0023E906:2,0x0023E908:3,0x00171869:4,0x00171868:5,0x00171867:6,0x00171866:7}

def decode_ctda(p):
    # full 32-byte CTDA
    op=p[0]
    # comparison value: union float/global at +0x04
    comp_f=struct.unpack_from('<f',p,4)[0]
    comp_i=struct.unpack_from('<I',p,4)[0]
    func=struct.unpack_from('<H',p,0x08)[0]
    p1=struct.unpack_from('<I',p,0x0C)[0]
    p2=struct.unpack_from('<I',p,0x10)[0]
    ro=struct.unpack_from('<I',p,0x1C)[0]
    # op high nibble = comparison; bit0 = OR with next
    cmp_code=(op>>5)&0x7
    cmp_name={0:'EqualTo',1:'NotEqual',2:'Greater',3:'GreaterEq',4:'Less',5:'LessEq'}.get(cmp_code,f'cmp{cmp_code}')
    OR = bool(op & 0x01)
    return op,cmp_name,OR,comp_f,comp_i,func,p1,p2,ro

# walk the flora FLST and print the reproduction blocks BYTE-EXACT
for sig,ff,rd in topwalk(data,t,{b'FLST'}):
    if sig!=b'FLST' or ff!=0x00160C96: continue
    lnam=[]; cur=-1; conds=[]
    blocks=[]
    for ssig,p in subrecords(rd):
        if ssig==b'LNAM' and len(p)>=4: lnam.append(struct.unpack_from('<I',p,0)[0])
        elif ssig==b'INAM' and len(p)>=4:
            if cur>=0: blocks.append((cur,conds))
            cur=struct.unpack_from('<I',p,0)[0]; conds=[]
        elif ssig==b'CTDA' and len(p)>=0x20: conds.append(p[:0x20])
    if cur>=0: blocks.append((cur,conds))
    print("=== FLORA reproduction marker blocks (byte-exact CTDA) ===")
    for idx,cs in blocks:
        mk=lnam[idx] if 0<=idx<len(lnam) else 0
        if mk not in REPRO_IDS: continue
        print(f"\nmarker[{idx}] 0x{mk:08X} {kywd_edid.get(mk,'?')}  (N={REPRO_IDS[mk]})")
        for p in cs:
            op,cmpn,OR,cf,ci,func,p1,p2,ro=decode_ctda(p)
            chain='OR' if OR else 'AND'
            if func==14:
                # GetActorValue / GetValue? param1=AVIF 0x0023E905 ; compare against comp value
                print(f"    [{chain}] func14(GetAV avif=0x{p1:08X}) {cmpn} {cf:g}   raw_op=0x{op:02X}")
            elif func==837:
                print(f"    [{chain}] func837 EvalCondForm(CNDF 0x{p1:08X} {kywd_edid.get(p1,'')}) {cmpn} {cf:g}  raw_op=0x{op:02X}")
            else:
                print(f"    [{chain}] func{func} p1=0x{p1:08X} {cmpn} {cf:g} raw_op=0x{op:02X}")

# also dump reproduction CNDFs byte-exact
print("\n\n=== Reproduction CNDFs byte-exact ===")
for sig,ff,rd in topwalk(data,t,{b'CNDF'}):
    if sig!=b'CNDF': continue
    if ff not in (0x00171350,0x00171351,0x00171352): continue
    e=edid_of(rd)
    print(f"\nCNDF 0x{ff:08X} {e}")
    for ssig,p in subrecords(rd):
        if ssig==b'CTDA' and len(p)>=0x20:
            op,cmpn,OR,cf,ci,func,p1,p2,ro=decode_ctda(p)
            chain='OR' if OR else 'AND'
            extra=kywd_edid.get(p1,'')
            print(f"    [{chain}] func{func} p1=0x{p1:08X} {extra} {cmpn} {cf:g}  raw_op=0x{op:02X}")
