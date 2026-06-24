// Whole-binary scan within an address range: report every "MOV [reg + offA], r32/imm"
// store, with the containing function. Used to find BGSPlanet::Manager's setter of
// field +0x80 (the current-body NumericID), which receives `this` in a register and
// therefore does not reference the singleton global.
//
// Headless: -postScript FindStoresInRange.java <out> <hexOffA> <loHex> <hiHex>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindStoresInRange extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 4) { println("usage: <out> <hexOffA> <loHex> <hiHex>"); return; }
        String outPath = args[0];
        String offHex = args[1].replace("0x", "").toLowerCase();
        long lo = Long.parseLong(args[2].replace("0x", ""), 16);
        long hi = Long.parseLong(args[3].replace("0x", ""), 16);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("Stores to [reg+0x" + offHex + "] in [" + Long.toHexString(lo)
                + "," + Long.toHexString(hi) + "]:");
            pw.println();
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext()) {
                Instruction insn = it.next();
                long a = insn.getAddress().getOffset();
                if (a < lo || a > hi) continue;
                if (!insn.getMnemonicString().equals("MOV")) continue;
                String rep0 = insn.getDefaultOperandRepresentation(0);
                if (rep0 == null) continue;
                if ((rep0.startsWith("dword ptr [") || rep0.startsWith("qword ptr ["))
                        && rep0.contains("0x" + offHex + "]")
                        && !rep0.contains("RSP") && !rep0.contains("RBP")) {
                    Function fn = getFunctionContaining(insn.getAddress());
                    String fnName = fn != null ? fn.getName() + "@" + fn.getEntryPoint() : "<none>";
                    pw.println("  " + insn.getAddress() + "  " + insn + "   in " + fnName);
                }
            }
        }
        println("done -> " + outPath);
    }
}
