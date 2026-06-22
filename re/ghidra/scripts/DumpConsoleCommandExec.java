// Resolve console-command SCRIPT_FUNCTION entries by command-name string and
// decompile their executeFunction (SCRIPT_FUNCTION.executeFunction @ +0x30).
//
// For each command name passed on argv, this script:
//   1. Finds the defined string literal whose value == the command name.
//   2. Finds every DATA reference to that string (the SCRIPT_FUNCTION.functionName
//      field @ +0x00 inside the command table in .rdata).
//   3. Treats each such ref site as a struct base and reads the pointer fields:
//        +0x00 functionName, +0x08 shortName, +0x18 helpString,
//        +0x22 numParams (u16), +0x28 params, +0x30 executeFunction,
//        +0x38 compileFunction, +0x40 conditionFunction.
//   4. Walks the SCRIPT_PARAMETER array (stride 0x10: name@+0x00, type@+0x08, optional@+0x0C)
//      to print the real arg signature.
//   5. Decompiles the executeFunction to the output file (logic modeled on DecompileIds.java),
//      and lists CALL targets resolved to ID_<n> address-library symbols where possible.
//
// Headless usage:
//   analyzeHeadless <proj> Starfield -process Starfield.exe -noanalysis \
//     -scriptPath re/ghidra/scripts -postScript DumpConsoleCommandExec.java <outPath> <name1> <name2> ...
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolTable;
import java.io.FileWriter;
import java.io.PrintWriter;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.Set;

public class DumpConsoleCommandExec extends GhidraScript {

    // SCRIPT_FUNCTION field offsets (verified against extern/CommonLibSF/.../Script.h)
    static final long OFF_FUNCTIONNAME = 0x00;
    static final long OFF_SHORTNAME    = 0x08;
    static final long OFF_OUTPUT       = 0x10;
    static final long OFF_HELPSTRING   = 0x18;
    static final long OFF_REFFUNC      = 0x20;
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
        if (args.length < 2) {
            println("usage: DumpConsoleCommandExec <outPath> <commandName...>");
            return;
        }
        String outPath = args[0];

        mem = currentProgram.getMemory();
        st = currentProgram.getSymbolTable();
        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        Listing listing = currentProgram.getListing();

        try (PrintWriter pw = new PrintWriter(new FileWriter(outPath))) {
            for (int i = 1; i < args.length; i++) {
                String name = args[i];
                pw.println("################################################################");
                pw.println("## COMMAND: " + name);
                pw.println("################################################################");

                // 1. Find the string literal(s) whose value == name.
                Set<Address> strAddrs = new LinkedHashSet<>();
                Iterator<Data> it = listing.getDefinedData(true);
                while (it.hasNext()) {
                    Data d = it.next();
                    if (!d.hasStringValue()) continue;
                    Object v = d.getValue();
                    if (v == null) continue;
                    if (name.equals(v.toString())) {
                        strAddrs.add(d.getAddress());
                    }
                }
                if (strAddrs.isEmpty()) {
                    pw.println("  (no string literal matched exactly)");
                    pw.println();
                    continue;
                }

                for (Address strAddr : strAddrs) {
                    pw.println("  string \"" + name + "\" @ " + strAddr);
                    Reference[] refs = getReferencesTo(strAddr);
                    // 2. Each DATA ref to the string is a candidate functionName field.
                    for (Reference r : refs) {
                        Address fromAddr = r.getFromAddress();
                        boolean isData = r.getReferenceType().isData();
                        // Code refs (e.g. _strnicmp lookups) are not the table; skip those.
                        Function inFn = getFunctionContaining(fromAddr);
                        if (inFn != null) {
                            // This ref lives inside a function body -> not a static table entry.
                            pw.println("    ref @ " + fromAddr + " inside function "
                                + inFn.getName() + " (skipped, not a table entry)");
                            continue;
                        }
                        pw.println("    --- candidate SCRIPT_FUNCTION base @ " + fromAddr
                            + " (ref type=" + r.getReferenceType() + ") ---");
                        dumpStruct(pw, fromAddr, name);
                    }
                }
                pw.println();
            }
        }
        decomp.dispose();
        println("Wrote console-command exec dump to " + outPath);
    }

    private void dumpStruct(PrintWriter pw, Address base, String expectedName) throws Exception {
        Address pName = readPtr(base.add(OFF_FUNCTIONNAME));
        Address pShort = readPtr(base.add(OFF_SHORTNAME));
        Address pHelp = readPtr(base.add(OFF_HELPSTRING));
        int numParams = mem.getShort(base.add(OFF_NUMPARAMS)) & 0xFFFF;
        Address pParams = readPtr(base.add(OFF_PARAMS));
        Address pExec = readPtr(base.add(OFF_EXECUTEFUNC));
        Address pCompile = readPtr(base.add(OFF_COMPILEFUNC));
        Address pCond = readPtr(base.add(OFF_CONDFUNC));

        pw.println("      functionName  (+0x00) = " + pName + "  \"" + readCStr(pName) + "\"");
        pw.println("      shortName/alias(+0x08)= " + pShort + "  \"" + readCStr(pShort) + "\"");
        pw.println("      helpString    (+0x18) = " + pHelp + "  \"" + readCStr(pHelp) + "\"");
        pw.println("      numParams     (+0x22) = " + numParams);
        pw.println("      params        (+0x28) = " + pParams);
        pw.println("      executeFunc   (+0x30) = " + pExec + "  " + symAt(pExec));
        pw.println("      compileFunc   (+0x38) = " + pCompile + "  " + symAt(pCompile));
        pw.println("      condFunc      (+0x40) = " + pCond + "  " + symAt(pCond));

        // Sanity: functionName should point back at the expected string.
        if (pName != null) {
            String fn = readCStr(pName);
            if (!expectedName.equals(fn)) {
                pw.println("      [WARN] functionName mismatch -- this ref is probably not a table base.");
            }
        }

        // Walk SCRIPT_PARAMETER[] (stride 0x10).
        if (pParams != null && numParams > 0 && numParams < 32) {
            pw.println("      params[]:");
            for (int p = 0; p < numParams; p++) {
                Address pe = pParams.add((long) p * 0x10);
                Address pn = readPtr(pe.add(0x00));
                int ptype = mem.getInt(pe.add(0x08));
                int opt = mem.getByte(pe.add(0x0C)) & 0xFF;
                pw.println("        [" + p + "] name=\"" + readCStr(pn)
                    + "\" type=0x" + Integer.toHexString(ptype) + " optional=" + opt);
            }
        }

        // Decompile the executeFunction.
        if (pExec == null) {
            pw.println("      (executeFunction pointer is null)");
            return;
        }
        Function fn = getFunctionAt(pExec);
        if (fn == null) fn = getFunctionContaining(pExec);
        if (fn == null) {
            pw.println("      (no function at executeFunction target " + pExec + ")");
            return;
        }
        pw.println("      ==== executeFunction " + fn.getName() + " @ " + fn.getEntryPoint()
            + " " + symAt(fn.getEntryPoint()) + " ====");

        // List CALL targets resolved to ID_<n> / named symbols.
        pw.println("      --- call targets ---");
        Set<String> seen = new LinkedHashSet<>();
        InstructionIterator iit = currentProgram.getListing().getInstructions(fn.getBody(), true);
        while (iit.hasNext()) {
            Instruction ins = iit.next();
            String mn = ins.getMnemonicString();
            if (!mn.equals("CALL") && !mn.equals("JMP")) continue;
            Reference[] rs = ins.getReferencesFrom();
            for (Reference rr : rs) {
                if (rr.getReferenceType() == RefType.UNCONDITIONAL_CALL
                    || rr.getReferenceType() == RefType.COMPUTED_CALL
                    || rr.getReferenceType() == RefType.CONDITIONAL_CALL
                    || (mn.equals("JMP") && rr.getReferenceType().isComputed())) {
                    Address t = rr.getToAddress();
                    Function tf = getFunctionAt(t);
                    String label = (tf != null ? tf.getName() : "?") + " @ " + t + " " + symAt(t);
                    if (seen.add(label)) {
                        pw.println("        " + mn + " -> " + label);
                    }
                }
            }
        }

        pw.println("      --- decomp ---");
        DecompileResults res = decomp.decompileFunction(fn, 120, monitor);
        if (res != null && res.getDecompiledFunction() != null) {
            pw.println(res.getDecompiledFunction().getC());
        } else {
            pw.println("      (decompile failed)");
        }
        pw.println("      ==== /executeFunction ====");
    }

    // Read an 8-byte little-endian pointer; return null if it doesn't map.
    private Address readPtr(Address at) {
        try {
            long p = mem.getLong(at);
            if (p == 0) return null;
            return toAddr(p);
        } catch (Exception e) {
            return null;
        }
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
        } catch (Exception e) {
            return sb.toString() + "<unmapped>";
        }
        return sb.toString();
    }

    // Best symbol label at an address: prefer an ID_<n> alias if one exists.
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
        if (other != null) {
            if (idSym != null) sb.append(" / ");
            sb.append(other);
        }
        sb.append("]");
        return sb.toString();
    }
}
