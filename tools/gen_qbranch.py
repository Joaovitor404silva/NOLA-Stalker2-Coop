import os
import re
import sys
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GD = r"D:\modding\zk_keep\Stalker2\Content\GameLite\GameData"
OUT = os.path.join(ROOT, "data", "s2u_qbranch.tsv")
COV = os.path.join(ROOT, "data", "s2u_qbranch_cov.tsv")
QITEMS = os.path.join(ROOT, "data", "s2u_qitems.tsv")

KIND = {"SetDialog": "D", "If": "I", "Random": "R", "SetAIBehavior": "B", "Container": "C", "ScheduledContainer": "C", "BridgeEvent": "E",
        "ResetAI": "O", "ActivateInteractableObject": "O", "ShowTutorialWidget": "W", "OnInfotopicFinishEvent": "T"}
TEST_Q = re.compile(r"^(AI2Quest|AI_|ALifeGym|UI_|.*Test|.*Benchmark|A-life)", re.I)
TEST_C = re.compile(r"^(AI2Quest|AI_|ALifeGym|UI_|QA_|.*Test|.*Benchmark|A-life)", re.I)
TOKEN = re.compile(r"^[A-Za-z0-9_\-\.]+$")
OPEN = re.compile(r"^\s*(\S+)\s*:\s*struct\.begin")
CLOSE = re.compile(r"^\s*struct\.end")
KV = re.compile(r"^\s*([^\s=:]+)\s*=\s*(.*?)\s*$")

STATE_ACTS = {"SetGlobalVariable", "Attack", "Hit"}
ITEM_ACTS = {"GiveItem", "GetItem"}
FREE_ACTS = {"None", "TerminateDialog", "TimedAnswer", "FastTravel", "OpenTradeMenu", "OpenUpgradeMenu", "Heal", "Bribe", "GetMoney", "ShowMoney", "GiveMedkit",
             "Bye", "ShowItem", "RankCheck", "FactionRelationshipCheck", "Guide", "GuideUI", "GuideSelection", "Threat", "Eat", "Drink", "SideQuest", "Reward",
             "Laugh"}
TRADE_ACTS = {"OpenTradeMenu", "OpenUpgradeMenu", "FastTravel", "Heal", "Bribe", "GetMoney", "ShowMoney", "GiveMedkit", "Reward", "Eat", "Drink"}


def tree(path):
    out = []
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        stack = []
        for ln in f:
            m = OPEN.match(ln)
            if m:
                node = [m.group(1), []]
                if stack:
                    stack[-1][1].append(node)
                else:
                    out.append(node)
                stack.append(node)
                continue
            if CLOSE.match(ln):
                if stack:
                    stack.pop()
                continue
            m = KV.match(ln)
            if m and stack:
                stack[-1][1].append([m.group(1), m.group(2)])
    return out


def get(t, k, d=None):
    for e in t[1]:
        if e[0] == k:
            return e[1] if isinstance(e[1], str) else e
    return d


def subs(t, k):
    s = get(t, k)
    return [e for e in s[1] if not isinstance(e[1], str)] if s is not None and not isinstance(s, str) else []


def vals(t, k):
    s = get(t, k)
    return [e[1] for e in s[1] if isinstance(e[1], str)] if s is not None and not isinstance(s, str) else []


def truthy(v):
    return (v or "").strip().lower() == "true"


def walk(t):
    yield t
    for e in t[1]:
        if not isinstance(e[1], str):
            yield from walk(e)


def bridge_reads(t, out):
    for s in walk(t):
        if (get(s, "ConditionType") or "").endswith("::Bridge"):
            node = get(s, "LinkedNodePrototypeSID") or ""
            for p in vals(s, "CompletedNodeLauncherNames"):
                if node and p:
                    out[node].add(p)


def quests(gd, reads):
    nodes = {}
    qd = os.path.join(gd, "QuestNodePrototypes")
    for fn in sorted(os.listdir(qd)):
        if not fn.endswith(".cfg"):
            continue
        for t in tree(os.path.join(qd, fn)):
            bridge_reads(t, reads)
            sid = get(t, "SID")
            ty = (get(t, "NodeType") or "").split("::")[-1]
            if not sid or not ty:
                continue
            n = {"t": ty, "q": get(t, "QuestSID") or "", "f": fn, "L": [], "last": [], "info": [], "chain": get(t, "DialogChainPrototypeSID") or "",
                 "radio": any(truthy(v) for v in vals(t, "TalkThroughRadio")) or truthy(get(t, "ContinueThroughRadio")),
                 "forced": truthy(get(t, "StartForcedDialog")), "comment": truthy(get(t, "IsComment")), "start": get(t, "DialogToStart") or ""}
            for alt in subs(t, "Launchers"):
                ex = truthy(get(alt, "Excluding"))
                for c in subs(alt, "Connections"):
                    n["L"].append((get(c, "SID") or "", ex, get(c, "Name") or ""))
            for lp in subs(t, "LastPhrases"):
                n["last"].append((truthy(get(lp, "FinishNode")), get(lp, "LastPhraseSID") or "", get(lp, "NextLaunchedPhraseSID") or ""))
            for lp in subs(t, "InfotopicLastPhrases"):
                n["info"].append(get(lp, "LastPhraseSID") or "")
            nodes[sid] = n
    return nodes


def act_tokens(t, qitems):
    toks, kinds = set(), set()
    for lst in ("DialogActions", "DialogAnswerActions"):
        for a in subs(t, lst):
            k = (get(a, "DialogAction") or "None").split("::")[-1]
            kinds.add(k)
            p = get(a, "DialogActionParam")
            if p is not None and not isinstance(p, str):
                p = get(p, "VariableValue") or ""
            p = p or ""
            if k == "SetGlobalVariable":
                toks.add("gv:%s:%s:%s" % (get(a, "GlobalVariablePrototypeSID") or "", (get(a, "ChangeValueMode") or "").split("::")[-1], get(a, "VariableValue") or ""))
            elif k in STATE_ACTS:
                toks.add("act:" + k)
            elif k in ITEM_ACTS:
                if p in qitems:
                    toks.add("item:%s:%s" % (k, p))
            elif k not in FREE_ACTS:
                toks.add("unknown:" + k)
    return toks, kinds


def dialogs(gd, reads, qitems):
    ph = {}
    dd = os.path.join(gd, "DialogPrototypes")
    for fn in sorted(os.listdir(dd)):
        if not fn.endswith(".cfg"):
            continue
        for t in tree(os.path.join(dd, fn)):
            bridge_reads(t, reads)
            sid = get(t, "SID")
            if not sid:
                continue
            opts = []
            for o in subs(t, "NextDialogOptions"):
                opts.append((o[0], get(o, "NextDialogSID") or "", get(o, "Conditions") is not None))
            mi = get(t, "DialogMemberIndex") or "0"
            toks, kinds = act_tokens(t, qitems)
            ph[sid] = {"m": int(mi) if re.fullmatch(r"-?\d+", mi) else 0, "o": opts, "c": get(t, "DialogChainPrototypeSID") or "", "f": fn, "tok": toks, "acts": kinds}
    return ph


def load_qitems(path):
    s = set()
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            for ln in f:
                c = ln.rstrip("\n").split("\t")
                if len(c) >= 2 and c[0] == "sS" and c[1].startswith("s"):
                    s.add(c[1][1:])
    return s


def pin_name(last, nxt):
    return last + "_" + nxt if nxt else last


def sig_id(sig):
    h = 5381
    for c in "\n".join(sorted(sig)).encode():
        h = (h * 33 + c) & 0xFFFFFFFF
    return "%08x" % h


def is_if(d):
    return any(o[0] in ("True", "False") for o in d["o"])


def main():
    gd = sys.argv[1] if len(sys.argv) > 1 else GD
    out = sys.argv[2] if len(sys.argv) > 2 else OUT
    cov = sys.argv[3] if len(sys.argv) > 3 else COV
    reads = defaultdict(set)
    qitems = load_qitems(QITEMS)
    N = quests(gd, reads)
    P = dialogs(gd, reads, qitems)
    succ = defaultdict(lambda: defaultdict(set))
    for s, n in N.items():
        for (l, ex, pin) in n["L"]:
            if l in N:
                succ[l][pin].add(("!" if ex else "") + s)
    cnt = defaultdict(int)
    rows = []
    for s in sorted(N):
        n = N[s]
        k = KIND.get(n["t"])
        if not k:
            continue
        if TEST_Q.match(n["q"]) or TEST_Q.match(n["f"]):
            cnt["test"] += 1
            continue
        if not TOKEN.match(s):
            cnt["bad token"] += 1
            continue
        if k == "T":
            if not any(v for v in succ[s].values()):
                cnt["T no successor"] += 1
                continue
            rows.append("N\t%s\tT\t%s\t-\n" % (s, n["q"]))
            cnt["T"] += 1
            continue
        named = {p: frozenset(v) for p, v in succ[s].items() if p}
        if k == "D":
            for fin, last, nxt in n["last"]:
                named.setdefault(pin_name(last, nxt), frozenset())
        if k == "I":
            named.setdefault("True", frozenset())
            named.setdefault("False", frozenset())
        split = len(set(named.values())) >= 2
        read = k == "D" and len(named) >= 2 and any(p in reads.get(s, ()) for p in named)
        if not split and not read:
            cnt[k + " same path"] += 1
            continue
        if not all(TOKEN.match(p) for p in named):
            cnt["bad pin"] += 1
            continue
        if read and not split:
            cnt["D pin read by bridge"] += 1
        rows.append("N\t%s\t%s\t%s\t%s\n" % (s, k, n["q"], " ".join(sorted(named))))
        cnt[k] += 1

    by_chain = defaultdict(list)
    for s, n in N.items():
        if n["t"] == "SetDialog" and n["chain"] and not (TEST_Q.match(n["q"]) or TEST_Q.match(n["f"])) and TOKEN.match(s):
            by_chain[n["chain"]].append(s)
    info_of = defaultdict(set)
    for s, n in N.items():
        if n["t"] == "OnInfotopicFinishEvent" and not (TEST_Q.match(n["q"]) or TEST_Q.match(n["f"])):
            for p in n["info"]:
                if p:
                    info_of[p].add(s)
    started = {n["start"] for n in N.values() if n["start"]}
    fin_tok = defaultdict(set)
    edge_tok = defaultdict(lambda: defaultdict(set))
    nxt_of = defaultdict(set)
    for ch, ss in by_chain.items():
        for s in ss:
            for f, last, nxt in N[s]["last"]:
                tok = "pin:%s:%s" % (s, pin_name(last, nxt))
                if nxt:
                    edge_tok[last][nxt].add(tok)
                    nxt_of[last].add(nxt)
                else:
                    fin_tok[last].add(tok)

    def player_opts(u):
        if u in P:
            d = P[u]
            if is_if(d):
                return []
            opts = {o[1] for o in d["o"] if o[1]}
        else:
            opts = set()
        if not opts:
            opts = set(nxt_of.get(u, ()))
        return sorted(o for o in opts if o in P and P[o]["m"] == -1 and not is_if(P[o]))

    def node_tok(u):
        t = set(fin_tok.get(u, ()))
        t |= {"info:" + x for x in info_of.get(u, ())}
        if u in P:
            t |= P[u]["tok"]
        return t

    def reach(a, home):
        seen, st, got = {a, home}, [a], set(node_tok(a))
        while st:
            u = st.pop()
            vs = set(nxt_of.get(u, ()))
            if u in P:
                vs |= {o[1] for o in P[u]["o"] if o[1]}
            for v in vs:
                got |= edge_tok.get(u, {}).get(v, set())
                if v in seen:
                    continue
                seen.add(v)
                got |= node_tok(v)
                st.append(v)
        return frozenset(got)

    def family(ch, phrases):
        ss = by_chain.get(ch, [])
        fam = []
        if ss:
            if any(N[s]["radio"] for s in ss):
                fam.append("radio")
            if any(N[s]["forced"] for s in ss):
                fam.append("forced")
            if any(N[s]["comment"] for s in ss):
                fam.append("comment")
            if not any(N[s]["last"] for s in ss):
                fam.append("nopins")
            fam.append("quest")
        if any(p in info_of for p in phrases):
            fam.append("infotopic")
        if ch in started:
            fam.append("dlgstart")
        if not ss:
            acts = set()
            for p in phrases:
                acts |= P[p]["acts"]
            fam.append("trade" if acts & TRADE_ACTS else "hub")
        return fam

    chains = defaultdict(list)
    for p, d in P.items():
        chains[d["c"] or ("file:" + d["f"])].append(p)
    ans, covrows = [], []
    for ch in sorted(chains):
        phrases = chains[ch]
        f0 = P[phrases[0]]["f"]
        if TEST_C.match(ch) or TEST_C.match(f0):
            cnt["choice test"] += 1
            continue
        fam = ",".join(family(ch, phrases))
        owner = sorted(by_chain.get(ch, []))
        home = owner[0] if owner else ch
        cands = set(phrases) | {u for s in by_chain.get(ch, []) for f, u, nx in N[s]["last"] if nx}
        for u in sorted(cands):
            opts = player_opts(u)
            if len(opts) < 2:
                continue
            sig = {a: reach(a, u) for a in opts}
            gated = [a for a in opts if sig[a] and any(sig[b] != sig[a] for b in opts)]
            verdict = "gate" if gated else ("same" if any(sig.values()) else "free")
            cnt["choice " + verdict] += 1
            for a in opts:
                g = a in gated
                why = " ".join(sorted(x.split(":")[0] for x in sig[a])) or "-"
                covrows.append("%s\t%s\t%s\t%s\t%s\t%s\t%s\n" % (fam, ch, u, a, "gate" if g else "free", " ".join(sorted(set(why.split()))), sig_id(sig[a])))
                if g:
                    ans.append("A\t%s\t%s\t%s\n" % (a, home, u))
    seen = set()
    uniq = []
    for a in ans:
        key = a.split("\t")[1]
        if key in seen:
            continue
        seen.add(key)
        uniq.append(a)
    cnt["A"] = len(uniq)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(rows)
        f.writelines(uniq)
    with open(cov, "w", encoding="utf-8", newline="\n") as f:
        f.write("family\tchain\tchoice\tanswer\tverdict\teffects\tsig\n")
        f.writelines(covrows)
    print("%d quest nodes, %d phrases -> %d branch nodes, %d gated answers -> %s" % (len(N), len(P), len(rows), len(uniq), out))
    print(" ".join("%s=%d" % kv for kv in sorted(cnt.items())))


if __name__ == "__main__":
    main()
