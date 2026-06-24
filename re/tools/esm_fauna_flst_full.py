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

def load():
    with open(ESM,'rb') as f: return f.read()

def walk_all(data, want_ids, want_sigs):
    """Return dict formid->(sig, edid, rawdata) for matching ids OR sigs."""
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    out={}; bysig={s:[] for s in want_sigs}
    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            rd=d[i+24:i+24+size]
            sd=sig.decode('latin1')
            if ff in want_ids or sd in want_sigs:
                if flags&COMPRESSED and len(rd)>=4:
                    try: rd=zlib.decompress(rd[4:])
                    except: rd=b''
                edid=None
                for ssig,p in subrecords(rd):
                    if ssig==b'EDID': edid=p.rstrip(b'\x00').decode('latin1','replace'); break
                rec=(sd,edid,rd)
                if ff in want_ids: out[ff]=rec
                if sd in want_sigs: bysig[sd].append((ff,edid,rd))
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]; gt=struct.unpack_from('<I',data,pos+12)[0]
        if gt==0: walk(data[pos+24:pos+gs])
        pos+=gs
    return out, bysig

def get_edids(data, ids):
    out,_=walk_all(data, set(ids), set())
    return {fid:(out.get(fid,('?','?',b''))[0], out.get(fid,('?','?',b''))[1]) for fid in ids}

FAUNA_FLST=0x00160C97

def main():
    data=load()
    out,_=walk_all(data,{FAUNA_FLST},set())
    sig,edid,rd=out[FAUNA_FLST]
    lnam=[]; entries=[]; cur=None
    for ssig,p in subrecords(rd):
        if ssig==b'LNAM': lnam.append(struct.unpack_from('<I',p,0)[0])
        elif ssig==b'INAM': cur={'inam':struct.unpack_from('<I',p,0)[0],'ctda':[]}; entries.append(cur)
        elif ssig==b'CITC': cur['citc']=struct.unpack_from('<I',p,0)[0]
        elif ssig==b'CTDA': cur['ctda'].append(p)

    # gather all referenced ids to resolve EDIDs
    ref=set(lnam)
    for e in entries:
        for c in e['ctda']:
            ref.add(struct.unpack_from('<I',c,0xC)[0])
    names=get_edids(data, ref)

    def nm(x): return names.get(x,('?','?'))[1] or '?'
    def sg(x): return names.get(x,('?','?'))[0] or '?'

    print(f"FLST 0x{FAUNA_FLST:08X} {edid}  LNAM count={len(lnam)} conditioned={len(entries)}")
    print("\n=== LNAM marker array (catalog order) ===")
    for i,m in enumerate(lnam):
        conditioned = any(e['inam']==i for e in entries)
        tag = "" if conditioned else "  <<< UNCONDITIONAL (always emit)"
        print(f"  [{i:2d}] 0x{m:08X} {sg(m):4s} {nm(m)}{tag}")

    print("\n=== conditioned entries (sorted by INAM) ===")
    for e in sorted(entries,key=lambda x:x['inam']):
        idx=e['inam']; marker=lnam[idx]
        print(f"  INAM idx={idx:2d} -> marker 0x{marker:08X} {nm(marker)}  citc={e.get('citc')}")
        for c in e['ctda']:
            op=c[0]; comp=struct.unpack_from('<f',c,4)[0]; func=struct.unpack_from('<H',c,8)[0]
            p1=struct.unpack_from('<I',c,0xC)[0]
            print(f"      op=0x{op:02X} func={func} comp={comp:g} param1=0x{p1:08X} {sg(p1):4s} {nm(p1)}")

if __name__=='__main__': main()
