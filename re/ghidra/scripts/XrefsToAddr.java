// Dump all references to one or more raw hex addresses (data globals), with containing function.
// Headless: <outPath> <hexaddr...>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class XrefsToAddr extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String h = args[i].toLowerCase().replace("0x", "");
                Address a = toAddr(Long.parseLong(h, 16));
                pw.println("======== xrefs to " + a + " ========");
                for (Symbol s : st.getSymbols(a)) pw.println("  symbol: " + s.getName());
                Reference[] refs = getReferencesTo(a);
                for (Reference r : refs) {
                    Function fn = getFunctionContaining(r.getFromAddress());
                    String fnName = fn != null ? fn.getName() + "@" + fn.getEntryPoint() : "<none>";
                    String idsym = "";
                    if (fn != null) {
                        for (Symbol sym : st.getSymbols(fn.getEntryPoint()))
                            if (sym.getName().startsWith("ID_")) { idsym = " [" + sym.getName() + "]"; break; }
                    }
                    pw.println("  <- " + r.getFromAddress() + " in " + fnName + idsym + " (" + r.getReferenceType() + ")");
                }
                pw.println();
            }
        }
        println("done");
    }
}
