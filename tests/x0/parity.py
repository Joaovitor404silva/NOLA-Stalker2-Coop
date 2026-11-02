import os
import random
import re
import subprocess
import sys
import tempfile
from pathlib import Path

from lupa import lua54

HERE = Path(__file__).resolve().parent
EXE = HERE / "out" / "test_x0.exe"
MAIN = HERE / "lua" / "main.lua"
CORE = HERE / "lua" / "s2core.lua"
LIVE = [Path("C:/s2coop_inst/A/flags.txt"), Path("C:/s2coop_inst/B/flags.txt")]


def run(*args):
    r = subprocess.run([str(EXE), *map(str, args)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        raise SystemExit("exe failed: %s %s" % (args, r.stderr))
    return r.stdout.splitlines()


def lua_block(src, start_pat):
    lines = src.splitlines()
    for i, l in enumerate(lines):
        if l.startswith(start_pat):
            for j in range(i + 1, len(lines)):
                if lines[j] == "end":
                    return "\n".join(lines[i:j + 1])
    raise SystemExit("not found: " + start_pat)


def lua_literal(kind, s):
    if kind == "s":
        return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'
    if kind == "i":
        return "math.tointeger(" + s + ")"
    if s in ("inf", "-inf"):
        return ("" if s == "inf" else "-") + "math.huge"
    return "(" + s + (".0" if re.fullmatch(r"-?\d+", s) else "") + ")"


def flags_parity():
    defaults = {}
    for ln in run("defaults"):
        k, kind, s, lk = ln.split("\t")
        defaults[k] = (kind, s, lk == "1")
    loader = lua_block(MAIN.read_text(encoding="utf-8", errors="replace"), "function S2_loadFlags(quiet)")
    texts = []
    for p in LIVE:
        if p.exists():
            texts.append((p.name + ":" + p.parent.name, p.read_bytes().decode("latin-1")))
    texts.append(("edges", "\ufeffbeacon=0\r\n  hz = 12.5  \n#natnpc=1\nscanr=1e3\nmaxdist=0x10\nrotate=.5\nleashm=5.\nnatnpc=abc\n"
                           "autobridge=1 # comment\navatarmode=mesh-x.1\ndifficulty=Hard\nsyncgap=-0\nsyncthresh=-0.0\nshotdmg=1e400\n"
                           "npcdmgtype=nan\nwxtest=9007199254740993\nmaxnpcs=99999999999999999999\ncsave=\nqsync:1\nK=1\nnatbr =\t1\n"))
    rnd = random.Random(227)
    keys = [k for k, v in defaults.items() if v[2]]
    lkeys = set(keys)
    xlogs = []
    vals = ["1", "0", "-1", "1.5", "1e3", "0x10", ".5", "5.", "abc", "-", "1-2", "2", "3.25", "inf", "1e400", "00012", "-0", "0.1", "1E2", "0X1F", "1.0", "x1", "ON"]
    for n in range(300):
        lines = []
        for _ in range(rnd.randint(1, 25)):
            k = rnd.choice(keys)
            v = rnd.choice(vals)
            sp = rnd.choice(["", " ", "\t", "  "])
            pre = rnd.choice(["", "", "", " ", "#", "\ufeff"])
            lines.append(pre + k + sp + "=" + sp + v + rnd.choice(["", "", " # c", "\r"]))
        texts.append(("fuzz%d" % n, "\n".join(lines) + rnd.choice(["", "\n"])))
    bad = 0
    tmp = Path(tempfile.mkdtemp(prefix="s2x_par_"))
    for name, text in texts:
        fp = tmp / "flags.txt"
        fp.write_bytes(text.encode("utf-8") if not name.startswith("flags.txt") else text.encode("latin-1"))
        L = lua54.LuaRuntime(unpack_returned_tuples=True)
        init = ["FL = {}", "LOGS = {}", "function log(s) LOGS[#LOGS + 1] = s end", 'BR = [[' + str(tmp) + '\\]]']
        for k, (kind, s, lk) in defaults.items():
            if lk:
                init.append('FL["%s"] = %s' % (k, lua_literal(kind, s)))
        L.execute("\n".join(init))
        L.execute(loader)
        L.execute("S2_loadFlags(true)")
        lfl = L.eval("FL")
        llogs = list(L.eval("LOGS").values())
        out = run("flags", fp, "all")
        cv, clogs = {}, []
        for ln in out:
            p = ln.split("\t")
            if p[0] == "V":
                cv[p[1]] = (p[2], p[3])
            elif p[0] == "L" and p[1].startswith("AVATAR flags:"):
                if p[1].split(" ")[2] in lkeys:
                    clogs.append(p[1])
                else:
                    xlogs.append(p[1])
        for k in keys:
            lv = lfl[k]
            lt = L.eval("math.type")(lv) if isinstance(lv, (int, float)) else None
            lkind = "s" if isinstance(lv, str) else ("i" if lt == "integer" else "f")
            ls = lv if isinstance(lv, str) else L.eval("tostring")(lv)
            if cv.get(k) != (lkind, ls):
                bad += 1
                if bad <= 20:
                    print("FLAG MISMATCH %s %s: lua=%s %s cpp=%s" % (name, k, lkind, ls, cv.get(k)))
        if llogs != clogs:
            bad += 1
            if bad <= 20:
                print("LOG MISMATCH %s:\n lua=%s\n cpp=%s" % (name, llogs, clogs))
    for f in tmp.iterdir():
        f.unlink()
    tmp.rmdir()
    for ln in sorted(set(xlogs)):
        print("C++-only key, no Lua line: " + ln)
    print("flags parity: %d texts, %d keys, %d mismatches" % (len(texts), len(keys), bad))
    return bad


def num_parity():
    lits = ["0", "1", "-1", "1.5", "1e3", "1E-2", "0x10", "0XfF", ".5", "5.", "-0", "-0.0", "1e400", "-1e400", "inf", "nan", "00012", "9007199254740993",
            "99999999999999999999", "9223372036854775807", "9223372036854775808", "-9223372036854775808", "0.1", "1/3", "abc", "-", "1-2", "", "0x", "0x1p4",
            "0x.8", "1e", "e1", "123456789012345", "1234567890123456", "3.14159265358979", "2.5e-7", "1e15", "1e16", "100.25", "1e+5", "0x7fffffffffffffff", "0xffffffffffffffff"]
    tmp = Path(tempfile.mkdtemp(prefix="s2x_num_"))
    fp = tmp / "n.txt"
    fp.write_text("\n".join(l for l in lits if l) + "\n", encoding="ascii")
    L = lua54.LuaRuntime()
    tonum, tostr, mtype = L.eval("tonumber"), L.eval("tostring"), L.eval("math.type")
    bad = 0
    for ln in run("num", fp):
        lit, kind, s = ln.split("\t")
        v = tonum(lit)
        if v is None:
            exp = ("n", "nil")
        else:
            exp = ("i" if mtype(v) == "integer" else "f", tostr(v))
        if exp != (kind, s):
            bad += 1
            print("NUM MISMATCH %r lua=%s cpp=%s" % (lit, exp, (kind, s)))
    fp.unlink()
    tmp.rmdir()
    print("number parity: %d literals, %d mismatches" % (len([l for l in lits if l]), bad))
    return bad


def netrel_lua():
    src = CORE.read_text(encoding="utf-8", errors="replace")
    a = src.index("S2L.NRTAG = {")
    b = src.index("\nend\n", src.index("function S2L.nrSess(")) + 5
    c = src.index("function S2L.nrCount(t)")
    d = src.index("S2L.nrSnap0 = M.snapExtra")
    return src[a:b] + "\n" + src[c:d]


STEP = """
ST = {A = {me = {nonce = 'aa01'}}, B = {me = {nonce = 'bb02'}}}
function step(side, op, t, pid, ok, s, rx)
    CLOCK = t
    local X = ST[side]
    if pid == '-' then X.peer = nil else X.peer = {id = pid, state = ok == '1' and 'up' or 'silent', live = true} end
    S2.state = X
    LOGS = {}
    local r
    if op == 'out' then S2.io.head = '{"x1":"' .. rx .. '"}'; r = S2L.nrOut(X, s) else r = S2L.nrX1(s) end
    return r, LOGS
end
"""


def netrel_parity():
    L = lua54.LuaRuntime(unpack_returned_tuples=True)
    L.execute("CLOCK = 0; os.clock = function() return CLOCK end; LOGS = {}; function log(s) LOGS[#LOGS + 1] = s end; S2L = {}; S2 = {io = {}}; M = {}; "
              "FL = {netrel = 1, netrelmax = 120, netrelhold = 6, netrelseen = 300, netrelper = 40, x1c = 0, nrdoor = 0}")
    L.execute(netrel_lua())
    L.execute(STEP)
    step = L.eval("step")
    rnd = random.Random(1227)
    cmds = []
    pidA, pidB = "-", "-"
    lastA, lastB = "", ""
    seqA = seqB = 0
    pend = {"A": {}, "B": {}}
    up = True
    t = 0.0
    for i in range(3000):
        t = round(t + 0.1, 3)
        if i == 20:
            pidA, pidB = "s1/bb02", "s1/aa01"
        if i == 1500:
            pidB = "s2/aa01"
        gap = (300 <= i < 360) or (900 <= i < 990) or (2000 <= i < 2100) or rnd.random() < 0.08
        silent = (900 <= i < 990)
        for side in ("A", "B"):
            P = pend[side]
            if rnd.random() < 0.05:
                tag = rnd.choice(["J", "QN", "QP", "T", "G", "U", "DR", "O", "Y"])
                P["%s%d" % (tag, i)] = (tag, "%s%d.%d" % (side.lower(), i, rnd.randint(0, 9)), i + rnd.randint(1, 30))
            for k in [k for k, v in P.items() if v[2] <= i]:
                del P[k]
            tags = {}
            for tag, e, _ in P.values():
                tags.setdefault(tag, []).append(e)
            s = "".join("#%s:%s" % (tg, "|".join(es)) for tg, es in tags.items())
            if rnd.random() < 0.5:
                s = "#H:%s,v37,c,Medium,%s" % ("host" if side == "A" else "client", "aa01" if side == "A" else "bb02") + s + "#P:%d" % i
            pid = pidA if side == "A" else pidB
            ok = "0" if (silent and pid != "-") else "1"
            rx = lastB if side == "A" else lastA
            cmds.append((side, "out", "%.3f" % t, pid, ok, "aa01" if side == "A" else "bb02", s, rx))
            r, _ = step(side, "out", t, pid, ok, s, rx)
            if not gap:
                if side == "A":
                    lastA = r
                else:
                    lastB = r
            other = "B" if side == "A" else "A"
            opid = pidB if side == "A" else pidA
            ook = "0" if (silent and opid != "-") else "1"
            cmds.append((other, "in", "%.3f" % (t + 0.01), opid, ook, "bb02" if side == "A" else "aa01", lastA if side == "A" else lastB, ""))
            step(other, "in", t + 0.01, opid, ook, lastA if side == "A" else lastB, "")
    L2 = lua54.LuaRuntime(unpack_returned_tuples=True)
    L2.execute("CLOCK = 0; os.clock = function() return CLOCK end; LOGS = {}; function log(s) LOGS[#LOGS + 1] = s end; S2L = {}; S2 = {io = {}}; M = {}; "
               "FL = {netrel = 1, netrelmax = 120, netrelhold = 6, netrelseen = 300, netrelper = 40, x1c = 0, nrdoor = 0}")
    L2.execute(netrel_lua())
    L2.execute(STEP)
    step2 = L2.eval("step")
    exp = []
    for c in cmds:
        r, logs = step2(c[0], c[1], float(c[2]), c[3], c[4], c[6], c[7])
        exp.append("R\t" + r)
        exp.extend("L\t" + l.replace("S2CORE", "NOLA") for l in logs.values())
    tmp = Path(tempfile.mkdtemp(prefix="s2x_nr_"))
    fp = tmp / "cmds.txt"
    fp.write_text("\n".join("\t".join(c) for c in cmds) + "\n", encoding="utf-8", newline="\n")
    got = run("netrel", fp)
    fp.unlink()
    tmp.rmdir()
    bad = 0
    for i in range(max(len(exp), len(got))):
        e = exp[i] if i < len(exp) else "<none>"
        g = got[i] if i < len(got) else "<none>"
        if e != g:
            bad += 1
            if bad <= 10:
                print("NETREL MISMATCH line %d\n lua=%s\n cpp=%s" % (i, e[:300], g[:300]))
    resent = sum(1 for e in exp if "reenviado" in e)
    rin = sum(1 for e in exp if "recebido de novo" in e)
    print("netrel parity: %d commands, %d output lines, %d resend logs, %d re-received logs, %d mismatches" % (len(cmds), len(exp), resent, rin, bad))
    return bad


if __name__ == "__main__":
    total = num_parity() + flags_parity() + netrel_parity()
    print("PARITY", "PASS" if total == 0 else "FAIL %d" % total)
    sys.exit(1 if total else 0)
