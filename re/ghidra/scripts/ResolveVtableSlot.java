// Resolve a vtable slot to its concrete target function, then decompile it.
// Finds symbols whose name contains the given substring (the vftable label),
// reads the pointer at (vftable_addr + slotOffset), reports the target function,
// and decompiles it.
// Headless: <outPath> <vftableNameSubstr> <slotOffsetHex> [moreSubstr moreSlotHex ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryAccessException;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class ResolveVtableSlot extends GhidraScript {
    private DecompInterface decomp;
    private SymbolTable st;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: <outPath> <vftSubstr> <slotHex> [...]"); return; }
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i + 1 < args.length; i += 2) {
                String substr = args[i];
                long slot = Long.parseLong(args[i + 1].toLowerCase().replace("0x", ""), 16);
                pw.println("################ vftable substr='" + substr + "' slot=0x" + Long.toHexString(slot) + " ################");

                // Find every symbol whose name contains substr.
                SymbolIterator it = st.getSymbolIterator();
                int hits = 0;
                while (it.hasNext()) {
                    Symbol s = it.next();
                    String n = s.getName();
                    if (n == null || !n.contains(substr)) continue;
                    // Only interested in vftable data symbols.
                    if (!n.toLowerCase().contains("vftable") && !n.toLowerCase().contains("vtable")) continue;
                    hits++;
                    Address vftAddr = s.getAddress();
                    pw.println("==== vftable symbol: " + n + " @ " + vftAddr + " ====");
                    try {
                        Address slotAddr = vftAddr.add(slot);
                        long ptr = getLong(slotAddr); // little-endian 8-byte read
                        Address target = toAddr(ptr);
                        pw.println("  slot ptr @ " + slotAddr + " -> " + target + " " + symAt(target));
                        Function fn = getFunctionAt(target);
                        if (fn == null) fn = getFunctionContaining(target);
                        if (fn == null) {
                            pw.println("  (no function at target)");
                        } else {
                            pw.println("  TARGET FUNCTION: " + fn.getName() + " @ " + fn.getEntryPoint() + " " + symAt(fn.getEntryPoint()));
                            DecompileResults res = decomp.decompileFunction(fn, 180, monitor);
                            if (res != null && res.getDecompiledFunction() != null)
                                pw.println(res.getDecompiledFunction().getC());
                            else pw.println("  (decompile failed)");
                        }
                    } catch (MemoryAccessException mae) {
                        pw.println("  (memory read failed at slot: " + mae.getMessage() + ")");
                    }
                    pw.println();
                }
                if (hits == 0) pw.println("(no vftable symbol matched substr)\n");
            }
        }
        decomp.dispose();
        println("Wrote " + args[0]);
    }

    private String symAt(Address at) {
        if (at == null) return "";
        Symbol[] syms = st.getSymbols(at);
        if (syms == null || syms.length == 0) return "";
        StringBuilder sb = new StringBuilder("[");
        boolean first = true;
        for (Symbol s : syms) {
            if (!first) sb.append(" / ");
            sb.append(s.getName());
            first = false;
        }
        sb.append("]");
        return sb.toString();
    }
}
