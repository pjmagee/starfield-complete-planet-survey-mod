// Locate a vtable symbol by name substring, dump its slots resolved to functions.
// Headless: <outPath> <nameSubstr> <numSlots>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindVtableAndDump extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: <outPath> <nameSubstr> <numSlots>"); return; }
        String sub = args[1];
        int n = Integer.parseInt(args[2]);
        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            SymbolIterator it = st.getAllSymbols(false);
            int found = 0;
            while (it.hasNext()) {
                Symbol s = it.next();
                String nm = s.getName(true);
                if (nm == null || !nm.contains(sub) || !nm.toLowerCase().contains("vftable")) continue;
                found++;
                Address base = s.getAddress();
                pw.println("==== " + nm + " @ " + base + " ====");
                for (int i = 0; i < n; i++) {
                    Address slotAddr = base.add((long) i * 8);
                    try {
                        long ptr = getLong(slotAddr);
                        Address t = toAddr(ptr);
                        Function fn = getFunctionContaining(t);
                        pw.println("  +0x" + Long.toHexString((long) i * 8) + " -> " + t
                            + (fn != null ? "  " + fn.getName() : ""));
                    } catch (Exception e) { pw.println("  +0x" + Long.toHexString((long) i * 8) + " (err)"); break; }
                }
                pw.println();
            }
            if (found == 0) pw.println("no vtable symbol matching: " + sub);
        }
        println("done");
    }
}
