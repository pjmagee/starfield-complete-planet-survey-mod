#!/usr/bin/env python3
"""Stage 2: resolve target IDs in 236+244, disassemble 244 bodies, extract memory
displacements, and diff the load-bearing offsets the mod depends on."""
import struct, sys, re
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_OP_IMM

sys.path.insert(0, r"D:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/ghidra/scripts")
from offset_skew import load_versiondb, V236, V244, EXE, TXT236, IMAGE_BASE

# ID -> (name/role, list of load-bearing offsets the mod depends on within this fn)
TARGETS = {
    52159: ("green reader (species-scanned check)", [0x268, 0x30, 0x21, 0x40, 0x48, 0x18, 0x12]),
    90491: ("outline decider", [0x268]),
    90548: ("outline decider 2", [0x28, 0x268]),
    52188: ("planet-from-player", []),
    52180: ("membership", [0x268]),
    52158: ("write percent/scan (real scan)", [0x268, 0x30, 0x21, 0x40, 0x48, 0x18, 0x12]),
    52157: ("scan chain entry", [0x268]),
    124898: ("writer +0x21 (scan flag)", [0x21, 0x30, 0x40, 0x48, 0x18]),
    124899: ("writer +0x20 (percent byte)", [0x20, 0x30, 0x40, 0x48, 0x18]),
    124901: ("FNV slot hash", [0x30, 0x28, 0x00]),
    126806: ("db lookup (bucket table)", [0x12]),
    126578: ("GetKnowledgeManager", [0x8b0]),
    83009: ("ScannableComponent +0x24 canonical", [0x24, 0x268, 0xc8]),
    83004: ("ScannableComponent lifecycle", [0x24, 0x28, 0xc8]),
    83006: ("ScannableComponent resolve", [0x24, 0x28]),
    83038: ("ScannableComponent canonical2", [0x24, 0x28]),
}

# the universal data-path offsets we care most about across the whole green/survey path
KEY_OFFSETS = [0x268, 0x3d8, 0x8b0, 0x20, 0x21, 0x24, 0x28, 0x30, 0x38, 0x40, 0x48,
               0x60, 0x18, 0x12, 0xc8]


def disasm_displacements(md, code, base_va, max_bytes):
    """Return (list of (va, mnemonic, op_str), multiset of displacements, immediates set).
    Stops at the first plausible function end (ret followed by alignment / int3 padding)
    or after max_bytes."""
    disps = {}     # disp -> count
    imms = set()
    insns = []
    last_was_ret = False
    n = 0
    for ins in md.disasm(code, base_va):
        insns.append((ins.address, ins.mnemonic, ins.op_str))
        # function-end heuristic: ret then int3/nop padding
        if last_was_ret and ins.mnemonic in ('int3', 'nop'):
            # peek: if we've seen a ret and now hit padding, likely end. But keep going
            # a little to be safe; we cap by max_bytes anyway.
            pass
        for op in ins.operands:
            if op.type == X86_OP_MEM:
                d = op.mem.disp
                if d != 0:
                    # normalize to unsigned small positive struct offset view
                    disps[d] = disps.get(d, 0) + 1
            elif op.type == X86_OP_IMM:
                imms.add(op.imm)
        last_was_ret = (ins.mnemonic == 'ret')
        n = ins.address + ins.size - base_va
        if n >= max_bytes:
            break
    return insns, disps, imms


def main():
    db236, *_ = load_versiondb(V236)
    db244, *_ = load_versiondb(V244)
    pe = pefile.PE(EXE, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase
    data = pe.get_memory_mapped_image()  # RVA-indexed image
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    results = {}
    for tid, (role, expected) in TARGETS.items():
        rva236 = db236.get(tid)
        rva244 = db244.get(tid)
        if rva244 is None:
            results[tid] = dict(role=role, rva236=rva236, rva244=None, err="ID not in 244 db")
            continue
        # read up to 3KB window from RVA
        WIN = 0x0C00
        code = data[rva244:rva244+WIN]
        base_va = image_base + rva244
        insns, disps, imms = disasm_displacements(md, code, base_va, WIN)
        present = {o for o in disps}
        # which of the expected load-bearing offsets are present in the 244 body
        exp_present = {o: (o in present) for o in expected}
        key_present = {o: (o in present) for o in KEY_OFFSETS if o in present}
        results[tid] = dict(role=role, rva236=rva236, rva244=rva244,
                            n_insns=len(insns), disps=disps, exp_present=exp_present,
                            present_key=key_present, addr_moved=(rva236 != rva244))

    # ---- print summary ----
    print("="*100)
    print(f"{'ID':>7} {'role':<40} {'236 addr':>12} {'244 addr':>12} {'addr':>6}")
    print("-"*100)
    for tid, r in results.items():
        if 'err' in r:
            print(f"{tid:>7} {r['role']:<40} {(IMAGE_BASE+r['rva236']):#012x}   {'--':>12}  {r['err']}")
            continue
        a236 = IMAGE_BASE + r['rva236']
        a244 = IMAGE_BASE + r['rva244']
        moved = "MOVED" if r['addr_moved'] else "same"
        print(f"{tid:>7} {r['role']:<40} {a236:#012x} {a244:#012x} {moved:>6}")
    print("="*100)
    print("\nPer-ID load-bearing offset presence in the 244 disassembly:")
    for tid, r in results.items():
        if 'err' in r:
            print(f"  ID_{tid}: ERROR {r['err']}")
            continue
        ep = r['exp_present']
        miss = [hex(o) for o, ok in ep.items() if not ok]
        have = [hex(o) for o, ok in ep.items() if ok]
        status = "ALL PRESENT" if not miss else f"MISSING {miss}"
        print(f"  ID_{tid} ({r['role']}): n_insns={r['n_insns']}  have={have}  -> {status}")

    return results, db236, db244


if __name__ == '__main__':
    main()
