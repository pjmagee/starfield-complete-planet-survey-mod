// Find callers of survey-event accessor stubs and decompile them.
// Writers of survey state post events; callers of GetEventSource() stubs are the writers.
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

public class DecompileSurveyWriters extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0]
            : getSourceFile().getParentFile().getParent() + "/output/survey-writers.txt";

        // 92492 = stub -> PlayerKnowledgeFlagSetEvent::GetEventSource
        // 92389, 92396, 92398 = callers of 92492 (event dispatch sites)
        // 51417, 51418 = callers of PlanetData vtable setter (51449)
        // 51398 = caller of PlanetData sub (51399)
        List<Long> targets = Arrays.asList(92492L, 92389L, 92396L, 92398L, 51417L, 51418L, 51398L);

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
                    if (fn == null) { pw.println("ID_" + id + ": no function"); continue; }
                    if (dumped.contains(fn.getEntryPoint())) continue;
                    dumped.add(fn.getEntryPoint());
                    pw.println("=== ID_" + id + " / " + fn.getName() + " @ " + fn.getEntryPoint() + " ===");
                    DecompileResults res = decomp.decompileFunction(fn, 60, monitor);
                    if (res.getDecompiledFunction() != null) {
                        pw.println(res.getDecompiledFunction().getC());
                    } else {
                        pw.println("(decompile failed)");
                    }
                    pw.println();

                    // Also list callers (xrefs-to function entry)
                    pw.println("--- callers of " + fn.getName() + " ---");
                    Reference[] refs = getReferencesTo(fn.getEntryPoint());
                    for (Reference r : refs) {
                        Function caller = getFunctionContaining(r.getFromAddress());
                        if (caller != null)
                            pw.println("  " + caller.getName() + "@" + caller.getEntryPoint() + " via " + r.getFromAddress() + " (" + r.getReferenceType() + ")");
                    }
                    pw.println();
                }
            }
        }
        decomp.dispose();
        println("Wrote decompile dump");
    }
}
