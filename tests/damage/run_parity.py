import difflib
import glob
import os
import re
import subprocess
import sys

from lupa import lua54

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.abspath(os.path.join(HERE, "..", "..", "data"))
MOD = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork"
CORE = MOD + r"\s2coop\Mods\S2CoopAlife\Scripts\s2core.lua"
EXE = os.path.join(HERE, "out", "test_damage.exe")
OUT = os.path.join(HERE, "out")

STARTS = [
    "local function dmgStr(ST)",
    "local function lifeStr(ST, pawn)",
    "function M.remoteLife()",
    "local function dmgApply(pawn, amount)",
    "local function dmgIn(ST)",
    "function S2L.downMsg(L, pw, now, off)",
    "local function lifeGuard(ST)",
    "function S2L.repUid(ST, pawn, best)",
    "function S2L.repAfter(ST, best, sid, h0, h1, okD)",
    "function S2L.foeUid(ST, pawn, best, dmg, h0)",
    "function S2L.relHost(ST, pawn)",
    "function S2L.relKind(ST, pawn, best)",
    "function S2L.repDeadTxt(o)",
    "function S2L.shHit(o, ox, oy, oz, dx, dy, dz)",
    "function M.shotResolve(ST, str, pawn)",
]
ONE_LINE = ["function M.remoteDown()"]


LEGACY = {"S2CORE": "NOLA", "S2Coop_": "Nola_"}
LEGACY_RE = re.compile("|".join(re.escape(k) for k in LEGACY))


def read(p):
    with open(p, "r", encoding="utf-8", errors="replace", newline="") as f:
        return f.read().replace("\r\n", "\n")


def chunks():
    lines = read(CORE).split("\n")
    parts = []
    for s in STARTS:
        i = next(k for k, l in enumerate(lines) if l.startswith(s))
        j = next(k for k in range(i + 1, len(lines)) if lines[k] == "end")
        parts.append("\n".join(lines[i : j + 1]))
    for s in ONE_LINE:
        parts.append(next(l for l in lines if l.startswith(s)))
    return "\n".join(parts)


def flags_table(lua):
    fl = lua.table()
    for name in ("flags_s2core.tsv", "flags_main.tsv"):
        rows = read(os.path.join(DATA, name)).split("\n")[1:]
        for r in rows:
            c = r.split("\t")
            if len(c) < 3 or not c[0]:
                continue
            k = c[0][1:]
            if c[1] == "i":
                fl[k] = int(c[2])
            elif c[1] == "f":
                fl[k] = float(c[2])
            else:
                fl[k] = c[2]
    return fl


def run_lua(scn, body):
    lua = lua54.LuaRuntime(unpack_returned_tuples=True)
    lua.globals().FL = flags_table(lua)
    lua.globals().SCENARIO = scn
    code = read(os.path.join(HERE, "twin_pre.lua")) + "\n" + body + "\n" + read(os.path.join(HERE, "twin_drv.lua"))
    try:
        out = lua.execute(code)
    except Exception as e:
        return "LUA ERROR %s\n" % e
    kept = "".join(l for l in out.splitlines(True) if "mensagem na tela:" not in l and "dica do parceiro na tela:" not in l)
    return LEGACY_RE.sub(lambda m: LEGACY[m.group(0)], kept)


def run_cpp(path):
    r = subprocess.run([EXE, DATA, path], capture_output=True, text=True, encoding="utf-8", timeout=120)
    if r.returncode != 0:
        return "EXIT %d\n%s%s" % (r.returncode, r.stdout, r.stderr)
    return r.stdout.replace("\r\n", "\n")


def main():
    body = chunks()
    os.makedirs(OUT, exist_ok=True)
    args = [x for x in sys.argv[1:] if not x.startswith("--")]
    names = args or sorted(os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(HERE, "scn", "*.txt")))
    bad = 0
    for n in names:
        p = os.path.join(HERE, "scn", n + ".txt")
        b = run_cpp(p)
        if n.startswith("cpp_"):
            g = os.path.join(HERE, "scn", n + ".expect")
            if "--bless" in sys.argv[1:] or not os.path.exists(g):
                with open(g, "w", encoding="utf-8", newline="\n") as f:
                    f.write(b)
            a = read(g)
        else:
            a = run_lua(read(p), body)
        with open(os.path.join(OUT, n + ".lua.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(a)
        with open(os.path.join(OUT, n + ".cpp.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(b)
        lines = a.count("\n")
        if a == b:
            print("PASS %-16s %d linhas" % (n, lines))
        else:
            bad += 1
            print("FAIL %s" % n)
            for d in difflib.unified_diff(a.split("\n"), b.split("\n"), "lua", "cpp", lineterm="", n=1):
                print("   " + d)
    print("RESULT %d/%d PASS" % (len(names) - bad, len(names)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
