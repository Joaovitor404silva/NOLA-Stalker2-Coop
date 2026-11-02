import glob
import os
import random
import subprocess
import sys

import lupa.lua54 as lupa

ROOT = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.join(ROOT, "s2core_0b56e1b5.lua")
EXE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "test_orphans.exe")

HARNESS = r"""
TR = {}
local function tr(s) TR[#TR + 1] = s end
T = 100
FL = {}
api = {ROLE = "host"}
REM = nil
api.getRemote = function() return REM end
function log(s) tr("log " .. tostring(s)) end
os.clock = function() return T end
S2 = {io = {}, errs = {n = 0}}
ST = {pup = {}}
M = {hooks = {}, dg = {}}
S2L = {}
LOCK = nil
MIR, NAT = {}, {}
OBJS, BYN, BYID, PATHS = {}, {}, {}, {}
TIMERS, TSEQ = {}, 0
local O = {}
local MT = {}
MT.__index = function(o, k)
    local m = O[k]
    if m then return m end
    return rawget(o, "_p")[k]
end
MT.__newindex = function(o, k, v)
    rawget(o, "_p")[k] = v
    if k == "Velocity" or k == "Acceleration" then tr(string.format("set %s %s %.1f %.1f %.1f", o._name, k, v.X, v.Y, v.Z))
    elseif k == "bHasRequestedVelocity" then tr(string.format("set %s %s %d", o._name, k, v and 1 or 0)) end
end
function make(cls, name, path, full)
    local o = setmetatable({_p = {}, _cls = cls, _name = name, _alive = true, _id = #OBJS + 1, _loc = {X = 0, Y = 0, Z = 0}, _rot = {Pitch = 0, Yaw = 0, Roll = 0},
        _vel = {X = 0, Y = 0, Z = 0}, _isAlive = true, _threat = false, _sleeping = false, _tickOn = true, _uid = 0, _pid = 0, _socks = {}}, MT)
    o._full = (full and full ~= "") and (cls .. " " .. full) or (cls .. " /Game/Map.Map:PersistentLevel." .. name)
    OBJS[#OBJS + 1] = o; BYN[name] = o; BYID[o._id] = o
    if path then PATHS[full] = o end
    return o
end
local function fn(s) return setmetatable({s = s}, {__index = {ToString = function(t) return t.s end}}) end
local function top(o) while o._owner do o = o._owner end return o end
local function b(v) return v and 1 or 0 end
local function nm(o) return o and o._name or "nil" end
function O.IsValid(o) return o._alive end
function O.GetAddress(o) return o._id end
function O.GetFName(o) return fn(o._name) end
function O.GetFullName(o) return o._full end
function O.K2_GetActorLocation(o) return {X = o._loc.X, Y = o._loc.Y, Z = o._loc.Z} end
function O.K2_GetActorRotation(o) return {Pitch = o._rot.Pitch, Yaw = o._rot.Yaw, Roll = o._rot.Roll} end
function O.K2_SetActorLocation(o, l, sw, hit, tp) o._loc = {X = l.X, Y = l.Y, Z = l.Z}; tr(string.format("call %s K2_SetActorLocation %.1f %.1f %.1f sweep=%d tp=%d", o._name, l.X, l.Y, l.Z, b(sw), b(tp))) end
function O.K2_SetActorRotation(o, r) o._rot = {Pitch = r.Pitch, Yaw = r.Yaw, Roll = r.Roll}; tr(string.format("call %s K2_SetActorRotation %.1f %.1f %.1f", o._name, r.Pitch, r.Yaw, r.Roll)) end
function O.IsAlive(o) return o._isAlive end
function O.IsInThreat(o) return o._threat end
function O.IsSleeping(o) return o._sleeping end
function O.GetVelocity(o) return {X = o._vel.X, Y = o._vel.Y, Z = o._vel.Z} end
function O.GetGUID(o, a) return a._uid end
function O.GetPrototypeID(o, a) return a._pid end
function O.GetController(o) return o._ctl end
function O.StopLogic(o, r) tr(string.format("call %s StopLogic %s", o._name, r)) end
function O.RestartLogic(o) tr(string.format("call %s RestartLogic", o._name)) end
function O.SetActorTickEnabled(o, v) tr(string.format("call %s SetActorTickEnabled %d", o._name, b(v))) end
function O.TogglePathFollowingComponent(o, v) tr(string.format("call %s TogglePathFollowingComponent %d", o._name, b(v))) end
function O.StopMovementImmediately(o) tr(string.format("call %s StopMovementImmediately", o._name)) end
function O.SetComponentTickEnabled(o, v) o._tickOn = v; tr(string.format("call %s SetComponentTickEnabled %d", o._name, b(v))) end
function O.IsComponentTickEnabled(o) return o._tickOn end
function O.GetAnimInstance(o) return o._ai end
function O.GetCurrentActiveMontage(o) return o._mont end
function O.XActivateAIFeature(o, m) tr(string.format("call %s XActivateAIFeature %d", o._name, m)) end
function O.XALifeEnable(o) tr(string.format("call %s XALifeEnable", o._name)) end
function O.GetOwner(o) return o._owner end
function O.GetWeaponInHandsMeshComponent(o) return o._weapon end
function O.GetSkeletalMeshAsset(o) return o._skel end
function O.DoesSocketExist(o, f) return o._socks[f] == true end
function O.GetSocketLocation(o, f) local a = top(o); return {X = a._loc.X + 5, Y = a._loc.Y + #f, Z = a._loc.Z + 100} end
function O.GetSocketRotation(o, f) return {Pitch = 3, Yaw = top(o)._rot.Yaw, Roll = 0} end
function O.K2_GetComponentLocation(o) local a = top(o); return {X = a._loc.X + 30, Y = a._loc.Y + 10, Z = a._loc.Z + 120} end
function O.K2_GetComponentRotation(o) return {Pitch = 1, Yaw = top(o)._rot.Yaw, Roll = 2} end
function O.PostEventAtLocation(o, ev, l, r, a) tr(string.format("call %s PostEventAtLocation %s %.1f %.1f %.1f at=%s", o._name, nm(ev), l.X, l.Y, l.Z, nm(a))); return 1 end
function O.SpawnSystemAtLocation(o, a, ns, l, r) tr(string.format("call %s SpawnSystemAtLocation %s %.1f %.1f %.1f %.1f", o._name, nm(ns), l.X, l.Y, l.Z, r.Yaw)) end
function O.SpawnAkComponentAtLocation(o, a, ev, l, r, ap, ad)
    local k = make("AkComponent", "AkComp_" .. #OBJS)
    tr(string.format("call %s SpawnAkComponentAtLocation %s %.1f %.1f %.1f auto=%d,%d", o._name, nm(ev), l.X, l.Y, l.Z, b(ap), b(ad)))
    return k
end
function O.SetSwitch(o, sw) tr(string.format("call AkComp SetSwitch %s", nm(sw))) end
function O.PostAkEventAndWaitForEnd(o, ev, li) tr(string.format("call AkComp PostAkEventAndWaitForEnd %s", nm(ev))); return 7 end
function FindAllOf(cls)
    local r = {}
    for _, o in ipairs(OBJS) do if o._alive and ((cls == "Pawn" and o._pawn) or o._cls == cls) then r[#r + 1] = o end end
    if #r == 0 then return nil end
    return r
end
function FindFirstOf(cls) for _, o in ipairs(OBJS) do if o._alive and o._cls == cls then return o end end end
function StaticFindObject(p) local o = PATHS[p]; if o and o._alive then return o end end
function LoadAsset(p) return nil end
function FindObject(cls, n) for _, o in ipairs(OBJS) do if o._alive and o._cls == cls and o._name == n then return o end end end
function FName(s) return s end
function ExecuteInGameThreadWithDelay(ms, f) TSEQ = TSEQ + 1; TIMERS[#TIMERS + 1] = {at = T + ms / 1000, seq = TSEQ, fn = f} end
M.dg.cdoOf = StaticFindObject
function M.isOwnBody(ST, o) return false end
function s2n_nat_actor(n) local o = NAT[n]; if o and o._alive then return o, o._weapon, nil end end
function s2n_nat_fire(n) end
function s2n_nat_listed_addr(ad) local o = BYID[ad]; return o ~= nil and o._listed == true end
ST.alife = {hid = setmetatable({}, {__index = function(t, ad) local o = BYID[ad]; if o and o._hidden then return true end end})}
function S2L.ftLock(ST, F, now) return LOCK end
function S2L.clMirs(ST)
    local t = {}
    for k, o in pairs(MIR) do t[k] = {actor = o, socks = {o._weapon}, comp = nil} end
    return t
end
function S2L.nsSt(ST)
    local N = ST.ns
    if not N then
        local store = {}
        N = {store = store, s = {es = 0, en = 0}}
        N.k = setmetatable({}, {__index = store, __newindex = function(t, k, v) if v == nil then tr("mover forget " .. k) end; store[k] = v end})
        ST.ns = N
    end
    return N
end
function S2L.nsMover(N, a, key)
    local addr = a:GetAddress()
    local K = N.store[key]
    if K and K.addr == addr then return K, false end
    K = {addr = addr, key = key, vx = 0, vy = 0, vz = 0, pmc = {Deactivate = function() tr("mover deactivate " .. key) end}}
    N.store[key] = K
    tr("mover new " .. key)
    return K, true
end
function S2L.nsSample(K, x, y, z, yaw, ts, now, vx, vy, vz)
    tr(string.format("mover sample %s %.1f %.1f %.1f ts=%.2f v=%.1f,%.1f,%.1f", K.key, x, y, z, ts, vx, vy, vz))
    K.x, K.y, K.z, K.vx, K.vy, K.vz, K.ts = x, y, z, vx, vy, vz, ts
    return true
end
function S2L.nsDrive(N, K, a, now)
    if not valid(a) then return false end
    N.s.es = N.s.es + 5; N.s.en = N.s.en + 1
    tr(string.format("mover drive %s yaw=%.1f", K.key, K.yaw or -1))
    return true
end
function S2L.nsStop(K) tr("mover stop " .. K.key) end
"""

RUNNER = r"""
local function toks(l) local t = {} for w in l:gmatch("%S+") do t[#t + 1] = w end return t end
local function rest(l, n) local p = 1 for _ = 1, n do p = l:find("%S", p) or #l + 1; p = l:find(" ", p) or #l + 1 end return (l:sub(p):gsub("^ +", "")) end
local function me() for _, o in ipairs(OBJS) do if o._alive and o._cls == "BP_Stalker2Character_C" then return o end end end
local function sub(ow, cls, n) local o = make(cls, n); o._owner = ow; return o end
local function N(n) return BYN[n] end
local function pawn(n, x, y, z, yaw, uid, pid)
    local p = make("BP_NPC_C", n)
    p._pawn = true; p._loc = {X = x, Y = y, Z = z}; p._rot = {Pitch = 0, Yaw = yaw, Roll = 0}; p._uid = uid; p._pid = pid
    p._ctl = sub(p, "AIController", n .. "_ctl")
    local br = sub(p._ctl, "BrainComponent", n .. "_brain")
    p._ctl._p.BrainComponent = br
    local cm = sub(p, "CharacterMovementComponent", n .. "_cm")
    p._p.CharacterMovement = cm
    local mesh = sub(p, "SkeletalMeshComponent", n .. "_mesh")
    p._p.Mesh = mesh
    mesh._ai = sub(mesh, "AnimInstance", n .. "_ai")
    return p
end
local function montage(full)
    if full == "-" then return nil end
    local n = full:match("([^.]*)$")
    return make("AnimMontage", n, false, full)
end
local function advance(t)
    while true do
        local bi
        for i, x in ipairs(TIMERS) do if not bi or x.at < TIMERS[bi].at or (x.at == TIMERS[bi].at and x.seq < TIMERS[bi].seq) then bi = i end end
        if not bi or TIMERS[bi].at > t + 1e-9 then break end
        local x = table.remove(TIMERS, bi)
        T = x.at
        x.fn()
    end
    T = t
end
local function head()
    local m = me()
    S2.io.pawn = m
end
function RUN(text)
    local out = {}
    local ln = 0
    for line in (text .. "\n"):gmatch("(.-)\r?\n") do
        ln = ln + 1
        local t = toks(line)
        if #t > 0 and t[1]:sub(1, 1) ~= ";" then
            TR = {}
            local c = t[1]
            head()
            if c == "role" then api.ROLE = t[2]
            elseif c == "t" then advance(tonumber(t[2]))
            elseif c == "flag" then FL[t[2]] = math.tointeger(tonumber(t[3])) or tonumber(t[3])
            elseif c == "me" then local m = me(); if not m then m = make("BP_Stalker2Character_C", "Me"); m._pawn = true end; m._loc = {X = tonumber(t[2]), Y = tonumber(t[3]), Z = tonumber(t[4])}
            elseif c == "newme" then local m = me(); local l = m and m._loc or {X = 0, Y = 0, Z = 0}; if m then m._alive = false end; local n = make("BP_Stalker2Character_C", "Me"); n._pawn = true; n._loc = l
            elseif c == "remote" then if t[2] == "none" then REM = {} else REM = {x = tonumber(t[2]), y = tonumber(t[3]), z = tonumber(t[4])} end
            elseif c == "pawn" then pawn(t[2], tonumber(t[3]), tonumber(t[4]), tonumber(t[5]), tonumber(t[6]), math.tointeger(tonumber(t[7])), math.tointeger(tonumber(t[8])))
            elseif c == "mv" then local o = N(t[2]); o._loc = {X = tonumber(t[3]), Y = tonumber(t[4]), Z = tonumber(t[5])}; if t[6] then o._rot.Yaw = tonumber(t[6]) end
            elseif c == "vel" then N(t[2])._vel = {X = tonumber(t[3]), Y = tonumber(t[4]), Z = 0}
            elseif c == "threat" then N(t[2])._threat = t[3] == "1"
            elseif c == "dead" then N(t[2])._isAlive = false
            elseif c == "gone" then N(t[2])._alive = false
            elseif c == "lock" then LOCK = (t[2] ~= "-") and t[2] or nil
            elseif c == "sleep" then me()._sleeping = t[2] == "1"
            elseif c == "cmcon" then N(t[2])._p.CharacterMovement._tickOn = true
            elseif c == "x1" then S2.io.head = string.format('{"t":%s,"seq":%s,"x1":"%s"}', t[3], t[2], #t > 3 and rest(line, 3) or "")
            elseif c == "weapon" then
                local o = N(t[2])
                o._weapon = sub(o, "SkeletalMeshComponent", t[2] .. "_wpn")
                o._weapon._skel = make("SkeletalMesh", t[3])
                if t[4] and t[4] ~= "-" then for s in t[4]:gmatch("[^,]+") do o._weapon._socks[s] = true end end
            elseif c == "montage" then N(t[2])._p.Mesh._ai._mont = montage(t[3])
            elseif c == "face" then
                local o = N(t[2]); local ai = o._p.Mesh._ai
                local f = sub(ai, "SkeletalMeshComponent", t[2] .. "_face")
                f._ai = sub(f, "AnimInstance", t[2] .. "_fai")
                ai._p.FaceData = {FaceMeshComponent = f}
            elseif c == "facemont" then N(t[2])._p.Mesh._ai._p.FaceData.FaceMeshComponent._ai._mont = montage(t[3])
            elseif c == "animref" then
                local m = N(t[2])._p.Mesh._ai._mont
                if m then m._p.SlotAnimTracks = {{AnimTrack = {AnimSegments = {{AnimReference = montage(t[3])}}}}} end
            elseif c == "speaker" then
                local o = N(t[2])
                local vc = make("VoiceoverSpeechComponent", t[2] .. "_vc")
                vc._p.AkVoiceoverSpeakerComponent = sub(o, "AkComponent", t[2] .. "_spk")
            elseif c == "spkev" then
                local s = N(t[2] .. "_spk")
                if t[3] == "-" then s._p.AkAudioEvent = nil else s._p.AkAudioEvent = make("AkAudioEvent", t[3]:match("([^.]*)$"), false, t[3]) end
            elseif c == "asset" then make(t[2], t[3]:match("([^.]*)$"), true, t[3])
            elseif c == "natactor" then NAT[t[2]] = N(t[3])
            elseif c == "mgr" then make("CustomConsoleManagerAM", "AM"); make("CustomConsoleManagerSS", "SS")
            elseif c == "med" then make("CppMediator", "Default__CppMediator", true, "/Script/Stalker2.Default__CppMediator")
            elseif c == "cdo" then
                make("AkGameplayStatics", "AkGS", true, "/Script/AkAudio.Default__AkGameplayStatics")
                make("NiagaraFunctionLibrary", "Niagara", true, "/Script/Niagara.Default__NiagaraFunctionLibrary")
            elseif c == "step" then
                if t[2] == "ws" then S2L.wsTick(ST)
                elseif t[2] == "aiw" then pcall(S2L.aiwTick, ST) end
            elseif c == "out" then
                tr("out " .. S2L.wsOut(ST))
            elseif c == "shot" then
                local o = N(t[2])
                local k = N(t[2] .. "_iac") or sub(o, "ItemAppearanceComponent", t[2] .. "_iac")
                M.hooks["/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"](ST, {get = function() return k end}, nil)
            else error("bad command " .. line) end
            table.sort(TR)
            out[#out + 1] = "== " .. ln .. " " .. line
            for _, s in ipairs(TR) do out[#out + 1] = s end
        end
    end
    return table.concat(out, "\n") .. "\n"
end
"""


def lines():
    with open(CORE, encoding="utf-8") as f:
        return f.read().split("\n")


def chunk(L, a, b):
    return "\n".join(L[a - 1:b])


def build_lua():
    L = lines()
    lua = lupa.LuaRuntime(unpack_returned_tuples=True)
    src = [HARNESS]
    src.append(chunk(L, 446, 450).replace("local function valid", "function valid"))
    src.append(chunk(L, 494, 498).replace("local function asset", "function asset"))
    src.append(chunk(L, 584, 593).replace("local function x1part", "function x1part"))
    src.append(chunk(L, 480, 483))
    src.append(chunk(L, 7382, 7382))
    src.append(chunk(L, 11415, 12207))
    src.append(chunk(L, 12209, 12242))
    src.append(chunk(L, 13659, 14129))
    src.append(RUNNER)
    lua.execute("\n".join(src))
    return lua, L


def hx(s):
    return s.encode("utf-8", errors="surrogateescape").hex()


def unhx(s):
    return bytes.fromhex(s).decode("utf-8", errors="surrogateescape")


def find_line(L, needle):
    for l in L:
        if needle in l:
            return l
    raise SystemExit("line not found: " + needle)


def pure_corpus():
    rnd = random.Random(146)
    FA = "/Game/_STALKER2/Animations/CharacterHead/"
    rows = []
    segs = ["General", "Zalissya", "Bark", "Voice", "VoiceA", "VO_X", "EN", "UA", "Combat", "Q01", "X", "VoiceZ", "Voice1", "Voice_"]
    names = ["AS_FAC_EN_VoiceA_123_abc", "AS_FAC_EN_123", "AS_FAC_UA_Voice1_x", "AS_FAC_en_x", "AS_FAC__x", "AS_FAC_EN_", "AS_FAC_EN_VoiceA_", "AS_FAC_ENUA_VoiceB_q.r",
             "AS_FAC_EN_Voiceab_1", "AS_FAC_EN_VoiceAA_1", "BS_FAC_EN_1", "AS_FAC_EN_VoiceA__", "AS_FAC_EN_Voice9_k"]
    for _ in range(600):
        p = rnd.choice([FA, FA, FA, "/Game/Other/", "/Game/_STALKER2/Animations/CharacterHead", ""])
        p += "/".join(rnd.choice(segs) for _ in range(rnd.randint(0, 4)))
        if rnd.random() < 0.9:
            p += "/" + rnd.choice(names)
        if rnd.random() < 0.5:
            p += "." + rnd.choice(names)
        rows.append(("wvrule", p))
    chars = "abAZ09_/.-,;#\" `~"
    for _ in range(300):
        s = "".join(rnd.choice(chars) for _ in range(rnd.choice([0, 1, 5, 20, 159, 160, 161, 199, 200, 201])))
        rows.append(("wssafe", s))
        rows.append(("wvsafe", s))
    for _ in range(300):
        parts = []
        for _ in range(rnd.randint(0, 5)):
            sq = str(rnd.randint(0, 999))
            parts.append(rnd.choice([
                sq + ",Bandit_" + str(rnd.randint(1, 99)) + ",s,SK_AK74," + str(rnd.randint(0, 9)),
                sq + ",X,v,Voiceover/A/VO_1," + str(rnd.randint(40, 95)),
                sq + ",,s,a,1", "x" + sq + ",a,s,b,2", sq + ",a,q,b,2", sq + ",a;b,s,c,3", sq + ",a,s,,4", sq + ",a,s,b,", "1,2,s,3,4,5"]))
        rows.append(("wsparse", rnd.choice([";", ";;", ",", ""]).join(parts)))
    for s in ["/Game/a/b.c", "/Game/a.b/c", "/Game/a/b", "AkAudioEvent /Game/a/b.c", " x", "Cls /p.q", "a.b.c", "/", "/Game/a/b.c.d", "noslash.x"]:
        rows.append(("strip", s))
    return rows


def lua_pure(lua, L, rows):
    G = lua.globals()
    f_item = lua.eval("function(it, p) local u, pd, nm, x, y, z, yw, vx, vy, st = it:match(p) if not u then return 'nil' end "
                      "return table.concat({tonumber(u), tonumber(pd), nm, string.format('%.17g', tonumber(x)), string.format('%.17g', tonumber(y)), string.format('%.17g', tonumber(z)), "
                      "string.format('%.17g', tonumber(yw)), string.format('%.17g', tonumber(vx)), string.format('%.17g', tonumber(vy)), tonumber(st) or 0}, ',') end")
    f_rule = lua.eval("function(p) local a, b, c = S2L.wvRule(p) if not a then return 'nil' end return a .. '|' .. b .. '|' .. c end")
    f_parse = lua.eval("function(s) local r = {} for _, e in ipairs(S2L.wsParse(s)) do r[#r + 1] = string.format('%.17g', e.sq) .. ',' .. e.nm .. ',' .. e.k .. ',' .. e.pl .. ',' .. string.format('%.17g', e.n) .. ';' end return table.concat(r) end")
    f_strip = lua.eval("function(p) local a = (p:gsub('%.[^/]*$', '')) local b = (p:gsub('^%S+ ', '')) local c = p:find('/[^/]*%.[^/]*$') and '1' or '0' local d = p:match('([^/]+)$') or '' return a .. '|' .. b .. '|' .. c .. '|' .. d end")
    out = []
    for kind, a in rows:
        if kind == "wvrule":
            r = f_rule(a)
        elif kind == "wssafe":
            r = "1" if G.S2L.wsSafe(a) else "0"
        elif kind == "wvsafe":
            r = "1" if G.S2L.wvSafe(a) else "0"
        elif kind == "wsparse":
            r = f_parse(a)
        elif kind == "strip":
            r = f_strip(a)
        out.append("%s\t%s\t%s" % (kind, hx(a), hx(r)))
    return out


def wsnorm(x):
    if not x.startswith("out #WS:"):
        return x
    it = [e.split(",", 1) for e in x[8:].split(";") if e]
    return (sorted(int(q) for q, _ in it), sorted(r for _, r in it))


def main():
    ok = True
    lua, L = build_lua()
    rows = pure_corpus()
    pin = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "pure_in.tsv")
    with open(pin, "w", encoding="utf-8", newline="\n") as f:
        for k, a in rows:
            f.write("%s\t%s\n" % (k, hx(a)))
    want = lua_pure(lua, L, rows)
    got = subprocess.run([EXE, "pure", pin], capture_output=True, text=True).stdout.split("\n")
    got = [g for g in got if g]
    bad = 0
    for w, g in zip(want, got):
        if w != g:
            bad += 1
            if bad <= 15:
                k, a, r = w.split("\t")
                print("PURE DIFF %s in=%r lua=%r cpp=%r" % (k, unhx(a), unhx(r), unhx(g.split("\t")[2]) if g.count("\t") == 2 else g))
    if len(want) != len(got):
        print("PURE count lua=%d cpp=%d" % (len(want), len(got)))
        bad += 1
    print("pure: %d cases, %d diffs" % (len(want), bad))
    ok = ok and bad == 0
    for sc in sorted(glob.glob(os.path.join(ROOT, "scen", "*.txt"))):
        lua2, _ = build_lua()
        text = open(sc, encoding="utf-8").read()
        w = lua2.globals().RUN(text).replace("S2CORE", "NOLA").replace("S2Coop_", "Nola_")
        g = subprocess.run([EXE, "scen", sc], capture_output=True, text=True).stdout
        wl, gl = w.rstrip("\n").split("\n"), g.rstrip("\n").split("\n")
        nd = 0
        for i in range(max(len(wl), len(gl))):
            a = wl[i] if i < len(wl) else "<none>"
            b = gl[i] if i < len(gl) else "<none>"
            if wsnorm(a) != wsnorm(b):
                nd += 1
                if nd <= 12:
                    print("SCEN %s line %d\n  lua: %s\n  cpp: %s" % (os.path.basename(sc), i + 1, a, b))
        steps = sum(1 for x in wl if x.startswith("== "))
        ev = sum(1 for x in wl if not x.startswith("== "))
        print("scen %s: %d steps, %d events, %d diffs" % (os.path.basename(sc), steps, ev, nd))
        if "-v" in sys.argv:
            print(w)
        ok = ok and nd == 0
    print("lua_ref: " + ("OK" if ok else "FAIL"))
    return 0 if ok else 1


sys.exit(main())
