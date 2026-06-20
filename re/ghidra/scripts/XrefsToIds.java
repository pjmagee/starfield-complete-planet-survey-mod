// Dump callers (xrefs-to) for an arbitrary list of Address Library IDs.
// Headless usage:
//   analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
//     -scriptPath re/ghidra/scripts -postScript XrefsToIds.java <outPath> <id...>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class XrefsToIds extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: XrefsToIds <outPath> <id...>");
            return;
        }
        String outPath = args[0];
        SymbolTable st = currentProgram.getSymbolTable();

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String label = "ID_" + args[i];
                pw.println("======== " + label + " callers ========");
                SymbolIterator it = st.getSymbols(label);
                boolean found = false;
                while (it.hasNext()) {
                    Symbol s = it.next();
                    found = true;
                    Reference[] refs = getReferencesTo(s.getAddress());
                    for (Reference r : refs) {
                        Address from = r.getFromAddress();
                        Function fn = getFunctionContaining(from);
                        String fnName = fn != null ? fn.getName() + "@" + fn.getEntryPoint() : "<none>";
                        pw.println("  from " + from + " in " + fnName + " (" + r.getReferenceType() + ")");
                    }
                }
                if (!found) pw.println("  (symbol not found)");
                pw.println();
            }
        }
        println("Wrote xrefs to " + outPath);
    }
}
