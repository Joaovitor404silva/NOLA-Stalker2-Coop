import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GD = os.environ.get("S2_ZONEKIT", r"D:\modding\zk_keep") + r"\Stalker2\Content\GameLite\GameData\QuestNodePrototypes"
OUT = os.path.join(ROOT, "data", "s2u_lockbehind.tsv")
TRIG = os.path.join(ROOT, "data", "s2u_triggers.tsv")

OPEN = re.compile(r"^\s*(\S+)\s*:\s*struct\.begin")
CLOSE = re.compile(r"^\s*struct\.end")
KV = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")

CLOSE_WORDS = ("close", "shut", "lock", "block", "seal", "restrict")
DOOR_WORDS = ("door", "gate", "hatch", "elevator", "locker", "ladder", "stairs", "barrier", "grate")
OFF_WORDS = ("receiveroff", "_off", "disable", "hide", "collision")
OPEN_WORDS = ("open", "enable", "receiveron", "_on", "show")
SEAL_TYPES = ("SendSignal", "ActivateRestrictor", "SetSpaceRestrictor")


def parse_text(text, nodes):
    depth = 0
    node = None
    for ln in text.splitlines():
        m = OPEN.match(ln)
        if m:
            depth += 1
            if depth == 1:
                node = {"sid": m.group(1), "q": "", "type": "", "event": "", "guid": "", "recv": "", "from": []}
            continue
        if CLOSE.match(ln):
            if depth == 1 and node:
                nodes[node["sid"]] = node
            depth -= 1
            if depth == 0:
                node = None
            continue
        m = KV.match(ln)
        if not m or not node:
            continue
        k, v = m.group(1), m.group(2)
        if depth == 1:
            if k == "QuestSID":
                node["q"] = v
            elif k == "NodeType":
                node["type"] = v.split("::")[-1]
            elif k == "EventType":
                node["event"] = v.split("::")[-1]
            elif k == "TriggerQuestGuid":
                node["guid"] = v.upper()
            elif k == "SignalReceiverGuid":
                node["recv"] = v.upper()
        elif k == "SID":
            node["from"].append(v)


def parse(path, nodes):
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        parse_text(f.read(), nodes)


def is_enter_trigger(n):
    if n["type"] != "Trigger" or not n["guid"].strip("0A"):
        return False
    return n["event"] in ("", "OnTriggerEnter")


def classify(sid):
    s = sid.lower()
    o = any(w in s for w in OPEN_WORDS) or "unlock" in s or "unblock" in s
    s = s.replace("unlock", "").replace("unblock", "")
    c = any(w in s for w in CLOSE_WORDS)
    off = any(w in s for w in OFF_WORDS)
    door = any(w in s for w in DOOR_WORDS)
    if o and (c or off):
        return "ambiguous"
    if o:
        return "open"
    if c or (off and door):
        return "close"
    if off:
        return "off"
    return "unknown"


def trigger_sids():
    out = set()
    if not os.path.exists(TRIG):
        return out
    with open(TRIG, encoding="utf-8") as h:
        for ln in h:
            c = ln.rstrip("\n").split("\t")
            if len(c) >= 11 and len(c[10]) == 32:
                out.add(c[0])
    return out


def rows_of(nodes, geo):
    rows = []
    for n in nodes.values():
        if n["type"] not in SEAL_TYPES:
            continue
        for src in n["from"]:
            t = nodes.get(src)
            if not t or not is_enter_trigger(t):
                continue
            kind = classify(n["sid"]) if n["type"] == "SendSignal" else "restrictor"
            if kind == "open":
                continue
            rows.append((n["q"], t["sid"], n["sid"], t["guid"], n["recv"], kind, "1" if t["sid"] in geo else "0"))
    return sorted(set(rows))


def main():
    gd = sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("--") else GD
    nodes = {}
    for f in sorted(os.listdir(gd)):
        if f.endswith(".cfg"):
            parse(os.path.join(gd, f), nodes)
    geo = trigger_sids()
    if "--stats" in sys.argv:
        succ = Counter()
        for n in nodes.values():
            for src in n["from"]:
                t = nodes.get(src)
                if t and is_enter_trigger(t):
                    succ[n["type"]] += 1
        for k, v in succ.most_common():
            print("%6d %s" % (v, k))
        return
    rows = rows_of(nodes, geo)
    with open(OUT, "w", encoding="utf-8", newline="\n") as o:
        o.write("quest\ttrigger\tnode\ttrigguid\treceiver\tkind\tgeo\n")
        for r in rows:
            o.write("\t".join(r) + "\n")
    kinds = Counter(r[5] for r in rows)
    print("%d lock-behind rows (%d quests, %d triggers) -> %s; close=%d ambiguous=%d unknown=%d; without trigger geometry=%d" % (
        len(rows), len({r[0] for r in rows}), len({r[1] for r in rows}), OUT, kinds["close"], kinds["ambiguous"], kinds["unknown"],
        sum(1 for r in rows if r[6] == "0")))


if __name__ == "__main__":
    main()
