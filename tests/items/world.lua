local W = {t = 0, npcs = {}, order = {}, byName = {}, vis = false, ia = nil, key = false, parts = {}, aa = true, rk = true, rnd = 0}
local OUT = {}
local function emit(s) OUT[#OUT + 1] = s end
local function stub(s) emit("STUB " .. s) end
function log(s) emit("LOG " .. s) end
ROOT = ROOT or ".\\"
os.clock = function() return W.t end
math.random = function(a, b) W.rnd = W.rnd + 1; return 1000 + W.rnd end
FL = FL or {}
api = {ROLE = "host", BR = ROOT}
BRIDGE_DIR = ROOT
M = {}
S2L = {}
S2 = {io = {}, state = nil}
M.isOwnBody = function() return false end
S2L.clIs = function() return false end
S2L.qiCorpse = function() return false end
S2L.qiHostKeep = function() return false end
function valid(o) return o ~= nil and o:IsValid() end
function x1part(tag) return W.parts[tag] end
function FName(s) return s end
local function actor(name, cls, x, y, z)
    local a = {name = name, cls = cls, pos = {X = x, Y = y, Z = z}, alive = true, inv = {}, money = 0, live = true}
    function a:IsValid() return self.live end
    function a:GetFullName() return self.cls .. " /Game/W/World.World:PersistentLevel." .. self.name end
    function a:GetFName() local n = self.name; return {ToString = function() return n end} end
    function a:K2_GetActorLocation() return {X = self.pos.X, Y = self.pos.Y, Z = self.pos.Z} end
    function a:IsAlive() return self.alive end
    function a:GetMoney() return self.money end
    function a:GetInteractedActor() return W.ia end
    function a:CloseLootMenu() stub("close") end
    return a
end
local ME = actor("BP_Stalker2Character_C_0", "BP_Stalker2Character_C", 0, 0, 0)
W.order[1] = ME
local PC = {}
function PC:IsValid() return true end
function PC:GetInteractedActor() return W.ia end
function PC:CloseLootMenu() stub("close") end
function PC:IsInputKeyDown(k) return W.key end
local WID = {}
function WID:IsValid() return true end
function WID:IsVisible() return W.vis end
local MED = {}
function MED:GetItemsCountInInventory(o, s) return o.inv[s] or 0 end
local RK = {}
function RK:IsValid() return true end
function RK:XCreateItemInInventoryByID(sid, uid, n, f)
    stub("create " .. sid .. " " .. string.format("%d", uid) .. " " .. string.format("%d", n))
    if uid == 0 then ME.inv[sid] = (ME.inv[sid] or 0) + n end
end
local AA = {}
function AA:IsValid() return true end
function AA:XStartQuestNodeBySID(sid) stub("node " .. sid) end
function StaticFindObject(p) if p == "/Script/Stalker2.Default__CppMediator" then return MED end end
function FindFirstOf(n)
    if n == "InventoryNew" then return WID end
    if n == "PlayerController" then return PC end
    if n == "CustomConsoleManagerRK" then return W.rk and RK or nil end
    if n == "CustomConsoleManagerAA" then return W.aa and AA or nil end
end
function FindAllOf(n)
    if n ~= "Pawn" then return {} end
    local r = {}
    for _, o in ipairs(W.order) do if o.cls ~= "CONT" and o.kind ~= "cont" then r[#r + 1] = o end end
    return r
end
local ST = {peer = {}}
S2.state = ST
S2.io.pawn = ME

CORE_CHUNK
M.LS.open = function() end
M.LS.hold = function() return false end

local function num(v) return math.tointeger(tonumber(v)) or tonumber(v) end
local function runTick()
    for _, nf in ipairs({{"itIn", itIn}, {"itRun", itRun}, {"lockTick", lockTick}, {"lootTick", lootTick}, {"ptakeTick", ptakeTick}}) do
        local ok, e = pcall(nf[2], ST)
        if not ok then emit("ERR " .. nf[1] .. " " .. tostring(e)) end
    end
end
local function outLine()
    local okL, ls = pcall(lootOut, ST)
    local okU, us = pcall(itOut, ST)
    emit("OUT #K:" .. ((ST.lock and ST.lock.mine) or "") .. "|" .. (okL and ls or "ERR") .. "|" .. (okU and us or "ERR"))
end
function RUN(lines)
    for _, ln in ipairs(lines) do
        local w = {}
        for x in ln:gmatch("%S+") do w[#w + 1] = x end
        local c = w[1]
        if c == nil or c:sub(1, 1) == "#" then
        elseif c == "role" then api.ROLE = w[2]
        elseif c == "flag" then FL[w[2]] = num(w[3]) or w[3]
        elseif c == "t" then W.t = tonumber(w[2])
        elseif c == "me" then ME.pos = {X = num(w[2]), Y = num(w[3]), Z = num(w[4])}
        elseif c == "myinv" then ME.inv[w[2]] = num(w[3])
        elseif c == "money" then ME.money = num(w[2])
        elseif c == "npc" then
            local o = actor("BP_AI_Human_C_" .. w[2], "BP_AI_Human_C", num(w[3]), num(w[4]), num(w[5]))
            o.alive = w[6] == "1"
            W.order[#W.order + 1] = o; W.byName[o.name] = o
        elseif c == "npcinv" then W.byName["BP_AI_Human_C_" .. w[2]].inv[w[3]] = num(w[4])
        elseif c == "kill" then W.byName["BP_AI_Human_C_" .. w[2]].alive = false
        elseif c == "cont" then
            local o = actor(w[2] .. "_" .. w[3], w[2], num(w[4]), num(w[5]), num(w[6]))
            o.kind = "cont"
            W.byName[o.name] = o
        elseif c == "open" then W.ia = w[2] ~= "-" and W.byName[w[2]] or nil
        elseif c == "vis" then W.vis = w[2] == "1"
        elseif c == "key" then W.key = w[2] == "1"
        elseif c == "latch" then ST.lootLatch = W.t
        elseif c == "part" then W.parts[w[2]] = ln:match("^part%s+%S+%s+(.*)$") or ""
        elseif c == "nopart" then W.parts[w[2]] = nil
        elseif c == "aa" then W.aa = w[2] == "1"
        elseif c == "send" then local ok = M.itemSend(ST, w[2], w[3], w[4], w[5]); emit("RET " .. tostring(ok))
        elseif c == "remove" then local ok = M.itemRemove(ST, w[2], w[3], w[4], w[5]); emit("RET " .. tostring(ok))
        elseif c == "tick" then runTick()
        elseif c == "out" then outLine()
        else emit("BAD " .. ln) end
    end
    return table.concat(OUT, "\n") .. "\n"
end
