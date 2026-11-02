local FL, S2, M, S2L, ST = {}, {io = {}, state = {}}, {}, {}, {}
local api = {BR = "B:\\", ROLE = "host"}
local OUT = {}
local CLOCK_MS = 0
local W = {view_on = false, vis = 0, hint = 0, npcs = {}, uses = {}, ph = {}, beds = {}, peer = nil, interact = nil, cm = nil}
local MEM = {}
local REAL_IO, REAL_DOFILE = io, dofile

local function emit(s) OUT[#OUT + 1] = s end
local function log(s) emit(string.format("L %d %s", CLOCK_MS, s)) end

os.clock = function() return CLOCK_MS / 1000 end
os.time = function() return 1700000000 + math.floor(CLOCK_MS / 1000) end
os.rename = function(a, b) MEM[b] = MEM[a]; MEM[a] = nil; return true end
os.remove = function(a) MEM[a] = nil; return true end

local function memreader(text)
    local f = {}
    function f.read(self, fmt) return text end
    function f.lines(self) return text:gmatch("([^\n]*)\n?") end
    function f.close(self) end
    return f
end
local function memwriter(path)
    local buf, f = {}, {}
    function f.write(self, s) buf[#buf + 1] = s end
    function f.close(self) MEM[path] = table.concat(buf) end
    return f
end
io = setmetatable({open = function(path, mode)
    if path:find("s2_blk_ledger", 1, true) then
        if mode == "w" then return memwriter(path) end
        if MEM[path] == nil then return nil end
        return memreader(MEM[path])
    end
    return REAL_IO.open(BRIDGE .. path:match("[^\\]+$"), mode)
end}, {__index = REAL_IO})
dofile = function(p) return REAL_DOFILE(BRIDGE .. p:match("[^\\]+$")) end

local function hexToGuid(h)
    local t = {}
    for i, k in ipairs({"A", "B", "C", "D"}) do
        local v = tonumber(h:sub(i * 8 - 7, i * 8), 16) or 0
        if v >= 0x80000000 then v = v - 0x100000000 end
        t[k] = v
    end
    return t
end

local function sortedKeys(t)
    local k = {}
    for key in pairs(t) do k[#k + 1] = key end
    table.sort(k)
    return k
end

local function mkNpc(d)
    local o = {id = d.id, addr = d.addr, x = d.x, y = d.y, z = d.z, alive = d.alive, own = d.own, guid = 0}
    function o.IsValid(self) return true end
    function o.GetAddress(self) return self.addr end
    function o.IsAlive(self) return self.alive end
    function o.K2_GetActorLocation(self) return {X = self.x, Y = self.y, Z = self.z} end
    function o.GetFName(self) local n = "Npc_" .. self.id; return {ToString = function() return n end} end
    if d.compok then
        o.NPCComponent = {bIsInteractionActive = d.active}
        function o.NPCComponent.IsValid(self) return true end
        function o.NPCComponent.SetInteractionActive(self, b)
            emit("A npc " .. o.id .. " active " .. (b and "1" or "0"))
            self.bIsInteractionActive = b
        end
    end
    return o
end

local function mkUse(d)
    local o = {id = d.id, x = d.x, y = d.y, z = d.z, Guid = hexToGuid(d.hex), comps = {}}
    function o.IsValid(self) return true end
    function o.K2_GetActorLocation(self) return {X = self.x, Y = self.y, Z = self.z} end
    function o.GetFullName(self) return "InteractableObject " .. self.id end
    for _, cd in ipairs(d.comps) do
        local c = {name = cd[1], bIsInteractionActive = cd[2]}
        function c.IsValid(self) return true end
        function c.GetOwner(self) return o end
        function c.GetFName(self) local n = self.name; return {ToString = function() return n end} end
        function c.SetInteractionActive(self, b)
            emit("A use " .. o.id .. " " .. self.name .. " active " .. (b and "1" or "0"))
            self.bIsInteractionActive = b
        end
        o.comps[#o.comps + 1] = c
    end
    return o
end

local VIEW = {}
function VIEW.IsValid(self) return true end
function VIEW.IsInViewport(self) return true end
function VIEW.IsVisible(self) return true end
VIEW.AnswerContainer = {}
function VIEW.AnswerContainer.GetVisibility(self) return W.vis end
function VIEW.AnswerContainer.SetVisibility(self, v) emit("A vis " .. v); W.vis = v end
VIEW.ButtonHintTrade = {}
function VIEW.ButtonHintTrade.GetVisibility(self) return W.hint end
function VIEW.UIDialogClose(self) emit("A close") end
function VIEW.UIDialogOpenTrade(self) emit("A open Trade") end
function VIEW.UIDialogOpenUpgrade(self) emit("A open Upgrade") end

local CM = {}
function CM.IsValid(self) return true end
function CM.GetGUID(self, o) return o.guid or 0 end
function CM.GetActorByGuid(self, g)
    local h = string.format("%08X%08X%08X%08X", g.A & 0xFFFFFFFF, g.B & 0xFFFFFFFF, g.C & 0xFFFFFFFF, g.D & 0xFFFFFFFF)
    local id = W.ph[h]
    if not id then return nil end
    return W.npcs[id]
end

function FindAllOf(cls)
    local r = {}
    if cls == "BP_AI_Human_C" then
        for _, k in ipairs(sortedKeys(W.npcs)) do r[#r + 1] = W.npcs[k] end
    elseif cls == "InteractableObject" then
        for _, k in ipairs(sortedKeys(W.uses)) do r[#r + 1] = W.uses[k] end
    elseif cls == "InteractionComponent" then
        for _, k in ipairs(sortedKeys(W.uses)) do for _, c in ipairs(W.uses[k].comps) do r[#r + 1] = c end end
    elseif cls == "BedView" then
        for _, b in ipairs(W.beds) do r[#r + 1] = b end
    elseif cls == "W_DialogueView_C" then
        if W.view_on then r[1] = VIEW end
    end
    return r
end

function StaticFindObject(p)
    if p == "/Script/Stalker2.Default__CppMediator" then return CM end
    return nil
end

local PAWN = {addr = 1000, x = 0, y = 0, z = 0}
function PAWN.IsValid(self) return true end
function PAWN.GetAddress(self) return self.addr end
function PAWN.K2_GetActorLocation(self) return {X = self.x, Y = self.y, Z = self.z} end
function PAWN.GetInteractedActor(self)
    if not W.interact then return nil end
    local q = W.interact
    return {IsValid = function() return true end, K2_GetActorLocation = function() return {X = q.x, Y = q.y, Z = q.z} end}
end
S2.io.pawn = PAWN

local function x1part(tag)
    if tag == "LK" then return W.peer end
    return nil
end

local DM = {g = {}, m = {}}
function DM.m.view(vc, G) if W.view_on then return VIEW end end
local function dmG() return DM.g end
