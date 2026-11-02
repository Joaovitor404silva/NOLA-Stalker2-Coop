import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent.parent
CORES = [HERE.parent / "x0" / "lua" / "s2core_ref.lua"]
CORE = next((p for p in CORES if p.exists()), CORES[0])
CORE_MD5 = "0b56e1b59fb6b0fa607c0a773b6bec88"
BRIDGE = Path(r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\release\S2Coop\bridge")
RANGES = [(1376, 1506), (1728, 2055), (3078, 3206), (3677, 3713), (3717, 3923), (4142, 4286), (4458, 4473), (4758, 4881), (4896, 5019), (5471, 5540)]
B164 = 9799
LUA_DATA = ["s2u_qitems.lua", "s2u_qpick.lua", "s2u_qdrop.lua", "s2u_triggers.tsv"]
SHARED = ["s2u_triggers.tsv", "s2c_skip.txt"]
EXE = HERE / "out" / "test_quest.exe"
WORK = HERE / "out" / "work"


def need(paths):
    miss = [str(p) for p in paths if not p.exists()]
    if miss:
        print("SKIP: missing external dependency " + ", ".join(miss))
        sys.exit(77)


def core_lines():
    data = CORE.read_bytes()
    md5 = hashlib.md5(data).hexdigest()
    if md5 != CORE_MD5:
        sys.exit("s2core md5 %s, expected %s" % (md5, CORE_MD5))
    return data.decode("utf-8").splitlines()


def chunk(lines):
    out = []
    for a, b in RANGES:
        out.extend(lines[a - 1:b])
    return "\n".join(out)


def b164(lines):
    ln = lines[B164 - 1]
    if not ln.lstrip().startswith("if (FL.b164off or 0) == 0"):
        sys.exit("b164 force not on line %d" % B164)
    return ln


def loot(lines):
    import re
    ln = lines[1315]
    if not ln.startswith("local LOOT_SIDS = {"):
        sys.exit("LOOT_SIDS not on line 1316")
    return re.findall(r'"([^"]+)"', ln)


def flag_defaults():
    nums, strs = {}, {}
    for name in ("flags_s2core.tsv", "flags_main.tsv"):
        for row in (PORT / "data" / name).read_text(encoding="utf-8").splitlines()[1:]:
            k, t, v = row.split("\t")
            if not k.startswith("s"):
                continue
            if t in ("i", "f"):
                nums[k[1:]] = v
            elif t == "s" and v and " " not in v:
                strs[k[1:]] = v
    return nums, strs


def fresh(root, lua):
    if root.exists():
        shutil.rmtree(root)
    b = root / "B"
    b.mkdir(parents=True)
    for n in (LUA_DATA if lua else []) + SHARED:
        src = HERE / "lua_data" / n
        if not src.exists():
            src = BRIDGE / n if (BRIDGE / n).exists() else BRIDGE / "client_only" / n
        shutil.copyfile(src, b / n)


def legacy_text(s):
    return s.replace("S2CORE", "NOLA").replace("S2Coop_", "Nola_")


def run_lua(core, b164line, sids, scen, root):
    from lupa import lua54
    L = lua54.LuaRuntime(unpack_returned_tuples=True)
    g = L.globals()
    g.ROOT = str(root) + "\\"
    t = L.table()
    for i, s in enumerate(sids, 1):
        t[i] = s
    g.LOOT_SIDS = t
    src = (HERE / "world.lua").read_text(encoding="utf-8").replace("CORE_CHUNK", core).replace("B164_LINE", b164line)
    L.execute(src)
    lt = L.table()
    for i, s in enumerate(scen, 1):
        lt[i] = s
    return g.RUN(lt)


def files(root):
    skip = set(LUA_DATA + SHARED + ["run.log"])
    out = {}
    for p in sorted(root.rglob("*")):
        if p.is_file() and p.name not in skip:
            out[p.relative_to(root).as_posix()] = p.read_bytes().replace(b"\r\n", b"\n")
    return out


def main():
    need([CORE, BRIDGE])
    lines = core_lines()
    core = chunk(lines)
    bl = b164(lines)
    sids = loot(lines)
    nums, strs = flag_defaults()
    loot_path = HERE / "out" / "loot_sids.txt"
    loot_path.parent.mkdir(parents=True, exist_ok=True)
    loot_path.write_text("\n".join(sids) + "\n", encoding="utf-8")
    nums["b164off"] = "1"
    nums["qevent"] = "0"
    nums["qbranch"] = "0"
    nums["qdmir"] = "0"
    nums["qdroll1"] = "0"
    nums["trigjoin"] = "0"
    pre = ["flag %s %s" % (k, v) for k, v in sorted(nums.items())] + ["flags %s %s" % (k, v) for k, v in sorted(strs.items())]
    only = sys.argv[1:]
    fails = total = 0
    for sp in sorted((HERE / "scen").glob("*.txt")):
        if only and sp.stem not in only:
            continue
        total += 1
        scen = [x.rstrip("\r") for x in sp.read_text(encoding="utf-8").splitlines()]
        full = pre + scen
        WORK.mkdir(parents=True, exist_ok=True)
        fp = WORK / (sp.stem + ".full.txt")
        fp.write_text("\n".join(full) + "\n", encoding="utf-8")
        root = WORK / "run"
        fresh(root, True)
        try:
            lua_out = legacy_text(run_lua(core, bl, sids, full, root))
        except Exception as e:
            print("FAIL", sp.stem, "lua error", e)
            fails += 1
            continue
        lf = files(root)
        ld = WORK / (sp.stem + ".lua")
        if ld.exists():
            shutil.rmtree(ld)
        root.rename(ld)
        (ld / "run.log").write_text(lua_out, encoding="utf-8")
        fresh(root, False)
        r = subprocess.run([str(EXE), str(fp), str(root), str(PORT / "data"), str(loot_path)], capture_output=True, text=True)
        if r.returncode != 0:
            print("FAIL", sp.stem, "cpp error", r.stderr)
            fails += 1
            continue
        cpp_out = (root / "run.log").read_text(encoding="utf-8")
        cf = files(root)
        cd = WORK / (sp.stem + ".cpp")
        if cd.exists():
            shutil.rmtree(cd)
        root.rename(cd)
        a = [x for x in lua_out.split("\n") if not x.startswith("CMD flag")]
        b = [x for x in cpp_out.split("\n") if not x.startswith("CMD flag")]
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
        outs = sum(1 for x in a if x.startswith("OUT ") and x != "OUT ")
        if diff or fdiff:
            fails += 1
            print("FAIL", sp.stem)
            if diff:
                print("  line", diff[0])
                print("  lua:", diff[1][:400])
                print("  cpp:", diff[2][:400])
            for k in fdiff:
                print("  file differs:", k)
        else:
            print("PASS %s (%d log lines, %d calls, %d tag outputs, %d files identical)" % (sp.stem, logs, stubs, outs, len(lf)))
    print("%d/%d scenarios pass" % (total - fails, total))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
