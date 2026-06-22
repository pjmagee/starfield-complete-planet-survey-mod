// List all references to a given ID_<n> symbol's address, with containing function.
// Headless: <outPath> <id...>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class XrefsToId extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String label = args[i].startsWith("ID_") ? args[i] : "ID_" + args[i];
                Address addr = null;
                SymbolIterator it = st.getSymbols(label);
                if (it.hasNext()) addr = it.next().getAddress();
                pw.println("==== " + label + " @ " + addr + " ====");
                if (addr == null) { pw.println("  not found\n"); continue; }
                Reference[] refs = getReferencesTo(addr);
                for (Reference r : refs) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    String idsym = "";
                    if (f != null) for (Symbol s : st.getSymbols(f.getEntryPoint()))
                        if (s.getName().startsWith("ID_")) { idsym = " [" + s.getName() + "]"; break; }
                    pw.println("  <- " + r.getFromAddress() + " " + r.getReferenceType()
                        + " in " + (f != null ? f.getName() + "@" + f.getEntryPoint() : "(none)") + idsym);
                }
                pw.println();
            }
        }
        println("wrote " + outPath);
    }
}
