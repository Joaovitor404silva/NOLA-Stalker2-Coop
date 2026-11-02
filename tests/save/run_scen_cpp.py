import sys, subprocess, tempfile
from pathlib import Path
here = Path(__file__).resolve().parent
exe = here / "out" / "test_save.exe"
loot = here / "out" / "loot_sids.txt"
bless = "--bless" in sys.argv
only = [a for a in sys.argv[1:] if not a.startswith("--")]
fails = 0
for sp in sorted((here / "scen_cpp").glob("*.txt")):
    if only and sp.stem not in only:
        continue
    root = Path(tempfile.mkdtemp(prefix="s2scen_"))
    (root / "B").mkdir()
    (root / "saved" / "STEAM" / "SaveGames" / "Data").mkdir(parents=True)
    r = subprocess.run([str(exe), str(sp), str(root), str(loot)], capture_output=True, text=True)
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
        if exp is not None:
            a, b = exp.split("\n"), out.split("\n")
            for i in range(max(len(a), len(b))):
                x = a[i] if i < len(a) else "<end>"
                y = b[i] if i < len(b) else "<end>"
                if x != y:
                    print("  line", i, "\n  exp:", x, "\n  got:", y)
                    break
    else:
        print("PASS", sp.stem)
    print("   log:", root / "run.log")
sys.exit(1 if fails else 0)
