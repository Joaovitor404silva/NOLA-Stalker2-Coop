import json
import math
import os
import sys

TAP_MS = 300
TRACK_STEP = 800.0
TRACK_TURN = 30.0
TRACK_DZ = 150.0
WALK, FALL = 1, 3


def load(path):
    ev = []
    with open(path, encoding="utf-8", errors="replace") as f:
        for ln in f:
            ln = ln.strip()
            if ln:
                try:
                    ev.append(json.loads(ln))
                except ValueError:
                    pass
    return ev


def d2(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def split_obj(s):
    t = (s or "").split()
    if not t:
        return None
    return {"cls": t[1] if len(t) > 1 else "?", "name": t[2] if len(t) > 2 else (t[1] if len(t) > 1 else "?")}


class Seg:
    def __init__(self, name):
        self.name = name
        self.goals = []
        self.track = []

    def add_track(self, p):
        if not self.track:
            self.track.append(p)
            return
        last = self.track[-1]
        if d2(p, last) >= TRACK_STEP or abs(p[2] - last[2]) >= TRACK_DZ:
            self.track.append(p)
            return
        if len(self.track) >= 2:
            a, b = self.track[-2], last
            h1 = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
            h2 = math.degrees(math.atan2(p[1] - b[1], p[0] - b[0]))
            dh = abs((h2 - h1 + 180) % 360 - 180)
            if dh >= TRACK_TURN and d2(p, last) >= 250:
                self.track.append(p)

    def flush_nav(self, to=None, tol=150):
        pts = self.track[:]
        if to is not None:
            pts.append(to)
        if len(pts) < 2 and to is None:
            return
        dest = pts[-1]
        via = [list(map(lambda v: round(v), q)) for q in pts[1:-1]]
        if self.goals and self.goals[-1].get("do") == "nav" and d2(self.goals[-1]["to"], dest) < 100:
            self.track = [dest]
            return
        self.goals.append({"do": "nav", "to": [round(v) for v in dest], "tol": tol, "via": via})
        self.track = [dest]


def compile_rec(events):
    meta = next((e for e in events if e.get("ev") == "start"), {})
    segs = [Seg("seg0")]
    cur = segs[0]
    keydown = {}
    fire_t = None
    ladder = None
    for e in events:
        k = e.get("ev")
        if k == "pos":
            cur.add_track(e["p"])
        elif k == "mark":
            cur.flush_nav()
            cur = Seg(e.get("text") or "seg%d" % len(segs))
            segs.append(cur)
            if e.get("p"):
                cur.track = [e["p"]]
        elif k == "interact":
            o = split_obj(e.get("actor"))
            stand = e["p"]
            cur.flush_nav(stand, tol=80)
            g = {"do": "interact", "act": e["act"], "ms": e["ms"], "stand": [round(v) for v in stand], "yaw": e.get("yaw"), "pitch": e.get("pitch")}
            if o:
                g["actor"] = o["name"]
                g["cls"] = o["cls"]
                g["apos"] = [round(v) for v in e.get("apos", [0, 0, 0])]
            c = split_obj(e.get("comp"))
            if c:
                g["comp_cls"] = c["cls"]
            if not o:
                g["note"] = "no aimed actor at press"
            cur.goals.append(g)
        elif k == "mode":
            fr, to = e.get("from"), e.get("to")
            if fr == WALK and to not in (WALK, FALL) and ladder is None:
                cur.flush_nav(e["p"], tol=80)
                ladder = {"enter": [round(v) for v in e["p"]], "mode": to, "cmode": e.get("cto"), "yaw": e.get("yaw")}
            elif ladder is not None and to == WALK:
                ladder["exit"] = [round(v) for v in e["p"]]
                cur.goals.append(dict({"do": "traverse", "kind": "ladder" if ladder["mode"] in (5, 6) else "mode%d" % ladder["mode"]}, **ladder))
                ladder = None
                cur.track = [e["p"]]
        elif k == "jump":
            cur.flush_nav(e["p"], tol=100)
            cur.goals.append({"do": "jump", "at": [round(v) for v in e["p"]], "yaw": e.get("yaw")})
        elif k == "crouch":
            cur.goals.append({"do": "crouch", "on": e.get("on"), "at": [round(v) for v in e["p"]]})
        elif k == "equip":
            cur.goals.append({"do": "equip", "type": e.get("to")})
        elif k == "key":
            name, down = e.get("k"), e.get("down")
            if name == "LMB":
                if down:
                    fire_t = e["t"]
                elif fire_t is not None:
                    cur.goals.append({"do": "fire", "ms": e.get("ms", e["t"] - fire_t)})
                    fire_t = None
            elif down and name in ("G", "V", "Q", "H", "R", "Tab", "Enter", "1", "2", "3", "4", "5", "6", "7", "8", "9", "I", "M"):
                keydown[name] = e["t"]
            elif not down and name in keydown:
                cur.goals.append({"do": "key", "k": name, "ms": e.get("ms", 0), "hint": "dialogue/menu/use; replay maps it to a stock call"})
                keydown.pop(name, None)
        elif k == "quest":
            cur.goals.append({"do": "expect_quest", "node": e.get("node"), "state": e.get("state")})
        elif k == "stop":
            cur.flush_nav()
    out = {"format": "nbot-script-1", "source_role": meta.get("role"), "port": meta.get("port"), "name": meta.get("name"), "segments": []}
    for s in segs:
        if s.goals:
            out["segments"].append({"seg": s.name, "goals": s.goals})
    return out


def dumps(res):
    head = {k: v for k, v in res.items() if k != "segments"}
    lines = ["{", '"meta": %s,' % json.dumps(head), '"segments": [']
    for i, s in enumerate(res["segments"]):
        lines.append(' {"seg": %s, "goals": [' % json.dumps(s["seg"]))
        lines.append(",\n".join("  " + json.dumps(g) for g in s["goals"]))
        lines.append(" ]}" + ("," if i + 1 < len(res["segments"]) else ""))
    lines.append("]}")
    return "\n".join(lines) + "\n"


def main():
    if len(sys.argv) < 2:
        print("usage: reccompile.py <rec.jsonl> [out.json]")
        return 2
    src = sys.argv[1]
    dst = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(src)[0] + ".script.json"
    res = compile_rec(load(src))
    res["source"] = os.path.abspath(src)
    with open(dst, "w", encoding="utf-8", newline="\n") as f:
        f.write(dumps(res))
    n = sum(len(s["goals"]) for s in res["segments"])
    print("%s: %d segments, %d goals" % (dst, len(res["segments"]), n))
    return 0


if __name__ == "__main__":
    sys.exit(main())
