// Decompila a função que contém cada endereço e lista os chamadores. args: <pasta> <addr hex>[,<addr hex>...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class DecompAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        try (PrintWriter idx = new PrintWriter(new File(out, "decomp_index.txt"))) {
            for (String s : String.join(",", Arrays.copyOfRange(a, 1, a.length)).split(",")) {
                Function f = getFunctionContaining(toAddr(Long.parseLong(s, 16)));
                if (f == null) { idx.println(s + ": sem função"); continue; }
                idx.println(s + " -> " + f.getName() + " @ " + f.getEntryPoint());
                Set<String> callers = new TreeSet<>();
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    Function c = getFunctionContaining(r.getFromAddress());
                    callers.add((c == null ? "?" : c.getEntryPoint().toString()) + " (" + r.getFromAddress() + ")");
                }
                for (String c : callers) idx.println("    chamador " + c);
                DecompileResults r = di.decompileFunction(f, 90, monitor);
                if (r.decompileCompleted())
                    try (PrintWriter w = new PrintWriter(new File(out, "f_" + f.getEntryPoint() + ".c"))) { w.println(r.getDecompiledFunction().getC()); }
            }
        }
    }
}
