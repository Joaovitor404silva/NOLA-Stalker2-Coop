import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
TSV = os.path.join(REPO, "data", "nola_texts.tsv")
OUT = os.path.join(REPO, "src", "s2m_text_tab.inc")
LANGS = ["en", "pt", "es"]
PH = re.compile(r"\{[a-z]+\}|%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z]")


def load():
    rows = []
    with open(TSV, encoding="utf-8", newline="") as f:
        data = f.read()
    if data.startswith("﻿") or "\r" in data:
        raise SystemExit("nola_texts.tsv: no BOM and LF line endings only")
    lines = data.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if lines[0].split("\t") != ["id"] + LANGS:
        raise SystemExit("nola_texts.tsv: header must be id" + "".join("\t" + l for l in LANGS))
    seen = set()
    for n, line in enumerate(lines[1:], 2):
        c = line.split("\t")
        if len(c) != 1 + len(LANGS):
            raise SystemExit("nola_texts.tsv:%d: %d columns, need %d" % (n, len(c), 1 + len(LANGS)))
        if not re.fullmatch(r"[a-z][a-z0-9_]*", c[0]):
            raise SystemExit("nola_texts.tsv:%d: bad id %r" % (n, c[0]))
        if c[0] in seen:
            raise SystemExit("nola_texts.tsv:%d: duplicate id %s" % (n, c[0]))
        seen.add(c[0])
        for l, t in zip(LANGS, c[1:]):
            if not t.strip():
                raise SystemExit("nola_texts.tsv:%d: empty %s text for %s" % (n, l, c[0]))
        base = sorted(PH.findall(c[1]))
        for l, t in zip(LANGS[1:], c[2:]):
            if sorted(PH.findall(t)) != base:
                raise SystemExit("nola_texts.tsv:%d: %s placeholders differ from en for %s: %s vs %s" % (n, l, c[0], sorted(PH.findall(t)), base))
        rows.append(c)
    return rows


def lit(s):
    o = ['"']
    for b in s.encode("utf-8"):
        if b == 0x22:
            o.append('\\"')
        elif b == 0x5C:
            o.append("\\\\")
        elif 0x20 <= b < 0x7F:
            o.append(chr(b))
        else:
            o.append("\\%03o" % b)
    o.append('"')
    return "".join(o)


def render(rows):
    return "".join("X(%s, %s)\n" % (c[0], ", ".join(lit(t) for t in c[1:])) for c in rows)


def main():
    text = render(load())
    if "--check" in sys.argv:
        cur = open(OUT, encoding="ascii", newline="").read() if os.path.exists(OUT) else ""
        if cur != text:
            print("FAIL s2m_text_tab.inc is not generated from nola_texts.tsv: run tools/gen_texts.py")
            return 1
        print("ok s2m_text_tab.inc matches nola_texts.tsv (%d ids)" % text.count("\n"))
        return 0
    with open(OUT, "w", encoding="ascii", newline="") as f:
        f.write(text)
    print("wrote %s (%d ids)" % (OUT, text.count("\n")))
    return 0


sys.exit(main())
