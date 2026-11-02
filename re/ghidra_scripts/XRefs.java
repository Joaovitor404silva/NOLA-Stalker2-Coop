// Lista as funções que referenciam cada endereço dado. args: <arquivo de saída> <addr hex>[,<addr hex>...]
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class XRefs extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            for (String s : a[1].split(",")) {
                Address at = toAddr(Long.parseLong(s, 16));
                Set<String> fs = new TreeSet<>();
                int n = 0;
                for (Reference r : getReferencesTo(at)) {
                    n++;
                    Function f = getFunctionContaining(r.getFromAddress());
                    fs.add((f == null ? "?" : f.getEntryPoint().toString()) + " " + r.getReferenceType());
                }
                w.println("== " + s + ": " + n + " refs, " + fs.size() + " funcoes");
                for (String f : fs) w.println("   " + f);
            }
        }
    }
}
