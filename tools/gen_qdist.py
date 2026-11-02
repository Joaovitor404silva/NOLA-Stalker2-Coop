import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "data", "s2u_triggers.tsv")
GD = r"D:\modding\zk_keep\Stalker2\Content\GameLite\GameData"
QN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(GD, "QuestNodePrototypes")
SUFFIX = "_CondDist"
PLAYER = "A" * 32

top = re.compile(r"^(\S+)\s*:\s*struct\.begin")
kv = re.compile(r"^\s*(\w+)\s*=\s*(\S+)\s*$")


def scan():
    out = {}
    for dp, _, fs in os.walk(QN):
        for f in fs:
            if not f.endswith(".cfg"):
                continue
            cur, p, ntype, ncond, dist = None, {}, "", 0, 0

            def flush():
                if cur and ntype == "Condition" and ncond == 1 and dist == 1:
                    try:
                        if (p.get("TargetCharacter") == PLAYER and p.get("ConditionComparance") in ("Less", "LessOrEqual")
                                and p.get("ConditionCheckType", "And") == "And"):
                            if float(p["X"]) == 0 and float(p["Y"]) == 0 and float(p["Z"]) == 0:
                                raise ValueError
                            out[cur] = (p["QuestSID"], float(p["X"]), float(p["Y"]), float(p["Z"]), float(p["NumericValue"]))
                    except (KeyError, ValueError):
                        pass

            with open(os.path.join(dp, f), encoding="utf-8-sig", errors="replace") as h:
                for ln in h:
                    m = top.match(ln)
                    if m:
                        flush()
                        cur, p, ntype, ncond, dist = m.group(1), {}, "", 0, 0
                        continue
                    m = kv.match(ln)
                    if not m or not cur:
                        continue
                    k, v = m.group(1), m.group(2)
                    if k == "NodeType":
                        ntype = v.split("::")[-1]
                    elif k == "ConditionType":
                        ncond += 1
                        dist += v.endswith("::DistanceToPoint")
                    elif k == "ConditionComparance":
                        p[k] = v.split("::")[-1]
                    elif k == "ConditionCheckType":
                        p[k] = v.split("::")[-1]
                    elif k in ("TargetCharacter", "QuestSID", "NumericValue", "X", "Y", "Z"):
                        p.setdefault(k, v)
            flush()
    return out


def main():
    nodes = scan()
    with open(TSV, encoding="utf-8", newline="") as h:
        rows = [ln for ln in h.read().splitlines() if ln.split("\t")[0].endswith(SUFFIX) is False]
    for sid in sorted(nodes):
        q, x, y, z, r = nodes[sid]
        rows.append("\t".join([sid + SUFFIX, q, f"{x:.1f}", f"{y:.1f}", f"{z:.1f}", "0.0", f"{r:.1f}", f"{r:.1f}", f"{r:.1f}", "Dist"]))
    with open(TSV, "w", encoding="utf-8", newline="\n") as h:
        h.write("\n".join(rows) + "\n")
    print(f"gen_qdist: {len(nodes)} {SUFFIX} rows in {TSV}")


if __name__ == "__main__":
    main()
