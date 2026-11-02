"""fakehost.py: scripted fake HOST, so ONE game in the client configuration can be tested on one PC (no second game, no network).
Mirror image of fakeclient.py and same transport: with the relay stopped it rewrites <bridge>/in.json (tmp + replace, 10 times a second)
and reads the game's own <bridge>/out.json for the client's position, clock, versions and messages.

Usage (game running with role.txt = client, relay stopped: relay_ctl.sh stop):
  uv run --no-project python -I fakehost.py --dir <bridge> --scenario fakehost_scenarios/01_catchup_first_join.txt
  uv run --no-project python -I fakehost.py --dir <bridge> --quest-log <copy of a host quest_log.txt> --seconds 120      (catch-up only)
  uv run --no-project python -I fakehost.py --dir <bridge> --seconds 60 --dx 500                                      (a plain host standing 5 m away)
  options: --log <file> (default <bridge>/fakehost_log.txt)   --catchup-secs 60   --hz 10
           --campaign <id>   campaign id in #B; default: a fixed id per scenario file (the same on every run), "fhlocal" without one
           --loss 5 --delay 80 [--jitter 40]   drop 5 % and delay 80 ms (+ up to 40 ms, which reorders), in each direction, both modes
           --profile internet-good|internet|internet-bad   presets for those three: 25 ms/10/0.5 %, 60 ms/30/2 %, 150 ms/80/5 % (each way)
           --key <text>   network mode: the shared secret of the game PC's relay (b27 relay.py --key)
NETWORK mode: run this file on ANOTHER LAN machine (plain Python 3, standard library only, Linux or Windows); it speaks the real relay
protocol (relay_v2.py datagrams) to the game PC, so timing, loss and reordering are real:
  other machine:  python3 fakehost.py --net --peer <game PC ip>:7778 --scenario fakehost_scenarios/01_catchup_first_join.txt
  game PC:        uv run --no-project python -I D:/modding/offline/b3/relay_v2.py --listen 7778 --peer <other machine ip>:7779 --dir <bridge>
  UDP 7778 must be reachable on the game PC (from the other machine) and UDP 7779 on the other machine (from the game PC); --listen changes 7779.
What the client sends back (#N #T #U #AT #K #Q #L, need, shots) is printed and logged whenever it changes.

Scenario file: one step per line, "<seconds to wait before it> <command> [arguments]"; a line starting with "#" is a comment; paths are relative to the file.
  log <quest_log.txt> [n]      load a host quest log (first n lines) and offer it as the catch-up (#S chunks), as a new client session gets
  quest <event>                live quest event (#J for 2 s, also appended to the log), e.g.  quest X,E01_AddNote_A
  cine on|off                  #V cinematic state               teleport            #V teleport count + 1
  emission on|off              #E                               places [n ...]      #AL closed artifact places (none = all open)
  hold <id>|client|off         #K: the host has this container open ("client" = the id the client sent last) -> LOCK_DENY on the client
  taken <key>|client           #X: taken key ("client" = the id the client sent last in #K) -> TAKEN_DENY on the client
  rollback <keep n>            the host loaded an older save: the log is cut to n events, #B generation + 1, catch-up offered again
  relaunch <keep n>            the host game was restarted on an older save: new session, log cut to n, same campaign id, generation unchanged
  campaign new|<id>            another campaign id in #B (core b32): the client must drop its record
  damage <amount>              #D for 2 s                       life a|w|d [hp]     #L state (w = downed, d = dead)
  silence <seconds>            nothing is written (PEER SILENT on the client after 5 s)
  session                      new session and launch id (the host relaunched); the catch-up is offered again
  pos <x> <y> <z> | pos near [dx cm]      time <seconds of day>|client      weather <0-9>|client
  x1 <seconds> <text>          any other x1 text for that long (e.g.  x1 3 #R:1;BP_AI_Human_C_1;Breadx1)
  key [text]                   wait for Enter in this window     say <text>          end
"""
import argparse, os, re, sys, time

PROTO_VER = 2                                           # main.lua P2.VER

# ---------- message builders (tested against the module: D:/modding/offline/b26/test_fakehost.py) ----------
def djb2(s):                                            # main.lua P2.hash
    h = 5381
    for b in s.encode("utf-8"): h = (h * 33 + b) & 0xFFFFFFFF
    return "%08x" % h
def look_hash(text): return djb2("@x\x01" + text)       # main.lua P2.lookPut
def app_entry(text): return '{"a":"%s","mesh":"@x","parts":"%s","mats":"","statics":""}' % (look_hash(text), text)
def hello(role, mod, core, diff, nonce): return "#H:%s,%s,%s,%s,%s" % (role, mod, core, diff, nonce)
def life(st="a", hp=100, mx=100): return "#L:%s,%.0f,%.0f" % (st, hp, mx)
def cine(on, tseq): return "#V:%d,%d" % (1 if on else 0, tseq)
def emission(on, n): return "#E:%d,%d" % (1 if on else 0, n)
def art_places(places): return "#AL:" + ".".join(str(i) for i in sorted(places))
def rollback(gen, n, cid=None): return "#B:%d,%d" % (gen, n) + (("," + cid) if cid else "")      # core b32: third field = the host's campaign id
def damage(events): return "#D:" + "|".join("%d,%.1f" % (s, a) for s, a in events)          # [(seq, amount)]
def lock(held): return "#K:" + (held or "")
def taken(keys): return ("#X:" + "|".join(keys)) if keys else ""
def quest_live(events): return ("#J:" + "|".join("%d,%s" % (s, e) for s, e in events)) if events else ""    # [(seq, "X,sid")]
def catchup_chunks(events):                             # NOLA catchupOut: 1,200-byte chunks, text "QL<i>~ev;ev"
    chunks, cur, size = [], [], 0
    for e in events:
        if size + len(e) + 1 > 1200 and cur: chunks.append(";".join(cur)); cur, size = [], 0
        cur.append(e); size += len(e) + 1
    if cur: chunks.append(";".join(cur))
    return ["QL%d~%s" % (i + 1, c) for i, c in enumerate(chunks)]
def catchup_tag(n, texts): return "#S:%d,%s" % (n, ".".join(look_hash(t) for t in texts))
def snapshot(sid, f, t, x, y, z, yaw, gt, wt, wx, x1, apps):
    return ('{"v":%d,"sid":"%s","f":%d,"t":%.3f,"x":%.1f,"y":%.1f,"z":%.1f,"vx":0.0,"vy":0.0,"yaw":%.1f,"gt":%.1f,"wt":%.1f,"wx":%d,"avmesh":"","avparts":"",'
            '"x1":"%s","shots":"","need":"","apps":[%s],"npcs":[]}') % (PROTO_VER, sid, f, t, x, y, z, yaw, gt, wt, wx, x1, ",".join(apps))
def read_quest_log(path, n=None):
    ev = []
    for ln in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"^[\d.]+\t(.+?)\r?\n?$", ln)
        if m: ev.append(m.group(1))
    return ev[:n] if n else ev
def parse_client(text):                                 # the client's out.json -> numbers and x1 tags
    head = text.split('"apps"')[0]
    def num(k):
        m = re.search(r'"%s"\s*:\s*([-\d.eE+]+)' % k, head); return float(m.group(1)) if m else None
    def s(k):
        m = re.search(r'"%s"\s*:\s*"(.*?)"' % k, head); return m.group(1) if m else ""
    x1 = s("x1")
    tags = {m.group(1): m.group(2) for m in re.finditer(r"#([A-Z]+):([^#]*)", x1)}
    return {"x": num("x"), "y": num("y"), "z": num("z"), "gt": num("gt"), "wt": num("wt"), "wx": num("wx"), "x1": x1, "tags": tags,
            "need": [h for h in s("need").split(",") if h], "shots": s("shots")}

# ---------- the fake host ----------
WATCH = ("N", "T", "U", "AT", "K", "Q", "L")
class FakeHost:
    def __init__(self, now, dx=300.0, catchup_secs=60.0, out=print):
        self.out, self.dx, self.cusecs = out, dx, catchup_secs
        self.f = 0; self.gen = 0; self.log = []; self.cu = None; self.cu_until = 0; self.cu_loads = None
        self.jseq = 0; self.live = []; self.dseq = 0; self.dmg = []; self.extra = []
        self.cine_on = False; self.tseq = 0; self.em_on = False; self.em_n = 0; self.places = None      # a real host always sends #E
        self.held = ""; self.taken = []; self.xi = 0; self.life = ("a", 100.0); self.silent_until = 0
        self.pos = None; self.gt = None; self.wx = None; self.last_k = ""; self.seen = {}; self.client = None
        self.cid = "fhc%x" % (int(now) & 0xFFFFFF)       # campaign id (core b32), kept across `session`; `campaign new` changes it
        self.new_session(now)
    def new_session(self, now):
        self.sid = "fakehost-%d" % int(now * 1000); self.nonce = "fh%08x" % (int(now * 1000) & 0xFFFFFFFF)
        self.offer(now)
    def offer(self, now):                               # like catchupOut: the log as it is now, for catchup_secs
        self.cu_n = len(self.log); self.cu = catchup_chunks(self.log) if self.log else None; self.cu_until = now + self.cusecs
    def do(self, now, cmd, args):
        a = args.split()
        if cmd == "log":
            m = re.match(r"^(.*?)(?:\s+(\d+))?$", args.strip())             # the path may hold spaces; an optional count ends the line
            self.log = read_quest_log(m.group(1), int(m.group(2)) if m.group(2) else None); self.offer(now)
            self.out("log: %d events, catch-up offered in %d chunks for %d s" % (len(self.log), len(self.cu or []), self.cusecs))
        elif cmd == "quest": self.jseq += 1; self.live.append((now, self.jseq, args.strip())); self.log.append(args.strip())
        elif cmd == "cine": self.cine_on = a[0] == "on"
        elif cmd == "teleport": self.tseq += 1
        elif cmd == "emission":
            on = a[0] == "on"
            if on and not self.em_on: self.em_n += 1
            self.em_on = on
        elif cmd == "places": self.places = [int(i) for i in a]
        elif cmd == "hold": self.held = "" if (not a or a[0] == "off") else (self.last_k if a[0] == "client" else args.strip())
        elif cmd == "taken":
            k = self.last_k if a[0] == "client" else args.strip()
            if k: self.taken.append(k)
            else: self.out("taken client: the client has not sent a #K id yet")
        elif cmd == "rollback":
            self.log = self.log[:int(a[0])]; self.gen += 1; self.offer(now)
        elif cmd == "relaunch":                          # the host game was closed and started again on an OLDER save: new session, shorter log, same campaign
            self.log = self.log[:int(a[0])]; self.new_session(now)
        elif cmd == "campaign": self.cid = ("fhc%x" % (int(now * 1000) & 0xFFFFFF)) if (not a or a[0] == "new") else a[0]
        elif cmd == "damage": self.dseq += 1; self.dmg.append((now, self.dseq, float(a[0])))
        elif cmd == "life": self.life = (a[0], float(a[1]) if len(a) > 1 else (100.0 if a[0] == "a" else (15.0 if a[0] == "w" else 0.0)))
        elif cmd == "silence": self.silent_until = now + float(a[0])
        elif cmd == "session": self.new_session(now)
        elif cmd == "pos":
            if a[0] == "near": self.pos = None; self.dx = float(a[1]) if len(a) > 1 else self.dx
            else: self.pos = tuple(float(v) for v in a[:3])
        elif cmd == "time": self.gt = None if a[0] == "client" else float(a[0])
        elif cmd == "weather": self.wx = None if a[0] == "client" else int(a[0])
        elif cmd == "x1": self.extra.append((now + float(a[0]), args.split(None, 1)[1]))
        elif cmd == "say": self.out(args)
        else: raise ValueError("unknown scenario command: " + cmd)
    def watch(self, c):
        for k in WATCH:
            v = c["tags"].get(k, "")
            if v != self.seen.get(k, ""): self.seen[k] = v; self.out("client #%s: %s" % (k, v if v != "" else "(empty)"))
        if c["tags"].get("K"): self.last_k = c["tags"]["K"]
        if c["shots"] and c["shots"] != self.seen.get("shots"): self.out("client shots: " + c["shots"])
        self.seen["shots"] = c["shots"]
    def step(self, now, client_text):
        """one snapshot for in.json, or None (no client snapshot yet, or the host is silent)"""
        try: c = parse_client(client_text or "")
        except Exception: c = None
        if not c or c["x"] is None: return None
        self.client = c; self.watch(c)
        nl = c["tags"].get("N")                          # like catchupOut: the client loaded a save -> offer the catch-up again
        if nl is not None and self.cu_loads is not None and nl != self.cu_loads: self.offer(now); self.out("the client loaded a save (#N %s): catch-up offered again" % nl)
        if nl is not None: self.cu_loads = nl
        if now < self.silent_until: return None
        ch = (c["tags"].get("H") or ",?,?,Medium,").split(",")          # same mod and module version as the client: no version warning
        x1 = life(self.life[0], self.life[1], 100) + lock(self.held) + hello("host", ch[1], ch[2], ch[3], self.nonce)
        self.dmg = [e for e in self.dmg if now - e[0] < 2]
        x1 += damage([(s, amt) for _, s, amt in self.dmg]) if not self.cine_on else "#D:"
        if self.taken:
            self.xi = (self.xi + 1) % len(self.taken)
            x1 += taken([self.taken[(self.xi + i) % len(self.taken)] for i in range(min(4, len(self.taken)))])
        self.live = [e for e in self.live if now - e[0] < 2]
        x1 += quest_live([(s, e) for _, s, e in self.live])
        apps = []
        if self.cu and now <= self.cu_until:
            x1 += catchup_tag(self.cu_n, self.cu)
            apps = [app_entry(t) for t in self.cu if look_hash(t) in c["need"]]
        x1 += cine(self.cine_on, self.tseq)
        if self.em_on is not None: x1 += emission(self.em_on, self.em_n)
        x1 += rollback(self.gen, len(self.log), self.cid)
        if self.places is not None: x1 += art_places(self.places)
        self.extra = [e for e in self.extra if now < e[0]]
        x1 += "".join(t for _, t in self.extra)
        x, y, z = self.pos or (c["x"] + self.dx, c["y"], c["z"])
        self.f += 1
        return snapshot(self.sid, self.f, now, x, y, z, 0.0, c["gt"] if self.gt is None else self.gt, c["wt"] if self.gt is None else self.gt,
                        int(self.host_wx(c) if self.wx is None else self.wx), x1, apps)
    def host_wx(self, c):
        """the host's weather type when no `weather` step set one: 7 (emission weather) while its emission is on; otherwise the client's own
        type, but never 7 (a host without an emission does not have emission weather, and echoing the client's 7 would keep its emission alive)"""
        if self.em_on: return 7
        return 0 if int(c["wx"] or 0) == 7 else (c["wx"] or 0)

def read_scenario(path):
    steps, base = [], os.path.dirname(os.path.abspath(path))
    for i, ln in enumerate(open(path, encoding="utf-8"), 1):
        ln = "" if ln.lstrip().startswith("#") else ln.strip()
        if not ln: continue
        p = ln.split(None, 2)
        try: delay = float(p[0])
        except ValueError: raise ValueError("%s line %d: the line must start with the seconds to wait" % (path, i))
        cmd, args = p[1], (p[2] if len(p) > 2 else "")
        if cmd == "log": args = os.path.join(base, args)
        steps.append((delay, cmd, args))
    return steps

# ---------- transport: the local bridge files, or the real relay protocol over UDP (relay_v2.py: b"S2" + version + type + zlib(json)) ----------
import json, random, socket, zlib
RELAY_MAGIC, T_HDR, T_APP = b"S2", 1, 3
import hashlib, hmac
KEY = b""                                               # the relay's shared secret (--key): an 8-byte HMAC-SHA256 tag ends every datagram
def dg_tag(body): return hmac.new(KEY, body, hashlib.sha256).digest()[:8]
def dg_pack(typ, obj):
    d = RELAY_MAGIC + bytes([PROTO_VER, typ]) + zlib.compress(json.dumps(obj, separators=(",", ":")).encode("utf-8"), 6)
    return d + dg_tag(d) if KEY else d
def dg_unpack(data):
    if data[:2] != RELAY_MAGIC or len(data) < 5 or data[2] != PROTO_VER: raise ValueError("not a relay v%d datagram" % PROTO_VER)
    if KEY:
        if len(data) < 13 or not hmac.compare_digest(dg_tag(data[:-8]), data[-8:]): raise ValueError("wrong shared-secret tag")
        data = data[:-8]
    return data[3], json.loads(zlib.decompress(data[4:]).decode("utf-8"))
def snapshot_datagrams(text):                           # one snapshot text -> HDR + one APP per look, as relay_v2.Sender does (no NPCs)
    m = json.loads(text); apps = m.pop("apps", []); m.pop("npcs", None); m["ids"] = []
    return [dg_pack(T_HDR, m)] + [dg_pack(T_APP, a) for a in apps]
class Link:
    """loss (percent) and delay + jitter (ms) for one direction"""
    def __init__(self, loss=0.0, delay=0.0, jitter=0.0, rnd=None):
        self.loss, self.delay, self.jitter, self.q, self.rnd, self.dropped = loss / 100.0, delay / 1000.0, jitter / 1000.0, [], rnd or random.Random(), 0
    def push(self, now, item):
        if self.loss and self.rnd.random() < self.loss: self.dropped += 1; return
        self.q.append((now + self.delay + (self.rnd.random() * self.jitter if self.jitter else 0), item))
    def pop(self, now):
        due = [e for e in self.q if e[0] <= now]; self.q = [e for e in self.q if e[0] > now]
        return [it for _, it in sorted(due, key=lambda e: e[0])]         # in arrival order: jitter reorders
class Transport:
    """recv(now) -> the newest snapshot text of the game side, or None;  send(now, text) -> True when something went out"""
    def __init__(self, a):
        self.a, self.tx, self.rx = a, Link(a.loss, a.delay, a.jitter), Link(a.loss, a.delay, a.jitter)
        self.last_in, self.hdr, self.sock, self.seen = None, None, None, None
        if a.net:
            host, port = a.peer.rsplit(":", 1); self.peer = (host, int(port))
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); self.sock.bind(("0.0.0.0", a.listen)); self.sock.setblocking(False)
            try: self.sock.ioctl(socket.SIO_UDP_CONNRESET, False)         # Windows: ignore "port unreachable" from an absent peer
            except (AttributeError, OSError): pass
    def recv(self, now):
        if self.sock:
            try:
                while True: self.rx.push(now, self.sock.recvfrom(65535)[0])
            except (BlockingIOError, ConnectionResetError, OSError): pass
            for data in self.rx.pop(now):
                try: typ, obj = dg_unpack(data)
                except Exception: continue
                if typ == T_HDR and not (self.hdr and self.hdr.get("sid") == obj.get("sid") and obj.get("f", 0) < self.hdr.get("f", 0)): self.hdr = obj
            return json.dumps(self.hdr, separators=(",", ":")) if self.hdr else None
        try: txt = open(os.path.join(self.a.dir, "out.json"), encoding="utf-8", errors="replace").read()
        except OSError: txt = None
        if txt and txt != self.seen: self.seen = txt; self.rx.push(now, txt)
        for t in self.rx.pop(now): self.last_in = t
        return self.last_in
    def send(self, now, text):
        if text is not None: self.tx.push(now, text)
        n = 0
        for t in self.tx.pop(now):
            if self.sock:
                for dg in snapshot_datagrams(t):
                    try: self.sock.sendto(dg, self.peer); n += 1
                    except OSError: pass
            else:
                tmp = os.path.join(self.a.dir, "in.tmp")
                try: open(tmp, "w", encoding="utf-8").write(t); os.replace(tmp, os.path.join(self.a.dir, "in.json")); n += 1
                except OSError: pass                    # the game has the file open this instant: the next snapshot goes through
        return n > 0
def add_transport_args(ap):
    ap.add_argument("--dir", help="bridge folder with out.json / in.json (local mode)")
    ap.add_argument("--net", action="store_true", help="speak the relay protocol over UDP instead of the local files")
    ap.add_argument("--peer", help="--net: <ip of the game PC>:<port its relay listens on>, e.g. 10.0.50.101:7778")
    ap.add_argument("--listen", type=int, default=7779, help="--net: UDP port this script listens on (the game PC's relay sends to it)")
    ap.add_argument("--loss", type=float, default=0, help="percent of snapshots/datagrams dropped, each direction")
    ap.add_argument("--delay", type=float, default=0, help="ms of delay, each direction"); ap.add_argument("--jitter", type=float, default=0, help="extra random ms (reorders)")
    ap.add_argument("--profile", choices=sorted(PROFILES), help="internet preset for --delay/--jitter/--loss (each direction)")
    ap.add_argument("--key", default="", help="--net: shared secret of the relay on the game PC")
PROFILES = {"internet-good": (25, 10, 0.5), "internet": (60, 30, 2), "internet-bad": (150, 80, 5)}      # ms delay, ms jitter, % loss, each way
def check_transport_args(ap, a):
    global KEY
    KEY = a.key.encode("utf-8")
    if a.profile: a.delay, a.jitter, a.loss = PROFILES[a.profile]
    if a.net and not a.peer: ap.error("--net needs --peer <ip>:<port>")
    if not a.net and not a.dir: ap.error("give --dir <bridge> (local mode) or --net --peer <ip>:<port>")

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    add_transport_args(ap)
    ap.add_argument("--scenario"); ap.add_argument("--quest-log"); ap.add_argument("--seconds", type=float, default=0)
    ap.add_argument("--dx", type=float, default=300); ap.add_argument("--catchup-secs", type=float, default=60); ap.add_argument("--hz", type=float, default=10)
    ap.add_argument("--campaign", help="campaign id sent in #B (default: one fixed id per scenario file, 'fhlocal' without one)")
    ap.add_argument("--log"); a = ap.parse_args(); check_transport_args(ap, a)
    lf = open(a.log or os.path.join(a.dir or ".", "fakehost_log.txt"), "a", encoding="utf-8")
    t0 = time.time()
    def out(msg):
        line = "%7.1f  %s" % (time.time() - t0, msg); print(line, flush=True); lf.write(line + "\n"); lf.flush()
    tr = Transport(a)
    fh = FakeHost(time.time(), a.dx, a.catchup_secs, out)
    fh.cid = a.campaign or (("fh" + djb2(os.path.basename(a.scenario))[:6]) if a.scenario else "fhlocal")    # stable across runs: the client's record stays valid
    if a.quest_log: fh.do(time.time(), "log", a.quest_log)
    steps = read_scenario(a.scenario) if a.scenario else []
    out("fake host started (%s): session %s, campaign %s, %d scenario steps%s" % ("UDP listen %d, peer %s" % (a.listen, a.peer) if a.net else "files in " + a.dir, fh.sid, fh.cid, len(steps), "" if steps or a.seconds else "; runs until Ctrl+C"))
    st = {"n": 0, "got": False}
    def pump(now):
        txt = tr.recv(now)
        if txt and not st["got"]: st["got"] = True; out("first snapshot of the game received")
        if tr.send(now, fh.step(now, txt) if txt else None): st["n"] += 1
    due, i = time.time() + (steps[0][0] if steps else 0), 0
    try:
        while True:
            now = time.time()
            while i < len(steps) and now >= due:
                _, cmd, args = steps[i]; i += 1
                if cmd == "end": i = len(steps); a.seconds = now - t0
                elif cmd == "key":
                    out("WAITING for Enter: " + (args or "do the step in the game, then press Enter"))
                    import threading
                    ev = threading.Event(); threading.Thread(target=lambda: (sys.stdin.readline(), ev.set()), daemon=True).start()
                    while not ev.is_set(): pump(time.time()); time.sleep(1.0 / a.hz)
                    now = time.time(); out("Enter")
                else: out("step: %s %s" % (cmd, args)); fh.do(now, cmd, args)
                due = now + (steps[i][0] if i < len(steps) else 0)
            pump(now)
            if (a.seconds and now - t0 >= a.seconds) or (steps and i >= len(steps) and not a.seconds): break
            time.sleep(1.0 / a.hz)
    except KeyboardInterrupt: pass
    if not st["got"]: out("NO snapshot of the game was ever received: " + ("is the relay running on the game PC and are the UDP ports open?" if a.net else "is the game in the world with the bridge on?"))
    out("fake host sent %d snapshots; simulated loss: %d out, %d in" % (st["n"], tr.tx.dropped, tr.rx.dropped))

if __name__ == "__main__": main()
