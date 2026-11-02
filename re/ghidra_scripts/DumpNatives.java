// Dump every UE FNameNativePtrPair {const char* name, exec*} in non-exec initialized memory.
// Output TSV: pair_addr  name  exec  callees(exec)   (the impl is usually the one non-helper callee)
// args: <outfile>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import java.io.*;
import java.util.*;

public class DumpNatives extends GhidraScript {
    Memory mem;
    boolean isExec(long v) { try { MemoryBlock b = mem.getBlock(toAddr(v)); return b != null && b.isExecute(); } catch (Exception e) { return false; } }
    String asciiAt(long v) {
        try {
            Address a = toAddr(v); MemoryBlock b = mem.getBlock(a);
            if (b == null || b.isExecute() || !b.isInitialized()) return null;
            StringBuilder sb = new StringBuilder();
            for (int i = 0; i < 128; i++) {
                int c = mem.getByte(a.add(i)) & 0xff;
                if (c == 0) break;
                if (!(Character.isLetterOrDigit(c) || c == '_')) return null;
                sb.append((char) c);
            }
            return sb.length() >= 2 ? sb.toString() : null;
        } catch (Exception e) { return null; }
    }
    @Override
    public void run() throws Exception {
        mem = currentProgram.getMemory();
        FunctionManager fm = currentProgram.getFunctionManager();
        int n = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(getScriptArgs()[0]))) {
            for (MemoryBlock b : mem.getBlocks()) {
                if (b.isExecute() || !b.isInitialized()) continue;
                long start = (b.getStart().getOffset() + 7) & ~7L, end = b.getEnd().getOffset() - 16;
                byte[] buf = new byte[(int) (end - start + 16)];
                mem.getBytes(toAddr(start), buf);
                java.nio.ByteBuffer bb = java.nio.ByteBuffer.wrap(buf).order(java.nio.ByteOrder.LITTLE_ENDIAN);
                for (long off = 0; off + 16 <= buf.length; off += 8) {
                    long q0 = bb.getLong((int) off), q1 = bb.getLong((int) off + 8);
                    if (q0 < 0x140000000L || q0 > 0x150000000L || q1 < 0x140000000L || q1 > 0x150000000L) continue;
                    if (!isExec(q1)) continue;
                    Function f = fm.getFunctionAt(toAddr(q1));
                    if (f == null) continue;
                    String s = asciiAt(q0);
                    if (s == null) continue;
                    StringBuilder cs = new StringBuilder();
                    for (Function c : f.getCalledFunctions(monitor)) cs.append(c.getEntryPoint()).append(' ');
                    w.println(toAddr(start + off) + "\t" + s + "\t" + toAddr(q1) + "\t" + cs.toString().trim());
                    n++;
                }
            }
        }
        println("natives: " + n);
    }
}
