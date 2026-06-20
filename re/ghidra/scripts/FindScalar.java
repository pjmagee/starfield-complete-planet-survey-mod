// Find every function with an instruction referencing a given scalar immediate.
// Use to locate subrecord-signature parse sites, e.g. 'PPBD' (LE uint32 0x44425050).
// Headless: -postScript FindScalar.java <outPath> <hexScalar>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.scalar.Scalar;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;

public class FindScalar extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: FindScalar <out> <hexScalar>"); return; }
        String outPath = args[0];
        long target = Long.parseLong(args[1].replace("0x", ""), 16);

        Set<Address> seenFns = new HashSet<>();
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("Instructions referencing scalar 0x" + Long.toHexString(target) + ":");
            InstructionIterator it = currentProgram.getListing().getInstructions(true);
            while (it.hasNext()) {
                Instruction insn = it.next();
                for (int op = 0; op < insn.getNumOperands(); op++) {
                    for (Object o : insn.getOpObjects(op)) {
                        if (o instanceof Scalar && ((Scalar) o).getUnsignedValue() == target) {
                            Function fn = getFunctionContaining(insn.getAddress());
                            String fnName = fn != null ? fn.getName() + "@" + fn.getEntryPoint() : "<none>";
                            pw.println("  " + insn.getAddress() + "  " + insn + "   in " + fnName);
                            if (fn != null) seenFns.add(fn.getEntryPoint());
                        }
                    }
                }
            }
            pw.println();
            pw.println("Distinct containing functions:");
            for (Address a : seenFns) pw.println("  " + a);
        }
        println("done");
    }
}
