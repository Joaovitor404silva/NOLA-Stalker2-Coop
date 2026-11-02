import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(ROOT, "data", "s2u_triggers.tsv")
GD = r"D:\modding\zk_keep\Stalker2\Content\GameLite\GameData"
QN = sys.argv[1] if len(sys.argv) > 1 else os.path.join(GD, "QuestNodePrototypes")
SP = sys.argv[2] if len(sys.argv) > 2 else os.path.join(GD, "SpawnActorPrototypes")
SUFFIX = "_CondTrigger"

top = re.compile(r"^(\S+)\s*:\s*struct\.begin")
key = re.compile(r"^\s*TriggerQuestGuid\s*=\s*([0-9A-Fa-f]{32})\s*$")
qsid = re.compile(r"^\s*QuestSID\s*=\s*(\S+)\s*$")
ctype = re.compile(r"^\s*ConditionType\s*=\s*EQuestConditionType::Trigger\s*$")
ctrig = re.compile(r"^\s*Trigger\s*=\s*([0-9A-Fa-f]{32})\s*$")
hexname = re.compile(r"^[0-9A-Fa-f]{32}\.cfg$")
prop = re.compile(r"^\s*(\w+)\s*=\s*(\S+)\s*$")


def scan():
    out, cond = {}, {}
    for dp, _, fs in os.walk(QN):
        for f in fs:
            if not f.endswith(".cfg"):
                continue
            cur, quest, armed = None, "", False
            with open(os.path.join(dp, f), encoding="utf-8-sig", errors="replace") as h:
                for ln in h:
                    m = top.match(ln)
                    if m:
                        cur, quest, armed = m.group(1), "", False
                        continue
                    m = qsid.match(ln)
                    if m and cur and not quest:
                        quest = m.group(1)
                        continue
                    m = key.match(ln)
                    if m and cur and cur not in out:
                        g = m.group(1).upper()
                        if g.strip("0"):
                            out[cur] = g
                        continue
                    if ctype.match(ln):
                        armed = True
                        continue
                    m = ctrig.match(ln)
                    if m and armed:
                        armed = False
                        g = m.group(1).upper()
                        if cur and g.strip("0") and g not in cond:
                            cond[g] = (cur, quest or os.path.splitext(f)[0])
    return out, cond


def spawn_index():
    idx = {}
    for dp, _, fs in os.walk(SP):
        for f in fs:
            if hexname.match(f):
                idx.setdefault(f[:32].upper(), os.path.join(dp, f))
    return idx


def geometry(path):
    p = {}
    with open(path, encoding="utf-8-sig", errors="replace") as h:
        for ln in h:
            m = prop.match(ln)
            if m and m.group(1) not in p:
                p[m.group(1)] = m.group(2)
    if p.get("SpawnType") != "ESpawnType::Trigger":
        return None
    try:
        v = [float(p[k]) for k in ("PositionX", "PositionY", "PositionZ", "RotatorAngleYaw", "ScaleX", "ScaleY", "ScaleZ")]
    except (KeyError, ValueError):
        return None
    shape = p.get("TriggerShape", "ETriggerShape::Box").split("::")[-1]
    return v, shape


def num(x):
    return f"{x:.1f}"


def main():
    g, cond = scan()
    with open(TSV, encoding="utf-8", newline="") as h:
        rows = h.read().splitlines()
    res, hit = [], 0
    for ln in rows:
        c = ln.split("\t")
        if len(c) < 5:
            res.append(ln)
            continue
        if SUFFIX in c[0]:
            continue
        c = c[:10]
        while len(c) < 10:
            c.append({5: "0.0", 6: "100", 7: "100", 8: "100", 9: "Box"}.get(len(c), ""))
        v = g.get(c[0], "-")
        hit += v != "-"
        res.append("\t".join(c + [v]))
    base = len(res)
    idx = spawn_index()
    miss = 0
    used = {}
    for guid in sorted(cond, key=lambda k: (cond[k][1], cond[k][0], k)):
        node, quest = cond[guid]
        gm = geometry(idx[guid]) if guid in idx else None
        if not gm:
            miss += 1
            continue
        v, shape = gm
        used[node] = used.get(node, 0) + 1
        name = node + SUFFIX + ("" if used[node] == 1 else f"_{used[node]}")
        res.append("\t".join([name, quest, num(v[0]), num(v[1]), num(v[2]), num(v[3]), num(v[4]), num(v[5]), num(v[6]), shape, guid]))
    with open(TSV, "w", encoding="utf-8", newline="\n") as h:
        h.write("\n".join(res) + "\n")
    print(f"gen_trigguid: {len(res)} rows, {hit} with TriggerQuestGuid, {len(g)} guid nodes in {QN}; {len(res) - base} {SUFFIX} rows added, {miss} cond guids without trigger geometry")


if __name__ == "__main__":
    main()
