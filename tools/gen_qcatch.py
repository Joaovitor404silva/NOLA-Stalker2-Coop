import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = r"D:\modding\zk_keep\Stalker2\Content\GameLite\GameData\QuestNodePrototypes"
PICK = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\paksrc\QuestPick\QuestNodePrototypes"
OUT = os.path.join(ROOT, "data", "s2u_qcatch.tsv")
DEPTH = 3

EVENTS = {
    "Trigger", "OnPlayerGetItemEvent", "OnPlayerLostItemEvent", "OnSignalReceived", "OnInteractEvent", "OnNPCDeathEvent",
    "OnNPCDefeatEvent", "OnKillerCheckEvent", "OnHitEvent", "OnNPCCreateEvent", "OnNPCBecomeEnemyEvent", "OnNPCBecomeFriendEvent",
    "OnFactionBecomeEnemyEvent", "OnFactionBecomeFriendEvent", "OnPlayerNoticedEvent", "OnTickEvent", "OnJournalQuestEvent",
    "OnEmissionStartEvent", "OnEmissionFinishEvent", "OnEmissionStageActivated", "OnEmissionStageFinished", "OnAbilityUsedEvent",
    "OnAbilityEndedEvent", "OnPlayerRankReachedEvent", "OnUpgradeInstallEvent", "OnMoneyAmountReachedEvent", "OnGetCompatibleAttachEvent",
}
STOP = {"SetDialog", "Random", "OnInfotopicFinishEvent", "BridgeEvent", "SetTimer", "Container", "ScheduledContainer", "End"}

KV = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")
OPEN = re.compile(r"^\s*(\S+)\s*:\s*struct\.begin")
CLOSE = re.compile(r"^\s*struct\.end")


def parse(path, nodes):
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        stack = []
        node = None
        for ln in f:
            m = OPEN.match(ln)
            if m:
                stack.append(m.group(1))
                if len(stack) == 1:
                    node = {"type": "", "q": "", "start": False, "alts": []}
                elif node is not None and len(stack) == 3 and stack[1] == "Launchers":
                    node["alts"].append({"ex": False, "c": []})
                elif node is not None and len(stack) == 5 and stack[1] == "Launchers" and stack[3] == "Connections":
                    node["alts"][-1]["c"].append(["", ""])
                continue
            if CLOSE.match(ln):
                if len(stack) == 1 and node is not None and node.get("sid"):
                    nodes[node["sid"]] = node
                    node = None
                if stack:
                    stack.pop()
                continue
            m = KV.match(ln)
            if not m or node is None:
                continue
            k, v = m.group(1), m.group(2)
            d = len(stack)
            if d == 1:
                if k == "SID":
                    node["sid"] = v
                elif k == "NodeType":
                    node["type"] = v.split("::")[-1]
                elif k == "QuestSID":
                    node["q"] = v
                elif k == "LaunchOnQuestStart":
                    node["start"] = v.lower() == "true"
                elif k in ("ItemSID", "ItemsCount", "TargetQuestGuid", "AddToPlayerStash", "ItemPrototypeSID", "ExpectedItemsCount"):
                    node[k] = v
            elif d == 3 and stack[1] == "Launchers" and k == "Excluding" and node["alts"]:
                node["alts"][-1]["ex"] = v.lower() == "true"
            elif d == 5 and stack[1] == "Launchers" and stack[3] == "Connections" and node["alts"] and node["alts"][-1]["c"]:
                if k == "SID":
                    node["alts"][-1]["c"][-1][0] = v
                elif k == "Name":
                    node["alts"][-1]["c"][-1][1] = v


def milestones(pick):
    out = []
    rx = re.compile(r"ConsoleCommand\s*=\s*s2q L (\S+)")
    for fn in sorted(os.listdir(pick)):
        if not fn.endswith(".cfg"):
            continue
        with open(os.path.join(pick, fn), encoding="utf-8-sig", errors="replace") as f:
            for ln in f:
                m = rx.search(ln)
                if m:
                    out.append(m.group(1))
    return out


def dialog_gated(nodes, sid):
    for alt in nodes[sid]["alts"]:
        if alt["ex"]:
            continue
        for c, pin in alt["c"]:
            if c in nodes and nodes[c]["type"] in STOP:
                return True
    return False


def forceable(nodes, sid, pin):
    t = nodes[sid]["type"]
    if t in EVENTS:
        return True
    return t == "Condition" and not pin and not dialog_gated(nodes, sid)


def walk(nodes, mile, sid):
    levels = []
    seen = {sid}
    front = [sid]
    if forceable(nodes, sid, ""):
        levels.append([sid])
    for depth in range(DEPTH):
        nxt, lev = [], []
        for s in front:
            for alt in nodes[s]["alts"]:
                if alt["ex"]:
                    continue
                for c, pin in alt["c"]:
                    if not c or c in seen or c not in nodes:
                        continue
                    seen.add(c)
                    t = nodes[c]["type"]
                    if forceable(nodes, c, pin):
                        lev.append(c)
                    if t in EVENTS or t in STOP or c in mile:
                        continue
                    nxt.append(c)
        if lev:
            levels.append(lev)
        front = nxt
    out = []
    for lev in reversed(levels):
        out.extend(lev)
    return out


def to_player(n):
    if n["type"] != "ItemAdd" or not n.get("ItemSID"):
        return False
    g = n.get("TargetQuestGuid", "")
    return n.get("AddToPlayerStash", "").lower() != "true" and (not g or set(g) == {"A"})


def items(nodes, ev):
    out = []
    for e in ev:
        n = nodes[e]
        if n["type"] == "OnPlayerGetItemEvent" and n.get("ItemPrototypeSID"):
            c = n.get("ExpectedItemsCount", "1")
            out.append("%s=%s:%s" % (e, n["ItemPrototypeSID"], c if c.isdigit() and int(c) > 0 else "1"))
    return out


def djb2(s):
    h = 5381
    for c in s.encode("utf-8"):
        h = (h * 33 + c) & 0xFFFFFFFF
    return h


def main():
    gd =sys.argv[1] if len(sys.argv) > 1 else GAMEDATA
    pick = sys.argv[2] if len(sys.argv) > 2 else PICK
    out = sys.argv[3] if len(sys.argv) > 3 else OUT
    nodes = {}
    for fn in sorted(os.listdir(gd)):
        if fn.endswith(".cfg"):
            parse(os.path.join(gd, fn), nodes)
    ms = milestones(pick)
    mile = set(ms)
    rows, miss, none, adds, gets = [], 0, 0, 0, 0
    for s in sorted(mile):
        if s not in nodes:
            miss += 1
            continue
        ev = walk(nodes, mile, s)
        add = to_player(nodes[s])
        if ev or add:
            it = items(nodes, ev)
            adds += add
            gets += len(it)
            rows.append("%s\t%s\t%s\t%s\t%s\n" % (s, nodes[s]["q"], " ".join(ev) or "-", "A" if add else "-", " ".join(it) or "-"))
        else:
            none += 1
    hs = {}
    for r in rows:
        k = r.split("\t", 1)[0]
        h = djb2(k)
        if h in hs:
            raise SystemExit("djb2 collision: %s %s" % (hs[h], k))
        hs[h] = k
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(rows)
    print("%d nodes, %d milestones, %d rows (%d ItemAdd to the player, %d item events), %d without, %d not in GameData -> %s" % (len(nodes), len(mile), len(rows), adds, gets, none, miss, out))


if __name__ == "__main__":
    main()
