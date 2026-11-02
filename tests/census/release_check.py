import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent.parent
SRC = PORT / "src"
GUARD_ON = ("!defined(S2X_RELEASE)", "#ifndef S2X_RELEASE")
WORDS = re.compile(r"census|cen::|CenNat|CenRow|NOLA INV|NOLA WH|fr_trigger|frtrig|lootclose|devinv|devwh")

ok = []
bad = []


def guarded_lines(text):
    stack = []
    out = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith("#if"):
            stack.append(any(g in s for g in GUARD_ON))
            out.append((line, True))
            continue
        if s.startswith("#else") or s.startswith("#elif"):
            if stack:
                stack[-1] = False
            out.append((line, True))
            continue
        if s.startswith("#endif"):
            if stack:
                stack.pop()
            out.append((line, True))
            continue
        out.append((line, any(stack)))
    return out


cen = (SRC / "s2m_census.cpp").read_text(encoding="utf-8")
first = cen.lstrip().splitlines()[0]
if first.startswith("#if") and "!defined(S2X_RELEASE)" in first and cen.rstrip().endswith("#endif"):
    ok.append("s2m_census.cpp is empty in S2X_RELEASE builds")
else:
    bad.append("s2m_census.cpp is not wrapped in #if !defined(S2X_RELEASE)")

skip = {"s2m_census.cpp", "s2m_census_logic.h", "s2m_census_inv.inc", "s2x_fr.h"}
files = 0
for p in sorted(list(SRC.glob("*.cpp")) + list(SRC.glob("*.h")) + list(SRC.glob("*.inc"))):
    if p.name in skip:
        continue
    text = p.read_text(encoding="utf-8", errors="replace")
    if not WORDS.search(text) and "s2m_census_logic.h" not in text:
        continue
    files += 1
    for n, (line, g) in enumerate(guarded_lines(text), 1):
        s = line.strip()
        if s.startswith("#define NOLA_CEN_EV") or "NOLA_CEN_EV(" in s and not s.startswith("#"):
            continue
        if (WORDS.search(line) or "s2m_census_logic.h" in line) and not g:
            bad.append("%s:%d unguarded: %s" % (p.name, n, s[:100]))
ok.append("%d source files with census code, all guarded" % files)

items_h = (SRC / "s2m_items.h").read_text(encoding="utf-8")
if re.search(r"#else\s*\n\s*#define NOLA_CEN_EV\(f, w, k\) \(\(void\)0\)", items_h):
    ok.append("NOLA_CEN_EV is a no-op in release")
else:
    bad.append("NOLA_CEN_EV has no ((void)0) release branch")

cm = (SRC / "CMakeLists.txt").read_text(encoding="utf-8")
if '"s2m_census.cpp"' in cm:
    ok.append("s2m_census.cpp is in the build")
else:
    bad.append("s2m_census.cpp missing from CMakeLists")

r = subprocess.run([sys.executable, "-I", str(PORT / "tools" / "gen_census_inv.py"), "--check"], capture_output=True, text=True)
if r.returncode == 0:
    ok.append("s2m_census_inv.inc matches data/invariants.tsv")
else:
    bad.append("gen_census_inv.py --check: " + (r.stdout + r.stderr).strip()[:200])

for s in ok:
    print("ok   " + s)
for s in bad:
    print("FAIL " + s)
print("%s %d/%d" % ("PASS" if not bad else "FAIL", len(ok), len(ok) + len(bad)))
sys.exit(1 if bad else 0)
