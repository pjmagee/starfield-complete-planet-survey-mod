// Dump the raw instruction listing for specified function IDs — for inspecting
// register setup (RCX/RDX/R8/R9) at call sites the decompiler elides.
// Headless: <outpath> <id1> [id2] ...
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;

public class DumpAsm extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: <outpath> <id1> [id2] ..."); return; }
        SymbolTable st = currentProgram.getSymbolTable();
        try (PrintWriter pw = new PrintWriter(new FileWriter(args[0]))) {
            for (int k = 1; k < args.length; k++) {
                String id = args[k].trim();
                SymbolIterator it = st.getSymbols("ID_" + id);
                while (it.hasNext()) {
                    Symbol s = it.next();
                    Function fn = getFunctionContaining(s.getAddress());
                    if (fn == null) { pw.println("ID_" + id + ": no function"); continue; }
                    pw.println("=== ID_" + id + " / " + fn.getName() + " @ " + fn.getEntryPoint() + " ===");
                    InstructionIterator ii = currentProgram.getListing().getInstructions(fn.getBody(), true);
                    while (ii.hasNext()) {
                        Instruction ins = ii.next();
                        pw.println(ins.getAddress() + "  " + ins.toString());
                    }
                    pw.println();
                }
            }
        }
        println("Wrote asm dump to " + args[0]);
    }
}
