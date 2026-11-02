import contextlib
import io
import json
import os
import random
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\tools\flight"
sys.path.insert(0, TOOL)
import flightmerge as fm  # noqa: E402

OUT = os.path.join(HERE, "out")
FAILS = []
PASSES = []


def check(ok, name, detail=""):
    (PASSES if ok else FAILS).append(name)
    print(("PASS " if ok else "FAIL ") + name + ("" if ok else " " + str(detail)))


def fnv(s):
    h = 2166136261
    for c in s.encode("latin-1"):
        h ^= c
        h = (h * 16777619) & 0xFFFFFFFF
    return h


class Writer:
    def __init__(self, role, host, pid, sid, wall_err, start, tracked="", net_pid=-1, cap=4096, freq=10000000):
        self.role, self.host, self.pid, self.sid, self.err, self.start = role, host, pid, sid, wall_err, start
        self.tracked, self.net_pid, self.cap, self.freq = tracked, net_pid, cap, freq
        self.qpc0 = 123456789 + pid * 1000
        self.recs = []

    def put(self, t_true, kind, fam, tag, seq=0, h=0, size=0, aux=0, peer=-1, text=""):
        qpc = self.qpc0 + int(round((t_true - self.start) * self.freq))
        self.recs.append((qpc, int((t_true + self.err) * 1e6), seq, h, size, aux, peer, fm.KIND_IDS[kind], fm.FAMS.index(fam), tag, text))

    def save(self, path, reason="test"):
        b = bytearray(fm.HDR_SIZE + self.cap * fm.REC_SIZE)
        n = len(self.recs)
        hdr = fm.HDR.pack(fm.MAGIC, 1, fm.HDR_SIZE, fm.REC_SIZE, self.cap, n, self.freq, self.qpc0, int((self.start + self.err) * 1e6), self.pid, 1, 1, self.net_pid, 0, 0,
                          self.role.encode(), self.sid.encode(), self.host.encode(), b"test", reason.encode(), self.tracked.encode())
        b[: len(hdr)] = hdr
        for i, r in enumerate(self.recs):
            if i < n - self.cap:
                continue
            slot = i % self.cap
            v = fm.REC.pack(i + 1, r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7], r[8], b"\0\0\0", r[9].encode(), r[10].encode()[:75])
            b[fm.HDR_SIZE + slot * fm.REC_SIZE: fm.HDR_SIZE + (slot + 1) * fm.REC_SIZE] = v
        with open(path, "wb") as fh:
            fh.write(b)


def scenario(folder, handled_bug, offset=3.217, seed=7):
    rnd = random.Random(seed)
    shutil.rmtree(folder, ignore_errors=True)
    os.makedirs(folder)
    T0 = 1729500000.0
    host = Writer("host", "PCC", 1111, "1729500000-11", 0.0, T0, tracked="QB,QN", net_pid=0)
    cli = Writer("client", "PCD", 2222, "1729500000-22", offset, T0 + 0.4, tracked="QB,QN", net_pid=1)
    for k in range(0, 60):
        t = T0 + 1.0 + k * 0.8
        f = (k + 1) * 8
        lat = 0.02 + rnd.random() * 0.04
        host.put(t, "tx_snap", "net", "snap", seq=f, size=900, text="f=%d x1=600B" % f)
        cli.put(t + lat, "rx_snap", "net", "snap", seq=f, h=fnv(host.sid), size=900, peer=0, text="sid=%s f=%d t=%.3f" % (host.sid, f, t - T0))
        lat2 = 0.02 + rnd.random() * 0.04
        t2 = t + 0.37
        cli.put(t2, "tx_snap", "net", "snap", seq=f, size=700, text="f=%d x1=400B" % f)
        host.put(t2 + lat2, "rx_snap", "net", "snap", seq=f, h=fnv(cli.sid), size=700, peer=1, text="sid=%s f=%d t=%.3f" % (cli.sid, f, t2 - T0))

    def send(t, tag, val, handled):
        host.put(t, "tx", "net", tag, seq=int(t * 10) % 100000, h=fnv(val), size=len(val), peer=1, text=val)
        cli.put(t + 0.045, "rx", "net", tag, seq=int(t * 10) % 100000, h=fnv(val), size=len(val), peer=0, text=val)
        if handled:
            cli.put(t + 0.09, "handled", "net", tag, h=fnv(val), size=len(val), text=val)

    send(T0 + 5.0, "QN", "12;MainQuest_01;3", True)
    send(T0 + 10.0, "QB", "1729500000;1:a1b2c3d4:0:1:3;", not handled_bug)
    send(T0 + 20.0, "QB", "1729500000;1:a1b2c3d4:0:1:3|2:00ff00ff:0:0:1;", not handled_bug)
    send(T0 + 30.0, "W", "4;rain", False)
    host.put(T0 + 30.0, "tx", "net", "QB", h=fnv("x"), text="x")
    host.put(T0 + 30.1, "tx", "net", "QB", h=fnv("y"), text="y")
    cli.put(T0 + 30.2, "rx", "net", "QB", h=fnv("y"), text="y")
    cli.put(T0 + 30.25, "handled", "net", "QB", h=fnv("y"), text="y")
    host.put(T0 + 12.0, "log", "quest", "qbranch", text="NOLA qbranch: host rec 1")
    cli.put(T0 + 12.5, "log", "lock", "doorlock", text="NOLA doorlock: x")
    host.save(os.path.join(folder, "fr_host_1111.frd"))
    cli.save(os.path.join(folder, "fr_client_2222.ring"))
    return host, cli


def run_cli(args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = fm.main(args)
    return rc, buf.getvalue()


def main():
    wrap = os.path.join(OUT, "wrap.frd")
    if not os.path.exists(wrap):
        check(False, "C++ fixture out\\wrap.frd exists (run test_fr.exe first)")
    else:
        h, recs, torn = fm.read_dump(wrap)
        check(h["cap"] == 1024 and h["next"] == 2600 and len(recs) == 1024 and torn == 0, "dump format: C++ wrapped ring decodes 1024 valid records", (h["cap"], h["next"], len(recs), torn))
        ns = sorted(r["n"] for r in recs)
        check(ns[0] == 1577 and ns[-1] == 2600, "dump format: oldest/newest record numbers", (ns[0], ns[-1]))
        last = max(recs, key=lambda r: r["n"])
        check(last["text"] == "rec 2599" and last["tag"] == "w" and last["seq"] == 2599 and last["hash"] == 2599 * 7 and last["fam"] == fm.FAMS[2599 % 16],
              "dump format: record fields round-trip C++ -> Python", last)
        check(h["role"] == "host" and h["sid"] == "1729500000-77" and h["host"] == "TESTPC" and h["reason"] == "wrap_test" and h["tracked"] == ["QB"] and h["pid"] == 4242,
              "dump format: header fields round-trip", h)
        data = bytearray(open(wrap, "rb").read())
        struct.pack_into("<Q", data, fm.HDR_SIZE + 5 * fm.REC_SIZE, 0)
        bad = os.path.join(OUT, "torn.frd")
        open(bad, "wb").write(data)
        h2, recs2, torn2 = fm.read_dump(bad)
        check(torn2 == 1 and len(recs2) == 1023, "dump format: a record caught mid-write (n=0) is dropped as torn", (torn2, len(recs2)))
        os.remove(bad)

    bug = os.path.join(OUT, "qx1_bug")
    fix = os.path.join(OUT, "qx1_fixed")
    scenario(bug, True)
    scenario(fix, False)
    srcs, info, idx = fm.prepare([bug], 10.0)
    cli = [s for s in srcs if s.hdr["role"] == "client"][0]
    check(info["method"] == "snap pairs" and info["pairs"] == 120, "clock alignment uses the send/receive snapshot pairs", info)
    check(abs(cli.offset - 3.217) < 0.01, "clock alignment: client wall clock 3.217 s ahead recovered within 10 ms", cli.offset)
    for off in (-7.5, 0.0, 0.9):
        sub = os.path.join(OUT, "align_%s" % off)
        scenario(sub, False, offset=off, seed=11)
        s2, i2, _ = fm.prepare([sub], 10.0)
        c2 = [s for s in s2 if s.hdr["role"] == "client"][0]
        check(abs(c2.offset - off) < 0.01, "clock alignment: offset %+.3f s recovered" % off, c2.offset)
        shutil.rmtree(sub, ignore_errors=True)

    qs = fm.never_handled(srcs, idx, 10.0, 1.0)
    names = sorted((q["query"], q["tag"]) for q in qs)
    check(names == [("sent, never handled", "QB"), ("sent, never handled", "QB")], "never-handled query: both QB values the client got but never handled (QX1 class)", names)
    check(all(q["from"].startswith("host@") and q["to"].startswith("client@") for q in qs), "never-handled query names host -> client")
    rc, out = run_cli(["query", bug])
    check(rc == 2 and "sent, never handled" in out and "QB" in out, "query CLI exits 2 on the QX1 dump", out[-300:])
    rc, out = run_cli(["query", fix])
    check(rc == 0 and "0 finding(s)" in out, "query CLI exits 0 when every QB was handled", out[-300:])
    rc, out = run_cli(["query", "--json", bug])
    check(rc == 2 and len(json.loads(out)["queries"]) == 2, "query --json for scripts")

    mo = os.path.join(OUT, "merged")
    shutil.rmtree(mo, ignore_errors=True)
    rc, out = run_cli(["merge", bug, "--out", mo])
    tj = json.load(open(os.path.join(mo, "timeline.json"), encoding="utf-8"))
    ev = tj["traceEvents"]
    procs = sorted(e["args"]["name"] for e in ev if e["ph"] == "M" and e["name"] == "process_name")
    threads = {(e["pid"], e["args"]["name"]) for e in ev if e["ph"] == "M" and e["name"] == "thread_name"}
    xs = [e for e in ev if e["ph"] == "X"]
    flows = [e for e in ev if e["ph"] in ("s", "f")]
    qmarks = [e for e in ev if e["ph"] == "i" and e["cat"] == "query"]
    total = sum(len(s.recs) for s in srcs)
    check(rc == 0 and procs == ["client@PCD:2222", "host@PCC:1111"], "Perfetto JSON: one process per PC", procs)
    check((1, "quest") in threads and (2, "lock") in threads and (1, "net") in threads, "Perfetto JSON: one track per family")
    check(len(xs) == total, "Perfetto JSON: every record is a slice", (len(xs), total))
    check(len(flows) >= 2 * 5 and len(flows) % 2 == 0, "Perfetto JSON: send -> receive flow arrows", len(flows))
    check(len(qmarks) == 2, "Perfetto JSON: query findings marked on the receiver", len(qmarks))
    txt = open(os.path.join(mo, "timeline.txt"), encoding="utf-8").read().splitlines()
    body = [l for l in txt if not l.startswith("#")]
    check(len(body) == total, "text view: one line per record", (len(body), total))
    ts = [float(l.split()[0]) for l in body]
    check(ts == sorted(ts), "text view is sorted by aligned time")
    qb_tx = [l for l in body if " TX " in l and " QB " in l and "1:a1b2c3d4:0:1:3;" in l]
    qb_rx = [l for l in body if " RX " in l and " QB " in l and "1:a1b2c3d4:0:1:3;" in l]
    d = float(qb_rx[0].split()[0]) - float(qb_tx[0].split()[0]) if qb_tx and qb_rx else -1
    check(0.0 < d < 0.1, "text view: after alignment the receive follows the send by the link latency", d)
    qtxt = open(os.path.join(mo, "queries.txt"), encoding="utf-8").read()
    check(qtxt.count("sent, never handled") == 2, "queries.txt lists the findings")
    rc, out = run_cli(["events", bug, "--tag", "QB", "--kind", "rx", "--json"])
    check(rc == 0 and len(json.loads(out)) == 3, "events CLI filter for scripts (tag + kind, JSON)")
    print("%d passed, %d failed" % (len(PASSES), len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
