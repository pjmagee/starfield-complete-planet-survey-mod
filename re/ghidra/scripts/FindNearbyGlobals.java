// ID_938333 is the trait-knowledge discriminator at 1461e9b94.
// Siblings (flora/fauna/resource) likely live at nearby addresses.
// Dump ID_* labels and their 2-byte values in range [1461e9b00..1461ea000].
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.TreeMap;

public class FindNearbyGlobals extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];
        long start = 0x1461e9000L;
        long end   = 0x1461ea000L;

        SymbolTable st = currentProgram.getSymbolTable();
        Memory mem = currentProgram.getMemory();

        TreeMap<Long, String> labels = new TreeMap<>();
        SymbolIterator it = st.getAllSymbols(true);
        while (it.hasNext()) {
            Symbol s = it.next();
            long addr = s.getAddress().getOffset();
            if (addr >= start && addr < end && s.getName().startsWith("ID_")) {
                labels.merge(addr, s.getName(), (a, b) -> a + "," + b);
            }
        }
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("Addr             | u16    | u32         | Label(s)");
            for (var e : labels.entrySet()) {
                Address a = toAddr(e.getKey());
                String u16 = "?", u32 = "?";
                try { u16 = String.format("0x%04X", mem.getShort(a) & 0xFFFF); } catch (Exception ex) {}
                try { u32 = String.format("0x%08X", mem.getInt(a)); } catch (Exception ex) {}
                pw.printf("%016x | %s | %s | %s%n", e.getKey(), u16, u32, e.getValue());
            }
        }
        println("Wrote " + outPath);
    }
}
