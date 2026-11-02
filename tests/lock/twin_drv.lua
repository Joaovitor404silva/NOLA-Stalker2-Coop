M.isOwnBody = function(_, o) return o.own == true end

local function num(s)
    if s == "nil" then return nil end
    return tonumber(s) or s
end

local function words(l)
    local w = {}
    for t in l:gmatch("%S+") do w[#w + 1] = t end
    return w
end

local function b01(s) return s == "1" end

local tickn, last = 0, ""

local function step()
    tickn = tickn + 1
    local ok, e = pcall(function()
        S2L.vendTradeTick(ST)
        S2L.ldTick(ST)
        if tickn % 10 == 0 then
            S2L.chUseTick(ST)
            S2L.ldBlkTick(ST)
            S2L.lgTick(ST)
        end
    end)
    if not ok then emit("E " .. tostring(e)) end
    local o = S2L.ldOut(ST)
    if o ~= last then
        last = o
        emit("O " .. CLOCK_MS .. " " .. (o == "" and "-" or o))
    end
end


local cmds = {}
cmds.role = function(w) api.ROLE = w[2] end
cmds.flag = function(w) FL[w[2]] = num(w[3]) end
cmds.time = function(w) CLOCK_MS = tonumber(w[2]) end
cmds.run = function(w)
    local e = CLOCK_MS + tonumber(w[2])
    while CLOCK_MS < e do
        CLOCK_MS = CLOCK_MS + 100
        step()
    end
end
cmds.peer = function(w) W.peer = (w[2] ~= "-") and w[2] or nil end
cmds.pawn = function(w)
    PAWN.addr = tonumber(w[2]); PAWN.x = tonumber(w[3]); PAWN.y = tonumber(w[4]); PAWN.z = tonumber(w[5])
    S2.io.pawn = PAWN
end
cmds.nopawn = function(w) S2.io.pawn = nil end
cmds.view = function(w) W.view_on = (w[2] == "on") end
cmds.interact = function(w)
    if w[2] == "none" then W.interact = nil else W.interact = {x = tonumber(w[2]), y = tonumber(w[3]), z = tonumber(w[4])} end
end
cmds.hint = function(w) W.hint = tonumber(w[2]) end
cmds.vis = function(w) W.vis = tonumber(w[2]) end
cmds.npc = function(w)
    W.npcs[w[2]] = mkNpc({id = w[2], addr = tonumber(w[3]), x = tonumber(w[4]), y = tonumber(w[5]), z = tonumber(w[6]),
        alive = b01(w[7]), active = b01(w[8]), compok = b01(w[9]), own = b01(w[10])})
end
cmds.npcset = function(w)
    local n = W.npcs[w[2]]
    local v = b01(w[4])
    if w[3] == "alive" then n.alive = v elseif w[3] == "active" then n.NPCComponent.bIsInteractionActive = v end
end
cmds.guid = function(w) W.npcs[w[2]].guid = tonumber(w[3]) end
cmds.ph = function(w) W.ph[w[2]] = w[3] end
cmds.usable = function(w)
    local comps = {}
    for p in w[7]:gmatch("[^,]+") do
        local n, a = p:match("^(.-):(%d)$")
        comps[#comps + 1] = {n, a == "1"}
    end
    W.uses[w[2]] = mkUse({id = w[2], hex = w[3], x = tonumber(w[4]), y = tonumber(w[5]), z = tonumber(w[6]), comps = comps})
end
cmds.beds = function(w)
    W.beds = {}
    local total, off = tonumber(w[2]), tonumber(w[3])
    for i = 1, total do
        local on = i > off
        local b = {IsValid = function() return true end}
        b.BedMeshComponent = {IsValid = function() return true end, GetCollisionEnabled = function() return on and 3 or 0 end}
        W.beds[i] = b
    end
end
cmds.bedprobe = function(w) ST.sl = {bedOff = (w[2] == "1") or nil} end
cmds.ledger = function(w)
    local p = "B:\\s2_blk_ledger.txt"
    MEM[p] = (MEM[p] or "") .. w[2] .. "\t" .. w[3] .. "\t" .. w[4] .. "\n"
end
cmds.shot = function(w) emit("R shot " .. (S2L.chShotDeny(ST, W.npcs[w[2]]) and "1" or "0")) end
cmds.trig = function(w) emit("R trig " .. (S2L.chTrigDeny(ST, w[2], w[3]) and "1" or "0")) end
cmds.trigenter = function(w) S2L.ldTrig(ST, {sid = w[2], x = tonumber(w[3]), y = tonumber(w[4]), z = tonumber(w[5])}) end
cmds.usedeny = function(w) emit("R use " .. (S2L.chUseDeny(ST, W.uses[w[2]], w[3]) and "1" or "0")) end
cmds.ismain = function(w) emit("R ismain " .. (S2L.isMain(w[2]) and "1" or "0")) end
cmds.mqseen = function(w) S2L.mqSeen(ST, {"X", w[2]}) end
local MQV = {}
cmds.mqflag = function(w)
    MQV.on = b01(w[2]); MQV.sleep = b01(w[3]); MQV.kind = b01(w[4]) and "d" or "c"
    emit("R mqflag " .. S2L.mqFlag(ST, MQV))
end
cmds.mqok = function(w) emit("R mqok " .. (S2L.mqOk({mEp = b01(w[2])}) and "1" or "0")) end
cmds.mqsid = function(w) emit("R mqsid " .. (S2L.mqSid(w[2]) and "1" or "0")) end
cmds.state = function(w)
    local mine = S2L.ldMine(ST) ~= nil
    local peer = S2L.ldPeer(ST) ~= nil
    local owner = ""
    if S2L.ldMine(ST) then owner = api.ROLE elseif S2L.ldPeer(ST) then owner = (api.ROLE == "host") and "client" or "host" end
    emit("S mine=" .. (mine and "1" or "0") .. " peer=" .. (peer and "1" or "0") .. " owner=" .. owner)
end
cmds.leaddlg = function(w)
    local m = S2L.ldMine(ST)
    local a = (m and m.why == "dlg") and "1" or "0"
    local b = (ST.ld and ST.ld.dlg) and "1" or "0"
    emit("R leaddlg " .. a .. b)
end
cmds.ledgerdump = function(w)
    local t = MEM["B:\\s2_blk_ledger.txt"] or ""
    emit("F " .. #t)
    for l in t:gmatch("([^\n]*)\n") do emit("F| " .. l) end
end

for raw in SCENARIO:gmatch("([^\n]*)\n?") do
    local line = raw:gsub("\r", "")
    if line ~= "" and line:sub(1, 1) ~= "#" then
        local w = words(line)
        emit("> " .. line)
        local f = cmds[w[1]]
        if f then f(w) else emit("? unknown command " .. w[1]) end
    end
end

return table.concat(OUT, "\n") .. "\n"
