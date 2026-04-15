// Decompile biome actor functions: ID_83010, ID_83012, ID_47749
// Outputs full C pseudocode to re/ghidra/output/biome-body.txt
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DecompileBiomeBody extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] targetIds = {"83010", "83012", "47749"};
        String outPath = "re/ghidra/output/biome-body.txt";

        SymbolTable st = currentProgram.getSymbolTable();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (String idStr : targetIds) {
                pw.println("================================================================================");
                pw.println("ID_" + idStr);
                pw.println("================================================================================");
                pw.println();

                SymbolIterator it = st.getSymbols("ID_" + idStr.trim());
                boolean found = false;
                while (it.hasNext()) {
                    Symbol s = it.next();
                    Function fn = getFunctionAt(s.getAddress());
                    if (fn == null) fn = getFunctionContaining(s.getAddress());
                    if (fn == null) {
                        pw.println("(no function at this address)");
                        continue;
                    }
                    found = true;
                    pw.println("Address: " + fn.getEntryPoint());
                    pw.println("Function name (Ghidra): " + fn.getName());
                    pw.println();
                    pw.println("--- Decompiled C pseudocode ---");
                    pw.println();

                    // 120 second timeout for decompilation
                    DecompileResults res = decomp.decompileFunction(fn, 120, monitor);
                    if (res != null && res.getDecompiledFunction() != null) {
                        String cCode = res.getDecompiledFunction().getC();
                        pw.println(cCode);
                    } else {
                        pw.println("(decompilation failed or timed out)");
                    }
                    pw.println();
                }
                if (!found) {
                    pw.println("(symbol ID_" + idStr + " not found)");
                    pw.println();
                }
            }
        }
        decomp.dispose();
        println("Decompilation complete. Output written to " + outPath);
    }
}
