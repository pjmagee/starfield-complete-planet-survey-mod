// Walk the real console-command table (RE::ID::Script::GetConsoleCommands = 896666,
// symbol ID_896666) as an array of SCRIPT_FUNCTION (stride 0x58) and, for each
// entry whose functionName matches one of the requested names, dump the struct
// fields and decompile the executeFunction (@ +0x30).
//
// The ID_896666 symbol is the relocation target = base of the SCRIPT_FUNCTION[]
// array (see Script.h GetConsoleCommands()). kNumConsoleCommands = 0x244.
//
// Headless usage:
//   analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
//     -scriptPath re/ghidra/scripts -postScript DumpConsoleCommandTable.java <outPath> <name...>
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.Set;

public class DumpConsoleCommandTable extends GhidraScript {

    static final long STRIDE = 0x58;
    static final int  NUM_CONSOLE = 0x244;

    static final long OFF_FUNCTIONNAME = 0x00;
    static final long OFF_SHORTNAME    = 0x08;
    static final long OFF_HELPSTRING   = 0x18;
    static final long OFF_NUMPARAMS    = 0x22;
    static final long OFF_PARAMS       = 0x28;
    static final long OFF_EXECUTEFUNC  = 0x30;
    static final long OFF_COMPILEFUNC  = 0x38;
    static final long OFF_CONDFUNC     = 0x40;

    private Memory mem;
    private SymbolTable st;
    private DecompInterface decomp;

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) { println("usage: DumpConsoleCommandTable <outPath> <name...>"); return; }
        String outPath = args[0];
        Set<String> wanted = new HashSet<>(Arrays.asList(args).subList(1, args.length));

        mem = currentProgram.getMemory();
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Address tableBase = symbolAddr("ID_896666");
        if (tableBase == null) {
            println("ID_896666 not found");
            return;
        }

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            pw.println("Console command table base (ID_896666) = " + tableBase
                + "  num=" + NUM_CONSOLE + " stride=0x" + Long.toHexString(STRIDE));
            pw.println();

            for (int i = 0; i < NUM_CONSOLE; i++) {
                Address base = tableBase.add((long) i * STRIDE);
                Address pName = readPtr(base.add(OFF_FUNCTIONNAME));
                String fname = readCStr(pName);
                if (fname == null || fname.isEmpty()) continue;
                if (!wanted.contains(fname)) continue;

                pw.println("################################################################");
                pw.println("## [" + i + "] opcode 0x" + Integer.toHexString(0x0100 + i)
                    + "  \"" + fname + "\"  (struct @ " + base + ")");
                pw.println("################################################################");
                dumpStruct(pw, base, fname);
                pw.println();
            }
        }
        decomp.dispose();
        println("Wrote console command table dump to " + outPath);
    }

    private void dumpStruct(PrintWriter pw, Address base, String name) throws Exception {
        Address pShort = readPtr(base.add(OFF_SHORTNAME));
        Address pHelp = readPtr(base.add(OFF_HELPSTRING));
        int numParams = mem.getShort(base.add(OFF_NUMPARAMS)) & 0xFFFF;
        Address pParams = readPtr(base.add(OFF_PARAMS));
        Address pExec = readPtr(base.add(OFF_EXECUTEFUNC));
        Address pCompile = readPtr(base.add(OFF_COMPILEFUNC));
        Address pCond = readPtr(base.add(OFF_CONDFUNC));

        pw.println("  shortName/alias(+0x08) = \"" + readCStr(pShort) + "\"");
        pw.println("  helpString    (+0x18)  = \"" + readCStr(pHelp) + "\"");
        pw.println("  numParams     (+0x22)  = " + numParams);
        pw.println("  executeFunc   (+0x30)  = " + pExec + "  " + symAt(pExec));
        pw.println("  compileFunc   (+0x38)  = " + pCompile + "  " + symAt(pCompile));
        pw.println("  condFunc      (+0x40)  = " + pCond + "  " + symAt(pCond));

        if (pParams != null && numParams > 0 && numParams < 32) {
            pw.println("  params[]:");
            for (int p = 0; p < numParams; p++) {
                Address pe = pParams.add((long) p * 0x10);
                Address pn = readPtr(pe.add(0x00));
                int ptype = mem.getInt(pe.add(0x08));
                int opt = mem.getByte(pe.add(0x0C)) & 0xFF;
                pw.println("    [" + p + "] name=\"" + readCStr(pn)
                    + "\" type=0x" + Integer.toHexString(ptype) + " optional=" + opt);
            }
        }

        if (pExec == null) { pw.println("  (executeFunction null)"); return; }
        Function fn = getFunctionAt(pExec);
        if (fn == null) fn = getFunctionContaining(pExec);
        if (fn == null) { pw.println("  (no function at " + pExec + ")"); return; }

        pw.println("  ==== executeFunction " + fn.getName() + " @ " + fn.getEntryPoint()
            + " " + symAt(fn.getEntryPoint()) + " ====");

        pw.println("  --- call targets ---");
        Set<String> seen = new LinkedHashSet<>();
        InstructionIterator iit = currentProgram.getListing().getInstructions(fn.getBody(), true);
        while (iit.hasNext()) {
            Instruction ins = iit.next();
            String mn = ins.getMnemonicString();
            if (!mn.equals("CALL") && !mn.equals("JMP")) continue;
            for (Reference rr : ins.getReferencesFrom()) {
                RefType rt = rr.getReferenceType();
                if (rt == RefType.UNCONDITIONAL_CALL || rt == RefType.COMPUTED_CALL
                    || rt == RefType.CONDITIONAL_CALL || (mn.equals("JMP") && rt.isComputed())) {
                    Address t = rr.getToAddress();
                    Function tf = getFunctionAt(t);
                    String label = "@" + ins.getAddress() + " " + mn + " -> "
                        + (tf != null ? tf.getName() : "?") + " " + symAt(t);
                    if (seen.add(label)) pw.println("    " + label);
                }
            }
        }

        pw.println("  --- decomp ---");
        DecompileResults res = decomp.decompileFunction(fn, 180, monitor);
        if (res != null && res.getDecompiledFunction() != null) {
            pw.println(res.getDecompiledFunction().getC());
        } else {
            pw.println("  (decompile failed)");
        }
        pw.println("  ==== /executeFunction ====");
    }

    private Address symbolAddr(String label) {
        SymbolIterator it = st.getSymbols(label);
        if (it.hasNext()) return it.next().getAddress();
        return null;
    }

    private Address readPtr(Address at) {
        try { long p = mem.getLong(at); return p == 0 ? null : toAddr(p); }
        catch (Exception e) { return null; }
    }

    private String readCStr(Address at) {
        if (at == null) return "";
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 256; i++) {
                byte b = mem.getByte(at.add(i));
                if (b == 0) break;
                sb.append((char) (b & 0xFF));
            }
        } catch (Exception e) { return sb.toString() + "<unmapped>"; }
        return sb.toString();
    }

    private String symAt(Address at) {
        if (at == null) return "";
        Symbol[] syms = st.getSymbols(at);
        if (syms == null || syms.length == 0) return "";
        String idSym = null, other = null;
        for (Symbol s : syms) {
            String n = s.getName();
            if (n.startsWith("ID_") && idSym == null) idSym = n;
            else if (other == null) other = n;
        }
        StringBuilder sb = new StringBuilder("[");
        if (idSym != null) sb.append(idSym);
        if (other != null) { if (idSym != null) sb.append(" / "); sb.append(other); }
        sb.append("]");
        return sb.toString();
    }
}
