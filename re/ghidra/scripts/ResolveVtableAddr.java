// Read the function pointer at (vtableAddr + slotOffset) and decompile the target.
// Headless: <outPath> <vtableHexAddr> <slotHex> [moreAddr moreSlot ...]
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class ResolveVtableAddr extends GhidraScript {
    private DecompInterface decomp;
    private SymbolTable st;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: <outPath> <vtableHex> <slotHex> [...]"); return; }
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i + 1 < args.length; i += 2) {
                long vt = Long.parseLong(args[i].toLowerCase().replace("0x", ""), 16);
                long slot = Long.parseLong(args[i + 1].toLowerCase().replace("0x", ""), 16);
                Address vtAddr = toAddr(vt);
                Address slotAddr = vtAddr.add(slot);
                pw.println("################ vtable @ " + vtAddr + " " + symAt(vtAddr) + " slot=0x" + Long.toHexString(slot) + " ################");
                try {
                    long ptr = getLong(slotAddr);
                    Address target = toAddr(ptr);
                    pw.println("  slot @ " + slotAddr + " -> " + target + " " + symAt(target));
                    Function fn = getFunctionAt(target);
                    if (fn == null) fn = getFunctionContaining(target);
                    if (fn == null) { pw.println("  (no function at target)\n"); continue; }
                    pw.println("  TARGET: " + fn.getName() + " @ " + fn.getEntryPoint() + " " + symAt(fn.getEntryPoint()));
                    DecompileResults res = decomp.decompileFunction(fn, 240, monitor);
                    if (res != null && res.getDecompiledFunction() != null)
                        pw.println(res.getDecompiledFunction().getC());
                    else pw.println("  (decompile failed)");
                } catch (Exception e) {
                    pw.println("  (error: " + e.getMessage() + ")");
                }
                pw.println();
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
        for (Symbol s : syms) { if (!first) sb.append(" / "); sb.append(s.getName()); first = false; }
        sb.append("]");
        return sb.toString();
    }
}
