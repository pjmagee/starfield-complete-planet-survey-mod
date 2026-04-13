// For a given Papyrus native registrar call site, find the impl function pointer
// loaded into r9 just before the call.
// Scan backwards from call at 141f1bdb9 (GetSurveyPercent).
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import java.io.FileWriter;
import java.io.PrintWriter;

public class FindNativeImpls extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outPath = getScriptArgs()[0];

        long[] callSites = {
            0x141f1bdb9L, // GetSurveyPercent
            0x141f1b9b0L, // rough area around IsTraitKnown/SetTraitKnown for verification
        };

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (long site : callSites) {
                pw.println("=== around " + String.format("%016x", site) + " ===");
                Address a = toAddr(site);
                // Scan backwards ~60 instructions looking for LEA/MOV into r9 (impl ptr arg4)
                Instruction ins = getInstructionBefore(a);
                int steps = 0;
                while (ins != null && steps < 80) {
                    String mn = ins.getMnemonicString();
                    String rep = ins.toString();
                    pw.println("  " + ins.getAddress() + ": " + rep);
                    if (rep.toUpperCase().contains(",R9") || rep.toUpperCase().contains("R9,") || rep.toUpperCase().contains(" R9 ")) {
                        pw.println("    ^^ R9 touched");
                    }
                    ins = getInstructionBefore(ins.getAddress());
                    steps++;
                }
                pw.println();
            }
        }
        println("wrote " + outPath);
    }
}
