// Dumps xrefs to key survey-related Address Library IDs.
// Seed IDs (from CommonLibSF):
//   92501  BGSPlanet::PlayerKnowledgeFlagSetEvent::GetEventSource
//   92502  StarMap::PlanetTraitKnownEvent::GetEventSource
//   174581 Spaceship::PlanetScanEvent::GetEventSource
//   402674 BGSPlanet::PlanetData vtable
//   402676 BGSPlanet::PlanetData vtable (secondary)
//   867581 BSGalaxy::PlayerKnowledge component factory (RTTI) — the per-planet survey state
//   860384 BSTEventSink<PlayerPlanetSurveyCompleteEvent> (RTTI)
//   855566 BSTEventSink<PlayerPlanetSurveyProgressEvent> (RTTI)
//   860436 BSTEventSink<BGSPlanet::PlayerKnowledgeFlagSetEvent> (RTTI)
//   843159 BGSScanPlanetActivity (RTTI) — the scan-planet activity class
//   843161 BSTEventSink<Spaceship::PlanetScanEvent> (RTTI)
// Headless: pass output txt path as script arg.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.List;

public class DumpSurveyXrefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String outPath = args.length > 0 ? args[0]
            : getSourceFile().getParentFile().getParent() + "/output/survey-xrefs.txt";

        List<Long> seedIds = Arrays.asList(
            92501L, 92502L, 174581L, 402674L, 402676L,
            867581L, 860384L, 855566L, 860436L, 843159L, 843161L,
            // PlanetData ctors / callers found in first pass:
            51449L, 51399L, 19496L, 92492L
        );
        SymbolTable st = currentProgram.getSymbolTable();

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (Long id : seedIds) {
                String label = "ID_" + id;
                pw.println("=== " + label + " ===");
                SymbolIterator it = st.getSymbols(label);
                boolean found = false;
                while (it.hasNext()) {
                    Symbol s = it.next();
                    Address a = s.getAddress();
                    pw.println("at " + a);
                    found = true;

                    Reference[] refs = getReferencesTo(a);
                    for (Reference r : refs) {
                        Address from = r.getFromAddress();
                        Function fn = getFunctionContaining(from);
                        String fnName = fn != null ? fn.getName() + "@" + fn.getEntryPoint() : "<none>";
                        pw.println("  xref from " + from + " in " + fnName + " type=" + r.getReferenceType());
                    }
                }
                if (!found) pw.println("  (label not found)");
                pw.println();
            }
        }
        println("Wrote xref dump to " + outPath);
    }
}
