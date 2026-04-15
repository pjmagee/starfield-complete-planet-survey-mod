# @author
# @category Search.InstructionPattern
# @keybinding
# @menupath Search.Extract Biome Walk Functions
# @toolbar

from ghidra.program.model.address import AddressSet
from ghidra.program.model.listing import CodeUnit
import sys

def get_function_by_id(address_library_id):
    """Resolve Address Library ID to actual function address"""
    try:
        # Try via ghidra.program.model.listing.Program
        addr = currentProgram.parseAddress(str(address_library_id))
        func = getFunctionContaining(addr)
        return func
    except:
        return None

def decompile_function(func):
    """Decompile a function and return the source"""
    if func is None:
        return None
    
    try:
        from ghidra.app.decompiler import DecompilerFactory
        from ghidra.program.model.listing import Function
        
        decomp = DecompilerFactory.getDecompiler(currentProgram)
        result = decomp.decompile(func)
        return result.getDecompiledFunction()
    except:
        return None

def extract_function_info(func_id, func_name):
    """Extract function info from Address Library ID"""
    print("[*] Processing {} ({})".format(func_name, func_id))
    
    # Manually resolve IDs based on known offsets from internals.md
    # ID_118493: GetBiomeActors Papyrus native at 142086ca0
    # ID_118494: GetBiomeFlora at 142087450
    
    id_map = {
        118493: 0x142086ca0,
        118494: 0x142087450,
        83010: 0x1413076c0,  # inner biome dispatch
        97857: 0x1417dcd80,  # slot materializer
    }
    
    if func_id not in id_map:
        print("[!] ID {} not in known map".format(func_id))
        return
    
    offset = id_map[func_id]
    try:
        addr = toAddr(offset)
        func = getFunctionContaining(addr)
        if func:
            print("[+] Found function at {}: {}".format(addr, func.getName()))
            # Get disassembly
            listing = currentProgram.getListing()
            return func
    except Exception as e:
        print("[!] Error resolving {}: {}".format(func_id, str(e)))
    
    return None

# Main
output_lines = []
output_lines.append("=" * 80)
output_lines.append("BIOME WALK FUNCTION ANALYSIS - Starfield 1.16.236.0")
output_lines.append("=" * 80)

targets = [
    (118493, "GetBiomeActors (Papyrus native)"),
    (118494, "GetBiomeFlora (Papyrus native)"),
    (83010, "Inner biome dispatch"),
]

for func_id, name in targets:
    output_lines.append("\n[{}] {}".format(func_id, name))
    output_lines.append("-" * 40)
    func = extract_function_info(func_id, name)
    if func:
        output_lines.append("Address: {}".format(func.getEntryPoint()))
        output_lines.append("Size: {} bytes".format(func.getBody().getNumAddresses()))

# Write output
output_path = "d:/Projects/pjmagee/starfield-complete-planet-survey-mod/re/ghidra/output/biome_walk_extraction.txt"
with open(output_path, 'w') as f:
    for line in output_lines:
        f.write(line + "\n")
        print(line)

print("\n[+] Output written to {}".format(output_path))
