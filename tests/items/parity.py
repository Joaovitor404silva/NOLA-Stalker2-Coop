import hashlib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent.parent
CORE = PORT / "tests" / "x0" / "lua" / "s2core_ref.lua"
CORE_MD5 = "0b56e1b59fb6b0fa607c0a773b6bec88"
RANGES = [(1324, 1375), (1508, 1605), (1613, 1658), (2950, 2980), (3928, 4048), (4458, 4473), (5578, 5603), (5618, 5640), (5773, 5785), (7279, 7340)]
EXE = HERE / "test_items.exe"
LOOT = HERE / "loot_sids.tsv"
QTSV = PORT / "data" / "s2u_qitems.tsv"
WORK = Path(os.environ.get("S2M_ITEMS_WORK", str(HERE / "out")))


def core_lines():
    data = CORE.read_bytes()
    md5 = hashlib.md5(data).hexdigest()
    if md5 != CORE_MD5:
        sys.exit("s2core.lua md5 %s, expected %s" % (md5, CORE_MD5))
    return data.decode("utf-8").splitlines()


def chunk(lines):
    out = []
    for a, b in RANGES:
        out.extend(lines[a - 1:b])
    return "\n".join(out)


def loot(lines):
    ln = lines[1315]
    if not ln.startswith("local LOOT_SIDS = {"):
        sys.exit("LOOT_SIDS not on line 1316")
    return re.findall(r'"([^"]+)"', ln)


def flag_defaults():
    vals = {}
    for name in ("flags_s2core.tsv", "flags_main.tsv"):
        for row in (PORT / "data" / name).read_text(encoding="utf-8").splitlines()[1:]:
            k, t, v = row.split("\t")
            if k.startswith("s") and t in ("i", "f"):
                vals.setdefault(k[1:], v)
    return vals


def fresh(root):
    if root.exists():
        shutil.rmtree(root)
    root.mkdir(parents=True)


def seed(root, scen):
    for ln in scen:
        w = ln.split(" ", 2)
        if w[0] == "file" and len(w) == 3:
            p = root / w[1]
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(w[2].replace("\\n", "\n").replace("\\t", "\t").encode("utf-8"))
    return [ln for ln in scen if not ln.startswith("file ")]


def legacy_text(s):
    return s.replace("S2CORE", "NOLA").replace("S2Coop_", "Nola_")


def run_lua(core, sids, defaults, scen, root):
    from lupa import lua54
    L = lua54.LuaRuntime(unpack_returned_tuples=True)
    g = L.globals()
    g.ROOT = str(root) + "\\"
    t = L.table()
    for i, s in enumerate(sids, 1):
        t[i] = s
    g.LOOT_SIDS = t
    fl = L.table()
    for k, v in defaults.items():
        fl[k] = L.eval("math.tointeger(%s) or %s" % (v, v))
    g.FL = fl
    src = (HERE / "world.lua").read_text(encoding="utf-8").replace("CORE_CHUNK", core)
    L.execute(src)
    lt = L.table()
    for i, s in enumerate(scen, 1):
        lt[i] = s
    return g.RUN(lt)


def run_cpp(scen_path, root):
    r = subprocess.run([str(EXE), str(scen_path), str(root) + "\\", str(LOOT), str(QTSV)], capture_output=True, text=True)
    if r.returncode != 0:
        return None, "rc %d %s" % (r.returncode, r.stderr)
    return (root / "run.log").read_text(encoding="utf-8").replace("\r\n", "\n"), ""


def files(root):
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file() and p.name != "run.log":
            out[p.relative_to(root).as_posix()] = p.read_bytes().replace(b"\r\n", b"\n")
    return out


def main():
    lines = core_lines()
    core = chunk(lines)
    sids = loot(lines)
    tsv = [l.split("\t")[2] for l in LOOT.read_text(encoding="utf-8").splitlines()[1:]]
    if tsv != sids:
        sys.exit("loot_sids.tsv out of date: run gen_loot_sids.py")
    defaults = flag_defaults()
    pre = ["flag %s %s" % (k, v) for k, v in sorted(defaults.items())]
    only = sys.argv[1:]
    fails = total = 0
    WORK.mkdir(parents=True, exist_ok=True)
    for sp in sorted((HERE / "scen").glob("*.txt")):
        if only and sp.stem not in only:
            continue
        total += 1
        raw = [x.rstrip("\r") for x in sp.read_text(encoding="utf-8").splitlines()]
        root = WORK / "run"
        fresh(root)
        scen = pre + seed(root, raw)
        fp = WORK / (sp.stem + ".full.txt")
        fp.write_text("\n".join(scen) + "\n", encoding="utf-8")
        try:
            lua_out = legacy_text(run_lua(core, sids, defaults, scen, root))
        except Exception as e:
            print("FAIL", sp.stem, "lua error", e)
            fails += 1
            continue
        lf = files(root)
        lua_dir = WORK / (sp.stem + ".lua")
        if lua_dir.exists():
            shutil.rmtree(lua_dir)
        root.rename(lua_dir)
        (lua_dir / "run.log").write_text(lua_out, encoding="utf-8")
        fresh(root)
        seed(root, raw)
        cpp_out, err = run_cpp(fp, root)
        if cpp_out is None:
            print("FAIL", sp.stem, "cpp error", err)
            fails += 1
            continue
        cf = files(root)
        cpp_dir = WORK / (sp.stem + ".cpp")
        if cpp_dir.exists():
            shutil.rmtree(cpp_dir)
        root.rename(cpp_dir)
        a = lua_out.rstrip("\n").split("\n")
        b = cpp_out.rstrip("\n").split("\n")
        diff = None
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else "<end>"
            y = b[i] if i < len(b) else "<end>"
            if x != y:
                diff = (i, x, y)
                break
        fdiff = sorted(set(lf) ^ set(cf)) + sorted(k for k in set(lf) & set(cf) if lf[k] != cf[k])
        errs = [x for x in a if x.startswith("ERR ") or x.startswith("BAD ")]
        logs = sum(1 for x in a if x.startswith("LOG "))
        stubs = sum(1 for x in a if x.startswith("STUB "))
        outs = sum(1 for x in a if x.startswith("OUT "))
        if diff or fdiff or errs:
            fails += 1
            print("FAIL", sp.stem)
            for e in errs[:3]:
                print("  lua:", e)
            if diff:
                print("  line", diff[0])
                print("  lua:", diff[1])
                print("  cpp:", diff[2])
            for k in fdiff:
                print("  file differs:", k)
        else:
            print("PASS %s (%d log, %d calls, %d out, %d files identical)" % (sp.stem, logs, stubs, outs, len(lf)))
    print("%d/%d scenarios pass" % (total - fails, total))
    return 1 if fails or not total else 0


if __name__ == "__main__":
    sys.exit(main())
