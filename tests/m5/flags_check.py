import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(os.path.dirname(HERE)), "src", "s2m_world.cpp")
FLAGS = os.path.join(HERE, "build", "flags.txt")
NEW = {"dobjfast": 1, "dobjfsec": 0.15, "dobjfmiss": 1, "mcworld": 1, "lockbrk": 1, "qobjsync": 1}
SITE = {"dobjnear": 120, "dobjquiet": 6, "dobjrot": 10, "doorfix": 1, "ienecho": 10}


def main():
    lua = {}
    with open(FLAGS, encoding="utf-8") as f:
        for ln in f:
            k, v = ln.split()
            lua[k] = float(v)
    with open(SRC, encoding="utf-8") as f:
        text = f.read()
    seen = {}
    bad = 0
    for m in re.finditer(r'flag\("(\w+)",\s*(-?[0-9.]+)\)', text):
        k, v = m.group(1), float(m.group(2))
        seen.setdefault(k, set()).add(v)
    for k in sorted(seen):
        vals = seen[k]
        if k in NEW:
            want = {float(NEW[k])}
            tag = "new"
        elif k in SITE:
            want = {float(SITE[k])}
            tag = "callsite"
        elif k in lua:
            want = {lua[k]}
            tag = "lua"
        else:
            print("MISSING in Lua FLAGS: %s %s" % (k, sorted(vals)))
            bad += 1
            continue
        if vals != want:
            print("DEFAULT MISMATCH %s: cpp=%s %s=%s" % (k, sorted(vals), tag, sorted(want)))
            bad += 1
    print("FLAGS %s checked=%d" % ("OK" if not bad else "FAIL", len(seen)))
    sys.exit(1 if bad else 0)


main()
