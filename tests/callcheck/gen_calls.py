import sys, os, re, glob, collections
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from odump import load, DUMP as DUMP_PATH

SRC = os.path.normpath(os.path.join(HERE, "..", "..", "src"))
DATA = os.path.normpath(os.path.join(HERE, "..", "..", "data"))
OUT = os.path.join(HERE, "CALLS.tsv")
D = load()
FUNCS, CLASSES, STRUCTS, BYADDR, ENUMS = D["funcs"], D["classes"], D["structs"], D["byaddr"], D["enums"]
SRCNAME = "UE4SS_ObjectDump.txt (Stalker2 5.5.4, ingame, 2026-10-09)"

MODFILES = {
    "M1 bodies": ["s2m_bodies.cpp"],
    "M2 damage": ["s2m_damage.cpp"],
    "M3b wshot": ["s2m_wshot.cpp", "s2m_aiwatch.cpp", "s2m_console.cpp"],
    "M4 items": ["s2m_items.cpp", "s2m_items_x0.cpp"],
    "M5 world": ["s2m_world_x0.cpp", "s2m_world.cpp"],
    "M6 pop": ["s2m_pop_x0.cpp", "s2m_pop.cpp"],
    "M7 quest": ["s2m_quest.cpp"],
    "M8 lock": ["s2m_lock.cpp"],
    "M9 save": ["s2m_save.cpp", "s2m_save_x0.cpp"],
    "M10 travel": ["s2m_travel_x0.cpp", "s2m_travel.cpp"],
    "M11 session": ["s2m_session_x0.cpp", "s2m_session.cpp"],
    "R1 relay": ["s2r_net.cpp", "s2r_x0.cpp"],
}

ALIAS = {"W_DialogueView_C": "DialogueViewFolder"}
short = collections.defaultdict(list)
for p in CLASSES:
    short[p.rsplit(".", 1)[-1]].append(p)
for p in STRUCTS:
    short.setdefault(p.rsplit(".", 1)[-1], []).append(p)

for _a, _b in ALIAS.items():
    short[_a] = list(short[_b])

owner_funcs = collections.defaultdict(dict)
for p, f in FUNCS.items():
    o, _, n = p.rpartition(":")
    owner_funcs[o][n] = f


def chain(path):
    out = []
    seen = set()
    while path and path not in seen:
        seen.add(path)
        out.append(path)
        c = CLASSES.get(path)
        if not c or not c["sps"]:
            break
        path = BYADDR.get(c["sps"])
    return out


def sname(p):
    return p.rsplit(".", 1)[-1]


def find_fn(cls, fn):
    for cp in short.get(cls, []):
        if cp not in CLASSES:
            continue
        for q in chain(cp):
            if fn in owner_funcs.get(q, {}):
                return q, owner_funcs[q][fn]
    return None, None


def find_fn_any(fn):
    r = []
    for o, m in owner_funcs.items():
        if fn in m:
            r.append((o, m[fn]))
    return r


def find_prop(cls_paths, name):
    for cp in cls_paths:
        for q in chain(cp):
            c = CLASSES.get(q)
            if not c:
                continue
            for pr in c["props"]:
                if pr[0] == name:
                    return q, pr
    return None, None


def struct_prop(sp, name):
    s = STRUCTS.get(sp)
    cur = sp
    while s:
        for pr in s["props"]:
            if pr[0] == name:
                return pr
        break
    return None


KSIZE = {"BoolProperty": 1, "ByteProperty": 1, "Int8Property": 1, "Int16Property": 2, "UInt16Property": 2, "IntProperty": 4,
         "UInt32Property": 4, "FloatProperty": 4, "Int64Property": 8, "UInt64Property": 8, "DoubleProperty": 8, "ObjectProperty": 8,
         "ClassProperty": 8, "NameProperty": 8, "StrProperty": 16, "ArrayProperty": 16, "WeakObjectProperty": 8, "TextProperty": 24,
         "InterfaceProperty": 16, "SoftObjectProperty": 24, "MapProperty": 80, "SetProperty": 80, "LazyObjectProperty": 28, "ObjectPtrProperty": 8}


def struct_chain(sp):
    out = []
    while sp and sp in STRUCTS and sp not in out:
        out.append(sp)
        a = STRUCTS[sp].get("sps")
        sp = BYADDR.get(a) if a else None
    return out


def struct_member(sp, name):
    for q in struct_chain(sp):
        for x in STRUCTS[q]["props"]:
            if x[0] == name:
                return x
    return None


def struct_size(sp):
    best = None
    for q in struct_chain(sp):
        s = STRUCTS[q]
        if not s["props"]:
            continue
        pr = sorted(s["props"], key=lambda x: x[2] or 0)
        last = pr[-1]
        sz = KSIZE.get(last[1])
        if last[1] == "StructProperty":
            sz = struct_size(BYADDR.get(last[3])) if last[3] else None
        if sz is not None:
            return (last[2] or 0) + sz
    return None


def pdesc(pr):
    n, k, o, x = pr
    t = k[:-8]
    if x and x in BYADDR:
        t += "<" + sname(BYADDR[x]) + ">"
    return t


def psize(fnrec, idx):
    n, k, o, x = fnrec["params"][idx]
    if k == "StructProperty" and x in BYADDR:
        return struct_size(BYADDR[x])
    if k == "EnumProperty":
        en = ENUMS.get(BYADDR.get(x, ""), [])
        mx = max([v for _, v in en] or [0])
        nxt = fnrec["params"][idx + 1][2] if idx + 1 < len(fnrec["params"]) else None
        if nxt is not None and o is not None:
            return nxt - o if nxt - o in (1, 2, 4, 8) else None
        return 1 if mx < 256 else 4 if mx < 2 ** 32 else 8
    s = KSIZE.get(k)
    return s


BUILD_OK = {
    "i": {"ByteProperty", "EnumProperty", "IntProperty", "UInt32Property", "Int64Property", "UInt64Property", "Int16Property", "UInt16Property", "Int8Property"},
    "f": {"FloatProperty"},
    "d": {"DoubleProperty", "FloatProperty"},
    "b": {"BoolProperty", "ByteProperty"},
    "o": {"ObjectProperty", "ClassProperty", "ObjectPtrProperty"},
    "n": {"NameProperty"},
    "s": {"StrProperty"},
}
READ_OK = {
    "rb": BUILD_OK["b"],
    "ri": BUILD_OK["i"] | {"BoolProperty"},
    "rf": {"FloatProperty", "DoubleProperty"},
    "rd": {"FloatProperty", "DoubleProperty"},
    "ro": BUILD_OK["o"],
    "rs": {"StrProperty"},
}
STRUCT_FOR = {"v": "Vector", "r": "Rotator"}
RSTRUCT_FOR = {"rv": "Vector", "rr": "Rotator"}


def check_param(fnrec, name, how):
    path = name.split(".")
    base = None
    for i, pr in enumerate(fnrec["params"]):
        if pr[0] == path[0]:
            base = i
            break
    if base is None:
        return "WRONG", "no parameter '%s' (params: %s)" % (path[0], ", ".join(p[0] for p in fnrec["params"]) or "none")
    pr = fnrec["params"][base]
    cur = pr
    for seg in path[1:]:
        if cur[1] != "StructProperty" or cur[3] not in BYADDR:
            return "WRONG", "'%s' is not a struct" % cur[0]
        sp = BYADDR[cur[3]]
        found = None
        found = struct_member(sp, seg)
        if not found:
            return "WRONG", "struct %s has no member '%s'" % (sname(sp), seg)
        cur = found
    kind = cur[1]
    desc = pdesc(cur)
    size = psize(fnrec, base) if len(path) == 1 else (KSIZE.get(kind) if kind != "StructProperty" else (struct_size(BYADDR.get(cur[3])) if cur[3] in BYADDR else None))
    if how in BUILD_OK or how in READ_OK:
        ok = (BUILD_OK.get(how) or READ_OK.get(how))
        if kind not in ok:
            return "WRONG", "%s builder .%s does not fit %s" % (name, how, desc)
        if how == "f" and size not in (4, None):
            return "WRONG", "float builder needs size 4, real %s" % size
        if how == "o" and size not in (8, None):
            return "WRONG", "object builder needs size 8, real %s" % size
        return "OK", "%s size %s" % (desc, size)
    if how in STRUCT_FOR or how in RSTRUCT_FOR:
        want = STRUCT_FOR.get(how) or RSTRUCT_FOR.get(how)
        if kind != "StructProperty":
            return "WRONG", "builder .%s needs struct %s, real %s" % (how, want, desc)
        sp = BYADDR.get(cur[3], "")
        sz = struct_size(sp)
        if sz != 24:
            return "WRONG", "builder .%s needs a 24-byte struct, real %s size %s" % (how, desc, sz)
        if want == "Vector" and not (sname(sp).startswith("Vector") or sname(sp) in ("Vector", "Vector_NetQuantize", "Vector3d")):
            return "WRONG", "want Vector, real %s" % desc
        if want == "Rotator" and sname(sp) != "Rotator":
            return "WRONG", "want Rotator, real %s" % desc
        return "OK", "%s size %s" % (desc, sz)
    return "OK", desc


def cls_of_prop(pr):
    n, k, o, x = pr
    if x and x in BYADDR:
        return sname(BYADDR[x])
    return None


FIRST_RE = re.compile(r'(?:first_of|FindFirstOf)\(L"([^"]+)"\)')
ALL_RE = re.compile(r'all_of\(L"([^"]+)"')
PATH_RE = re.compile(r'find_path\(L"([^"]+)"\)')
COBJ_RE = re.compile(r'call_o\((\w+),\s*L"(\w+)"\)')
PROPREF_RE = re.compile(r'prop<(?:x0::)?Ref>\((\w+),\s*"([\w.]+)",\s*(\w+)\)')

HINTS = {}
CUR = [""]


def resolve_var(lines, idx, var, depth=0):
    if depth > 4:
        return None
    lo = max(0, idx - 80)
    for j in range(idx, lo - 1, -1):
        ln = lines[j]
        if not re.search(r'\b' + re.escape(var) + r'\b', ln):
            continue
        m = re.search(r'(?:Ref|auto&?|const Ref&|const auto&)\s+(?:\w+\s*,\s*)*' + re.escape(var) + r'\b[^;]*?=\s*(?:x0::)?first_of\(L"([^"]+)"\)', ln)
        if m:
            return m.group(1)
        m = re.search(r'\b' + re.escape(var) + r'\b\s*=\s*(?:x0::)?first_of\(L"([^"]+)"\)', ln)
        if m:
            return m.group(1)
        m = re.search(r'\b' + re.escape(var) + r'\b[^;]*?=\s*(?:x0::)?find_path\(L"[^"]*\.Default__(\w+)"\)', ln)
        if m:
            return m.group(1)
        m = re.search(r'(?:Ref|auto&?|const Ref&)\s+' + re.escape(var) + r'\s*=\s*(?:\w+::)?mediator\(\)', ln)
        if m:
            return "CppMediator"
        m = re.search(r'for\s*\((?:const\s+)?(?:auto|x0::Ref|Ref)\s*&?\s*' + re.escape(var) + r'\s*:\s*(?:x0::)?all_of\(L"([^"]+)"', ln)
        if m:
            return m.group(1)
        m = re.search(r'\b' + re.escape(var) + r'\b[^;]*?=\s*(?:x0::)?call_o\((\w+),\s*L"(\w+)"\)', ln)
        if m:
            oc = resolve_var(lines, j - 1, m.group(1), depth + 1)
            return ret_class(oc, m.group(2))
        m = re.search(r'prop<(?:x0::)?Ref>\((\w+),\s*"([\w.]+)",\s*' + re.escape(var) + r'\)', ln)
        if m:
            oc = resolve_var(lines, j - 1, m.group(1), depth + 1)
            return prop_class(oc, m.group(2))
        m = re.search(r'\b' + re.escape(var) + r'\b[^;]*?=\s*(?:x0::)?find_path\(', ln)
        if m:
            return None
        if re.search(r'\b(?:Ref|auto|Ref&|auto&)\s+(?:\w+\s*(?:=[^,;]*)?,\s*)*' + re.escape(var) + r'\b(?!\s*\()', ln) or re.search(r'\bRef\s*&?\s*' + re.escape(var) + r'\b\s*[,)]', ln):
            return HINTS.get((CUR[0], var))
    return HINTS.get((CUR[0], var))


def ret_class(oc, fn):
    if not oc:
        return None
    q, f = find_fn(oc, fn)
    if not f:
        return None
    for n, k, o, x in f["params"]:
        if n == "ReturnValue" and x in BYADDR:
            return sname(BYADDR[x])
    return None


def prop_class(oc, path):
    if not oc:
        return None
    cps = [p for p in short.get(oc, []) if p in CLASSES]
    seg = path.split(".")[0]
    q, pr = find_prop(cps, seg)
    if not pr:
        return None
    return cls_of_prop(pr)


CALL_DECL = re.compile(r'(?:x0::)?Call\s+(\w+)\(\s*([^,]+?),\s*(L"[^"]*"|[\w:.()>?\-" ]+?)\s*\)\s*;')
CALL_TMP = re.compile(r'(?:x0::)?Call\(\s*([^,]+?),\s*(L"[^"]*"|[^)]+?)\)((?:\s*\.\s*[ifdbonvrs]\(L"[^"]*"[^;]*?\))*)')
CALL_FN = re.compile(r'(?:x0::)?(call_b|call_d|call_o|call_v|call0)\(\s*([^,]+?),\s*(L"[^"]*"|[\w:.()>?\-" ]+?)\s*[,)]')
BUILD = re.compile(r'\b(\w+)\s*\.\s*(i|f|d|b|o|n|v|r|s|raw)\(\s*L"([^"]+)"')
READ = re.compile(r'\b(\w+)\s*\.\s*(rb|ri|rf|rd|ro|rv|rr|rs|rarr|rraw)\(\s*(?:[^;]*?L"([^"]+)")?')
CHAINB = re.compile(r'\.\s*(i|f|d|b|o|n|v|r|s|raw)\(\s*L"([^"]+)"')
PROP_RE = re.compile(r'(?:x0::)?(prop|prop_set)(?:<([\w:]+)>)?\(\s*([^,]+?),\s*"([\w.]+)"')

rows = []
dyn_names = collections.defaultdict(set)


def lit(expr):
    m = re.fullmatch(r'L"([^"]*)"', expr.strip())
    return m.group(1) if m else None


HINT_RE = [
    ("s2m_world_x0.cpp", r'prop<x0::Ref>\(a, "StaticMeshComponent"', "DestructibleObject"),
    ("s2m_world_x0.cpp", r'prop<x0::Ref>\(comp, "StaticMesh"', "StaticMeshComponent"),
    ("s2m_world_x0.cpp", r'"DoorState"', "AnimBP_Military_Radio_C"),
    ("s2m_pop_x0.cpp", r'"bHidden", b\)', "Actor"),
    ("s2m_pop_x0.cpp", r'"NPCComponent"', "BP_AI_Human_C"),
    ("s2m_pop_x0.cpp", r'"MeshComponent", m\)', "BP_StaticItemContainer_C"),
    ("s2m_lock.cpp", r'"AnswerContainer"', "DialogueViewFolder"),
    ("s2m_lock.cpp", r'"ButtonHintTrade"', "DialogueViewFolder"),
    ("s2m_lock.cpp", r'"Guid\.', "InteractableObject"),
    ("s2m_lock.cpp", r'SetVisibility', "ScrollBox"),
    ("s2m_lock.cpp", r'GetVisibility', "UserWidget"),
    ("s2m_travel_x0.cpp", r'"CharacterMovement"', "BP_Stalker2Character_C"),
    ("s2m_travel_x0.cpp", r'"MovementMode"', "CharacterMovementComponent"),
    ("s2m_travel_x0.cpp", r'"bInCinematicSequence"', "PC"),
    ("s2m_session_x0.cpp", r'"bInCinematicSequence"', "PC"),
    ("s2m_damage.cpp", r'"Pawn", p\)', "PlayerController"),
    ("s2m_damage.cpp", r'"CharacterMovement"', "Character"),
    ("s2m_damage.cpp", r'"CapsuleComponent"', "Character"),
    ("s2m_damage.cpp", r'"PlayerCameraManager"', "PlayerController"),
]


def hint_for(lines, idx):
    base = CUR[0]
    for f, rx, cls in HINT_RE:
        if f == base and re.search(rx, lines[idx]):
            return cls
    return None


def recv_class(lines, idx, expr):
    h = hint_for(lines, idx)
    if h:
        return h
    e = expr.strip()
    m = re.match(r'^(\w+)$', e)
    if m:
        return resolve_var(lines, idx, m.group(1))
    m = re.match(r'^(?:x0::)?first_of\(L"([^"]+)"\)$', e)
    if m:
        return m.group(1)
    m = re.match(r'^(\w+)\(\)$', e)
    if m:
        if m.group(1) in ("mediator", "med"):
            return "CppMediator"
        return None
    return None


def add(mod, file, ln, cls, name, used, real, status, src=SRCNAME):
    rows.append((mod, "%s:%d" % (file, ln), cls or "?", name, used, real, status, src))


def eval_call(mod, file, lines, i, recv_expr, fn_expr, builders, readers, label):
    ln = i + 1
    rcls = recv_class(lines, i, recv_expr)
    lits = re.findall(r'L"([^"]*)"', fn_expr)
    if lits and not re.fullmatch(r'\s*\w+\s*', fn_expr):
        for fn in lits:
            check_fn(mod, os.path.basename(file), ln, rcls, fn, label, builders, readers)
        return
    if not lits:
        add(mod, os.path.basename(file), ln, rcls, "(dynamic) " + fn_expr.strip(), label, "name is a variable; the literals reaching it are in the dynamic rows", "NOT-FOUND", "see rows module-level dynamic")
        return
    check_fn(mod, os.path.basename(file), ln, rcls, lits[0], label, builders, readers)


def check_fn(mod, file, ln, rcls, fn, label, builders, readers, extra_note="", extra_src=""):
    q, f = (None, None)
    if rcls:
        q, f = find_fn(rcls, fn)
    notes = ""
    srcx = SRCNAME
    if not f:
        cands = find_fn_any(fn)
        if not cands:
            add(mod, file, ln, rcls, fn, label, "no UFunction of that name anywhere in the dump", "NOT-FOUND")
            return
        known = any(p in CLASSES for p in short.get(rcls or "", []))
        if rcls and known:
            near = ", ".join(o.rsplit(".", 1)[-1] for o, _ in cands[:6])
            add(mod, file, ln, rcls, fn, label, "not on %s chain; exists on: %s" % (rcls, near), "WRONG")
            return
        q, f = cands[0]
        if rcls:
            notes = " (class %s absent from dump: blueprint class not loaded at dump time; function exists on native %s)" % (rcls, ", ".join(o.rsplit(".", 1)[-1] for o, _ in cands[:3]))
            srcx = SRCNAME + " (native base only; BP class not in dump)"
        else:
            notes = " (receiver class unresolved; first candidate)"
    realsig = "%s::%s(%s)" % (sname(q.split(":")[0]) if ":" in q else sname(q), fn, ", ".join("%s:%s" % (p[0], pdesc(p)) for p in f["params"]))
    st, why = "OK", ""
    detail = []
    for how, pname in builders:
        s2, w2 = check_param(f, pname, how)
        detail.append("%s=%s" % (pname, w2))
        if s2 == "WRONG":
            st = "WRONG"
            why += w2 + "; "
    for how, pname in readers:
        s2, w2 = check_param(f, pname or "ReturnValue", how)
        detail.append("ret=%s" % w2 if not pname or pname == "ReturnValue" else "%s=%s" % (pname, w2))
        if s2 == "WRONG":
            st = "WRONG"
            why += w2 + "; "
    used = label
    add(mod, file, ln, sname(q.split(":")[0]) if q else rcls, fn, used, (why or "; ".join(detail)) + notes + extra_note + " | " + realsig, st, srcx + extra_src)


def scan_file(mod, path):
    lines = open(path, encoding="utf-8", errors="replace").read().split("\n")
    base = os.path.basename(path)
    CUR[0] = base
    n = len(lines)
    i = 0
    declared = []
    for i, ln in enumerate(lines):
        m = CALL_DECL.search(ln)
        if m and "x0::Call c(" not in ln.split("=")[0] or m:
            if m:
                var, recv, fnx = m.group(1), m.group(2), m.group(3)
                end = n
                for j in range(i + 1, min(n, i + 45)):
                    if re.search(r'\bCall\s+' + re.escape(var) + r'\(', lines[j]):
                        end = j
                        break
                    if re.match(r'^\s{0,12}[\w:<>&\*\s]+\([^;]*\)\s*(override|const)?\s*$', lines[j]) and "return" not in lines[j] and j > i + 1:
                        end = j
                        break
                end = min(end, i + 30)
                builders, readers = [], []
                run_at = end
                for j in range(i, end):
                    if re.search(r'\b' + re.escape(var) + r'\s*\.\s*run\(\)', lines[j]):
                        run_at = j
                        break
                bseg = lines[i + 1:run_at + 1]
                if run_at == i:
                    bseg = [lines[i][m.end():]]
                for sl in bseg:
                    for b in re.finditer(r'\.\s*(i|f|d|b|o|n|v|r|s|raw)\(\s*L"([^"]+)"', sl):
                        builders.append((b.group(1), b.group(2)))
                rseg = lines[run_at:min(end, run_at + 10)] if run_at < end else lines[i + 1:end]
                for sl in rseg:
                    for r in re.finditer(r'\b' + re.escape(var) + r'\s*\.\s*(rb|ri|rf|rd|ro|rv|rr|rs|rarr|rraw)\(([^;)]*)', sl):
                        pn = re.search(r'L"([^"]+)"', r.group(2))
                        readers.append((r.group(1), pn.group(1) if pn else None))
                builders = list(dict.fromkeys(builders))
                readers = list(dict.fromkeys(readers))
                label = ",".join("%s(%s)" % (h, p) for h, p in builders)
                if readers:
                    label += (" -> " if label else "-> ") + ",".join("%s(%s)" % (h, p or "ReturnValue") for h, p in readers)
                eval_call(mod, path, lines, i, recv, fnx, builders, readers, label or "(no args)")
                continue
        for m in re.finditer(r'(?<![\w.])(?:x0::)?Call\(\s*([^,()]+?),\s*(L"[^"]*"|\w+)\s*\)((?:\s*\.\s*(?:i|f|d|b|o|n|v|r|s|raw)\(\s*L"[^"]*"[^;]*?)*)(?:\.run\(\)|;|\))', ln):
            recv, fnx, tail = m.group(1), m.group(2), m.group(3)
            builders = [(b.group(1), b.group(2)) for b in CHAINB.finditer(tail)]
            label = ",".join("%s(%s)" % (h, p) for h, p in builders)
            eval_call(mod, path, lines, i, recv, fnx, builders, [], label or "(no args)")
        for m in CALL_FN.finditer(ln):
            kind = m.group(1)
            rmap = {"call_b": ("rb", None), "call_d": ("rd", None), "call_o": ("ro", None), "call_v": ("rv", None), "call0": None}
            readers = [rmap[kind]] if rmap[kind] else []
            eval_call(mod, path, lines, i, m.group(2), m.group(3), [], readers, "%s -> %s" % (kind, readers[0][0] if readers else "-"))
        for m in PROP_RE.finditer(ln):
            kind, T, obj, pth = m.group(1), m.group(2), m.group(3), m.group(4)
            eval_prop(mod, path, lines, i, kind, T, obj, pth)


PROP_T_OK = {
    "bool": {"BoolProperty", "ByteProperty"},
    "float": {"FloatProperty", "DoubleProperty"},
    "double": {"FloatProperty", "DoubleProperty"},
    "int32_t": {"ByteProperty", "EnumProperty", "IntProperty", "UInt32Property", "Int64Property", "UInt64Property", "Int16Property", "UInt16Property", "Int8Property"},
    "int64_t": {"ByteProperty", "EnumProperty", "IntProperty", "UInt32Property", "Int64Property", "UInt64Property", "Int16Property", "UInt16Property", "Int8Property"},
    "uint8_t": {"ByteProperty", "EnumProperty", "IntProperty", "UInt32Property", "Int64Property", "UInt64Property", "Int16Property", "UInt16Property", "Int8Property"},
    "Ref": {"ObjectProperty", "ClassProperty", "ObjectPtrProperty"},
    "x0::Ref": {"ObjectProperty", "ClassProperty", "ObjectPtrProperty"},
}


def eval_prop(mod, path, lines, i, kind, T, obj, pth):
    base = os.path.basename(path)
    ln = i + 1
    ocls = recv_class(lines, i, obj)
    if T is None:
        m = re.search(r'prop(?:_set)?\(\s*' + re.escape(obj) + r',\s*"' + re.escape(pth) + r'"\s*,\s*(?:\(?(float|double|int|bool)\)?)?\s*(\w+)', lines[i])
        T = "?"
        win = " ".join(lines[max(0, i - 6):i + 1])
        vm = re.search(r'\b(bool|float|double|int|int32_t|uint8_t|Ref)\s+(\w+)\b[^;]*;', win)
        mm = re.search(r',\s*(\w+)\s*\)', lines[i][lines[i].find(pth):])
        if mm:
            vn = mm.group(1)
            for j in range(i, max(0, i - 10), -1):
                dm = re.search(r'\b(bool|float|double|int|int32_t|int64_t|uint8_t|Ref|x0::Ref)\s+(?:\w+\s*,\s*)*' + re.escape(vn) + r'\b', lines[j])
                if dm:
                    T = dm.group(1)
                    break
        if T == "int":
            T = "int32_t"
    check_prop(mod, base, ln, ocls, kind, T, pth)


def check_prop(mod, base, ln, ocls, kind, T, pth, extra_note="", extra_src=""):
    cps = [p for p in short.get(ocls or "", []) if p in CLASSES]
    segs = pth.split(".")
    label = "%s<%s>(\"%s\")" % (kind, T, pth)
    if not ocls:
        add(mod, base, ln, None, pth, label, "receiver class unresolved offline", "NOT-FOUND", SRCNAME)
        return
    if not cps:
        add(mod, base, ln, ocls, pth, label, "class %s is not in the dump" % ocls, "NOT-FOUND", SRCNAME)
        return
    q, pr = find_prop(cps, segs[0])
    if not pr:
        alt = []
        for cp, c in CLASSES.items():
            for p in c["props"]:
                if p[0] == segs[0]:
                    alt.append(sname(cp))
        add(mod, base, ln, ocls, pth, label, "no property '%s' on %s chain; exists on: %s" % (segs[0], ocls, ",".join(alt[:6]) or "nowhere"), "WRONG" if alt else "NOT-FOUND")
        return
    cur = pr
    for seg in segs[1:]:
        if cur[1] == "StructProperty" and cur[3] in BYADDR:
            sp = BYADDR[cur[3]]
            f = struct_member(sp, seg)
            if not f:
                add(mod, base, ln, ocls, pth, label, "struct %s has no member %s" % (sname(sp), seg), "WRONG")
                return
            cur = f
        elif cur[1] in ("ObjectProperty", "ClassProperty") and cur[3] in BYADDR:
            q2, f = find_prop([BYADDR[cur[3]]], seg)
            if not f:
                add(mod, base, ln, ocls, pth, label, "class %s has no property %s" % (sname(BYADDR[cur[3]]), seg), "WRONG")
                return
            cur = f
        else:
            add(mod, base, ln, ocls, pth, label, "'%s' is %s, cannot descend" % (cur[0], pdesc(cur)), "WRONG")
            return
    ok = PROP_T_OK.get(T)
    st = "OK"
    why = pdesc(cur) + " @0x%x" % (cur[2] or 0)
    if ok is not None and cur[1] not in ok:
        st = "WRONG"
        why = "C++ type %s does not fit %s" % (T, pdesc(cur))
    if T in ("Ref", "x0::Ref") and cur[1] == "WeakObjectProperty":
        st = "WRONG"
        why = "WeakObjectProperty cannot be read as a raw UObject*"
    add(mod, base, ln, sname(q), pth, label, why + extra_note, st, SRCNAME + extra_src)


def main():
    for mod, fl in MODFILES.items():
        for fn in fl:
            p = os.path.join(SRC, fn)
            if os.path.exists(p):
                scan_file(mod, p)
    import extra
    extra.add_extra(sys.modules[__name__])
    seen = set()
    out = []
    for r in rows:
        if r in seen:
            continue
        seen.add(r)
        out.append(r)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("module\tfile:line\tclass\tfunction_or_property\tas_used\treal\tstatus\tsource\n")
        for r in out:
            f.write("\t".join(x.replace("\t", " ").replace("\n", " ") for x in r) + "\n")
    c = collections.Counter(r[6] for r in out)
    print(len(out), dict(c))
    for m in MODFILES:
        cm = collections.Counter(r[6] for r in out if r[0] == m)
        print(m, dict(cm))


if __name__ == "__main__":
    main()
