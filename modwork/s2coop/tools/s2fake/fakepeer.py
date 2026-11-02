import argparse
import json
import math
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s2proto as P


def hexnonce(rnd):
    return "%x%x" % (int(time.time()) % 0xFFFFFF, rnd.randint(1, 0xFFFFF))


class FakePeer:
    def __init__(self, a, log, rnd=None):
        self.a = a
        self.log = log
        self.rnd = rnd or random.Random()
        self.nonce = a.nonce or hexnonce(self.rnd)
        self.sid = "%d-%d" % (int(time.time()), self.rnd.randint(1, 99999))
        self.f = 0
        self.t_start = None
        self.anchor = tuple(float(v) for v in a.anchor.split(",")) if a.anchor else None
        self.anchor_set = self.anchor is not None
        self.host = None
        self.host_sid = None
        self.host_rx = -1.0
        self.host_up = False
        self.host_sessions = 0
        self.host_tags = {}
        self.host_ack = ""
        self.peers = {}
        self.roster = None
        self.slot = None
        self.full = False
        self.cnt = {k: 0 for k in P.NAMES.values()}
        self.bad = 0
        self.rel = 0
        self.g_seq = 0
        self.q_seq = 0
        self.g_until = -1.0
        self.g_entry = ""
        self.q_until = -1.0
        self.q_entry = ""
        self.done = set()
        self.last_pos = None
        self.go_seen = []
        self.stat_at = 0.0
        self.sent = 0
        self.sends = [parse_send(v) for v in (a.send or [])]
        self.send_until = {}
        self.holders = {}
        self.relayed = []
        self.lead_owner = None
        self.sl_seen = set()
        self.tlen = {}
        self.hdr_n = 0
        self.hdr_bytes = 0
        self.hdr_max = 0
        self.hdr_big = 0
        self.nn_first = ""
        self.sl_ans = ""
        self.sl_until = -1.0
        self.sl_log = []
        self.rv = [0, 0, 0]
        self.revives = [tuple(int(float(x)) if i else float(x) for i, x in enumerate(v.split(":"))) for v in (a.revive or [])]
        self.rv_seen = {}
        self.revived_by = None
        self.go_last = None
        self.go_dead = None
        self.go_events = []

    def rv_watch(self, pid, v):
        f = (v or "").split(",")
        if len(f) not in (2, 3) or not all(x.isdigit() for x in f):
            return
        rev, die = int(f[0]), int(f[1])
        tpid = int(f[2]) if len(f) == 3 else self.slot
        old = self.rv_seen.get(pid)
        self.rv_seen[pid] = (rev, die)
        if old is None or rev < old[0] or die < old[1]:
            return
        if rev > old[0]:
            mine = tpid is not None and tpid == self.slot
            self.log("REVIVE pid=%s -> pid %s (%s)" % (pid, tpid, "me" if mine else "other"))
            if mine and self.downed_now and self.revived_by is None:
                self.revived_by = pid
                self.log("REVIVED by pid=%s: #L back to a" % pid)
        if die > old[1]:
            self.log("pid=%s died (#RV die %d)" % (pid, die))

    def go_watch(self, v):
        f = (v or "").split(",")
        if len(f) < 2 or not f[0].isdigit() or f[1] not in ("alldown", "bleed"):
            return
        seq = int(f[0])
        if self.go_last is None or seq <= self.go_last:
            return
        self.go_last = seq
        who = f[2] if len(f) > 2 else None
        self.go_events.append(v)
        if f[1] == "bleed" and who is not None and self.slot is not None and who.isdigit() and int(who) == self.slot:
            self.log("GAME OVER #GO:%s is my own death" % v)
            return
        self.go_dead = v
        self.log("GAME OVER #GO:%s: I die too and follow the host load" % v)

    def feed(self, data, now):
        try:
            typ, obj = P.unpack(data)
        except Exception as e:
            self.bad += 1
            if self.bad <= 5:
                self.log("bad datagram: %s" % e)
            return
        self.cnt[P.NAMES.get(typ, str(typ))] = self.cnt.get(P.NAMES.get(typ, str(typ)), 0) + 1
        if typ == P.T_HDR:
            self.hdr_n += 1
            self.hdr_bytes += len(data)
            self.hdr_max = max(self.hdr_max, len(data))
            self.hdr_big += len(data) > 1200
            self.on_host(obj, now)
        elif typ == P.T_PEER:
            self.on_peer(obj, now)
        elif typ == P.T_ROSTER:
            r = json.dumps(obj, sort_keys=True)
            if r != self.roster:
                self.roster = r
                self.slot = obj.get("you")
                self.log("ROSTER you=%s max=%s peers=%s" % (obj.get("you"), obj.get("max"), [(p.get("pid"), p.get("st")) for p in obj.get("peers", [])]))
        elif typ == P.T_FULL:
            if not self.full:
                self.log("HOST FULL: max %s clients, this client was not admitted" % obj.get("max"))
            self.full = True
        elif typ == P.T_BYE:
            self.log("host BYE (sid=%s): the host game is closing" % obj.get("sid"))
            self.host_up = False

    def on_host(self, m, now):
        if self.host and m.get("sid") == self.host.get("sid") and m.get("f", 0) < self.host.get("f", 0):
            return
        sid = m.get("sid")
        x1 = m.get("x1", "")
        h = P.hello(x1)
        if sid != self.host_sid:
            self.host_sid = sid
            self.host_sessions += 1
            self.host_tags = {}
            self.go_last = None
            self.log("HOST SESSION #%d sid=%s hello=%s" % (self.host_sessions, sid, h))
        if not self.host_up:
            self.host_up = True
            self.log("PEER UP: host at %.0f,%.0f,%.0f" % (m.get("x", 0), m.get("y", 0), m.get("z", 0)))
        self.host = m
        self.host_rx = now
        if not self.anchor_set:
            self.anchor_set = True
            self.anchor = (m.get("x", 0) + self.a.dx, m.get("y", 0) + self.a.dy, m.get("z", 0))
        cur = {}
        for t, v in P.tags(x1):
            cur.setdefault(t, v)
        cur["@W"] = P.lead(x1)
        if self.go_last is None:
            g = (cur.get("GO") or "").split(",")
            self.go_last = int(g[0]) if g[0].isdigit() else 0
            if self.go_last:
                self.log("GAME OVER #GO:%s was already up when I joined: ignored" % cur.get("GO"))
        for t, v in cur.items():
            s = self.tlen.setdefault(t, [0, 0, 0])
            s[0] += 1
            s[1] += len(v or "")
            s[2] = max(s[2], len(v or ""))
        if cur.get("NN"):
            self.nn_first = cur["NN"][:48]
        ash = cur.get("AS")
        if ash and "." in ash:
            self.host_ack = "#AK:" + ash
        for t in sorted(set(cur) | set(self.host_tags)):
            if t in ("AS", "AK", "RS") and not self.a.verbose:
                continue
            v = cur.get(t)
            if v != self.host_tags.get(t):
                self.log("host #%s: %s" % (t, "(gone)" if v is None else P.short(v, self.a.width)))
                if t == "SL" and v:
                    self.sleep_ask(v, now)
                if t == "GO" and v:
                    self.go_seen.append(v)
                    self.go_watch(v)
                if t == "RV" and v:
                    self.rv_watch(0, v)
                if t in ITEM_TAGS:
                    self.item_tag(t, v or "")
        self.host_tags = cur

    def sleep_ask(self, v, now):
        f = v.split(",")
        if len(f) < 4 or f[1] != "S" or not f[0].isdigit() or int(f[0]) <= 0:
            return
        key = f[0] + "@" + f[3]
        if key in self.sl_seen:
            return
        self.sl_seen.add(key)
        ans = self.a.sl_answer
        self.sl_log.append({"seq": int(f[0]), "ts": f[3], "answer": ans or "-"})
        if not ans:
            self.log("SLEEP ask #SL %s from the host: no answer (--sl-answer not set)" % f[0])
            return
        self.sl_ans = "0,-,0.00,0,%s,%s,%s,0" % (f[0], f[3], ans)
        self.sl_until = now + max(self.a.sl_hold, 1.0)
        self.log("SLEEP ask #SL %s from the host: answering %s" % (f[0], ans))

    def item_tag(self, t, v):
        if t in ("KH", "GH", "CLH"):
            cur = holders_of(v)
            self.holders[t] = cur
            for key, pid in sorted(cur.items()):
                self.log("ITEM %s: %s held by pid %d (%s)" % (t, key, pid, "me" if pid == self.slot else "other"))
            if not cur:
                self.log("ITEM %s: no holder" % t)
        elif t in ("GR", "UR", "YR"):
            for e in [x for x in v.split("|") if x]:
                tok = e.split(";" if t == "GR" else ",", 1)[0]
                pid = int(tok.split(".", 1)[0]) if tok.split(".", 1)[0].isdigit() else -1
                if e not in self.relayed:
                    self.relayed.append(e)
                    self.log("ITEM %s relayed by the host from pid %d (%s): %s" % (t, pid, "me" if pid == self.slot else "other", P.short(e, self.a.width)))
        elif t == "LO":
            f = v.split(",", 1)[0]
            self.lead_owner = int(f) if f.isdigit() else None
            self.log("LEAD owner pid %s (%s)" % (self.lead_owner, "me" if self.lead_owner == self.slot else "other"))

    def on_peer(self, o, now):
        pid = o.get("pid")
        x1 = o.get("x1", "")
        p = self.peers.get(pid)
        life = P.first(x1, "L")
        if p is None or p["sid"] != o.get("sid"):
            self.log("OTHER PLAYER pid=%s sid=%s hello=%s at %.0f,%.0f" % (pid, o.get("sid"), P.hello(x1), o.get("x", 0), o.get("y", 0)))
            p = {"sid": o.get("sid"), "life": None, "n": 0}
            self.peers[pid] = p
        if life != p["life"]:
            self.log("other pid=%s #L: %s" % (pid, life))
            p["life"] = life
        rv = P.first(x1, "RV")
        if rv:
            self.rv_watch(pid, rv)
        p["n"] += 1
        p["rx"] = now
        p["pos"] = (o.get("x", 0), o.get("y", 0), o.get("z", 0))

    def due(self, name, at, el):
        if at is None or at < 0 or name in self.done or el < at:
            return False
        self.done.add(name)
        return True

    downed_now = False

    def life(self, el):
        a = self.a
        self.downed_now = False
        if self.go_dead or (a.dead_at is not None and a.dead_at >= 0 and el >= a.dead_at):
            return "d,0,100"
        if a.down_at is not None and a.down_at >= 0 and el >= a.down_at and not (a.up_at is not None and a.up_at >= 0 and el >= a.up_at):
            if self.revived_by is not None:
                return "a,30,100"
            self.downed_now = True
            return "w,15,100"
        return "a,100,100"

    def build(self, now):
        if self.t_start is None:
            self.t_start = now
        el = now - self.t_start
        a = self.a
        ax, ay, az = self.anchor or (0.0, 0.0, 0.0)
        w = 2 * math.pi / max(a.period, 1.0)
        ang = w * el + (a.phase * math.pi / 180.0)
        x, y = ax + a.radius * math.cos(ang), ay + a.radius * math.sin(ang)
        vx, vy = -a.radius * w * math.sin(ang), a.radius * w * math.cos(ang)
        yaw = (math.degrees(math.atan2(vy, vx)) + 360.0) % 360.0
        self.last_pos = (x, y, az)
        hh = P.hello(self.host.get("x1", "")) if self.host else None
        mod, core, diff = (hh["mod"], hh["core"], hh["diff"]) if hh else (a.mod, a.core, a.diff)
        crouch = 1 if a.crouch_every > 0 and int(el / a.crouch_every) % 2 == 1 else 0
        x1 = "W1~~#H:client,%s,%s,%s,%s" % (mod, core, diff, self.nonce)
        x1 += "#L:" + self.life(el)
        if self.due("die", a.dead_at, el) and not self.go_dead:
            self.rv[1] += 1
        for i, (at, pid) in enumerate(self.revives):
            if self.due("rv%d" % i, at, el):
                self.rv[0] += 1
                self.rv[2] = pid
                self.log("SEND revive #RV %d -> pid %d" % (self.rv[0], pid))
        rv = ("%d,%d,%d" % tuple(self.rv)) if self.slot is not None else ("%d,%d" % tuple(self.rv[:2]))
        x1 += "#MV:%d,0,0#FH:88.0#RV:%s" % (crouch, rv)
        if self.due("take", a.take_at, el):
            self.g_seq += 1
            self.rel += 1
            gx, gy, gz = (int(v) for v in (a.ground.split(";")[0].split(",") if a.ground else (x + 80, y, az)))
            rest = a.ground.split(";", 1)[1] if a.ground else "S2FakeItem;SM_S2Fake"
            self.g_entry = "%d;%d,%d,%d;%s" % (self.g_seq, gx, gy, gz, rest)
            self.g_until = now + 4
            self.log("SEND ground take #G:%s" % self.g_entry)
        if now < self.g_until:
            x1 += "#G:" + self.g_entry
        if self.due("quest", a.quest_at, el):
            self.q_seq += 1
            self.rel += 1
            self.q_entry = "c,%d,%s" % (self.q_seq, a.quest_sid)
            self.q_until = now + 12
            self.log("SEND quest signal #QN:%s" % self.q_entry)
        if now < self.q_until:
            x1 += "#QN:" + self.q_entry
        for name, at, msg in (("down", a.down_at, "SEND life downed #L:w"), ("up", a.up_at, "SEND life back #L:a"), ("dead", a.dead_at, "SEND life dead #L:d")):
            if self.due(name, at, el):
                self.log(msg)
        for i, (at, tag, pay, hold) in enumerate(self.sends):
            if self.due("send%d" % i, at, el):
                self.rel += 1
                self.send_until[i] = now + hold
                self.log("SEND #%s:%s (%.0f s)" % (tag, pay, hold))
            if now < self.send_until.get(i, -1.0):
                x1 += "#%s:%s" % (tag, pay)
        if a.x1:
            x1 += a.x1
        if now < self.sl_until:
            x1 += "#SL:" + self.sl_ans
        x1 += "#AS:%s.%d" % (self.nonce, self.rel)
        x1 += self.host_ack
        hm = self.host or {}
        self.f += 1
        return P.snapshot(self.sid, self.f, now, x, y, az, yaw, vx, vy, hm.get("gt", -1.0), hm.get("wt", -1.0), hm.get("wx", -1), x1)

    def tick(self, now):
        out = []
        if self.host_up and now - self.host_rx > self.a.silent:
            self.host_up = False
            self.log("HOST SILENT: no host snapshot for %.0f s" % (now - self.host_rx))
        out = P.snapshot_datagrams(self.build(now))
        self.sent += 1
        if now - self.stat_at >= 10:
            self.stat_at = now
            self.log("stats: rx %s bad=%d others=%s sent=%d host_up=%s" % ({k: v for k, v in self.cnt.items() if v}, self.bad, sorted(self.peers), self.sent, self.host_up))
        return out

    def summary(self):
        return {"name": self.a.name, "sid": self.sid, "nonce": self.nonce, "sent": self.sent, "rx": {k: v for k, v in self.cnt.items() if v},
                "host_sessions": self.host_sessions, "host_up": self.host_up, "slot": self.slot, "full": self.full,
                "others": {str(k): {"sid": v["sid"], "n": v["n"], "life": v["life"]} for k, v in self.peers.items()}, "go": self.go_seen,
                "host_tags": sorted(self.host_tags), "holders": {t: dict(sorted(v.items())) for t, v in sorted(self.holders.items())},
                "relayed": self.relayed, "lead_owner": self.lead_owner, "sleep": self.sl_log, "rv": self.rv, "revived_by": self.revived_by,
                "go_events": self.go_events, "go_dead": self.go_dead,
                "tag_len": {t: {"n": s[0], "avg": round(s[1] / max(1, s[0]), 1), "max": s[2]} for t, s in sorted(self.tlen.items())},
                "hdr": {"n": self.hdr_n, "avg": round(self.hdr_bytes / max(1, self.hdr_n), 1), "max": self.hdr_max, "big": self.hdr_big}, "nn_first": self.nn_first}


ITEM_TAGS = ("KH", "GH", "CLH", "GR", "UR", "YR", "LO")


def holders_of(v):
    r = {}
    for e in [x for x in v.split("|") if x]:
        pid, _, key = e.partition(":")
        if pid.isdigit() and key:
            r[key] = int(pid)
    return r


def parse_send(v):
    at, tag, rest = v.split(":", 2)
    hold = 6.0
    if ":" in rest:
        head, tail = rest.rsplit(":", 1)
        try:
            hold = float(tail)
            rest = head
        except ValueError:
            pass
    return float(at), tag, rest, hold


def args(argv=None):
    ap = argparse.ArgumentParser(description="S2 co-op fake client: no game, Python 3 standard library only (Linux or Windows).")
    ap.add_argument("--peer", required=True, help="host relay address <ip>:<port> (today the host listens on netlisten, default 7779)")
    ap.add_argument("--listen", type=int, default=7778, help="UDP port to bind; today it must equal the host's netpeerport (default 7778); 0 = any (hub host only)")
    ap.add_argument("--bind", default="0.0.0.0")
    ap.add_argument("--name", default="fake1")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--hz", type=float, default=10)
    ap.add_argument("--anchor", help="x,y,z centre of the walk in cm; default: 0,0,0 until the first host snapshot, then the host position + (--dx, --dy)")
    ap.add_argument("--dx", type=float, default=300)
    ap.add_argument("--dy", type=float, default=0)
    ap.add_argument("--radius", type=float, default=400)
    ap.add_argument("--period", type=float, default=20)
    ap.add_argument("--phase", type=float, default=0)
    ap.add_argument("--crouch-every", type=float, default=8)
    ap.add_argument("--take-at", type=float, default=15, help="seconds after start: one ground take (#G); -1 = never")
    ap.add_argument("--ground", help="'x,y,z;Class;Mesh' of the taken item (default: 80 cm in front, class S2FakeItem: matches no real item)")
    ap.add_argument("--quest-at", type=float, default=20, help="seconds after start: one quest signal (#QN c); -1 = never")
    ap.add_argument("--quest-sid", default="S2FakeNode_M1", help="quest node SID sent in #QN (default matches no node, so the host only logs it)")
    ap.add_argument("--down-at", type=float, default=-1)
    ap.add_argument("--up-at", type=float, default=-1)
    ap.add_argument("--dead-at", type=float, default=-1, help="seconds after start: dies (#L:d and #RV die+1: the host answers #GO:<seq>,bleed,<my pid>)")
    ap.add_argument("--revive", action="append", help="'AT:PID': at AT s revive pid PID (#RV rev+1 with target pid PID; the target stands up if it is downed); repeatable")
    ap.add_argument("--send", action="append", help="'AT:TAG:PAYLOAD[:HOLD]': from AT s after start send #TAG:PAYLOAD for HOLD s (default 6), e.g. 5:K:BP_Box_C@520,30,1 (container lock), 5:GC:Class@x,y,z (ground claim), 8:U:1,P,A012A,2 (item event); repeatable")
    ap.add_argument("--sl-answer", default="", help="answer to every new host sleep ask (#SL): Y = accept, N = refuse, empty = never answer")
    ap.add_argument("--sl-hold", type=float, default=20, help="seconds the #SL answer stays in the snapshot")
    ap.add_argument("--x1", default="", help="extra x1 text appended to every snapshot")
    ap.add_argument("--mod", default="0")
    ap.add_argument("--core", default="0")
    ap.add_argument("--diff", default="Medium")
    ap.add_argument("--nonce", default="")
    ap.add_argument("--silent", type=float, default=5)
    ap.add_argument("--bye", action="store_true", help="send BYE when the run ends")
    ap.add_argument("--log")
    ap.add_argument("--summary", help="write the final summary JSON to this file")
    ap.add_argument("--verbose", action="store_true")
    ap.add_argument("--width", type=int, default=90)
    return ap.parse_args(argv)


def main(argv=None):
    a = args(argv)
    log = P.Log(a.log, "[%s] " % a.name)
    host, port = a.peer.rsplit(":", 1)
    dst = (host, int(port))
    s = P.udp(a.listen, a.bind)
    fp = FakePeer(a, log)
    log("fake client up: UDP %s:%d -> host %s:%d, sid=%s nonce=%s" % (a.bind, s.getsockname()[1], dst[0], dst[1], fp.sid, fp.nonce))
    t0 = time.time()
    nxt = t0
    try:
        while a.seconds <= 0 or time.time() - t0 < a.seconds:
            now = time.time()
            for data, src in P.drain(s):
                fp.feed(data, now)
            if now >= nxt:
                nxt = now + 1.0 / a.hz
                for dg in fp.tick(now):
                    try:
                        s.sendto(dg, dst)
                    except OSError as e:
                        log("sendto failed: %s" % e)
            time.sleep(0.005)
    except KeyboardInterrupt:
        pass
    if a.bye:
        for _ in range(3):
            try:
                s.sendto(P.pack(P.T_BYE, {"sid": fp.sid}), dst)
            except OSError:
                pass
        log("BYE sent")
    sm = fp.summary()
    if fp.host is None:
        log("NO host snapshot was ever received: is the host relay running, does it send to this PC's IP and UDP port %d, is the port open?" % s.getsockname()[1])
    log("SUMMARY " + json.dumps(sm, sort_keys=True))
    if a.summary:
        open(a.summary, "w", encoding="utf-8").write(json.dumps(sm, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
