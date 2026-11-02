import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GD = os.environ.get("S2_ZONEKIT", r"D:\modding\zk_keep") + r"\Stalker2\Content\GameLite\GameData\QuestNodePrototypes"
OUT = os.path.join(ROOT, "data", "s2u_qobj.tsv")

OPEN = re.compile(r"^\s*(\S+)\s*:\s*struct\.begin")
CLOSE = re.compile(r"^\s*struct\.end")
KV = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*?)\s*$")


def parse(path, rows):
    depth = 0
    node = None
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        for ln in f:
            m = OPEN.match(ln)
            if m:
                depth += 1
                if depth == 1:
                    node = {"sid": m.group(1), "q": "", "g": ""}
                continue
            if CLOSE.match(ln):
                if depth == 1 and node and node["g"].strip("0") and node["q"]:
                    rows.append((node["g"].upper(), node["q"], node["sid"]))
                depth -= 1
                if depth == 0:
                    node = None
                continue
            m = KV.match(ln)
            if m and node and depth == 1:
                k, v = m.group(1), m.group(2)
                if k == "QuestSID":
                    node["q"] = v
                elif k == "InteractableQuestGuid":
                    node["g"] = v


def main():
    gd = sys.argv[1] if len(sys.argv) > 1 else GD
    rows = []
    for f in sorted(os.listdir(gd)):
        if f.endswith(".cfg"):
            parse(os.path.join(gd, f), rows)
    rows.sort()
    with open(OUT, "w", encoding="utf-8", newline="\n") as o:
        o.write("guid\tquest\tnode\n")
        for g, q, n in rows:
            o.write("%s\t%s\t%s\n" % (g, q, n))
    print("%d objective rows (%d guids, %d quests) -> %s" % (len(rows), len({r[0] for r in rows}), len({r[1] for r in rows}), OUT))


if __name__ == "__main__":
    main()
