import argparse
import os
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
PORT = os.path.normpath(os.path.join(ROOT, ".."))
SRC = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\tools\s2fake\mc_tags.tsv"
OUT = os.path.join(PORT, "data", "mc_tags.tsv")
COLS = (("module", "s"), ("from", "s"), ("scope", "s"), ("relay", "i"), ("rule", "s"))
SCOPES = ("peer", "host", "personal")


def main(argv=None):
    ap = argparse.ArgumentParser(description="mc_tags.tsv (s2fake tag table) -> data/mc_tags.tsv in D1 form (k1 tag, k2 column)")
    ap.add_argument("--src", default=SRC)
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args(argv)
    with open(a.src, encoding="utf-8") as f:
        lines = f.read().splitlines()
    head = lines[0].split("\t")
    if head[:6] != ["tag", "module", "from", "scope", "relay", "rule"]:
        raise SystemExit("unexpected header: %r" % head)
    out = ["k1\tk2\tt\tv"]
    seen = set()
    for n, ln in enumerate(lines[1:], 2):
        if not ln.strip():
            continue
        f = ln.split("\t")
        while len(f) < 6:
            f.append("")
        tag = f[0]
        if not tag or tag in seen:
            raise SystemExit("line %d: empty or repeated tag %r" % (n, tag))
        if f[3] not in SCOPES:
            raise SystemExit("line %d: scope %r not in %s" % (n, f[3], SCOPES))
        if f[4] not in ("0", "1"):
            raise SystemExit("line %d: relay %r is not 0/1" % (n, f[4]))
        seen.add(tag)
        for (col, t), v in zip(COLS, f[1:6]):
            if "\t" in v:
                raise SystemExit("line %d: tab in %s" % (n, col))
            out.append("s%s\ts%s\t%s\t%s" % (tag, col, t, v))
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    relay = sorted(t for t in seen if any(l == "s%s\tsrelay\ti\t1" % t for l in out))
    print("%s: %d tags, relay=1: %s" % (a.out, len(seen), ",".join(relay)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
