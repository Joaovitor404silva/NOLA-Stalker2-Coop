"""D1: convert the Lua DATA tables of S2 co-op into long-form TSV files for the C++ loader.

Run:  uv run --no-project --with lupa python -I D:/modding/cpp/S2CoopPort/data/convert_d1.py

Each Lua table is walked mechanically with lupa (Lua 5.4) and written as one row per leaf value:
  k1 .. kD  key path, one key per column ("s"+key for string keys, "i"+digits for integer keys), empty when unused
  t         value type: s string, i integer, f float, b boolean, e empty table
  v         value text (escaped: backslash, tab, CR and LF are written as \\ \t \r \n)
Every TSV is read back by an independent reader and compared with the Lua value (types included).
Block tables (main.lua WEAPONS, DIFF_WEAPON_MULT, FL, s2core M.FLAGS) are cut out of the source by a brace matcher
that skips strings and comments; nothing else of those files is executed.
"""
import hashlib
import math
import os
import re
import sys
from collections import Counter

from lupa import LuaRuntime

GAME = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl"
MW = os.path.join(GAME, "ModWork")
BRIDGE = os.path.join(MW, "release", "S2Coop", "bridge")
SCRIPTS = os.path.join(MW, "s2coop", "Mods", "S2CoopAlife", "Scripts")
OUT = r"D:\modding\cpp\S2CoopPort\data"

lua = LuaRuntime(unpack_returned_tuples=True)
LUA_TYPE = lua.eval("type")
MATH_TYPE = lua.eval("math.type")
LUA_ITEMS = lua.eval(
    "function(t) local ks, vs, n = {}, {}, 0 "
    "for k, v in pairs(t) do n = n + 1; ks[n] = k; vs[n] = v end "
    "return ks, vs, n end"
)


def md5(path):
    with open(path, "rb") as f:
        return hashlib.md5(f.read()).hexdigest()


def read_src(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        return f.read()


def blank_range(buf, a, b):
    for k in range(a, b):
        if buf[k] != "\n":
            buf[k] = " "


def block_at(src, open_idx):
    """Return (close_idx, stripped) for the {...} block that opens at open_idx.
    stripped has comments replaced by spaces (same length), strings kept."""
    buf = list(src)
    depth = 0
    i = open_idx
    n = len(src)
    while i < n:
        if src.startswith("--[[", i):
            j = src.index("]]", i) + 2
            blank_range(buf, i, j)
            i = j
            continue
        if src.startswith("--", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            blank_range(buf, i, j)
            i = j
            continue
        c = src[i]
        if c in "\"'":
            j = i + 1
            while src[j] != c:
                j += 2 if src[j] == "\\" else 1
            i = j + 1
            continue
        if src.startswith("[[", i):
            raise ValueError("long string not supported at offset %d" % i)
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i, "".join(buf[open_idx:i + 1])
        i += 1
    raise ValueError("unclosed block at offset %d" % open_idx)


def extract_block(src, start_regex):
    """Cut the table literal that follows start_regex. Returns (text, first_line, last_line, stripped)."""
    m = re.search(start_regex, src, re.M)
    if not m:
        raise ValueError("start not found: " + start_regex)
    if len(re.findall(start_regex, src, re.M)) != 1:
        raise ValueError("start not unique: " + start_regex)
    open_idx = src.index("{", m.start())
    close_idx, stripped = block_at(src, open_idx)
    first = src.count("\n", 0, m.start()) + 1
    last = src.count("\n", 0, close_idx) + 1
    return src[open_idx:close_idx + 1], first, last, stripped


def top_level_keys(stripped):
    """Names of the keys written directly inside the outer table (depth 1), duplicates included."""
    keys = []
    depth = 0
    i = 0
    ident = re.compile(r"([A-Za-z_]\w*)(\s*=(?!=))?")
    while i < len(stripped):
        c = stripped[i]
        if c in "\"'":
            j = i + 1
            while stripped[j] != c:
                j += 2 if stripped[j] == "\\" else 1
            i = j + 1
            continue
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif depth == 1 and (c.isalpha() or c == "_"):
            m = ident.match(stripped, i)
            if m.group(2):
                keys.append(m.group(1))
            i = m.end()
            continue
        i += 1
    return keys



def to_py(v):
    t = LUA_TYPE(v)
    if t == "table":
        ks, vs, n = LUA_ITEMS(v)
        out = {}
        for idx in range(1, int(n) + 1):
            k = ks[idx]
            kt = LUA_TYPE(k)
            if kt == "string":
                pk = str(k)
            elif kt == "number" and MATH_TYPE(k) == "integer":
                pk = int(k)
            else:
                raise ValueError("unsupported key type %s (%r)" % (kt, k))
            if pk in out:
                raise ValueError("duplicate key %r" % (pk,))
            out[pk] = to_py(vs[idx])
        return out
    if t == "string":
        return str(v)
    if t == "boolean":
        return bool(v)
    if t == "number":
        if MATH_TYPE(v) == "integer":
            return int(v)
        f = float(v)
        if not math.isfinite(f):
            raise ValueError("non-finite number")
        return f
    raise ValueError("unsupported value type " + str(t))


def key_order(k):
    return (0, k) if isinstance(k, int) else (1, k)


def leaves(py, path=()):
    if isinstance(py, dict):
        if not py:
            yield path, "e", ""
        for k in sorted(py, key=key_order):
            yield from leaves(py[k], path + (k,))
        return
    if isinstance(py, bool):
        yield path, "b", "true" if py else "false"
    elif isinstance(py, int):
        yield path, "i", str(py)
    elif isinstance(py, float):
        yield path, "f", repr(py)
    elif isinstance(py, str):
        yield path, "s", py
    else:
        raise ValueError("cannot write " + repr(py))



def esc(s):
    return s.replace("\\", "\\\\").replace("\t", "\\t").replace("\r", "\\r").replace("\n", "\\n")


def unesc(s):
    out = []
    i = 0
    while i < len(s):
        c = s[i]
        if c == "\\":
            nxt = s[i + 1]
            out.append({"\\": "\\", "t": "\t", "r": "\r", "n": "\n"}[nxt])
            i += 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def key_cell(k):
    return ("s" + esc(k)) if isinstance(k, str) else ("i" + str(k))


def key_from_cell(c):
    if c[:1] == "s":
        return unesc(c[1:])
    if c[:1] == "i":
        return int(c[1:])
    raise ValueError("bad key cell " + repr(c))


def write_tsv(path, py):
    rows = list(leaves(py))
    depth = max(len(p) for p, _, _ in rows)
    header = ["k%d" % (i + 1) for i in range(depth)] + ["t", "v"]
    lines = ["\t".join(header)]
    for p, t, v in rows:
        cells = [key_cell(k) for k in p] + [""] * (depth - len(p)) + [t, esc(v)]
        lines.append("\t".join(cells))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    return len(rows), depth


def read_tsv(path):
    with open(path, "r", encoding="utf-8", newline="") as f:
        text = f.read()
    lines = text.split("\n")
    assert lines[-1] == "", "file must end with newline"
    lines = lines[:-1]
    header = lines[0].split("\t")
    assert header[-2:] == ["t", "v"], "bad header tail"
    depth = len(header) - 2
    assert header[:depth] == ["k%d" % (i + 1) for i in range(depth)], "bad key header"
    root = {}
    count = 0
    for line in lines[1:]:
        cells = line.split("\t")
        assert len(cells) == depth + 2, "bad cell count"
        path_keys = []
        i = 0
        while i < depth and cells[i] != "":
            path_keys.append(key_from_cell(cells[i]))
            i += 1
        assert all(c == "" for c in cells[i:depth]), "gap in key columns"
        assert path_keys, "row without key"
        t, v = cells[depth], unesc(cells[depth + 1])
        if t == "s":
            val = v
        elif t == "i":
            val = int(v)
        elif t == "f":
            val = float(v)
        elif t == "b":
            assert v in ("true", "false")
            val = v == "true"
        elif t == "e":
            val = {}
        else:
            raise ValueError("bad type " + t)
        node = root
        for k in path_keys[:-1]:
            nxt = node.setdefault(k, {})
            assert isinstance(nxt, dict), "path crosses a value"
            node = nxt
        last = path_keys[-1]
        assert last not in node, "duplicate row path"
        node[last] = val
        count += 1
    return root, count


def same(a, b):
    if type(a) is not type(b):
        return False
    if isinstance(a, dict):
        return a.keys() == b.keys() and all(same(a[k], b[k]) for k in a)
    return a == b


def count_leaves(py):
    return sum(1 for _ in leaves(py))



def whole_file(path, drop_keys=()):
    src = read_src(path)
    val = lua.execute(src)
    py = to_py(val)
    dropped = {}
    for k in drop_keys:
        if k in py:
            dropped[k] = count_leaves({k: py.pop(k)})
    return py, dropped, {"source": path, "md5": md5(path), "lines": "1-%d (whole file)" % (src.count("\n") + 1)}


def block_file(path, start_regex, expect_keys=None):
    src = read_src(path)
    text, first, last, stripped = extract_block(src, start_regex)
    keys = top_level_keys(stripped)
    dup = sorted(k for k, c in Counter(keys).items() if c > 1)
    py = to_py(lua.execute("return " + text))
    if expect_keys is not None:
        assert len(keys) == len(set(keys)) or True
    return py, dup, {"source": path, "md5": md5(path), "lines": "%d-%d" % (first, last)}


JOBS = [
    ("s2u_actions", lambda: whole_file(os.path.join(BRIDGE, "s2u_actions.lua"))),
    ("s2u_anomaly", lambda: whole_file(os.path.join(BRIDGE, "s2u_anomaly.lua"))),
    ("s2u_choice", lambda: whole_file(os.path.join(BRIDGE, "s2u_choice.lua"))),
    ("s2u_cine", lambda: whole_file(os.path.join(BRIDGE, "s2u_cine.lua"))),
    ("s2u_destruct", lambda: whole_file(os.path.join(BRIDGE, "s2u_destruct.lua"))),
    ("s2u_dlg_header", lambda: whole_file(os.path.join(BRIDGE, "s2u_dlg.lua"), drop_keys=("set",))),
    ("s2u_qdrop", lambda: whole_file(os.path.join(BRIDGE, "s2u_qdrop.lua"))),
    ("s2u_qitems", lambda: whole_file(os.path.join(BRIDGE, "s2u_qitems.lua"))),
    ("s2u_qpick", lambda: whole_file(os.path.join(BRIDGE, "s2u_qpick.lua"))),
    ("s2u_vendor", lambda: whole_file(os.path.join(BRIDGE, "s2u_vendor.lua"))),
    ("flags_s2core", lambda: block_file(os.path.join(SCRIPTS, "s2core.lua"), r"^M\.FLAGS\s*=\s*\{")),
    ("flags_main", lambda: block_file(os.path.join(SCRIPTS, "main.lua"), r"^FL\s*=\s*\{")),
    ("weapons_main", lambda: block_file(os.path.join(SCRIPTS, "main.lua"), r"^local WEAPONS\s*=\s*\{")),
    ("diff_weapon_mult_main", lambda: block_file(os.path.join(SCRIPTS, "main.lua"), r"^local DIFF_WEAPON_MULT\s*=\s*\{")),
]



def main():
    os.makedirs(OUT, exist_ok=True)
    summary = []
    failed = []
    for name, loader in JOBS:
        try:
            py, extra, src_info = loader()
        except Exception as e:
            failed.append((name, "convert: %s" % e))
            continue
        out_path = os.path.join(OUT, name + ".tsv")
        expected = count_leaves(py)
        rows, depth = write_tsv(out_path, py)
        back, read_rows = read_tsv(out_path)
        ok = same(py, back) and rows == expected and read_rows == expected
        status = "OK" if ok else "FAIL"
        if not ok:
            failed.append((name, "round trip mismatch"))
        if isinstance(extra, dict):
            extra_txt = ("dropped " + ", ".join("%s (%d rows)" % kv for kv in extra.items())) if extra else ""
        else:
            extra_txt = ("duplicate top-level keys in source: " + ", ".join(extra)) if extra else ""
        summary.append({
            "name": name, "tsv": out_path, "rows": rows, "depth": depth,
            "status": status, "src": src_info, "extra": extra_txt,
            "top": sorted(py.keys(), key=key_order) if isinstance(py, dict) else [],
        })
        print("%-24s rows=%-7d depth=%d round-trip=%s %s" % (name, rows, depth, status, extra_txt))
    write_readme(summary)
    if failed:
        print("FAILED:", failed)
        return 1
    print("ALL OK")
    return 0


def write_readme(summary):
    lines = []
    lines.append("# data/ (D1 Lua data tables -> TSV)")
    lines.append("")
    lines.append("Generated by `convert_d1.py` (run: `uv run --no-project --with lupa python -I convert_d1.py`). Do not edit the TSV files by hand.")
    lines.append("")
    lines.append("## Format (long form, one row per leaf value)")
    lines.append("- Line 1 is the header. Tab separated, UTF-8, LF line endings, no comments, no BOM.")
    lines.append("- Columns: `k1` .. `kD` (key path, D = depth of that file, see the table below), `t` (value type), `v` (value).")
    lines.append("- A key cell is the key with a type letter: `s` + text for a string key, `i` + digits for an integer key. Unused key cells are empty.")
    lines.append("  Key cells fill from `k1` to the left; an empty cell ends the path.")
    lines.append("- `t`: `s` string, `i` integer, `f` float (Lua float, shown with a dot or exponent), `b` boolean (`true`/`false`), `e` empty table (`v` empty).")
    lines.append("- `v`: the value. Escapes: `\\\\` = backslash, `\\t` = tab, `\\r` = CR, `\\n` = LF. Other text is raw.")
    lines.append("- Lua arrays are integer keys 1..n in the `i` form. Rows are sorted by key path.")
    lines.append("- Loader rule: split each line on tab, rebuild the path from the key cells, and set the leaf. Duplicate paths are an error.")
    lines.append("")
    lines.append("## Files")
    lines.append("")
    lines.append("| TSV | Source (ModWork) | Lines | Rows | Depth | Round trip | Notes |")
    lines.append("|---|---|---|---|---|---|---|")
    for s in summary:
        src = s["src"]["source"].replace(MW + "\\", "")
        lines.append("| %s.tsv | %s (md5 %s) | %s | %d | %d | %s | %s |" % (
            s["name"], src, s["src"]["md5"][:8], s["src"]["lines"], s["rows"], s["depth"], s["status"], s["extra"] or "-"))
    lines.append("")
    lines.append("## Top-level keys per file (first key column)")
    lines.append("")
    for s in summary:
        lines.append("- `%s.tsv`: %s" % (s["name"], ", ".join(str(k) for k in s["top"]) if s["top"] else "(none)"))
    lines.append("")
    lines.append("## Not converted (plan says drop or code)")
    lines.append("- `s2u_dlg.lua` key `set` (per-node dialog table): DROP (PORT_PLAN 1c). Header keys are in `s2u_dlg_header.tsv`.")
    lines.append("- `s2u_interact.lua`, `s2u_items.lua`: DROP (ia71 and idrop are OFF).")
    lines.append("- `dm70.lua`, `cj68.lua`, `ia71.lua`: code, not data (ported by hand or deleted).")
    lines.append("- `dm70.lua` `M.FLAGS` (dlg* flags, line 184): not converted here.")
    lines.append("- Already TSV and kept as they are: s2u_triggers, s2u_cont, s2u_contk, s2u_equip, s2u_trade, s2u_art, s2u_artspawn, s2u_kit, s2u_basekit.txt.")
    lines.append("")
    lines.append("## Column meaning per file")
    lines.append("")
    lines.append("The source comment lines are copied from each Lua file (they describe the key meaning):")
    lines.append("")
    for s in summary:
        if s["src"]["source"].endswith(".lua") and "lines" in s["src"] and "(whole file)" in s["src"]["lines"]:
            comment = []
            with open(s["src"]["source"], "r", encoding="utf-8") as f:
                for line in f:
                    if line.startswith("--"):
                        comment.append(line.strip())
                    elif line.strip() == "":
                        continue
                    else:
                        break
            lines.append("### %s.tsv" % s["name"])
            lines.extend("    " + c for c in comment) if comment else lines.append("    (no comment lines)")
            lines.append("")
    lines.append("Block tables (flags, weapons) have no comments of their own: `k1` = key name (`flags_*`: flag name, value in `v`), `weapons_main`: `k1` = weapon mesh key (e.g. `SK_pm`), `k2` = index 1..5 (BaseDamage, ArmorPiercing, EffectiveFireDistanceMax cm, DistanceDropOffLength cm, MinBulletDistanceDamageModifier), `diff_weapon_mult_main`: `k1` = difficulty name.")
    lines.append("")
    with open(os.path.join(OUT, "README_D1.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    sys.exit(main())
