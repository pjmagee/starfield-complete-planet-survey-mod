// Ghidra script: import Address Library txt and create labels ID_<n> at each VA.
// Headless usage: pass txt path as script arg.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.SymbolTable;
import java.io.BufferedReader;
import java.io.FileReader;

public class ImportAddressLibrary extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        java.io.File f = args.length > 0 ? new java.io.File(args[0]) : askFile("Select offsets txt", "Import");
        SymbolTable st = currentProgram.getSymbolTable();
        int count = 0;
        try (BufferedReader br = new BufferedReader(new FileReader(f))) {
            String line;
            while ((line = br.readLine()) != null) {
                String[] parts = line.trim().split("\\s+");
                if (parts.length < 2) continue;
                try {
                    long id = Long.parseLong(parts[0]);
                    long va = Long.parseLong(parts[1], 16);
                    Address addr = toAddr(va);
                    st.createLabel(addr, "ID_" + id, SourceType.IMPORTED);
                    count++;
                    if (count % 50000 == 0) println("Labeled " + count);
                } catch (NumberFormatException e) { /* skip header/garbage */ }
            }
        }
        println("Done. Labels created: " + count);
    }
}
