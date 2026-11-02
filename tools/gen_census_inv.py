import argparse
import os
import sys

PORT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(PORT, "data", "invariants.tsv")
OUT = os.path.join(PORT, "src", "s2m_census_inv.inc")
DELIM = "NOLAINV"


def render(text):
    text = text.replace("\r\n", "\n")
    if (")" + DELIM + '"') in text:
        raise SystemExit("delimiter inside the table")
    return 'R"' + DELIM + "(" + text + ")" + DELIM + '"\n'


def main():
    ap = argparse.ArgumentParser(description="data/invariants.tsv -> src/s2m_census_inv.inc (dev-only census invariant table)")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()
    with open(SRC, encoding="utf-8", newline="") as f:
        want = render(f.read())
    if a.check:
        have = open(OUT, encoding="utf-8", newline="").read() if os.path.exists(OUT) else ""
        if have != want:
            print("FAIL s2m_census_inv.inc is stale: run tools/gen_census_inv.py")
            return 1
        print("ok   s2m_census_inv.inc matches data/invariants.tsv")
        return 0
    with open(OUT, "w", encoding="utf-8", newline="") as f:
        f.write(want)
    print("wrote", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
