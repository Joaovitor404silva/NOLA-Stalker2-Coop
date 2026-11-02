local W = {t = 0, pw = 0, inv = {}, cash = 0, pos = {X = 0, Y = 0, Z = 0}, parts = {}, gs = nil, aa = true, aaFail = false, dk = true, rk = true, mp = true,
    loadOk = true, lead = nil, hostDlg = false, deny = false, settled = true, n7stage = "idle", dead = false, keyd = false, invOpen = false, hv = 0, inter = 0}
local OUT = {}
local function emit(s) OUT[#OUT + 1] = s end
local function stub(s) emit("STUB " .. s) end
function log(s) emit("LOG " .. s) end
ROOT = ROOT or ".\\"
os.clock = function() return W.t end
os.time = function() return 1700000000 end
math.random = function(a, b) return 4660 end
FL = FL or {}
api = {ROLE = "client", BR = ROOT .. "B\\"}
function api.lookPut(text) return "H" .. #text .. string.sub(text, 1, 4) end
local objs = {}
local function objOf(addr)
    if not addr or addr == 0 then return nil end
    local o = objs[addr]
    if o then return o end
    o = {addr = addr}
    function o:GetAddress() return self.addr end
    function o:IsValid() return true end
    function o:GetMoney() return W.cash end
    function o:ModifyMoney(d) stub("modifyMoney " .. string.format("%d", d)); W.cash = W.cash + d end
    function o:K2_GetActorLocation() return {X = W.pos.X, Y = W.pos.Y, Z = W.pos.Z} end
    function o:GetInteractedActor() return objOf(W.inter) end
    objs[addr] = o
    return o
end
local function invOf(addr) W.inv[addr] = W.inv[addr] or {}; return W.inv[addr] end
S2 = {io = {}}
api.UEHelpers = {GetPlayerController = function() return {Pawn = objOf(W.pw)} end}
function valid(o) return o ~= nil end
function x1part(tag) return W.parts[tag] end
remoteField = x1part
local MED = {}
function MED:GetItemsCountInInventory(o, s) return invOf(o.addr)[s] or 0 end
function StaticFindObject(p) if p == "/Script/Stalker2.Default__CppMediator" then return MED end end
local AA = {}
local function aaCall(fn, sid)
    stub("aa " .. fn .. " " .. sid)
    if W.aaFail then error("falhou", 0) end
    local q = sid:match("^S2QI_(.+)$")
    if q and fn == "XStartQuestNodeBySID" then invOf(W.pw)[q] = nil end
end
function AA:XEndQuestNodeBySID(s) aaCall("XEndQuestNodeBySID", s) end
function AA:XStartQuestNodeBySID(s) aaCall("XStartQuestNodeBySID", s) end
function AA:XExcludeQuestNodeBySID(s) aaCall("XExcludeQuestNodeBySID", s) end
local DK = {}
function DK:XDKStartQuest(a) stub("dk XDKStartQuest " .. a) end
function DK:XDKStartQuestStage(a, b) stub("dk XDKStartQuestStage " .. a .. " " .. b) end
function DK:XDKFinishQuest(a, w) stub("dk XDKFinishQuest " .. a .. " " .. tostring(w)) end
function DK:XDKFinishQuestStage(a, b, w) stub("dk XDKFinishQuestStage " .. a .. " " .. b .. " " .. tostring(w)) end
local RK = {}
function RK:XCreateItemInInventoryByID(sid, a, n, f)
    stub("createItem " .. sid .. " " .. string.format("%d", n))
    local inv = invOf(W.pw); inv[sid] = (inv[sid] or 0) + n
end
function RK:XSetGlobalBool(a, v) stub("rk XSetGlobalBool " .. a .. " " .. tostring(v)) end
local MP = {}
function MP:XLoadLastSave() stub("loadLastSave"); if not W.loadOk then error("falhou", 0) end end
local PC = {}
function PC:IsInputKeyDown(k) return W.keyd end
local INV = {}
function INV:IsVisible() return W.invOpen end
function INV:UIInventoryDropItem() stub("invCall UIInventoryDropItem") end
function INV:OnDropItemHoldTriggered() stub("invCall OnDropItemHoldTriggered") end
setmetatable(INV, {__index = function(_, k) if k == "HoveredItem" then return objOf(W.hv) end end})
function FindFirstOf(n)
    if n == "CustomConsoleManagerAA" then return W.aa and AA or nil end
    if n == "CustomConsoleManagerDK" then return W.dk and DK or nil end
    if n == "CustomConsoleManagerRK" then return W.rk and RK or nil end
    if n == "CustomConsoleManagerMP" then return W.mp and MP or nil end
    if n == "BP_Stalker2Character_C" then return objOf(W.pw) end
    if n == "PlayerController" then return PC end
    if n == "InventoryNew" then return INV end
end
Key = {D = "D"}
function FName(s) return s end
function RegisterKeyBind(k, fn) W.keyFn = fn end
function cjG() return nil end
CJ = {m = {}, st = {}}
function dmirLet() return false end
function takenFile() return api.BR .. "taken.txt" end
function mineFile() return api.BR .. "taken_mine.txt" end
function takenLoad(ST) stub("takenLoad") end
function qApplied(Q) Q.ap = Q.ap or {}; return Q.ap end
LOOT_SIDS = LOOT_SIDS or {}
M = {}
function M.cpFlush(ST, why) stub("cpFlush " .. why) end
S2L = {}
function S2L.ldMine(ST) if W.lead == "dlg" then return {why = "dlg"} elseif W.lead == "mine" then return {why = "trig"} end return nil end
function S2L.chTrigDeny(ST, sid, seq) return W.deny end
function S2L.mqSid(sid) return false end
function S2L.mqSeen(ST, t) end
function S2L.ldTrig(ST, r) stub("ldTrig " .. r.sid) end
function S2L.qgDead(ST, Q, pw, now, rebase) return W.dead end
function S2L.qfArm(ST, why) stub("qfArm " .. why) end
function S2L.n7St(ST) return {stage = W.n7stage} end
function S2L.n7Follow(ST, N, now) stub("n7Follow") end
function S2L.n7Rw(ST, q) return false end
function S2L.n7B(ST) return "" end
function S2L.qlSettled(Q) return W.settled end
local ST = {qs = {seq = 0, list = {}, got = 0, sent = 0, seen = {}, rx = 0}, peer = {state = nil, sessions = 0, loopback = false}, ld = {}}
S2.state = ST
CORE_CHUNK
local function b164(ST)
B164_LINE
end
local function tick100()
    S2.io.pawn = objOf(W.pw)
    pcall(b164, ST)
    local ok, e = pcall(rbTick, ST)
    if not ok then ST.b6err = (ST.b6err or 0) + 1; if ST.b6err <= 5 then log("S2CORE rbTick erro: " .. tostring(e)) end end
    ok, e = pcall(rewardTick, ST)
    if not ok then ST.c22err = (ST.c22err or 0) + 1; if ST.c22err <= 5 then log("S2CORE rewardTick erro " .. tostring(e)) end end
    pcall(trigTick, ST)
    pcall(S2L.qpTick, ST)
    pcall(S2L.qiTick, ST)
    pcall(S2L.qdrTick, ST)
end
local beat = 0
local function tick1000()
    S2.io.pawn = objOf(W.pw)
    beat = beat + 1
    if beat % 5 == 0 then pcall(questRollback, ST) end
    pcall(M.qlogFlush, ST)
    pcall(S2L.qdTick, ST)
end
local function outs()
    local r = ""
    for _, f in ipairs({trigOut, rewardOut, questOut, catchupOut, rbOut, S2L.qiOut, S2L.qpOut}) do
        local ok, s = pcall(f, ST)
        r = r .. (ok and s or "")
    end
    return r
end
local function unesc(s)
    return (s:gsub("\\(.)", function(c) if c == "n" then return "\n" elseif c == "t" then return "\t" elseif c == "r" then return "\r" elseif c == "s" then return " " else return c end end))
end
local function wfile(p, d) local f = assert(io.open(api.BR .. p, "wb")); f:write(d); f:close() end
local function num(s) local v = tonumber(s); return math.tointeger(v) or v end
function RUN(lines)
    for _, line in ipairs(lines) do
        if line ~= "" and line:sub(1, 1) ~= "#" then
            emit("CMD " .. line)
            local c, r = line:match("^(%S+)%s?(.*)$")
            local a = {}
            for w in r:gmatch("%S+") do a[#a + 1] = w end
            if c == "role" then api.ROLE = a[1]
            elseif c == "flag" then FL[a[1]] = num(a[2])
            elseif c == "flags" then FL[a[1]] = a[2]
            elseif c == "noflag" then FL[a[1]] = nil
            elseif c == "t" then W.t = tonumber(a[1])
            elseif c == "pawn" then W.pw = num(a[1])
            elseif c == "inv" then local n = num(a[2]); invOf(W.pw)[a[1]] = n > 0 and n or nil
            elseif c == "oinv" then local n = num(a[3]); invOf(num(a[1]))[a[2]] = n > 0 and n or nil
            elseif c == "money" then W.cash = tonumber(a[1])
            elseif c == "pos" then W.pos = {X = tonumber(a[1]), Y = tonumber(a[2]), Z = tonumber(a[3])}
            elseif c == "part" then W.parts[a[1]] = unesc(r:match("^%S+%s?(.*)$"))
            elseif c == "nopart" then W.parts[a[1]] = nil
            elseif c == "peer" then ST.peer.state = (a[1] ~= "none") and a[1] or nil; ST.peer.sessions = num(a[2]); ST.peer.loopback = a[3] == "1"
            elseif c == "gs" then S2_gameSecs = (a[1] ~= "none") and tonumber(a[1]) or nil
            elseif c == "aa" then W.aa = a[1] ~= "0"
            elseif c == "aafail" then W.aaFail = a[1] ~= "0"
            elseif c == "dk" then W.dk = a[1] ~= "0"
            elseif c == "rk" then W.rk = a[1] ~= "0"
            elseif c == "loadok" then W.loadOk = a[1] ~= "0"
            elseif c == "lead" then W.lead = (a[1] ~= "none") and a[1] or nil
            elseif c == "hostdlg" then W.hostDlg = a[1] ~= "0"; ST.ld.dlg = W.hostDlg or nil
            elseif c == "deny" then W.deny = a[1] ~= "0"
            elseif c == "settled" then W.settled = a[1] ~= "0"
            elseif c == "n7stage" then W.n7stage = a[1]
            elseif c == "dead" then W.dead = a[1] ~= "0"
            elseif c == "keyd" then W.keyd = a[1] ~= "0"
            elseif c == "invopen" then W.invOpen = a[1] ~= "0"
            elseif c == "hover" then W.hv = num(a[1])
            elseif c == "inter" then W.inter = num(a[1])
            elseif c == "press" then ST.qdrKeyAt = os.clock()
            elseif c == "file" then wfile(a[1], unesc(r:match("^%S+%s?(.*)$")))
            elseif c == "s2q" then pcall(M.questCmd, ST, line, nil)
            elseif c == "apply" then
                local ok, r1, r2 = pcall(questApply, parseEv(a[1]))
                emit("RET " .. tostring(ok) .. " " .. tostring(r1) .. " " .. tostring(r2))
            elseif c == "ishost" then emit("RET " .. tostring(S2L.qiHostKeep(ST, a[1], objOf(W.pw))))
            elseif c == "corpse" then ST.loot = ST.loot or {}; emit("RET " .. tostring(S2L.qiCorpse(ST, objOf(num(a[1])), a[2])))
            elseif c == "fin" then M.questFinished(ST, a[1])
            elseif c == "out" then emit("OUT " .. outs())
            elseif c == "tick" then tick100()
            elseif c == "ticks" then
                local n, dt = num(a[1]), tonumber(a[2])
                for _ = 1, n do tick100(); W.t = W.t + dt end
            elseif c == "tick1k" then tick1000()
            elseif c == "run" then
                local n = num(a[1])
                for _ = 1, n do
                    for _ = 1, 10 do tick100(); W.t = W.t + 0.1 end
                    tick1000()
                    emit("OUT " .. outs())
                end
            else error("unknown command " .. c) end
        end
    end
    return table.concat(OUT, "\n") .. "\n"
end
