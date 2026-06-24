// Find defined strings containing ANY of the given substrings (case-insensitive),
// and list all xrefs to each string with the containing function (+ ID_ symbol).
// Headless: <outPath> <substr1> [substr2 ...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSetView;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindStringXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: <outPath> <substr...>"); return; }
        SymbolTable st = currentProgram.getSymbolTable();
        Listing listing = currentProgram.getListing();
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            DataIterator di = listing.getDefinedData(true);
            int hits = 0;
            while (di.hasNext()) {
                Data d = di.next();
                if (d == null) continue;
                String t = d.getDataType() != null ? d.getDataType().getName().toLowerCase() : "";
                if (!(t.contains("unicode") || t.contains("string") || t.contains("char"))) continue;
                Object v = d.getValue();
                if (v == null) continue;
                String s = v.toString();
                String ls = s.toLowerCase();
                boolean match = false;
                for (int i = 1; i < args.length; i++) {
                    if (ls.contains(args[i].toLowerCase())) { match = true; break; }
                }
                if (!match) continue;
                hits++;
                pw.println("STR @ " + d.getAddress() + " : \"" + s.replace("\n","\\n") + "\"");
                Reference[] refs = getReferencesTo(d.getAddress());
                if (refs.length == 0) { pw.println("    (no direct xrefs)"); }
                for (Reference r : refs) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    String idsym = "";
                    if (f != null) for (Symbol sym : st.getSymbols(f.getEntryPoint()))
                        if (sym.getName().startsWith("ID_")) { idsym = " [" + sym.getName() + "]"; break; }
                    pw.println("    <- " + r.getFromAddress() + " " + r.getReferenceType()
                        + " in " + (f != null ? f.getName() + "@" + f.getEntryPoint() : "(none)") + idsym);
                }
                pw.println();
            }
            pw.println("# total string hits: " + hits);
        }
        println("Wrote " + args[0]);
    }
}
