// Lists every instruction that WRITES memory at [reg+disp] (MOV/AND/OR/... with the memory operand first),
// optionally only inside functions that also call one of the given addresses. Read-only.
// args: <outfile> <disp hex>[,<disp hex>...] [c<callee hex>,...]  (args may be split; 'c' prefix = only functions calling it)
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class FindOffsetWrites extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        Set<Long> disps = new HashSet<>(); List<String> cal = new ArrayList<>();
        for (int i = 1; i < a.length; i++) for (String s : a[i].split(",")) {
            if (s.startsWith("c")) cal.add(s.substring(1)); else disps.add(Long.parseLong(s, 16)); }
        Set<Function> only = null;
        if (!cal.isEmpty()) {
            only = new HashSet<>();
            for (String s : cal) {
                Address t = toAddr(Long.parseLong(s, 16));
                for (Reference r : getReferencesTo(t)) {
                    Function f = getFunctionContaining(r.getFromAddress());
                    if (f != null) only.add(f);
                }
            }
        }
        Listing L = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(new File(a[0]))) {
            InstructionIterator it = L.getInstructions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Instruction ins = it.next();
                if (ins.getNumOperands() < 1) continue;
                String m = ins.getMnemonicString();
                if (m.startsWith("CMP") || m.startsWith("TEST") || m.startsWith("LEA") || m.startsWith("PUSH")) continue;
                String op0 = ins.getDefaultOperandRepresentation(0);
                if (!op0.contains("[")) continue;
                long disp = -1;
                for (Object o : ins.getOpObjects(0)) if (o instanceof Scalar) disp = ((Scalar) o).getUnsignedValue();
                if (!disps.contains(disp)) continue;
                Function f = getFunctionContaining(ins.getAddress());
                if (only != null && (f == null || !only.contains(f))) continue;
                String src = ins.getNumOperands() > 1 ? ins.getDefaultOperandRepresentation(1) : "";
                w.println(ins.getAddress() + "\t" + (f == null ? "?" : f.getEntryPoint().toString()) + "\t" + m + " " + op0 + ", " + src);
            }
        }
    }
}
