// Dump the first N entries of the SCRIPT_FUNCTION table at ID_896666 to see what
// command names/opcodes it actually holds, and search it for target names.
// Headless: <outPath> <count> [name...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpTableHead extends GhidraScript {
    static final long STRIDE = 0x58;
    private Memory mem;
    private SymbolTable st;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        int count = Integer.parseInt(args[1]);
        mem = currentProgram.getMemory();
        st = currentProgram.getSymbolTable();

        Address base = null;
        SymbolIterator it = st.getSymbols("ID_896666");
        if (it.hasNext()) base = it.next().getAddress();
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("ID_896666 = " + base);
            if (base == null) { pw.println("not found"); return; }
            for (int i = 0; i < count; i++) {
                Address b = base.add((long) i * STRIDE);
                Address pn = readPtr(b);
                Address ps = readPtr(b.add(0x08));
                Address pexec = readPtr(b.add(0x30));
                int np = 0; try { np = mem.getShort(b.add(0x22)) & 0xFFFF; } catch (Exception e) {}
                pw.printf("[%3d] @%s name=\"%s\" alias=\"%s\" numParams=%d exec=%s %s%n",
                    i, b, readCStr(pn), readCStr(ps), np, pexec, symAt(pexec));
            }
        }
        println("wrote " + outPath);
    }

    private Address readPtr(Address at) {
        try { long p = mem.getLong(at); return p == 0 ? null : toAddr(p); } catch (Exception e) { return null; }
    }
    private String readCStr(Address at) {
        if (at == null) return "";
        StringBuilder sb = new StringBuilder();
        try { for (int i = 0; i < 64; i++) { byte b = mem.getByte(at.add(i)); if (b == 0) break; if (b < 0x20 || b > 0x7e) return "<bin>"; sb.append((char) b); } }
        catch (Exception e) { return "<unmapped>"; }
        return sb.toString();
    }
    private String symAt(Address at) {
        if (at == null) return "";
        Symbol[] s = st.getSymbols(at);
        if (s == null || s.length == 0) return "";
        for (Symbol x : s) if (x.getName().startsWith("ID_")) return "[" + x.getName() + "]";
        return "[" + s[0].getName() + "]";
    }
}
