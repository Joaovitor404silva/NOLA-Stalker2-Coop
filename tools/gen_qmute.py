import glob
import os
import re
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GD = os.environ.get("S2_ZONEKIT", r"D:\modding\zk_keep") + r"\Stalker2\Content\GameLite\GameData\QuestNodePrototypes"
PAKSRC = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\paksrc"
OUT = os.path.join(ROOT, "data", "qmute_client.txt")
SEED = os.path.join(ROOT, "tools", "qmute_seed.txt")

STORY = r"^(?:\w+_L_)?(E\d+_MQ|E\d+_Bossfight|E\d+_BossFight|E\d+_DugaAssault|C_E\d+)|Bossfight|^Arch_L_Assault_E\d|^Arch_E\d+_"
SIDE = r"^(?:\w+_L_)?(SQ\d|RSQ\d|SEQ\d|EQ\d|ANCQ_?\d|E\d+_SQ|E\d+_EQ)"
FAMILY = re.compile(STORY + "|" + SIDE)
STORY_RX = re.compile(STORY)
REPORT = ("_S2E",)

OPEN = re.compile(r"^\s*(\S+)\s*:\s*struct\.begin")
CLOSE = re.compile(r"^\s*struct\.end")
KV = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
CUT = re.compile(r"^C_|Cutscene")
TRADE = re.compile(r"Trad|Barter|Vendor|Shop", re.I)
KEEP = ("StartForcedDialog", "IsComment", "OverrideDialogTopic", "DialogChainPrototypeSID", "LoopSequence")
MEMBER = re.compile(r"^\s*\[\d+\]\s*=\s*(\S*)\s*$")


def parse(path, out):
    st = []
    n = None
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for ln in f:
            m = OPEN.match(ln)
            if m:
                st.append(m.group(1))
                if len(st) == 1:
                    n = {"sid": m.group(1), "t": "", "q": "", "cq": ""}
                continue
            if CLOSE.match(ln):
                if len(st) == 1 and n:
                    out[n["sid"]] = n
                    n = None
                if st:
                    st.pop()
                continue
            if n is not None and len(st) == 2 and st[1] == "DialogMembers":
                dm = MEMBER.match(ln)
                if dm and dm.group(1):
                    n["dm"] = n.get("dm", 0) + 1
                continue
            m = KV.match(ln)
            if not m or n is None or len(st) != 1:
                continue
            k, v = m.groups()
            if k == "NodeType":
                n["t"] = v.split("::")[-1]
            elif k == "QuestSID":
                n["q"] = v
            elif k == "ContaineredQuestPrototypeSID":
                n["cq"] = v
            elif k in KEEP:
                n[k] = v


def load(root):
    nodes = {}
    for p in sorted(glob.glob(os.path.join(root, "**", "*.cfg"), recursive=True)):
        parse(p, nodes)
    return nodes


def main():
    nodes = load(GD)
    if not nodes:
        sys.exit("no GameData quest nodes under " + GD)
    byq = defaultdict(list)
    for s, d in nodes.items():
        byq[d["q"]].append(s)
    live, frontier, stack = {"rootgraph"}, {}, ["rootgraph"]
    while stack:
        q = stack.pop()
        for s in byq.get(q, ()):
            d = nodes[s]
            if d["t"] not in ("Container", "ScheduledContainer") or not d["cq"]:
                continue
            if FAMILY.search(d["cq"]):
                frontier[s] = d["cq"]
                continue
            if d["cq"] not in live:
                live.add(d["cq"])
                stack.append(d["cq"])
    children = defaultdict(set)
    for s, d in nodes.items():
        if d["t"] in ("Container", "ScheduledContainer") and d["cq"]:
            children[d["q"]].add(d["cq"])
    quests = set(byq) | {c for cs in children.values() for c in cs}
    muted = {}
    for q in sorted(quests):
        if q in live:
            continue
        if FAMILY.search(q):
            muted[q] = "historia" if STORY_RX.search(q) else "secundaria"
    for cq in set(frontier.values()):
        if cq not in muted and cq not in live:
            muted[cq] = "historia" if STORY_RX.search(cq) else "secundaria"
    stack = list(muted)
    while stack:
        q = stack.pop()
        for c in children.get(q, ()):
            if c not in muted and c not in live:
                muted[c] = "dentro de " + q
                stack.append(c)

    seed = []
    if os.path.exists(SEED):
        with open(SEED, encoding="utf-8") as f:
            seed = [ln.strip() for ln in f if ln.strip()]
    low = {k.lower(): k for k in nodes}
    uncovered = []
    for s in seed:
        k = low.get(s.lower())
        if s in frontier or (k and nodes[k]["q"] in muted):
            continue
        uncovered.append(s)
    extra_front = sorted(set(frontier) - set(seed))

    pak = {}
    if os.path.isdir(PAKSRC):
        for d in sorted(os.listdir(PAKSRC)):
            tmp = load(os.path.join(PAKSRC, d))
            for s, n in tmp.items():
                n["dir"] = d
                pak[s] = n
    inside = Counter()
    for s, n in pak.items():
        if n["q"] in muted:
            m = re.search(r"(_S2[A-Z0-9]+)$", s)
            inside[(n["dir"], m.group(1) if m else "-")] += 1
    s2c_bad = sorted(s for s, n in pak.items() if s.endswith("_S2C") and n["q"] in muted)
    if s2c_bad:
        sys.exit("host replay copies inside a muted quest: " + " ".join(s2c_bad[:10]))

    rows = []
    for q in sorted(muted):
        rows.append("Q\t%s\t%s\n" % (q, muted[q]))
    for s in sorted(frontier):
        rows.append("N\t%s\tabre %s\n" % (s, frontier[s]))
    for s in sorted(set(seed) - set(frontier)):
        rows.append("N\t%s\tabertura (lista antiga)\n" % s)
    for x in REPORT:
        rows.append("P\t%s\trelato de evento ao host\n" % x)
    dlg = sorted(s for s, d in nodes.items() if d["t"] == "SetDialog" and d["q"] in muted)
    for s in dlg:
        rows.append("D\t%s\tSetDialog de missao so do host\n" % s)
    scn = {}
    for s in dlg:
        d = nodes[s]
        if d.get("StartForcedDialog") != "true" or d.get("IsComment") != "false":
            continue
        if d.get("OverrideDialogTopic") in ("EOverrideDialogTopic::Trade", "EOverrideDialogTopic::Upgrade") or TRADE.search(d.get("DialogChainPrototypeSID", "")):
            continue
        scn[s] = "S" if not d.get("dm", 0) else "T"
    for s, d in nodes.items():
        if d["q"] not in muted:
            continue
        if d["t"] == "PlayVideo" or (d["t"] == "SequenceStart" and CUT.search(d["q"]) and d.get("LoopSequence") == "false"):
            scn[s] = "V"
    desc = {"S": "cena de dialogo sem NPC no no, o host abre e os clientes seguem", "T": "dialogo de NPC forcado, o host abre e os clientes seguem", "V": "cutscene de video ou sequencia, o host inicia e os clientes seguem"}
    for s in sorted(scn):
        rows.append("%s\t%s\t%s\n" % (scn[s], s, desc[scn[s]]))
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(rows)

    fam = Counter(v if not v.startswith("dentro") else "dentro" for v in muted.values())
    print("%d quest nodes, %d quests, live %d, muted %d %s, frontier containers %d, SetDialog %d, scenes %s -> %s" % (len(nodes), len(quests), len(live), len(muted), dict(fam), len(frontier), len(dlg), dict(Counter(scn.values())), OUT))
    print("seed %d: covered %d, uncovered %d %s; frontier not in seed %d %s" % (len(seed), len(seed) - len(uncovered), len(uncovered), uncovered[:8], len(extra_front), extra_front[:8]))
    print("NOLA paksrc nodes inside muted quests (dir, suffix): " + " ".join("%s/%s=%d" % (k[0], k[1], v) for k, v in sorted(inside.items())))


if __name__ == "__main__":
    main()
