import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")


def main():
    text = open(os.path.join(OUT, "imports.txt"), encoding="utf-8", errors="replace").read()
    block = re.search(r"^\s+UE4SS\.dll\s*$(.*?)^\s+\S+\.dll\s*$", text, re.S | re.M)
    if not block:
        print("no UE4SS.dll import block")
        return 1
    names = re.findall(r"^\s+[0-9A-F]+\s+(\S+)\s*$", block.group(1), re.M)
    names = [n for n in names if not n.startswith("Import")]
    with open(os.path.join(OUT, "ue4ss_stub.def"), "w", newline="\n") as f:
        f.write("LIBRARY UE4SS\nEXPORTS\n")
        for n in names:
            f.write("    %s=stub_fn\n" % n)
    with open(os.path.join(OUT, "ue4ss_stub.c"), "w", newline="\n") as f:
        f.write("static char z[256];\nvoid* stub_fn(void) { return z; }\n")
    print("stub exports: %d" % len(names))
    return 0


sys.exit(main())
