// Lê N quadwords a partir de um endereço, mostra para onde apontam e decompila ponteiros de função.
// args: <pasta de saída> <endereço hex> <N>
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;

public class DumpAt extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        Address at = toAddr(Long.parseLong(a[1], 16));
        int n = Integer.parseInt(a[2]);
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        try (PrintWriter w = new PrintWriter(new File(out, "dump_" + a[1] + ".txt"))) {
            for (int i = 0; i < n; i++, at = at.add(8)) {
                long v = mem.getLong(at);
                Address t = toAddr(v);
                String d = "";
                if (t != null && mem.contains(t)) {
                    Function f = getFunctionAt(t);
                    if (f != null) {
                        d = "FUNC " + f.getName();
                        DecompileResults r = di.decompileFunction(f, 60, monitor);
                        if (r.decompileCompleted())
                            try (PrintWriter c = new PrintWriter(new File(out, "f_" + f.getEntryPoint() + ".c"))) { c.println(r.getDecompiledFunction().getC()); }
                    } else {
                        Data dd = getDataAt(t);
                        d = "DATA " + (dd == null ? "?" : dd.getDataType().getName() + " " + dd.getDefaultValueRepresentation());
                    }
                }
                w.println(String.format("%s: %016x  %s", at, v, d));
            }
        }
    }
}
