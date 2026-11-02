import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GAMEDATA = r"D:\modding\zk_keep\Stalker2\Content\GameLite\GameData"
TAG = "Item.WoundedHeal"


def parse_nodes(path):
    nodes = {}
    stack = []
    cur = None
    with open(path, encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith("//"):
                continue
            m = re.match(r"^(\S+)\s*:\s*struct\.begin\s*(\{[^}]*\})?", line)
            if m:
                if not stack:
                    ref = re.search(r"refkey=([^;}\s]+)", m.group(2) or "")
                    cur = {"ref": ref.group(1) if ref else None, "kv": {}, "lists": {}}
                    nodes[m.group(1)] = cur
                    stack.append(m.group(1))
                else:
                    stack.append(m.group(1))
                    if len(stack) == 2:
                        cur["lists"][m.group(1)] = []
                continue
            if line == "struct.end":
                if stack:
                    stack.pop()
                continue
            if cur is None or not stack:
                continue
            if len(stack) == 1:
                kv = re.match(r"^(\w+)\s*=\s*(.*)$", line)
                if kv:
                    cur["kv"][kv.group(1)] = kv.group(2).strip()
            elif len(stack) == 2:
                it = re.match(r"^\[\d+\]\s*=\s*(.*)$", line)
                if it:
                    cur["lists"][stack[1]].append(it.group(1).strip())
    return nodes


def resolve(nodes, name, key, kind):
    seen = set()
    while name and name not in seen and name in nodes:
        seen.add(name)
        n = nodes[name]
        src = n["kv"] if kind == "kv" else n["lists"]
        if key in src:
            return src[key]
        name = n["ref"]
    return None


def num(s):
    s = s.strip().rstrip("fF")
    return float(s.rstrip("%"))


def build(gd):
    items = parse_nodes(os.path.join(gd, "ItemPrototypes", "ConsumablePrototypes.cfg"))
    effects = parse_nodes(os.path.join(gd, "EffectPrototypes.cfg"))
    sids = []
    for name in items:
        tags = resolve(items, name, "ItemTags", "lists")
        if tags and TAG in tags:
            sids.append(name)
    heal = []
    for sid in sids:
        fx = resolve(items, sid, "EffectPrototypeSIDs", "lists") or []
        if not fx:
            raise SystemExit("%s has no EffectPrototypeSIDs" % sid)
        total = 0.0
        for i, e in enumerate(fx, 1):
            if e not in effects:
                raise SystemExit("%s: effect %s not found in EffectPrototypes.cfg" % (sid, e))
            heal.append(("s" + sid, "i%d" % i, "s", e))
            if resolve(effects, e, "Type", "kv") == "EEffectType::Health":
                total += num(resolve(effects, e, "ValueMin", "kv"))
        heal.append(("s" + sid, "shp", "f", "%.1f" % total))
    return sids, heal


def main(argv=None):
    ap = argparse.ArgumentParser(description="revive_items.tsv + revive_heal.tsv from the GameData configs (items tagged Item.WoundedHeal)")
    ap.add_argument("--gamedata", default=GAMEDATA)
    ap.add_argument("--out", default=os.path.join(ROOT, "data"))
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args(argv)
    sids, heal = build(a.gamedata)
    items_txt = "k1\tt\tv\n" + "".join("i%d\ts\t%s\n" % (i, s) for i, s in enumerate(sids, 1))
    heal_txt = "k1\tk2\tt\tv\n" + "".join("%s\t%s\t%s\t%s\n" % r for r in heal)
    files = {"revive_items.tsv": items_txt, "revive_heal.tsv": heal_txt}
    bad = 0
    for name, txt in files.items():
        p = os.path.join(a.out, name)
        if a.check:
            cur = open(p, encoding="utf-8", newline="").read() if os.path.exists(p) else ""
            if cur != txt:
                bad += 1
                print("DIFF %s" % name)
            else:
                print("OK %s" % name)
        else:
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write(txt)
            print("wrote %s (%d rows)" % (p, txt.count("\n") - 1))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
