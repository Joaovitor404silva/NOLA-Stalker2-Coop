import argparse
import json
import os
import random
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s2proto as P

MAXV = 4


def view_path(d, v):
    return os.path.join(d, "in.json" if v == 0 else "in_v%d.json" % v)


def main(argv=None):
    ap = argparse.ArgumentParser(description="Stands in for the GAME side of the real C++ relay (net_tool run --dir, files mode): writes out.json, reads in.json + in_v1..3.json + roster.json, logs who it sees per view.")
    ap.add_argument("--dir", required=True)
    ap.add_argument("--role", default="host", choices=["host", "client"])
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--hz", type=float, default=10)
    ap.add_argument("--log")
    ap.add_argument("--summary")
    a = ap.parse_args(argv)
    log = P.Log(a.log, "[game-%s] " % a.role)
    nonce = "%x%x" % (int(time.time()) % 0xFFFFFF, random.randint(1, 0xFFFFF))
    sid = "%d-%d" % (int(time.time()), random.randint(1, 99999))
    out_p, tmp = os.path.join(a.dir, "out.json"), os.path.join(a.dir, "out.json.tmp")
    ros_p = os.path.join(a.dir, "roster.json")
    t0 = time.time()
    f = 0
    vw = [{"text": None, "sid": None, "pid": None, "flips": 0, "reads": 0, "sids": {}} for _ in range(MAXV)]
    ros_text = None
    roster = None
    roster_changes = 0
    while time.time() - t0 < a.seconds:
        now = time.time()
        f += 1
        x1 = "W1~~#H:%s,v37-b53,b205,Medium,%s#L:a,100,100" % (a.role, nonce)
        m = P.snapshot(sid, f, now, 100.0, 200.0, 300.0, 0.0, 0.0, 0.0, 43200.0, 43200.0, 0, x1)
        open(tmp, "w", encoding="utf-8").write(P.dumps(m))
        for _ in range(50):
            try:
                os.replace(tmp, out_p)
                break
            except PermissionError:
                time.sleep(0.002)
        for v in range(MAXV):
            st = vw[v]
            try:
                txt = open(view_path(a.dir, v), encoding="utf-8").read()
            except OSError:
                txt = None
            if not txt or txt == st["text"]:
                continue
            st["text"] = txt
            try:
                r = json.loads(txt)
            except ValueError:
                continue
            st["reads"] += 1
            rs = r.get("sid")
            pid = r.get("pid")
            h = P.hello(r.get("x1", "")) or {}
            e = st["sids"].setdefault(rs, {"nonce": h.get("nonce"), "n": 0, "L": None, "pid": pid})
            e["n"] += 1
            e["L"] = P.first(r.get("x1", ""), "L")
            if rs != st["sid"] or pid != st["pid"]:
                if st["sid"] is not None:
                    st["flips"] += 1
                if st["flips"] <= 10 or st["flips"] % 50 == 0:
                    log("view %d (in%s.json) now from pid=%s sid=%s nonce=%s (session change #%d, like NOLA PEER SESSION in the game)" % (v, "" if v == 0 else "_v%d" % v, pid, rs, h.get("nonce"), st["flips"]))
                st["sid"] = rs
                st["pid"] = pid
        try:
            rt = open(ros_p, encoding="utf-8").read()
        except OSError:
            rt = None
        if rt and rt != ros_text:
            ros_text = rt
            try:
                r = json.loads(rt)
            except ValueError:
                r = None
            if r is not None:
                roster_changes += 1
                roster = r
                log("roster seq=%s you=%s max=%s full=%s peers=%s" % (r.get("seq"), r.get("you"), r.get("max"), r.get("full"), [(p.get("pid"), p.get("sid"), p.get("st")) for p in r.get("peers", [])]))
        time.sleep(1.0 / a.hz)
    views = {str(v): {"reads": vw[v]["reads"], "session_changes": vw[v]["flips"], "pid": vw[v]["pid"], "sid": vw[v]["sid"], "sids": vw[v]["sids"]} for v in range(MAXV) if vw[v]["reads"]}
    sm = {"reads": vw[0]["reads"], "session_changes": vw[0]["flips"], "sids": vw[0]["sids"], "views": views, "session_changes_all": sum(x["flips"] for x in vw), "roster": roster,
          "roster_changes": roster_changes}
    log("SUMMARY " + json.dumps(sm, sort_keys=True))
    if a.summary:
        open(a.summary, "w", encoding="utf-8").write(json.dumps(sm, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
