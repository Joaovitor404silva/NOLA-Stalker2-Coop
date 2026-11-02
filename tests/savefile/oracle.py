import sys, os, json
spec = sys.argv[1]
src = sys.argv[2]
dst = sys.argv[3]
sys.path.insert(0, spec)
import transplant
pairs = [l.split() for l in open(sys.argv[4]) if l.strip() and not l.startswith("#")]
os.makedirs(dst, exist_ok=True)
for name, h, d, flags in pairs:
    q = os.path.join(dst, name + ".raw")
    if os.path.exists(q):
        print(name, "cached")
        continue
    hb = open(os.path.join(src, h), "rb").read()
    db = open(os.path.join(src, d), "rb").read()
    ob, rep = transplant.build(hb, db, keep_pos="p" not in flags, vitals="v" not in flags)
    open(q, "wb").write(ob)
    print(name, len(ob), "names", len(rep["names_added"]), "dropped", len(rep["dropped_host_slots"]), "log", len(rep["log"]))
