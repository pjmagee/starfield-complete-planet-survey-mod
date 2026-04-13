// Find survey-related string literals and their xrefs.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Reference;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Iterator;
import java.util.List;
import java.util.Set;

public class FindSurveyStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0]
            : getSourceFile().getParentFile().getParent() + "/output/survey-strings.txt";

        List<String> needles = Arrays.asList(
            "GetSurveyPercent",
            "SetTraitKnown",
            "IsTraitKnown",
            "GetBiomeFlora",
            "GetBiomeActors",
            "SetScanned",
            "PlayerKnowledge"
        );
        Set<String> needleSet = new HashSet<>(needles);

        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Listing listing = currentProgram.getListing();
        Iterator<Data> it = listing.getDefinedData(true);

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            Set<String> dumpedFns = new HashSet<>();
            while (it.hasNext()) {
                Data d = it.next();
                if (!d.hasStringValue()) continue;
                Object v = d.getValue();
                if (v == null) continue;
                String s = v.toString();
                if (!needleSet.contains(s)) continue;

                Address strAddr = d.getAddress();
                Reference[] refs = getReferencesTo(strAddr);
                pw.println("=== \"" + s + "\" @ " + strAddr + " (" + refs.length + " refs) ===");
                for (Reference r : refs) {
                    Function caller = getFunctionContaining(r.getFromAddress());
                    if (caller == null) {
                        pw.println("  ref @ " + r.getFromAddress() + " (outside function)");
                        continue;
                    }
                    pw.println("  ref @ " + r.getFromAddress() + " in " + caller.getName() + "@" + caller.getEntryPoint());
                    String key = caller.getEntryPoint().toString() + "|" + s;
                    if (dumpedFns.contains(key)) continue;
                    dumpedFns.add(key);
                    DecompileResults res = decomp.decompileFunction(caller, 30, monitor);
                    if (res != null && res.getDecompiledFunction() != null) {
                        pw.println("    --- decomp ---");
                        String code = res.getDecompiledFunction().getC();
                        if (code.length() > 4000) code = code.substring(0, 4000) + "\n...[truncated]";
                        for (String line : code.split("\n")) pw.println("    " + line);
                        pw.println("    --- /decomp ---");
                    }
                }
                pw.println();
            }
        }
        decomp.dispose();
        println("Wrote survey strings dump");
    }
}
