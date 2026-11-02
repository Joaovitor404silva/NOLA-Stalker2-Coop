import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\offline\b87qitem\qitems_report.tsv"
OUT = os.path.join(ROOT, "data", "s2u_qshare.tsv")
NEED = {"OnPlayerGetItemEvent", "Condition", "If", "ItemRemove"}


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    rows = []
    with open(src, encoding="utf-8") as f:
        head = f.readline().rstrip("\n").split("\t")
        col = {n: i for i, n in enumerate(head)}
        for ln in f:
            p = ln.rstrip("\n").split("\t")
            if len(p) < len(head) or p[col["isquest"]] != "1" or p[col["guarded"]] != "0":
                continue
            nb = set(x for x in p[col["needed_by"]].split(",") if x)
            if not nb & NEED:
                continue
            rows.append((p[col["sid"]], ",".join(sorted(nb))))
    rows.sort()
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write("sid\tneeded_by\n")
        for r in rows:
            f.write("\t".join(r) + "\n")
    print("%s: %d rows" % (OUT, len(rows)))


if __name__ == "__main__":
    main()
