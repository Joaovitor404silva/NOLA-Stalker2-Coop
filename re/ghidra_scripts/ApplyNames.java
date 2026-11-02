// Idempotent: applies names TSVs (rva<TAB>kind<TAB>name<TAB>source, rva hex from image base) to the program.
// args: <out_summary.txt> <file1.tsv> [file2.tsv ...]   later files override earlier ones for the same rva.
// "A::B::f" -> namespace A::B, function f. Function-like kinds (or an existing function at the address) rename the
// function (creating it if missing); other kinds set the primary label. Unchanged names are skipped.
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.model.mem.*;
import ghidra.app.util.NamespaceUtils;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        long base = currentProgram.getImageBase().getOffset();
        LinkedHashMap<Long, String[]> m = new LinkedHashMap<>();
        for (int i = 1; i < a.length; i++) {
            File f = new File(a[i]); if (!f.exists()) { println("missing " + f); continue; }
            try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), StandardCharsets.UTF_8))) {
                String l;
                while ((l = r.readLine()) != null) {
                    String[] p = l.split("\t");
                    if (p.length < 3 || p[0].equals("rva") || p[2].isBlank()) continue;
                    try { m.put(Long.parseLong(p[0].trim().replaceFirst("^0x", ""), 16), p); } catch (NumberFormatException e) {}
                }
            }
        }
        SymbolTable st = currentProgram.getSymbolTable();
        FunctionManager fm = currentProgram.getFunctionManager();
        Memory mem = currentProgram.getMemory();
        Namespace global = currentProgram.getGlobalNamespace();
        int ren = 0, same = 0, created = 0, labels = 0, bad = 0;
        try (PrintWriter log = new PrintWriter(new OutputStreamWriter(new FileOutputStream(a[0]), StandardCharsets.UTF_8))) {
            for (Map.Entry<Long, String[]> e : m.entrySet()) {
                if (monitor.isCancelled()) break;
                String kind = e.getValue()[1].toLowerCase(), full = e.getValue()[2].trim();
                Address ad = toAddr(base + e.getKey());
                MemoryBlock blk = mem.getBlock(ad);
                if (blk == null) { bad++; log.println("nomem\t" + Long.toHexString(e.getKey()) + "\t" + full); continue; }
                int k = full.lastIndexOf("::");
                String nsPath = k > 0 ? full.substring(0, k) : null;
                String nm = SymbolUtilities.replaceInvalidChars(k > 0 ? full.substring(k + 2) : full, true);
                if (nm.isEmpty()) { bad++; continue; }
                try {
                    Namespace ns = nsPath == null ? global
                        : NamespaceUtils.createNamespaceHierarchy(SymbolUtilities.replaceInvalidChars(nsPath, true).replace("_:_:", "::"), global, currentProgram, SourceType.IMPORTED);
                    Function f = fm.getFunctionAt(ad);
                    boolean funcKind = kind.matches(".*(func|impl|exec|vf|method|thunk|ctor|dtor|getter|setter).*");
                    if (funcKind && !blk.isExecute()) { bad++; log.println("notcode\t" + Long.toHexString(e.getKey()) + "\t" + full); continue; }
                    if (f == null && funcKind && blk.isExecute()) {
                        if (currentProgram.getListing().getInstructionAt(ad) == null) new DisassembleCommand(ad, null, true).applyTo(currentProgram, monitor);
                        f = createFunction(ad, null);
                        if (f != null) created++;
                    }
                    if (f != null) {
                        if (f.getName().equals(nm) && f.getParentNamespace().equals(ns)) { same++; continue; }
                        f.setParentNamespace(ns); f.setName(nm, SourceType.IMPORTED); ren++;
                    } else {
                        Symbol s = st.getPrimarySymbol(ad);
                        if (s != null && s.getName().equals(nm) && s.getParentNamespace().equals(ns)) { same++; continue; }
                        Symbol n = st.createLabel(ad, nm, ns, SourceType.IMPORTED);
                        n.setPrimary(); labels++;
                    }
                } catch (Exception ex) { bad++; log.println("err\t" + Long.toHexString(e.getKey()) + "\t" + full + "\t" + ex.getMessage()); }
            }
            String sum = "entries=" + m.size() + " renamed=" + ren + " unchanged=" + same + " created=" + created + " labels=" + labels + " bad=" + bad;
            log.println(sum); println(sum);
        }
    }
}
