import struct, zlib, sys
from collections import Counter, defaultdict

ESM = r"E:\SteamLibrary\steamapps\common\Starfield\Data\Starfield.esm"
COMPRESSED = 0x00040000

# Known forms from the prompt
TARGETS = {
    0x00225588: "KYWD PlanetTrait20SentientMicrobialColonies",
    0x0021B250: "ACTI PlanetTraitScanTarget20SentientMicrobialColonies",
    0x0021B251: "KYWD HandScannerTraitInfo20SentientMicrobialColonies01",
    0x0021B252: "ACTI name 0x0021B252",
    0x00225563: "ACTI name 0x00225563",
}

def subrecords(rd):
    j=0; real=None
    while j+6<=len(rd):
        ssig=rd[j:j+4]; ssz=struct.unpack_from('<H',rd,j+4)[0]
        if ssig==b'XXXX':
            real=struct.unpack_from('<I',rd,j+6)[0]; j+=6+ssz; continue
        dsz=ssz
        if real is not None: dsz=real; real=None
        yield ssig, rd[j+6:j+6+dsz]; j+=6+dsz

def inflate(rd,flags):
    if flags&COMPRESSED and len(rd)>=4:
        try: return zlib.decompress(rd[4:])
        except: return b''
    return rd

def get_edid(rd):
    for s,p in subrecords(rd):
        if s==b'EDID':
            return p.split(b'\x00',1)[0].decode('latin1','replace')
    return None

def main():
    with open(ESM,'rb') as f: data=f.read()
    t=struct.unpack_from('<I',data,4)[0]; pos=24+t; n=len(data)
    found = {}   # formid -> (sig, edid, [(ssig, payload)])
    # Also build a map: any record whose body references any of our target formids
    refs_to = defaultdict(list)  # target formid -> list of (referrer sig, fid, edid, ssig)
    # And a full EDID index for ACTI/KYWD records named PlanetTrait*/PlanetTraitScanTarget*
    edid_index = {}  # formid -> (sig, edid)

    def handle(sig, fid, flags, body):
        rd = inflate(body, flags)
        if not rd: return
        edid = get_edid(rd)
        if fid in TARGETS:
            found[fid] = (sig.decode('latin1'), edid, list(subrecords(rd)))
        if edid and (edid.startswith("PlanetTrait") or "ScanTarget" in edid or "HandScannerTraitInfo" in edid):
            edid_index[fid] = (sig.decode('latin1'), edid)
        # scan body for references to target formids
        for ssig,p in subrecords(rd):
            for o in range(0, max(0,len(p)-3)):
                v = struct.unpack_from('<I',p,o)[0]
                if v in TARGETS:
                    refs_to[v].append((sig.decode('latin1'), fid, edid, ssig.decode('latin1'), o))

    def walk(d):
        i=0; m=len(d)
        while i+24<=m:
            sig=d[i:i+4]; size=struct.unpack_from('<I',d,i+4)[0]
            if sig==b'GRUP': walk(d[i+24:i+size]); i+=size; continue
            flags=struct.unpack_from('<I',d,i+8)[0]; ff=struct.unpack_from('<I',d,i+12)[0]
            handle(sig, ff, flags, d[i+24:i+24+size])
            i+=24+size
    while pos+24<=n:
        if data[pos:pos+4]!=b'GRUP': break
        gs=struct.unpack_from('<I',data,pos+4)[0]
        walk(data[pos:pos+gs])  # walk includes top grup header skip? -> pass whole grup
        pos+=gs

    print("=== TARGET RECORDS (full subrecord dump) ===")
    for fid,(sig,edid,subs) in found.items():
        print(f"\n0x{fid:08X} {sig} EDID={edid}  ({TARGETS[fid]})")
        for ssig,p in subs:
            hexp = p[:64].hex()
            print(f"    {ssig.decode('latin1')} ({len(p)}B): {hexp}")
    print("\n=== REFERENCES TO TARGET FORMS (who points at them) ===")
    for tgt, lst in refs_to.items():
        print(f"\nReferences to 0x{tgt:08X} ({TARGETS[tgt]}): {len(lst)}")
        seen=set()
        for sig,fid,edid,ssig,o in lst:
            key=(sig,fid,ssig)
            if key in seen: continue
            seen.add(key)
            print(f"    {sig} 0x{fid:08X} EDID={edid} via {ssig} @off{o}")
    print(f"\n=== EDID index count (PlanetTrait*/ScanTarget*/HandScannerTraitInfo*): {len(edid_index)} ===")

if __name__=='__main__': main()
