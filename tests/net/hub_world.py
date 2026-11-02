import json
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "out", "net_tool.exe")
BASE = os.path.join(HERE, "fixtures", "A_out.json")
WORK = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix="s2hubw_")
fails = []


def check(name, ok, detail=""):
    print("%s %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)
    if not ok:
        fails.append(name)


def fresh(d):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def run(role, listen, peer, d, seconds, extra=()):
    args = [TOOL, "run", "--role", role, "--listen", str(listen), "--peer", "127.0.0.1:%d" % peer, "--dir", d, "--seconds", str(seconds)] + list(extra)
    return subprocess.Popen(args, stdout=open(os.path.join(d, "log.txt"), "w"), stderr=subprocess.STDOUT)


def roster(d):
    try:
        return json.load(open(os.path.join(d, "roster.json"), encoding="utf-8"))
    except (OSError, ValueError):
        return None


def case(name, world, port):
    h = fresh(os.path.join(WORK, name, "host"))
    c = fresh(os.path.join(WORK, name, "client"))
    frame = json.load(open(BASE, encoding="utf-8"))
    with open(os.path.join(c, "out.json"), "w", encoding="utf-8") as f:
        json.dump(frame, f)
    extra = ["--max", "4"] + (["--world", world] if world else [])
    ph = run("host", port, port + 1, h, 6, extra)
    pc = run("client", port + 1, port, c, 6)
    seen = None
    t0 = time.time()
    while time.time() - t0 < 5.5:
        r = roster(c)
        if r and r.get("you", -1) >= 1:
            seen = r
            if not world or r.get("w") == world:
                break
        time.sleep(0.1)
    ph.wait()
    pc.wait()
    return seen, roster(h)


r, hr = case("up", "4,3", 28110)
check("client admitted by the hub", bool(r) and r.get("you", -1) >= 1, json.dumps(r))
check("client roster carries the host world 'epoch,state'", bool(r) and r.get("w") == "4,3", json.dumps(r))
check("hub roster.json carries the same world", bool(hr) and hr.get("w") == "4,3", json.dumps(hr))
r, hr = case("none", "", 28120)
check("host without a world value: client admitted", bool(r) and r.get("you", -1) >= 1, json.dumps(r))
check("host without a world value: no 'w' field (old hosts and parity unchanged)", bool(r) and "w" not in r, json.dumps(r))
print("%d failed" % len(fails))
sys.exit(1 if fails else 0)
