local function find(id)
    if id == "-" then return nil end
    for i = #W, 1, -1 do if W[i].live and W[i].id == id then return W[i] end end
    return nil
end
local function num(v) return math.tointeger(tonumber(v)) or tonumber(v) or v end
local function sync()
    api.ROLE = ROLE
    if TGT then ST.tgt = {pawn = TGT, uid = TGT.uid >= 0 and TGT.uid or nil, parkOn = PARKED or nil} else ST.tgt = {} end
    ST.pup = {pawn = PUP}
end
ROLE = "client"
for line in (SCENARIO .. "\n"):gmatch("([^\n]*)\n") do
    line = line:gsub("\r$", "")
    if line ~= "" and line:sub(1, 1) ~= "#" then
        local a = {}
        for w in line:gmatch("%S+") do a[#a + 1] = w end
        local cmd = table.remove(a, 1)
        if cmd == "role" then ROLE = a[1]
        elseif cmd == "flag" then FL[a[1]] = num(a[2])
        elseif cmd == "t" then NOW = tonumber(a[1])
        elseif cmd == "me" or cmd == "newpawn" then
            local keep = ME
            if ME then ME.live = false; ME.CharacterMovement.live = false end
            ME = mk("me", "BP_Stalker2Character_C /Game:PersistentLevel.me", true)
            local cm = mk("me.cm", "CharacterMovementComponent /Game:PersistentLevel.me.cm", false)
            ME.CharacterMovement = cm
            if cmd == "newpawn" then ME.hp, ME.alive, ME.x, ME.y, ME.z, ME.uid = keep.hp, keep.alive, keep.x, keep.y, keep.z, keep.uid
            else ME.hp, ME.alive, ME.x, ME.y, ME.z = tonumber(a[1]) + 0.0, a[2] == "1", tonumber(a[3]) + 0.0, tonumber(a[4]) + 0.0, tonumber(a[5]) + 0.0 end
            S2.io.pawn = ME
        elseif cmd == "set" then
            local o, k = find(a[1]), a[2]
            if k == "hp" then o.hp = tonumber(a[3]) + 0.0
            elseif k == "alive" then o.alive = a[3] == "1"
            elseif k == "bleed" then o.bleed = tonumber(a[3]) + 0.0
            elseif k == "uid" then o.uid = math.tointeger(tonumber(a[3]))
            elseif k == "focus" then o.focus = find(a[3])
            elseif k == "wounded" then o.wounded = a[3] == "1"
            elseif k == "vel" then o.vx, o.vy, o.vz = tonumber(a[3]) + 0.0, tonumber(a[4]) + 0.0, tonumber(a[5]) + 0.0
            elseif k == "pos" then o.x, o.y, o.z = tonumber(a[3]) + 0.0, tonumber(a[4]) + 0.0, tonumber(a[5]) + 0.0 end
        elseif cmd == "npc" then
            local o = mk(a[1], "BP_AI_" .. a[2] .. "_C /Game/Maps/World:PersistentLevel.BP_AI_" .. a[2] .. "_C_" .. a[1], true)
            o.x, o.y, o.z, o.hp, o.alive = tonumber(a[3]) + 0.0, tonumber(a[4]) + 0.0, tonumber(a[5]) + 0.0, tonumber(a[6]) + 0.0, a[7] == "1"
            o.mx = o.hp
            local c = mk(a[1] .. ".cap", "CapsuleComponent /Game:PersistentLevel." .. a[1] .. ".cap", false)
            c.owner = o
            o.CapsuleComponent = c
            if a[9] then o.capr, o.caph = tonumber(a[8]) + 0.0, tonumber(a[9]) + 0.0 end
        elseif cmd == "tgt" then TGT = find(a[1])
        elseif cmd == "pup" then PUP = find(a[1])
        elseif cmd == "park" then PARKED = a[1] == "1"
        elseif cmd == "cine" then ST.cv.on = a[1] == "1"
        elseif cmd == "remote" then REMOTE = {x = tonumber(a[1]) + 0.0, y = tonumber(a[2]) + 0.0, z = tonumber(a[3]) + 0.0, recv = tonumber(a[4]) + 0.0}
        elseif cmd == "x1" then
            if not a[2] or a[2] == "-" then X1[a[1]] = nil elseif a[2] == "=" then X1[a[1]] = "" else X1[a[1]] = a[2] end
        elseif cmd == "dev" then ST.dmg.list[#ST.dmg.list + 1] = {seq = math.tointeger(tonumber(a[1])), amount = tonumber(a[2]) + 0.0, at = tonumber(a[3]) + 0.0}
        elseif cmd == "do" then
            sync()
            local n = a[1]
            if n == "dmgStr" then out("OUT " .. dmgStr(ST))
            elseif n == "lifeStr" then out("OUT " .. lifeStr(ST, S2.io.pawn))
            elseif n == "remoteDown" then out("OUT " .. tostring(M.remoteDown()))
            elseif n == "listN" then out("OUT n=" .. #ST.dmg.list)
            elseif n == "dmgIn" then dmgIn(ST)
            elseif n == "lifeGuard" then lifeGuard(ST)
            else error("do " .. tostring(n)) end
        elseif cmd == "shot" then
            sync()
            if ROLE == "host" and valid(S2.io.pawn) then
                for one in a[1]:gmatch("[^|]+") do M.shotResolve(ST, one, S2.io.pawn) end
            end
        elseif cmd == "hp" then
            local o = find(a[1])
            out(string.format("OUT %s hp=%.1f alive=%s", a[1], o.hp, tostring(o.alive)))
        else error("cmd " .. tostring(cmd)) end
    end
end
return table.concat(OUT, "\n") .. (#OUT > 0 and "\n" or "")
