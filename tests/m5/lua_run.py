import os
import sys

from lupa import LuaRuntime

HERE = os.path.dirname(os.path.abspath(__file__))

SEGMENTS = [
    [(1606, 1663, "-- ===== batch 53: doors")],
    [(2057, 2139, "local function doorTick(ST)")],
    [(2140, 2517, "local function dobjKey(o)")],
    [(2518, 2564, "-- ===== batch 59: interactable power")],
    [(2826, 2949, "local function ienTick(ST)")],
    [(7027, 7038, "function B73.ld(T, name)"), (7153, 7233, "-- ---------- (c) anomalies")],
]


def read_core(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return f.read().split("\n")


def segment(lines, parts):
    out = []
    for a, b, head in parts:
        if not lines[a - 1].startswith(head):
            raise SystemExit("core anchor moved at line %d: %r" % (a, lines[a - 1][:60]))
        out.extend(lines[a - 1:b])
    return "\n".join(out)


def flags_text(lua, lines):
    start = next(i for i, l in enumerate(lines) if l.startswith("M.FLAGS = {"))
    probe = lua.eval("function(s) return load('return ' .. s) end")
    first = lines[start].split("=", 1)[1].strip()
    acc = [first]
    for i in range(start + 1, start + 700):
        text = "\n".join(acc)
        res = probe(text)
        fn = res[0] if isinstance(res, tuple) else res
        if fn:
            return text
        acc.append(lines[i])
    raise SystemExit("M.FLAGS end not found")


def build(core_path, bridge_dir):
    lines = read_core(core_path)
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().DATADIR = bridge_dir.rstrip("/\\") + "/"
    ftext = flags_text(lua, lines)
    lua.globals().FLAGS_TEXT = ftext
    with open(os.path.join(HERE, "lua_ref.lua"), encoding="utf-8") as f:
        src = f.read()
    for i, parts in enumerate(SEGMENTS, 1):
        src = src.replace("--[[SEG%d]]" % i, segment(lines, parts))
    return lua, lua.execute(src), ftext


def write_flags(lua, ftext, path):
    tbl = lua.eval("(function(s) return load('return ' .. s)() end)")(ftext)
    rows = []
    for k in sorted(tbl.keys()):
        v = tbl[k]
        if isinstance(v, (int, float)) and not isinstance(v, bool):
            rows.append("%s %r" % (k, v))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(rows) + "\n")


def main():
    core, bridge, scenario = sys.argv[1], sys.argv[2], sys.argv[3]
    lua, ref, ftext = build(core, bridge)
    if len(sys.argv) > 5 and sys.argv[4] == "--flags-out":
        write_flags(lua, ftext, sys.argv[5])
        if scenario == "-":
            return
    ref.run(scenario)
    out = []
    tr = ref.trace()
    for i in range(1, len(tr) + 1):
        out.append(tr[i])
    sys.stdout.write("\n".join(out) + "\nEND\n")


main()
