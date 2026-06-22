// Resolve a list of ID_<n> symbols to their addresses and report what lives there
// (function? data? what's referenced from it). For function targets, decompile.
// Handles symbols that aren't the primary symbol at an address.
//
// Headless usage:
//   analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
//     -scriptPath re/ghidra/scripts -postScript ResolveIds.java <outPath> <id...>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class ResolveIds extends GhidraScript {
    private Memory mem;
    private SymbolTable st;
    private DecompInterface decomp;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: ResolveIds <outPath> <id...>"); return; }
        String outPath = args[0];
        mem = currentProgram.getMemory();
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String label = args[i].startsWith("ID_") ? args[i] : "ID_" + args[i];
                pw.println("======== " + label + " ========");
                Address addr = null;
                SymbolIterator it = st.getSymbols(label);
                while (it.hasNext()) { addr = it.next().getAddress(); break; }
                if (addr == null) {
                    // fall back: scan all symbols for the name (slow but robust)
                    SymbolIterator all = st.getAllSymbols(true);
                    while (all.hasNext()) {
                        Symbol s = all.next();
                        if (s.getName().equals(label)) { addr = s.getAddress(); break; }
                    }
                }
                if (addr == null) { pw.println("  (not found)\n"); continue; }
                pw.println("  addr = " + addr);

                Function fn = getFunctionAt(addr);
                if (fn == null) fn = getFunctionContaining(addr);
                if (fn != null) {
                    pw.println("  FUNCTION " + fn.getName() + " @ " + fn.getEntryPoint());
                    DecompileResults res = decomp.decompileFunction(fn, 180, monitor);
                    if (res != null && res.getDecompiledFunction() != null)
                        pw.println(res.getDecompiledFunction().getC());
                    else pw.println("  (decompile failed)");
                } else {
                    pw.println("  (no function here -- treating as data)");
                    Data d = getDataAt(addr);
                    if (d != null) pw.println("  data: " + d.getDataType() + " = " + d.getDefaultValueRepresentation());
                    // dump a few qwords for param-array inspection
                    for (int q = 0; q < 8; q++) {
                        try {
                            long v = mem.getLong(addr.add((long) q * 8));
                            String s = "";
                            try { // try to read as cstr ptr
                                Address p = toAddr(v);
                                StringBuilder sb = new StringBuilder();
                                for (int k = 0; k < 64; k++) {
                                    byte b = mem.getByte(p.add(k));
                                    if (b == 0) break;
                                    if (b < 0x20 || b > 0x7e) { sb.setLength(0); break; }
                                    sb.append((char) b);
                                }
                                if (sb.length() > 0) s = " -> \"" + sb + "\"";
                            } catch (Exception ex) {}
                            pw.printf("    +0x%02x: 0x%016x%s%n", q * 8, v, s);
                        } catch (Exception ex) { break; }
                    }
                }
                pw.println();
            }
        }
        decomp.dispose();
        println("Wrote " + outPath);
    }
}
