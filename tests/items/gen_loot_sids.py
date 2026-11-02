import hashlib
import re
import sys
from pathlib import Path

here = Path(__file__).resolve().parent
src = Path(sys.argv[1]) if len(sys.argv) > 1 else here.parent / "x0" / "lua" / "s2core.lua"
out = Path(sys.argv[2]) if len(sys.argv) > 2 else here / "loot_sids.tsv"

raw = src.read_bytes()
md5 = hashlib.md5(raw).hexdigest()[:8]
text = raw.decode("utf-8")
hits = [(i + 1, l) for i, l in enumerate(text.split("\n")) if l.startswith("local LOOT_SIDS = {")]
if len(hits) != 1:
    sys.exit(f"LOOT_SIDS: {len(hits)} definitions found in {src}")
lineno, line = hits[0]
m = re.fullmatch(r'local LOOT_SIDS = \{((?:"[^"\\]*",?)*)\}\s*', line)
if not m:
    sys.exit(f"LOOT_SIDS line {lineno}: not a plain string array")
sids = re.findall(r'"([^"\\]*)"', m.group(1))
if len(set(sids)) != len(sids):
    sys.exit("LOOT_SIDS: duplicate sids")
for s in sids:
    if "\t" in s or "\n" in s or "\r" in s:
        sys.exit(f"LOOT_SIDS: bad sid {s!r}")
rows = ["k1\tt\tv"]
rows += [f"i{i}\ts\t{s}" for i, s in enumerate(sids, 1)]
out.write_bytes(("\n".join(rows) + "\n").encode("utf-8"))
print(f"loot_sids.tsv: {len(sids)} sids from {src.name} md5 {md5} line {lineno} -> {out}")
