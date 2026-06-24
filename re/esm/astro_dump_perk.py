#!/usr/bin/env python3
"""Full structural dump of the Skill_Astrophysics PERK + the two globals +
locate the Astrophysics AVIF skill. Decodes PERK subrecords: PRKE/DATA/PRKC/
PRKF/EPFT/EPFD/EPF2/EPF3/CTDA so we can read each rank's entry point.
"""
import struct, zlib

ESM = r"E:/SteamLibrary/steamapps/common/Starfield/Data/Starfield.esm"
COMPRESSED = 0x00040000

WANT = {
    0x0027CBBB: "PERK Skill_Astrophysics",
    0x002508F2: "GLOB Skill_Astrophysics_DiscoverTraitChance",
    0x002B97EC: "GLOB PlanetTraitXPRewardAstrophysicsMult",
    0x00245AB9: "GLOB PlanetTraitXPReward",
    0x001AEB4E: "GLOB PlanetTraitXPRewardSkillBonus",
    0x0027A566: "AVIF PlanetTraitLocationScanCount",
    0x001BDC5C: "AVIF PlanetTraitLocationScanCountRequired",
}

def subrecords(rd):
    j = 0; real = None
    while j + 6 <= len(rd):
        ssig = rd[j:j+4]
        ssz = struct.unpack_from('<H', rd, j+4)[0]
        if ssig == b'XXXX':
            real = struct.unpack_from('<I', rd, j+6)[0]
            j += 6 + ssz; continue
        dsz = ssz
        if real is not None: dsz = real; real = None
        yield ssig, rd[j+6:j+6+dsz]
        j += 6 + dsz

def inflate(rd, flags):
    if flags & COMPRESSED and len(rd) >= 4:
        try: return zlib.decompress(rd[4:])
        except Exception: return b''
    return rd

found = {}
def walk(data, base, want):
    i = 0; n = len(data)
    while i + 24 <= n:
        sig = data[i:i+4]
        size = struct.unpack_from('<I', data, i+4)[0]
        if sig == b'GRUP':
            walk(data[i+24:i+size], base+i+24, want); i += size; continue
        flags = struct.unpack_from('<I', data, i+8)[0]
        fid = struct.unpack_from('<I', data, i+12)[0]
        if fid in want:
            found[fid] = (sig.decode('latin1'), flags, data[i+24:i+24+size])
        i += 24 + size

def ascii_of(b):
    return ''.join(chr(c) if 32 <= c < 127 else '.' for c in b)

def decode_perk(rd):
    print("  --- PERK subrecord stream ---")
    for ssig, p in subrecords(rd):
        s = ssig.decode('latin1')
        extra = ""
        if s == 'EDID':
            extra = p.split(b'\x00',1)[0].decode('latin1')
        elif s == 'FULL' and len(p) == 4:
            extra = f"lstring=0x{struct.unpack_from('<I',p,0)[0]:08X}"
        elif s == 'DESC' and len(p) == 4:
            extra = f"lstring=0x{struct.unpack_from('<I',p,0)[0]:08X}"
        elif s == 'DATA':
            # PERK DATA: trait(u8) level(u8) numRanks(u8) playable(u8) hidden(u8) ...
            extra = f"bytes={p.hex()} -> " + " ".join(f"{b:02X}" for b in p[:8])
        elif s == 'PRKE':
            # entry header: type(u8) rank(u8) priority(u8)
            if len(p) >= 3:
                extra = f"entryType={p[0]} rank={p[1]} priority={p[2]} (raw {p.hex()})"
            else:
                extra = f"raw {p.hex()}"
        elif s == 'PRKC' and len(p) >= 1:
            extra = f"conditionRunOn={p[0]} (raw {p.hex()})"
        elif s == 'PRKF':
            extra = f"endmarker raw {p.hex()}"
        elif s == 'EPFT' and len(p) >= 1:
            # entry point function-param type
            names = {0:'None',1:'Float',2:'Float/AV?',3:'Bool/Flags?',4:'SPEL?',5:'GLOB/lstring?',6:'Float+AV',7:'Script'}
            extra = f"paramType={p[0]} ({names.get(p[0],'?')}) raw {p.hex()}"
        elif s == 'EPFD':
            extra = f"len={len(p)} raw {p.hex()}"
            if len(p) == 4:
                extra += f"  u32=0x{struct.unpack_from('<I',p,0)[0]:08X} f32={struct.unpack_from('<f',p,0)[0]:.4f}"
            elif len(p) == 8:
                a=struct.unpack_from('<I',p,0)[0]; b=struct.unpack_from('<I',p,4)[0]
                fa=struct.unpack_from('<f',p,0)[0]; fb=struct.unpack_from('<f',p,4)[0]
                extra += f"  [0]=0x{a:08X}/{fa:.4f}  [4]=0x{b:08X}/{fb:.4f}"
        elif s == 'EPF2':
            extra = f"len={len(p)} raw {p.hex()}  ascii='{ascii_of(p)}'"
        elif s == 'EPF3':
            extra = f"len={len(p)} raw {p.hex()}"
        elif s == 'EPFB' and len(p) == 4:
            extra = f"formid=0x{struct.unpack_from('<I',p,0)[0]:08X}"
        elif s == 'CTDA':
            extra = decode_ctda(p)
        elif s == 'CIS1' or s == 'CIS2':
            extra = p.split(b'\x00',1)[0].decode('latin1')
        elif s == 'CNAM' and len(p) == 4:
            extra = f"formid=0x{struct.unpack_from('<I',p,0)[0]:08X}"
        else:
            extra = f"len={len(p)} raw {p.hex()[:64]}{'..' if len(p)>32 else ''} ascii='{ascii_of(p[:48])}'"
        print(f"    {s}: {extra}")

def decode_ctda(p):
    # Starfield CTDA is variable; common layout: op(1) unk(3) comparand(4) function(2) padding(2) param1(4) param2(4) runOn(4) reference(4) ...
    out = [f"len={len(p)} raw={p.hex()}"]
    if len(p) >= 12:
        op = p[0]
        comp_f = struct.unpack_from('<f', p, 4)[0]
        comp_i = struct.unpack_from('<I', p, 4)[0]
        func = struct.unpack_from('<H', p, 8)[0]
        out.append(f"op=0x{op:02X} comparand=0x{comp_i:08X}/{comp_f:.4f} func={func}")
        if len(p) >= 16:
            p1 = struct.unpack_from('<I', p, 12)[0]
            out.append(f"param1=0x{p1:08X}")
        if len(p) >= 20:
            p2 = struct.unpack_from('<I', p, 16)[0]
            out.append(f"param2=0x{p2:08X}")
    return "  ".join(out)

def main():
    with open(ESM, 'rb') as f:
        data = f.read()
    tes4size = struct.unpack_from('<I', data, 4)[0]
    pos = 24 + tes4size; n = len(data)
    while pos + 24 <= n:
        if data[pos:pos+4] != b'GRUP': break
        gsize = struct.unpack_from('<I', data, pos+4)[0]
        gtype = struct.unpack_from('<I', data, pos+12)[0]
        if gtype == 0:
            walk(data[pos+24:pos+gsize], pos+24, set(WANT))
        pos += gsize

    for fid in WANT:
        label = WANT[fid]
        if fid not in found:
            print(f"\n############ 0x{fid:08X} {label}: NOT FOUND ############")
            continue
        sig, flags, rd = found[fid]
        rd = inflate(rd, flags)
        print(f"\n############ 0x{fid:08X} {label} (sig={sig} flags=0x{flags:08X} len={len(rd)}) ############")
        if sig == 'PERK':
            decode_perk(rd)
        else:
            for ssig, p in subrecords(rd):
                s = ssig.decode('latin1')
                extra = ""
                if s == 'EDID':
                    extra = p.split(b'\x00',1)[0].decode('latin1')
                elif s == 'FLTV' and len(p) == 4:
                    extra = f"float={struct.unpack_from('<f',p,0)[0]}  u32=0x{struct.unpack_from('<I',p,0)[0]:08X}"
                elif s == 'FNAM' and len(p) >= 1:
                    extra = f"globType='{ascii_of(p)}' raw={p.hex()}"
                elif s == 'INTV' and len(p) == 4:
                    extra = f"int={struct.unpack_from('<i',p,0)[0]}"
                else:
                    extra = f"len={len(p)} raw={p.hex()} ascii='{ascii_of(p)}'"
                print(f"    {s}: {extra}")

if __name__ == '__main__':
    main()
