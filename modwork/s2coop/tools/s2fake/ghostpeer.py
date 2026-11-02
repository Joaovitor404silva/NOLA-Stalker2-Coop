import argparse
import json
import os
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
RELAY = os.path.normpath(os.path.join(HERE, "..", "..", "..", "tools", "relay"))
sys.path.insert(0, RELAY)
import s2proto as P
import netcap

T_SXW, T_SXI, T_SXC, T_BYE, T_ROSTER, T_DISC, T_DISCR = 4, 5, 6, 7, 9, 11, 12
SX_WIN = 64
SERVED = (T_SXI, T_SXC)
DIV_IGNORE = {"H", "@W", "MV", "FH", "L", "AS", "AK", "RS"}


def x1_tags(x1):
    cur = {}
    for t, v in P.tags(x1):
        cur.setdefault(t, v)
    return cur


def set_tag(x1, tag, val):
    pat = "#" + tag + ":"
    b = x1.find(pat)
    if b < 0:
        return x1 if val is None else x1 + pat + val
    e = x1.find("#", b + len(pat))
    if e < 0:
        e = len(x1)
    return x1[:b] + ("" if val is None else pat + val) + x1[e:]


def hold_nn(v, add_ms):
    out = []
    for e in v.split(";"):
        if e[:1] == "t" and e[1:].isdigit():
            e = "t%d" % (int(e[1:]) + add_ms)
        elif e[:1] == "b":
            continue
        out.append(e)
    return ";".join(out)


class Item:
    __slots__ = ("t", "typ", "data", "phase", "npc")

    def __init__(self, t, typ, data, phase, npc=None):
        self.t, self.typ, self.data, self.phase, self.npc = t, typ, data, phase, npc


class Recording:
    def __init__(self, path, cli=None, table=None):
        cap = netcap.read(path)
        self.path = path
        self.meta = cap["meta"]
        self.wrapped = cap["wrapped"]
        recs = cap["recs"]
        clis = []
        for r in recs:
            if r.dir == netcap.C2H and r.cli not in clis:
                clis.append(r.cli)
        if not clis:
            raise ValueError("%s: no client datagram in the capture" % path)
        self.clients = clis
        self.cli = clis[0] if cli is None else cli
        self.c2h = [r for r in recs if r.dir == netcap.C2H and r.cli == self.cli]
        if not self.c2h:
            raise ValueError("%s: client %s sent nothing" % (path, self.cli))
        self.a0 = self.c2h[0].t
        hdrs = [r for r in self.c2h if r.typ == P.T_HDR]
        self.a1 = hdrs[0].t if hdrs else None
        self.base = self.a1 if self.a1 is not None else self.a0
        self.marks = [(r.t, r.data.decode("utf-8", "replace")) for r in recs if r.dir == netcap.MARK]
        self.h2c = [r for r in recs if r.dir == netcap.H2C and r.cli == self.cli and r.t >= self.a0]
        self.sid, self.nonce = None, None
        self.events = []
        self.values = {}
        self.event_tags = event_tags(table)
        last = {}
        for r in hdrs:
            try:
                _, o = P.unpack(r.data)
            except Exception:
                continue
            x1 = o.get("x1", "")
            if self.sid is None:
                self.sid = o.get("sid")
                h = P.hello(x1)
                self.nonce = h.get("nonce") if h else None
            cur = x1_tags(x1)
            for t in self.event_tags:
                v = cur.get(t)
                if v and v != last.get(t):
                    self.events.append((r.t - self.base, t, v))
                    self.values.setdefault(t, {}).setdefault(v, r.t - self.base)
                last[t] = v
        self.pkgs = {}
        self.pkg = None
        self.stream = []
        for r in self.h2c:
            typ = r.typ
            if typ == T_SXI:
                try:
                    _, o = P.unpack(r.data)
                except Exception:
                    continue
                if o.get("st") == "ok":
                    wid = str(o.get("id"))
                    if wid not in self.pkgs:
                        size, ch = int(o["size"]), int(o["ch"])
                        self.pkgs[wid] = {"id": wid, "size": size, "crc": int(o["crc"]), "ch": ch, "buf": bytearray(size), "got": set(), "n": (size + ch - 1) // ch, "t": r.t}
                continue
            if typ == T_SXC:
                if len(r.data) >= 16:
                    wid, i = struct.unpack_from("<QI", r.data, 4)
                    k = self.pkgs.get(str(wid))
                    if k and i < k["n"]:
                        k["buf"][i * k["ch"]:i * k["ch"] + len(r.data) - 16] = r.data[16:]
                        k["got"].add(i)
                continue
            if typ in (T_DISCR,):
                continue
            npc = None
            if typ == P.T_NPC:
                try:
                    _, o = P.unpack(r.data)
                    npc = str(o.get("n", {}).get("id"))
                except Exception:
                    npc = None
            ph = 1 if (self.a1 is not None and r.t < self.a1) else 2
            self.stream.append(Item(r.t, typ, r.data, ph, npc))
        for k in self.pkgs.values():
            k["ok"] = len(k["got"]) == k["n"] and (zlib.crc32(bytes(k["buf"])) & 0xFFFFFFFF) == k["crc"]
            if k["ok"] and (self.pkg is None or k["t"] > self.pkg["t"]):
                self.pkg = k

    def mark_t(self, name):
        for t, txt in self.marks:
            if txt == name or txt.split()[0] == name:
                return t - self.base
        return None

    def info(self):
        cnt = {}
        for r in self.c2h:
            k = "c2h " + P.NAMES.get(r.typ, str(r.typ))
            cnt[k] = cnt.get(k, 0) + 1
        for r in self.h2c:
            k = "h2c " + P.NAMES.get(r.typ, str(r.typ))
            cnt[k] = cnt.get(k, 0) + 1
        end = max([self.c2h[-1].t] + [r.t for r in self.h2c[-1:]])
        return {"path": self.path, "meta": self.meta, "wrapped": self.wrapped, "clients": self.clients, "cli": self.cli, "client_sid": self.sid, "client_nonce": self.nonce,
                "a0": round(self.a0, 3), "a1": None if self.a1 is None else round(self.a1, 3), "world_after_s": None if self.a1 is None else round(self.a1 - self.a0, 3),
                "length_s": round(end - self.base, 3), "counts": dict(sorted(cnt.items())), "stream": len(self.stream),
                "marks": [[round(t - self.base, 3), m] for t, m in self.marks],
                "packages": [{"id": k["id"], "size": k["size"], "chunks": "%d/%d" % (len(k["got"]), k["n"]), "ok": k["ok"]} for k in self.pkgs.values()],
                "client_events": len(self.events), "event_tags": sorted({e[1] for e in self.events})}


def event_tags(table=None):
    try:
        rows = table if table is not None else P.load_tag_table()
    except OSError:
        rows = {}
    s = {t for t, r in rows.items() if r["from"] in ("client", "both") and not t.startswith("@")}
    return sorted(s - DIV_IGNORE)


class Ghost:
    def __init__(self, rec, a, log):
        self.rec = rec
        self.a = a
        self.log = log
        self.scale = a.scale
        self.ignore = set(x for x in (a.div_ignore or "").split(",") if x)
        self.addr = None
        self.sid = None
        self.nonce = None
        self.live_as = None
        self.t0 = None
        self.t1 = None
        self.idx = 0
        self.stopped = None
        self.stop_rel = a.stop_at if a.stop_at is not None else None
        if a.stop_at_mark:
            mt = rec.mark_t(a.stop_at_mark)
            if mt is None:
                raise ValueError("mark %r not in the capture (marks: %s)" % (a.stop_at_mark, [m for _, m in rec.marks]))
            self.stop_rel = mt if self.stop_rel is None else min(self.stop_rel, mt)
        self.last_hdr = None
        self.last_roster = None
        self.last_npc = {}
        self.hold_hdr_at = 0.0
        self.hold_slow_at = 0.0
        self.stopped_at = None
        self.sent = {}
        self.holds = 0
        self.sx = {"wants": 0, "chunks": 0, "waits": 0, "ids": []}
        self.live_tags = {}
        self.live_events = []
        self.div = None
        self.client_rx = -1.0
        self.client_bye = False
        self.rx = {}

    def rel_now(self, now):
        if self.t1 is not None:
            return (now - self.t1) / self.scale
        if self.t0 is not None:
            return (now - self.t0) / self.scale - (self.rec.base - self.rec.a0)
        return None

    def due(self, it):
        if self.t0 is None:
            return None
        if it.phase == 1 or self.rec.a1 is None:
            d = self.t0 + (it.t - self.rec.a0) * self.scale
            if self.t1 is not None:
                d = min(d, self.t1)
            return d
        if self.t1 is None:
            return None
        return self.t1 + (it.t - self.rec.a1) * self.scale

    def rewrite(self, typ, data):
        if typ == P.T_HDR:
            try:
                _, o = P.unpack(data)
            except Exception:
                return data
            x1 = o.get("x1", "")
            if "#AK:" not in x1 and not self.live_as:
                return data
            o["x1"] = set_tag(x1, "AK", self.live_as)
            return P.pack(P.T_HDR, o)
        if typ == T_ROSTER and self.sid and self.rec.sid and self.sid != self.rec.sid:
            try:
                _, o = P.unpack(data)
            except Exception:
                return data
            ch = False
            for p in o.get("peers", []):
                if p.get("sid") == self.rec.sid:
                    p["sid"] = self.sid
                    ch = True
            return P.pack(T_ROSTER, o) if ch else data
        return data

    def hold_hdr(self, now):
        try:
            _, o = P.unpack(self.last_hdr)
        except Exception:
            return self.rewrite(P.T_HDR, self.last_hdr)
        x1 = o.get("x1", "")
        nn = x1_tags(x1).get("NN")
        if nn:
            add = int(round((now - (self.stopped_at if self.stopped_at is not None else now)) * 1000))
            x1 = set_tag(x1, "NN", hold_nn(nn, add))
        o["x1"] = x1
        return self.rewrite(P.T_HDR, P.pack(P.T_HDR, o))

    def count(self, typ, n=1):
        k = P.NAMES.get(typ, str(typ))
        self.sent[k] = self.sent.get(k, 0) + n

    def stop(self, why, now):
        if self.stopped:
            return
        self.stopped = why
        self.stopped_at = now
        r = self.rel_now(now)
        self.log("GHOST STOP %s at rec t=%s s (stream %d of %d sent)%s" % (why, "?" if r is None else "%.2f" % r, self.idx, len(self.rec.stream), "; holding the last host state" if self.a.hold else ""))

    def serve_want(self, o):
        wid = str(o.get("id", ""))
        if not wid.isdigit():
            return []
        self.sx["wants"] += 1
        if wid not in self.sx["ids"]:
            self.sx["ids"].append(wid)
            self.log("GHOST sx: the client asks for the host world (request %s): %s" % (wid, "serving recorded package %s (%d B)" % (self.rec.pkg["id"], self.rec.pkg["size"]) if self.rec.pkg else "NO complete package in the capture"))
        k = self.rec.pkg
        if k is None:
            self.sx["waits"] += 1
            return [P.pack(T_SXI, {"id": wid, "st": "wait"})]
        out = [P.pack(T_SXI, {"id": wid, "st": "ok", "size": k["size"], "crc": k["crc"], "ch": k["ch"]})]
        n, ch, buf = k["n"], k["ch"], k["buf"]
        c = 0
        for ab in (o.get("r") or [])[:16]:
            try:
                lo, hi = int(ab[0]), int(ab[1])
            except (TypeError, ValueError, IndexError):
                continue
            for i in range(max(0, lo), min(n, hi + 1)):
                if c >= SX_WIN:
                    break
                out.append(P.MAGIC + bytes([P.PROTO, T_SXC]) + struct.pack("<QI", int(wid), i) + bytes(buf[i * ch:(i + 1) * ch]))
                c += 1
        self.sx["chunks"] += c
        return out

    def on_live_hdr(self, o, now):
        x1 = o.get("x1", "")
        sid = o.get("sid")
        h = P.hello(x1)
        if sid != self.sid:
            self.sid = sid
            self.nonce = h.get("nonce") if h else None
            self.log("GHOST client session sid=%s nonce=%s (recorded sid=%s nonce=%s)" % (sid, self.nonce, self.rec.sid, self.rec.nonce))
        cur = x1_tags(x1)
        ash = cur.get("AS")
        if ash and "." in ash:
            self.live_as = ash
        if self.t1 is None:
            self.t1 = now
            self.log("GHOST client in world (first snapshot) %.2f s after its first datagram; recorded %.2f s" % (now - self.t0, (self.rec.a1 - self.rec.a0) if self.rec.a1 is not None else -1))
        rel = self.rel_now(now)
        for t in sorted(set(cur) | set(self.live_tags)):
            v = cur.get(t)
            if v == self.live_tags.get(t):
                continue
            if t not in ("AS", "AK", "RS", "MV", "FH") and self.a.verbose:
                self.log("client #%s: %s" % (t, "(gone)" if v is None else P.short(v, 90)))
            if t in self.rec.event_tags and v and t not in self.ignore:
                self.live_events.append((rel, t, v))
                self.check_div(t, v, rel, now)
        self.live_tags = cur

    def check_div(self, t, v, rel, now):
        if self.a.div == "off" or self.div:
            return
        seen = self.rec.values.get(t, {})
        why = None
        if v not in seen:
            why = "new value"
        elif self.a.div_window > 0 and abs(seen[v] - rel) > self.a.div_window:
            why = "time %.1f s vs recorded %.1f s" % (rel, seen[v])
        if not why:
            return
        rv = sorted(seen.items(), key=lambda kv: kv[1])[:5]
        self.div = {"tag": t, "value": v, "rel": None if rel is None else round(rel, 3), "why": why, "recorded": [[x, round(y, 3)] for x, y in rv], "stream_sent": self.idx}
        self.log("GHOST DIVERGENCE client #%s:%s at rec t=%s s (%s); recorded values of #%s: %s" % (t, P.short(v, 80), "?" if rel is None else "%.2f" % rel, why, t, [x for x, _ in rv] or "none"))
        if self.a.div == "stop":
            self.stop("divergence", now)

    def feed(self, data, addr, now):
        if self.addr is None:
            self.addr = addr
            self.t0 = now
            self.log("GHOST client %s:%d first datagram: replay starts (scale %.2f, %d host datagrams, world package %s)" % (addr[0], addr[1], self.scale, len(self.rec.stream), "%d B" % self.rec.pkg["size"] if self.rec.pkg else "none"))
        elif addr != self.addr:
            return []
        self.client_rx = now
        if len(data) < 4 or data[:2] != P.MAGIC:
            return []
        typ = data[3]
        k = P.NAMES.get(typ, str(typ))
        self.rx[k] = self.rx.get(k, 0) + 1
        if typ == T_SXW:
            try:
                _, o = P.unpack(data)
            except Exception:
                return []
            out = self.serve_want(o)
            for d in out:
                self.count(d[3])
            return [(d, addr) for d in out]
        if typ == T_BYE:
            if not self.client_bye:
                self.client_bye = True
                self.log("GHOST client BYE: the client game is closing")
            return []
        if typ == P.T_HDR:
            try:
                _, o = P.unpack(data)
            except Exception:
                return []
            self.on_live_hdr(o, now)
        return []

    def tick(self, now):
        out = []
        if self.t0 is None:
            return out
        st = self.rec.stream
        while not self.stopped and self.idx < len(st):
            it = st[self.idx]
            d = self.due(it)
            if d is None or now < d:
                break
            if self.stop_rel is not None and it.t - self.rec.base >= self.stop_rel:
                self.stop("stop-at %.2f s" % self.stop_rel, now)
                break
            if it.typ == T_BYE and not self.a.bye:
                self.idx += 1
                continue
            dg = self.rewrite(it.typ, it.data)
            out.append((dg, self.addr))
            self.count(it.typ)
            if it.typ == P.T_HDR:
                self.last_hdr = it.data
            elif it.typ == T_ROSTER:
                self.last_roster = it.data
            elif it.typ == P.T_NPC and it.npc:
                self.last_npc[it.npc] = it.data
            self.idx += 1
        if not self.stopped and self.idx >= len(st):
            self.stop("end of recording", now)
        if self.stopped and self.a.hold and self.last_hdr is not None:
            if now >= self.hold_hdr_at:
                self.hold_hdr_at = now + 1.0 / self.a.hold_hz
                out.append((self.hold_hdr(now), self.addr))
                self.holds += 1
                try:
                    _, o = P.unpack(self.last_hdr)
                    ids = {str(i) for i in o.get("ids", [])}
                except Exception:
                    ids = set(self.last_npc)
                if now >= self.hold_slow_at:
                    self.hold_slow_at = now + 1.0
                    for i in sorted(ids):
                        if i in self.last_npc:
                            out.append((self.last_npc[i], self.addr))
                    if self.last_roster is not None:
                        out.append((self.rewrite(T_ROSTER, self.last_roster), self.addr))
        return out

    def status(self, now):
        r = self.rel_now(now)
        return {"capture": self.rec.path, "client": None if self.addr is None else "%s:%d" % self.addr, "client_sid": self.sid, "client_nonce": self.nonce,
                "rec_t": None if r is None else round(r, 3), "stream_sent": self.idx, "stream_total": len(self.rec.stream), "stopped": self.stopped,
                "holds": self.holds, "sent": dict(sorted(self.sent.items())), "rx": dict(sorted(self.rx.items())), "sx": self.sx, "divergence": self.div,
                "client_events": len(self.live_events), "recorded_events": len(self.rec.events), "client_bye": self.client_bye,
                "world_after_s": None if self.t1 is None or self.t0 is None else round(self.t1 - self.t0, 3), "scale": self.scale, "stop_rel": self.stop_rel}


def args(argv=None):
    ap = argparse.ArgumentParser(description="Ghost peer: plays the HOST side of a recorded NOLA session (relay_v2.py --tap capture) to ONE real client: the same datagrams at the same relative times, the host world package served from the recording, acks answered for the live client; stops at a divergence, a mark or the end and then holds the last host state.")
    ap.add_argument("capture", help=".ncap ring file written by relay_v2.py --tap")
    ap.add_argument("--listen", type=int, default=7796, help="UDP port the client sends to (its netpeerport)")
    ap.add_argument("--bind", default="127.0.0.1")
    ap.add_argument("--cli", type=int, default=None, help="which recorded client to replay (default: the first one)")
    ap.add_argument("--scale", type=float, default=1.0, help="time scale: 2 = twice as slow, 0.5 = twice as fast")
    ap.add_argument("--stop-at", type=float, default=None, help="stop at this recording time (s after the client's first snapshot)")
    ap.add_argument("--stop-at-mark", default="", help="stop at the time of this mark (relay_v2 --mark-file)")
    ap.add_argument("--div", choices=("stop", "log", "off"), default="stop", help="on the first client-originated difference: stop the replay (default), only log it, or ignore")
    ap.add_argument("--div-ignore", default="", help="comma list of client tags not checked for divergence")
    ap.add_argument("--div-window", type=float, default=0, help="also flag a recorded client value that comes more than this many s off its recorded time (0 = values only)")
    ap.add_argument("--no-hold", dest="hold", action="store_false", help="after the stop, send nothing (default: resend the last host snapshot, NPCs and roster)")
    ap.add_argument("--hold-hz", type=float, default=10)
    ap.add_argument("--bye", action="store_true", help="also replay a recorded host BYE")
    ap.add_argument("--seconds", type=float, default=0, help="run time limit (0 = until the client leaves or Ctrl+C)")
    ap.add_argument("--client-silent", type=float, default=120, help="end when the client sent nothing for this long after it started")
    ap.add_argument("--status", default="", help="write the status JSON here every second")
    ap.add_argument("--summary", default="")
    ap.add_argument("--log", default="")
    ap.add_argument("--info", action="store_true", help="print what the capture holds and exit")
    ap.add_argument("--verbose", action="store_true")
    return ap.parse_args(argv)


def write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(obj, f, sort_keys=True, indent=1)
    os.replace(tmp, path)


def main(argv=None):
    a = args(argv)
    rec = Recording(a.capture, a.cli)
    if a.info:
        print(json.dumps(rec.info(), indent=1, sort_keys=True))
        return 0
    log = P.Log(a.log or None, "[ghost] ")
    g = Ghost(rec, a, log)
    s = P.udp(a.listen, a.bind)
    inf = rec.info()
    log("ghost peer up: UDP %s:%d, capture %s: %d host datagrams over %.1f s, client world after %s s, package %s, %d recorded client events, stop %s" % (
        a.bind, a.listen, a.capture, inf["stream"], inf["length_s"], inf["world_after_s"], inf["packages"], inf["client_events"], "at %.2f s" % g.stop_rel if g.stop_rel is not None else "at the end"))
    t_start = time.time()
    stat_at = 0.0
    try:
        while True:
            now = time.time()
            for data, src in P.drain(s):
                for dg, dst in g.feed(data, src, now):
                    try:
                        s.sendto(dg, dst)
                    except OSError as e:
                        log("sendto failed: %s" % e)
            for dg, dst in g.tick(now):
                try:
                    s.sendto(dg, dst)
                except OSError as e:
                    log("sendto failed: %s" % e)
            if a.status and now >= stat_at:
                stat_at = now + 1.0
                write_json(a.status, g.status(now))
            if a.seconds and now - t_start >= a.seconds:
                break
            if g.client_bye or (g.t0 is not None and now - g.client_rx > a.client_silent):
                if not g.client_bye:
                    log("GHOST client silent for %.0f s: end" % (now - g.client_rx))
                break
            time.sleep(0.002)
    except KeyboardInterrupt:
        pass
    sm = g.status(time.time())
    log("SUMMARY " + json.dumps(sm, sort_keys=True))
    if a.status:
        write_json(a.status, sm)
    if a.summary:
        write_json(a.summary, sm)
    return 0


if __name__ == "__main__":
    sys.exit(main())
