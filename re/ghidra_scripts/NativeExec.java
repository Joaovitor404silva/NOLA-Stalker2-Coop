// UE FNameNativePtrPair lookup: for each UFunction name find the ASCII string, the {const char*, exec*} pairs that point at it,
// the exec thunk, the first non-trivial call inside it (likely the impl) and the callers of that impl.
// args: <outdir> <Name1,Name2,...>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class NativeExec extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        Memory mem = currentProgram.getMemory();
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        try (PrintWriter idx = new PrintWriter(new File(out, "exec_index.txt"))) {
            for (String name : String.join(",", Arrays.copyOfRange(a, 1, a.length)).split(",")) {
                byte[] pat = ("\0" + name + "\0").getBytes("US-ASCII");
                Address s = mem.getMinAddress();
                int hits = 0;
                while (s != null && hits < 6) {
                    Address f = mem.findBytes(s, pat, null, true, monitor);
                    if (f == null) break;
                    Address str = f.add(1);
                    s = f.add(1);
                    // pointers to the string anywhere in initialized memory
                    long v = str.getOffset();
                    byte[] le = new byte[8];
                    for (int i = 0; i < 8; i++) le[i] = (byte) (v >>> (8 * i));
                    Address q = mem.getMinAddress();
                    while (q != null) {
                        Address p = mem.findBytes(q, le, null, true, monitor);
                        if (p == null) break;
                        q = p.add(1);
                        long fn = mem.getLong(p.add(8));
                        Address fa = toAddr(fn);
                        Function ef = getFunctionAt(fa);
                        if (ef == null || mem.getBlock(fa) == null || !mem.getBlock(fa).isExecute()) continue;
                        hits++;
                        idx.println(name + " str=" + str + " pair=" + p + " exec=" + fa);
                        // callees of the exec thunk
                        StringBuilder cs = new StringBuilder();
                        for (Function c : ef.getCalledFunctions(monitor)) cs.append(" ").append(c.getEntryPoint());
                        idx.println("    callees:" + cs);
                        DecompileResults r = di.decompileFunction(ef, 60, monitor);
                        if (r.decompileCompleted())
                            try (PrintWriter w = new PrintWriter(new File(out, "exec_" + name + "_" + fa + ".c"))) { w.println(r.getDecompiledFunction().getC()); }
                    }
                }
                if (hits == 0) idx.println(name + ": no exec pair found");
                idx.flush();
            }
        }
    }
}
