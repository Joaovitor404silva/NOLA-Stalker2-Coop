import json
import os
import random
import socket
import struct
import subprocess
import sys
import tempfile
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
RELAY = os.path.normpath(os.path.join(HERE, "..", "..", "..", "tools", "relay"))
sys.path.insert(0, HERE)
sys.path.insert(0, RELAY)
import s2proto as P
import netcap
import ghostpeer as G

PY = [sys.executable, "-I"]
RES = []


def check(name, ok, detail=""):
    RES.append((name, bool(ok), detail))
    print("%s %s %s" % ("PASS" if ok else "FAIL", name, detail), flush=True)


def free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1", 0))
    p = s.getsockname()[1]
    s.close()
    return p


def t_netcap(d):
    path = os.path.join(d, "ring.ncap")
    w = netcap.Writer(path, 1 << 16, {"note": "t"})
    rnd = random.Random(5)
    sent = []
    for i in range(3000):
        b = bytes(rnd.getrandbits(8) for _ in range(rnd.randint(1, 300)))
        w.put(i % 2, b, ("127.0.0.1", 7000 + i % 3), i % 3, t=i * 0.01)
        sent.append(b)
    w.mark("END")
    w.close()
    c = netcap.read(path)
    recs = c["recs"]
    tail_ok = all(r.data == sent[r.seq - 1] for r in recs[:-1]) and recs[-1].dir == netcap.MARK and recs[-1].data == b"END"
    check("netcap ring wraps and keeps a contiguous tail", c["wrapped"] and c["closed"] and len(recs) > 100 and recs[-1].seq == 3001 and tail_ok, "%d recs kept of 3001, first seq %d" % (len(recs), recs[0].seq))
    with open(path, "r+b") as f:
        b = bytearray(f.read())
        mid = recs[len(recs) // 2]
        f.seek(0)
        buf = bytes(b)
    pos = buf.find(netcap.REC.pack(netcap.REC_MAGIC, len(mid.data), mid.seq, mid.t, mid.wall, mid.dir, mid.cli, mid.port, mid.ip, zlib.crc32(mid.data) & 0xFFFFFFFF))
    b[pos + netcap.REC.size] ^= 0xFF
    with open(path, "wb") as f:
        f.write(b)
    c2 = netcap.read(path)
    check("netcap crc: a corrupt record cuts the tail after it", c2["recs"][0].seq == mid.seq + 1 and c2["recs"][-1].seq == 3001, "first kept seq %d (corrupt %d)" % (c2["recs"][0].seq, mid.seq))


def t_tap(d):
    hub, tap = free_port(), free_port()
    cap = os.path.join(d, "tap.ncap")
    mf = os.path.join(d, "marks.txt")
    open(mf, "w").close()
    ph = subprocess.Popen(PY + [os.path.join(HERE, "fakehub.py"), "--listen", str(hub), "--bind", "127.0.0.1", "--seconds", "9"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    pt = subprocess.Popen(PY + [os.path.join(RELAY, "relay_v2.py"), "--tap", str(tap), "--upstream", "127.0.0.1:%d" % hub, "--cap", cap, "--capmb", "1", "--mark-file", mf, "--seconds", "9", "--note", "tap test"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(0.8)
    pp = subprocess.Popen(PY + [os.path.join(HERE, "fakepeer.py"), "--peer", "127.0.0.1:%d" % tap, "--listen", "0", "--bind", "127.0.0.1", "--seconds", "6", "--take-at", "-1", "--quest-at", "-1", "--send", "2:K:BP_Box_C@1,2,3"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(4)
    with open(mf, "a") as f:
        f.write("M1 four seconds\n")
    pp.wait(30)
    out_t = pt.communicate(timeout=30)[0].decode("utf-8", "replace")
    ph.communicate(timeout=30)
    c = netcap.read(cap)
    k = {}
    for r in c["recs"]:
        key = (netcap.DIRS[r.dir], P.NAMES.get(r.typ, str(r.typ)) if r.dir != netcap.MARK else "mark")
        k[key] = k.get(key, 0) + 1
    marks = [r.data for r in c["recs"] if r.dir == netcap.MARK]
    kseen = any(r.dir == netcap.C2H and r.typ == P.T_HDR and "#K:BP_Box_C" in P.unpack(r.data)[1].get("x1", "") for r in c["recs"])
    check("tap proxies fakepeer<->fakehub and captures both ways", k.get(("c2h", "HDR"), 0) > 20 and k.get(("h2c", "HDR"), 0) > 20 and kseen and c["closed"], json.dumps({"%s %s" % kk: v for kk, v in k.items()}))
    check("tap mark file line becomes a MARK record", marks == [b"M1 four seconds"], repr(marks))
    return out_t


def hdr(sid, f, x1, ids=()):
    m = P.snapshot(sid, f, f * 0.1, 100.0 + f, 200.0, 300.0, 0, 0, 0, 0, 0, 0, x1)
    m["ids"] = list(ids)
    m.pop("npcs", None)
    m.pop("apps", None)
    return P.pack(P.T_HDR, m)


def synth(d, pkg_size=50000):
    path = os.path.join(d, "synth.ncap")
    w = netcap.Writer(path, 8 << 20, {"note": "synthetic host session"})
    cli = ("127.0.0.1", 50001)
    host_sid, cli_sid = "H1111", "C2222"
    rnd = random.Random(9)
    pkg = bytes(rnd.getrandbits(8) for _ in range(pkg_size))
    crc = zlib.crc32(pkg) & 0xFFFFFFFF
    wid = 777000111
    t = 0.0
    w.put(netcap.C2H, P.pack(G.T_SXW, {"id": str(wid), "r": [[0, 63]]}), cli, 1, t)
    w.put(netcap.H2C, P.pack(G.T_SXI, {"id": str(wid), "st": "ok", "size": len(pkg), "crc": crc, "ch": 1100}), cli, 1, t + 0.01)
    n = (len(pkg) + 1099) // 1100
    for i in range(n):
        w.put(netcap.H2C, P.MAGIC + bytes([P.PROTO, G.T_SXC]) + struct.pack("<QI", wid, i) + pkg[i * 1100:(i + 1) * 1100], cli, 1, t + 0.02 + i * 0.001)
    w.put(netcap.H2C, P.pack(G.T_ROSTER, {"you": 1, "max": 4, "host": 0, "peers": [{"pid": 0, "sid": host_sid, "st": "in"}, {"pid": 1, "sid": cli_sid, "st": "in"}], "w": 1}), cli, 1, 0.5)
    w.put(netcap.H2C, hdr(host_sid, 1, "#H:host,m,c,Medium,hn1#AS:hn1.1"), cli, 1, 1.0)
    a1 = 3.0
    events = {50: "#K:BP_Box_C@1,2,3", 80: "#QN:Node_A c"}
    for f in range(1, 101):
        tt = a1 + (f - 1) * 0.1
        x1 = "#H:client,m,c,Medium,cn1#AS:cn1.%d#AK:hn1.%d" % (f, f)
        for ef, ev in events.items():
            if ef <= f < ef + 20:
                x1 += ev
        w.put(netcap.C2H, hdr(cli_sid, f, x1), cli, 1, tt)
        w.put(netcap.H2C, hdr(host_sid, f + 1, "#H:host,m,c,Medium,hn1#AS:hn1.%d#AK:cn1.%d" % (f + 1, f), ids=[5]), cli, 1, tt + 0.03)
        if f % 10 == 1:
            w.put(netcap.H2C, P.pack(P.T_NPC, {"sid": host_sid, "f": f + 1, "n": {"id": 5, "x": f, "y": 0, "z": 0}}), cli, 1, tt + 0.04)
        if f == 70:
            w.mark("M70", t=tt)
    w.put(netcap.H2C, P.pack(G.T_ROSTER, {"you": 1, "max": 4, "host": 0, "peers": [{"pid": 0, "sid": host_sid, "st": "in"}, {"pid": 1, "sid": cli_sid, "st": "in"}], "w": 2}), cli, 1, a1 + 5.0)
    w.put(netcap.H2C, P.pack(G.T_BYE, {"sid": host_sid}), cli, 1, a1 + 10.2)
    w.close()
    return path, pkg, a1


class LiveClient:
    def __init__(self, port, d, sid="LIVE9", nonce="ln7"):
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.bind(("127.0.0.1", 0))
        self.s.setblocking(False)
        self.dst = ("127.0.0.1", port)
        self.d = d
        self.sid, self.nonce = sid, nonce
        sys.path.insert(0, RELAY)
        import relay_v2
        self.sx = relay_v2.SaveXfer(d)
        self.rx = []

    def send(self, dg):
        self.s.sendto(dg, self.dst)

    def pump(self, now, t0):
        for data, _ in P.drain(self.s):
            self.rx.append((now - t0, data))
            if data[3] in (G.T_SXI, G.T_SXC):
                self.sx.feed(data, self.send)
        self.sx.tick(now, self.send)


def run_ghost(cap, port, extra, d, name):
    summ = os.path.join(d, name + ".json")
    logp = os.path.join(d, name + ".log")
    p = subprocess.Popen(PY + [os.path.join(HERE, "ghostpeer.py"), cap, "--listen", str(port), "--summary", summ, "--log", logp, "--client-silent", "3"] + extra, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)
    time.sleep(0.6)
    return p, summ, logp


def session(cap, d, name, extra, send_world_after=2.0, events=None, secs=14.0):
    port = free_port()
    p, summ, logp = run_ghost(cap, port, extra, d, name)
    sxd = os.path.join(d, name + "_sx")
    os.makedirs(sxd, exist_ok=True)
    with open(os.path.join(sxd, "sx_want.txt"), "w") as f:
        f.write("91792616075780\n")
    c = LiveClient(port, sxd)
    t0 = time.time()
    f = 0
    next_hdr = 0.0
    first_hdr_t = None
    while time.time() - t0 < secs:
        now = time.time()
        if f == 0 and now - t0 < 0.05:
            c.send(P.pack(G.T_SXW, {"id": "91792616075780", "r": [[0, 63]]}))
        c.pump(now, t0)
        el = now - t0
        if el >= send_world_after and now >= next_hdr:
            next_hdr = now + 0.1
            f += 1
            if first_hdr_t is None:
                first_hdr_t = el
            x1 = "#H:client,m,c,Medium,%s#AS:%s.%d" % (c.nonce, c.nonce, f)
            for (a, b), ev in (events or {}).items():
                if a <= el - send_world_after < b:
                    x1 += ev
            c.send(hdr(c.sid, f, x1))
        time.sleep(0.003)
    c.send(P.pack(G.T_BYE, {"sid": c.sid}))
    p.wait(20)
    with open(summ, encoding="utf-8") as fh:
        sm = json.load(fh)
    with open(logp, encoding="utf-8") as fh:
        lg = fh.read()
    return sm, lg, c, first_hdr_t, sxd


def pct(v, q):
    v = sorted(v)
    return v[min(len(v) - 1, int(q * len(v)))] if v else -1


def h2c_hdr_times(c, first):
    out = []
    for t, data in c.rx:
        if data[3] == P.T_HDR:
            o = P.unpack(data)[1]
            out.append((t - first, o["f"], o["x1"]))
    return out


def t_replay(d):
    cap, pkg, a1 = synth(d)
    rec = G.Recording(cap)
    inf = rec.info()
    check("recording: client, sids, anchors, package and events parsed", rec.sid == "C2222" and rec.nonce == "cn1" and abs(inf["world_after_s"] - 3.0) < 1e-6 and inf["packages"][0]["ok"] and [e[1] for e in rec.events] == ["K", "QN"], json.dumps({k: inf[k] for k in ("client_sid", "world_after_s", "packages", "client_events", "event_tags", "marks")}))
    sm, lg, c, first, sxd = session(cap, d, "s1", ["--no-hold", "--bye"], events={(4.9, 7.0): "#K:BP_Box_C@1,2,3", (7.9, 10.0): "#QN:Node_A c"})
    got = open(os.path.join(sxd, "sx_in.pkg"), "rb").read() if os.path.exists(os.path.join(sxd, "sx_in.pkg")) else b""
    check("SX: live request id served from the recorded package (relay_v2 SaveXfer client)", got == pkg and sm["sx"]["ids"] == ["91792616075780"], "got %d B of %d, sx %s" % (len(got), len(pkg), sm["sx"]))
    hs = h2c_hdr_times(c, first)
    host = [(t, f) for t, f, x in hs if f >= 2]
    errs = [abs(t - host[0][0] - (f - 2) * 0.1) for t, f in host] if host else []
    check("socket replay timing at scale 1 (loose under machine load; exact timing is the sim clock test): host snapshot k arrives (k-2)*0.1 s after snapshot 2", len(host) == 100 and pct(errs, 0.5) < 0.05 and pct(errs, 0.95) < 0.3, "n=%d p50 %.3f p95 %.3f max %.3f s" % (len(host), pct(errs, 0.5), pct(errs, 0.95), max(errs) if errs else -1))
    aks = [x for t, f, x in hs if f >= 2]
    okak = all(("#AK:ln7." in x) for x in aks[3:]) and not any("#AK:cn1." in x for x in aks)
    check("HDR #AK rewritten to the live client's latest #AS", okak, aks[5][-40:] if len(aks) > 5 else "")
    ros = [P.unpack(data)[1] for t, data in c.rx if data[3] == G.T_ROSTER and t > first + 0.5]
    check("ROSTER after the live client's first snapshot: recorded client sid rewritten to the live sid", ros and all(any(p["sid"] == "LIVE9" for p in r["peers"]) and not any(p["sid"] == "C2222" for p in r["peers"]) for r in ros), json.dumps(ros[:1]))
    check("no divergence when the client repeats the recorded events; recording ends", sm["divergence"] is None and sm["stopped"] == "end of recording" and sm["stream_sent"] == sm["stream_total"], "stopped=%s div=%s sent %d/%d" % (sm["stopped"], sm["divergence"], sm["stream_sent"], sm["stream_total"]))
    byes = sum(1 for t, data in c.rx if data[3] == G.T_BYE)
    check("recorded host BYE replayed only with --bye", byes == 1, "BYE x%d" % byes)

    sm2, lg2, c2, first2, _ = session(cap, d, "s2", ["--no-hold", "--scale", "2"], secs=32.0)
    hs2 = [(t, f) for t, f, x in h2c_hdr_times(c2, first2) if f >= 2]
    errs2 = [abs(t - hs2[0][0] - 2 * (f - 2) * 0.1) for t, f in hs2] if hs2 else []
    check("socket replay timing at scale 2 (twice as slow, loose)", len(hs2) == 100 and pct(errs2, 0.5) < 0.05 and pct(errs2, 0.95) < 0.3, "n=%d p50 %.3f p95 %.3f max %.3f s, last at %.2f s" % (len(hs2), pct(errs2, 0.5), pct(errs2, 0.95), max(errs2) if errs2 else -1, hs2[-1][0] if hs2 else -1))
    nb = sum(1 for t, data in c2.rx if data[3] == G.T_BYE)
    check("host BYE skipped by default", nb == 0, "BYE x%d" % nb)

    sm3, lg3, c3, first3, _ = session(cap, d, "s3", [], events={(2.0, 4.0): "#K:BP_Other_C@9,9,9"}, secs=9.0)
    dv = sm3["divergence"] or {}
    held = [t for t, f, x in h2c_hdr_times(c3, first3)]
    late = [t for t in held if t > 3.5]
    check("divergence: an unrecorded client value stops the replay", dv.get("tag") == "K" and dv.get("why") == "new value" and sm3["stopped"] == "divergence" and abs(dv.get("rel", -9) - 2.0) < 0.3, json.dumps(dv))
    check("hold after the stop: last host snapshot resent ~10 Hz", sm3["holds"] > 20 and len(late) > 15, "holds %d, hdr after 3.5 s: %d" % (sm3["holds"], len(late)))
    check("divergence logged in plain words", "GHOST DIVERGENCE client #K:BP_Other_C" in lg3, "")

    sm4, lg4, c4, first4, _ = session(cap, d, "s4", ["--stop-at-mark", "M70", "--no-hold"], secs=12.0)
    hs4 = [(t, f) for t, f, x in h2c_hdr_times(c4, first4) if f >= 2]
    check("--stop-at-mark stops at the recorded mark time", sm4["stopped"].startswith("stop-at 6.90") and hs4 and hs4[-1][1] == 70, "stopped=%s last host f=%s" % (sm4["stopped"], hs4[-1][1] if hs4 else None))

    sm5, lg5, c5, first5, _ = session(cap, d, "s5", ["--div", "log", "--no-hold"], events={(2.0, 4.0): "#K:BP_Other_C@9,9,9"}, secs=14.0)
    check("--div log: divergence recorded, replay continues to the end", (sm5["divergence"] or {}).get("tag") == "K" and sm5["stopped"] == "end of recording", "stopped=%s" % sm5["stopped"])


def t_simclock(d):
    cap, pkg, a1 = synth(d)
    rec = G.Recording(cap)
    out = {}
    for scale in (1.0, 2.0, 0.5):
        a = G.args([cap, "--scale", str(scale)])
        g = G.Ghost(rec, a, lambda m: None)
        addr = ("127.0.0.1", 1)
        t = 100.0
        g.feed(P.pack(G.T_SXW, {"id": "5", "r": [[0, 63]]}), addr, t)
        sent = []
        f = 0
        nh = 102.0
        while t < 100.0 + 40 * scale:
            if t >= nh:
                f += 1
                g.feed(hdr("LIVE9", f, "#H:client,m,c,Medium,ln7#AS:ln7.%d" % f), addr, t)
                nh += 0.1
            for dg, _ in g.tick(t):
                sent.append((t, dg))
            t += 0.001
        hs = [(tt - 102.0, P.unpack(dg)[1]["f"]) for tt, dg in sent if dg[3] == P.T_HDR]
        main = [(tt, ff) for tt, ff in hs if tt < 102.0 + 10.3 * scale - 102.0 and ff >= 2][:100]
        err = max(abs(tt - scale * ((ff - 2) * 0.1 + 0.03)) for tt, ff in main)
        p1 = [tt for tt, dg in sent if dg[3] == G.T_ROSTER]
        holds = [tt for tt, ff in hs if tt > 10.3 * scale]
        out[scale] = (len(main), err, g.stopped, len(holds), p1[0] if p1 else None)
        check("sim clock scale %.1f: 100 host snapshots at scale*(k*0.1+0.03) s after the client's first snapshot, then 10 Hz hold" % scale,
              len(main) == 100 and err < 0.0021 and g.stopped == "end of recording" and abs(len(holds) - 10 * (40 * scale - 2 - 10.3 * scale)) < 3,
              "n=%d max err %.4f s, stopped=%s, hold hdrs %d" % (len(main), err, g.stopped, len(holds)))
        check("sim clock scale %.1f: phase-1 roster at scale*(0.5) s after the client's first datagram" % scale, p1 and abs(p1[0] - 100.0 - scale * 0.5) < 0.002, "roster at +%.3f s" % (p1[0] - 100.0))


def t_hold_stamp(d):
    cap, pkg, a1 = synth(d)
    rec = G.Recording(cap)
    g = G.Ghost(rec, G.args([cap]), lambda m: None)
    g.last_hdr = hdr("HOST1", 7, "#AK:ln1.3#NN:t1000;b5,1,2;632,0,1;m632,b,1#S:x")
    g.live_as = "ln7.9"
    g.stop("test", 10.0)
    x = [x1 for x1 in [P.unpack(g.hold_hdr(t))[1]["x1"] for t in (10.0, 12.5)]]
    n0, n1 = G.x1_tags(x[0]).get("NN"), G.x1_tags(x[1]).get("NN")
    check("hold: NN host stamp advances with the hold time, one-shot bullets dropped, other NN entries and AK rewrite kept",
          n0 == "t1000;632,0,1;m632,b,1" and n1 == "t3500;632,0,1;m632,b,1" and G.x1_tags(x[1]).get("AK") == "ln7.9" and G.x1_tags(x[1]).get("S") == "x", "%s | %s | %s" % (n0, n1, x[1]))
    g2 = G.Ghost(rec, G.args([cap]), lambda m: None)
    g2.last_hdr = hdr("HOST1", 7, "#S:x")
    g2.stop("test", 10.0)
    check("hold: a snapshot without NN is resent unchanged apart from AK", G.x1_tags(P.unpack(g2.hold_hdr(15.0))[1]["x1"]) == {"S": "x"}, P.unpack(g2.hold_hdr(15.0))[1]["x1"])


def t_tap_replay(d):
    cap = os.path.join(d, "tap.ncap")
    rec = G.Recording(cap)
    inf = rec.info()
    check("recording from a real tap capture parses (fakehub/fakepeer)", inf["stream"] > 20 and inf["client_sid"] and "K" in inf["event_tags"], json.dumps({k: inf[k] for k in ("client_sid", "stream", "world_after_s", "event_tags", "marks")}))


def main():
    d = tempfile.mkdtemp(prefix="gp1_ghost_")
    print("tmp", d, flush=True)
    t_netcap(d)
    t_tap(d)
    t_tap_replay(d)
    t_simclock(d)
    t_hold_stamp(d)
    t_replay(d)
    bad = [r for r in RES if not r[1]]
    print("\n%d/%d PASS" % (len(RES) - len(bad), len(RES)), flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
