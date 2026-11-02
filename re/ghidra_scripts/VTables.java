// Acha vtables cujo símbolo/namespace casa com o regex e decompila todos os métodos.
// args: <pasta de saída> <regex de namespace/nome>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class VTables extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        Pattern p = Pattern.compile(a[1], Pattern.CASE_INSENSITIVE);
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        try (PrintWriter idx = new PrintWriter(new File(out, "vtables.txt"))) {
            for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false)) {
                String full = s.getName(true);
                if (!full.contains("vftable") || !p.matcher(full).find()) continue;
                idx.println("== " + full + " @ " + s.getAddress());
                Address at = s.getAddress();
                for (int i = 0; i < 400; i++) {
                    long v;
                    try { v = mem.getLong(at); } catch (MemoryAccessException e) { break; }
                    Address t = toAddr(v);
                    if (t == null || !mem.contains(t)) break;
                    Function f = getFunctionAt(t);
                    if (f == null) break;
                    idx.println(String.format("  [%d] %s", i, f.getEntryPoint()));
                    DecompileResults r = di.decompileFunction(f, 60, monitor);
                    if (r.decompileCompleted()) {
                        try (PrintWriter w = new PrintWriter(new File(out, full.replaceAll("[^A-Za-z0-9]", "_") + "_" + i + "_" + f.getEntryPoint() + ".c"))) {
                            w.println(r.getDecompiledFunction().getC());
                        }
                    }
                    at = at.add(8);
                }
            }
        }
    }
}
