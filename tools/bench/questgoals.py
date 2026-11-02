import os
import re
import sys

QDIR = r"C:\modding\s2update\gd\paks_d11a43b457411b559e574d402fc7a314\Stalker2\Content\GameLite_text\GameData\QuestNodePrototypes"
TRIG = r"D:\modding\cpp\S2CoopPort\data\s2u_triggers.tsv"
EVENT_TYPES = {"Trigger", "OnSignalReceived", "OnInteractEvent", "OnPlayerGetItemEvent", "OnNPCDeathEvent", "OnJournalQuestEvent", "OnHitEvent", "OnItemContainerLoot"}


def parse_nodes(text):
    nodes = {}
    cur = None
    depth = 0
    path = []
    for raw in text.splitlines():
        line = raw.split("//")[0].strip()
        if not line:
            continue
        m = re.match(r"^([\w\[\]\*]+)\s*:\s*struct\.begin", line)
        if m:
            if depth == 0:
                cur = {"name": m.group(1), "launch": [], "marker": None}
                path = []
            else:
                path.append(m.group(1))
            depth += 1
            continue
        if line.startswith("struct.end"):
            depth -= 1
            if depth == 0 and cur:
                nodes[cur.get("SID", cur["name"])] = cur
                cur = None
            elif path:
                path.pop()
            continue
        if cur is None or "=" not in line:
            continue
        k, v = [x.strip() for x in line.split("=", 1)]
        if path and path[0] == "Launchers" and k == "SID":
            cur["launch"].append(v)
            continue
        if path and path[0] == "Markers" and "MarkerLocation" in path and k in ("X", "Y", "Z"):
            if cur["marker"] is None:
                cur["marker"] = {}
            if len(cur["marker"]) < 3 and k not in cur["marker"]:
                cur["marker"][k] = float(v)
            continue
        if not path:
            cur[k] = v
    return nodes


def node_type(n):
    return n.get("NodeType", "").replace("EQuestNodeType::", "")


def load_triggers(path):
    t = {}
    if not os.path.exists(path):
        return t
    for ln in open(path, encoding="utf-8"):
        f = ln.rstrip("\n").split("\t")
        if len(f) >= 5:
            try:
                t[f[0]] = (float(f[2]), float(f[3]), float(f[4]))
            except ValueError:
                pass
    return t


def events_of(nodes, sid, depth=0, seen=None):
    seen = seen if seen is not None else set()
    if sid in seen or depth > 4 or sid not in nodes:
        return []
    seen.add(sid)
    n = nodes[sid]
    if node_type(n) in EVENT_TYPES:
        return [sid]
    out = []
    for l in n["launch"]:
        if l in nodes and node_type(nodes[l]) == "SetJournal":
            continue
        out += events_of(nodes, l, depth + 1, seen)
    return out


def build(qdir, trig):
    rows = []
    for fn in sorted(os.listdir(qdir)):
        if not fn.endswith(".cfg"):
            continue
        nodes = parse_nodes(open(os.path.join(qdir, fn), encoding="utf-8", errors="replace").read())
        rows += stage_rows(nodes, trig)
    return rows


def stage_rows(nodes, trig):
    rows = []
    starts, finishes = {}, {}
    for sid, n in nodes.items():
        if node_type(n) != "SetJournal" or n.get("JournalEntity", "") != "EJournalEntity::QuestStage":
            continue
        st = n.get("JournalQuestStageSID", "")
        act = n.get("JournalAction", "")
        if act.endswith("Start"):
            starts.setdefault(st, []).append(sid)
        elif act.endswith("Finish"):
            finishes.setdefault(st, []).append(sid)
    for st, ss in starts.items():
        quest = nodes[ss[0]].get("QuestSID", "")
        mk = next((nodes[s]["marker"] for s in ss if nodes[s]["marker"] and len(nodes[s]["marker"]) == 3), None)
        evs = []
        for f in finishes.get(st, []):
            for l in nodes[f]["launch"]:
                for e in events_of(nodes, l):
                    if e not in evs:
                        evs.append(e)
        goal = None
        for e in evs:
            n = nodes[e]
            if node_type(n) == "Trigger" and not n.get("EventType", "").endswith("OnTriggerExit") and e in trig:
                goal = ("trigger", e, trig[e])
                break
        if goal is None and mk:
            goal = ("marker", ss[0], (mk["X"], mk["Y"], mk["Z"]))
        g = goal or ("none", "-", (0.0, 0.0, 0.0))
        m = (mk["X"], mk["Y"], mk["Z"]) if mk else (0.0, 0.0, 0.0)
        rows.append([quest, st, ",".join(ss), ",".join(finishes.get(st, [])) or "-", "%.1f" % m[0], "%.1f" % m[1], "%.1f" % m[2],
                     g[0], g[1], "%.1f" % g[2][0], "%.1f" % g[2][1], "%.1f" % g[2][2], ",".join(evs) or "-"])
    return rows


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "nbot", "quest_goals.tsv")
    qdir = sys.argv[2] if len(sys.argv) > 2 else QDIR
    rows = build(qdir, load_triggers(TRIG))
    rows.sort(key=lambda r: (r[0], r[1]))
    with open(out, "w", encoding="utf-8", newline="\n") as fp:
        for r in rows:
            fp.write("\t".join(r) + "\n")
    kinds = {}
    for r in rows:
        kinds[r[7]] = kinds.get(r[7], 0) + 1
    print("stages=%d %s -> %s" % (len(rows), kinds, out))


if __name__ == "__main__":
    main()
