import re
import sys

REMOVED = {"t1000_fcTickP", "t100_msTick", "t100_nsTick", "t100_NP_tickP", "t1000_offaTick", "t1000_hlTick"}


def calls(lines, a, b):
    out = []
    for ln in range(a, b + 1):
        text = lines[ln - 1].split("--")[0]
        if "ipairs({{" in text:
            for i, m in enumerate(re.finditer(r'\{"([A-Za-z0-9_]+)",', text)):
                out.append((m.group(1), ln * 100 + i))
            continue
        i = 0
        for m in re.finditer(r'pcall\(((?:[A-Za-z0-9_]+\.)*[A-Za-z0-9_]+),\s*ST\b|(?<![A-Za-z0-9_.:])((?:[A-Za-z0-9_]+\.)*[A-Za-z0-9_]+)\(ST\b', text):
            name = m.group(1) or m.group(2)
            if name in ("pcall", "valid", "log", "f", "M.ownsAvatar", "S2L.nsErr", "S2L.natBrRecent", "tostring"):
                continue
            out.append((name, ln * 100 + i))
            i += 1
    return out


def cname(n):
    for p in ("S2L.", "M."):
        if n.startswith(p):
            n = n[len(p):]
    return n.replace(".", "_")


def body_end(lines, start):
    for i in range(start, len(lines)):
        if lines[i].startswith("end"):
            return i + 1
    raise SystemExit("no end")


def main():
    lines = open(sys.argv[1], encoding="utf-8", errors="replace").read().split("\n")
    rows = []
    for fn, pre in (("function M.tick100(ST)", "t100_"), ("function M.tick1000(ST, n)", "t1000_")):
        s = next(i for i, l in enumerate(lines) if l.startswith(fn)) + 1
        e = body_end(lines, s)
        seen = set()
        for n, o in calls(lines, s + 1, e - 1):
            c = pre + cname(n)
            if c in seen or c in REMOVED:
                continue
            seen.add(c)
            rows.append((c, o))
    with open(sys.argv[2], "w", encoding="ascii", newline="\r\n") as f:
        for c, o in rows:
            f.write("X0_ORDER(%s, %d)\n" % (c, o))
    print(len(rows))


if __name__ == "__main__":
    main()
