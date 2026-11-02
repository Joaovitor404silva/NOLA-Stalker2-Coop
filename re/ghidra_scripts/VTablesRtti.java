// Vtables via RTTI MSVC x64: TypeDescriptor -> CompleteObjectLocator -> vtable. Decompila os métodos.
// args: <pasta de saída> <endereço do TypeDescriptor, hex>[,<outro>...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class VTablesRtti extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        long base = currentProgram.getImageBase().getOffset();
        try (PrintWriter idx = new PrintWriter(new File(out, "vtables.txt"))) {
            for (String ts : a[1].split(",")) {
                long td = Long.parseLong(ts, 16);
                int rva = (int) (td - base);
                idx.println("== TypeDescriptor " + ts + " rva=" + Integer.toHexString(rva));
                for (ghidra.program.model.symbol.Reference rf : getReferencesTo(toAddr(td)))
                    idx.println("  ref " + rf.getFromAddress() + " " + rf.getReferenceType());
                idx.println("  dado em td: " + getDataAt(toAddr(td)) + " ; blocos: " + mem.getBlocks().length);
                byte[] pat =new byte[]{(byte) rva, (byte) (rva >> 8), (byte) (rva >> 16), (byte) (rva >> 24)};
                Address s = currentProgram.getMinAddress();
                while ((s = mem.findBytes(s, pat, null, true, monitor)) != null) {
                    Address col = s.subtract(12);
                    idx.println("  hit " + s + " sig=" + mem.getInt(col) + " self=" + Integer.toHexString(mem.getInt(col.add(20))));
                    // COL.signature==1 em x64 e pSelf RVA em +20 devem apontar para o próprio COL
                    int sig = mem.getInt(col), self = mem.getInt(col.add(20));
                    if (sig == 1 && (base + (self & 0xffffffffL)) == col.getOffset()) {
                        idx.println("  COL " + col + " offset=" + mem.getInt(col.add(4)));
                        byte[] cp = new byte[8]; long co = col.getOffset();
                        for (int i = 0; i < 8; i++) cp[i] = (byte) (co >> (8 * i));
                        Address v = currentProgram.getMinAddress();
                        while ((v = mem.findBytes(v, cp, null, true, monitor)) != null) {
                            Address vt = v.add(8);
                            idx.println("    vtable " + vt);
                            Address at = vt;
                            for (int i = 0; i < 300; i++) {
                                Address t;
                                try { t = toAddr(mem.getLong(at)); } catch (Exception e) { break; }
                                if (t == null || !mem.contains(t)) break;
                                Function f = getFunctionAt(t);
                                if (f == null) break;
                                idx.println(String.format("      [%d] %s", i, f.getEntryPoint()));
                                DecompileResults r = di.decompileFunction(f, 60, monitor);
                                if (r.decompileCompleted())
                                    try (PrintWriter w = new PrintWriter(new File(out, "vt" + vt + "_" + i + "_" + f.getEntryPoint() + ".c"))) { w.println(r.getDecompiledFunction().getC()); }
                                at = at.add(8);
                            }
                            v = v.add(1);
                        }
                    }
                    s = s.add(1);
                }
            }
        }
    }
}
