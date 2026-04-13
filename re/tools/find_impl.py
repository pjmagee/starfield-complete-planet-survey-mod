"""Follow the wrapper function to find the actual implementation.

The BEST match is a Papyrus wrapper that calls the real implementation.
Extract the wrapper's internal `call` target.
"""
import pefile
from pathlib import Path
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_IMM

# Adjust STARFIELD_EXE to your local Steam install.
STARFIELD_EXE = r"E:\SteamLibrary\steamapps\common\Starfield\Starfield.exe"
# offsets-1-16-236-0.txt lives at the repo root (not checked in — download separately).
OFFSETS_TXT = str(Path(__file__).resolve().parents[2] / "offsets-1-16-236-0.txt")

# Wrapper VAs we found
WRAPPERS = {
    "SetScanned": 0x00000001420845E0,
    "GetSurveyPercent": 0x0000000141F1B750,
    "Debug.Notification": 0x0000000142006EC0,
}

def load_offsets_map():
    offset_to_id = {}
    with open(OFFSETS_TXT, 'r') as f:
        for line in f:
            parts = line.split()
            if len(parts) == 2:
                offset_to_id[int(parts[1], 16)] = int(parts[0])
    return offset_to_id

def main():
    offset_to_id = load_offsets_map()
    pe = pefile.PE(STARFIELD_EXE, fast_load=True)
    image_base = pe.OPTIONAL_HEADER.ImageBase

    with open(STARFIELD_EXE, 'rb') as f:
        data = f.read()

    def va_to_file_off(va):
        rva = va - image_base
        for s in pe.sections:
            if s.VirtualAddress <= rva < s.VirtualAddress + s.SizeOfRawData:
                return s.PointerToRawData + (rva - s.VirtualAddress)
        return None

    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True

    for name, wrapper_va in WRAPPERS.items():
        file_off = va_to_file_off(wrapper_va)
        if not file_off:
            print(f"{name}: can't resolve VA")
            continue

        print(f"=== {name} (wrapper at 0x{wrapper_va:016X}) ===")

        # Disassemble first 64 bytes of wrapper
        window = data[file_off:file_off+128]
        calls = []
        for insn in md.disasm(window, wrapper_va):
            print(f"  0x{insn.address:016X}  {insn.mnemonic:8s} {insn.op_str}")
            if insn.mnemonic == 'call':
                if len(insn.operands) >= 1 and insn.operands[0].type == X86_OP_IMM:
                    target = insn.operands[0].imm
                    id_val = offset_to_id.get(target, None)
                    calls.append((insn.address, target, id_val))
            if insn.mnemonic == 'ret':
                break
            if insn.address - wrapper_va > 100:
                break

        print(f"  Calls found:")
        for src, tgt, id_val in calls:
            print(f"    0x{src:016X} -> 0x{tgt:016X} ID={id_val}")
        print()

if __name__ == "__main__":
    main()
