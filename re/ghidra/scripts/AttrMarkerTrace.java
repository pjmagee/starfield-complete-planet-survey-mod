// One-shot batch for the attribute-marker (slot+0x08) ref-free source investigation.
// For each ID_<n> arg: print xrefs-TO (callers) + the decompiled body (if a function)
// or a data dump (if data, e.g. ID_909810/909812/909826 globals).
//
// Headless usage:
//   analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
//     -scriptPath re/ghidra/scripts -postScript AttrMarkerTrace.java <outPath> <id...>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class AttrMarkerTrace extends GhidraScript {
    private Memory mem;
    private SymbolTable st;
    private DecompInterface decomp;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: AttrMarkerTrace <outPath> <id...>"); return; }
        mem = currentProgram.getMemory();
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                String label = args[i].startsWith("ID_") ? args[i] : "ID_" + args[i];
                pw.println("################ " + label + " ################");
                Address addr = null;
                SymbolIterator it = st.getSymbols(label);
                while (it.hasNext()) { addr = it.next().getAddress(); break; }
                if (addr == null) { pw.println("  (symbol not found)\n"); continue; }
                pw.println("  addr = " + addr + "  " + symAt(addr));

                // callers
                pw.println("  --- xrefs TO (callers) ---");
                for (Reference r : getReferencesTo(addr)) {
                    Address from = r.getFromAddress();
                    Function ff = getFunctionContaining(from);
                    pw.println("    from " + from + " in " + (ff != null ? ff.getName() + "@" + ff.getEntryPoint() : "<none>") + " (" + r.getReferenceType() + ")");
                }

                Function fn = getFunctionAt(addr);
                if (fn == null) fn = getFunctionContaining(addr);
                if (fn != null) {
                    pw.println("  --- call targets ---");
                    Set<String> seen = new LinkedHashSet<>();
                    InstructionIterator iit = currentProgram.getListing().getInstructions(fn.getBody(), true);
                    while (iit.hasNext()) {
                        Instruction ins = iit.next();
                        String mn = ins.getMnemonicString();
                        if (!mn.equals("CALL") && !mn.equals("JMP")) continue;
                        for (Reference rr : ins.getReferencesFrom()) {
                            RefType rt = rr.getReferenceType();
                            if (rt == RefType.UNCONDITIONAL_CALL || rt == RefType.COMPUTED_CALL
                                || rt == RefType.CONDITIONAL_CALL || (mn.equals("JMP") && rt.isComputed())) {
                                Address t = rr.getToAddress();
                                Function tf = getFunctionAt(t);
                                String s = "@" + ins.getAddress() + " " + mn + " -> " + (tf != null ? tf.getName() : "?") + " " + symAt(t);
                                if (seen.add(s)) pw.println("    " + s);
                            }
                        }
                    }
                    pw.println("  --- decomp ---");
                    DecompileResults res = decomp.decompileFunction(fn, 240, monitor);
                    if (res != null && res.getDecompiledFunction() != null)
                        pw.println(res.getDecompiledFunction().getC());
                    else pw.println("  (decompile failed)");
                } else {
                    pw.println("  --- data dump (16 qwords) ---");
                    Data d = getDataAt(addr);
                    if (d != null) pw.println("  data: " + d.getDataType() + " = " + d.getDefaultValueRepresentation());
                    for (int q = 0; q < 16; q++) {
                        try {
                            long v = mem.getLong(addr.add((long) q * 8));
                            pw.printf("    +0x%02x: 0x%016x %s%n", q * 8, v, symFor(v));
                        } catch (Exception ex) { break; }
                    }
                }
                pw.println();
            }
        }
        decomp.dispose();
        println("Wrote " + args[0]);
    }

    private String symFor(long v) {
        try {
            Address a = toAddr(v);
            Symbol[] s = st.getSymbols(a);
            if (s != null && s.length > 0) return "[" + s[0].getName() + "]";
        } catch (Exception e) {}
        return "";
    }

    private String symAt(Address at) {
        if (at == null) return "";
        Symbol[] syms = st.getSymbols(at);
        if (syms == null || syms.length == 0) return "";
        StringBuilder sb = new StringBuilder("[");
        for (int i = 0; i < syms.length && i < 4; i++) { if (i > 0) sb.append(" / "); sb.append(syms[i].getName()); }
        sb.append("]");
        return sb.toString();
    }
}
