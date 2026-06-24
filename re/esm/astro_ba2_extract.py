#!/usr/bin/env python3
"""Extract specific .pex files from Starfield - Misc.ba2 to re/esm/pex/ for
decompilation/inspection."""
import struct, zlib, os

BA2 = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield - Misc.ba2"
OUT = r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/esm/pex"
WANT = {
    "scripts/sq_parentscript.pex",
    "scripts/fragments/quests/qf_playerskills_002c59e4.pex",
    "scripts/planettraitterminalmenuscript.pex",
    "scripts/planettraitterminalscript.pex",
    "scripts/defaultsurveytraitquestscript.pex",
    "scripts/rq_scantraitscript.pex",
    "scripts/missionsurveytraitscript.pex",
    "scripts/planettraitenablehandler.pex",
}

def main():
    os.makedirs(OUT, exist_ok=True)
    with open(BA2,'rb') as f: data=f.read()
    file_count=struct.unpack_from('<I',data,12)[0]
    name_tbl_off=struct.unpack_from('<Q',data,16)[0]
    names=[]; p=name_tbl_off
    for _ in range(file_count):
        ln=struct.unpack_from('<H',data,p)[0]; p+=2
        names.append(data[p:p+ln].decode('latin1','replace')); p+=ln
    rec_off=32; rec_size=36
    got=0
    for idx in range(file_count):
        nm=names[idx] if idx<len(names) else None
        if nm not in WANT: continue
        base=rec_off+idx*rec_size
        offset=struct.unpack_from('<Q',data,base+16)[0]
        packed=struct.unpack_from('<I',data,base+24)[0]
        unpacked=struct.unpack_from('<I',data,base+28)[0]
        size = packed if packed else unpacked
        raw=data[offset:offset+size]
        if packed:
            try: raw=zlib.decompress(raw)
            except Exception as e:
                print("decompress fail", nm, e); continue
        outp=os.path.join(OUT, os.path.basename(nm))
        with open(outp,'wb') as o: o.write(raw)
        print(f"wrote {outp} ({len(raw)} bytes)")
        got+=1
    print("extracted", got)

if __name__=='__main__':
    main()
