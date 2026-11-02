import sys, subprocess, tempfile
from pathlib import Path
here = Path(__file__).resolve().parent
exe = here / "test_items.exe"
loot = here / "loot_sids.tsv"
qtsv = here.parent.parent / "data" / "s2u_qitems.tsv"
bless = "--bless" in sys.argv
only = [a for a in sys.argv[1:] if not a.startswith("--")]
fails = 0
for sp in sorted((here / "scen_cpp").glob("*.txt")):
    if only and sp.stem not in only:
        continue
    root = Path(tempfile.mkdtemp(prefix="s2items_"))
    r = subprocess.run([str(exe), str(sp), str(root) + "\\", str(loot), str(qtsv)], capture_output=True, text=True)
    out = (root / "run.log").read_text(encoding="utf-8").replace("\r\n", "\n")
    ep = sp.with_suffix(".expect")
    if bless:
        ep.write_text(out, encoding="utf-8")
        print("BLESS", sp.stem)
        continue
    exp = ep.read_text(encoding="utf-8").replace("\r\n", "\n") if ep.exists() else None
    if r.returncode != 0 or exp != out:
        fails += 1
        print("FAIL", sp.stem, r.returncode)
    else:
        print("PASS", sp.stem)
sys.exit(1 if fails else 0)
