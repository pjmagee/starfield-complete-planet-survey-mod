// Decompile an arbitrary list of Address Library IDs to C.
// Headless usage:
//   analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
//     -scriptPath re/ghidra/scripts -postScript DecompileIds.java <outPath> <id1> <id2> ...
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

public class DecompileIds extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) {
            println("usage: DecompileIds <outPath> <id...>");
            return;
        }
        String outPath = args[0];

        SymbolTable st = currentProgram.getSymbolTable();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String label = "ID_" + args[i];
                SymbolIterator it = st.getSymbols(label);
                boolean found = false;
                while (it.hasNext()) {
                    Symbol s = it.next();
                    Function fn = getFunctionAt(s.getAddress());
                    if (fn == null) fn = getFunctionContaining(s.getAddress());
                    if (fn == null) continue;
                    found = true;
                    pw.println("======== " + label + " / " + fn.getName() + " @ " + fn.getEntryPoint() + " ========");
                    DecompileResults res = decomp.decompileFunction(fn, 90, monitor);
                    if (res.getDecompiledFunction() != null) {
                        pw.println(res.getDecompiledFunction().getC());
                    } else {
                        pw.println("(decompile failed)");
                    }
                    pw.println();
                }
                if (!found) pw.println("======== " + label + ": (symbol not found) ========\n");
            }
        }
        decomp.dispose();
        println("Wrote decompile dump to " + outPath);
    }
}
