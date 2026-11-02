import ghidra.app.script.GhidraScript;
import ghidra.app.util.NamespaceUtils;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.*;

public class ApplySdk extends GhidraScript {
    static class P { String name, type, extra; int off, elem, dim; }
    static class S { String name, kind, sup; int size; List<P> props = new ArrayList<>(); }

    Map<String, S> structs = new LinkedHashMap<>();
    Map<String, Structure> dts = new HashMap<>();
    DataTypeManager dtm;
    CategoryPath cat = CategoryPath.ROOT;
    Structure tArray, fString, fName;

    String clean(String s) { return SymbolUtilities.replaceInvalidChars(s, true); }

    Structure mk(String name, int size) {
        StructureDataType sd = new StructureDataType(cat, name, size, dtm);
        return (Structure) dtm.addDataType(sd, DataTypeConflictHandler.REPLACE_HANDLER);
    }

    DataType uint(int n) {
        switch (n) { case 1: return ByteDataType.dataType; case 2: return WordDataType.dataType; case 4: return DWordDataType.dataType; case 8: return QWordDataType.dataType; }
        return null;
    }

    DataType typeOf(P p) {
        String t = p.type, x = p.extra == null ? "" : p.extra;
        switch (t) {
            case "FloatProperty": return FloatDataType.dataType;
            case "DoubleProperty": return DoubleDataType.dataType;
            case "Int8Property": return SignedByteDataType.dataType;
            case "Int16Property": return ShortDataType.dataType;
            case "IntProperty": return IntegerDataType.dataType;
            case "Int64Property": return LongLongDataType.dataType;
            case "ByteProperty": case "BoolProperty": return ByteDataType.dataType;
            case "UInt16Property": return UnsignedShortDataType.dataType;
            case "UInt32Property": return UnsignedIntegerDataType.dataType;
            case "UInt64Property": return UnsignedLongLongDataType.dataType;
            case "EnumProperty": return uint(p.elem);
            case "NameProperty": return fName;
            case "StrProperty": return fString;
            case "ArrayProperty": return tArray;
            case "ObjectProperty": case "ClassProperty": {
                Structure s = dts.get(x);
                return new PointerDataType(s != null ? s : VoidDataType.dataType, dtm);
            }
            case "StructProperty": { Structure s = dts.get(x); if (s != null && s.getLength() == p.elem) return s; return null; }
        }
        return null;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        dtm = currentProgram.getDataTypeManager();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(a[0]), StandardCharsets.UTF_8))) {
            String l; r.readLine();
            while ((l = r.readLine()) != null) {
                String[] c = l.split("\t", -1);
                if (c.length < 11) continue;
                if (c[0].equals("S")) {
                    if (structs.containsKey(c[1])) continue;
                    S s = new S(); s.name = c[1]; s.kind = c[2]; s.sup = c[3]; s.size = Integer.decode(c[4]); structs.put(s.name, s);
                } else {
                    S s = structs.get(c[1]); if (s == null) continue;
                    P p = new P(); p.name = c[5]; p.type = c[6]; p.off = Integer.decode(c[7]); p.elem = Integer.parseInt(c[8]); p.dim = Integer.parseInt(c[9]); p.extra = c[10];
                    s.props.add(p);
                }
            }
        }
        int tx = currentProgram.startTransaction("ApplySdk");
        int fields = 0, skipped = 0, classes = 0, thisSet = 0;
        try {
            fName = mk("FName", 8); fName.replaceAtOffset(0, IntegerDataType.dataType, 4, "ComparisonIndex", null); fName.replaceAtOffset(4, IntegerDataType.dataType, 4, "Number", null);
            tArray = mk("TArray", 16); tArray.replaceAtOffset(0, new PointerDataType(VoidDataType.dataType, dtm), 8, "Data", null); tArray.replaceAtOffset(8, IntegerDataType.dataType, 4, "Num", null); tArray.replaceAtOffset(12, IntegerDataType.dataType, 4, "Max", null);
            fString = mk("FString", 16); fString.replaceAtOffset(0, new PointerDataType(WideChar16DataType.dataType, dtm), 8, "Data", null); fString.replaceAtOffset(8, IntegerDataType.dataType, 4, "Num", null); fString.replaceAtOffset(12, IntegerDataType.dataType, 4, "Max", null);
            for (S s : structs.values()) if (s.size > 0 && !s.name.equals("FName")) dts.put(s.name, mk(clean(s.name), s.size));
            List<S> order = new ArrayList<>(); Set<String> done = new HashSet<>();
            for (S s : structs.values()) visit(s, order, done, new HashSet<>());
            for (S s : order) {
                if (monitor.isCancelled()) break;
                Structure st = dts.get(s.name); if (st == null) continue;
                List<P> all = new ArrayList<>();
                for (S c = s; c != null; c = structs.get(c.sup)) { all.addAll(c.props); if (all.size() > 20000) break; }
                all.sort(Comparator.comparingInt(p -> p.off));
                int end = 0;
                for (P p : all) {
                    int len = p.elem * Math.max(1, p.dim);
                    if (p.off < end || p.off + len > s.size || len <= 0) { skipped++; continue; }
                    DataType dt = typeOf(p);
                    if (dt == null || dt.getLength() != p.elem) dt = new ArrayDataType(ByteDataType.dataType, p.elem, 1, dtm);
                    if (p.dim > 1) dt = new ArrayDataType(dt, p.dim, dt.getLength(), dtm);
                    try { st.replaceAtOffset(p.off, dt, len, clean(p.name), null); fields++; end = p.off + len; } catch (Exception e) { skipped++; }
                }
            }
            SymbolTable symt = currentProgram.getSymbolTable();
            FunctionManager fm = currentProgram.getFunctionManager();
            for (Namespace ns : listTopNamespaces(symt)) {
                Structure st = dts.get(ns.getName()); if (st == null) continue;
                GhidraClass gc = ns instanceof GhidraClass ? (GhidraClass) ns : NamespaceUtils.convertNamespaceToClass(ns);
                classes++;
                for (Symbol sym : symt.getSymbols(gc)) {
                    if (sym.getSymbolType() != SymbolType.FUNCTION) continue;
                    Function f = fm.getFunctionAt(sym.getAddress()); if (f == null) continue;
                    try { if (!"__thiscall".equals(f.getCallingConventionName())) { f.setCallingConvention("__thiscall"); thisSet++; } } catch (Exception e) { }
                }
            }
        } finally { currentProgram.endTransaction(tx, true); }
        println("ApplySdk structs=" + dts.size() + " fields=" + fields + " skipped=" + skipped + " classes=" + classes + " thiscall=" + thisSet);
    }

    List<Namespace> listTopNamespaces(SymbolTable symt) {
        List<Namespace> r = new ArrayList<>();
        for (Symbol s : symt.getChildren(currentProgram.getGlobalNamespace().getSymbol())) {
            if (s.getSymbolType() == SymbolType.NAMESPACE || s.getSymbolType() == SymbolType.CLASS) r.add((Namespace) s.getObject());
        }
        return r;
    }

    void visit(S s, List<S> order, Set<String> done, Set<String> path) {
        if (done.contains(s.name) || path.contains(s.name)) return;
        path.add(s.name);
        for (P p : s.props) if (p.type.equals("StructProperty")) { S d = structs.get(p.extra); if (d != null) visit(d, order, done, path); }
        S sp = structs.get(s.sup); if (sp != null) for (S c = sp; c != null && !done.contains(c.name); c = structs.get(c.sup)) for (P p : c.props) if (p.type.equals("StructProperty")) { S d = structs.get(p.extra); if (d != null) visit(d, order, done, path); }
        path.remove(s.name); done.add(s.name); order.add(s);
    }
}
