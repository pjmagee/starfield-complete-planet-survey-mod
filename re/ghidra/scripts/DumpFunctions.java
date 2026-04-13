// Dump decompile for specified function IDs (not truncated, full output).
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpFunctions extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: <outpath> <id1> [id2] ..."); return; }
        String outPath = args[0];
        String[] ids = new String[args.length - 1];
        for (int k = 1; k < args.length; k++) ids[k - 1] = args[k];

        SymbolTable st = currentProgram.getSymbolTable();
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (String idStr : ids) {
                SymbolIterator it = st.getSymbols("ID_" + idStr.trim());
                while (it.hasNext()) {
                    Symbol s = it.next();
                    Function fn = getFunctionAt(s.getAddress());
                    if (fn == null) fn = getFunctionContaining(s.getAddress());
                    if (fn == null) { pw.println("ID_" + idStr + ": no function"); continue; }
                    pw.println("=== ID_" + idStr + " / " + fn.getName() + " @ " + fn.getEntryPoint() + " ===");
                    DecompileResults res = decomp.decompileFunction(fn, 120, monitor);
                    if (res != null && res.getDecompiledFunction() != null) {
                        pw.println(res.getDecompiledFunction().getC());
                    } else {
                        pw.println("(decompile failed)");
                    }
                    pw.println();
                }
            }
        }
        decomp.dispose();
        println("Wrote dump to " + outPath);
    }
}
