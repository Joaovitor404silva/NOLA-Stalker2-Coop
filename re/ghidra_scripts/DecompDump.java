// Parallel full decompile dump. One DecompInterface per worker thread, work unit = 64 KB RVA bucket.
// args: <outdir> <threads> <timeoutSec> <sampleN (0 = full run)>
// Full run: <outdir>\addr\b_<rva>.c + b_<rva>.idx (rva,name,file,line,size,callers); a bucket whose .idx exists is skipped
// (resumable). Failures -> b_<rva>.fail. Progress -> <outdir>\progress.txt.
// Sample run: every (total/N)-th function, timing only -> <outdir>\sample_<N>.txt.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;

public class DecompDump extends GhidraScript {
    long base;
    ReferenceManager rm;
    int sliceK, sliceN;

    DecompInterface newDi() {
        DecompInterface di = new DecompInterface();
        DecompileOptions o = new DecompileOptions();
        o.grabFromProgram(currentProgram);
        di.setOptions(o);
        di.toggleCCode(true); di.toggleSyntaxTree(false); di.setSimplificationStyle("decompile");
        if (!di.openProgram(currentProgram)) throw new RuntimeException("decompiler open failed: " + di.getLastMessage());
        return di;
    }

    int callers(Function f) {
        int n = 0;
        for (Reference r : rm.getReferencesTo(f.getEntryPoint())) if (r.getReferenceType().isCall()) n++;
        return n;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        int threads = Integer.parseInt(a[1]), tmo = Integer.parseInt(a[2]), sample = Integer.parseInt(a[3]);
        sliceK = a.length > 5 ? Integer.parseInt(a[4]) : 0; sliceN = a.length > 5 ? Integer.parseInt(a[5]) : 1;
        base = currentProgram.getImageBase().getOffset();
        rm = currentProgram.getReferenceManager();
        List<Function> all = new ArrayList<>();
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) if (!f.isExternal()) all.add(f);
        println("functions: " + all.size());
        ThreadLocal<DecompInterface> tl = ThreadLocal.withInitial(this::newDi);
        List<DecompInterface> dis = Collections.synchronizedList(new ArrayList<>());
        ExecutorService ex = Executors.newFixedThreadPool(threads);
        long t0 = System.currentTimeMillis();

        if (sample > 0) {
            int step = Math.max(1, all.size() / sample);
            AtomicInteger ok = new AtomicInteger(), fail = new AtomicInteger(); AtomicLong bytes = new AtomicLong();
            List<Future<?>> fs = new ArrayList<>();
            for (int i = 0; i < all.size() && fs.size() < sample; i += step) {
                Function f = all.get(i);
                fs.add(ex.submit(() -> {
                    DecompInterface di = tl.get(); if (!dis.contains(di)) dis.add(di);
                    DecompileResults r = di.decompileFunction(f, tmo, TaskMonitor.DUMMY);
                    if (r != null && r.decompileCompleted()) { ok.incrementAndGet(); bytes.addAndGet(r.getDecompiledFunction().getC().length()); }
                    else fail.incrementAndGet();
                }));
            }
            for (Future<?> f : fs) f.get();
            double sec = (System.currentTimeMillis() - t0) / 1000.0;
            String s = String.format("sample=%d threads=%d timeout=%ds ok=%d fail=%d sec=%.1f rate=%.1f fn/s avgC=%d B total_functions=%d ETA_full=%.1f h est_size=%.1f GB",
                fs.size(), threads, tmo, ok.get(), fail.get(), sec, fs.size() / sec, ok.get() == 0 ? 0 : bytes.get() / ok.get(), all.size(),
                all.size() / (fs.size() / sec) / 3600.0, ok.get() == 0 ? 0 : (double) bytes.get() / ok.get() * all.size() / 1e9);
            println(s);
            Files.writeString(new File(out, "sample_" + sample + ".txt").toPath(), s + "\n");
        } else {
            File dir = new File(out, "addr"); dir.mkdirs();
            TreeMap<Long, List<Function>> buckets = new TreeMap<>();
            for (Function f : all) buckets.computeIfAbsent((f.getEntryPoint().getOffset() - base) >>> 16, k -> new ArrayList<>()).add(f);
            AtomicInteger doneB = new AtomicInteger(), doneF = new AtomicInteger(), failF = new AtomicInteger(), skipB = new AtomicInteger();
            int totalB = buckets.size();
            List<Future<?>> fs = new ArrayList<>();
            int bi = -1;
            for (Map.Entry<Long, List<Function>> e : buckets.entrySet()) {
                if (++bi % sliceN != sliceK) continue;
                String tag0 = String.format("b_%05x", e.getKey());
                File idx = new File(dir, tag0 + ".idx");
                List<Function> work0 = e.getValue();
                if (idx.exists()) {
                    // incremental: functions created after this bucket was dumped (e.g. by ApplyNames) go to b_<rva>_x
                    Set<String> have = new HashSet<>();
                    for (String l : Files.readAllLines(idx.toPath(), StandardCharsets.UTF_8)) have.add(l.split("\t")[0]);
                    File xi = new File(dir, tag0 + "_x.idx");
                    if (xi.exists()) for (String l : Files.readAllLines(xi.toPath(), StandardCharsets.UTF_8)) have.add(l.split("\t")[0]);
                    work0 = new ArrayList<>();
                    for (Function f : e.getValue()) if (!have.contains(Long.toHexString(f.getEntryPoint().getOffset() - base))) work0.add(f);
                    if (work0.isEmpty()) { skipB.incrementAndGet(); doneB.incrementAndGet(); continue; }
                    tag0 = tag0 + "_x";
                    if (xi.exists()) { // merge: re-dump old _x members plus new ones
                        Set<String> xs = new HashSet<>();
                        for (String l : Files.readAllLines(xi.toPath(), StandardCharsets.UTF_8)) xs.add(l.split("\t")[0]);
                        for (Function f : e.getValue()) if (xs.contains(Long.toHexString(f.getEntryPoint().getOffset() - base))) work0.add(f);
                        work0.sort(Comparator.comparing(Function::getEntryPoint));
                    }
                }
                final String tag = tag0; final List<Function> work = work0;
                fs.add(ex.submit(() -> {
                    DecompInterface di = tl.get(); if (!dis.contains(di)) dis.add(di);
                    StringBuilder c = new StringBuilder(), ix = new StringBuilder(), fl = new StringBuilder();
                    int line = 1;
                    for (Function f : work) {
                        if (monitor.isCancelled()) return null;
                        long rva = f.getEntryPoint().getOffset() - base;
                        String name = f.getName(true);
                        String body;
                        DecompileResults r = null;
                        try { r = di.decompileFunction(f, tmo, TaskMonitor.DUMMY); } catch (Exception x) { }
                        if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) body = r.getDecompiledFunction().getC();
                        else {
                            String why = r == null ? "exception" : String.valueOf(r.getErrorMessage()).replaceAll("[\\r\\n\\t]+", " ");
                            fl.append(String.format("%x\t%s\t%s\n", rva, name, why)); failF.incrementAndGet();
                            body = "/* DECOMPILE FAILED: " + why.replace("*/", "* /") + " */\n";
                            if (r == null || r.isTimedOut() || r.failedToStart()) { try { di.dispose(); } catch (Exception y) {} dis.remove(di); di = newDi(); tl.set(di); dis.add(di); }
                        }
                        String hdr = String.format("// ==== %x %s size=%d callers=%d ====\n", rva, name, f.getBody().getNumAddresses(), callers(f));
                        ix.append(String.format("%x\t%s\taddr/%s.c\t%d\t%d\t%d\n", rva, name, tag, line, f.getBody().getNumAddresses(), callers(f)));
                        c.append(hdr).append(body).append('\n');
                        line += countLines(hdr) + countLines(body) + 1;
                        doneF.incrementAndGet();
                    }
                    Files.writeString(new File(dir, tag + ".c").toPath(), c.toString(), StandardCharsets.UTF_8);
                    if (fl.length() > 0) Files.writeString(new File(dir, tag + ".fail").toPath(), fl.toString(), StandardCharsets.UTF_8);
                    File tmp = new File(dir, tag + ".idx.tmp");
                    Files.writeString(tmp.toPath(), ix.toString(), StandardCharsets.UTF_8);
                    Files.move(tmp.toPath(), new File(dir, tag + ".idx").toPath(), StandardCopyOption.REPLACE_EXISTING);
                    int db = doneB.incrementAndGet();
                    if (db % 5 == 0) progress(out, t0, db, totalB, skipB.get(), doneF.get(), failF.get(), all.size());
                    return null;
                }));
            }
            for (Future<?> f : fs) f.get();
            progress(out, t0, doneB.get(), totalB, skipB.get(), doneF.get(), failF.get(), all.size());
            println("done buckets=" + doneB.get() + "/" + totalB + " functions=" + doneF.get() + " failed=" + failF.get());
        }
        ex.shutdown();
        for (DecompInterface di : dis) try { di.dispose(); } catch (Exception y) {}
    }

    static int countLines(String s) { int n = 0; for (int i = 0; i < s.length(); i++) if (s.charAt(i) == '\n') n++; return n; }

    synchronized void progress(File out, long t0, int db, int tb, int sb, int df, int ff, int total) {
        double sec = (System.currentTimeMillis() - t0) / 1000.0;
        double rate = df / Math.max(1, sec);
        String s = String.format("%tF %<tT buckets %d/%d (skipped as done %d) functions this run %d failed %d rate %.1f fn/s elapsed %.2f h%n",
            new Date(), db, tb, sb, df, ff, rate, sec / 3600);
        try { Files.writeString(new File(out, "progress_" + sliceK + ".txt").toPath(), s, StandardOpenOption.CREATE, StandardOpenOption.APPEND); } catch (IOException e) {}
    }
}
