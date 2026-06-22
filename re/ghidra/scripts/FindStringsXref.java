// Find defined strings matching any needle (substring, case-insensitive) and
// list the functions that reference them. Helps locate the real landing/travel
// implementation behind the stubbed console commands.
// Headless: <outPath> <needle...>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Iterator;
import java.util.Locale;

public class FindStringsXref extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        String[] needles = new String[args.length - 1];
        for (int i = 1; i < args.length; i++) needles[i - 1] = args[i].toLowerCase(Locale.ROOT);

        SymbolTable st = currentProgram.getSymbolTable();
        Listing listing = currentProgram.getListing();
        Iterator<Data> it = listing.getDefinedData(true);
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            while (it.hasNext()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                Object v = d.getValue();
                if (v == null) continue;
                String s = v.toString();
                String sl = s.toLowerCase(Locale.ROOT);
                boolean hit = false;
                for (String n : needles) if (sl.contains(n)) { hit = true; break; }
                if (!hit) continue;
                Address a = d.getAddress();
                Reference[] refs = getReferencesTo(a);
                pw.println("\"" + s + "\" @ " + a + " (" + refs.length + " refs)");
                for (Reference r : refs) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    String fn = f != null ? f.getName() + "@" + f.getEntryPoint() : "(no func)";
                    String idsym = "";
                    if (f != null) {
                        for (Symbol sym : st.getSymbols(f.getEntryPoint()))
                            if (sym.getName().startsWith("ID_")) { idsym = " [" + sym.getName() + "]"; break; }
                    }
                    pw.println("    <- " + r.getFromAddress() + " in " + fn + idsym);
                }
            }
        }
        println("wrote " + outPath);
    }
}
