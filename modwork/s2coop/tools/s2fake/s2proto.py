import json
import os
import re
import socket
import time
import zlib

PROTO = 2
T_HDR, T_NPC, T_APP, T_SXW, T_SXI, T_SXC, T_BYE = 1, 2, 3, 4, 5, 6, 7
T_PEER, T_ROSTER, T_FULL = 8, 9, 10
NAMES = {1: "HDR", 2: "NPC", 3: "APP", 4: "SXW", 5: "SXI", 6: "SXC", 7: "BYE", 8: "PEER", 9: "ROSTER", 10: "FULL"}
MAGIC = b"S2"
TAG_RE = re.compile(r"#([A-Z][A-Z0-9]*):([^#]*)")
HERE = os.path.dirname(os.path.abspath(__file__))


def dumps(obj):
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)


def pack(typ, obj):
    return MAGIC + bytes([PROTO, typ]) + zlib.compress(dumps(obj).encode("utf-8"), 6)


def unpack(data):
    if len(data) < 5 or data[:2] != MAGIC:
        raise ValueError("not an S2 datagram (%d bytes)" % len(data))
    if data[2] != PROTO:
        raise ValueError("protocol v%d, this tool v%d" % (data[2], PROTO))
    typ = data[3]
    if typ == T_SXC:
        return typ, {"wid": int.from_bytes(data[4:12], "little"), "i": int.from_bytes(data[12:16], "little"), "n": len(data) - 16}
    return typ, json.loads(zlib.decompress(data[4:]).decode("utf-8"))


def tags(x1):
    out = []
    i, n = 0, len(x1)
    while i < n:
        if x1[i] != "#":
            i += 1
            continue
        j = i + 1
        if j < n and "A" <= x1[j] <= "Z":
            while j < n and (("A" <= x1[j] <= "Z") or ("0" <= x1[j] <= "9")):
                j += 1
            if j < n and x1[j] == ":":
                e = x1.find("#", j + 1)
                if e < 0:
                    e = n
                out.append((x1[i + 1:j], x1[j + 1:e]))
                i = e
                continue
        i += 1
    return out


def first(x1, tag):
    for t, v in tags(x1):
        if t == tag:
            return v
    return None


def lead(x1):
    p = x1.find("#")
    return x1 if p < 0 else x1[:p]


def hello(x1):
    h = first(x1, "H")
    if h is None:
        return None
    f = h.split(",")
    return dict(zip(("role", "mod", "core", "diff", "nonce"), f)) if len(f) == 5 else None


def filter_x1(x1, keep):
    o = lead(x1) if "@W" in keep else ""
    for t, v in tags(x1):
        if t in keep:
            o += "#%s:%s" % (t, v)
    return o


def snapshot(sid, f, t, x, y, z, yaw, vx, vy, gt, wt, wx, x1, need=""):
    return {"v": PROTO, "sid": sid, "f": f, "t": round(t, 3), "x": round(x, 1), "y": round(y, 1), "z": round(z, 1), "vx": round(vx, 1),
            "vy": round(vy, 1), "yaw": round(yaw, 1), "gt": gt, "wt": wt, "wx": wx, "avmesh": "", "avparts": "", "x1": x1, "shots": "",
            "need": need, "apps": [], "npcs": []}


def snapshot_datagrams(m):
    m = dict(m)
    apps = m.pop("apps", [])
    m.pop("npcs", None)
    m["ids"] = []
    return [pack(T_HDR, m)] + [pack(T_APP, a) for a in apps]


def load_tag_table(path=None):
    rows = {}
    path = path or os.path.join(HERE, "mc_tags.tsv")
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().splitlines()
    for ln in lines:
        f = ln.rstrip("\r\n").split("\t")
        if len(f) < 5 or f[0] == "tag":
            continue
        rows[f[0]] = {"module": f[1], "from": f[2], "scope": f[3], "relay": f[4] == "1", "rule": f[5] if len(f) > 5 else ""}
    return rows


def relay_tags(table):
    return {t for t, r in table.items() if r["relay"]}


class Log:
    def __init__(self, path=None, prefix=""):
        self.t0 = time.time()
        self.prefix = prefix
        self.f = open(path, "a", encoding="utf-8") if path else None

    def __call__(self, msg):
        line = "%8.2f %s%s" % (time.time() - self.t0, self.prefix, msg)
        print(line, flush=True)
        if self.f:
            self.f.write(line + "\n")
            self.f.flush()


def udp(listen, bind="0.0.0.0"):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 0)
    s.bind((bind, listen))
    s.setblocking(False)
    if hasattr(socket, "SIO_UDP_CONNRESET"):
        try:
            s.ioctl(socket.SIO_UDP_CONNRESET, False)
        except OSError:
            pass
    return s


def drain(sock, cap=4096):
    out = []
    while len(out) < cap:
        try:
            out.append(sock.recvfrom(65535))
        except (BlockingIOError, InterruptedError):
            break
        except ConnectionResetError:
            continue
        except OSError:
            break
    return out


def short(v, n=90):
    return v if len(v) <= n else v[:n] + "..(%d)" % len(v)
