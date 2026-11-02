import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
FX = os.path.join(HERE, "fixtures")
TOOL = os.path.join(HERE, "worlddiff.py")

ok = []
bad = []


def run(*args):
    r = subprocess.run([sys.executable, "-I", TOOL] + list(args), capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def expect(name, cond, out=""):
    (ok if cond else bad).append(name if cond else name + "\n" + out[-800:])


host = os.path.join(FX, "fixture_host.json")
client = os.path.join(FX, "fixture_client.json")
tmp = tempfile.mkdtemp(prefix="worlddiff_")

rc, out = run("check")
expect("check rules + invariants", rc == 0 and "19 rules" in out, out)

rc, out = run("run", host, host, "--out", tmp)
expect("host vs itself has no divergence", rc == 0 and "SUMMARY 0 divergences" in out, out)

rc, out = run("run", host, client, "--out", tmp)
expect("fixture diff exits 1", rc == 1, out)
expect("SUMMARY counts", "SUMMARY 6 divergences (errors 5, warn 1)" in out, out)
for want in ("client_extra     u555", "field locked     100,200,30", "host_missing     Q_Main_B",
             "client_extra     BP_Box_C@520,30,1", "field items      BP_Box_C@520,30,1",
             "loot_autoclose", "unpaired_visible_npc"):
    expect("finds " + want.split()[-1] + " " + want.split()[0], want in out, out)
expect("writes worlddiff.tsv", os.path.exists(os.path.join(tmp, "worlddiff.tsv")) and os.path.exists(os.path.join(tmp, "worlddiff.txt")))

rc, out = run("run", host, client, "--out", tmp, "--fail-on", "never") if False else (None, None)

rc, out = run("logs", os.path.join(FX, "wh_host.log"), os.path.join(FX, "wh_client.log"))
expect("logs exits 1 on divergence", rc == 1, out)
expect("FIRST DIVERGENCE quest", "FIRST DIVERGENCE quest" in out and "1760000041200" in out, out)
expect("npc one-sample blip ignored", "FIRST DIVERGENCE npc" not in out, out)
expect("INV line listed", "INV wh_client.log:7 unpaired_visible_npc" in out, out)

rc, out = run("logs", os.path.join(FX, "wh_host.log"), os.path.join(FX, "wh_host.log"))
expect("logs host vs itself clean", "FIRST DIVERGENCE" not in out, out)

for s in ok:
    print("ok   " + s)
for s in bad:
    print("FAIL " + s)
print("%s %d/%d" % ("PASS" if not bad else "FAIL", len(ok), len(ok) + len(bad)))
sys.exit(1 if bad else 0)
