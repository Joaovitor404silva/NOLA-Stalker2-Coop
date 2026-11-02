import os
import subprocess
import sys

from lupa import LuaRuntime

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MOD = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork"
BRIDGE = MOD + r"\release\S2Coop\bridge"
EXE = os.path.join(HERE, "build", "test_world.exe")
DATA = os.path.join(ROOT, "data")

DUMP = r"""
local function load(p) return dofile(p) end
local out = {}
local function s(v) if v == nil then return "" end return tostring(v) end
local function n(v) if v == nil then return 0 end return math.tointeger(v) or v end
local D = load(BRIDGE .. "/s2u_destruct.lua")
for m, d in pairs(D) do
  out[#out + 1] = string.format("destruct\t%s\t%s\t%s\t%d\t%s\t%s", m, s(d.k), s(d.nm), n(d.p), s(d.fx), s(d.au))
end
local A = load(BRIDGE .. "/s2u_actions.lua")
for c, a in pairs(A) do
  local click = a.click or {}
  out[#out + 1] = string.format("action\t%s\t%s\t%s\t%s\t%s\t%d", c, s(a.rule), s(a.cc), s(a.on), s(a.off), #click)
  for i, st in ipairs(click) do
    out[#out + 1] = string.format("click\t%s\t%d\t%s\t%s\t%s\t%s", c, i, s(st[1]), s(st[2]), s(st[3]), s(st[4]))
  end
end
local N = load(BRIDGE .. "/s2u_anomaly.lua")
for c, a in pairs(N) do
  out[#out + 1] = string.format("anomaly\t%s\t%d\t%d\t%s\t%d", c, n(a.cd), n(a.h), s(a.n), n(a.r))
end
table.sort(out)
return out
"""


def main():
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().BRIDGE = BRIDGE.replace("\\", "/")
    rows = lua.execute(DUMP)
    want = [rows[i] for i in range(1, len(rows) + 1)]
    r = subprocess.run([EXE, "dump", DATA], capture_output=True, text=True, encoding="utf-8")
    got = sorted(x for x in r.stdout.split("\n") if x)
    want = sorted(want)
    counts = {}
    for x in got:
        counts[x.split("\t", 1)[0]] = counts.get(x.split("\t", 1)[0], 0) + 1
    print("cpp rows: " + " ".join("%s=%d" % kv for kv in sorted(counts.items())))
    miss = [x for x in want if x not in set(got)]
    extra = [x for x in got if x not in set(want)]
    for x in miss[:5]:
        print("only lua: " + x[:200])
    for x in extra[:5]:
        print("only cpp: " + x[:200])
    print("TABLES %s lua=%d cpp=%d" % ("OK" if not miss and not extra and len(want) == len(got) else "FAIL", len(want), len(got)))
    sys.exit(0 if not miss and not extra else 1)


main()
