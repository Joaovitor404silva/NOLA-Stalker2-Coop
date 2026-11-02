// Procura strings (ASCII e UTF-16) que casam com os padrões e decompila as funções que as referenciam.
// args: <pasta de saída> <regex> [maxFuncs]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.program.util.DefinedStringIterator;
import java.io.*;
import java.util.*;
import java.util.regex.*;

public class FindStrings extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        File out = new File(a[0]); out.mkdirs();
        Pattern p = Pattern.compile(a[1], Pattern.CASE_INSENSITIVE);
        int max = a.length > 2 ? Integer.parseInt(a[2]) : 40;
        DecompInterface di = new DecompInterface(); di.openProgram(currentProgram);
        Set<Function> funcs = new LinkedHashSet<>();
        try (PrintWriter idx = new PrintWriter(new File(out, "strings.txt"))) {
            for (Data d : DefinedStringIterator.forProgram(currentProgram)) {
                String s = String.valueOf(d.getValue());
                if (!p.matcher(s).find()) continue;
                idx.println(d.getAddress() + "  \"" + s + "\"");
                for (Reference r : getReferencesTo(d.getAddress())) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    idx.println("    xref " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
                    if (f != null) funcs.add(f);
                }
            }
        }
        int n = 0;
        for (Function f : funcs) {
            if (n++ >= max) break;
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            if (!r.decompileCompleted()) continue;
            try (PrintWriter w = new PrintWriter(new File(out, f.getEntryPoint() + ".c"))) { w.println(r.getDecompiledFunction().getC()); }
        }
        println("funcoes decompiladas: " + Math.min(n, funcs.size()) + " de " + funcs.size());
    }
}
