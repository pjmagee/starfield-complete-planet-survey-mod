// Find xrefs to ID_938333 (trait discriminator) and functions structurally similar to ID_52154/52155
// that call ID_126578 and ID_126806 — those are knowledge-DB accessors.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
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
import java.util.TreeSet;

public class FindDiscriminators extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        SymbolTable st = currentProgram.getSymbolTable();

        // 1. Xrefs to ID_938333 — reveal all functions that use the trait discriminator (or siblings nearby in code).
        Symbol disc = st.getSymbols("ID_938333").hasNext() ? st.getSymbols("ID_938333").next() : null;
        // 2. Xrefs to ID_126578 — the knowledge-DB singleton getter. Every knowledge accessor calls this.
        Address kdb = st.getSymbols("ID_126578").hasNext() ? st.getSymbols("ID_126578").next().getAddress() : null;
        Address kdbMapLookup = st.getSymbols("ID_126806").hasNext() ? st.getSymbols("ID_126806").next().getAddress() : null;

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            if (disc != null) {
                pw.println("=== xrefs to ID_938333 (trait discriminator) @ " + disc.getAddress() + " ===");
                Reference[] refs = getReferencesTo(disc.getAddress());
                Set<Address> fns = new TreeSet<>();
                for (Reference r : refs) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f != null) fns.add(f.getEntryPoint());
                    else pw.println("  (no fn) " + r.getFromAddress());
                }
                for (Address a : fns) {
                    Function f = getFunctionAt(a);
                    pw.println("  " + f.getName() + " @ " + a);
                }
                pw.println();
            }

            // Collect callers of both ID_126578 and ID_126806 — knowledge accessors typically use both.
            Set<Address> dbCallers = new TreeSet<>();
            Set<Address> mapCallers = new TreeSet<>();
            if (kdb != null) {
                for (Reference r : getReferencesTo(kdb)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f != null) dbCallers.add(f.getEntryPoint());
                }
            }
            if (kdbMapLookup != null) {
                for (Reference r : getReferencesTo(kdbMapLookup)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f != null) mapCallers.add(f.getEntryPoint());
                }
            }
            pw.println("=== functions calling BOTH ID_126578 and ID_126806 (knowledge accessors) ===");
            Set<Address> both = new TreeSet<>(dbCallers);
            both.retainAll(mapCallers);
            for (Address a : both) {
                Function f = getFunctionAt(a);
                pw.println("  " + f.getName() + " @ " + a + " (size " + f.getBody().getNumAddresses() + ")");
            }
            pw.println();

            // Decompile each with short timeout
            DecompInterface decomp = new DecompInterface();
            decomp.openProgram(currentProgram);
            for (Address a : both) {
                Function f = getFunctionAt(a);
                pw.println("=== " + f.getName() + " ===");
                DecompileResults res = decomp.decompileFunction(f, 30, monitor);
                if (res != null && res.getDecompiledFunction() != null) {
                    String code = res.getDecompiledFunction().getC();
                    if (code.length() > 3000) code = code.substring(0, 3000) + "\n...[truncated]";
                    pw.println(code);
                }
                pw.println();
            }
            decomp.dispose();
        }
        println("Wrote " + outPath);
    }
}
