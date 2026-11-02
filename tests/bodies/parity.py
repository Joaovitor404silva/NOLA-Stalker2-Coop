import hashlib
import math
import random
import re
import subprocess
import sys
from pathlib import Path

import lupa.lua54 as lupa

G = Path(r"C:/Program Files (x86)/Steam/steamapps/common/S.T.A.L.K.E.R. 2 Heart of Chornobyl/ModWork")
CORE = G / "s2coop/Mods/S2CoopAlife/Scripts/s2core.lua"
HERE = Path(__file__).resolve().parent
WANT_MD5 = "0b56e1b5"

FUNCS = [
    "function S2L.pfZ(", "function S2L.paAsset(", "function S2L.paClip(", "function S2L.paFam(",
    "function S2L.paSector(", "function S2L.paPick(", "function S2L.paYaw(", "function S2L.paClips(",
    "function S2L.pfxTick(", "function S2L.nbSt(", "function S2L.nsHist(", "function S2L.nsPath(",
    "function S2L.nsNoBack(", "function S2L.nsSample(", "function S2L.nsDrive(", "function S2L.rVel(",
    "function S2L.nsPup(", "function S2L.smStat(", "function S2L.pupTick(", "local function tgtTick(",
    "function M.remoteLife(", "function S2L.nsStop(", "function M.NP.rel(",
]
ONE = ["function S2L.paPath(", "function S2L.pmAddr(", "function S2L.nsS0(", "local TGT_HP = "]

STUBS = r"""
FL = {}
LOGS = {}
NOW = 0
LINE = 0
function log(s) LOGS[#LOGS + 1] = LINE .. " " .. s end
os.clock = function() return NOW end
function valid(o) return o ~= nil and (type(o) ~= "table" or not o.dead) end
function FName(s) return s end
S2L = {paMiss = {}}
M = {remoteCine = function() return false end, remoteDown = function() return false end}
M.NP = {}
M.pupRecycle = function() RECYC = (RECYC or 0) + 1 end
api = {angDiff = function(a, b) local d = (a - b) % 360; if d > 180 then d = d - 360 end; return d end}
api.setAvatar = function() end
X1 = {}
function x1part(tag) return X1[tag] end
seqs = {}
MISS = 0
function asset(full) if MISS > 0 and #full % MISS == 0 then return nil end return {full = full} end
bodyOk = function() return true end
bodyLost = function() end
pupSpawn = function() end
S2 = {errs = {n = 0}, state = {}}
S2L.cmOff = function() end
S2L.nsErr = function(ST, w, e) log("NSERR " .. w .. " " .. tostring(e)) end
S2L.pupAnim = function() end
S2L.pupWeapon = function() end
S2L.pmOff = function() end
S2L.farHide = function() end
S2L.tgBack = function() end
S2L.tgPark = function() end
S2L.tgNote = function() end
function mkPawn()
    local p = {CapsuleComponent = {}, Mesh = {IsValid = function() return true end}, hp = 5000, ax = 0, ay = 0, az = 0, cur = 0}
    function p:K2_SetActorLocation(v) self.loc = {v.X, v.Y, v.Z}; self.snap = true; return true end
    function p:K2_SetActorRotation(r) self.rot = r.Yaw; return true end
    function p:K2_GetActorRotation() return {Pitch = 0, Yaw = self.cur, Roll = 0} end
    function p:K2_GetActorLocation() return {X = self.ax, Y = self.ay, Z = self.az} end
    function p:SetActorHiddenInGame(h) end
    function p:GetHP() return self.hp end
    function p:IsAlive() return true end
    function p:ForceSetHP(v) self.hp = v end
    function p:GetBleeding() return 0 end
    function p.CapsuleComponent:GetScaledCapsuleHalfHeight() if OWN == nil then error("no capsule") end return OWN end
    return p
end
"""

DRIVER = r"""
function fresh()
    ST = {pup = {pawn = mkPawn()}, tgt = {pawn = mkPawn(), setup = true, path = "T"}, dmg = {seq = 0, total = 0, dropped = 0, list = {}}}
    PAQ = {clipBad = 0, clipN = 0}
    PAP = {}
    AI = {GetAddress = function() return 77 end}
    function AI:PlaySlotAnimationAsDynamicMontage() return {} end
    function AI:Montage_SetPlayRate() end
    NSK = {}
    NSN = {s = S2L.nsS0(0)}
    S2L.NB = nil
    PFXP = {pawn = mkPawn()}
    S2L.paMiss = {}
    seqs = {}
    RECYC = 0
end
fresh()
function G(v) if v == nil then return "nil" end return string.format("%.9g", v) end
function SO(v) if v == nil then return "nil" end return tostring(v) end
REMOTES = {}
function remoteOf(seq, recv, x, y, z, vx, vy, yaw, t)
    local r = REMOTES[seq]
    if not r then r = {}; REMOTES[seq] = r end
    r.recv, r.x, r.y, r.z, r.vx, r.vy, r.yaw, r.t = recv, x, y, z, vx, vy, yaw, t
    return r
end
function doPup(now, r, fh, own)
    NOW = now
    X1.FH = fh
    OWN = own
    api.getRemote = function() return r end
    api.getLocal = function() return {X = r.x, Y = r.y, Z = r.z} end
    S2L.pupTick(ST)
    local P = ST.pup
    return G(P.pos and P.pos.X) .. " " .. G(P.pos and P.pos.Y) .. " " .. G(P.pos and P.pos.Z) .. " " .. G(P.yaw)
end
function doTgt(now, hp, px, py, pz)
    NOW = now
    ST.pup.pos = {X = px, Y = py, Z = pz}
    ST.tgt.pawn.hp = hp
    local r = {x = px, y = py, z = pz, recv = now}
    api.getRemote = function() return r end
    api.getLocal = function() return {X = px, Y = py, Z = pz} end
    tgtTick(ST)
    local D = ST.dmg
    return G(ST.tgt.ref) .. " " .. string.format("%d", D.seq) .. " " .. G(D.total) .. " " .. string.format("%d", D.dropped or 0)
end
function doPa(now, recv, ryaw, rt, speed, vx, vy, wstr, mv, miss)
    NOW = now
    MISS = miss
    X1.MV = mv
    local r = {recv = recv, yaw = ryaw, t = rt}
    api.getRemote = function() return r end
    PAP.wstr = wstr
    S2L.paClips(PAP, PAQ, AI, speed, vx, vy)
    local Q = PAQ
    return SO(Q.paSt) .. " " .. SO(Q.paFace) .. " " .. SO(Q.clip) .. " " .. G(Q.rate) .. " " .. string.format("%d", Q.clipBad) .. " " .. SO(PAP.paFace)
end
function doNss(x, y, z, yaw, ts, now, vx, vy, vz, still)
    NOW = now
    return tostring(S2L.nsSample(NSK, x, y, z, yaw, ts, now, vx, vy, vz, still))
end
function doNsd(now, ax, ay, az, cur)
    NOW = now
    local a = mkPawn()
    a.ax, a.ay, a.az, a.cur = ax, ay, az, cur
    a.snap = false
    NSK.pmc = NSK.pmc or {}
    local ok = S2L.nsDrive(NSN, NSK, a, now)
    if not ok then return "false" end
    local v = NSK.pmc.Velocity
    local s = NSN.s
    return "true " .. (a.snap and (G(a.loc[1]) .. "," .. G(a.loc[2]) .. "," .. G(a.loc[3])) or "-") .. " " .. G(v.X) .. " " .. G(v.Y) .. " " .. G(v.Z) .. " " ..
        (a.rot and G(a.rot) or "-") .. " " .. string.format("%d", s.dr) .. " " .. string.format("%d", s.sn) .. " " .. G(s.emax)
end
function doPfx(now, path, uid, ax, ay, az, wx, wy, speed)
    NOW = now
    local P = PFXP
    P.path, P.uid = path, uid
    P.pmW = {X = wx, Y = wy}
    P.pawn.ax, P.pawn.ay, P.pawn.az = ax, ay, az
    return tostring(S2L.pfxTick(ST, P, speed, now))
end
"""


def extract(lines):
    out = []
    for h in ONE:
        out.append(next(l for l in lines if l.startswith(h)))
    for h in FUNCS:
        a = next(i for i, l in enumerate(lines) if l.startswith(h))
        b = next(i for i in range(a + 1, len(lines)) if lines[i] == "end")
        out.append("\n".join(lines[a:b + 1]))
    return out


def tab_blocks(lines):
    def block(s, e):
        a = next(i for i, l in enumerate(lines) if l.startswith(s))
        b = next(i for i in range(a, len(lines)) if lines[i].startswith(e))
        return "\n".join(lines[a:b + 1])
    return [block("local PUP_ANIM = {", "local WALK_MAX"), block('S2L.PA_DIR = "', "S2L.PA_SEC = {")]


def tok(v):
    if v is None:
        return "nil"
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return repr(v)
    return str(v)


def flag(cases, k, v):
    cases.append(("F", k, v))


def gen():
    rnd = random.Random(1717)
    cases = []
    base = {"avlead": 200, "avgain": 30, "smoothsnap": 300, "smoothgain": 4, "smoothlog": 1, "avatar": 1, "bodysafe": 1,
            "kzsafe": 0, "farbody": 0, "pupmove": 0, "nsmooth": 0, "pupfix": 0, "pupmvlog": 0, "dmgscale": 1, "dmgmin": 1,
            "tgtgod": 0, "tgtdiag": 0, "tgtnofall": 0, "avoffx": 0, "nsxcap": 0}
    for k, v in base.items():
        flag(cases, k, v)
    ws = ["", "W1~/Game/_Stalker_2/weapons/pt/pt_pm/SK_pm.SK_pm@@W@s~", "/Game/Weapons/ar/x", "/Game/weapons/knifes/k/",
          "/game/Weapons/pt/", "/Weapons/grenades/g/", "/Weapons/sg/a/b", "/Weapons/Attaches/", "/weapons/items/x/", "/Weapons//"]
    for w in ws:
        cases.append(("fam", w if w else "nil"))
    for _ in range(300):
        cases.append(("sec", rnd.choice([None, rnd.uniform(-400, 400)]), rnd.uniform(-600, 600), rnd.uniform(-600, 600), rnd.choice([0, 20, 29.9, 30, 150, 400])))
    secs = ["f", "l", "r", "bl", "br", "b"]
    for fl in [{}, {"pabh": 1}, {"pafall": 0}, {"pupmvsprint": 400}]:
        for k, v in fl.items():
            flag(cases, k, v)
        for _ in range(120):
            cases.append(("pick", rnd.choice([0, 10, 30, 100, 229, 230, 300, 450, 520, 700]), rnd.choice(secs), rnd.randint(0, 1), rnd.randint(0, 1),
                          rnd.randint(0, 1), rnd.choice([None, "turnl", "turnr"])))
        for k in fl:
            flag(cases, k, None)
    sts = ["idle", "walk", "run", "sprint", "cidle", "cwalk", "aidle", "awalk", "left", "right", "bleft", "bright", "back", "fall",
           "turnl", "turnr", "rf", "rl", "rr", "rb", "rbl", "rbr", "nope"]
    for fl in [{}, {"pabh": 1}, {"papt": 0}]:
        for k, v in fl.items():
            flag(cases, k, v)
        for st in sts:
            for fam in ["bh", "ar", "pt"]:
                for miss in [0, 3, 7]:
                    cases.append(("clip", st, fam, miss))
        for k in fl:
            flag(cases, k, None)
    for s in ["1,0", "1,0,1", "0,1,", "1,1,x", "x,1", "12,1", "1,", "", "1,0,", "3,4,5,6"]:
        cases.append(("mv", s if s else "nil"))
    for s in [None, "a,100,200", "w,-1,5000.5", "d,0,0", "a,", "aa,1,2", "a,1", "a,1,", "1,2,3", "x,1.5,-2#"]:
        cases.append(("life", s))
    for _ in range(100):
        cases.append(("ang", rnd.uniform(-900, 900), rnd.uniform(-900, 900)))
    for v in [1.0, 0.1, 100000.0, 5000.0, 1e20, 1e15, 123456789012345.0, 2.5, -0.0, 3.14159265358979, 1e-5]:
        cases.append(("num", v))
    for variant in [{}, {"smooth": 1}, {"rvder": 1}, {"pupfeet": 1}, {"smooth": 1, "rvder": 1, "pupfeet": 1}]:
        cases.append(("RESET",))
        for k, v in variant.items():
            flag(cases, k, v)
        now, recv, seq, t = 1.0, 0.95, 1, 100.0
        x, y, z = rnd.uniform(-5000, 5000), rnd.uniform(-5000, 5000), rnd.uniform(0, 500)
        yaw = rnd.uniform(-180, 180)
        for i in range(260):
            now += rnd.choice([0.1, 0.1, 0.1, 0.12, 0.25, 0.05])
            if rnd.random() < 0.85:
                seq += 1
                dt = rnd.choice([0.1, 0.1, 0.15, 0.3, 0.02])
                t += dt
                recv = now - rnd.uniform(0, 0.08)
                vx, vy = rnd.choice([(0, 0), (rnd.uniform(-600, 600), rnd.uniform(-600, 600))])
                x += vx * dt + rnd.uniform(-3, 3)
                y += vy * dt + rnd.uniform(-3, 3)
                if rnd.random() < 0.02:
                    x += 300000
                yaw = yaw + rnd.uniform(-40, 40)
                svx, svy = (vx, vy) if rnd.random() < 0.6 else (0.0, 0.0)
                snap = (seq, recv, x, y, z, svx, svy, rnd.choice([yaw, yaw, None]), rnd.choice([t, t, None]))
            if rnd.random() < 0.03:
                now += 6
            fh = rnd.choice([None, "88.0", "45", "5", "abc", "250"])
            own = rnd.choice([None, 88.0, 60.0, 5.0])
            cases.append(("pup", now, snap[1], snap[0], snap[2], snap[3], snap[4], snap[5], snap[6], snap[7], snap[8], fh, own))
        for k in variant:
            flag(cases, k, None)
    for variant in [{}, {"dmgmin": 0}, {"dmgscale": 2.5, "dmgmin": 3}]:
        cases.append(("RESET",))
        for k, v in variant.items():
            flag(cases, k, v)
        now = 10.0
        hp = 5000.0
        for i in range(120):
            now += 0.1
            r = rnd.random()
            if r < 0.4:
                hp = hp - rnd.choice([0.5, 2, 30, 4600, 0.01, 10])
            elif r < 0.5:
                hp = 5000.0
            px, py, pz = rnd.uniform(-100, 100), rnd.uniform(-100, 100), rnd.uniform(0, 50)
            if rnd.random() < 0.05:
                cases.append(("tjump", now + rnd.choice([-1, 1])))
            if rnd.random() < 0.05:
                cases.append(("tquiet", 0.0, 0.0, rnd.choice([50, 500]), now + rnd.choice([-1, 2])))
            if rnd.random() < 0.08:
                d = (rnd.uniform(-1, 1), rnd.uniform(-1, 1), rnd.uniform(-0.2, 0.2))
                n = math.sqrt(sum(c * c for c in d)) or 1
                cases.append(("tshot", now - rnd.choice([0.1, 0.9]), px - 500, py, pz, 1.0, 0.0, 0.0) if rnd.random() < 0.5 else
                             ("tshot", now - 0.1, px, py, pz + 1000, d[0] / n, d[1] / n, d[2] / n))
            cases.append(("tgt", now, hp, px, py, pz))
            if hp < 4999.95:
                hp = 5000.0
        for k in variant:
            flag(cases, k, None)
    for variant in [{}, {"pabh": 1}, {"papt": 0}, {"pahold": 1, "patrn": 20}]:
        cases.append(("RESET",))
        for k, v in variant.items():
            flag(cases, k, v)
        now, recv, ry, rt = 5.0, 4.9, 0.0, 50.0
        w = rnd.choice(ws[:6])
        for i in range(150):
            now += 0.1
            if rnd.random() < 0.8:
                recv = now - 0.02
                rt += 0.1
                ry += rnd.choice([0, 2, 10, -15, 30])
            speed = rnd.choice([0, 10, 50, 150, 240, 300, 600])
            ang = math.radians(rnd.uniform(-180, 180))
            if rnd.random() < 0.05:
                w = rnd.choice(ws)
            cases.append(("pa", now, recv, rnd.choice([ry, ry, None]), rnd.choice([rt, None]), speed, speed * math.cos(ang), speed * math.sin(ang),
                          w if w else None, rnd.choice(["0,0,0", "1,0,0", "0,1,0", "0,0,1", "1,1", None]), rnd.choice([0, 0, 0, 5])))
        for k in variant:
            flag(cases, k, None)
    for variant in [{"nsip": 0, "nsnoback": 0}, {"nsip": 1, "nsnoback": 0}, {"nsip": 1, "nsnoback": 1}, {"nsip": 0, "nsnoback": 1, "nscap": 0}]:
        cases.append(("RESET",))
        for k, v in variant.items():
            flag(cases, k, v)
        now, ts = 3.0, 30.0
        x, y, z = 100.0, 200.0, 0.0
        ax, ay, az = x, y, z
        for i in range(300):
            now += rnd.choice([0.033, 0.016, 0.1, 0.2])
            if rnd.random() < 0.3:
                ts += rnd.choice([0.1, 0.1, 0.5, -2, 0.0])
                vx = rnd.choice([None, rnd.uniform(-400, 400)])
                x += rnd.uniform(-50, 50)
                y += rnd.uniform(-50, 50)
                if rnd.random() < 0.03:
                    x += 2000
                cases.append(("nss", x, y, z, rnd.choice([None, rnd.uniform(-180, 180)]), rnd.choice([ts, ts, None]), now, vx,
                              None if vx is None else rnd.uniform(-400, 400), None if vx is None else 0.0, rnd.randint(0, 1)))
            ax += (x - ax) * 0.3 + rnd.uniform(-2, 2)
            ay += (y - ay) * 0.3 + rnd.uniform(-2, 2)
            cases.append(("nsd", now, ax, ay, az, rnd.uniform(-180, 180)))
        for k in variant:
            flag(cases, k, None)
    cases.append(("RESET",))
    for variant in [{}, {"pupfixn": 2, "pupfixmax": 2}]:
        for k, v in variant.items():
            flag(cases, k, v)
        now = 100.0
        for i in range(200):
            now += rnd.choice([1, 2, 5, 60, 300])
            path = rnd.choice(["A", "A", "A", "B", None])
            uid = rnd.choice([7, 7, 7, 8, None])
            cases.append(("pfx", now, path, uid, rnd.uniform(-500, 500), rnd.uniform(-500, 500), 10.0, 0.0, 0.0, rnd.uniform(0, 500)))
        for k in variant:
            flag(cases, k, None)
    return cases


def num_or_none(s):
    return None if s == "nil" else float(s)


def lval(s, kind=float):
    if s == "nil":
        return None
    return kind(s)


def run_lua(lines, cases):
    L = lupa.LuaRuntime(unpack_returned_tuples=True)
    L.execute(STUBS)
    chunk = "\n".join(tab_blocks(lines) + extract(lines)) + "\n_G.tgtTick = tgtTick\n_G.WALK_MAX = WALK_MAX\n"
    L.execute(chunk)
    L.execute(DRIVER)
    g = L.globals()
    out = []
    for n, c in enumerate(cases, 1):
        g.LINE = n
        k = c[0]
        t = [tok(v) for v in c[1:]]
        if k == "F":
            v = c[2]
            g.FL[c[1]] = v
            continue
        if k == "RESET":
            L.execute("fresh()")
            continue
        if k == "fam":
            g.PAP.wstr = None if t[0] == "nil" else t[0]
            res = g.S2L.paFam(g.PAP)
        elif k == "sec":
            res = g.S2L.paSector(lval(t[0]), float(t[1]), float(t[2]), float(t[3]))
        elif k == "pick":
            r = g.S2L.paPick(float(t[0]), t[1], t[2] == "1", t[3] == "1", t[4] == "1", lval(t[5], str))
            res = "%s %s" % r
        elif k == "clip":
            g.MISS = int(t[2])
            g.S2L.paMiss = L.table()
            g.seqs = L.table()
            r = g.S2L.paClip(t[0], t[1])
            if r is None or r[0] is None:
                res = "nil"
            else:
                e = r[1]
                res = "%s %s %s" % (e[1], r[2], "nil" if e[2] is None else ("%.9g" % e[2]))
        elif k == "mv":
            s = "" if t[0] == "nil" else t[0]
            r = L.eval("function(s) return s:match('^(%d),(%d),?(%d?)') end")(s)
            if r is None:
                r = (None, None, None)
            if not isinstance(r, tuple):
                r = (r, None, None)
            res = " ".join("nil" if v is None else ("<e>" if v == "" else v) for v in r)
        elif k == "life":
            g.X1.L = lval(t[0], str)
            r = g.M.remoteLife()
            st = r[0] if isinstance(r, tuple) else r
            res = "nil" if st is None else st
        elif k == "ang":
            res = "%.9g" % g.api.angDiff(float(t[0]), float(t[1]))
        elif k == "num":
            res = L.eval("tostring")(float(t[0]))
        elif k == "pup":
            r = g.remoteOf(int(t[2]), float(t[1]), float(t[3]), float(t[4]), float(t[5]), float(t[6]), float(t[7]), lval(t[8]), lval(t[9]))
            res = g.doPup(float(t[0]), r, lval(t[10], str), lval(t[11]))
        elif k == "tjump":
            g.ST.jumpUntil = float(t[0])
            continue
        elif k == "tquiet":
            q = L.table()
            q.x, q.y, q.r, q.untilT = float(t[0]), float(t[1]), float(t[2]), float(t[3])
            g.ST.anomQuiet = q
            continue
        elif k == "tshot":
            q = L.table()
            q.at, q.ox, q.oy, q.oz, q.dx, q.dy, q.dz = [float(v) for v in t[:7]]
            g.ST.lshot = q
            continue
        elif k == "tgt":
            res = g.doTgt(*[float(v) for v in t[:5]])
        elif k == "pa":
            res = g.doPa(float(t[0]), float(t[1]), lval(t[2]), lval(t[3]), float(t[4]), float(t[5]), float(t[6]), lval(t[7], str), lval(t[8], str), int(t[9]))
        elif k == "nsk":
            L.execute("NSK = {}")
            continue
        elif k == "nss":
            res = g.doNss(float(t[0]), float(t[1]), float(t[2]), lval(t[3]), lval(t[4]), float(t[5]), lval(t[6]), lval(t[7]), lval(t[8]), t[9] == "1")
        elif k == "nsd":
            res = g.doNsd(float(t[0]), float(t[1]), float(t[2]), float(t[3]), float(t[4]))
        elif k == "pfx":
            res = g.doPfx(float(t[0]), lval(t[1], str), lval(t[2], int), float(t[3]), float(t[4]), float(t[5]), float(t[6]), float(t[7]), float(t[8]))
        else:
            res = "?" + k
        out.append("R %d %s" % (n, res))
    logs = ["L " + g.LOGS[i].replace("S2CORE", "NOLA").replace("S2Coop_", "Nola_") for i in range(1, len(g.LOGS) + 1)]
    return out, logs


NUM = re.compile(r"-?\d+(?:\.\d+)?(?:e[-+]?\d+)?|-?inf|nan", re.I)


def same(a, b):
    if a == b:
        return True
    ta, tb = NUM.split(a), NUM.split(b)
    na, nb = NUM.findall(a), NUM.findall(b)
    if ta != tb or len(na) != len(nb):
        return False
    for x, y in zip(na, nb):
        fx, fy = float(x), float(y)
        if fx == fy:
            continue
        if abs(fx - fy) > 1e-7 * max(1.0, abs(fx), abs(fy)):
            return False
    return True


def main():
    raw = CORE.read_bytes()
    md5 = hashlib.md5(raw).hexdigest()
    if not md5.startswith(WANT_MD5):
        sys.exit("core md5 %s != %s" % (md5, WANT_MD5))
    lines = raw.decode("utf-8").replace("\r\n", "\n").split("\n")
    cases = gen()
    cf = HERE / "cases.txt"
    with cf.open("w", newline="\n") as f:
        for c in cases:
            f.write(" ".join([c[0]] + [tok(v) for v in c[1:]]) + "\n")
    exe = Path(sys.argv[1]) if len(sys.argv) > 1 else HERE / "out" / "test_bodies.exe"
    cpp = subprocess.run([str(exe), str(cf)], capture_output=True, text=True, encoding="utf-8").stdout.splitlines()
    c_res = [l for l in cpp if l.startswith("R ")]
    c_log = [l for l in cpp if l.startswith("L ")]
    l_res, l_log = run_lua(lines, cases)
    bad = 0
    for a, b in zip(c_res, l_res):
        if not same(a, b):
            bad += 1
            if bad <= 15:
                print("RES  C++ :", a, "\n     Lua :", b)
    if len(c_res) != len(l_res):
        bad += 1
        print("result count C++ %d Lua %d" % (len(c_res), len(l_res)))
    lb = 0
    for a, b in zip(c_log, l_log):
        if not same(a, b):
            lb += 1
            if lb <= 15:
                print("LOG  C++ :", a, "\n     Lua :", b)
    if len(c_log) != len(l_log):
        lb += 1
        print("log count C++ %d Lua %d" % (len(c_log), len(l_log)))
    kinds = {}
    for c in cases:
        kinds[c[0]] = kinds.get(c[0], 0) + 1
    print("cases", len(cases), "results", len(l_res), "logs", len(l_log), "kinds", kinds)
    print("result mismatches", bad, "log mismatches", lb)
    sys.exit(1 if bad or lb else 0)


if __name__ == "__main__":
    main()
