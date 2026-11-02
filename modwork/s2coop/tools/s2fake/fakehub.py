import argparse
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s2proto as P


class Slot:
    def __init__(self, pid, addr, now):
        self.pid = pid
        self.addr = addr
        self.sid = None
        self.nonce = None
        self.hdr = None
        self.recv = now
        self.state = "none"
        self.silent_at = 0.0
        self.sessions = 0
        self.tags = {}
        self.life = "a"
        self.ack = ""
        self.qseen = set()
        self.npc_dg = 0
        self.app_dg = 0
        self.full_at = -1e9
        self.rv = None


class Hub:
    def __init__(self, a, log, table=None):
        self.a = a
        self.log = log
        self.table = table if table is not None else P.load_tag_table()
        self.relay = P.relay_tags(self.table)
        self.slots = {}
        self.by_addr = {}
        self.refused = {}
        self.nonce = "%x%x" % (int(time.time()) % 0xFFFFFF, random.randint(1, 0xFFFFF))
        self.sid = "hub-%d" % int(time.time() * 1000)
        self.f = 0
        self.t0 = None
        self.go = []
        self.go_seq = 0
        self.go_latch = False
        self.go_at = -1e9
        self.host_rv = [0, 0, 0]
        self.host_revived = False
        self.host_dead = False
        self.done = set()
        self.revives = []
        self.roster_at = -1e9
        self.stat_at = -1e9
        self.peer_dg = 0
        self.rel = 0
        self.hl = "a"

    def free_pid(self):
        used = {s.pid for s in self.slots.values()}
        for i in range(1, self.a.max + 1):
            if i not in used:
                return i
        return None

    def feed(self, data, addr, now):
        out = []
        try:
            typ, obj = P.unpack(data)
        except Exception as e:
            self.log("bad datagram from %s:%d: %s" % (addr[0], addr[1], e))
            return out
        s = self.by_addr.get(addr)
        if typ == P.T_BYE:
            if s and s.state != "left":
                self.log("S2HUB PEER LEFT pid=%d (BYE, sid=%s)" % (s.pid, obj.get("sid")))
                self.drop(s)
            return out
        if typ != P.T_HDR:
            if s:
                if typ == P.T_NPC:
                    s.npc_dg += 1
                elif typ == P.T_APP:
                    s.app_dg += 1
            return out
        if s is None:
            for o in self.slots.values():
                if o.sid and o.sid == obj.get("sid"):
                    self.log("S2HUB pid=%d moved %s:%d -> %s:%d" % (o.pid, o.addr[0], o.addr[1], addr[0], addr[1]))
                    del self.by_addr[o.addr]
                    o.addr = addr
                    self.by_addr[addr] = o
                    s = o
                    break
        if s is None:
            pid = self.free_pid()
            if pid is None:
                if now - self.refused.get(addr, -1e9) >= 1:
                    if addr not in self.refused:
                        self.log("S2HUB FULL: %s:%d refused (max %d clients)" % (addr[0], addr[1], self.a.max))
                    self.refused[addr] = now
                    out.append((P.pack(P.T_FULL, {"max": self.a.max}), addr))
                return out
            s = Slot(pid, addr, now)
            self.slots[pid] = s
            self.by_addr[addr] = s
        if s.hdr and obj.get("sid") == s.sid and obj.get("f", 0) < s.hdr.get("f", 0):
            return out
        self.take(s, obj, now)
        return out

    def take(self, s, m, now):
        x1 = m.get("x1", "")
        h = P.hello(x1) or {}
        if m.get("sid") != s.sid or h.get("nonce") != s.nonce:
            s.sid, s.nonce = m.get("sid"), h.get("nonce")
            s.sessions += 1
            s.tags = {}
            s.qseen = set()
            s.rv = None
            self.log("S2HUB PEER SESSION pid=%d #%d sid=%s role=%s mod=%s core=%s diff=%s nonce=%s addr=%s:%d" % (
                s.pid, s.sessions, s.sid, h.get("role"), h.get("mod"), h.get("core"), h.get("diff"), s.nonce, s.addr[0], s.addr[1]))
            if h.get("role") == "host":
                self.log("S2HUB PEER ERRO pid=%d: role=host (two hosts)" % s.pid)
        if s.state != "up":
            self.log("S2HUB PEER UP pid=%d%s; players: %d of %d clients" % (s.pid, " (back after %.0f s silent)" % (now - s.silent_at) if s.state == "silent" else "",
                                                                       sum(1 for o in self.slots.values() if o.state == "up" or o is s), self.a.max))
            s.state = "up"
        s.hdr = m
        s.recv = now
        cur = {}
        for t, v in P.tags(x1):
            cur.setdefault(t, v)
        ash = cur.get("AS")
        if ash and "." in ash:
            s.ack = "#AK:" + ash
        for t in sorted(set(cur) | set(s.tags)):
            if t in ("AS", "AK", "RS", "H"):
                continue
            v = cur.get(t)
            if v != s.tags.get(t):
                self.log("pid=%d #%s: %s" % (s.pid, t, "(gone)" if v is None else P.short(v, self.a.width)))
                if t == "QN" and v:
                    for e in v.split("|"):
                        f = e.split(",", 2)
                        if len(f) == 3 and f[0] == "c" and (f[1], f[2]) not in s.qseen:
                            s.qseen.add((f[1], f[2]))
                            self.log("S2HUB qn: origem c pid=%d %s seq=%s (key %d:%s:%s)" % (s.pid, f[2], f[1], s.pid, f[2], f[1]))
                if t == "G" and v:
                    self.log("S2HUB ground take pid=%d %s -> host taken list" % (s.pid, v))
        s.tags = cur
        lf = cur.get("L", "a,100,100")
        s.life = lf[:1] if lf else "a"
        self.rv_in(s, cur.get("RV"), now)

    def rv_in(self, s, v, now):
        f = (v or "").split(",")
        if len(f) not in (2, 3) or not all(x.isdigit() for x in f):
            return
        rev, die = int(f[0]), int(f[1])
        tpid = int(f[2]) if len(f) == 3 else 0
        if s.rv is None or rev < s.rv[0] or die < s.rv[1]:
            s.rv = [rev, die]
            return
        if rev > s.rv[0]:
            s.rv[0] = rev
            self.log("S2HUB REVIVE pid=%d -> pid %d (#RV:%s)" % (s.pid, tpid, v))
            self.revives.append((s.pid, tpid))
            if tpid == 0 and self.host_down_now():
                self.host_revived = True
                self.log("S2HUB HOST REVIVED by pid=%d" % s.pid)
        if die > s.rv[1]:
            s.rv[1] = die
            self.raise_go("bleed", s.pid, now)

    def host_down_now(self):
        return self.hl == "w"

    def raise_go(self, kind, pid, now):
        if self.go_latch:
            self.log("S2HUB GAME OVER #%d already sent; %s pid=%s ignored" % (self.go_seq, kind, pid))
            return
        self.go_latch = True
        self.go_at = now
        self.go_seq += 1
        txt = "%d,%s" % (self.go_seq, kind) if pid is None else "%d,%s,%d" % (self.go_seq, kind, pid)
        self.go = [(now + self.a.gosend, txt)]
        self.log("S2HUB GAME OVER #%d (%s): every player dies now, the host loads, clients follow" % (self.go_seq, txt.split(",", 1)[1]))
        self.host_dead = True

    def drop(self, s):
        s.state = "left"
        self.by_addr.pop(s.addr, None)
        self.slots.pop(s.pid, None)

    def host_life(self, el):
        a = self.a
        if self.host_dead or (a.host_dead_at >= 0 and el >= a.host_dead_at):
            return "d"
        if self.host_revived:
            return "a"
        if a.host_down_at >= 0 and el >= a.host_down_at and not (a.host_up_at >= 0 and el >= a.host_up_at):
            return "w"
        return "a"

    def rules(self, now, hl):
        if self.go_latch and now - self.go_at > self.a.golatch:
            self.go_latch = False
        if hl == "d" and not self.host_dead:
            self.host_rv[1] += 1
            self.log("S2HUB HOST DIED (#RV die %d)" % self.host_rv[1])
            if any(s.state == "up" for s in self.slots.values()):
                self.raise_go("bleed", 0, now)
            self.host_dead = True
            return
        if hl != "w":
            return
        n, ok = 0, True
        for s in self.slots.values():
            if s.hdr is None or s.state == "left":
                continue
            if s.state != "up":
                ok = False
                continue
            n += 1
            if s.life != "w" or now - s.recv >= 2:
                ok = False
        if n > 0 and ok:
            self.raise_go("alldown", None, now)

    def tick(self, now):
        if self.t0 is None:
            self.t0 = now
        el = now - self.t0
        out = []
        a = self.a
        for s in list(self.slots.values()):
            age = now - s.recv
            if s.state == "up" and age >= a.silent:
                s.state = "silent"
                s.silent_at = now
                self.log("S2HUB PEER SILENT pid=%d: no snapshot for %.0f s" % (s.pid, age))
            if s.state == "silent" and now - s.silent_at >= a.left:
                self.log("S2HUB PEER LEFT pid=%d (no answer for %.0f s): slot freed" % (s.pid, a.left))
                self.drop(s)
        hl = self.host_life(el)
        self.hl = hl
        for i, (at, pid) in enumerate(a.host_revive or []):
            if ("hr%d" % i) not in self.done and el >= at:
                self.done.add("hr%d" % i)
                self.host_rv[0] += 1
                self.host_rv[2] = pid
                self.log("S2HUB HOST SEND revive #%d -> pid %d" % (self.host_rv[0], pid))
        self.rules(now, hl)
        hl = self.host_life(el)
        self.hl = hl
        self.go = [g for g in self.go if now < g[0]]
        ang = 2 * math.pi * el / 30.0
        hx, hy, hz = a.x + 200 * math.cos(ang), a.y + 200 * math.sin(ang), a.z
        base = "W1~~#H:host,%s,%s,%s,%s#L:%s#MV:0,0,0#FH:88.0" % (a.mod, a.core, a.diff, self.nonce, {"w": "w,15,100", "d": "d,0,100"}.get(hl, "a,100,100"))
        base += ("#RV:%d,%d,%d" % tuple(self.host_rv)) if a.max > 1 else ("#RV:%d,%d" % tuple(self.host_rv[:2]))
        if self.go:
            base += "#GO:" + "|".join(g[1] for g in self.go)
        self.f += 1
        live = [s for s in self.slots.values() if s.state in ("up", "silent")]
        for s in live:
            x1 = base + "#D:#AS:%s.%d%s" % (self.nonce, self.rel, s.ack)
            m = P.snapshot(self.sid, self.f, now, hx, hy, hz, 0.0, 0.0, 0.0, 43200.0, 43200.0, 0, x1)
            for dg in P.snapshot_datagrams(m):
                out.append((dg, s.addr))
            if a.max > 1:
                for o in live:
                    if o is s or o.hdr is None or o.state != "up":
                        continue
                    h = o.hdr
                    po = {"pid": o.pid, "sid": o.sid, "f": h.get("f", 0), "t": h.get("t", -1), "x": h.get("x", 0), "y": h.get("y", 0), "z": h.get("z", 0),
                          "yaw": h.get("yaw", 0), "vx": h.get("vx", 0), "vy": h.get("vy", 0), "x1": P.filter_x1(h.get("x1", ""), self.relay)}
                    out.append((P.pack(P.T_PEER, po), s.addr))
                    self.peer_dg += 1
        if a.max > 1 and now - self.roster_at >= 1:
            self.roster_at = now
            peers = [{"pid": s.pid, "sid": s.sid, "st": s.state} for s in sorted(live, key=lambda q: q.pid)]
            for s in live:
                out.append((P.pack(P.T_ROSTER, {"you": s.pid, "max": a.max, "host": self.nonce, "peers": peers}), s.addr))
        if now - self.stat_at >= 10:
            self.stat_at = now
            self.log("S2HUB peers up=%d of %d: %s host_life=%s peer_dg=%d" % (sum(1 for s in self.slots.values() if s.state == "up"), a.max,
                                                                        ["%d:%s:%s" % (s.pid, s.state, s.life) for s in sorted(self.slots.values(), key=lambda q: q.pid)], hl, self.peer_dg))
        return out

    def summary(self):
        return {"max": self.a.max, "slots": {str(s.pid): {"sid": s.sid, "state": s.state, "life": s.life, "sessions": s.sessions, "tags": sorted(s.tags)} for s in self.slots.values()},
                "go": self.go_seq, "go_txt": [g[1] for g in self.go], "revives": self.revives, "host_rv": self.host_rv, "host_revived": self.host_revived,
                "peer_dg": self.peer_dg, "refused": len(self.refused)}


def args(argv=None):
    ap = argparse.ArgumentParser(description="S2 co-op reference HUB host (no game): 1 host + up to --max clients, fan-out of client state to the other clients.")
    ap.add_argument("--listen", type=int, default=7779)
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--max", type=int, default=4)
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--hz", type=float, default=10)
    ap.add_argument("--x", type=float, default=0)
    ap.add_argument("--y", type=float, default=0)
    ap.add_argument("--z", type=float, default=0)
    ap.add_argument("--silent", type=float, default=5)
    ap.add_argument("--left", type=float, default=60)
    ap.add_argument("--host-down-at", type=float, default=-1)
    ap.add_argument("--host-up-at", type=float, default=-1)
    ap.add_argument("--host-dead-at", type=float, default=-1, help="the host dies (bleeds out): #RV die+1 and #GO:<seq>,bleed,0")
    ap.add_argument("--host-revive", action="append", type=lambda v: (float(v.split(":")[0]), int(v.split(":")[1])), help="'AT:PID': the host revives pid PID (#RV rev+1, tpid PID); repeatable")
    ap.add_argument("--gosend", type=float, default=10, help="seconds #GO stays in the host snapshot (C++: 10)")
    ap.add_argument("--golatch", type=float, default=60, help="seconds a game over blocks a new one (C++: 60)")
    ap.add_argument("--mod", default="v37-b53")
    ap.add_argument("--core", default="b205")
    ap.add_argument("--diff", default="Medium")
    ap.add_argument("--log")
    ap.add_argument("--summary")
    ap.add_argument("--width", type=int, default=90)
    return ap.parse_args(argv)


def main(argv=None):
    a = args(argv)
    log = P.Log(a.log, "[hub] ")
    s = P.udp(a.listen, a.bind)
    hub = Hub(a, log)
    log("hub host up: UDP %s:%d max=%d clients, relay tags=%s" % (a.bind, a.listen, a.max, ",".join(sorted(hub.relay))))
    t0 = time.time()
    nxt = t0
    try:
        while a.seconds <= 0 or time.time() - t0 < a.seconds:
            now = time.time()
            for data, src in P.drain(s):
                for dg, dst in hub.feed(data, src, now):
                    s.sendto(dg, dst)
            if now >= nxt:
                nxt = now + 1.0 / a.hz
                for dg, dst in hub.tick(now):
                    try:
                        s.sendto(dg, dst)
                    except OSError as e:
                        log("sendto %s:%d failed: %s" % (dst[0], dst[1], e))
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    sm = hub.summary()
    log("SUMMARY " + json.dumps(sm, sort_keys=True))
    if a.summary:
        open(a.summary, "w", encoding="utf-8").write(json.dumps(sm, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
