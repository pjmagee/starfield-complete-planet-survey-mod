"""
Find Papyrus native function addresses in Starfield.exe.

Strategy:
1. Find target strings in .rdata (like "SetScanned")
2. Find xrefs: LEA instructions in .text that load those string addresses
3. Near each xref, look for function pointer loads (LEA/MOV of addresses in .text)
4. Those are candidate function pointers for the registered native function
5. Look up each candidate in offsets.txt to get the REL::ID
"""
import sys
import pefile
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_REG_RIP, X86_OP_MEM

# Adjust STARFIELD_EXE to your local Steam install.
STARFIELD_EXE = r"E:\SteamLibrary\steamapps\common\Starfield\Starfield.exe"
# offsets-1-16-236-0.txt lives at the repo root (not checked in — download separately).
OFFSETS_TXT = str(Path(__file__).resolve().parents[2] / "offsets-1-16-236-0.txt")

TARGETS = [
    "SetScanned",
    "SetTraitKnown",
    "IsTraitKnown",
    "GetSurveyPercent",
    "Notification",
]

def load_offsets_map():
    """Load offsets.txt into offset->id map for reverse lookup"""
    offset_to_id = {}
    with open(OFFSETS_TXT, 'r') as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                id_val = int(parts[0])
                offset_val = int(parts[1], 16)
                offset_to_id[offset_val] = id_val
    return offset_to_id

def main():
    print(f"Loading offsets map...")
    offset_to_id = load_offsets_map()
    print(f"Loaded {len(offset_to_id)} ID mappings\n")

    pe = pefile.PE(STARFIELD_EXE, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    with open(STARFIELD_EXE, 'rb') as f:
        data = f.read()

    # Find sections
    text_section = None
    rdata_section = None
    for section in pe.sections:
        name = section.Name.rstrip(b'\x00').decode('ascii', errors='ignore')
        if name == '.text':
            text_section = section
        elif name == '.rdata':
            rdata_section = section

    text_start = text_section.PointerToRawData
    text_end = text_start + text_section.SizeOfRawData
    text_rva_base = text_section.VirtualAddress
    rdata_rva_base = rdata_section.VirtualAddress
    rdata_rva_end = rdata_rva_base + rdata_section.SizeOfRawData

    def offset_to_rva(offset):
        for section in pe.sections:
            if section.PointerToRawData <= offset < section.PointerToRawData + section.SizeOfRawData:
                return section.VirtualAddress + (offset - section.PointerToRawData)
        return None

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    for target in TARGETS:
        print(f"=== {target} ===")
        target_bytes = target.encode('ascii') + b'\x00'

        # Find string in .rdata
        search_pos = rdata_section.PointerToRawData
        rdata_end_off = search_pos + rdata_section.SizeOfRawData
        string_offsets = []
        while True:
            pos = data.find(target_bytes, search_pos, rdata_end_off)
            if pos == -1:
                break
            if pos > 0 and data[pos-1] == 0:
                string_offsets.append(pos)
            search_pos = pos + 1

        if not string_offsets:
            print("  Not found\n")
            continue

        for str_off in string_offsets:
            str_rva = offset_to_rva(str_off)
            str_va = image_base + str_rva
            print(f"  String at VA 0x{str_va:016X}")

            # Search .text for LEA instructions referencing this string
            # LEA rXX, [rip+disp32] encodes the target RVA
            # We scan the whole .text section looking for bytes that
            # when decoded as LEA would resolve to our string's RVA
            xrefs = []
            for scan_off in range(text_start, text_end - 7, 1):
                # LEA reg, [rip+disp32] starts with 48 8D (REX.W LEA) or 4C 8D (R8-R15)
                b = data[scan_off:scan_off+3]
                if len(b) < 3: continue
                if b[0] not in (0x48, 0x4C): continue
                if b[1] != 0x8D: continue

                # 3rd byte: ModRM, must be RIP-relative (mod=00, rm=101)
                modrm = b[2]
                if (modrm & 0xC7) != 0x05: continue

                # Next 4 bytes are disp32
                disp = int.from_bytes(data[scan_off+3:scan_off+7], 'little', signed=True)
                instr_len = 7
                next_rip = image_base + offset_to_rva(scan_off) + instr_len
                target_va = next_rip + disp

                if target_va == str_va:
                    xref_rva = offset_to_rva(scan_off)
                    xrefs.append((scan_off, xref_rva))

            print(f"    Found {len(xrefs)} xrefs")

            # For each xref, scan forward/backward for LEA instructions
            # that load addresses in .text (function pointers)
            text_rva_end = text_rva_base + text_section.SizeOfRawData

            for xref_off, xref_rva in xrefs[:5]:
                xref_va = image_base + xref_rva
                print(f"    xref at VA 0x{xref_va:016X}:")

                # Disassemble a window
                window_start = max(text_start, xref_off - 200)
                window_end = min(text_end, xref_off + 200)
                window_data = data[window_start:window_end]
                window_va = image_base + offset_to_rva(window_start)

                candidates = []
                for insn in md.disasm(window_data, window_va):
                    if insn.mnemonic == 'lea':
                        if len(insn.operands) >= 2:
                            op = insn.operands[1]
                            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                                target = insn.address + insn.size + op.mem.disp
                                target_rva = target - image_base
                                if text_rva_base <= target_rva < text_rva_end:
                                    candidates.append((insn.address, target))

                # Find the LEA closest to the xref (should be the function pointer
                # that's registered alongside the string)
                # The registration pattern is typically:
                #   lea r8, func_impl
                #   lea rdx, "funcName"  <- this is xref_va
                #   lea rcx, "ClassName"
                #   call RegisterNativeFunction
                # So the function pointer LEA is within ~30 bytes BEFORE the xref

                # Filter to LEAs within 50 bytes before xref_va
                close = [(src, tgt) for src, tgt in candidates
                         if src < xref_va and xref_va - src <= 50]

                # Sort by distance (closest first)
                close.sort(key=lambda x: xref_va - x[0])

                # Print the best candidate
                if close:
                    src_va, tgt_va = close[0]
                    id_val = offset_to_id.get(tgt_va, None)
                    id_str = f"ID={id_val}" if id_val else "(no ID)"
                    print(f"      BEST: LEA @ VA 0x{src_va:016X} -> 0x{tgt_va:016X} {id_str} [distance={xref_va - src_va}]")

                # Also show all candidates for debugging
                for src_va, tgt_va in candidates[:3]:
                    id_val = offset_to_id.get(tgt_va, None)
                    id_str = f"ID={id_val}" if id_val else "(no ID)"
                    rel = "before" if src_va < xref_va else "after"
                    dist = abs(xref_va - src_va)
                    print(f"        LEA @ 0x{src_va:016X} -> 0x{tgt_va:016X} {id_str} [{rel} by {dist}]")

        print()

if __name__ == "__main__":
    main()
