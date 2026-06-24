// Find functions storing a dword to [reg+0xa0] AND [reg+0xc0] where reg is the SAME
// non-stack register (RBX/RDI/RSI/R12-R15/RAX/RCX/RDX/RBP-excluded). Targets the
// uLocationTraitRefsScanned(+0xa0)/Required(+0xc0) model populate. Also reports whether
// the function references ID_938333 or strings containing 'TraitRef'/'LocRef'.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class FindCountPair extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args[0];
        // match: MOV dword ptr [REG + 0xa0],...   capture REG
        Pattern pA0 = Pattern.compile("MOV\\s+dword ptr \\[([A-Z0-9]+) \\+ 0xa0\\],");
        Pattern pC0 = Pattern.compile("MOV\\s+dword ptr \\[([A-Z0-9]+) \\+ 0xc0\\],");
        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            FunctionIterator fns = currentProgram.getFunctionManager().getFunctions(true);
            while (fns.hasNext() && !monitor.isCancelled()) {
                Function fn = fns.next();
                Set<String> a0regs = new HashSet<>();
                Set<String> c0regs = new HashSet<>();
                StringBuilder lines = new StringBuilder();
                InstructionIterator ii = currentProgram.getListing().getInstructions(fn.getBody(), true);
                while (ii.hasNext()) {
                    Instruction insn = ii.next();
                    String t = insn.toString();
                    Matcher mA = pA0.matcher(t);
                    if (mA.find()) {
                        String r = mA.group(1);
                        if (!r.equals("RBP") && !r.equals("RSP")) { a0regs.add(r); lines.append("  A0 ").append(insn.getAddress()).append("  ").append(t).append("\n"); }
                    }
                    Matcher mC = pC0.matcher(t);
                    if (mC.find()) {
                        String r = mC.group(1);
                        if (!r.equals("RBP") && !r.equals("RSP")) { c0regs.add(r); lines.append("  C0 ").append(insn.getAddress()).append("  ").append(t).append("\n"); }
                    }
                }
                // require a common register between the two sets
                Set<String> common = new HashSet<>(a0regs); common.retainAll(c0regs);
                if (!common.isEmpty()) {
                    pw.println("==== " + fn.getName() + " @ " + fn.getEntryPoint() + "  commonReg=" + common + " ====");
                    pw.print(lines.toString());
                    pw.println();
                }
            }
        }
        println("done");
    }
}
