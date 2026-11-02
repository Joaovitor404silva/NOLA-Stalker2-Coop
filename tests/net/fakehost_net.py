import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "out", "net_tool.exe")
RELAY = os.path.join(HERE, "fixtures", "relay_v2.py")
TOOLS = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\tools"
SCEN = os.path.join(TOOLS, "fakehost_scenarios", "01_catchup_first_join.txt")
BASE_B = os.path.join(HERE, "fixtures", "B_out.json")
WORK = os.path.join(HERE, "out", "fakehost")


def one(kind):
    d = os.path.join(WORK, kind)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    m = json.load(open(BASE_B, encoding="utf-8"))
    args = ["--listen", "27941", "--peer", "127.0.0.1:27942", "--dir", d, "--seconds", "9"]
    cmd = [TOOL, "run"] + args if kind == "cpp" else [sys.executable, "-I", RELAY] + args
    r = subprocess.Popen(cmd, stdout=open(os.path.join(d, "relay.log"), "w"), stderr=subprocess.STDOUT)
    fh = subprocess.Popen([sys.executable, "-I", os.path.join(TOOLS, "fakehost.py"), "--net", "--peer", "127.0.0.1:27941", "--listen", "27942", "--scenario", SCEN, "--seconds", "7", "--log", os.path.join(d, "fakehost.log")],
                          stdout=open(os.path.join(d, "fakehost.out"), "w"), stderr=subprocess.STDOUT, cwd=TOOLS)
    seen = set()
    t0 = time.time()
    f = 0
    while time.time() - t0 < 7.5:
        m["f"] = 5000 + f
        m["t"] = round(f * 0.1, 3)
        f += 1
        tmp = os.path.join(d, "out.json.tmp")
        open(tmp, "w", encoding="utf-8").write(json.dumps(m, separators=(",", ":")))
        os.replace(tmp, os.path.join(d, "out.json"))
        try:
            seen.add(json.load(open(os.path.join(d, "in.json"), encoding="utf-8")).get("f"))
        except (OSError, ValueError):
            pass
        time.sleep(0.1)
    fh.wait(timeout=20)
    r.wait(timeout=20)
    log = open(os.path.join(d, "relay.log")).read()
    fo = open(os.path.join(d, "fakehost.out")).read()
    return len(seen), log, fo


for k in ("py", "cpp"):
    n, log, fo = one(k)
    st = [l for l in log.splitlines() if l.startswith("relay stats")]
    print("%s relay: in.json distinct f from fakehost = %d; %s; bad lines %d; fakehost tail: %s" % (k, n, st[-1] if st else "no stats", sum("RELAY ERROR" in l for l in log.splitlines()), fo.strip().splitlines()[-1:] if fo.strip() else ""))
