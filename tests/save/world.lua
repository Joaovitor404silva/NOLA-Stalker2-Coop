local W = {t = 0, pw = 0, inv = {}, cash = 0, lpos = {X = 0, Y = 0, Z = 0}, rpos = nil, parts = {}, stamp = "STEAM=1:1;NULL=-", savedir = true,
    saveOk = true, loadOk = true, createOk = true, pullOk = true, kh = 0, qtab = nil, mm = 1,
    sx = {state = "none", id = "", money = 0, items = "", qs = "", hand = "-", inSt = "none", inD = "-", instOk = false, instG = "", instKh = -1, instCid = "",
        snapOk = true, snapId = "SX1", snapN = 0, snapMoney = 0, snapHand = "-", snapReused = false}}
local OUT = {}
local function emit(s) OUT[#OUT + 1] = s end
local function stub(s) emit("STUB " .. s) end
function log(s) emit("LOG " .. s) end
ROOT = ROOT or ".\\"
local CLOCK = os.clock
os.clock = function() return W.t end
os.date = function() return "DATE" end
FL = FL or {}
api = {ROLE = "client", BR = ROOT .. "B\\"}
local pawns = {}
local function pawnOf(addr)
    if addr == 0 then return nil end
    local p = pawns[addr]
    if p then return p end
    local cm = setmetatable({}, {__index = function(_, k) if k == "MovementMode" then return W.mm end end})
    function cm:SetMovementMode(m, c) stub("setMovementMode " .. string.format("%d", m)); W.mm = m end
    p = {addr = addr, CharacterMovement = cm}
    function p:GetAddress() return self.addr end
    function p:GetHP() return 100 end
    function p:GetMoney() return W.cash end
    function p:ModifyMoney(d) stub("modifyMoney " .. string.format("%d", d)); W.cash = W.cash + d end
    function p:K2_GetActorLocation() return {X = W.lpos.X, Y = W.lpos.Y, Z = W.lpos.Z} end
    pawns[addr] = p
    return p
end
S2 = {io = {}}
api.UEHelpers = {GetPlayerController = function() return {Pawn = pawnOf(W.pw)} end}
function valid(o) return o ~= nil end
function x1part(tag) return W.parts[tag] end
local MED = {}
function MED:GetItemsCountInInventory(pw, s) return W.inv[s] or 0 end
function StaticFindObject(p) if p == "/Script/Stalker2.Default__CppMediator" then return MED end end
local RK = {}
function RK:XTriggerForcedAutoSave() stub("forcedAutoSave"); if not W.saveOk then error("falhou", 0) end end
function RK:XCreateItemInInventoryByID(sid, a, n, f) stub("createItem " .. sid .. " " .. string.format("%d", n)); if not W.createOk then error("falhou", 0) end; W.inv[sid] = (W.inv[sid] or 0) + n end
local MP = {}
function MP:XLoadLastSave() stub("loadLastSave"); if not W.loadOk then error("falhou", 0) end end
function FindFirstOf(n)
    if n == "CustomConsoleManagerRK" then return RK end
    if n == "CustomConsoleManagerMP" then return MP end
    if n == "BP_Stalker2Character_C" then return pawnOf(W.pw) end
end
function s2n_save_info()
    if not W.savedir then return nil, "x" end
    return ROOT .. "saved\\", W.stamp, "STEAM", ""
end
function s2n_save_list() return PYLIST(ROOT .. "saved\\STEAM\\SaveGames\\Data") end
function s2n_sx_host(br, kh) stub("sxHost " .. string.format("%d", kh)); return "ok" end
function s2n_sx_snap(br, addr, sids)
    stub("sxSnap " .. string.format("%d", addr))
    local X = W.sx
    if not X.snapOk then return nil, "falhou" end
    return X.snapId, X.snapN, X.snapMoney, X.snapHand, X.snapReused
end
function s2n_sx_get(br) local X = W.sx; return X.state, X.id, X.money, X.items, X.qs, X.hand end
function s2n_sx_mark(br, id, st) stub("sxMark " .. id .. " " .. st); return true end
function s2n_sx_want(br, id) stub("sxWant " .. id); return true end
function s2n_sx_in(br, id) return W.sx.inSt, W.sx.inD end
function s2n_sx_install(br, id)
    stub("sxInstall " .. id)
    local X = W.sx
    if not X.instOk then return false, "recusado" end
    return true, X.instG, X.instKh, X.instCid
end
function s2n_sx_undo(br, g) stub("sxUndo " .. g); return true end
LOOT_SIDS = LOOT_SIDS or {}
local function questApply(kind, a, b, c, sid, live)
    stub("quest " .. tostring(kind) .. " " .. tostring(a) .. " " .. tostring(b) .. " " .. tostring(c) .. " " .. tostring(sid) .. " " .. tostring(live))
    return true
end
local function qlogLoad(Q) local t = {}; for i = 1, W.kh do t[i] = i end; Q.log = t end
M = {}
function M.rbDone(ST, why) stub("rbDone " .. why); ST.qs.rbWait = nil end
function M.itemRemove(ST, kind, sid, n)
    stub("itemRemove " .. sid .. " " .. string.format("%d", n))
    W.inv[sid] = (W.inv[sid] or 0) - n
    if W.inv[sid] <= 0 then W.inv[sid] = nil end
end
S2L = {}
function S2L.lrSet(ST, name, m, secs, why, hard) stub("lrSet " .. name .. " " .. why) end
function S2L.lrDrop(ST, name, why) stub("lrDrop " .. name .. " " .. why) end
function S2L.lrPull(ST, name, m, secs, why) stub("lrPull " .. name .. " " .. why); return W.pullOk end
function S2L.remoteIn(why) if W.rpos then return {x = W.rpos.X, y = W.rpos.Y, z = W.rpos.Z} end end
function S2L.qiTab() if W.qtab then return {S = W.qtab} end end
function S2L.ioPawn() return pawnOf(W.pw) end
local ST = {qs = {seq = 0, list = {}, seen = {}, got = 0, sent = 0, applied = 0, skipped = 0}, peer = {state = nil, live = true, sessions = 0},
    cv = {fz = nil, rc = false}, it = {run = {}}, em = {noSave = false}, life = {down = nil}}
ST.qi = setmetatable({}, {__newindex = function(t, k, v) if k == "based" then stub("qiReset") end end})
S2.state = ST
local function syncIo()
    S2.io.pawn = pawnOf(W.pw)
    S2.io.p = W.pw ~= 0 and {X = W.lpos.X, Y = W.lpos.Y, Z = W.lpos.Z} or nil
end
CORE_CHUNK
local function tick100()
    syncIo()
    joinTick(ST)
    questIn(ST)
    csTick(ST)
    S2L.n7Tick(ST)
    kitTick(ST)
end
local function unesc(s)
    return (s:gsub("\\(.)", function(c) if c == "n" then return "\n" elseif c == "t" then return "\t" elseif c == "r" then return "\r" elseif c == "s" then return " " else return c end end))
end
local function wfile(p, d) local f = assert(io.open(ROOT .. p:gsub("/", "\\"), "wb")); f:write(d); f:close() end
function RUN(lines)
    for _, line in ipairs(lines) do
        if line ~= "" and line:sub(1, 1) ~= "#" then
            emit("CMD " .. line)
            local c, r = line:match("^(%S+)%s?(.*)$")
            local a = {}
            for w in r:gmatch("%S+") do a[#a + 1] = w end
            if c == "role" then api.ROLE = a[1]
            elseif c == "flag" then FL[a[1]] = math.tointeger(tonumber(a[2])) or tonumber(a[2])
            elseif c == "t" then W.t = tonumber(a[1])
            elseif c == "pawn" then W.pw = math.tointeger(tonumber(a[1]))
            elseif c == "inv" then local n = math.tointeger(tonumber(a[2])); W.inv[a[1]] = n > 0 and n or nil
            elseif c == "invclear" then W.inv = {}
            elseif c == "money" then W.cash = tonumber(a[1])
            elseif c == "pos" then W.lpos = {X = tonumber(a[1]), Y = tonumber(a[2]), Z = tonumber(a[3])}
            elseif c == "rpos" then if a[1] == "none" then W.rpos = nil else W.rpos = {X = tonumber(a[1]), Y = tonumber(a[2]), Z = tonumber(a[3])} end
            elseif c == "part" then W.parts[a[1]] = unesc(r:match("^%S+%s?(.*)$"))
            elseif c == "nopart" then W.parts[a[1]] = nil
            elseif c == "peer" then ST.peer.state = (a[1] ~= "none") and a[1] or nil; ST.peer.sessions = math.tointeger(tonumber(a[2])); ST.peer.live = a[3] ~= "0"
            elseif c == "stamp" then W.stamp = (r == "none") and "" or r
            elseif c == "savedir" then W.savedir = a[1] ~= "0"
            elseif c == "file" then wfile(a[1], unesc(r:match("^%S+%s?(.*)$")))
            elseif c == "rm" then os.remove(ROOT .. a[1]:gsub("/", "\\"))
            elseif c == "saveok" then W.saveOk = a[1] ~= "0"
            elseif c == "loadok" then W.loadOk = a[1] ~= "0"
            elseif c == "createok" then W.createOk = a[1] ~= "0"
            elseif c == "pullok" then W.pullOk = a[1] ~= "0"
            elseif c == "kh" then W.kh = math.tointeger(tonumber(a[1]))
            elseif c == "gen" then ST.qs.gen = (a[1] ~= "none") and math.tointeger(tonumber(a[1])) or nil
            elseif c == "qtab" then if a[1] == "none" then W.qtab = nil else W.qtab = {}; for s in a[1]:gmatch("[^,]+") do W.qtab[s] = true end end
            elseif c == "busy" then ST.it.run = (a[1] ~= "0") and {1} or {}
            elseif c == "frozen" then ST.cv.fz = (a[1] ~= "0") and {} or nil
            elseif c == "rcine" then ST.cv.rc = a[1] ~= "0"
            elseif c == "downed" then ST.life.down = (a[1] ~= "0") and 1 or nil
            elseif c == "emnosave" then ST.em.noSave = a[1] ~= "0"
            elseif c == "mm" then W.mm = math.tointeger(tonumber(a[1]))
            elseif c == "rbaddr" then ST.qs.rbAddr = (a[1] ~= "0") and 1 or nil
            elseif c == "rbcheck" then ST.qs.rbCheckAt = (a[1] ~= "0") and 1 or nil
            elseif c == "rbdone" then ST.qs.rbWait = nil
            elseif c == "qlok" then ST.qs.qlOk = (a[1] ~= "0") or nil
            elseif c == "sxget" then
                local X = W.sx
                X.state = a[1]; X.id = a[2] == "-" and "" or a[2]; X.money = tonumber(a[3]); X.items = a[4] == "-" and "" or a[4]; X.qs = a[5] == "-" and "" or a[5]
            elseif c == "sxin" then W.sx.inSt = a[1]; W.sx.inD = a[2]
            elseif c == "sxinstall" then local X = W.sx; X.instOk = a[1] ~= "0"; X.instG = a[2]; X.instKh = math.tointeger(tonumber(a[3])); X.instCid = a[4]
            elseif c == "sxsnap" then
                local X = W.sx
                X.snapOk = a[1] ~= "0"; X.snapId = a[2]; X.snapN = math.tointeger(tonumber(a[3])); X.snapMoney = tonumber(a[4]); X.snapHand = a[5]; X.snapReused = a[6] ~= "0"
            elseif c == "rw" then emit("RET " .. tostring(S2L.n7Rw(ST, a[1])))
            elseif c == "out" then emit("OUT N=" .. csOut(ST) .. " B=" .. S2L.n7B(ST))
            elseif c == "tick" then tick100()
            elseif c == "ticks" then
                local n, dt = math.tointeger(tonumber(a[1])), tonumber(a[2])
                for _ = 1, n do tick100(); W.t = W.t + dt end
            else error("unknown command " .. c) end
        end
    end
    return table.concat(OUT, "\n") .. "\n"
end
