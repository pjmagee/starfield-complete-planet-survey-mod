// Find functions that (a) reference a given global symbol (ID_<n>) and
// (b) contain a store/load to [reg + <offset>]. Used to locate the setter
// of a singleton field, e.g. BGSPlanet::Manager (ID_937609) field +0x80.
//
// Headless: -postScript FindFieldStores.java <outPath> <globalId> <hexOffset>
// Reports every MOV/store/load instruction whose displacement == offset,
// inside any function that also references ID_<globalId>. Marks W if the
// destination is the memory operand (a write), R if source.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindFieldStores extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: FindFieldStores <out> <globalId> <hexOffset>"); return; }
        String outPath = args[0];
        String gid = args[1].trim();
        long off = Long.parseLong(args[2].replace("0x", ""), 16);

        SymbolTable st = currentProgram.getSymbolTable();

        // Collect all functions that reference ID_<gid>.
        Set<Address> fnsRefGlobal = new HashSet<>();
        SymbolIterator sit = st.getSymbols("ID_" + gid);
        while (sit.hasNext()) {
            Symbol s = sit.next();
            for (Reference r : getReferencesTo(s.getAddress())) {
                Function fn = getFunctionContaining(r.getFromAddress());
                if (fn != null) fnsRefGlobal.add(fn.getEntryPoint());
            }
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("Functions referencing ID_" + gid + ": " + fnsRefGlobal.size());
            pw.println("Instructions touching [reg + 0x" + Long.toHexString(off) + "] in those functions:");
            pw.println();
            for (Address fa : fnsRefGlobal) {
                Function fn = getFunctionAt(fa);
                if (fn == null) continue;
                boolean header = false;
                InstructionIterator it =
                    currentProgram.getListing().getInstructions(fn.getBody(), true);
                while (it.hasNext()) {
                    Instruction insn = it.next();
                    String mn = insn.getMnemonicString();
                    // Only interested in data-movement-ish instructions.
                    boolean interesting = false;
                    for (int op = 0; op < insn.getNumOperands(); op++) {
                        for (Object o : insn.getOpObjects(op)) {
                            if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == off) {
                                interesting = true;
                            }
                        }
                    }
                    if (!interesting) continue;
                    if (!header) {
                        pw.println("=== " + fn.getName() + " @ " + fn.getEntryPoint() + " ===");
                        header = true;
                    }
                    // crude W/R tag: if operand 0 (dest) is the memory ref -> write
                    String tag = "?";
                    String rep0 = insn.getDefaultOperandRepresentation(0);
                    if (rep0 != null && rep0.contains("0x" + Long.toHexString(off))) tag = "W";
                    else tag = "R";
                    pw.println("  [" + tag + "] " + insn.getAddress() + "  " + insn + "   ("
                        + mn + ")");
                }
                if (header) pw.println();
            }
        }
        println("done -> " + outPath);
    }
}
