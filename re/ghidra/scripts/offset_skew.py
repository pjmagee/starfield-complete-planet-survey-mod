#!/usr/bin/env python3
"""Resolve target IDs in 1.16.236 and 1.16.244 versionlibs, disassemble the 244
function bodies, extract memory displacements, and diff against the 236 dumps.

Parser replicates CommonLibSF REL::IDDB::unpack_file (meh321 versionlibdb v2 format)
exactly. Header: int32 format(==2); 4x uint32 gameVersion; int32 nameLen + name bytes;
int32 pointerSize; int32 addressCount; then addressCount entries.
"""
import struct, sys, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM

V236 = r"E:/SteamLibrary/steamapps/common/Starfield/Data/SFSE/Plugins/versionlib-1-16-236-0.bin"
V244 = r"E:/SteamLibrary/steamapps/common/Starfield/Data/SFSE/Plugins/versionlib-1-16-244-0.bin"
EXE  = r"E:/SteamLibrary/steamapps/common/Starfield/Starfield.exe"
TXT236 = r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/offsets-1-16-236-0.txt"
IMAGE_BASE = 0x140000000


class R:
    def __init__(self, data): self.d = data; self.o = 0
    def u8(self):  v = self.d[self.o]; self.o += 1; return v
    def u16(self): v = struct.unpack_from('<H', self.d, self.o)[0]; self.o += 2; return v
    def u32(self): v = struct.unpack_from('<I', self.d, self.o)[0]; self.o += 4; return v
    def u64(self): v = struct.unpack_from('<Q', self.d, self.o)[0]; self.o += 8; return v
    def take(self, n): v = self.d[self.o:self.o+n]; self.o += n; return v


def load_versiondb_v2(r, ptrSize, addrCount):
    out = {}
    prevID = 0
    prevOffset = 0
    for _ in range(addrCount):
        typ = r.u8()
        lo = typ & 0xF
        hi = typ >> 4
        if lo == 0:   tid = r.u64()
        elif lo == 1: tid = prevID + 1
        elif lo == 2: tid = prevID + r.u8()
        elif lo == 3: tid = prevID - r.u8()
        elif lo == 4: tid = prevID + r.u16()
        elif lo == 5: tid = prevID - r.u16()
        elif lo == 6: tid = r.u16()
        elif lo == 7: tid = r.u32()
        else: raise ValueError("bad id type")
        tmp = (prevOffset // ptrSize) if (hi & 8) else prevOffset
        h = hi & 7
        if h == 0:   toff = r.u64()
        elif h == 1: toff = tmp + 1
        elif h == 2: toff = tmp + r.u8()
        elif h == 3: toff = tmp - r.u8()
        elif h == 4: toff = tmp + r.u16()
        elif h == 5: toff = tmp - r.u16()
        elif h == 6: toff = r.u16()
        elif h == 7: toff = r.u32()
        else: raise ValueError("bad off type")
        if hi & 8:
            toff = toff * ptrSize
        out[tid] = toff
        prevID = tid
        prevOffset = toff
    return out


def load_versiondb(path):
    """Handles both V2 (meh321 delta-packed) and V5 (flat uint32[id]) formats."""
    with open(path, 'rb') as f:
        data = f.read()
    r = R(data)
    fmt = r.u32()
    if fmt == 2:
        gameVersion = [r.u32() for _ in range(4)]
        nameLen = r.u32()
        name = r.take(nameLen).decode('utf-8', 'replace')
        ptrSize = r.u32()
        addrCount = r.u32()
        out = load_versiondb_v2(r, ptrSize, addrCount)
        return out, name, gameVersion, ptrSize, addrCount
    elif fmt == 5:
        # HEADER_V5: uint32[4] gameVersion; char[64] name; int32 ptrSize; int32 dataFormat; int32 offsetCount
        gameVersion = [r.u32() for _ in range(4)]
        name = r.take(64).split(b'\x00')[0].decode('utf-8', 'replace')
        ptrSize = r.u32()
        dataFormat = r.u32()
        offsetCount = r.u32()
        # data is a flat uint32 array indexed by id; m_v5[id] -> offset (RVA). id 0 unused.
        out = {}
        for i in range(offsetCount):
            v = r.u32()
            if v:
                out[i] = v
        return out, name, gameVersion, ptrSize, offsetCount
    else:
        raise ValueError(f"unexpected format {fmt} in {path}")


def main():
    db236, n236, gv236, ps236, c236 = load_versiondb(V236)
    db244, n244, gv244, ps244, c244 = load_versiondb(V244)
    print(f"236: name={n236!r} ver={gv236} ptr={ps236} count={c236} parsed={len(db236)}")
    print(f"244: name={n244!r} ver={gv244} ptr={ps244} count={c244} parsed={len(db244)}")

    # --- validate 236 parse against the canonical txt table ---
    txt = {}
    with open(TXT236) as f:
        for line in f:
            p = line.split()
            if len(p) >= 2:
                try:
                    i = int(p[0]); va = int(p[1], 16)
                    txt[i] = va - IMAGE_BASE   # store as RVA
                except ValueError:
                    pass
    mism = 0
    checked = 0
    sample_ids = [52151,52157,52158,52159,52180,52188,83004,83006,83009,83038,
                  90491,90548,124775,124898,124899,124901,126578,126806]
    for i in sample_ids:
        checked += 1
        a = db236.get(i)
        b = txt.get(i)
        ok = (a == b)
        if not ok:
            mism += 1
        print(f"  validate ID {i}: parsed_rva={a:#x} txt_rva={b:#x} {'OK' if ok else 'MISMATCH'}")
    # broad validation over full table
    broad_mism = sum(1 for i, v in txt.items() if db236.get(i) != v)
    print(f"VALIDATION: sample {checked-mism}/{checked} match; full-table mismatches={broad_mism}/{len(txt)}")
    if broad_mism != 0:
        print("!!! PARSER DOES NOT MATCH 236 TABLE — aborting trust")
    return db236, db244, txt


if __name__ == '__main__':
    main()
