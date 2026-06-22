// Decompile the function at each raw hex address argument; list resolved CALL
// targets (ID_<n>/named) for each. Output modeled on DecompileIds.java.
// Headless: <outPath> <hexaddr...>   (addresses like 1402b7950, 0x-prefix optional)
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.LinkedHashSet;
import java.util.Set;

public class DecompileAddrs extends GhidraScript {
    private SymbolTable st;
    private DecompInterface decomp;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: DecompileAddrs <outPath> <hexaddr...>"); return; }
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 1; i < args.length; i++) {
                String h = args[i].toLowerCase().replace("0x", "");
                Address a = toAddr(Long.parseLong(h, 16));
                Function fn = getFunctionAt(a);
                if (fn == null) fn = getFunctionContaining(a);
                pw.println("======== " + a + " " + symAt(a) + " ========");
                if (fn == null) { pw.println("  (no function here)\n"); continue; }
                pw.println("  function " + fn.getName() + " @ " + fn.getEntryPoint());

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
                            String label = "@" + ins.getAddress() + " " + mn + " -> "
                                + (tf != null ? tf.getName() : "?") + " " + symAt(t);
                            if (seen.add(label)) pw.println("    " + label);
                        }
                    }
                }
                pw.println("  --- decomp ---");
                DecompileResults res = decomp.decompileFunction(fn, 240, monitor);
                if (res != null && res.getDecompiledFunction() != null)
                    pw.println(res.getDecompiledFunction().getC());
                else pw.println("  (decompile failed)");
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
        String idSym = null, other = null;
        for (Symbol s : syms) {
            String n = s.getName();
            if (n.startsWith("ID_") && idSym == null) idSym = n;
            else if (other == null) other = n;
        }
        StringBuilder sb = new StringBuilder("[");
        if (idSym != null) sb.append(idSym);
        if (other != null) { if (idSym != null) sb.append(" / "); sb.append(other); }
        sb.append("]");
        return sb.toString();
    }
}
