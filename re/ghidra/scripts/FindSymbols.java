// List all symbols whose name contains ANY of the given substrings (case-insensitive).
// Headless: <outPath> <substr1> [substr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindSymbols extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: <outPath> <substr...>"); return; }
        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            SymbolIterator it = st.getSymbolIterator();
            int count = 0;
            while (it.hasNext()) {
                Symbol s = it.next();
                String n = s.getName();
                if (n == null) continue;
                String ln = n.toLowerCase();
                for (int i = 1; i < args.length; i++) {
                    if (ln.contains(args[i].toLowerCase())) {
                        pw.println(s.getAddress() + "  " + n);
                        count++;
                        break;
                    }
                }
            }
            pw.println("\n# total: " + count);
        }
        println("Wrote " + args[0]);
    }
}
