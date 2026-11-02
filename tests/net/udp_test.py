import json
import os
import random
import re
import shutil
import socket
import statistics
import subprocess
import sys
import threading
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "out", "net_tool.exe")
RELAY = os.path.join(HERE, "fixtures", "relay_v2.py")
BASE_A = os.path.join(HERE, "fixtures", "A_out.json")
BASE_B = os.path.join(HERE, "fixtures", "B_out.json")
WORK = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "out", "udp")
PORT0 = 27900
fails = []
REF = {}
GLUE = os.path.join(HERE, "out", "glue_test.exe")
report = []


def check(name, ok, detail=""):
    report.append("%s %s %s" % ("PASS" if ok else "FAIL", name, detail))
    print(report[-1], flush=True)
    if not ok:
        fails.append(name)


def fresh(d):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    return d


def relay(kind, listen, peer, d, log, seconds, extra=()):
    args = ["--listen", str(listen), "--peer", "127.0.0.1:%d" % peer, "--dir", d, "--seconds", str(seconds)] + list(extra)
    if kind == "py":
        cmd = [sys.executable, "-I", RELAY] + args
    else:
        cmd = [TOOL, "run"] + args
    return subprocess.Popen(cmd, stdout=open(log, "w"), stderr=subprocess.STDOUT)


def write_atomic(path, text):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="") as f:
        f.write(text)
    for _ in range(20):
        try:
            os.replace(tmp, path)
            return
        except PermissionError:
            time.sleep(0.002)


class Feeder(threading.Thread):
    def __init__(self, d, base, seed, n, npcs=True, apps=True, need=""):
        super().__init__(daemon=True)
        self.d, self.n = d, n
        self.rnd = random.Random(seed)
        self.base = json.load(open(base, encoding="utf-8"))
        self.base["need"] = need
        self.npcs_on, self.apps_on = npcs, apps
        self.sent = {}

    def frame(self, i):
        m = dict(self.base)
        m["f"] = 1000 + i
        m["t"] = round(10 + i * 0.1, 3)
        m["x"] = round(self.base["x"] + i * 2.5, 1)
        npcs = []
        if self.npcs_on:
            src = self.base.get("npcs") or [{"id": "n%d" % k, "x": 1.0 * k, "y": 2.0, "z": 3.0, "yaw": 0.0, "a": "%08x" % k, "s": k} for k in range(8)]
            for k, p in enumerate(src):
                q = dict(p)
                if k % 3 == 0:
                    q["x"] = round(q["x"] + i * 1.5, 1)
                if (i // 20) % 2 == 1 and k == len(src) - 1:
                    continue
                npcs.append(q)
        m["npcs"] = npcs
        m["apps"] = [{"a": "%08x" % (k * 7919), "mesh": "/Game/M_%d.M_%d" % (k, k), "parts": "x;y"} for k in range(4)] if self.apps_on else []
        return m

    def run(self):
        for i in range(self.n):
            txt = json.dumps(self.frame(i), separators=(",", ":"))
            write_atomic(os.path.join(self.d, "out.json"), txt)
            self.sent[1000 + i] = time.perf_counter()
            time.sleep(0.1)


class Reader(threading.Thread):
    def __init__(self, d):
        super().__init__(daemon=True)
        self.p = os.path.join(d, "in.json")
        self.stop = False
        self.seen = []

    def run(self):
        last = None
        while not self.stop:
            try:
                with open(self.p, "rb") as f:
                    b = f.read()
            except OSError:
                b = None
            if b and b != last:
                last = b
                self.seen.append((time.perf_counter(), b))
            time.sleep(0.004)


def settled(seen):
    by_f = {}
    first = {}
    for t, b in seen:
        try:
            m = json.loads(b)
        except ValueError:
            continue
        f = m.get("f")
        by_f[f] = b
        first.setdefault(f, t)
    return by_f, first


def pair_run(ka, kb, tag, seconds=6.0, frames=50, extra_a=(), extra_b=(), proxy=None):
    base = fresh(os.path.join(WORK, tag))
    da, db = fresh(os.path.join(base, "A")), fresh(os.path.join(base, "B"))
    pa, pb = PORT0 + 1, PORT0 + 2
    a_peer = pb
    if proxy:
        a_peer = PORT0 + 3
    ra = relay(ka, pa, a_peer, da, os.path.join(base, "a.log"), seconds, extra_a)
    rb = relay(kb, pb, pa, db, os.path.join(base, "b.log"), seconds, extra_b)
    px = None
    if proxy:
        px = Proxy(PORT0 + 3, pb, proxy)
        px.start()
    time.sleep(0.4)
    fa = Feeder(da, BASE_A, 1, frames, need="")
    fb = Feeder(db, BASE_B, 2, frames, npcs=False, apps=False, need="%08x,%08x,%08x" % (0, 7919, 99))
    rd = Reader(db)
    rd.start()
    rda = Reader(da)
    rda.start()
    fa.start()
    fb.start()
    fa.join()
    fb.join()
    time.sleep(0.5)
    rd.stop = True
    rd.join()
    rda.stop = True
    rda.join()
    ra.wait(timeout=seconds + 10)
    rb.wait(timeout=seconds + 10)
    if px:
        px.stop = True
    by_f, first = settled(rd.seen)
    lat = [first[f] - fa.sent[f] for f in first if f in fa.sent]
    by_f_a, _ = settled(rda.seen)
    return dict(by_f_a=by_f_a, by_f=by_f, first=first, lat=lat, seen=rd.seen, alog=open(os.path.join(base, "a.log")).read(), blog=open(os.path.join(base, "b.log")).read(), dir_a=da, dir_b=db)


class Proxy(threading.Thread):
    def __init__(self, listen, to, jitter):
        super().__init__(daemon=True)
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", listen))
        self.s.settimeout(0.01)
        self.to, self.jitter = to, jitter
        self.stop = False
        self.q = []
        self.rnd = random.Random(5)
        self.passed = 0

    def run(self):
        while not self.stop:
            try:
                d, _ = self.s.recvfrom(65535)
                self.q.append((time.perf_counter() + self.rnd.uniform(0, self.jitter), d))
            except (socket.timeout, ConnectionResetError):
                pass
            now = time.perf_counter()
            due = [x for x in self.q if x[0] <= now]
            self.q = [x for x in self.q if x[0] > now]
            due.sort(key=lambda x: x[0])
            for _, d in due:
                self.s.sendto(d, ("127.0.0.1", self.to))
                self.passed += 1


def test_pairings():
    runs = {}
    for ka, kb in (("py", "py"), ("cpp", "cpp"), ("cpp", "py"), ("py", "cpp")):
        runs[(ka, kb)] = pair_run(ka, kb, "%s_%s" % (ka, kb))
    ref = runs[("py", "py")]["by_f"]
    REF["b"] = ref
    REF["a"] = runs[("py", "py")]["by_f_a"]
    for k, r in runs.items():
        common = [f for f in r["by_f"] if f in ref]
        same = [f for f in common if r["by_f"][f] == ref[f]]
        nf = len(r["by_f"])
        lat = sorted(r["lat"])
        med = statistics.median(lat) * 1000 if lat else -1
        p95 = lat[int(len(lat) * 0.95) - 1] * 1000 if lat else -1
        upd = len(r["seen"])
        ok = nf >= 20 and len(common) >= 20 and len(same) == len(common)
        check("udp %s->%s settled in.json byte-equal to py->py" % k, ok,
              "frames seen %d/50, common %d, equal %d; latency out.json->in.json median %.1f ms p95 %.1f ms; in.json updates %d" % (nf, len(common), len(same), med, p95, upd))
        hdr = re.findall(r"^relay v2 listen=\d+ peer=\S+ dir=.*$", r["alog"], re.M)
        check("start line format (%s)" % k[0], bool(hdr), hdr[0] if hdr else r["alog"][:200])


def test_loss_reorder():
    for kb in ("py", "cpp"):
        r = pair_run("py", kb, "loss_%s" % kb, seconds=11.5, frames=105, extra_b=("--loss", "0.3"))
        st = re.findall(r"^relay stats 10s: .*$", r["blog"], re.M)
        m = re.search(r"dropped_test=(\d+)", st[0]) if st else None
        fs = [json.loads(b).get("f") for _, b in r["seen"]]
        mono = all(fs[i] <= fs[i + 1] for i in range(len(fs) - 1))
        check("loss 0.3 on %s receiver" % kb, bool(m) and int(m.group(1)) > 0 and mono and len(r["by_f"]) > 25,
              "frames seen %d/105, f monotonic %s, stats: %s" % (len(r["by_f"]), mono, st[0] if st else "none"))
    for kb in ("py", "cpp"):
        r = pair_run("py", kb, "reorder_%s" % kb, seconds=11.5, frames=105, proxy=0.25)
        st = re.findall(r"^relay stats 10s: .*$", r["blog"], re.M)
        m = re.search(r"stale=(\d+)", st[0]) if st else None
        fs = [json.loads(b).get("f") for _, b in r["seen"]]
        mono = all(fs[i] <= fs[i + 1] for i in range(len(fs) - 1))
        check("reorder proxy 0-250 ms jitter into %s receiver" % kb, bool(m) and int(m.group(1)) > 0 and mono,
              "frames seen %d/105, f monotonic %s, stats: %s" % (len(r["by_f"]), mono, st[0] if st else "none"))


def test_stats_format():
    a = open(os.path.join(WORK, "loss_py", "B", "..", "b.log")).read()
    b = open(os.path.join(WORK, "loss_cpp", "B", "..", "b.log")).read()
    rx = re.compile(r"^relay stats 10s: sent=\d+ \(\d+ B, max \d+ B, over 1200 B: \d+\) recv=\d+ dropped_test=\d+ stale=\d+ in\.json=\d+ send_err=\d+ bad_local=\d+ bad_peer=\d+ apps_cached=\d+ npcs=\d+$", re.M)
    check("stats line format py/cpp identical shape", bool(rx.search(a)) and bool(rx.search(b)))


def sx_run(kh, kc, tag, loss, size, late_pkg):
    base = fresh(os.path.join(WORK, tag))
    dh, dc = fresh(os.path.join(base, "H")), fresh(os.path.join(base, "C"))
    ph, pc = PORT0 + 11, PORT0 + 12
    rnd = random.Random(size)
    body = bytes(rnd.getrandbits(8) for _ in range(size))
    pkg = b"S2SX v=1 id=4242 save=test\n" + body
    if not late_pkg:
        open(os.path.join(dh, "sx_out.pkg"), "wb").write(pkg)
    ex = ("--loss", str(loss)) if loss else ()
    rh = relay(kh, ph, pc, dh, os.path.join(base, "h.log"), 25, ex)
    rc = relay(kc, pc, ph, dc, os.path.join(base, "c.log"), 25, ex)
    time.sleep(0.3)
    open(os.path.join(dc, "sx_want.txt"), "w").write("4242\n")
    t0 = time.perf_counter()
    req = None
    done_t = None
    while time.perf_counter() - t0 < 23:
        if late_pkg and req is None and os.path.exists(os.path.join(dh, "sx_req.txt")):
            req = open(os.path.join(dh, "sx_req.txt"), "rb").read()
            time.sleep(0.5)
            tmp = os.path.join(dh, "sx_out.pkg.tmp")
            open(tmp, "wb").write(pkg)
            os.replace(tmp, os.path.join(dh, "sx_out.pkg"))
        p = os.path.join(dc, "sx_in.pkg")
        if os.path.exists(p) and done_t is None:
            done_t = time.perf_counter() - t0
            time.sleep(0.3)
            break
        time.sleep(0.02)
    rh.kill()
    rc.kill()
    rh.wait()
    rc.wait()
    got = open(os.path.join(dc, "sx_in.pkg"), "rb").read() if os.path.exists(os.path.join(dc, "sx_in.pkg")) else b""
    prog = open(os.path.join(dc, "sx_in.prog"), "rb").read() if os.path.exists(os.path.join(dc, "sx_in.prog")) else b""
    return dict(ok=got == pkg, t=done_t, req=req, prog=prog, clog=open(os.path.join(base, "c.log")).read(), hlog=open(os.path.join(base, "h.log")).read())


def test_sx():
    for kh, kc in (("cpp", "cpp"), ("py", "cpp"), ("cpp", "py")):
        r = sx_run(kh, kc, "sx_%s_%s" % (kh, kc), 0.2, 300000, True)
        msgs = re.findall(r"^relay sx: .*$", r["clog"] + r["hlog"], re.M)
        check("save transfer host=%s client=%s loss 0.2, host asked first (sx_req.txt) then pkg appears" % (kh, kc),
              r["ok"] and r["req"] == b"4242\r\n" and r["prog"] == b"4242 273/273 done\r\n",
              "pkg byte-equal %s in %s s, sx_req.txt=%r, sx_in.prog=%r, %d sx log lines" % (r["ok"], None if r["t"] is None else round(r["t"], 2), r["req"], r["prog"], len(msgs)))


def test_glue():
    base = fresh(os.path.join(WORK, "glue"))
    da, db = fresh(os.path.join(base, "A")), fresh(os.path.join(base, "B"))
    fa = Feeder(da, BASE_A, 1, 50, need="")
    fh = os.path.join(base, "frames.hex")
    with open(fh, "w") as f:
        for i in range(50):
            f.write(json.dumps(fa.frame(i), separators=(",", ":")).encode().hex() + chr(10))
    rb = relay("py", PORT0 + 22, PORT0 + 21, db, os.path.join(base, "b.log"), 9)
    time.sleep(0.3)
    outp = os.path.join(base, "glue_out.txt")
    g = subprocess.Popen([GLUE, str(PORT0 + 21), str(PORT0 + 22), fh, outp, da], stdout=open(os.path.join(base, "glue.log"), "w"), stderr=subprocess.STDOUT)
    fb = Feeder(db, BASE_B, 2, 50, npcs=False, apps=False, need="%08x,%08x,%08x" % (0, 7919, 99))
    rd = Reader(db)
    rd.start()
    fb.start()
    fb.join()
    g.wait(timeout=30)
    time.sleep(0.3)
    rd.stop = True
    rd.join()
    rb.wait(timeout=20)
    by_f, _ = settled(rd.seen)
    def nopid(b):
        d = json.loads(b)
        d.pop("pid", None)
        d.pop("need", None)
        d.pop("apps", None)
        d.pop("npcs", None)
        return d
    ref = REF["b"]
    common = [f for f in by_f if f in ref]
    same = [f for f in common if nopid(by_f[f]) == nopid(ref[f])]
    dkb = set()
    for f in common:
        x, y = nopid(by_f[f]), nopid(ref[f])
        dkb |= {k for k in set(x) | set(y) if x.get(k) != y.get(k)}
    sent, got = {}, []
    for ln in open(outp).read().split(chr(10)):
        p = ln.split(chr(9))
        if p[0] == "O":
            sent[1000 + int(p[1])] = float(p[2])
        elif p[0] == "I":
            got.append((float(p[1]), bytes.fromhex(p[2])))
    by_fa, first = settled(got)
    refa = REF["a"]
    ca = [f for f in by_fa if f in refa]
    sa = [f for f in ca if nopid(by_fa[f]) == nopid(refa[f])]
    dk = set()
    for f in ca:
        if nopid(by_fa[f]) != nopid(refa[f]):
            x, y = nopid(by_fa[f]), nopid(refa[f])
            dk |= {k for k in set(x) | set(y) if x.get(k) != y.get(k)}
    glog = open(os.path.join(base, "glue.log")).read()
    check("X0 glue mailbox mode (DLL side) against a real relay_v2.py: in.json on the Python side byte-equal", len(common) >= 20 and len(same) == len(common), "frames %d, common %d, equal %d, differing keys %s" % (len(by_f), len(common), len(same), sorted(dkb)))
    check("X0 glue mailbox mode: x0_in text byte-equal to the in.json relay_v2.py writes", len(ca) >= 20 and len(sa) == len(ca), "frames %d, common %d, equal %d, differing keys %s" % (len(by_fa), len(ca), len(sa), sorted(dk)))
    check("X0 glue flag switch cpp_net 0->1->0->1 and netfiles toggle", "GLUE OK" in glog and "relay v2 listen=" in glog, " | ".join(l for l in glog.splitlines() if l.startswith(("LOG S2NET", "STATUS", "OK", "FAIL", "GLUE"))))


def main():
    if not os.path.exists(TOOL):
        print("net_tool.exe missing: run build_test.bat")
        return 2
    os.makedirs(WORK, exist_ok=True)
    test_pairings()
    test_glue()
    test_loss_reorder()
    test_stats_format()
    test_sx()
    print("RESULT %s (%d fail)" % ("PASS" if not fails else "FAIL", len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
