import argparse
import glob
import os
import re
import shutil
import sys
from collections import Counter, defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GD = os.environ.get("S2_ZONEKIT", r"D:\modding\zk_keep") + r"\Stalker2\Content\GameLite\GameData\QuestNodePrototypes"
PAKSRC = os.environ.get("S2_PAKSRC", r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\paksrc")
MUTE = os.path.join(ROOT, "data", "qmute_client.txt")
QEVENT = os.path.join(ROOT, "data", "s2u_qevent.tsv")
QITEMS = os.path.join(ROOT, "data", "s2u_qitems.tsv")
DIR_C = "EffectReplay_ClientOnly"
DIR_H = "EffectObserver_HostOnly"
PLAYER = "A" * 32
TOKEN = re.compile(r"^[A-Za-z0-9_\-\.]+$")
COPY_KEYS = ("Repeatable", "LaunchOnQuestStart", "StartDelay", "BrokenGameDataFilter")
IND = "   "


def blocks(text):
    cur = None
    for ln in text.splitlines():
        if cur is None:
            m = re.match(r"^([^\s:]+)\s*:\s*struct\.begin(.*)$", ln)
            if m:
                cur = (m.group(1), ln, [ln])
        else:
            cur[2].append(ln)
            if ln.rstrip() == "struct.end":
                yield cur
                cur = None


def child(lines, key):
    ind = None
    for ln in lines[1:-1]:
        s = ln.lstrip(" \t")
        lead = len(ln) - len(s)
        if ind is None and s:
            ind = lead
        if lead == ind:
            m = re.match(r"^%s\s*=\s*(.*?)\s*$" % re.escape(key), s)
            if m:
                return m.group(1)
    return None


def sub_block(lines, key):
    ind = None
    for i, ln in enumerate(lines[1:-1], 1):
        s = ln.lstrip(" \t")
        lead = len(ln) - len(s)
        if ind is None and s:
            ind = lead
        if lead != ind:
            continue
        if re.match(r"^%s\s*:\s*struct\.begin" % re.escape(key), s):
            depth, out = 0, []
            for ln2 in lines[i:]:
                out.append(ln2)
                t = ln2.strip()
                if re.search(r"struct\.begin", t):
                    depth += 1
                elif t == "struct.end":
                    depth -= 1
                    if depth == 0:
                        return out
            return out
        if re.match(r"^%s\s*=\s*$" % re.escape(key), s):
            return []
    return None


def client_copy(name, lines):
    out, skip, depth, qs = [], False, 0, False
    for i, ln in enumerate(lines):
        s = ln.strip()
        if i == 0:
            out.append(ln.replace(name, name + "_S2C", 1))
            continue
        if skip:
            if re.search(r"struct\.begin", s):
                depth += 1
            elif s == "struct.end":
                depth -= 1
                if depth == 0:
                    skip = False
            continue
        if re.match(r"^Launchers\s*:\s*struct\.begin", s):
            skip, depth = True, 1
            continue
        if re.match(r"^Launchers\s*=\s*$", s) or re.match(r"^LaunchOnQuestStart\s*=", s) or re.match(r"^Repeatable\s*=", s):
            continue
        if re.match(r"^SID\s*=\s*%s\s*$" % re.escape(name), s):
            out.append(ln.replace(name, name + "_S2C", 1))
            continue
        m = re.match(r"^(\s*)QuestSID(\s*=\s*)\S+\s*$", ln)
        if m and not qs:
            qs = True
            out.append(m.group(1) + "QuestSID" + m.group(2) + "GDEQ_Global")
            out.append(m.group(1) + "Repeatable = true")
            continue
        out.append(ln)
    return out


def host_node(name, lines, kind):
    node = ["%s_S2Q : struct.begin" % name, "%sSID = %s_S2Q" % (IND, name)]
    v = child(lines, "NodePrototypeVersion")
    if v is not None:
        node.append("%sNodePrototypeVersion = %s" % (IND, v))
    node += ["%sQuestSID = %s" % (IND, child(lines, "QuestSID")), "%sNodeType = EQuestNodeType::ConsoleCommand" % IND]
    launch = sub_block(lines, "Launchers")
    once = not launch and (child(lines, "LaunchOnQuestStart") or "").lower() == "true"
    for k in COPY_KEYS:
        v = child(lines, k)
        if k == "Repeatable" and once:
            continue
        if v is not None:
            node.append("%s%s = %s" % (IND, k, v))
    if launch:
        base = len(launch[0]) - len(launch[0].lstrip(" \t"))
        node += [IND + ln[base:] if len(ln) >= base else IND + ln.lstrip() for ln in launch]
    node += ["%sConsoleCommand = s2q %s %s" % (IND, kind, name), "struct.end"]
    return "\n".join(node)


def relay_node(name):
    s = name + "_S2CR"
    return "\n".join([
        "%s : struct.begin" % s,
        "%sSID = %s" % (IND, s),
        "%sNodePrototypeVersion = 1" % IND,
        "%sQuestSID = GDEQ_Global" % IND,
        "%sNodeType = EQuestNodeType::ConsoleCommand" % IND,
        "%sRepeatable = true" % IND,
        "%sLaunchers : struct.begin" % IND,
        "%s   [0] : struct.begin" % IND,
        "%s      Excluding = false" % IND,
        "%s      Connections : struct.begin" % IND,
        "%s         [0] : struct.begin" % IND,
        "%s            SID = %s_S2C" % (IND, name),
        "%s            Name =" % IND,
        "%s         struct.end" % IND,
        "%s      struct.end" % IND,
        "%s   struct.end" % IND,
        "%sstruct.end" % IND,
        "%sConsoleCommand = s2q E %s" % (IND, name),
        "struct.end",
    ])


def end_node(name, lines):
    s = name + "_S2QE"
    return "\n".join([
        "%s : struct.begin" % s,
        "%sSID = %s" % (IND, s),
        "%sNodePrototypeVersion = 1" % IND,
        "%sQuestSID = %s" % (IND, child(lines, "QuestSID")),
        "%sNodeType = EQuestNodeType::ConsoleCommand" % IND,
        "%sRepeatable = true" % IND,
        "%sLaunchers : struct.begin" % IND,
        "%s   [0] : struct.begin" % IND,
        "%s      Excluding = false" % IND,
        "%s      Connections : struct.begin" % IND,
        "%s         [0] : struct.begin" % IND,
        "%s            SID = %s" % (IND, name),
        "%s            Name =" % IND,
        "%s         struct.end" % IND,
        "%s      struct.end" % IND,
        "%s   struct.end" % IND,
        "%sstruct.end" % IND,
        "%sConsoleCommand = s2q Z %s" % (IND, name),
        "struct.end",
    ])


def rearm_needed(lines):
    return (not sub_block(lines, "Launchers") and (child(lines, "LaunchOnQuestStart") or "").lower() == "true"
            and (child(lines, "Repeatable") or "").lower() == "true")


def rearm_node(name, lines):
    s = name + "_S2QH"
    return "\n".join([
        "%s : struct.begin" % s,
        "%sSID = %s" % (IND, s),
        "%sNodePrototypeVersion = 1" % IND,
        "%sQuestSID = %s" % (IND, child(lines, "QuestSID")),
        "%sNodeType = EQuestNodeType::ConsoleCommand" % IND,
        "%sRepeatable = true" % IND,
        "%sLaunchers : struct.begin" % IND,
        "%s   [0] : struct.begin" % IND,
        "%s      Excluding = false" % IND,
        "%s      Connections : struct.begin" % IND,
        "%s         [0] : struct.begin" % IND,
        "%s            SID = %s_S2QE" % (IND, name),
        "%s            Name =" % IND,
        "%s         struct.end" % IND,
        "%s      struct.end" % IND,
        "%s   struct.end" % IND,
        "%sstruct.end" % IND,
        "%sConsoleCommand = s2q H %s r" % (IND, name),
        "struct.end",
    ])


def downstream(texts):
    qof, down = {}, defaultdict(set)
    for txt in texts.values():
        for name, _, lines in blocks(txt):
            qof[name] = child(lines, "QuestSID")
            lb = sub_block(lines, "Launchers")
            for ln in lb or ():
                m = re.match(r"^\s*SID\s*=\s*(\S+)\s*$", ln)
                if m:
                    down[m.group(1)].add(name)
    return qof, down


def sweep_check(texts, muted):
    qof, down = downstream(texts)
    swept = []
    with open(MUTE, encoding="utf-8") as f:
        for ln in f:
            c = ln.rstrip("\n").split("\t")
            if len(c) >= 2 and c[0] == "D":
                swept.append(c[1])
    edges, bad = 0, []
    for d in swept:
        for x in sorted(down.get(d, ())):
            edges += 1
            if qof.get(x) not in muted:
                bad.append((d, x, qof.get(x)))
    return len(swept), edges, bad


def tsv_rows(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            c = ln.rstrip("\r\n").split("\t")
            if len(c) >= 2 and c[0].startswith("s"):
                rows.append([x[1:] if x.startswith("s") else x for x in c])
    return rows


def classify(t, lines, qitems):
    tgt = (child(lines, "TargetQuestGuid") or "").upper()
    if t == "ItemRemove":
        if tgt == PLAYER and (child(lines, "ItemSID") or "") in qitems:
            return None, "item de missao: qimirror"
        return "V", None
    if t in ("DisableNPCInteraction", "AchievementUnlock", "ReputationLocker"):
        return "R", None
    if t == "ChangeRelationships":
        return "V", None
    if t == "PlaySound":
        return "V", None
    if t in ("SetItemGenerator", "EquipItemInHands", "SetCharacterParam"):
        return ("V", None) if tgt == PLAYER else (None, "alvo NPC: o host manda")
    if t == "MoveInventory":
        return ("V", None) if (child(lines, "MoveFrom") or "").upper() == PLAYER else (None, "origem nao e o jogador")
    return None, "tipo fora do conjunto"


TYPES = ("ItemRemove", "DisableNPCInteraction", "AchievementUnlock", "ReputationLocker", "ChangeRelationships", "PlaySound", "SetItemGenerator",
         "EquipItemInHands", "SetCharacterParam", "MoveInventory")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=PAKSRC)
    a = ap.parse_args()
    muted = set()
    with open(MUTE, encoding="utf-8") as f:
        for ln in f:
            c = ln.rstrip("\n").split("\t")
            if len(c) >= 2 and c[0] == "Q":
                muted.add(c[1])
    if not muted:
        sys.exit("no Q rows in " + MUTE)
    events = {r[1] for r in tsv_rows(QEVENT) if len(r) >= 2 and r[0] == "ev"}
    qitems = {r[1] for r in tsv_rows(QITEMS) if len(r) >= 2 and r[0] == "S"}
    taken = set()
    if os.path.isdir(PAKSRC):
        for d in os.listdir(PAKSRC):
            if d in (DIR_C, DIR_H):
                continue
            for p in glob.glob(os.path.join(PAKSRC, d, "**", "*.cfg"), recursive=True):
                with open(p, encoding="utf-8-sig", errors="replace") as f:
                    for name, _, _ in blocks(f.read()):
                        taken.add(name)
    files = sorted(glob.glob(os.path.join(GD, "**", "*.cfg"), recursive=True))
    if not files:
        sys.exit("no GameData quest nodes under " + GD)
    stock = set()
    texts = {}
    for p in files:
        with open(p, encoding="utf-8-sig", errors="replace") as f:
            texts[p] = f.read()
        for name, _, _ in blocks(texts[p]):
            stock.add(name)
    nsw, nedge, bad = sweep_check(texts, muted)
    print("swept SetDialog nodes %d, downstream edges %d, outside muted quests %d" % (nsw, nedge, len(bad)))
    if bad:
        for d, x, q in bad[:20]:
            print("  UNSAFE sweep: %s -> %s (quest %s not muted)" % (d, x, q))
        sys.exit("a swept SetDialog node launches a node of a quest the client runs: remove it from the D rows of qmute_client.txt")
    per = defaultdict(lambda: ([], []))
    cnt, skip = Counter(), Counter()
    rows = []
    for p in files:
        fn = os.path.basename(p)
        for name, head, lines in blocks(texts[p]):
            q = child(lines, "QuestSID")
            if q not in muted:
                continue
            t = (child(lines, "NodeType") or "").split("::")[-1]
            if t == "OnSignalReceived":
                skip["OnSignalReceived: emissor em missao muda, nunca dispara no cliente"] += 1
                continue
            if name in events:
                kind, why = "H", None
            elif t in TYPES:
                kind, why = classify(t, lines, qitems)
            else:
                continue
            if kind is None:
                skip["%s: %s" % (t, why)] += 1
                continue
            if "{" in head:
                skip["patch header"] += 1
                continue
            if not TOKEN.match(name):
                skip["SID nao e token de console"] += 1
                continue
            clash = [s for s in (name + "_S2Q", name + "_S2C", name + "_S2CR", name + "_S2QE", name + "_S2QH") if s in stock or s in taken]
            if clash:
                skip["SID ja usado"] += 1
                continue
            if sub_block(lines, "Launchers") is None and child(lines, "LaunchOnQuestStart") is None:
                skip["sem launcher"] += 1
                continue
            h, c = per[fn]
            h.append(host_node(name, lines, kind))
            c.append("\n".join(client_copy(name, lines)))
            if kind == "H":
                h.append(end_node(name, lines))
                if rearm_needed(lines):
                    h.append(rearm_node(name, lines))
                    cnt["rearm H"] += 1
                c.append(relay_node(name))
            cnt["%s %s" % (kind, t)] += 1
            rows.append((kind, name, t, q))
    for d, idx, tag, what in ((DIR_H, 0, "S2RQ", "observer, HOST ONLY: ConsoleCommand s2q R/V/H <sid> with the stock node's launchers"),
                              (DIR_C, 1, "S2RC", "replay, CLIENT ONLY: copies <sid>_S2C (GDEQ_Global, Repeatable, no launchers); event copies relay s2q E <sid> by <sid>_S2CR")):
        out = os.path.join(a.out, d)
        shutil.rmtree(out, ignore_errors=True)
        qn = os.path.join(out, "QuestNodePrototypes")
        os.makedirs(qn)
        for fn, pair in sorted(per.items()):
            if not pair[idx]:
                continue
            with open(os.path.join(qn, "zzzzzz_%s_%s" % (tag, fn)), "w", encoding="utf-8", newline="\r\n") as f:
                f.write("// NOLA effect %s (gen_qcopy.py from %s). No stock node is changed.\n" % (what, fn) + "\n".join(pair[idx]) + "\n")
    print("muted quests %d, events %d, quest items %d, out %s" % (len(muted), len(events), len(qitems), a.out))
    for k in sorted(cnt):
        print("  made %-34s %d" % (k, cnt[k]))
    for k in sorted(skip):
        print("  skip %-50s %d" % (k, skip[k]))
    by = Counter(r[0] for r in rows)
    print("total R %d V %d H %d, files %d" % (by["R"], by["V"], by["H"], len(per)))


if __name__ == "__main__":
    main()
