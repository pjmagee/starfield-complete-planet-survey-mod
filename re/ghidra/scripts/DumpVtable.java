// Dump a range of pointer slots from a vtable, resolving each to a function/symbol.
// Headless: <outPath> <vtableHex> <numSlots>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpVtable extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: <outPath> <vtableHex> <numSlots>"); return; }
        SymbolTable st = currentProgram.getSymbolTable();
        long vt = Long.parseLong(args[1].toLowerCase().replace("0x", ""), 16);
        int n = Integer.parseInt(args[2]);
        Address base = toAddr(vt);
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            pw.println("vtable base @ " + base);
            for (int i = 0; i < n; i++) {
                long off = (long) i * 8;
                Address slotAddr = base.add(off);
                String line = "  +0x" + Long.toHexString(off) + " @" + slotAddr + " : ";
                try {
                    long ptr = getLong(slotAddr);
                    Address t = toAddr(ptr);
                    Function fn = getFunctionAt(t);
                    if (fn == null) fn = getFunctionContaining(t);
                    String sym = "";
                    Symbol[] syms = st.getSymbols(t);
                    if (syms != null && syms.length > 0) {
                        StringBuilder sb = new StringBuilder();
                        for (Symbol s : syms) { sb.append(s.getName()).append(" "); }
                        sym = sb.toString().trim();
                    }
                    line += t + (fn != null ? " FUNC=" + fn.getName() : "") + (sym.isEmpty() ? "" : " [" + sym + "]");
                } catch (Exception e) { line += "(read err)"; }
                pw.println(line);
            }
        }
        println("Wrote " + args[0]);
    }
}
