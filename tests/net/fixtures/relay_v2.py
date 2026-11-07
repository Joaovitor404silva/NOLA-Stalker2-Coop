"""relay_v2.py - UDP relay for snapshot protocol v2 (offline agent 2026-10-10; tested only with stubs, NOT in a game session).
Patched copy of bridge\\relay.py. NOT installed: it lives in D:\\modding\\offline\\b2 until the supervisor decides.

Usage (same as v1):  python relay_v2.py --listen 7777 --peer <ip>:7778 [--dir <bridge>] [--dx 150] [--loss 0.2] [--record f]

What changed against v1:
  * one snapshot = several small datagrams instead of one big one:
      HDR  header of out.json (player, clocks, shots, need) + the list of NPC ids of this snapshot      every snapshot
      NPC  one NPC (position, pose, appearance hash)        only when it changed, or once per second as keepalive
      APP  one appearance (mesh, parts, materials, statics) only when the mod put it in out.json (new or requested)
    every datagram = b"S2" + version byte + type byte + zlib(json). A lost datagram costs one NPC for one tick, not the snapshot.
  * the relay keeps every appearance it has seen (by content hash). The local mod asks for what it lacks in "need";
    the relay answers from its cache when it can and forwards only the rest to the peer.
  * in.json is rebuilt from the latest header + the latest state of each NPC of that header + the appearances the local mod asked for.
  * version checks, all loud (printed every 5 s, counted in the 10 s statistics line): local out.json without "v":2,
    datagram from a v1 relay (plain JSON), datagram with another version byte.
  * send errors are printed (v1 swallowed them: a snapshot over 65,507 bytes was silently never sent).
Packet loss: HDR/NPC are state, the next one replaces a lost one; an NPC that did not change is resent within 1 s;
  a lost APP is asked again by the receiver (its "need" travels in every HDR) and answered within 1 s; shots ride in HDR
  and the mod repeats each shot for 1.5 s. Reordered datagrams older than what is already known are dropped (sid + f)."""
import argparse, json, os, random, socket, struct, sys, time, zlib

PROTO = 2
MAGIC = b"S2"
T_HDR, T_NPC, T_APP = 1, 2, 3
T_SXW, T_SXI, T_SXC = 4, 5, 6
MTU_SAFE = 1200
KEEPALIVE = 1.0
APP_MIN_GAP = 0.5
NPC_STALE = 5.0

class ProtoError(Exception):
    pass

def pack(typ, obj):
    return MAGIC + bytes([PROTO, typ]) + zlib.compress(json.dumps(obj, separators=(",", ":")).encode("utf-8"), 6)

def unpack(data):
    if data[:1] == b"{":
        raise ProtoError("peer relay speaks protocol v1 (plain JSON datagram): update the other PC's relay and main.lua")
    if data[:2] != MAGIC or len(data) < 5:
        raise ProtoError("unknown datagram (%d bytes)" % len(data))
    if data[2] != PROTO:
        raise ProtoError("peer relay speaks protocol v%d, this relay v%d" % (data[2], PROTO))
    return data[3], json.loads(zlib.decompress(data[4:]).decode("utf-8"))

SX_CH = 1100
SX_WIN = 64
SX_GAP = 0.05
SX_RETRY = 0.6

class SaveXfer:
    """b167 save transfer. Client role while <dir>/sx_want.txt names an id; host role whenever a WANT arrives."""
    def __init__(self, d):
        self.d = d
        self.p = lambda n: os.path.join(d, n)
        self.want_id, self.want_chk = None, 0.0
        self.info, self.got, self.asked, self.part = None, None, {}, None
        self.next_want, self.map_at, self.last_info_ask = 0.0, 0.0, 0.0
        self.pkg, self.pkg_key, self.pkg_id = None, None, None
        self.stats = dict(sent=0, recv=0, done=0, crcbad=0, req=0)
    def _load_pkg(self):
        try:
            stt = os.stat(self.p("sx_out.pkg")); key = (stt.st_mtime_ns, stt.st_size)
        except OSError:
            self.pkg = None; self.pkg_key = None; self.pkg_id = None; return
        if key == self.pkg_key: return
        try:
            with open(self.p("sx_out.pkg"), "rb") as f: b = f.read()
        except OSError:
            return
        if len(b) != key[1]: return
        hdr = b[:b.find(b"\n")].decode("ascii", "replace")
        ids = [t[3:] for t in hdr.split() if t.startswith("id=")]
        self.pkg, self.pkg_key, self.pkg_id = b, key, (ids[0] if ids else None)
        self.pkg_crc = zlib.crc32(b) & 0xFFFFFFFF
    def on_want(self, obj, send):
        wid = str(obj.get("id", ""))
        if not wid.isdigit(): return
        self._load_pkg()
        if self.pkg_id != wid:
            try:
                with open(self.p("sx_req.txt"), encoding="utf-8") as f: cur = f.read().strip()
            except OSError:
                cur = ""
            if cur != wid:
                with open(self.p("sx_req.txt") + ".tmp", "w", encoding="utf-8") as f: f.write(wid + "\n")
                os.replace(self.p("sx_req.txt") + ".tmp", self.p("sx_req.txt")); self.stats["req"] += 1
                print("relay sx: client asked for the host save (request %s) -> sx_req.txt" % wid, flush=True)
            send(pack(T_SXI, {"id": wid, "st": "wait"})); return
        n = (len(self.pkg) + SX_CH - 1) // SX_CH
        send(pack(T_SXI, {"id": wid, "st": "ok", "size": len(self.pkg), "crc": self.pkg_crc, "ch": SX_CH}))
        k = 0
        for a, b in obj.get("r", [])[:16]:
            for i in range(max(0, int(a)), min(n, int(b) + 1)):
                if k >= SX_WIN: return
                send(MAGIC + bytes([PROTO, T_SXC]) + struct.pack("<QI", int(wid), i) + self.pkg[i * SX_CH:(i + 1) * SX_CH]); k += 1
                self.stats["sent"] += 1
    def _reset(self):
        if self.part: self.part.close()
        self.info, self.got, self.asked, self.part = None, None, {}, None
    def _start(self, info):
        size, crc, ch = int(info["size"]), int(info["crc"]), int(info["ch"])
        n = (size + ch - 1) // ch
        head = "%s %d %d %d\n" % (self.want_id, size, crc, ch)
        got = bytearray(n)
        try:
            with open(self.p("sx_in.map"), "rb") as f: m = f.read()
            nl = m.find(b"\n")
            if m[:nl + 1].decode("ascii", "replace") == head and len(m) - nl - 1 == n and os.path.getsize(self.p("sx_in.part")) == size:
                got = bytearray(m[nl + 1:])
                print("relay sx: resuming request %s: %d/%d chunks already here" % (self.want_id, sum(got), n), flush=True)
        except OSError:
            pass
        if not any(got):
            with open(self.p("sx_in.part"), "wb") as f: f.truncate(size)
        self.part = open(self.p("sx_in.part"), "r+b")
        self.info, self.got, self.head, self.n = dict(size=size, crc=crc, ch=ch), got, head, n
    def _save_map(self):
        with open(self.p("sx_in.map") + ".tmp", "wb") as f: f.write(self.head.encode("ascii") + bytes(self.got))
        os.replace(self.p("sx_in.map") + ".tmp", self.p("sx_in.map"))
        with open(self.p("sx_in.prog") + ".tmp", "w", encoding="utf-8") as f: f.write("%s %d/%d\n" % (self.want_id, sum(self.got), self.n))
        os.replace(self.p("sx_in.prog") + ".tmp", self.p("sx_in.prog"))
    def _finish(self):
        self.part.close(); self.part = None
        with open(self.p("sx_in.part"), "rb") as f: b = f.read()
        if (zlib.crc32(b) & 0xFFFFFFFF) != self.info["crc"]:
            self.stats["crcbad"] += 1
            print("relay sx: request %s CRC32 bad: asking every chunk again" % self.want_id, flush=True)
            self.got = bytearray(self.n); self.asked = {}; self.part = open(self.p("sx_in.part"), "r+b"); return
        os.replace(self.p("sx_in.part"), self.p("sx_in.pkg"))
        for x in ("sx_in.map",):
            try: os.remove(self.p(x))
            except OSError: pass
        with open(self.p("sx_in.prog"), "w", encoding="utf-8") as f: f.write("%s %d/%d done\n" % (self.want_id, self.n, self.n))
        self.stats["done"] += 1
        print("relay sx: request %s complete: %d B -> sx_in.pkg" % (self.want_id, len(b)), flush=True)
        self._reset(); self.want_id = "done:" + self.want_id
    def on_info(self, obj):
        if self.want_id is None or str(obj.get("id")) != self.want_id or obj.get("st") != "ok" or self.info is not None: return
        self._start(obj)
    def on_chunk(self, data):
        if self.info is None or len(data) < 16: return
        wid, i = struct.unpack("<QI", data[4:16])
        if str(wid) != self.want_id or i >= self.n or self.got[i]: return
        self.part.seek(i * self.info["ch"]); self.part.write(data[16:]); self.got[i] = 1; self.stats["recv"] += 1
        self.asked.pop(i, None)
        if all(self.got): self._finish()
    def tick(self, now, send):
        if now >= self.want_chk:
            self.want_chk = now + 0.25
            try:
                with open(self.p("sx_want.txt"), encoding="utf-8") as f: wid = f.read().strip()
            except OSError:
                wid = ""
            if not wid.isdigit(): wid = None
            if wid is None:
                if self.want_id is not None and not str(self.want_id).startswith("done:"):
                    if self.part: self._save_map()
                    self._reset()
                self.want_id = None
            elif wid != self.want_id and self.want_id != "done:" + wid:
                if self.part: self._save_map()
                self._reset(); self.want_id = wid
                print("relay sx: asking the host for its save (request %s)" % wid, flush=True)
        if self.want_id is None or str(self.want_id).startswith("done:") or now < self.next_want: return
        self.next_want = now + SX_GAP
        if self.info is None:
            if now - self.last_info_ask >= 0.5:
                self.last_info_ask = now; send(pack(T_SXW, {"id": self.want_id, "r": [[0, SX_WIN - 1]]}))
            return
        r, k, i = [], 0, 0
        while i < self.n and k < SX_WIN and len(r) < 16:
            if not self.got[i] and now - self.asked.get(i, -1e9) >= SX_RETRY:
                a = i
                while i < self.n and k < SX_WIN and not self.got[i] and now - self.asked.get(i, -1e9) >= SX_RETRY:
                    self.asked[i] = now; i += 1; k += 1
                r.append([a, i - 1])
            else:
                i += 1
        if r: send(pack(T_SXW, {"id": self.want_id, "r": r}))
        if now >= self.map_at: self.map_at = now + 1.0; self._save_map()
    def feed(self, data, send):
        typ = data[3]
        if typ == T_SXC: self.on_chunk(data); return
        _, obj = unpack(data)
        if typ == T_SXW: self.on_want(obj, send)
        elif typ == T_SXI: self.on_info(obj)

class Sender:
    """out.json text -> datagrams."""
    def __init__(self):
        self.npc_sent, self.app_sent, self.local_need = {}, {}, set()
        self.sid = ""
    def split(self, txt, now, have=()):
        m = json.loads(txt)
        if m.get("v") != PROTO:
            raise ProtoError("local main.lua writes snapshot v=%r, this relay needs v=%d: update main.lua (or use the v1 relay)" % (m.get("v"), PROTO))
        npcs, apps = m.pop("npcs", []), m.pop("apps", [])
        need = [h for h in str(m.get("need", "")).split(",") if h]
        self.local_need = set(need)
        m["need"] = ",".join(h for h in need if h not in have)
        m["ids"] = [n["id"] for n in npcs]
        sid, f = m.get("sid"), m.get("f")
        # A new session gets every NPC and appearance again, not deltas.
        if str(sid) != self.sid:
            self.npc_sent.clear(); self.app_sent.clear()
        self.sid = str(sid)
        out = [pack(T_HDR, m)]
        for n in npcs:
            sig = (n.get("x"), n.get("y"), n.get("z"), n.get("yaw"), n.get("a"), n.get("s"), n.get("wp"))
            prev = self.npc_sent.get(n["id"])
            if prev and prev[0] == sig and now - prev[1] < KEEPALIVE:
                continue
            self.npc_sent[n["id"]] = (sig, now)
            out.append(pack(T_NPC, {"sid": sid, "f": f, "n": n}))
        for a in apps:
            if now - self.app_sent.get(a["a"], -1e9) < APP_MIN_GAP:
                continue
            self.app_sent[a["a"]] = now
            out.append(pack(T_APP, a))
        for k in [k for k, v in self.npc_sent.items() if now - v[1] > 30]:
            del self.npc_sent[k]
        return out

class Assembler:
    """datagrams -> in.json content."""
    def __init__(self):
        self.hdr, self.npcs, self.apps = None, {}, {}
        self.stale = 0
        self.retired = set()
    def feed(self, data, now):
        typ, obj = unpack(data)
        if typ == T_HDR:
            if self.hdr is not None and self.hdr.get("sid") == obj.get("sid") and obj.get("f", 0) < self.hdr.get("f", 0):
                self.stale += 1; return typ
            if self.hdr is not None and self.hdr.get("sid") != obj.get("sid"):
                # Datagrams of a session that was replaced are dropped.
                if str(obj.get("sid")) in self.retired:
                    self.stale += 1; return typ
                if len(self.retired) >= 256:
                    raise ProtoError("session history full: reconnect relay")
                self.retired.add(str(self.hdr.get("sid")))
                self.npcs.clear()
            self.hdr = obj
        elif typ == T_NPC:
            n = obj["n"]; nid = n["id"]
            if str(obj.get("sid")) in self.retired:
                self.stale += 1; return typ
            if self.hdr is not None and self.hdr.get("sid") != obj.get("sid"):
                self.stale += 1; return typ
            cur = self.npcs.get(nid)
            if cur is not None and cur["sid"] == obj.get("sid") and obj.get("f", 0) < cur["f"]:
                self.stale += 1; return typ
            self.npcs[n["id"]] = {"sid": obj.get("sid"), "f": obj.get("f", 0), "n": n, "at": now}
        elif typ == T_APP:
            self.apps[obj["a"]] = obj
            if len(self.apps) > 512:
                for k in list(self.apps)[:64]: del self.apps[k]
        else:
            raise ProtoError("unknown datagram type %r" % typ)
        return typ
    def render(self, local_need, now, dx=0.0):
        if self.hdr is None:
            return None
        m = dict(self.hdr)
        ids = m.pop("ids", [])
        keep = set(ids)
        for k in [k for k, v in self.npcs.items() if (k not in keep and now - v["at"] > 2.0) or now - v["at"] > 30]:
            del self.npcs[k]
        npcs = []
        for i in ids:
            v = self.npcs.get(i)
            if v is None or v["sid"] != m.get("sid") or now - v["at"] > NPC_STALE:
                continue
            n = dict(v["n"])
            if dx: n["x"] = n["x"] + dx
            npcs.append(n)
        if dx and "x" in m: m["x"] = m["x"] + dx
        m["apps"] = [self.apps[h] for h in sorted(local_need) if h in self.apps]
        m["npcs"] = npcs
        return m

def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--listen", type=int, default=7777)
    ap.add_argument("--peer", default="127.0.0.1:7777")
    ap.add_argument("--dx", type=float, default=0, help="shift the remote X (cm), loopback tests")
    ap.add_argument("--dir", default="", help="folder with out.json/in.json (default: the script's folder)")
    ap.add_argument("--record", default="", help="append every out.json that was sent (one JSON line each)")
    ap.add_argument("--loss", type=float, default=0, help="drop this fraction of incoming datagrams (tests)")
    ap.add_argument("--seconds", type=float, default=0, help="stop after this many seconds (tests); 0 = run forever")
    a = ap.parse_args(argv)
    d = a.dir or os.path.dirname(os.path.abspath(__file__))
    host, port = a.peer.rsplit(":", 1)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(("127.0.0.1" if host.startswith("127.") else "0.0.0.0", a.listen)); s.setblocking(False)
    try: s.ioctl(socket.SIO_UDP_CONNRESET, False)
    except (AttributeError, OSError): pass
    out, inn = os.path.join(d, "out.json"), os.path.join(d, "in.json")
    snd, asm = Sender(), Assembler()
    sx = SaveXfer(d)
    def sx_send(dg):
        try: s.sendto(dg, (host, int(port)))
        except OSError as e: warn("sxsend", "sx sendto failed: %s" % e)
    st = dict(sent=0, bytes=0, big=0, maxlen=0, recv=0, lost=0, senderr=0, badlocal=0, badpeer=0, written=0)
    last, last_need, last_warn, last_stat, t_start = None, None, {}, time.time(), time.time()
    def warn(key, msg):
        if time.time() - last_warn.get(key, 0) >= 5:
            last_warn[key] = time.time(); print("RELAY ERROR: " + msg, flush=True)
    print("relay v%d listen=%d peer=%s dir=%s" % (PROTO, a.listen, a.peer, d), flush=True)
    while True:
        now = time.time()
        dirty = False
        try:
            with open(out, encoding="utf-8") as f: txt = f.read().strip()
            if txt and txt != last:
                last = txt
                try:
                    for dg in snd.split(txt, now, asm.apps):
                        try:
                            s.sendto(dg, (host, int(port)))
                            st["sent"] += 1; st["bytes"] += len(dg); st["maxlen"] = max(st["maxlen"], len(dg))
                            if len(dg) > MTU_SAFE: st["big"] += 1
                        except OSError as e:
                            st["senderr"] += 1; warn("send", "sendto failed (%d bytes): %s" % (len(dg), e))
                    if a.record:
                        with open(a.record, "a", encoding="utf-8") as rf: rf.write(txt + "\n")
                except ProtoError as e:
                    st["badlocal"] += 1; warn("local", str(e))
                except ValueError:
                    st["badlocal"] += 1
                if snd.local_need != last_need:
                    last_need = set(snd.local_need); dirty = True
        except OSError:
            pass
        try:
            while True:
                data, _ = s.recvfrom(65535)
                if a.loss and random.random() < a.loss:
                    st["lost"] += 1; continue
                if data[:2] == MAGIC and len(data) >= 4 and data[2] == PROTO and data[3] in (T_SXW, T_SXI, T_SXC):
                    try: sx.feed(data, sx_send)
                    except (ValueError, KeyError, OSError, zlib.error, struct.error) as e: warn("sx", "save transfer: %r" % (e,))
                    continue
                try:
                    asm.feed(data, now); st["recv"] += 1; dirty = True
                except ProtoError as e:
                    st["badpeer"] += 1; warn("peer", str(e))
                except (ValueError, KeyError, zlib.error) as e:
                    st["badpeer"] += 1; warn("peer", "corrupt datagram: %r" % (e,))
        except (BlockingIOError, ConnectionResetError):
            pass
        try: sx.tick(now, sx_send)
        except (OSError, ValueError) as e: warn("sx", "save transfer: %r" % (e,))
        if dirty:
            m = asm.render(snd.local_need, now, a.dx)
            if m is not None:
                with open(inn + ".tmp", "w", encoding="utf-8") as f: json.dump(m, f)
                for _ in range(5):
                    try: os.replace(inn + ".tmp", inn); st["written"] += 1; break
                    except PermissionError: time.sleep(0.01)
        if now - last_stat >= 10:
            print("relay stats 10s: sent=%d (%d B, max %d B, over %d B: %d) recv=%d dropped_test=%d stale=%d in.json=%d send_err=%d bad_local=%d bad_peer=%d apps_cached=%d npcs=%d"
                  % (st["sent"], st["bytes"], st["maxlen"], MTU_SAFE, st["big"], st["recv"], st["lost"], asm.stale, st["written"], st["senderr"], st["badlocal"], st["badpeer"], len(asm.apps), len(asm.npcs)), flush=True)
            if any(sx.stats.values()):
                print("relay sx 10s: chunks sent=%d recv=%d done=%d crc_bad=%d requests=%d" % (sx.stats["sent"], sx.stats["recv"], sx.stats["done"], sx.stats["crcbad"], sx.stats["req"]), flush=True)
                for k in sx.stats: sx.stats[k] = 0
            for k in st: st[k] = 0
            last_stat = now
        if a.seconds and now - t_start >= a.seconds:
            return 0
        time.sleep(0.02)

if __name__ == "__main__":
    sys.exit(main())
