import json
import socket
import sys
import time
import zlib

PROTO = 2
T_DISC = 11
T_DISCR = 12


def pack(t, obj):
    return b"S2" + bytes([PROTO, t]) + zlib.compress(json.dumps(obj, separators=(",", ":")).encode(), 6)


def unpack(d):
    if len(d) < 5 or d[:2] != b"S2" or d[2] != PROTO:
        return None, None
    try:
        return d[3], json.loads(zlib.decompress(d[4:]))
    except Exception:
        return None, None


def host(port, name, n, mx, seconds, nonce_add=0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(("0.0.0.0", port))
    s.settimeout(0.2)
    end = time.time() + seconds
    got = 0
    print("fake host %s on UDP %d (%d/%d players)" % (name, port, n, mx), flush=True)
    while time.time() < end:
        try:
            d, a = s.recvfrom(4096)
        except socket.timeout:
            continue
        t, o = unpack(d)
        if t != T_DISC or not isinstance(o, dict) or o.get("nola") != 1:
            print("ignored %d bytes from %s:%d" % (len(d), a[0], a[1]), flush=True)
            continue
        got += 1
        r = {"nola": 1, "nonce": int(o.get("nonce", 0)) + nonce_add, "name": name, "id": "fake-%s-%d" % (name, port), "n": n, "max": mx, "port": port, "hub": 1}
        s.sendto(pack(T_DISCR, r), a)
        print("probe #%d from %s:%d nonce=%s -> reply" % (got, a[0], a[1], o.get("nonce")), flush=True)
    print("fake host %s done: %d probes" % (name, got), flush=True)


def probe(ip, port, wait):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.2)
    nonce = int(time.time() * 1000) % 1000000007
    end = time.time() + wait
    nxt = 0
    found = []
    while time.time() < end:
        if time.time() >= nxt:
            s.sendto(pack(T_DISC, {"nola": 1, "nonce": nonce}), (ip, port))
            nxt = time.time() + 0.4
        try:
            d, a = s.recvfrom(4096)
        except socket.timeout:
            continue
        t, o = unpack(d)
        if t == T_DISCR and isinstance(o, dict) and o.get("nonce") == nonce:
            k = (a[0], o.get("port"))
            if k not in [f[0] for f in found]:
                found.append((k, o))
                print("reply from %s:%d %s" % (a[0], a[1], json.dumps(o, sort_keys=True)), flush=True)
    print("probe %s:%d: %d host(s)" % (ip, port, len(found)), flush=True)
    return 0 if found else 1


if __name__ == "__main__":
    if sys.argv[1] == "host":
        host(int(sys.argv[2]), sys.argv[3], int(sys.argv[4]), int(sys.argv[5]), float(sys.argv[6]), int(sys.argv[7]) if len(sys.argv) > 7 else 0)
    elif sys.argv[1] == "probe":
        sys.exit(probe(sys.argv[2], int(sys.argv[3]), float(sys.argv[4])))
