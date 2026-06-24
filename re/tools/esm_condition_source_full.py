import struct, zlib
from collections import Counter, defaultdict

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

def load():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]
    return data,t

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

def kwda_of(rd):
    for ssig,p in subrecords(rd):
        if ssig==b'KWDA': return list(struct.unpack_from('<%dI'%(len(p)//4),p,0))
    return []

def obts_omods(rd):
    """parse OBTS subrecords: entryCount@0, 18-byte prefix, then 7-byte entries (u32 omod + 3 bytes)."""
    out=[]
    for ssig,p in subrecords(rd):
        if ssig==b'OBTS' and len(p)>=0x12:
            cnt=struct.unpack_from('<I',p,0)[0]
            off=0x12; got=[]
            while off+7<=len(p):
                got.append(struct.unpack_from('<I',p,off)[0]); off+=7
            # validate
            valid = (cnt*7 == len(p)-0x12)
            out.append((cnt,valid,got))
    return out

def main():
    data,t=load()

    # ---- 1. EDID resolver for everything we touch (KYWD names) ----
    kywd_edid={}
    for sig,ff,rd in topwalk(data,t,{b'KYWD'}):
        if sig==b'KYWD':
            kywd_edid[ff]=edid_of(rd)

    # ---- 2. Build complete mod_CCT_* OMOD -> NKEY map, by category ----
    print("="*78)
    print("PART 1: ALL OMODs with EDID prefix 'mod_CCT_' -> their NKEY keyword(s)")
    print("="*78)
    omod_meta={}      # omodid -> (edid, [nkeys])
    cct_by_cat=defaultdict(list)  # category -> [(omodid,edid,nkeys)]
    nkey_cat_kw=defaultdict(set)  # category -> set of nkey keyword ids
    for sig,ff,rd in topwalk(data,t,{b'OMOD'}):
        if sig!=b'OMOD': continue
        e=edid_of(rd); nkeys=[]
        for ssig,p in subrecords(rd):
            if ssig==b'DATA':
                o=0
                while o+8<=len(p):
                    if p[o:o+4]==b'NKEY':
                        nkeys.append(struct.unpack_from('<I',p,o+4)[0]); o+=8; continue
                    o+=1
        omod_meta[ff]=(e,nkeys)
        if e and e.startswith('mod_CCT_'):
            # category = token after mod_CCT_
            parts=e.split('_')
            cat=parts[2] if len(parts)>2 else '?'
            cct_by_cat[cat].append((ff,e,nkeys))
            for k in nkeys: nkey_cat_kw[cat].add(k)
    for cat in sorted(cct_by_cat):
        lst=cct_by_cat[cat]
        print(f"\n--- mod_CCT_{cat}_*  ({len(lst)} OMODs) ---")
        for ff,e,nk in sorted(lst,key=lambda x:x[1]):
            nkr=' '.join(f"0x{k:08X}({kywd_edid.get(k,'?')})" for k in nk) or "(no NKEY)"
            print(f"   OMOD 0x{ff:08X} {e}")
            print(f"        NKEY: {nkr}")

    # ---- 3. FLST flora condition table: keyword -> marker (func 560/837/858) ----
    print("\n"+"="*78)
    print("PART 2: FLST 0x00160C96 (flora) condition decode -> {input -> marker}")
    print("="*78)
    def dump_flst(target, name):
        for sig,ff,rd in topwalk(data,t,{b'FLST'}):
            if sig!=b'FLST' or ff!=target: continue
            lnam=[]; blocks=[]; curidx=-1; curconds=[]
            for ssig,p in subrecords(rd):
                if ssig==b'LNAM' and len(p)>=4:
                    lnam.append(struct.unpack_from('<I',p,0)[0])
                elif ssig==b'INAM' and len(p)>=4:
                    if curidx>=0: blocks.append((curidx,curconds))
                    curidx=struct.unpack_from('<I',p,0)[0]; curconds=[]
                elif ssig==b'CTDA' and len(p)>=0x20:
                    op=p[0]; comp=struct.unpack_from('<f',p,4)[0]
                    func=struct.unpack_from('<H',p,0x08)[0]
                    param1=struct.unpack_from('<I',p,0x0C)[0]
                    param2=struct.unpack_from('<I',p,0x10)[0]
                    runon=struct.unpack_from('<I',p,0x1C)[0]
                    curconds.append((func,param1,param2,op,comp,runon))
            if curidx>=0: blocks.append((curidx,curconds))
            print(f"\n{name}: FLST 0x{ff:08X}  LNAM markers={len(lnam)} conditioned blocks={len(blocks)}")
            print("  -- LNAM markers (catalog order) --")
            for i,m in enumerate(lnam):
                print(f"     [{i:2d}] 0x{m:08X} {kywd_edid.get(m,'?')}")
            print("  -- conditioned entries (marker <= conditions) --")
            for idx,conds in blocks:
                if 0<=idx<len(lnam):
                    mk=lnam[idx]; mn=kywd_edid.get(mk,'?')
                else:
                    mk=0; mn='IDX-OOR'
                print(f"     marker[{idx}] 0x{mk:08X} {mn}")
                for func,p1,p2,op,comp,ro in conds:
                    fn={560:'HasKeyword(species)',837:'EvalCondForm(CNDF)',858:'HasKeyword(PLANET-trait)',
                        448:'HasPerk',699:'HasMagicEffectKeyword',882:'GetIsID/always'}.get(func,f'func{func}')
                    extra=''
                    if func in (560,858): extra=f" kw={kywd_edid.get(p1,hex(p1))}"
                    print(f"        func {func} {fn} param1=0x{p1:08X}{extra} runOn=0x{ro:08X}")
            return lnam,blocks
    flor_lnam,flor_blocks=dump_flst(0x00160C96,"FLORA")
    fauna_lnam,fauna_blocks=dump_flst(0x00160C97,"FAUNA")

    # ---- 4. CNDF resolution (func 837 recursion) ----
    print("\n"+"="*78)
    print("PART 3: CNDF records referenced by flora func-837 (decode their leaves)")
    print("="*78)
    cndf={}
    for sig,ff,rd in topwalk(data,t,{b'CNDF'}):
        if sig!=b'CNDF': continue
        e=edid_of(rd); conds=[]
        for ssig,p in subrecords(rd):
            if ssig==b'CTDA' and len(p)>=0x20:
                func=struct.unpack_from('<H',p,0x08)[0]
                param1=struct.unpack_from('<I',p,0x0C)[0]
                conds.append((func,param1))
        cndf[ff]=(e,conds)
    # which CNDFs does flora reference?
    ref_cndf=set()
    for idx,conds in flor_blocks:
        for func,p1,p2,op,comp,ro in conds:
            if func==837: ref_cndf.add(p1)
    for c in sorted(ref_cndf):
        e,conds=cndf.get(c,('?',[]))
        print(f"\n  CNDF 0x{c:08X} {e}")
        for func,p1 in conds:
            fn={560:'HasKeyword',837:'EvalCondForm',858:'HasKeyword(PLANET)',882:'always'}.get(func,f'func{func}')
            sub=''
            if func==837 and p1 in cndf: sub=f" -> CNDF {cndf[p1][0]}"
            print(f"      func {func} {fn} 0x{p1:08X} {kywd_edid.get(p1,'')}{sub}")

if __name__=='__main__': main()
