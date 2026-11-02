import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import parity

parity.need([parity.CORE, parity.BRIDGE])

bless = "--bless" in sys.argv
only = [a for a in sys.argv[1:] if not a.startswith("--")]
nums, strs = parity.flag_defaults()
pre = ["flag %s %s" % (k, v) for k, v in sorted(nums.items())] + ["flags %s %s" % (k, v) for k, v in sorted(strs.items())]
loot_path = HERE / "out" / "loot_sids.txt"
if not loot_path.exists():
    sys.exit("run parity.py first (it writes out/loot_sids.txt)")
fails = total = 0
for sp in sorted((HERE / "scen_mc").glob("*.txt")):
    if only and sp.stem not in only:
        continue
    total += 1
    scen = [x.rstrip("\r") for x in sp.read_text(encoding="utf-8").splitlines()]
    work = parity.WORK
    work.mkdir(parents=True, exist_ok=True)
    fp = work / (sp.stem + ".mc.txt")
    fp.write_text("\n".join(pre + scen) + "\n", encoding="utf-8")
    root = work / ("mc_" + sp.stem)
    parity.fresh(root, False)
    r = subprocess.run([str(parity.EXE), str(fp), str(root), str(parity.PORT / "data"), str(loot_path)], capture_output=True, text=True)
    if r.returncode != 0:
        print("FAIL", sp.stem, "cpp error", r.stderr)
        fails += 1
        continue
    out = "\n".join(x for x in (root / "run.log").read_text(encoding="utf-8").replace("\r\n", "\n").split("\n") if not x.startswith("CMD flag"))
    ep = sp.with_suffix(".expect")
    if bless:
        ep.write_text(out, encoding="utf-8")
        print("BLESS", sp.stem)
        continue
    exp = ep.read_text(encoding="utf-8").replace("\r\n", "\n") if ep.exists() else None
    if exp != out:
        fails += 1
        print("FAIL", sp.stem)
        if exp is not None:
            a, b = exp.split("\n"), out.split("\n")
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else "<end>"
                y = b[i] if i < len(b) else "<end>"
                if x != y:
                    print("  line", i, "\n  exp:", x[:300], "\n  got:", y[:300])
                    break
    else:
        print("PASS", sp.stem, "(%d log lines)" % sum(1 for x in out.split("\n") if x.startswith("LOG ")))
print("%d/%d mc scenarios pass" % (total - fails, total))
sys.exit(1 if fails else 0)
