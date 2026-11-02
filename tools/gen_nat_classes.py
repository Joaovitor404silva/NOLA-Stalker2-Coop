import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(ROOT, ".."))
CFG = r"D:\modding\zk_keep\Stalker2\Content\GameLite\GameData"
OUT = os.path.join(PORT, "data", "nat_classes.tsv")
MONT = os.path.join(PORT, "tests", "nat", "mab_montages.tsv")
ANY_MONT = re.compile(r"AnimMontage'(/Game/[^']*)'")
ANY_BP = re.compile(r"Blueprint'(/Game/GameLite/Blueprints/Characters/[^']*)'")
TOP = re.compile(r"^(\S+)\s*:\s*struct\.begin(?:\s*\{([^}]*)\})?")
OPEN = re.compile(r"^\s*(\S+)\s*:\s*struct\.begin")
END = re.compile(r"^\s*struct\.end")
KV = re.compile(r"^\s*([A-Za-z0-9_]+)\s*=\s*(.*?)\s*(?://.*)?$")
KINDS = ("human", "mutant", "mutant_phantom")
SPEEDS = (("WalkSpeed", "walk"), ("RunSpeed", "run"), ("SprintSpeed", "sprint"))
INVIS = (("ToInvisibleSeconds", "invis"), ("ToVisibleSeconds", "tovis"))


def cfg_files(base):
    out = [os.path.join(base, "ObjPrototypes.cfg")]
    sub = os.path.join(base, "ObjPrototypes")
    for dp, _, fs in os.walk(sub):
        for f in sorted(fs):
            if f.lower().endswith(".cfg"):
                out.append(os.path.join(dp, f))
    return [p for p in out if os.path.isfile(p)]


def kind_of(bp):
    p = bp.lower()
    if "/characters/player/" in p:
        return "player"
    if "/monster/" in p and "/phantoms/" in p:
        return "mutant_phantom"
    if "/monster/" in p:
        return "mutant"
    if "/npc/" in p:
        return "human"
    return "other"


def refs(attr):
    d = {}
    for part in (attr or "").split(";"):
        if "=" in part:
            k, v = part.split("=", 1)
            d[k.strip()] = v.strip()
    return d


def parse(path, cache):
    path = os.path.normcase(os.path.normpath(path))
    if path in cache:
        return cache[path]
    structs = {}
    order = []
    cache[path] = (structs, order)
    if not os.path.isfile(path):
        return cache[path]
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        lines = f.read().splitlines()
    cur = None
    stack = []
    for ln in lines:
        if cur is None:
            m = TOP.match(ln)
            if m:
                r = refs(m.group(2))
                cur = {"name": m.group(1), "sid": None, "bp": None, "hp": None, "walk": None, "run": None, "sprint": None, "invis": None, "tovis": None, "refurl": r.get("refurl"), "refkey": r.get("refkey"), "file": path, "bpall": []}
                stack = []
            continue
        if END.match(ln):
            if not stack:
                structs.setdefault(cur["name"], cur)
                order.append(cur)
                cur = None
            else:
                stack.pop()
            continue
        m = OPEN.match(ln)
        if m:
            if not stack and m.group(1) == "InvisibilityFeatureData" and cur["invis"] is None:
                cur["invis"] = 1.0
            stack.append(m.group(1))
            continue
        for b in ANY_BP.finditer(ln):
            cur["bpall"].append(b.group(1))
        m = KV.match(ln)
        if not m:
            continue
        k, v = m.group(1), m.group(2)
        if not stack and k == "SID":
            cur["sid"] = v
        elif not stack and k == "Blueprint":
            b = ANY_BP.search(v)
            if b:
                cur["bp"] = b.group(1)
        elif stack == ["VitalParams"] and k == "MaxHP":
            try:
                cur["hp"] = float(v.rstrip("fF"))
            except ValueError:
                pass
        elif stack == ["MovementParams"] and k in dict(SPEEDS):
            try:
                cur[dict(SPEEDS)[k]] = float(v.rstrip("fF"))
            except ValueError:
                pass
        elif stack == ["InvisibilityFeatureData"] and k in dict(INVIS):
            try:
                cur[dict(INVIS)[k]] = max(0.001, float(v.rstrip("fF")))
            except ValueError:
                pass
    return cache[path]


def parent(s, cache):
    key = s.get("refkey")
    if not key:
        return None
    url = s.get("refurl")
    f = os.path.join(os.path.dirname(s["file"]), url) if url else s["file"]
    structs, order = parse(f, cache)
    m = re.fullmatch(r"\[(\d+)\]", key)
    p = order[int(m.group(1))] if m and int(m.group(1)) < len(order) else structs.get(key)
    return p if p is not s else None


def resolved(s, field, cache):
    seen = set()
    while s is not None and id(s) not in seen:
        seen.add(id(s))
        if s.get(field) is not None:
            return s[field]
        s = parent(s, cache)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cfg", default=CFG)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--mont", default=MONT)
    a = ap.parse_args()
    cache = {}
    cls = {}
    for p in cfg_files(a.cfg):
        _, order = parse(p, cache)
        for s in order:
            for bp in s["bpall"]:
                if kind_of(bp) in KINDS:
                    cls.setdefault(bp, {"own": [], "inh": []})
    par = set()
    for p in cfg_files(a.cfg):
        _, order = parse(p, cache)
        for s in order:
            q = parent(s, cache)
            if q is not None:
                par.add(id(q))
    for p in cfg_files(a.cfg):
        _, order = parse(p, cache)
        for s in order:
            sid = s.get("sid") or s["name"]
            if sid.startswith("["):
                continue
            bp = resolved(s, "bp", cache)
            if bp not in cls:
                continue
            hp = resolved(s, "hp", cache)
            sp = tuple(resolved(s, f, cache) for _, f in SPEEDS)
            iv = tuple(resolved(s, f, cache) for _, f in INVIS)
            cls[bp]["own" if s.get("bp") else "inh"].append((sid, hp, id(s) not in par, sp, iv))
    rows = []
    for bp, v in cls.items():
        name = bp.rsplit(".", 1)[-1] + "_C"
        all_ = v["own"] + v["inh"]
        stem = name[len("BP_AI_"):-2].lower() if name.startswith("BP_AI_") else ""
        pick = [e for e in all_ if e[0].lower() == stem] or v["own"] or all_
        sid = pick[0][0] if pick else ""
        hps = [e[1] for e in all_ if e[1] is not None]
        sp = pick[0][3] if pick else (None, None, None)
        iv = pick[0][4] if pick else (None, None)
        rows.append((name, kind_of(bp), sid, max(hps) if hps else 0.0, len(all_), sp, iv))
    order = {k: i for i, k in enumerate(KINDS)}
    rows.sort(key=lambda r: (order[r[1]], r[0]))
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("k1\tk2\tt\tv\n")
        for name, kind, sid, hp, n, sp, iv in rows:
            f.write("s%s\tskind\ts\t%s\n" % (name, kind))
            if sid:
                f.write("s%s\tssid\ts\t%s\n" % (name, sid))
            f.write("s%s\tshpmax\tf\t%g\n" % (name, hp))
            for (_, fld), v in zip(SPEEDS, sp):
                if v is not None:
                    f.write("s%s\ts%s\tf\t%g\n" % (name, fld, v))
            if iv[0] is not None:
                for (_, fld), v in zip(INVIS, iv):
                    if v is not None:
                        f.write("s%s\ts%s\tf\t%g\n" % (name, fld, v))
    for name, kind, sid, hp, n, sp, iv in rows:
        inv = " invis=%g/%g" % (iv[0], iv[1] or 0) if iv[0] is not None else ""
        print("%-34s %-15s %-40s hpmax=%-7g sids=%d %s%s" % (name, kind, sid or "-", hp, n, "/".join("-" if v is None else "%g" % v for v in sp), inv))
    print("classes=%d out=%s" % (len(rows), a.out))
    mont = set()
    for dp, _, fs in os.walk(os.path.join(a.cfg, "AbilityPrototypes")):
        for fn in sorted(fs):
            if fn.lower().endswith(".cfg"):
                with open(os.path.join(dp, fn), encoding="utf-8-sig", errors="replace") as g:
                    for ln in g:
                        for m in ANY_MONT.finditer(ln):
                            mont.add((fn[:-4], m.group(1)))
    with open(a.mont, "w", encoding="utf-8", newline="\n") as f:
        f.write("file\tmontage\n")
        for fn, p in sorted(mont):
            f.write("%s\t%s\n" % (fn, p))
    print("ability_montages=%d files=%d out=%s" % (len(mont), len({m[0] for m in mont}), a.mont))
    return 0


if __name__ == "__main__":
    sys.exit(main())
