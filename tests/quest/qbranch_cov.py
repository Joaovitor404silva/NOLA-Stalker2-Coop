import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
PORT = HERE.parent.parent
DATA = PORT / "data"
GEN = PORT / "tools" / "gen_qbranch.py"
GD = Path(os.environ.get("S2_GD", r"D:\modding\offline\up5\zk_new\Stalker2\Content\GameLite\GameData"))
FAMILIES = ["quest", "radio", "forced", "nopins", "infotopic", "dlgstart", "hub", "trade"]
GATED = ["E01_MQ01_Dialog_Pripoy_in_Bunker_skif_interested_43517", "E01_MQ01_Dialog_Pripoy_in_Bunker_skif_knows_36652"]
HELD = "E01_MQ01_SetDialog_E01_MQ01_Dialog_Herman_ArtOrDetector"

fails = []
if not GD.exists():
    print("SKIP: missing external dependency " + str(GD))
    sys.exit(77)


def check(ok, what):
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        fails.append(what)


def rows(p):
    return [ln.rstrip("\n").split("\t") for ln in p.read_text(encoding="utf-8").splitlines() if ln.strip()]


tab = DATA / "s2u_qbranch.tsv"
cov = DATA / "s2u_qbranch_cov.tsv"
if GD.is_dir():
    work = HERE / "out" / "work" / "qbranch_cov"
    work.mkdir(parents=True, exist_ok=True)
    t2, c2 = work / "s2u_qbranch.tsv", work / "s2u_qbranch_cov.tsv"
    r = subprocess.run([sys.executable, "-I", str(GEN), str(GD), str(t2), str(c2)], capture_output=True, text=True)
    check(r.returncode == 0, "gen_qbranch.py runs over GameData")
    if r.returncode == 0:
        check(t2.read_bytes() == tab.read_bytes(), "data/s2u_qbranch.tsv matches a fresh run of gen_qbranch.py")
        check(c2.read_bytes() == cov.read_bytes(), "data/s2u_qbranch_cov.tsv matches a fresh run of gen_qbranch.py")
else:
    print("SKIP GameData not found: table freshness not checked")

T = rows(tab)
C = rows(cov)[1:]
A = {r[1]: r for r in T if r[0] == "A"}
N = {r[1]: r for r in T if r[0] == "N"}
check(all(len(r) == 4 and r[1] and r[2] and r[3] for r in T if r[0] == "A"), "every A row has phrase, dialog and choice point")
check(all(len(r) == 5 and len(r[2]) == 1 for r in T if r[0] == "N"), "every N row has node, kind, quest and pins")
check(all(len(r) == 7 and r[4] in ("gate", "free") for r in C), "every answer at every choice point is classified gate or free")
gate = {r[3] for r in C if r[4] == "gate"}
free = {r[3] for r in C if r[4] == "free"}
check(gate == set(A), "the gated answers in the coverage report are exactly the A rows")
check(all(r[5] != "-" for r in C if r[4] == "gate"), "no answer without a quest effect is gated")
bypt = defaultdict(list)
for r in C:
    bypt[(r[1], r[2])].append(r)
check(all(len({x[6] for x in v}) > 1 for v in bypt.values() if any(x[4] == "gate" for x in v)), "a choice point is gated only when its answers differ in quest effect")
fam = defaultdict(int)
for r in C:
    for f in r[0].split(","):
        fam[f] += 1
for f in FAMILIES:
    check(fam[f] > 0, "dialog family %s is covered (%d answers)" % (f, fam[f]))
check(all(r[4] == "free" for r in C if r[0] in ("hub", "trade") and r[5] == "-"), "trade and info answers stay free")
for g in GATED:
    check(g in A, "radio dialog answer %s is gated" % g)
check(HELD in N and N[HELD][2] == "D", "Herman ArtOrDetector exits are held for the host")
check(not any(r[2] == HELD for r in A.values()), "Herman ArtOrDetector has no player choice to gate")
print("%d answers classified (%d gated, %d free), %d families" % (len(C), len(gate), len(free), len(fam)))
sys.exit(1 if fails else 0)
