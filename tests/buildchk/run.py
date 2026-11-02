import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(REPO, "tools"))
import gen_buildchk as G

RETAIL = G.EXE_DEFAULT
DEV = r"C:\S2DevRoot2\Stalker2\Binaries\Win64\Stalker2-Win64-Shipping.exe"
SYNTH_DIR = r"D:\modding\offline\gp1\synth"
SYNTH = os.path.join(SYNTH_DIR, "Stalker2-Win64-Shipping.exe")
EXE = os.path.join(HERE, "out", "test_buildchk.exe")

fails = 0


def check(ok, what):
    global fails
    print("%s %s" % ("PASS" if ok else "FAIL", what))
    if not ok:
        fails += 1


def run_cpp(path, want, first="-", sub=""):
    r = subprocess.run([EXE, "file", path, want, first, sub], capture_output=True, text=True, errors="replace")
    out = r.stdout + r.stderr
    for line in out.splitlines():
        if line.startswith(("PASS ", "FAIL ")):
            print("%s [cpp] %s" % (line[:4], line[5:]))
        elif line.startswith("  "):
            print(line)
    if r.returncode not in (0, 1):
        check(False, "test_buildchk.exe exit %d on %s" % (r.returncode, path))
    return out


def py_mismatch(pe, rows):
    bad = []
    for r in rows:
        buf = pe.read(r["at"], len(r["pat"]))
        if not G.tokens_match(r["pat"], buf):
            bad.append(r["name"])
    return bad


def main():
    if not os.path.isfile(EXE):
        check(False, "test_buildchk.exe not built")
        return
    data = open(RETAIL, "rb").read()
    g = G.generate(data)
    tsv = open(os.path.join(REPO, "data", "buildchk.tsv"), encoding="utf-8", newline="").read()
    inc = open(os.path.join(REPO, "src", "s2x_buildchk_tab.inc"), encoding="ascii", newline="").read()
    check(g["tsv"] == tsv, "data/buildchk.tsv is up to date with the retail exe, data/hooks.tsv and src (rerun tools/gen_buildchk.py)")
    check(g["inc"] == inc, "src/s2x_buildchk_tab.inc is up to date (rerun tools/gen_buildchk.py)")
    check(g["manifest"] == 90 and g["extra"] > 0, "table has the 90 hook manifest rows and %d source rows" % g["extra"])
    with tempfile.TemporaryDirectory() as td:
        t1, t2 = os.path.join(td, "buildchk.tsv"), os.path.join(td, "tab.inc")
        r = subprocess.run([sys.executable, "-I", os.path.join(REPO, "tools", "gen_buildchk.py"), "--quiet", "--out-tsv", t1, "--out-inc", t2], capture_output=True, text=True)
        same = r.returncode == 0 and open(t1, "rb").read() == tsv.encode("utf-8") and open(t2, "rb").read() == inc.encode("ascii")
        check(same, "generator CLI rerun to a temp dir gives the committed files byte for byte")
    rows = g["rows"]
    pe = G.Pe(data)
    check(py_mismatch(pe, rows) == [], "python matcher: all %d rows match the retail exe" % len(rows))
    run_cpp(RETAIL, "ok")
    if os.path.isfile(DEV):
        run_cpp(DEV, "ok")
    else:
        print("  note: dev exe %s missing, case not run" % DEV)
    os.makedirs(SYNTH_DIR, exist_ok=True)
    buf = bytearray(data)
    e = struct.unpack_from("<I", buf, 0x3C)[0]
    try:
        man = rows[:g["manifest"]]
        tgt = man[len(man) // 2]
        spans = [(r["at"], r["at"] + len(r["pat"]), r["name"]) for r in rows]
        k = next(i for i, t in enumerate(tgt["pat"]) if t != "??" and sum(1 for a, b, n in spans if a <= tgt["at"] + i < b) == 1)
        off = pe.section(tgt["at"])
        fo = off[3] + tgt["at"] - off[1] + k
        buf[fo] ^= 0x01
        open(SYNTH, "wb").write(buf)
        exp = py_mismatch(G.Pe(bytes(buf)), rows)
        check(exp == [tgt["name"]], "synth byte change: python sees only %s differ (%s)" % (tgt["name"], exp[:3]))
        run_cpp(SYNTH, "fail", tgt["name"], "(1 of %d signatures differ, first %s@%x)" % (len(rows), tgt["name"], tgt["at"]))
        buf[fo] ^= 0x01

        ts = struct.unpack_from("<I", buf, e + 8)[0]
        struct.pack_into("<I", buf, e + 8, ts ^ 0x00010000)
        open(SYNTH, "wb").write(buf)
        run_cpp(SYNTH, "fail", "-", "FAIL game build %s %08x != %s %08x (Steam build %s) (0 of %d signatures differ)" % (g["ver"], ts ^ 0x00010000, g["ver"], ts, g["build"], len(rows)))
        struct.pack_into("<I", buf, e + 8, ts)

        src = rows[g["manifest"] + len(rows[g["manifest"]:]) // 2]
        so = pe.section(src["at"])
        lo = so[3] + src["at"] - so[1] - 16
        n = 256
        saved = bytes(buf[lo:lo + n + 1])
        buf[lo + 1:lo + n + 1] = saved[:n]
        buf[lo] = 0xCC
        open(SYNTH, "wb").write(buf)
        exp = py_mismatch(G.Pe(bytes(buf)), rows)
        check(src["name"] in exp, "synth shift: python sees %s@%x differ (%d rows: %s)" % (src["name"], src["at"], len(exp), exp[:4]))
        if exp:
            run_cpp(SYNTH, "fail", exp[0], "(%d of %d signatures differ, first %s@" % (len(exp), len(rows), exp[0]))
        buf[lo:lo + n + 1] = saved

        tv = next(s for s in pe.sections if s[0] == ".text")
        so2 = e + 24 + struct.unpack_from("<H", buf, e + 20)[0]
        for i in range(len(pe.sections)):
            if buf[so2 + 40 * i:so2 + 40 * i + 5] == b".text":
                struct.pack_into("<I", buf, so2 + 40 * i + 8, tv[2] + 0x10)
        open(SYNTH, "wb").write(buf)
        run_cpp(SYNTH, "fail", "-", "text %x != " % (tv[2] + 0x10))
    finally:
        if os.path.exists(SYNTH):
            os.remove(SYNTH)
    check(not os.path.exists(SYNTH), "synthetic copy deleted")


if __name__ == "__main__":
    main()
    print("buildchk run.py: %d fail" % fails)
    sys.exit(1 if fails else 0)
