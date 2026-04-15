// Decompile biome enumeration dispatchers and related functions
// Target IDs: 83010 (fauna), 83012 (flora), 52188 (ref-to-planet resolver)
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public class DecompileBiomeWalk extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0]
            : getSourceFile().getParentFile().getParent() + "/output/biome-walk.txt";

        // Target: ID_83010 (fauna), ID_83012 (flora), ID_52188 (resolver)
        // Also: ID_118493/118494 (Papyrus natives), ID_47749 (biome load)
        List<Long> targets = Arrays.asList(83010L, 83012L, 52188L, 47749L, 118493L, 118494L);

        SymbolTable st = currentProgram.getSymbolTable();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            Set<Address> dumped = new HashSet<>();
            for (Long id : targets) {
                SymbolIterator it = st.getSymbols("ID_" + id);
                while (it.hasNext()) {
                    Symbol s = it.next();
                    Function fn = getFunctionAt(s.getAddress());
                    if (fn == null) fn = getFunctionContaining(s.getAddress());
                    if (fn == null) { 
                        pw.println("=== ID_" + id + " / NOT FOUND ===\n"); 
                        continue; 
                    }
                    if (dumped.contains(fn.getEntryPoint())) continue;
                    dumped.add(fn.getEntryPoint());
                    
                    pw.println("=== ID_" + id + " / " + fn.getName() + " @ " + fn.getEntryPoint() + " ===");
                    pw.println();
                    DecompileResults res = decomp.decompileFunction(fn, 60, monitor);
                    if (res.getDecompiledFunction() != null) {
                        pw.println(res.getDecompiledFunction().getC());
                    } else {
                        pw.println("(decompile failed)");
                    }
                    pw.println();

                    // List callers
                    pw.println("--- callers of ID_" + id + " ---");
                    Reference[] refs = getReferencesTo(fn.getEntryPoint());
                    for (Reference r : refs) {
                        Function caller = getFunctionContaining(r.getFromAddress());
                        if (caller != null)
                            pw.println("  " + caller.getName() + "@" + caller.getEntryPoint());
                    }
                    pw.println();
                    pw.println();
                }
            }
        }
        decomp.dispose();
        println("Wrote decompile dump to " + outPath);
    }
}
