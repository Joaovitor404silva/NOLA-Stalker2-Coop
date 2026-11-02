import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent.parent
CORE = Path(r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\Mods\S2CoopAlife\Scripts\s2core.lua")
CORE_MD5 = "0b56e1b59fb6b0fa607c0a773b6bec88"
RANGES = [(3879, 3884), (4050, 4077), (4143, 4147), (4296, 4473), (4475, 4757), (5021, 5256), (5258, 5469), (1040, 1141), (1664, 1700), (5956, 5970)]
SORTED_FILES = re.compile(r"(client_applied\.txt|n7ap_.*\.txt|n7sv_pares\.txt)$")
EXE = HERE / "out" / "test_save.exe"
WORK = Path(os.environ.get("S2M_SAVE_WORK", str(HERE / "out" / "work")))


def core_lines():
    import hashlib
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


def flag_defaults(names=("flags_s2core.tsv", "flags_main.tsv")):
    vals = {}
    for name in names:
        for row in (PORT / "data" / name).read_text(encoding="utf-8").splitlines()[1:]:
            k, t, v = row.split("\t")
            if k.startswith("s") and t in ("i", "f"):
                vals[k[1:]] = v
    return vals


def check_defaults(vals):
    src = (PORT / "src" / "s2m_save.cpp").read_text(encoding="utf-8")
    bad = []
    for k, d in re.findall(r'fl\("(\w+)",\s*([-0-9.e]+)\)', src):
        if k in vals and float(vals[k]) != float(d):
            bad.append("%s code %s tsv %s" % (k, d, vals[k]))
        if k not in vals:
            bad.append("%s code %s tsv missing" % (k, d))
    for k, d in re.findall(r'flagText\("(\w+)",\s*"([^"]*)"\)', src):
        if k in vals and vals[k] != d:
            bad.append("%s text %s tsv %s" % (k, d, vals[k]))
    return sorted(set(bad))


def n7tag(p):
    h = 5381
    for b in p.lower().replace("/", "\\").encode("utf-8"):
        h = (h * 33 + b) % 2147483647
    return "%x" % h


def fresh(root):
    if root.exists():
        shutil.rmtree(root)
    (root / "B").mkdir(parents=True)
    (root / "saved" / "STEAM" / "SaveGames" / "Data").mkdir(parents=True)


def pylist(d):
    p = Path(d)
    if not p.is_dir():
        return ""
    return ",".join(sorted(x.stem for x in p.iterdir() if x.is_file() and x.suffix == ".sav"))


def legacy_text(s):
    return s.replace("S2CORE", "NOLA").replace("S2Coop_", "Nola_")


def run_lua(core, sids, defaults, scen, root):
    from lupa import lua54
    L = lua54.LuaRuntime(unpack_returned_tuples=True)
    g = L.globals()
    g.ROOT = str(root) + "\\"
    g.PYLIST = pylist
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


def run_cpp(scen_path, root, loot_path):
    r = subprocess.run([str(EXE), str(scen_path), str(root), str(loot_path)], capture_output=True, text=True)
    if r.returncode != 0:
        return None, r.stderr
    return (root / "run.log").read_text(encoding="utf-8").replace("\r\n", "\n"), ""


def files(root):
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file() and p.name != "run.log":
            d = p.read_bytes()
            rel = p.relative_to(root).as_posix()
            if SORTED_FILES.search(rel):
                d = b"\n".join(sorted(d.split(b"\n")))
            out[rel] = d
    return out


def main():
    lines = core_lines()
    core = chunk(lines)
    sids = loot(lines)
    defaults = flag_defaults()
    bad = check_defaults(flag_defaults(("flags_s2core.tsv", "flags_main.tsv", "flags_x0.tsv")))
    for b in bad:
        print("DEFAULT MISMATCH", b)
    loot_path = HERE / "out" / "loot_sids.txt"
    loot_path.parent.mkdir(parents=True, exist_ok=True)
    loot_path.write_text("\n".join(sids) + "\n", encoding="utf-8")
    defaults["sxscope"] = "0"
    pre = ["flag %s %s" % (k, v) for k, v in sorted(defaults.items())]
    only = sys.argv[1:]
    fails = 0
    total = 0
    for sp in sorted((HERE / "scen").glob("*.txt")):
        if only and sp.stem not in only:
            continue
        total += 1
        scen = [x.rstrip("\r") for x in sp.read_text(encoding="utf-8").splitlines()]
        full = pre + scen
        fp = WORK / (sp.stem + ".full.txt")
        WORK.mkdir(parents=True, exist_ok=True)
        fp.write_text("\n".join(full) + "\n", encoding="utf-8")
        root = WORK / "run"
        fresh(root)
        try:
            lua_out = legacy_text(run_lua(core, sids, defaults, full, root))
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
        cpp_out, err = run_cpp(fp, root, loot_path)
        if cpp_out is None:
            print("FAIL", sp.stem, "cpp error", err)
            fails += 1
            continue
        cf = files(root)
        cpp_dir = WORK / (sp.stem + ".cpp")
        if cpp_dir.exists():
            shutil.rmtree(cpp_dir)
        root.rename(cpp_dir)
        a = [x for x in lua_out.split("\n") if not x.startswith("CMD flag ")]
        b = [x for x in cpp_out.split("\n") if not x.startswith("CMD flag ")]
        diff = None
        for i in range(max(len(a), len(b))):
            x = a[i] if i < len(a) else "<end>"
            y = b[i] if i < len(b) else "<end>"
            if x != y:
                diff = (i, x, y)
                break
        fdiff = sorted(set(lf) ^ set(cf)) + sorted(k for k in set(lf) & set(cf) if lf[k] != cf[k])
        logs = sum(1 for x in a if x.startswith("LOG "))
        stubs = sum(1 for x in a if x.startswith("STUB "))
        if diff or fdiff:
            fails += 1
            print("FAIL", sp.stem)
            if diff:
                print("  line", diff[0])
                print("  lua:", diff[1])
                print("  cpp:", diff[2])
            for k in fdiff:
                print("  file differs:", k)
        else:
            print("PASS %s (%d log lines, %d calls, %d files identical)" % (sp.stem, logs, stubs, len(lf)))
    print("%d/%d scenarios pass" % (total - fails, total))
    return 1 if fails or bad else 0


if __name__ == "__main__":
    sys.exit(main())
