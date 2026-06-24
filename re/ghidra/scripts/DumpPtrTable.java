// Dump N pointers starting at each raw hex address; resolve each pointer's symbol
// AND, if it points to a function, name it. Used to inspect factory/serializer tables.
// Headless: <outPath> <count> <hexaddr...>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpPtrTable extends GhidraScript {
    private SymbolTable st;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: DumpPtrTable <outPath> <count> <hexaddr...>"); return; }
        st = currentProgram.getSymbolTable();
        int count = Integer.parseInt(args[1]);
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int i = 2; i < args.length; i++) {
                String h = args[i].toLowerCase().replace("0x", "");
                Address base = toAddr(Long.parseLong(h, 16));
                pw.println("======== table @ " + base + " " + symAt(base) + " ========");
                for (int k = 0; k < count; k++) {
                    Address slot = base.add((long) k * 8);
                    long val;
                    try { val = getLong(slot); } catch (Exception e) { pw.println(String.format("  +0x%02x: <unreadable>", k*8)); continue; }
                    String desc = "";
                    if (val != 0) {
                        Address ta = toAddr(val);
                        Function fn = getFunctionAt(ta);
                        if (fn != null) desc = "FUNC " + fn.getName();
                        else {
                            String s = symAt(ta);
                            desc = s.isEmpty() ? "(data)" : s;
                        }
                    } else desc = "NULL";
                    pw.println(String.format("  +0x%02x: 0x%012x  %s", k*8, val, desc));
                }
                pw.println();
            }
        }
        println("Wrote " + args[0]);
    }

    private String symAt(Address at) {
        if (at == null) return "";
        Symbol[] syms = st.getSymbols(at);
        if (syms == null || syms.length == 0) return "";
        StringBuilder sb = new StringBuilder("[");
        boolean first = true;
        for (Symbol s : syms) { if (!first) sb.append(" / "); sb.append(s.getName()); first = false; }
        sb.append("]");
        return sb.toString();
    }
}
