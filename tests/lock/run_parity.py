import difflib
import os
import re
import subprocess
import sys

from lupa import LuaRuntime

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.abspath(os.path.join(HERE, "..", "..", "src"))
DATA = os.path.abspath(os.path.join(HERE, "..", "..", "data"))
MOD = r"C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork"
CORE = MOD + r"\s2coop\Mods\S2CoopAlife\Scripts\s2core.lua"
BRIDGE = MOD + "\\release\\S2Coop\\bridge\\"
EXE = os.path.join(HERE, "out", "test_lock.exe")
OUT = os.path.join(HERE, "out")
if not os.path.exists(BRIDGE):
    print("SKIP: missing external dependency " + BRIDGE)
    sys.exit(77)


def read(p):
    with open(p, "r", encoding="utf-8", errors="replace", newline="") as f:
        return f.read().replace("\r\n", "\n")


def cut(lines, start, stop, inclusive_end_line=None):
    i = next(k for k, l in enumerate(lines) if l.startswith(start))
    if inclusive_end_line is not None:
        j = next(k for k in range(i, len(lines)) if lines[k] == inclusive_end_line)
        return "\n".join(lines[i : j + 1])
    j = next(k for k in range(i + 1, len(lines)) if lines[k].startswith(stop))
    return "\n".join(lines[i:j])


def build_chunks():
    lines = read(CORE).split("\n")
    parts = [
        cut(lines, "local function valid(o)", None, "end"),
        cut(lines, "local function doorKey(o)", None, "end"),
        cut(lines, "local function ienComps(o)", None, "end"),
        cut(lines, "function S2L.ioPawn()", None, "end"),
        cut(lines, "function S2L.chTab()", "-- ===== end b91choice"),
        cut(lines, "function S2L.ldSt(ST)", "-- ===== end b92lead"),
        cut(lines, "function S2L.vendRows()", "-- ===== b108tpfloor"),
    ]
    return "\n".join(parts)


LEGACY = {"S2CORE": "NOLA", "S2Coop_": "Nola_"}
LEGACY_RE = re.compile("|".join(re.escape(k) for k in LEGACY))


def run_lua(chunks, scenario):
    lua = LuaRuntime(unpack_returned_tuples=True)
    lua.globals().BRIDGE = BRIDGE
    lua.globals().SCENARIO = scenario
    code = read(os.path.join(HERE, "twin_pre.lua")) + "\n" + chunks + "\n" + read(os.path.join(HERE, "twin_drv.lua"))
    return LEGACY_RE.sub(lambda m: LEGACY[m.group(0)], lua.execute(code))


def run_cpp(path):
    r = subprocess.run([EXE, DATA, path], capture_output=True, text=True, encoding="utf-8", timeout=120)
    if r.returncode != 0:
        return "EXIT %d\n%s%s" % (r.returncode, r.stdout, r.stderr)
    return r.stdout.replace("\r\n", "\n")


def tables():
    lua = LuaRuntime(unpack_returned_tuples=True)
    ch = lua.execute(read(BRIDGE + "s2u_choice.lua"))
    vd = lua.execute(read(BRIDGE + "s2u_vendor.lua"))
    npc = None
    for g, v in sorted(ch.npc.items(), key=lambda kv: kv[0]):
        if (v[5] or 0) == 0 and v[4] <= 2 and v[1] and v[2]:
            npc = (g, float(v[1]), float(v[2]), float(v[3]), int(v[4]))
            break
    use = next(g for g, v in sorted(ch.use.items(), key=lambda kv: kv[0]) if v == 1)
    trig = next((s, int(t)) for s, t in sorted(ch.trig.items(), key=lambda kv: kv[0]) if int(t) == 1)
    trig2 = next((s, int(t)) for s, t in sorted(ch.trig.items(), key=lambda kv: kv[0]) if int(t) == 2)
    vend = vd.rows[1]
    tech = next((r for r in vd.rows.values() if r[4] == "technician"), vend)
    return npc, use, trig, trig2, (float(vend[1]), float(vend[2]), float(vend[3]), vend[4], vend[5]), (
        float(tech[1]), float(tech[2]), float(tech[3]), tech[4], tech[5])


def scenarios():
    npc, use, trig, trig2, vend, tech = tables()
    g, nx, ny, nz, _ = npc
    px, py = nx + 150, ny + 100
    S = {}
    S["lead_basic"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
run 300
view on
interact {nx} {ny} {nz}
run 500
state
leaddlg
peer c,3,1700000005,dlg,{nx},{ny},{nz}
run 300
state
leaddlg
peer -
run 300
view off
run 500
state
leaddlg
run 1200
state
leaddlg
"""
    S["follower_held"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
peer h,4,1700000002,dlg,{nx},{ny},{nz}
view on
interact {nx} {ny} {nz}
run 600
state
leaddlg
run 4500
peer -
run 300
state
leaddlg
run 800
view off
run 2500
state
leaddlg
"""
    S["follower_far_peer"] = f"""
role client
flag lead 1
flag ldclose 2
pawn 1000 {px} {py} 0
peer h,4,1700000002,dlg,0,0,0
view on
interact {nx} {ny} {nz}
run 3000
view off
run 500
peer -
run 500
view on
run 300
state
leaddlg
view off
run 2300
state
leaddlg
"""
    S["no_npc_read"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
view on
interact none
run 1500
run 1000
state
leaddlg
view off
run 300
state
leaddlg
"""
    S["tie_client_drops"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 400
state
leaddlg
peer h,2,1700000001,dlg,{nx},{ny},{nz}
run 300
state
leaddlg
run 1000
peer -
run 300
state
leaddlg
"""
    S["tie_host_wins"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 400
peer c,7,1700000001,dlg,{nx},{ny},{nz}
run 300
run 300
peer c,8,1700000001,trig,{nx},{ny},{nz}
run 300
state
leaddlg
"""
    S["vendor_free"] = f"""
role host
flag lead 1
flag ldblk 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 500
state
leaddlg
view off
run 400
state
leaddlg
"""
    S["vendor_lock_when_vendtrade0"] = f"""
role host
flag lead 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 500
state
leaddlg
"""
    S["trig_timed"] = f"""
role host
flag lead 1
flag ldtrig 2
pawn 1000 {px} {py} 0
trigenter {trig[0]} {nx} {ny} {nz}
run 300
state
leaddlg
trigenter {trig[0]} {nx} {ny} {nz}
run 1500
state
leaddlg
run 400
state
leaddlg
trigenter {trig2[0]} {nx} {ny} {nz}
state
leaddlg
trigenter NOT_A_TRIGGER 1 2 3
state
leaddlg
"""
    S["trig_host_while_client_leads"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
peer c,2,1700000001,dlg,{nx},{ny},{nz}
trigenter {trig[0]} {nx} {ny} {nz}
run 100
role client
trigenter {trig[0]} {nx} {ny} {nz}
run 100
state
leaddlg
"""
    S["denies_lead1_host_mine"] = f"""
role host
flag lead 1
flag choice 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
ph {g} n1
usable u1 {use} {nx} {ny} 0 A:1,B:1
trigenter {trig[0]} {nx} {ny} {nz}
shot n1
shot n1
trig {trig[0]} 7
trig {trig2[0]} 8
trig NOT_A_TRIGGER 9
usedeny u1 11,22,33
state
leaddlg
run 4000
state
leaddlg
shot n1
state
leaddlg
run 300
state
leaddlg
run 3200
state
leaddlg
"""
    S["denies_lead1_no_lock"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
npc n2 5002 {nx + 90000} {ny} {nz} 1 1 1 0 0
ph {g} n1
shot n1
shot n2
trig {trig[0]} 7
usedeny u1 11,22,33
state
leaddlg
"""
    S["denies_choice_only"] = f"""
role host
flag choice 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
ph {g} n1
usable u1 {use} {nx} {ny} 0 A:1,B:1
usable u2 FFFFFFFF000000010000000200000003 {nx} {ny} 0 A:1
shot n1
trig {trig[0]} 7
trig {trig2[0]} 8
usedeny u1 11,22,33
usedeny u2 44,55,66
run 100
state
leaddlg
"""
    S["denies_client_role"] = f"""
role client
flag choice 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
ph {g} n1
usable u1 {use} {nx} {ny} 0 A:1,B:1
shot n1
usedeny u1 11,22,33
trig {trig[0]} 7
run 2100
run 2100
"""
    S["denies_choice_off"] = f"""
role host
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
ph {g} n1
shot n1
trig {trig[0]} 7
usedeny u1 11,22,33
"""
    S["choice_client_block"] = f"""
role client
flag choice 1
pawn 1000 {px} {py} 0
usable u1 {use} {nx} {ny} 0 A:1,B:0,C:1
usable u2 {use} {nx + 90000} {ny} 0 A:1
run 2100
run 2100
run 2100
"""
    S["ldblk_block_release"] = f"""
role client
flag lead 1
flag ldblk 1
pawn 1000 {vend[0]} {vend[1] - 600} {vend[2]}
npc n1 5001 {vend[0] + 2000} {vend[1]} {vend[2]} 1 1 1 0 0
npc n2 5002 {vend[0] + 2100} {vend[1]} {vend[2]} 0 1 1 0 0
npc n3 5003 {vend[0]} {vend[1]} {vend[2]} 1 1 1 0 0
npc n4 5004 {vend[0] + 90000} {vend[1]} {vend[2]} 1 1 1 0 0
npc n5 5005 {vend[0] + 2200} {vend[1]} {vend[2]} 1 1 1 1 0
npc n6 5006 {vend[0] + 2300} {vend[1]} {vend[2]} 1 0 1 0 0
npc n7 5007 {vend[0] + 2400} {vend[1]} {vend[2]} 1 1 0 0 0
npc n8 5008 {vend[0] + 2500} {vend[1]} {vend[2]} 1 1 1 0 0
guid n1 777
guid n6 888
guid n8 999
peer h,5,1700000002,dlg,{nx},{ny},{nz}
run 2100
state
leaddlg
npcset n1 alive 0
run 1100
run 1100
peer -
run 1100
state
leaddlg
ledgerdump
"""
    S["ldblk_flag_off_midway"] = f"""
role client
flag lead 1
flag ldblk 1
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
peer h,5,1700000002,dlg,{nx},{ny},{nz}
run 1100
flag ldblk 0
run 1100
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
flag ldblk 1
run 1100
flag lead 0
run 1100
"""
    S["ldblk_no_pawn"] = f"""
role host
flag lead 1
flag ldblk 1
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
peer c,5,1700000002,dlg,{nx},{ny},{nz}
nopawn
run 1500
pawn 1000 {px} {py} 0
run 1100
"""
    S["usegate_give_back"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
usable u1 {use} {nx} {ny} 0 A:1,B:1
usable u2 {use} {nx + 90000} {ny} 0 A:1
peer h,5,1700000002,dlg,{nx},{ny},{nz}
run 2100
run 2100
peer -
run 1100
run 1100
run 2100
"""
    S["usegate_far_left"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
usable u1 {use} {nx} {ny} 0 A:1
usable u2 {use} {nx + 9000} {ny} 0 A:1
peer c,5,1700000002,dlg,{nx},{ny},{nz}
run 2100
pawn 1000 {px + 9000} {py} 0
peer -
run 2100
pawn 1000 {px} {py} 0
run 2100
"""
    S["vendtrade_trade"] = f"""
role client
flag vendtrade 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
hint 3
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 400
view off
run 400
view on
run 400
view off
run 400
interact none
run 400
"""
    S["vendtrade_technician"] = f"""
role client
flag vendtrade 1
pawn 1000 {tech[0]} {tech[1]} {tech[2]}
view on
interact {tech[0]} {tech[1]} {tech[2]}
run 400
view off
run 400
interact none
run 400
"""
    S["vendtrade_timeout"] = f"""
role client
flag vendtrade 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 3600
run 1000
"""
    S["vendtrade_closing_timeout"] = f"""
role client
flag vendtrade 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 400
view off
run 400
view on
run 2500
"""
    S["vendtrade_dry"] = f"""
role client
flag vendtrade 2
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 600
view off
run 300
"""
    S["vendtrade_not_vendor"] = f"""
role client
flag vendtrade 1
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 600
view off
run 300
"""
    S["vendtrade_host_role"] = f"""
role host
flag vendtrade 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 600
"""
    S["vendtrade_off_midway"] = f"""
role client
flag vendtrade 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact {vend[0]} {vend[1]} {vend[2]}
run 400
flag vendtrade 0
run 400
"""
    S["vendtrade_no_read"] = f"""
role client
flag vendtrade 1
pawn 1000 {vend[0]} {vend[1]} {vend[2]}
view on
interact none
run 2500
"""
    S["ldsave_report"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
ledger N 111 1699999900
ledger N 999 1699999900
ledger U {use} 1699999900
npc n1 5001 {nx} {ny} {nz} 1 0 1 0 0
npc n2 5002 {nx + 100} {ny} {nz} 1 0 1 0 0
npc n3 5003 {nx + 200} {ny} {nz} 1 1 1 0 0
npc n4 5004 {nx + 300} {ny} {nz} 0 0 1 0 0
npc n5 5005 {nx + 400} {ny} {nz} 1 0 1 1 0
guid n1 111
guid n2 222
usable u1 {use} {nx} {ny} 0 A:0,B:1
usable u2 {use} {nx + 100} {ny} 0 A:0
usable u3 AAAAAAAA000000010000000200000003 {nx} {ny} 0 A:0
beds 4 2
run 9000
run 26000
ledgerdump
"""
    S["ldsave_fix"] = f"""
role client
flag lead 1
flag ldsave 2
pawn 1000 {px} {py} 0
ledger N 111 1699999900
ledger U {use} 1699999900
npc n1 5001 {nx} {ny} {nz} 1 0 1 0 0
npc n2 5002 {nx + 100} {ny} {nz} 1 0 1 0 0
guid n1 111
guid n2 222
usable u1 {use} {nx} {ny} 0 A:0,B:1
beds 3 1
bedprobe 1
run 9000
ledgerdump
pawn 2000 {px} {py} 0
run 9000
run 26000
ledgerdump
"""
    S["ldsave_off"] = f"""
role client
flag lead 1
flag ldsave 0
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 0 1 0 0
run 9000
ledgerdump
"""
    S["ldsave_expiry"] = f"""
role client
flag lead 1
flag ldblk 1
flag ldsavekeep 100
time 2000000
pawn 1000 {px} {py} 0
ledger N 111 1699990000
ledger N 112 1700001900
ledger N 113 1700001999
ledger U {use} 1699999000
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
guid n1 555
peer h,5,1700000002,dlg,{nx},{ny},{nz}
run 1100
ledgerdump
"""
    led = "".join("ledger N %d %d\n" % (1000 + i, 1699990000 + (i * 7) % 300) for i in range(405))
    led += "".join("ledger U %032X %d\n" % (i, 1699990000 + (i * 11) % 300) for i in range(10))
    S["ldsave_cap"] = f"""
role client
flag lead 1
flag ldblk 1
time 5000
pawn 1000 {px} {py} 0
{led}
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
guid n1 77
peer h,5,1700000002,dlg,{nx},{ny},{nz}
run 1100
ledgerdump
"""
    S["mq_functions"] = """
ismain E01_MQ01_Foo
ismain C_E02_MQ10_Bar
ismain E03_Boss1
ismain C_E03_BossFinal
ismain E04_Flashback_A
ismain Arch_Bossfight_One
ismain Arch_E01_Scene
ismain E05_Duga
ismain SQ02_X
ismain E01_SQ_Foo
ismain Arch_Other
ismain
mqflag 0 0 1
time 1000
mqseen SQ02_X
mqflag 1 0 1
mqseen E01_MQ03
time 2000
mqflag 1 0 1
mqflag 1 0 1
mqflag 0 0 0
time 100000
mqflag 1 1 0
mqflag 1 0 0
mqflag 0 0 0
time 200000
mqflag 1 0 0
mqok 0
mqok 1
mqsid SQ02_X
mqsid E01_MQ01
flag mqpull 1
mqok 0
mqok 1
mqsid SQ02_X
mqsid E01_MQ01
time 300000
mqseen E01_MQ99
mqflag 1 0 0
"""
    S["watchdog"] = f"""
role host
flag lead 1
flag ldwatchs 3
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 400
state
leaddlg
run 3000
state
leaddlg
run 400
state
leaddlg
"""
    S["watchdog_off"] = f"""
role host
flag lead 1
flag ldwatchs 3
flag ldwatch 0
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 400
run 4000
state
leaddlg
"""
    S["lead_off"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 400
state
leaddlg
flag lead 0
run 200
state
leaddlg
run 300
view off
run 300
"""
    S["held_hide_then_lead0"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
peer h,4,1700000002,dlg,{nx},{ny},{nz}
view on
interact {nx} {ny} {nz}
run 600
flag lead 0
run 200
state
leaddlg
"""
    S["peer_trig_hold"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
peer h,4,1700000002,trig,{nx},{ny},{nz}
view on
interact {nx} {ny} {nz}
run 5200
view off
run 500
peer -
run 100
"""
    S["peer_garbage"] = f"""
role client
flag lead 1
pawn 1000 {px} {py} 0
peer x,1,2
view on
interact {nx} {ny} {nz}
run 400
state
leaddlg
"""
    S["view_returns_within_hold"] = f"""
role host
flag lead 1
pawn 1000 {px} {py} 0
view on
interact {nx} {ny} {nz}
run 400
view off
run 500
view on
run 300
view off
run 1300
state
leaddlg
"""
    S["lead_float_flags"] = f"""
role host
flag lead 1
flag ldhold 0.5
flag ldkill 3.5
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
ph {g} n1
shot n1
view on
interact {nx} {ny} {nz}
run 300
view off
run 700
state
leaddlg
"""
    return S


def mc_scenarios():
    npc, _, _, _, _, _ = tables()
    g, nx, ny, nz, _ = npc
    px, py = nx + 150, ny + 100
    S = {}
    S["mc_host_arbiter"] = f"""
role host
flag lead 1
mc 5 4 0
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
run 300
vpeer 2 c,1,1700000001,dlg,{nx},{ny},{nz}
vpeer 1 c,4,1700000002,dlg,{nx},{ny},{nz}
run 300
peer c,2,1700000003,trig,1,2,3
run 300
out
ownerpid
state
view on
interact {nx} {ny} {nz}
run 500
state
leaddlg
vpeer 1 -
run 300
out
ownerpid
vpeer 2 -
run 300
out
ownerpid
peer -
run 300
out
ownerpid
state
run 600
state
leaddlg
"""
    S["mc_client_follow"] = f"""
role client
flag lead 1
mc 5 4 2
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
run 300
view on
interact {nx} {ny} {nz}
run 500
out
lo 2,1,1700000001,dlg,{nx},{ny},{nz}
run 300
state
ownerpid
lo 3,7,1700000009,dlg,{nx},{ny},{nz}
run 300
state
ownerpid
leaddlg
lo -
run 300
ownerpid
peer h,5,1700000010,dlg,{nx},{ny},{nz}
run 300
ownerpid
state
"""
    S["mc_off_one_client"] = f"""
role host
flag lead 1
flag mclock 1
mc 1 1 0
pawn 1000 {px} {py} 0
npc n1 5001 {nx} {ny} {nz} 1 1 1 0 0
run 300
peer c,3,1700000005,dlg,{nx},{ny},{nz}
run 300
out
ownerpid
state
"""
    S["mc_flag_off"] = f"""
role client
flag lead 1
flag mclock 0
mc 5 4 2
pawn 1000 {px} {py} 0
run 300
lo 3,7,1700000009,dlg,{nx},{ny},{nz}
run 300
ownerpid
state
"""
    return S


def run_mc(sel, bless):
    exp = os.path.join(HERE, "mc_expect")
    os.makedirs(exp, exist_ok=True)
    bad = total = 0
    for name, text in mc_scenarios().items():
        if sel and name not in sel:
            continue
        total += 1
        text = text.strip("\n") + "\n"
        path = os.path.join(OUT, "scn", name + ".txt")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        b = run_cpp(path)
        ep = os.path.join(exp, name + ".txt")
        if bless or not os.path.exists(ep):
            with open(ep, "w", encoding="utf-8", newline="\n") as f:
                f.write(b)
            print("BLESS %s" % name)
            continue
        a = read(ep)
        if a == b:
            print("PASS %-34s %4d lines (C++ only)" % (name, a.count("\n")))
        else:
            bad += 1
            print("FAIL %s (C++ only)" % name)
            for l in list(difflib.unified_diff(a.split("\n"), b.split("\n"), "expect", "cpp", lineterm="", n=2))[:24]:
                print("   " + l)
    print("%d/%d mc scenarios identical" % (total - bad, total))
    return bad


def main():
    os.makedirs(os.path.join(OUT, "scn"), exist_ok=True)
    sel = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--mc" in sys.argv:
        return 1 if run_mc(sel, "--bless" in sys.argv) else 0
    chunks = build_chunks()
    bad = 0
    total = 0
    for name, text in scenarios().items():
        if sel and name not in sel:
            continue
        text = text.strip("\n") + "\n"
        path = os.path.join(OUT, "scn", name + ".txt")
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        total += 1
        try:
            a = run_lua(chunks, text)
        except Exception as e:
            a = "LUA ERROR %s\n" % e
        b = run_cpp(path)
        with open(os.path.join(OUT, name + ".lua.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(a)
        with open(os.path.join(OUT, name + ".cpp.txt"), "w", encoding="utf-8", newline="\n") as f:
            f.write(b)
        if a == b:
            print("PASS %-34s %4d lines" % (name, a.count("\n")))
        else:
            bad += 1
            print("FAIL %s" % name)
            d = list(difflib.unified_diff(a.split("\n"), b.split("\n"), "lua", "cpp", lineterm="", n=2))
            for l in d[:24]:
                print("   " + l)
    print("%d/%d scenarios identical" % (total - bad, total))
    return 1 if bad else 0


sys.exit(main())
