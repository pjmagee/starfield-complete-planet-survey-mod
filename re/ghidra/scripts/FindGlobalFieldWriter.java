// For a global pointer at <hexGlobalAddr>, find functions that (a) reference the global,
// and (b) within the same function store to [reg + <fieldOff>] where reg was loaded from
// that global. Approximation: report every function referencing the global, and list its
// stores to [reg + fieldOff] for the candidate offsets.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindGlobalFieldWriter extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        long global = Long.parseLong(args[1].toLowerCase().replace("0x",""), 16);
        // candidate field offsets to look for as STORE destinations
        String[] offs = {"0x38", "0x39", "0x3a", "0x3c", "0x10", "0x18", "0x20"};
        SymbolTable st = currentProgram.getSymbolTable();
        Address ga = toAddr(global);
        Reference[] refs = getReferencesTo(ga);
        Set<Function> fns = new HashSet<>();
        for (Reference r : refs) {
            Function fn = getFunctionContaining(r.getFromAddress());
            if (fn != null) fns.add(fn);
        }
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("# functions referencing global " + ga + " that STORE to candidate field offsets");
            for (Function fn : fns) {
                StringBuilder sb = new StringBuilder();
                InstructionIterator ii = currentProgram.getListing().getInstructions(fn.getBody(), true);
                while (ii.hasNext()) {
                    Instruction insn = ii.next();
                    String t = insn.toString();
                    if (!t.startsWith("MOV")) continue;
                    for (String o : offs) {
                        // store form: MOV <size> ptr [REG + o], SRC  (dest is memory)
                        String needle = "ptr [";
                        int pi = t.indexOf("+ " + o + "],");
                        if (pi > 0 && t.contains(needle) && t.indexOf(needle) < pi
                            && !t.contains("[RBP") && !t.contains("[RSP")) {
                            sb.append("    ").append(insn.getAddress()).append("  ").append(t).append("\n");
                        }
                    }
                }
                if (sb.length() > 0) {
                    String idsym = "";
                    for (Symbol sym : st.getSymbols(fn.getEntryPoint()))
                        if (sym.getName().startsWith("ID_")) { idsym = " [" + sym.getName() + "]"; break; }
                    pw.println("==== " + fn.getName() + "@" + fn.getEntryPoint() + idsym + " ====");
                    pw.print(sb);
                }
            }
        }
        println("done");
    }
}
