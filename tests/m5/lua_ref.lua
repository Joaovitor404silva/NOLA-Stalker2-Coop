local M = {}
local S2L = {}
local B73 = {}
local IA = {}
local W = {t = 0, objs = {}, ord = {}, remote = {}, loop = false, sessions = 1, level = nil, deny = {}, cdo = {}, assets = {}}
local TRACE = {}
local function rec(s) TRACE[#TRACE + 1] = s end
setmetatable(_G, {__index = function(_, k) rec("UNDEF " .. tostring(k)) end})
local os = {clock = function() return W.t end, time = os.time}
local FL = {}
local api = {ROLE = "host", BR = DATADIR, angDiff = function(a, b) local d = (a - b) % 360; if d > 180 then d = d - 360 end; return d end}
local ST = {peer = {state = "up", sessions = 1, loopback = false}, wg = {key = "k1", open = true}}
local S2 = {io = {pawn = nil}, errs = {n = 0}}
local function log(s) rec("LOG " .. s) end
local function valid(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v
end
local function x1part(tag) return W.remote[tag] end
local function remoteField(tag) return x1part(tag) end

local OM = {}
local function fmtarg(a)
    if a == nil then return "nil" end
    if type(a) == "table" and rawget(a, "d") then return a.d.id end
    return tostring(a)
end
local METHODS = {
    door = {ForceOpenDoor = true, ForceCloseDoor = true},
    inter = {SetEnabled = true, Toggle = true, StateChange = true, IsEnabled = true, SetBroken = true, PauseSkeletalComponent = true, BNDEVT = true},
    lamp = {SetEnabled = true, Toggle = true, StateChange = true, IsEnabled = true, SetBroken = true, PauseSkeletalComponent = true, LightOn = true, LightOff = true, BNDEVT = true},
    destr = {StartDestructActions = true, SetActorHiddenInGame = true, SetActorEnableCollision = true},
    anom = {ForceActivateOnce = true},
    comp = {SetInteractionActive = true},
    meshcomp = {SetStaticMesh = true, SetSimulatePhysics = true},
    light = {},
    leaf = {},
    mesh = {},
    other = {},
}
local function effect(d, k, args)
    if k == "ForceOpenDoor" then local lf = d.comps.FirstLeafMeshComponent; if lf then lf.d.yaw = lf.d.base + 90 end
    elseif k == "ForceCloseDoor" then local lf = d.comps.FirstLeafMeshComponent; if lf then lf.d.yaw = lf.d.base end
    elseif k == "SetEnabled" then d.p.bEnabled = args[1]
    elseif k == "Toggle" then d.p.bEnabled = not d.p.bEnabled
    elseif k == "LightOn" then if d.comps.MainLight then d.comps.MainLight.d.p.vis = true end
    elseif k == "LightOff" then if d.comps.MainLight then d.comps.MainLight.d.p.vis = false end
    elseif k == "SetBroken" then d.p.bBroken = args[1]
    elseif k == "SetActorHiddenInGame" then d.p.bHidden = args[1]
    elseif k == "SetStaticMesh" then d.mesh = args[1]
    elseif k == "SetInteractionActive" then d.p.bIsInteractionActive = args[1]
    elseif k:find("^BndEvt__") and k:find("InteractSignature") then d.p.bEnabled = not d.p.bEnabled
    end
end
OM.__index = function(o, k)
    local d = rawget(o, "d")
    if k == "d" then return d end
    if k == "IsValid" then return function() return not d.dead end end
    if d.dead then error("dead object " .. d.id) end
    if k == "GetFullName" then return function() if d.asset then return "Asset " .. d.id end return d.cls .. " /Game/Test." .. d.id end end
    if k == "GetFName" then return function() return {ToString = function() return d.fname or d.id end} end end
    if k == "GetClass" then
        return function()
            local function cobj(i)
                local nm = i == 0 and d.cls or d.anc[i]
                if not nm then return nil end
                return {IsValid = function() return true end, GetFName = function() return {ToString = function() return nm end} end,
                        GetSuperStruct = function() return cobj(i + 1) end}
            end
            return cobj(0)
        end
    end
    if k == "K2_GetActorLocation" and d.pos then return function() return {X = d.pos.X, Y = d.pos.Y, Z = d.pos.Z} end end
    if k == "GetActorScale3D" and d.scale then return function() return {X = d.scale[1], Y = d.scale[2], Z = d.scale[3]} end end
    if k == "K2_GetComponentRotation" and d.kind == "leaf" then return function() return {Pitch = 0, Yaw = d.yaw, Roll = 0} end end
    if k == "GetCurrentPhaseIndex" and d.phase ~= nil then return function() return d.phase end end
    if k == "GetOwner" and d.owner then return function() return d.owner end end
    if k == "IsVisible" and d.kind == "light" then return function() return d.p.vis == true end end
    if k == "GetAnimInstance" and d.anim then return function() return d.anim end end
    if k == "StaticMesh" and d.kind == "meshcomp" then return d.mesh end
    if d.fns and d.fns[k] then
        return function(self, ...)
            local parts = {}
            for i = 1, select("#", ...) do parts[#parts + 1] = fmtarg((select(i, ...))) end
            rec("CALL " .. d.id .. "." .. k .. "(" .. table.concat(parts, ",") .. ")")
            local e = d.fns[k]
            if e == "toggle" then d.p.bEnabled = not d.p.bEnabled
            elseif e == "on" then d.p.bEnabled = true
            elseif e == "off" then d.p.bEnabled = false end
        end
    end
    local m = METHODS[d.kind] or {}
    local isbnd = k:find("^BndEvt__") and m.BNDEVT
    if (m[k] or isbnd) and not (k == "IsEnabled" and false) then
        return function(self, ...)
            local args = {...}
            local parts = {}
            for i = 1, select("#", ...) do parts[#parts + 1] = fmtarg(args[i]) end
            rec("CALL " .. d.id .. "." .. k .. "(" .. table.concat(parts, ",") .. ")")
            if k == "IsEnabled" then return d.p.bEnabled end
            effect(d, k, args)
        end
    end
    if d.kind == "cdo" and k == "SpawnSystemAtLocation" then
        return function(self, a, b) rec("CALL " .. d.id .. "." .. k .. "(" .. fmtarg(b) .. ")") end
    end
    if d.kind == "cdo" and k == "PostEventAtLocation" then
        return function(self, a) rec("CALL " .. d.id .. "." .. k .. "(" .. fmtarg(a) .. ")") end
    end
    if k == "K2_GetActorRotation" and d.pos then return function() return {Pitch = 0, Yaw = 0, Roll = 0} end end
    if k == "GetAddress" then return function() return d.addr end end
    if d.p[k] ~= nil then return d.p[k] end
    if d.comps[k] ~= nil then return d.comps[k] end
    return nil
end
OM.__newindex = function(o, k, v)
    local d = rawget(o, "d")
    if d.kind == "anim" then rec("ANIM " .. k .. "=" .. tostring(v)); d.p[k] = v; return end
    d.p[k] = v
end

local function mk(id, cls, anc, kind, x, y, z)
    W.addr = (W.addr or 1000) + 1
    local d = {id = id, cls = cls, anc = anc, kind = kind, p = {}, comps = {}, dead = false, addr = W.addr, scale = {1, 1, 1}}
    if x then d.pos = {X = x, Y = y, Z = z} end
    local o = setmetatable({d = d}, OM)
    W.objs[id] = o
    return o
end
local function listed(o) W.ord[#W.ord + 1] = o end
local function unlist(o) for i, v in ipairs(W.ord) do if v == o then table.remove(W.ord, i); return end end end

local function FindAllOf(cls)
    local r = {}
    if cls == "World" then return nil end
    for _, o in ipairs(W.ord) do
        local d = o.d
        local hit = d.cls == cls
        for _, a in ipairs(d.anc) do if a == cls then hit = true end end
        if hit then r[#r + 1] = o end
    end
    if #r == 0 then return nil end
    return r
end
local function FindFirstOf(cls)
    if cls == "World" and W.level then
        return {GetFullName = function() return W.level end}
    end
    local t = FindAllOf(cls)
    return t and t[1] or nil
end
local function StaticFindObject(path)
    if path:find("^/Script/") then
        local id = path:match("[^%.]+$")
        W.cdo[path] = W.cdo[path] or mk(id, "CDO", {}, "cdo")
        return W.cdo[path]
    end
    if path:find("^/Game/") then
        if not W.assets[path] then
            local o = mk(path, "Asset", {}, "other")
            o.d.asset = true
            W.assets[path] = o
        end
        return W.assets[path]
    end
    return nil
end
local function LoadAsset(path) return nil end

local function asset(path)
    local a = StaticFindObject(path)
    if not valid(a) then pcall(function() a = LoadAsset(path) end) end
    if valid(a) then return a end
end

local FLAGS_DEFAULTS = load("return " .. FLAGS_TEXT)()
for k, v in pairs(FLAGS_DEFAULTS) do FL[k] = v end

--[[SEG1]]
--[[SEG2]]
--[[SEG3]]
--[[SEG4]]
--[[SEG5]]
--[[SEG6]]

function S2L.chUseDeny(ST_, hit, k)
    return W.deny[hit.d.id] == true
end


local function sorted_tag(s)
    if s == nil or s == "" then return nil end
    local tag, body = s:match("^(#%w+:)(.*)$")
    local items = {}
    for it in body:gmatch("[^|]+") do items[#items + 1] = it end
    table.sort(items)
    return tag .. table.concat(items, "|")
end

local function toks(line)
    local t = {}
    for w in line:gmatch("%S+") do t[#t + 1] = w end
    return t
end

local function runtick()
    local ok, e = pcall(doorTick, ST); if not ok then rec("ERR door " .. tostring(e)) end
    ok, e = pcall(dobjTick, ST); if not ok then rec("ERR dobj " .. tostring(e)) end
    ok, e = pcall(ienTick, ST); if not ok then rec("ERR ien " .. tostring(e)) end
    ok, e = pcall(B73.anomTick, ST); if not ok then rec("ERR anom " .. tostring(e)) end
    local outs = {
        select(2, pcall(doorOut, ST)), select(2, pcall(dobjOut, ST)), select(2, pcall(ienOut, ST)), select(2, pcall(B73.anomOut, ST)),
    }
    for _, s in ipairs(outs) do
        local n = sorted_tag(s)
        if n then rec("OUT " .. n) end
    end
end

local function run(path)
    for line in io.lines(path) do
        local t = toks(line)
        local c = t[1]
        if not c or c:sub(1, 1) == "#" then
        elseif c == "role" then api.ROLE = t[2]
        elseif c == "flag" then
            local v = tonumber(t[3]); if v == nil then v = t[3] end
            FL[t[2]] = v
        elseif c == "gate" then ST.wg = {key = t[2], open = t[3] == "1"}
        elseif c == "nogate" then ST.wg = nil
        elseif c == "level" then W.level = t[2] ~= "-" and t[2] or nil
        elseif c == "sessions" then ST.peer.sessions = tonumber(t[2])
        elseif c == "loopback" then ST.peer.loopback = t[2] == "1"
        elseif c == "time" then W.t = tonumber(t[2])
        elseif c == "remote" then W.remote[t[2]] = (t[3] ~= "-") and t[3] or nil
        elseif c == "pawn" then
            local o = W.objs["PAWN"] or mk("PAWN", "Pawn", {}, "other", 0, 0, 0)
            o.d.pos = {X = tonumber(t[2]), Y = tonumber(t[3]), Z = tonumber(t[4])}
            S2.io.pawn = o
        elseif c == "pawnid" then W.objs["PAWN"].d.addr = tonumber(t[2])
        elseif c == "nopawn" then S2.io.pawn = nil
        elseif c == "obj" then
            local anc = {}
            if t[4] ~= "-" then for a in t[4]:gmatch("[^,]+") do anc[#anc + 1] = a end end
            local o = mk(t[2], t[3], anc, t[5], tonumber(t[6]), tonumber(t[7]), tonumber(t[8]))
            if t[5] ~= "comp" and t[5] ~= "leaf" and t[5] ~= "mesh" and t[5] ~= "light" and t[5] ~= "meshcomp" and t[5] ~= "cdo" then listed(o) end
        elseif c == "scale" then W.objs[t[2]].d.scale = {tonumber(t[3]), tonumber(t[4]), tonumber(t[5])}
        elseif c == "yaw" then
            local o = W.objs[t[2]]
            local lf = o.d.comps.FirstLeafMeshComponent
            if not lf then
                lf = mk(t[2] .. "_leaf", "Leaf", {}, "leaf")
                lf.d.base = tonumber(t[3])
                o.d.comps.FirstLeafMeshComponent = lf
            end
            lf.d.yaw = tonumber(t[3])
        elseif c == "prop" then
            local v = t[4]
            if v == "1" then v = true elseif v == "0" then v = false else v = tonumber(v) end
            W.objs[t[2]].d.p[t[3]] = v
        elseif c == "phase" then W.objs[t[2]].d.phase = tonumber(t[3])
        elseif c == "mesh" then
            local o = W.objs[t[2]]
            if t[3] == "none" then o.d.comps.StaticMeshComponent = nil
            else
                local mc = o.d.comps.StaticMeshComponent
                if not mc then
                    mc = mk(t[2] .. "/StaticMeshComponent", "StaticMeshComponent", {}, "meshcomp")
                    o.d.comps.StaticMeshComponent = mc
                end
                if t[3] == "null" then mc.d.mesh = nil
                else
                    local m = mk("mesh_" .. t[3], "StaticMesh", {}, "mesh")
                    m.d.fname = t[3]
                    mc.d.mesh = m
                end
            end
        elseif c == "comp" then
            local own = W.objs[t[3]]
            local co = mk(t[2], "InteractionComponent", {}, "comp")
            co.d.owner = own
            co.d.fname = t[4]
            co.d.p.bIsInteractionActive = t[5] == "1"
            listed(co)
        elseif c == "compprop" then W.objs[t[2]].d.comps[t[3]] = W.objs[t[4]]
        elseif c == "mainlight" then
            local l = mk(t[2] .. "/MainLight", "Light", {}, "light")
            l.d.p.vis = t[3] == "1"
            W.objs[t[2]].d.comps.MainLight = l
        elseif c == "anim" then
            local a = mk(t[2] .. "/Anim", "Anim", {}, "anim")
            a.d.p.DoorState = 1
            W.objs[t[2]].d.anim = a
            W.objs[t[2]].d.comps[t[3]] = {GetAnimInstance = function() return a end}
            W.objs[t[2]].d.comps[t[3]] = setmetatable({d = {kind = "animhost", id = t[2] .. "/" .. t[3], p = {}, comps = {}, anc = {}, anim = a}}, OM)
            W.objs[t[2]].d.comps[t[3]].d.cls = "AnimHost"
            METHODS.animhost = {}
        elseif c == "move" then W.objs[t[2]].d.pos = {X = tonumber(t[3]), Y = tonumber(t[4]), Z = tonumber(t[5])}
        elseif c == "setyaw" then W.objs[t[2]].d.comps.FirstLeafMeshComponent.d.yaw = tonumber(t[3])
        elseif c == "destroy" then local o = W.objs[t[2]]; o.d.dead = true; unlist(o)
        elseif c == "kill" then W.objs[t[2]].d.dead = true
        elseif c == "deny" then W.deny[t[2]] = true
        elseif c == "fn" then local o = W.objs[t[2]]; o.d.fns = o.d.fns or {}; o.d.fns[t[3]] = t[4]
        elseif c == "tick" then W.ticks = (W.ticks or 0) + 1; rec("TICK " .. W.ticks); runtick()
        elseif c == "loop" then
            for _ = 1, tonumber(t[2]) do W.t = W.t + tonumber(t[3]); W.ticks = (W.ticks or 0) + 1; rec("TICK " .. W.ticks); runtick() end
        elseif c:find("^expect_") then
        elseif c == "mark" then rec("MARK " .. t[2])
        else error("scenario: " .. line) end
    end
end

return {run = run, trace = function() return TRACE end, flags = function() return FL end}
