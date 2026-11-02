import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = r"D:\modding\offline\b73items\out\s2u_items.tsv"
OUT = os.path.join(ROOT, "data", "s2u_drop.tsv")
SKIP_KINDS = {"Money"}
RULES = {"sync", "artifact"}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    rows = []
    with open(src, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        col = {n: i for i, n in enumerate(head)}
        for ln in f:
            p = ln.rstrip("\n").split("\t")
            if len(p) < len(head) or p[col["drop_rule"]] not in RULES or p[col["in_loot_sids"]] != "1":
                continue
            if p[col["kind"]] in SKIP_KINDS or not p[col["world_mesh"]]:
                continue
            rows.append((p[col["sid"]], p[col["world_mesh"]], p[col["max_stack"]], p[col["kind"]]))
    rows.sort()
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("sid\tmesh\tmax_stack\tkind\n")
        for r in rows:
            f.write("\t".join(r) + "\n")
    print("%s: %d rows" % (OUT, len(rows)))


if __name__ == "__main__":
    main()
