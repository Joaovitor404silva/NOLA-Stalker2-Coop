import difflib
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MOD = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork"
CORE = MOD + r"\s2coop\Mods\S2CoopAlife\Scripts\s2core.lua"
BRIDGE = MOD + r"\release\S2Coop\bridge"
DATA = os.path.join(ROOT, "data")
EXE = os.path.join(HERE, "build", "test_world.exe")
FLAGS = os.path.join(HERE, "build", "flags.txt")


LEGACY = {"S2CORE": "NOLA", "S2Coop_": "Nola_"}
LEGACY_RE = re.compile("|".join(re.escape(k) for k in LEGACY))


def norm_line(s):
    s = re.sub(r"ok=false .*$", "ok=false", s)
    if s.startswith("LOG NOLA b73:"):
        s = s.replace(".tsv", ".lua")
    if s.startswith("LOG NOLA ientab:"):
        s = re.sub(r"!.*?(?=,| \(now)", "!", s)
    return s


def blocks(text):
    out = [[]]
    for ln in text.split("\n"):
        ln = ln.rstrip("\r")
        if not ln or ln == "END":
            continue
        if ln.startswith("END fails") or ln.startswith("QUIET "):
            continue
        if ln.startswith("TICK"):
            out.append([ln])
        else:
            out[-1].append(norm_line(ln))
    return out


def run_lua(scn):
    r = subprocess.run(["uv", "run", "--no-project", "--with", "lupa", "python", "-I", os.path.join(HERE, "lua_run.py"), CORE, BRIDGE, scn],
                       capture_output=True, text=True, encoding="utf-8")
    if r.returncode != 0:
        sys.stdout.write(r.stdout + r.stderr)
    r.stdout = LEGACY_RE.sub(lambda m: LEGACY[m.group(0)], r.stdout)
    return r.stdout


def run_cpp(scn):
    r = subprocess.run([EXE, "run", scn, FLAGS, DATA], capture_output=True, text=True, encoding="utf-8")
    return r.stdout, r.returncode


def cpp_only(scn, show):
    cpp_out, rc = run_cpp(scn)
    for ln in cpp_out.split("\n"):
        if ln.startswith("EXPECT FAIL") or ln.startswith("BAD SCENARIO"):
            print("    " + ln)
    print("%s %s (C++ only)" % ("OK  " if rc == 0 else "FAIL", os.path.basename(scn)))
    if show:
        print(cpp_out)
    return rc == 0


def compare(scn, show):
    if os.path.basename(scn).startswith(("fast_", "mc_", "lock_")):
        return cpp_only(scn, show)
    lua_out = run_lua(scn)
    cpp_out, rc = run_cpp(scn)
    lb, cb = blocks(lua_out), blocks(cpp_out)
    bad = 0
    for i in range(max(len(lb), len(cb))):
        a = sorted(lb[i]) if i < len(lb) else ["<missing block>"]
        b = sorted(cb[i]) if i < len(cb) else ["<missing block>"]
        if a != b:
            bad += 1
            print("  tick block %d differs" % i)
            for d in difflib.unified_diff(a, b, "lua", "cpp", lineterm="", n=0):
                print("    " + d)
    expect_fail = rc != 0
    if expect_fail:
        print("  C++ expectations failed:")
        for ln in cpp_out.split("\n"):
            if ln.startswith("EXPECT FAIL") or ln.startswith("BAD SCENARIO"):
                print("    " + ln)
    lines = sum(len(x) for x in lb)
    print("%s %s lines=%d ticks=%d" % ("OK  " if not bad and not expect_fail else "FAIL", os.path.basename(scn), lines, len(lb) - 1))
    if show:
        print(lua_out)
    return not bad and not expect_fail


def main():
    args = sys.argv[1:]
    show = "--show" in args
    args = [a for a in args if a != "--show"]
    scns = args or sorted(glob.glob(os.path.join(HERE, "scenarios", "*.scn")))
    if not os.path.exists(FLAGS):
        subprocess.run(["uv", "run", "--no-project", "--with", "lupa", "python", "-I", os.path.join(HERE, "lua_run.py"), CORE, BRIDGE, "-", "--flags-out", FLAGS], check=True)
    ok = 0
    for s in scns:
        if compare(s, show):
            ok += 1
    print("%d/%d scenarios identical" % (ok, len(scns)))
    sys.exit(0 if ok == len(scns) else 1)


main()
