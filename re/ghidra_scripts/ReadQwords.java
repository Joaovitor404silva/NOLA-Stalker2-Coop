// Prints N qwords from an address (index, offset, value, containing function / symbol). No decompile. Read-only.
// args: <outfile> <addr hex> <N>
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;

public class ReadQwords extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Address at = toAddr(Long.parseLong(a[1], 16));
        int n = Integer.parseInt(a[2]);
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            for (int i = 0; i < n; i++) {
                Address p = at.add(8L * i);
                long v = getLong(p);
                String what = "";
                try {
                    Address t = toAddr(v);
                    Function f = getFunctionAt(t);
                    Symbol s = getSymbolAt(t);
                    what = f != null ? "func " + f.getName() : (s != null ? s.getName(true) : "");
                } catch (Exception e) { }
                Symbol here = getSymbolAt(p);
                w.println(p + "\t+0x" + Long.toHexString(8L * i) + "\t" + Long.toHexString(v) + "\t" + what + (here != null ? "\t<" + here.getName(true) + ">" : ""));
            }
        }
    }
}
