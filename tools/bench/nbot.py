import os
import sys
import time
import itertools

IO = r"C:\modding\s2harness\bench\io"
PORTS = {"C": 27063, "D": 27062, "E": 27064, "A": 27063, "B": 27062}
_seq = itertools.count(int(time.time() * 1000) % 100000000)


def port_of(side):
    s = str(side)
    return PORTS[s.upper()] if s.upper() in PORTS else int(s)


def send(side, lines, timeout=10.0):
    if isinstance(lines, str):
        lines = [lines]
    d = os.path.join(IO, str(port_of(side)))
    rid = "%d_%d" % (os.getpid(), next(_seq))
    tmp = os.path.join(d, "req_%s.tmp" % rid)
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    os.replace(tmp, os.path.join(d, "req_%s.txt" % rid))
    res = os.path.join(d, "res_%s.txt" % rid)
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(res):
            for _ in range(20):
                try:
                    with open(res, encoding="utf-8", errors="replace") as f:
                        txt = f.read()
                    os.remove(res)
                    return txt.rstrip("\n")
                except OSError:
                    time.sleep(0.02)
        time.sleep(0.03)
    req = os.path.join(d, "req_%s.txt" % rid)
    if os.path.exists(req):
        os.remove(req)
    raise TimeoutError("nbot %s no reply in %.0f s to %s" % (side, timeout, lines))


def wait_goal(side, timeout=120.0, poll=0.5):
    import json
    end = time.time() + timeout
    last = None
    while time.time() < end:
        last = json.loads(send(side, "status"))
        if last["state"] != "running":
            return last
        time.sleep(poll)
    send(side, "stop")
    return {"state": "timeout", "detail": str(last)}


if __name__ == "__main__":
    print(send(sys.argv[1], " ".join(sys.argv[2:]), timeout=30))
