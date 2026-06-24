// Whole-binary scan: find functions that contain a store "MOV [reg + offA], r32"
// AND also reference a given anchor global symbol ID_<anchor> somewhere in the
// same function (read or write). Used to find the BGSPlanet::Manager method that
// stamps +0x80 together with the adjacent current-planet global ID_937669.
//
// Headless: -postScript FindStoreSig.java <out> <hexOffA> <anchorId>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindStoreSig extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 3) { println("usage: FindStoreSig <out> <hexOffA> <anchorId>"); return; }
        String outPath = args[0];
        long offA = Long.parseLong(args[1].replace("0x", ""), 16);
        String anchor = "ID_" + args[2].trim();

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("Functions with a 4-byte store to [reg+0x" + Long.toHexString(offA)
                + "] that also reference " + anchor + ":");
            pw.println();
            FunctionIterator fit = currentProgram.getFunctionManager().getFunctions(true);
            while (fit.hasNext()) {
                Function fn = fit.next();
                boolean hasStore = false;
                boolean refsAnchor = false;
                String storeLine = null;
                InstructionIterator it =
                    currentProgram.getListing().getInstructions(fn.getBody(), true);
                while (it.hasNext()) {
                    Instruction insn = it.next();
                    // store to [reg + offA] as a MOV with dword operand
                    if (insn.getMnemonicString().equals("MOV")) {
                        String rep0 = insn.getDefaultOperandRepresentation(0);
                        if (rep0 != null && rep0.startsWith("dword ptr [")
                                && rep0.contains("0x" + Long.toHexString(offA) + "]")) {
                            hasStore = true;
                            if (storeLine == null) storeLine = insn.getAddress() + "  " + insn;
                        }
                    }
                    // references to anchor global via this instruction's refs
                    for (Reference r : insn.getReferencesFrom()) {
                        Address to = r.getToAddress();
                        if (to != null) {
                            ghidra.program.model.symbol.Symbol sym =
                                getSymbolAt(to);
                            if (sym != null && sym.getName().equals(anchor)) refsAnchor = true;
                        }
                    }
                }
                if (hasStore && refsAnchor) {
                    pw.println("=== " + fn.getName() + " @ " + fn.getEntryPoint()
                        + " ===   first store: " + storeLine);
                }
            }
        }
        println("done -> " + outPath);
    }
}
