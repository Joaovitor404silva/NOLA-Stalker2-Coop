-- Pasta da ponte: S2_BRIDGE (ambiente) > a primeira destas que tiver role.txt. O papel vem de S2_ROLE ou do role.txt dela ("host" ou "client").
local BRIDGE_CANDIDATES = {
    [[C:\Program Files (x86)\Steam\steamapps\common\S.T.A.L.K.E.R. 2 Heart of Chornobyl\ModWork\s2coop\bridge\]],
    [[C:\s2coop_bridge\]],
    [[D:\s2coop_bridge\]],
}
local function findBridge()
    local e = os.getenv("S2_BRIDGE")
    if e then return e end
    for _, c in ipairs(BRIDGE_CANDIDATES) do
        local f = io.open(c .. "role.txt", "r")
        if f then f:close(); return c end
    end
    return BRIDGE_CANDIDATES[2]
end
BRIDGE_DIR = findBridge()
ROLE = os.getenv("S2_ROLE") or (function() local f = io.open(BRIDGE_DIR .. "role.txt", "r"); if not f then return "host" end; local r = f:read("*l"); f:close(); return ((r or "host"):gsub("%s", "")) end)()
SPAWN_DX = 80000
-- fixC: teclas de depuracao so com debugkeys=1 em flags.txt (padrao 0). Antes, F5 (salvar rapido do jogo), F9, F7, PAUSE, HOME, DEL...
-- rodavam experimentos nos dois PCs. RegisterKeyBind passa a registrar uma tecla de depuracao; tecla de producao usa S2_RegisterKeyBindRaw.
S2_RegisterKeyBindRaw = S2_RegisterKeyBindRaw or RegisterKeyBind
-- b137 keypoll (default ON; keypoll=0 in flags.txt + relaunch = old path). Host crashes 19:06 / 19:26 (2 dumps): UE4SS runs every
-- RegisterKeyBind callback on its event-loop thread, on the same lua_State as the game-thread loops -> two OS threads in one Lua
-- state -> corrupted stack -> AV in luaH_getshortstr (D pressed while strafing = qdrop's key). So no key is given to UE4SS: keys are
-- recorded here and polled in the 100 ms game-thread ticker (PlayerController:IsInputKeyDown), callback on the press edge.
do
    local off = false
    pcall(function()
        local f = io.open(BRIDGE_DIR .. "flags.txt", "r")
        if f then local t = f:read("*a") or ""; f:close(); off = ("\n" .. t):find("\n[\239\187\191]*%s*keypoll%s*=%s*0%f[^%w.]") ~= nil end
    end)
    if not off and not rawget(_G, "S2_KEYPOLL") then
        local KP = {list = {}, fires = 0, errs = 0, last = "-", unk = {}}
        local UEN = {PAUSE = "Pause", HOME = "Home", END = "End", INS = "Insert", DEL = "Delete", PAGE_UP = "PageUp", PAGE_DOWN = "PageDown",
                     SPACE = "SpaceBar", TAB = "Tab", RETURN = "Enter", ESCAPE = "Escape", BACKSPACE = "BackSpace",
                     ZERO = "Zero", ONE = "One", TWO = "Two", THREE = "Three", FOUR = "Four", FIVE = "Five", SIX = "Six", SEVEN = "Seven", EIGHT = "Eight", NINE = "Nine"}
        local MODN = {CONTROL = {"LeftControl", "RightControl"}, SHIFT = {"LeftShift", "RightShift"}, ALT = {"LeftAlt", "RightAlt"}}
        local function nameOf(tbl, v) for k, x in pairs(type(tbl) == "table" and tbl or {}) do if x == v then return k end end end
        function KP.fkey(v)                            -- UE4SS Key value -> UE FKey name ("D", "F5", "NumPadZero", "Pause"), nil if unknown
            local n = nameOf(rawget(_G, "Key"), v)
            if not n then return nil end
            if n:match("^%u$") or n:match("^F%d%d?$") then return n end
            local d = n:match("^NUM_(%u+)$")
            if d then return UEN[d] and ("NumPad" .. UEN[d]) or nil end
            return UEN[n]
        end
        function KP.add(key, mods, fn, debug)
            local e = {vk = key, name = KP.fkey(key), fn = fn, debug = debug and true or false, down = false, mods = {}}
            if type(mods) == "table" then
                for _, mv in ipairs(mods) do e.mods[#e.mods + 1] = MODN[nameOf(rawget(_G, "ModifierKey"), mv) or ""] or {"?"} end
            end
            if not e.name then KP.unk[#KP.unk + 1] = tostring(key) end   -- never fires (no fallback to the thread-unsafe path)
            KP.list[#KP.list + 1] = e
            return true
        end
        local function isDown(pc, n)
            local d = false
            pcall(function() d = pc:IsInputKeyDown({KeyName = FName(n)}) and true or false end)
            return d
        end
        function KP.poll()                             -- game thread only (100 ms ticker)
            local dbg = FL and FL.debugkeys == 1
            local pc
            for _, e in ipairs(KP.list) do
                if e.name and (dbg or not e.debug) then
                    if pc == nil then
                        pc = FindFirstOf("PlayerController") or false
                        if pc and not pc:IsValid() then pc = false end
                    end
                    if not pc then return end
                    local d = isDown(pc, e.name)
                    if d and not e.down then
                        local okm = true
                        for _, alts in ipairs(e.mods) do
                            local any = false
                            for _, an in ipairs(alts) do if isDown(pc, an) then any = true end end
                            okm = okm and any
                        end
                        if okm then
                            KP.fires = KP.fires + 1; KP.last = e.name
                            local ok, er = pcall(e.fn)
                            if not ok then KP.errs = KP.errs + 1; if KP.errs <= 5 then print("[S2Alife] S2KEY erro " .. e.name .. ": " .. tostring(er) .. "\n") end end
                        end
                    end
                    e.down = d
                end
            end
        end
        S2_RegisterKeyBindRaw = function(key, a, b)
            if b ~= nil then return KP.add(key, a, b, false) end
            return KP.add(key, nil, a, false)
        end
        rawset(_G, "S2_KEYPOLL", KP)
    end
    -- b139 hookgt (default 1; hookgt=0 in flags.txt = check + log only). b137's S2_offThread compared tostring(GetCurrentThreadId()) =
    -- "ThreadIdUserdata: <address of a NEW userdata>" -> it differed on EVERY call (live 19:42 / 19:44: n +2 per OnShotSoundStart = its 2
    -- hooks) = false alarm. UE4SS 5e627997: IsInGameThread() = (this thread == GetGameThreadId()), a bool with no allocation; ThreadId has
    -- :ToString() (the number; its __eq returns a count without pushing a bool: never ==, never tostring()). RegisterHook callbacks run on whatever thread calls the UFunction, with no lock
    -- against the game-thread loops, so off the game thread a hook only bumps slots S2_hookReg pre-seeded (integers/booleans: no new key,
    -- no string, no UE call) and returns; the 100 ms game-thread ticker logs it and hands the module a deferred handler(ST, nil, n).
    S2_HOOKT = rawget(_G, "S2_HOOKT") or {mode = false, tid = "-", gtid = "-", off = 0, uncal = 0, pend = false, logged = 0, n = {}, q = {}, last = {}, seen = {}}
    function S2_tidStr() return GetCurrentThreadId():ToString() end
    function S2_hookReg(path)                          -- at RegisterHook time (not off-thread): pre-seed every slot S2_hookGT writes
        local H = S2_HOOKT
        if H.n[path] == nil then H.n[path] = 0; H.q[path] = 0; H.last[path] = "-"; H.seen[path] = false end
    end
    function S2_hookGT(path)                           -- true = run the hook body here; false = off the game thread with hookgt on
        local H = S2_HOOKT
        local m, on = H.mode, true
        if m == 1 then on = IsInGameThread() == true
        elseif m == 2 then local ok, t = pcall(S2_tidStr); on = (not ok) or t == H.tid
        elseif m == false then H.uncal = H.uncal + 1 end   -- before the first ticker: not calibrated, old behaviour
        local c = H.n[path]
        if c then H.n[path] = c + 1; H.last[path] = on end
        if on then return true end
        H.off = H.off + 1; H.pend = true
        if c then H.q[path] = H.q[path] + 1 end
        local fl = rawget(_G, "FL")
        return fl ~= nil and fl.hookgt == 0
    end
end
RegisterKeyBind = function(key, a, b)
    local fn = b or a
    local function guarded(...) if FL and FL.debugkeys == 1 then return fn(...) end end
    local kp = rawget(_G, "S2_KEYPOLL"); if kp then return kp.add(key, b and a or nil, guarded, true) end   -- b137 keypoll
    if b then return S2_RegisterKeyBindRaw(key, a, guarded) end
    return S2_RegisterKeyBindRaw(key, guarded)
end
-- Marco 4: F7 testa se XALifeDisable/XALifeEnable realmente param/retomam o A-Life.
local UEHelpers = require("UEHelpers")
local function log(s) print("[S2Alife:" .. ROLE .. "] " .. s .. "\n") end
do  -- fixD: bloco dos experimentos antigos (F1-F12, PAUSE, HOME, END). Os locais daqui (count, cmd, busy, near, tele, netinfo, fake, track) deixam de contar no limite de 200 locais do arquivo. Sem reindentar de proposito (diff minimo).
local function count()
    local n = {}
    for _, cn in ipairs({"Pawn", "Character", "NPCComponent", "ShootingWeaponComponent", "ModelCharacter"}) do
        local all = FindAllOf(cn); n[#n + 1] = cn .. "=" .. (all and #all or 0)
    end
    return table.concat(n, " ")
end
local function cmd(c)
    local mgr = FindFirstOf("CustomConsoleManagerSS")
    log("CustomConsoleManagerSS valido: " .. tostring(mgr and mgr:IsValid()))
    if mgr and mgr:IsValid() then
        local ok, e = pcall(function() mgr[c](mgr) end)
        log("chamada direta " .. c .. ": ok=" .. tostring(ok) .. " " .. tostring(e))
    end
    local pc = FindFirstOf("PlayerController")
    UEHelpers.GetKismetSystemLibrary():ExecuteConsoleCommand(UEHelpers.GetWorld(), c, pc)
    log("cmd string: " .. c)
end
local busy = false
RegisterKeyBind(Key.PAUSE, function()
    if busy then return end
    busy = true
    ExecuteInGameThread(function()
        log("antes: " .. count())
        cmd("XALifeDisable")
        ExecuteInGameThreadWithDelay(15000, function()  -- fixC: era ExecuteWithDelay (thread async)
            ExecuteInGameThread(function()
                log("15s depois do Disable: " .. count())
                cmd("XALifeEnable")
                ExecuteInGameThreadWithDelay(15000, function()  -- fixC: era ExecuteWithDelay (thread async)
                    ExecuteInGameThread(function() log("15s depois do Enable: " .. count()); busy = false end)
                end)
            end)
        end)
    end)
end)
log("pronto, aperte F7 no jogo")


-- F9 = teleporta 800 m (controle) ; F10 = idem com XALifeDisable antes. Mede NPCs perto do jogador depois de 45 s e volta.
local function near(pawn, radius)
    local p = pawn:K2_GetActorLocation(); local n, tot = 0, 0
    for _, c in ipairs(FindAllOf("Character") or {}) do
        if c:IsValid() and c:GetAddress() ~= pawn:GetAddress() then
            tot = tot + 1
            local l = c:K2_GetActorLocation()
            if (l.X - p.X)^2 + (l.Y - p.Y)^2 < radius * radius then n = n + 1 end
        end
    end
    return n, tot
end
local function tele(disable, dx, dy)
    if busy then return end
    busy = true
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("PlayerController").Pawn
        local home = pawn:K2_GetActorLocation()
        local n0, t0 = near(pawn, 10000)
        log(string.format("tele(disable=%s) antes: perto(100m)=%d total=%d pos=%.0f,%.0f,%.0f", tostring(disable), n0, t0, home.X, home.Y, home.Z))
        if disable then cmd("XALifeDisable") end
        pawn:K2_SetActorLocation({X = home.X + (dx or 80000), Y = home.Y + (dy or 0), Z = home.Z + 300}, false, {}, true)
        ExecuteInGameThreadWithDelay(45000, function()  -- fixC: era ExecuteWithDelay (thread async)
            ExecuteInGameThread(function()
                local n1, t1 = near(pawn, 10000)
                local p = pawn:K2_GetActorLocation()
                log(string.format("tele(disable=%s) 45s depois: perto(100m)=%d total=%d pos=%.0f,%.0f,%.0f", tostring(disable), n1, t1, p.X, p.Y, p.Z))
                pawn:K2_SetActorLocation({X = home.X, Y = home.Y, Z = home.Z + 200}, false, {}, true)
                if disable then cmd("XALifeEnable") end
                busy = false
            end)
        end)
    end)
end

-- Marco 3: estado de rede (F6) e tentativa de ?listen no mapa atual (F5)
local function netinfo()
    ExecuteInGameThread(function()
        local w = FindFirstOf("PlayerController"):GetWorld()
        log("mundo: " .. (w and w:GetFullName() or "nil"))
        for _, cn in ipairs({"NetDriver", "IpNetDriver", "SteamNetDriver", "NetConnection", "PlayerController"}) do
            local all = FindAllOf(cn)
            log(cn .. ": " .. (all and #all or 0))
            if all then for i, o in ipairs(all) do if i <= 2 then log("  " .. o:GetFullName()) end end end
        end
        pcall(function() log("NetMode=" .. tostring(w:GetNetMode())) end)
        pcall(function() local gm = UEHelpers.GetGameModeBase(); log("GameMode=" .. gm:GetFullName()) end)
    end)
end
RegisterKeyBind(Key.F6, netinfo)

-- F3: imprime endereço das instâncias A-Life (para ler a vtable com ReadProcessMemory)
RegisterKeyBind(Key.F3, function()
    ExecuteInGameThread(function()
        for _, cn in ipairs({"ALifeDirector", "ALifePopulationManager", "ALifePolicyManager", "BaseTickableManager"}) do
            local all = FindAllOf(cn)
            if all then
                for i, o in ipairs(all) do
                    if i <= 4 then log(string.format("ADDR %s %X %s", cn, o:GetAddress(), o:GetFullName())) end
                end
            end
        end
    end)
end)

-- F2: posição do pawn do jogador (para comparar com o histórico do Director lido da memória)
RegisterKeyBind(Key.F1, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        if pawn and pawn:IsValid() then
            local p = pawn:K2_GetActorLocation()
            log(string.format("PLAYERPOS %.1f %.1f %.1f", p.X, p.Y, p.Z))
        else
            log("PLAYERPOS: pawn nao encontrado")
        end
    end)
end)

-- F12: destravar (tempo normal + sobe o pawn 150 cm)
RegisterKeyBind(Key.F12, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local w = pawn and pawn:GetWorld()
        if not w then log("F12: sem pawn/mundo"); return end
        local ksl = UEHelpers.GetKismetSystemLibrary()
        ksl:ExecuteConsoleCommand(w, "slomo 1", nil)
        local p = pawn:K2_GetActorLocation()
        pawn:K2_SetActorLocation({X = p.X, Y = p.Y, Z = p.Z + 150}, false, {}, true)
        log(string.format("F12: slomo 1 e pawn subiu para %.0f %.0f %.0f", p.X, p.Y, p.Z + 150))
    end)
end)

-- F4: força o A-Life ligado
RegisterKeyBind(Key.F4, function()
    ExecuteInGameThread(function() cmd("XALifeEnable"); log("F4: XALifeEnable enviado") end)
end)

-- F2: conta NPCs/pawns perto do jogador e perto de um ponto a 80000 cm a leste (centro falso do experimento)
RegisterKeyBind(Key.F2, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        if not (pawn and pawn:IsValid()) then log("F2: sem pawn"); return end
        local h = pawn:K2_GetActorLocation()
        local fx, fy = h.X + 80000, h.Y
        local nh, nf, total = 0, 0, 0
        for _, o in ipairs(FindAllOf("Pawn") or {}) do
            if o:IsValid() and o ~= pawn then
                local ok, p = pcall(function() return o:K2_GetActorLocation() end)
                if ok and p then
                    total = total + 1
                    if (p.X - h.X)^2 + (p.Y - h.Y)^2 < 15000^2 then nh = nh + 1 end
                    if (p.X - fx)^2 + (p.Y - fy)^2 < 15000^2 then nf = nf + 1 end
                end
            end
        end
        log(string.format("F2 COUNT jogador<150m=%d falso<150m=%d total=%d  (pos %.0f,%.0f)", nh, nf, total, h.X, h.Y))
    end)
end)

-- F5: tempo do mundo, dilatação global, pausa
RegisterKeyBind(Key.F5, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local w = pawn and pawn:GetWorld()
        if not w then log("F5: sem mundo"); return end
        local gs = StaticFindObject("/Script/Engine.Default__GameplayStatics")
        local function try(name, f) local ok, v = pcall(f); log("F5 " .. name .. " = " .. tostring(ok and v or ("erro " .. tostring(v)))) end
        try("TimeSeconds", function() return gs:GetTimeSeconds(w) end)
        try("UnpausedTimeSeconds", function() return gs:GetUnpausedTimeSeconds(w) end)
        try("RealTimeSeconds", function() return gs:GetRealTimeSeconds(w) end)
        try("GlobalTimeDilation", function() return gs:GetGlobalTimeDilation(w) end)
        try("IsGamePaused", function() return gs:IsGamePaused(w) end)
        try("pawn CustomTimeDilation", function() return pawn.CustomTimeDilation end)
    end)
end)

-- F9: cria um TargetPoint a +80000 cm (leste) com WorldPartitionStreamingSourceComponent (segundo centro de streaming)
local fake = nil
RegisterKeyBind(Key.F9, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local w = pawn and pawn:GetWorld()
        if not w then log("F9: sem mundo"); return end
        local h = pawn:K2_GetActorLocation()
        local tp = StaticFindObject("/Script/Engine.TargetPoint")
        local ok, a = pcall(function() return w:SpawnActor(tp, {X = h.X + 80000, Y = h.Y, Z = h.Z + 500}, {Pitch = 0, Yaw = 0, Roll = 0}) end)
        if not ok or not a or not a:IsValid() then log("F9: SpawnActor falhou: " .. tostring(a)); return end
        fake = a
        log("F9: ator criado " .. a:GetFullName())
        local cls = StaticFindObject("/Script/Engine.WorldPartitionStreamingSourceComponent")
        local ok2, c = pcall(function() return a:AddComponentByClass(cls, false, {Rotation = {X = 0, Y = 0, Z = 0, W = 1}, Translation = {X = 0, Y = 0, Z = 0}, Scale3D = {X = 1, Y = 1, Z = 1}}, false) end)
        log("F9: componente ok=" .. tostring(ok2) .. " valido=" .. tostring(ok2 and c and c:IsValid()) .. " -> " .. tostring(ok2 and c and c:GetFullName() or c))
        if ok2 and c and c:IsValid() then
            pcall(function() c.bStreamingSourceEnabled = true end)
            pcall(function() c.TargetState = 1 end)
            local ok3, st = pcall(function() return c.TargetState end)
            log("F9: TargetState=" .. tostring(st))
        end
    end)
end)
RegisterKeyBind(Key.F10, function()
    ExecuteInGameThread(function()
        if fake and fake:IsValid() then fake:K2_DestroyActor(); fake = nil; log("F10: ator falso destruido") else log("F10: nada a destruir") end
    end)
end)

-- F8: conta atores (qualquer tipo) perto do ponto falso e perto do jogador
RegisterKeyBind(Key.F8, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        if not (pawn and pawn:IsValid()) then log("F8: sem pawn"); return end
        local h = pawn:K2_GetActorLocation()
        local fx, fy = h.X + 80000, h.Y
        local nf, nh, total = 0, 0, 0
        for _, o in ipairs(FindAllOf("Pawn") or {}) do  -- fixC: era FindAllOf("Actor") (proibido); agora conta so pawns
            if o:IsValid() then
                local ok, p = pcall(function() return o:K2_GetActorLocation() end)
                if ok and p then
                    total = total + 1
                    if (p.X - fx)^2 + (p.Y - fy)^2 < 30000^2 then nf = nf + 1 end
                    if (p.X - h.X)^2 + (p.Y - h.Y)^2 < 30000^2 then nh = nh + 1 end
                end
            end
        end
        log(string.format("F8 ATORES jogador<300m=%d falso<300m=%d total=%d", nh, nf, total))
    end)
end)

-- F7 substitui? não: F11 destrói todos os TargetPoint longe do jogador (restos do experimento F9)
RegisterKeyBind(Key.F11, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOF and nil or FindFirstOf("BP_Stalker2Character_C")
        local h = pawn and pawn:K2_GetActorLocation()
        local n = 0
        for _, o in ipairs(FindAllOf("TargetPoint") or {}) do
            if o:IsValid() and h then
                local p = o:K2_GetActorLocation()
                if (p.X - h.X)^2 + (p.Y - h.Y)^2 > 50000^2 then o:K2_DestroyActor(); n = n + 1 end
            end
        end
        log("F11: TargetPoints destruidos = " .. n)
    end)
end)

-- F7: spawna um NPC pelo console de debug (XSpawnObjBySID) no ponto falso (+80000 cm leste do jogador)
RegisterKeyBind(Key.F7, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        if not (pawn and pawn:IsValid()) then log("F7: sem pawn"); return end
        local h = pawn:K2_GetActorLocation()
        local mgr = FindFirstOf("CustomConsoleManagerRK")
        log("F7: mgr=" .. tostring(mgr and mgr:IsValid() and mgr:GetFullName()))
        if not (mgr and mgr:IsValid()) then return end
        local sid = "GeneralNPC_Spark_CloseCombat"
        local ok, e = pcall(function() mgr:XSpawnObjBySID(sid, 1, false, h.X + (SPAWN_DX or 80000), h.Y, h.Z + 100, 0, 0, 0) end)
        log("F7: XSpawnObjBySID " .. sid .. " ok=" .. tostring(ok) .. " " .. tostring(e))
    end)
end)

-- HOME: liga/desliga o rastreador da bolha: loga a distancia em que cada pawn aparece/some e o max/min das distancias
local track = { on = false, seen = {} }
RegisterKeyBind(Key.HOME, function()
    track.on = not track.on
    log("HOME: rastreador " .. tostring(track.on))
    if not track.on then return end
    track.seen = {}
    track.gen = (track.gen or 0) + 1  -- fixC: laco na game thread; a geracao mata o laco anterior (return true nao para o laco neste UE4SS)
    local trackGen = track.gen
    LoopInGameThreadWithDelay(2000, function()
        if not track.on or trackGen ~= track.gen then return true end
        ExecuteInGameThread(function()
            local pawn = FindFirstOf("BP_Stalker2Character_C")
            if not (pawn and pawn:IsValid()) then return end
            local h = pawn:K2_GetActorLocation()
            local now, mx, n = {}, 0, 0
            local bands, edges = {0,0,0,0,0,0}, {100,300,600,1000,2000}
            for _, o in ipairs(FindAllOf("Pawn") or {}) do
                if o:IsValid() and o ~= pawn then
                    local ok, p = pcall(function() return o:K2_GetActorLocation() end)
                    if ok and p then
                        local id = o:GetFullName()
                        local d = math.sqrt((p.X - h.X)^2 + (p.Y - h.Y)^2) / 100
                        now[id] = d; n = n + 1
                        local bi = 6
                        for k, e in ipairs(edges) do if d < e then bi = k; break end end
                        bands[bi] = bands[bi] + 1
                        if d > mx then mx = d end
                        if not track.seen[id] then log(string.format("TRK +%s d=%.0fm", id:match("[^%.]+$") or id, d)) end
                    end
                end
            end
            for id, d in pairs(track.seen) do
                if not now[id] then log(string.format("TRK -%s ultima d=%.0fm", id:match("[^%.]+$") or id, d)) end
            end
            track.seen = now
            log(string.format("TRK pos=%.0f,%.0f n=%d <100=%d <300=%d <600=%d <1000=%d <2000=%d >=2000=%d", h.X, h.Y, n, bands[1], bands[2], bands[3], bands[4], bands[5], bands[6]))
        end)
        return false
    end)
end)

-- END: teleporta o pawn 100 m para leste (+3 m de altura) para medir a bolha sem andar
RegisterKeyBind(Key.END, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        if not (pawn and pawn:IsValid()) then log("END: sem pawn"); return end
        local p = pawn:K2_GetActorLocation()
        pawn:K2_SetActorLocation({X = p.X + 10000, Y = p.Y, Z = p.Z + 300}, false, {}, true)
        log(string.format("END: pawn -> %.0f %.0f %.0f", p.X + 10000, p.Y, p.Z + 300))
    end)
end)

end  -- fixD: fim do bloco dos experimentos antigos
-- Teste de alvo da IA: PAGEUP cria um NPC neutro (avatar) 5 m a frente; PAGEDOWN cria um bandido 15 m a leste do avatar
-- e loga por 60 s a distancia dele ao avatar e ao jogador, e se o avatar ainda e valido.
local avatar
local function pawnSet() local s = {}; for _, o in ipairs(FindAllOf("Pawn") or {}) do s[o:GetFullName()] = true end; return s end
RegisterKeyBind(Key.INS, function()
    log("INS: pressionado")
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local p = pawn:K2_GetActorLocation()
        local before = pawnSet()
        local mgr = FindFirstOf("CustomConsoleManagerRK")
        local ok, e = pcall(function() mgr:XSpawnObjBySID("GeneralNPC_Neutral_CloseCombat", 1, false, p.X + 500, p.Y, p.Z + 50, 0, 0, 0) end)
        log("PGUP: spawn ok=" .. tostring(ok) .. " " .. tostring(e))
        ExecuteInGameThreadWithDelay(1500, function()  -- fixC: era ExecuteWithDelay (thread async)
            ExecuteInGameThread(function()
                for _, o in ipairs(FindAllOf("Pawn") or {}) do
                    if not before[o:GetFullName()] and o:IsValid() then avatar = o; log("PGUP: avatar = " .. o:GetFullName()) end
                end
            end)
        end)
    end)
end)
RegisterKeyBind(Key.DEL, function()
    log("DEL: pressionado")
    ExecuteInGameThread(function()
        if not (avatar and avatar:IsValid()) then log("PGDN: sem avatar"); return end
        local c = avatar:K2_GetActorLocation()
        local before = pawnSet()
        local mgr = FindFirstOf("CustomConsoleManagerRK")
        local ok, e = pcall(function() mgr:XSpawnObjBySID("GeneralNPC_Bandit_CloseCombat", 1, false, c.X + 1500, c.Y, c.Z + 50, 0, 0, 0) end)
        log("PGDN: spawn ok=" .. tostring(ok) .. " " .. tostring(e))
        local t = 0
        LoopInGameThreadWithDelay(2000, function()  -- fixC: era LoopAsync; depois de 60 s o corpo vira no-op
            if t >= 60 then return true end
            t = t + 2
            ExecuteInGameThread(function()
                local pawn = FindFirstOf("BP_Stalker2Character_C")
                local av = (avatar and avatar:IsValid()) and avatar:K2_GetActorLocation() or nil
                for _, o in ipairs(FindAllOf("Pawn") or {}) do
                    if not before[o:GetFullName()] and o:IsValid() then
                        local p, q = o:K2_GetActorLocation(), pawn:K2_GetActorLocation()
                        log(string.format("PGDN t=%d %s dAvatar=%s dPlayer=%.1fm avatarValid=%s", t, o:GetFullName():match("[^%.]+$"),
                            av and string.format("%.1fm", math.sqrt((p.X-av.X)^2+(p.Y-av.Y)^2)/100) or "-", math.sqrt((p.X-q.X)^2+(p.Y-q.Y)^2)/100, tostring(av ~= nil)))
                    end
                end
            end)
            return t >= 60
        end)
    end)
end)

-- NUM_ZERO: liga/desliga a ponte de arquivos com o relay.py (out.json = posicao local, in.json = jogador 2).
-- O avatar do jogador 2 e o NPC criado por INS; a cada 100 ms ele e movido para a posicao de in.json.
local BR = BRIDGE_DIR
local MYGEN = tostring(os.time()) .. "-" .. tostring(math.random(100000))
do local gf = io.open(BR .. "gen.txt", "w"); if gf then gf:write(MYGEN); gf:close() end end
local staleChk, staleVal = -10, false
local function staleGen()  -- cache de 1 s (os lacos de 30 Hz nao devem ler arquivo a cada passo)
    local n = os.clock()
    if n - staleChk < 1 then return staleVal end
    staleChk = n
    local gf = io.open(BR .. "gen.txt", "r")
    if not gf then staleVal = false; return false end
    local g = gf:read("*a"); gf:close()
    staleVal = g ~= MYGEN
    return staleVal
end
-- v7: causa provavel da corrupcao (numeros virando funcao, "UFunction expected N, received N+1", "Ref was not function"): LoopAsync/ExecuteWithDelay
-- rodam Lua na thread async do UE4SS ao mesmo tempo que a game thread roda Lua no mesmo lua_State (sem trava). Regra: lacos/atrasos do co-op
-- so na game thread (LoopInGameThreadWithDelay / ExecuteInGameThreadWithDelay, UE4SS 3.0.1). fn() roda na game thread; true encerra; erro nao mata.
print("[S2Alife:" .. tostring(ROLE) .. "] AVATAR v7: LoopInGameThreadWithDelay=" .. type(LoopInGameThreadWithDelay) .. " ExecuteInGameThreadWithDelay=" .. type(ExecuteInGameThreadWithDelay) .. " CancelDelayedAction=" .. type(CancelDelayedAction) .. string.char(10))
if type(ExecuteInGameThreadWithDelay) ~= "function" then  -- reserva (UE4SS sem a API): comportamento antigo
    ExecuteInGameThreadWithDelay = function(ms, f) ExecuteWithDelay(ms, function() ExecuteInGameThread(f) end) end
end
local LoopGT = LoopInGameThreadWithDelay
if type(LoopGT) ~= "function" then
    LoopGT = function(ms, f) LoopAsync(ms, function() local r = false; ExecuteInGameThread(function() f() end); return r end) end
end
S2PROF = S2PROF or {}                    -- batch 53: name -> {n, ms, max, over8} game-thread time per loop body
local function profAdd(name, ms)
    local e = S2PROF[name]; if not e then e = {n = 0, ms = 0, max = 0, over8 = 0}; S2PROF[name] = e end
    e.n = e.n + 1; e.ms = e.ms + ms; if ms > e.max then e.max = ms end; if ms > 8 then e.over8 = e.over8 + 1 end
end
local function gameLoop(ms, name, fn)
    local stop, h, errs = false, nil, 0
    h = LoopGT(ms, function()
        if stop then return true end
        if staleGen() then stop = true end
        local ok, r = true, nil
        if not stop then local t0 = os.clock(); local s2 = S2; if s2 then s2.ffSeq = (s2.ffSeq or 0) + 1; s2.ffBody = s2.ffSeq end; ok, r = pcall(fn); if s2 then s2.ffBody = nil end; profAdd(name, (os.clock() - t0) * 1000) end   -- b143perf: S2.ffBody = this body (core ffc)
        if ok and r == true then stop = true end
        if not ok then errs = errs + 1; if errs <= 5 or errs % 100 == 0 then print("[S2Alife:" .. tostring(ROLE) .. "] AVATAR erro (laco " .. name .. ") #" .. errs .. ": " .. tostring(r) .. string.char(10)) end end
        if stop and h then pcall(CancelDelayedAction, h) end
        return stop
    end)
end
local bridgeOn = false
local leash = "ok"
ROLE = ROLE or "host"  -- "client": a coleira teleporta o pawn local para perto do host quando passa de 200 m
MIRROR_TEST = false  -- true: aplica os espelhos mesmo no host (teste em loopback com relay --dx)
SCAN_AVATAR = false  -- teste: inclui o NPC do INS no snapshot (em producao: false)
local mirrors, npcJson, tick, lastIn = {}, "[]", 0, nil
local mirrorLastX = 0
local scanErrLogged = false
local meshCache = {}
local boneNames = {}  -- mesh -> lista de nomes de osso (mesma ordem em host e cliente)
local function bonesOf(mesh, comp)
    local l = boneNames[mesh]
    if not l then
        l = {}
        for i = 0, comp:GetNumBones() - 1 do l[#l + 1] = comp:GetBoneName(i) end
        boneNames[mesh] = l
    end
    return l
end
-- varre os NPCs a ate 60 m do jogador local (max 8) e devolve o array JSON do snapshot, com a pose (rotacao mundial de cada osso, graus x10)
-- Scan de NPCs (experimental, scan=1): 1 pawn por chamada (round-robin), so dentro de 2000 cm do jogador local, so com o jogador PARADO (o chamador checa |v|<30),
-- revalidando por nome imediatamente antes de ler Mesh/ossos. Resultados ficam num cache por id (validade 15 s) que forma o snapshot.
local scanIdx, scanCache = 0, {}
-- v15: aparencia do NPC (lida 1 vez por id): meshes extras (casaco, cabeca, mochila...) e materiais do mesh principal (MID -> material pai)
local lookCache = {}
local lookDiagN = 0
local function compList(o, clsPath)
    local arr = o:K2_GetComponentsByClass(StaticFindObject(clsPath))
    local comps = {}
    local okn, len = pcall(function() return #arr end)
    local function unwrap(e)
        local okf = pcall(function() return e:GetFullName() end)
        if okf then return e end
        local okg, g = pcall(function() return e:get() end)
        if okg and g then return g end
        return e
    end
    if okn and len and len > 0 then for i = 1, len do comps[#comps + 1] = unwrap(arr[i]) end
    else pcall(function() arr:ForEach(function(_, e) comps[#comps + 1] = unwrap(e) end) end) end
    return comps
end
-- lookdiag=1: despeja (2 NPCs) todos os componentes do pawn: nome, classe, mesh, visibilidade, leader
local function lookDiag(o, sid)
    if lookDiagN >= 2 then return end
    lookDiagN = lookDiagN + 1
    local comps = compList(o, "/Script/Engine.ActorComponent")
    log("LOOKDIAG " .. sid .. ": componentes=" .. #comps)
    local nerr = 0
    for _, c in ipairs(comps) do
        local okD, eD = pcall(function()
            local full = c:GetFullName()
            local line = (full:match("^%S+") or "?") .. " " .. (full:match("[^%.:]+$") or "?")
            local extra = ""
            pcall(function() local a = c:GetSkeletalMeshAsset(); extra = extra .. " sk=" .. ((a and a:IsValid()) and a:GetFullName():match("[^%.]+$") or "nil") end)
            pcall(function() local a = c.StaticMesh; if a and a:IsValid() then extra = extra .. " sm=" .. a:GetFullName():match("[^%.]+$") end end)
            pcall(function() extra = extra .. " vis=" .. tostring(c:IsVisible()) end)
            pcall(function() local l = c.LeaderPoseComponent; if l then local g = l; pcall(function() g = l:get() end); if g and g:IsValid() then extra = extra .. " leader=" .. g:GetFName():ToString() end end end)
            pcall(function() local at = c:GetAttachParent(); if at and at:IsValid() then extra = extra .. " pai=" .. at:GetFName():ToString() end end)
            log("LOOKDIAG  " .. line .. extra)
        end)
        if not okD then nerr = nerr + 1; if nerr <= 3 then log("LOOKDIAG  erro: " .. tostring(eD) .. " tipo=" .. type(c)) end end
    end
end
-- caminhos dos materiais de um componente (ate 8); MaterialInstanceDynamic -> material pai (o MID nao existe no outro PC)
local function matsOf(comp)
    local mats = {}
    pcall(function()
        for i = 0, math.min(comp:GetNumMaterials(), 8) - 1 do
            local mp = ""
            pcall(function()
                local m = comp:GetMaterial(i)
                if m and m:IsValid() then
                    local full = m:GetFullName()
                    if full:find("^MaterialInstanceDynamic ") then local par = m.Parent; if par and par:IsValid() then full = par:GetFullName() else full = "" end end
                    mp = (full:gsub("^%S+ ", ""))
                end
            end)
            mats[#mats + 1] = mp
        end
    end)
    return mats
end
local function applyMats(comp, list, sep)
    local n, i = 0, 0
    for mp in (list .. sep):gmatch("(.-)" .. sep) do
        if mp ~= "" then pcall(function()
            local mo = StaticFindObject(mp)
            if not (mo and mo:IsValid()) then pcall(function() mo = LoadAsset(mp) end) end
            if mo and mo:IsValid() then comp:SetMaterial(i, mo); n = n + 1 end
        end) end
        i = i + 1
    end
    return n
end
local function scanLook(o, mc, asset, sid)
    local c = lookCache[sid]
    -- v25b: a aparencia e relida quando a arma na mao muda (endereco do componente da arma)
    local wa = 0
    pcall(function() local w = o:GetWeaponInHandsMeshComponent(); if w and w:IsValid() then wa = w:GetAddress() end end)
    if c and c.wa == wa then return c.parts, c.mats, c.statics end
    local parts, mats = {}, {}
    if FL.lookdiag == 1 then pcall(lookDiag, o, sid) end
    pcall(function()
        for _, pc in ipairs(compList(o, "/Script/Engine.SkeletalMeshComponent")) do
            pcall(function()
                if #parts >= 12 or not (pc and pc:IsValid()) or pc:GetAddress() == mc:GetAddress() then return end   -- batch 53: 8 -> 12
                local a = pc:GetSkeletalMeshAsset()
                if not (a and a:IsValid()) then return end
                local path = (a:GetFullName():gsub("^%S+ ", ""))
                if path == asset then return end
                local nm = pc:GetFName():ToString()
                if nm == "ShadowMesh" then return end  -- copia invisivel do corpo do jogador (so sombra)
                -- v25: arma na mao = skeletal preso por soquete, sem leader pose -> manda o soquete (4o campo); o espelho prende no soquete em vez de leader pose
                local sock, lead = "", false
                pcall(function() local l = pc.LeaderPoseComponent; local g = l; pcall(function() g = l:get() end); lead = (g and g:IsValid()) and true or false end)
                if not lead then pcall(function()
                    local par = pc:GetAttachParent()
                    local sn = pc:GetAttachSocketName():ToString()
                    if par and par:IsValid() and par:GetAddress() == mc:GetAddress() and sn ~= "None" and sn ~= "" then sock = sn end
                end) end
                parts[#parts + 1] = path .. "@" .. table.concat(matsOf(pc), ",") .. "@" .. nm .. "@" .. sock
            end)
        end
    end)
    mats = matsOf(mc)
    -- v17: acessorios estaticos (lanterna de cabeca, NVG, pecas da mascara): nome>pai>soquete>mesh>transform relativo
    local statics = {}
    pcall(function()
        for _, sc in ipairs(compList(o, "/Script/Engine.StaticMeshComponent")) do
            pcall(function()
                if #statics >= 12 or not (sc and sc:IsValid()) or not sc:IsVisible() then return end
                local nm = sc:GetFName():ToString()
                if nm:find("FakeLight") then return end
                local a = sc.StaticMesh
                if not (a and a:IsValid()) then return end
                local par = sc:GetAttachParent()
                if not (par and par:IsValid()) then return end
                local sock = sc:GetAttachSocketName():ToString()
                local l, r, k = sc.RelativeLocation, sc.RelativeRotation, sc.RelativeScale3D
                statics[#statics + 1] = string.format("%s>%s>%s>%s>%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f", nm, par:GetFName():ToString(), sock,
                    (a:GetFullName():gsub("^%S+ ", "")), l.X, l.Y, l.Z, r.Pitch, r.Yaw, r.Roll, k.X, k.Y, k.Z)
            end)
        end
    end)
    local wdiag = "arma=?"
    pcall(function()
        local w = o:GetWeaponInHandsMeshComponent()
        if w and w:IsValid() then
            wdiag = "arma=" .. w:GetFName():ToString()   -- batch 53: name only; no extra native calls on a possibly half-spawned NPC
        else wdiag = "arma=nil" end
    end)
    c = {parts = table.concat(parts, "|"), mats = table.concat(mats, "|"), statics = table.concat(statics, "|"), wa = wa}
    lookCache[sid] = c
    log("SCAN aparencia " .. sid .. ": partes=" .. #parts .. " mats=" .. #mats .. " estaticos=" .. #statics .. " " .. wdiag)
    return c.parts, c.mats, c.statics
end
local npcDmgDone = 0
local shotSeq, shotStr, shotUntil, shotTestDone, shotSeen = 0, "", 0, 0, {}
local shotDet = {hooks = false, n = {}, last = nil, polls = 0}
local function shotDetectInit()
    if shotDet.hooks then return end
    shotDet.hooks = true
    for _, fn in ipairs({"/Script/Stalker2.ShootingWeaponComponent:Shoot", "/Script/Stalker2.ShootingWeaponComponent:ShootWithOwnerOverride",
                         "/Script/Stalker2.PC:OnFirePressed", "/Script/Stalker2.PC:OnFireReleased", "/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"}) do
        local short = fn:match("[^%.]+$")
        local okH, eH = pcall(function()
            local hk = "shotdet:" .. short
            S2_hookReg(hk)
            RegisterHook(fn, function(self)
                if not S2_hookGT(hk) then return end   -- b139 hookgt: off the game thread = counted, body skipped
                if short == "ItemAppearanceComponent:OnShotSoundStart" then
                    local mine = false
                    pcall(function()
                        local ow = self:get():GetOwner()
                        mine = shotDet.pawnAddr ~= nil and ow:GetAddress() == shotDet.pawnAddr
                        if (shotDet.n[short] or 0) < 20 then log("SHOTDET dono=" .. ow:GetFullName():match("[^%.:]+$") .. " meu=" .. tostring(mine)) end
                    end)
                    if mine then shotDet.pending = (shotDet.pending or 0) + 1 end
                end
                shotDet.n[short] = (shotDet.n[short] or 0) + 1
                if shotDet.n[short] <= 20 then
                    local who = "?"
                    pcall(function() who = self:get():GetFullName():match("[^%.]+$") end)
                    log("SHOTDET hook " .. short .. " n=" .. shotDet.n[short] .. " self=" .. who)
                end
            end)
        end)
        log("SHOTDET RegisterHook " .. short .. " ok=" .. tostring(okH) .. " " .. (okH and "" or tostring(eH)))
    end
end
local shotQ = {}  -- {str=, t=}
-- v26: dano real por arma. Chave = mesh da arma na mao; valores do jogo (CharacterWeaponSettingsPrototypes.cfg.bin, entradas *_Player, lidas com D:\modding\cfgparse.py):
-- {BaseDamage, ArmorPiercing, EffectiveFireDistanceMax (cm), DistanceDropOffLength (cm), MinBulletDistanceDamageModifier}. So a Viper (SK_vip) foi medida no jogo.
local WEAPONS = {
    SK_pm = {30.0, 1.0, 1000.0, 1200.0, 0.25}, -- GunPM_HG_Player
    SK_udp = {25.0, 1.0, 1100.0, 1500.0, 0.3}, -- GunUDP_HG_Player
    SK_apb = {20.0, 3.0, 1500.0, 1400.0, 0.35}, -- GunAPB_HG_Player
    SK_m1000 = {9.5, 2.0, 1000.0, 1000.0, 0.34}, -- GunM10_HG_Player
    SK_rhino00000 = {100.0, 3.0, 3000.0, 1800.0, 0.3}, -- GunRhino_HG_Player
    SK_kora = {51.0, 2.0, 1000.0, 1500.0, 0.2}, -- GunKora_HG_Player
    SK_vip = {19.0, 1.0, 2000.0, 800.0, 0.45}, -- GunViper_PP_Player
    SK_aku = {19.0, 1.0, 2000.0, 1500.0, 0.5}, -- GunAKU_PP_Player
    SK_bucket0 = {23.0, 2.0, 2000.0, 1300.0, 0.35}, -- GunBucket_PP_Player
    SK_integ = {13.0, 2.8, 2500.0, 1100.0, 0.57}, -- GunIntegral_PP_Player
    SK_zubr0 = {20.0, 2.7, 2500.0, 1000.0, 0.48}, -- GunZubr_PP_Player
    SK_Fora230 = {20.0, 2.0, 1800.0, 1600.0, 0.41}, -- GunFora230_PP_Player
    SK_AK74 = {23.0, 1.0, 3000.0, 1500.0, 0.65}, -- GunAK74_ST_Player
    SK_m160 = {16.0, 1.0, 3000.0, 3000.0, 0.75}, -- GunM16_ST_Player
    SK_gp37 = {16.0, 2.0, 3500.0, 3500.0, 0.75}, -- GunG37_ST_Player
    SK_fora0 = {17.0, 2.0, 3000.0, 1500.0, 0.65}, -- GunFora_ST_Player
    SK_grim0 = {25.0, 2.7, 2500.0, 2000.0, 0.6}, -- GunGrim_ST_Player
    SK_gvi = {35.0, 2.0, 2200.0, 3500.0, 0.9}, -- GunGvintar_ST_Player
    SK_kharod000 = {18.0, 3.0, 3500.0, 2500.0, 0.85}, -- GunKharod_ST_Player
    SK_lav = {20.0, 2.8, 2200.0, 3500.0, 0.9}, -- GunLavina_ST_Player
    SK_dnipro = {23.0, 3.0, 3000.0, 1500.0, 0.75}, -- GunDnipro_ST_Player
    Sk_arev = {16.0, 2.6, 3000.0, 3000.0, 0.7}, -- GunArev_ST_Player
    SK_pkp00000 = {25.0, 3.0, 3000.0, 5000.0, 0.6}, -- GunPKP_MG_Player
    SK_obrez = {150.0, 1.0, 1000.0, 880.0, 0.1}, -- GunObrez_SG_Player
    SK_Toz34 = {150.0, 1.0, 2100.0, 1500.0, 0.1}, -- GunTOZ_SG_Player
    SK_m86000 = {160.0, 2.0, 1500.0, 1200.0, 0.1}, -- GunM860_SG_Player
    SK_spsa00 = {140.0, 2.4, 1500.0, 950.0, 0.1}, -- GunSPSA_SG_Player
    SK_d1200 = {140.0, 3.0, 1500.0, 950.0, 0.1}, -- GunD12_SG_Player
    SK_ram2 = {150.0, 3.0, 2000.0, 1500.0, 0.1}, -- GunRam2_SG_Player
    SK_Skp = {75.0, 2.5, 2500.0, 3000.0, 0.4}, -- GunSKP_DMR_Player
    SK_Gp3a = {40.0, 3.0, 3000.0, 3000.0, 0.5}, -- GunGP3A_DMR_Player
    SK_mar = {55.0, 2.0, 4000.0, 3000.0, 0.5}, -- GunMark_SP_Player
    SK_svm = {70.0, 2.0, 4000.0, 2500.0, 0.5}, -- GunSVDM_SP_Player
    SK_m701 = {160.0, 3.0, 5000.0, 2500.0, 0.55}, -- GunM701_SP_Player
    SK_svu = {80.0, 3.0, 4500.0, 2500.0, 0.6}, -- GunSVU_SP_Player
    SK_ThreeLine = {100.0, 1.0, 4000.0, 4000.0, 0.6}, -- GunThreeLine_SP_Player
    SK_gauss = {500.0, 4.0, 10000.0, 1000.0, 1.0}, -- GunGauss_SP_Player
    SK_rpg7 = {10.0, 1.0, 10000.0, 10000.0, 1.0}, -- GunRpg7_GL_Player
}
-- v27 (NAO TESTADO no jogo): multiplicador de dano do jogador por dificuldade (DifficultyPrototypes.cfg do Zone Kit, Weapon_BaseDamage).
-- Nao ha leitura da dificuldade em runtime: cada jogador poe a sua em flags.txt (difficulty=Easy|Medium|Hard|Stalker; Stalker = Mestre). Custom/desconhecida = 1.0.
local DIFF_WEAPON_MULT = {Easy = 1.3, Medium = 1.0, Hard = 0.75, Stalker = 0.75}
local function weaponStats(pawn)
    local st, nm = nil, "?"
    pcall(function()
        local w = pawn:GetWeaponInHandsMeshComponent()
        if w and w:IsValid() then nm = w:GetSkeletalMeshAsset():GetFullName():match("[^%.]+$"); st = WEAPONS[nm] end
    end)
    return st, nm
end
local function shotEmitReal(pawn, p)
    shotDet.pawnAddr = pawn:GetAddress()
    local n = shotDet.pending or 0
    if n == 0 then return end
    shotDet.pending = 0
    local ox, oy, oz, dx, dy, dz
    local okC, eC = pcall(function()
        local cm = UEHelpers.GetPlayerController().PlayerCameraManager
        local l, r = cm:GetCameraLocation(), cm:GetCameraRotation()
        local cp, yw = math.cos(math.rad(r.Pitch)), math.rad(r.Yaw)
        ox, oy, oz, dx, dy, dz = l.X, l.Y, l.Z, cp * math.cos(yw), cp * math.sin(yw), math.sin(math.rad(r.Pitch))
    end)
    if not okC then log("SHOT camera erro " .. tostring(eC)); return end
    local ws, wname = weaponStats(pawn)
    local tail = string.format("%.1f", FL.shotdmg)  -- arma desconhecida: dano fixo (caminho antigo)
    if ws then tail = string.format("%.2f;%.2f;%.0f;%.0f;%.3f;%.2f", ws[1], ws[2], ws[3], ws[4], ws[5], DIFF_WEAPON_MULT[tostring(FL.difficulty)] or 1.0) end
    for _ = 1, n do
        shotSeq = shotSeq + 1
        shotQ[#shotQ + 1] = {t = os.clock(), str = string.format("%d;%.1f;%.1f;%.1f;%.5f;%.5f;%.5f;", shotSeq, ox, oy, oz, dx, dy, dz) .. tail}
    end
    log(string.format("SHOT real: %d disparo(s), seq ate %d, arma=%s (%s) origem %.0f,%.0f,%.0f dir %.3f,%.3f,%.3f", n, shotSeq, wname, ws and "tabela" or "dano fixo", ox, oy, oz, dx, dy, dz))
end
local function shotOutStr()
    local now, keep, parts = os.clock(), {}, {}
    for _, e in ipairs(shotQ) do if now - e.t < 1.5 then keep[#keep + 1] = e; parts[#parts + 1] = e.str end end
    shotQ = keep
    if shotStr ~= "" then parts[#parts + 1] = shotStr end
    return table.concat(parts, "|")
end
local function shotDetectPoll(pawn)
    local okI, v = pcall(function() return pawn:IsShooting() end)
    if not okI then if shotDet.last ~= "erro" then shotDet.last = "erro"; log("SHOTDET IsShooting erro " .. tostring(v)) end; return end
    v = v and true or false
    if v ~= shotDet.last then shotDet.last = v; log("SHOTDET IsShooting -> " .. tostring(v)) end
end
-- v19 (roteiro c): tiros. Quem atira manda "shots":"seq;ox;oy;oz;dx;dy;dz;dano" no snapshot; o HOST resolve o acerto (raio x capsula dos NPCs, em Lua,
-- sem LineTrace: nao ve paredes) e aplica Obj:ReceiveDamage. Teste: flag shottest=N (mudar o valor dispara 1 tiro): 1 = mira no NPC mais proximo,
-- 3 = mira 3 m ao lado (deve errar), 2 = direcao da camera.
shotSeq, shotStr, shotUntil, shotTestDone, shotSeen = 0, "", 0, 0, {}
local function nearestHuman(pawn, p)
    local best, bd, bq = nil, 1e18, nil
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        pcall(function()
            if not o:IsValid() or o == pawn then return end
            if not o:GetFullName():find("BP_AI_Human_C", 1, true) then return end
            local q = o:K2_GetActorLocation()
            local d = (q.X-p.X)^2 + (q.Y-p.Y)^2
            if d < bd and o:IsAlive() then best, bd, bq = o, d, q end
        end)
    end
    return best, bq
end
local function shotTestFire(pawn, p)
    shotTestDone = FL.shottest
    local ox, oy, oz = p.X, p.Y, p.Z + 60
    local dx, dy, dz
    if FL.shottest == 2 then
        local r = UEHelpers.GetPlayerController():GetControlRotation()
        local cp, yw = math.cos(math.rad(r.Pitch)), math.rad(r.Yaw)
        dx, dy, dz = cp * math.cos(yw), cp * math.sin(yw), math.sin(math.rad(r.Pitch))
    else
        local t, q = nearestHuman(pawn, p)
        if not t then log("SHOT teste: nenhum NPC humano vivo"); return end
        local tx, ty, tz = q.X, q.Y, q.Z + 20
        if FL.shottest == 3 then  -- 3 m para o lado (perpendicular a linha de tiro)
            local lx, ly = tx - ox, ty - oy
            local ln = math.sqrt(lx * lx + ly * ly)
            tx, ty = tx - ly / ln * 300, ty + lx / ln * 300
        end
        dx, dy, dz = tx - ox, ty - oy, tz - oz
        local n = math.sqrt(dx * dx + dy * dy + dz * dz)
        dx, dy, dz = dx / n, dy / n, dz / n
    end
    shotSeq = shotSeq + 1
    shotStr = string.format("%d;%.1f;%.1f;%.1f;%.5f;%.5f;%.5f;%.1f", shotSeq, ox, oy, oz, dx, dy, dz, FL.shotdmg)
    shotUntil = os.clock() + 1.5
    log("SHOT enviado (teste " .. tostring(FL.shottest) .. "): " .. shotStr)
end
-- host: resolve um tiro recebido. dx vem com o deslocamento de teste do relay (--dx) so na origem? nao: o relay so desloca x do jogador e dos npcs.
local function shotResolve(str, pawn)
    if S2 and S2.mod and S2.mod.shotResolve then return S2.mod.shotResolve(S2.state, str, pawn) end
    local v = {}
    for num in str:gmatch("[^;]+") do v[#v + 1] = tonumber(num) end
    if #v < 8 then return end
    -- fixA: shotSeen[seq] = hora em que o seq foi visto pela ultima vez (antes: true para sempre). Um seq so e repetido se foi visto ha menos de 5 s
    -- (o atirador reenvia cada tiro por 1,5 s; um in.json parado e relido a cada passo e mantem a marca viva). Quem atira e relanca o jogo
    -- recomeca do seq 1: as marcas antigas ja venceram, entao os tiros novos valem. A tabela fica limitada aos seqs dos ultimos 5 s.
    local nowS = os.clock()
    if nowS - (shotSeen.pruneAt or -10) >= 1 then
        shotSeen.pruneAt = nowS
        for k, at in pairs(shotSeen) do if type(k) == "number" and nowS - at > 5 then shotSeen[k] = nil end end
    end
    local seenAt = shotSeen[v[1]]
    shotSeen[v[1]] = nowS
    if seenAt then return end
    local ox, oy, oz, dx, dy, dz, dmg = v[2], v[3], v[4], v[5], v[6], v[7], v[8]
    local best, bt, bh, n = nil, 1e18, 0, 0
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        pcall(function()
            if not o:IsValid() or o == pawn then return end
            if not o:GetFullName():find("BP_AI_Human_C", 1, true) or not o:IsAlive() then return end
            if avatar and o == avatar then return end  -- v28: o corpo do jogador remoto nao e alvo dos tiros retransmitidos
            n = n + 1
            local q = o:K2_GetActorLocation()
            -- ponto do raio mais proximo do eixo vertical da capsula (centro q, meia-altura 90, raio 45)
            local t = (q.X - ox) * dx + (q.Y - oy) * dy + (q.Z - oz) * dz
            if t <= 0 or t > 30000 then return end
            local hx, hy, hz = ox + dx * t, oy + dy * t, oz + dz * t
            local dh = math.sqrt((hx - q.X)^2 + (hy - q.Y)^2)
            if dh <= 45 and math.abs(hz - q.Z) <= 95 and t < bt then best, bt, bh = o, t, hz - q.Z end
        end)
    end
    if not best then log(string.format("SHOT seq=%d hit=nenhum (testados=%d)", v[1], n)); return end
    local sid = best:GetFullName():match("[^%.]+$")
    local h0, h1 = -1, -1
    pcall(function() h0 = best:GetHP() end)
    local bone = bh > 60 and "jnt_head" or "None"
    local okD, eD
    if #v >= 12 then
        -- v26: dano da arma. ReceiveDamageStruct com DamageSource=Bullet faz o jogo aplicar a armadura (medido: Viper 19*0.45, AP 1 -> 11.455; bala nativa 11.453),
        -- mas ignora o osso; o coeficiente de osso (cabeca 6.0, pernas 0.7 = o que ReceiveDamage aplica nestes NPCs) entra aqui. Queda linear com a distancia: NAO medida entre os extremos.
        local ap, eff, drop, minmod = v[9], v[10], v[11], v[12]
        local fall = 1.0
        if bt >= eff + drop then fall = minmod elseif bt > eff and drop > 0 then fall = 1.0 - (1.0 - minmod) * (bt - eff) / drop end
        local coef = 1.0
        if bh > 60 then coef = 6.0 elseif bh < -10 then coef = 0.7; bone = "jnt_l_leg" end
        dmg = dmg * fall * coef * (v[13] or 1.0)  -- v[13] = multiplicador de dificuldade de quem atirou (v27)
        okD, eD = pcall(function() best:ReceiveDamageStruct({Damage = dmg, ArmorDamage = 0.0, ArmorPiercing = ap, Bleeding = 0.0, BleedingChance = 0.0, BoneHit = FName(bone), DamageType = 6, DamageSource = 2}) end)
    else
        okD, eD = pcall(function() best:ReceiveDamage(dmg + 0.0, 6, 0.0, 0.0, 0.0, 0.0, FName(bone)) end)
    end
    pcall(function() h1 = best:GetHP() end)
    log(string.format("SHOT seq=%d hit=%s dist=%.1fm altura=%.0f osso=%s dano=%.1f hp %.1f -> %.1f ok=%s %s", v[1], sid, bt / 100, bh, bone, dmg, h0, h1, tostring(okD), okD and "" or tostring(eD)))
end
local function npcDmgTest(pawn, p)
    npcDmgDone = FL.npcdmg
    local best, bd = nil, 1e18
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        pcall(function()
            if not o:IsValid() or o == pawn then return end
            if not o:GetFullName():find("BP_AI_Human_C", 1, true) then return end
            local q = o:K2_GetActorLocation()
            local d = (q.X-p.X)^2 + (q.Y-p.Y)^2
            if d < bd and o:IsAlive() then best, bd = o, d end
        end)
    end
    if not best then log("NPCDMG: nenhum NPC humano vivo"); return end
    local sid = best:GetFullName():match("[^%.]+$")
    local function vit() local h, m, a = "?", "?", "?"; pcall(function() h = string.format("%.1f", best:GetHP()) end); pcall(function() m = string.format("%.1f", best:GetMaxHP()) end); pcall(function() a = tostring(best:IsAlive()) end); return "hp=" .. h .. " max=" .. m .. " vivo=" .. a end
    log(string.format("NPCDMG: alvo %s a %.1f m ANTES %s dano=%s tipo=%s", sid, math.sqrt(bd) / 100, vit(), tostring(FL.npcdmg), tostring(FL.npcdmgtype)))
    local okD, eD = pcall(function() best:ReceiveDamage(FL.npcdmg + 0.0, FL.npcdmgtype, 0.0, 0.0, 0.0, 0.0, FName("None")) end)
    log("NPCDMG: ReceiveDamage ok=" .. tostring(okD) .. " " .. tostring(eD) .. " LOGO_DEPOIS " .. vit())
    ExecuteInGameThreadWithDelay(2000, function()
        local okV, eV = pcall(function() if best:IsValid() then log("NPCDMG: 2 s depois " .. vit() .. " alvo=" .. sid) else log("NPCDMG: 2 s depois alvo invalido " .. sid) end end)
        if not okV then log("NPCDMG: erro leitura " .. tostring(eV)) end
    end)
end
-- ===== PROTOCOLO v2 (snapshot) =====
-- out.json: {"v":2,"sid":"<sessao>","f":<passo>, cabecalho de sempre, "need":"h1,h2", "apps":[...], "npcs":[...]}
--   npcs[i] = {"id","x","y","z","yaw","a":"<hash da aparencia>","s":<n. da pose>,"pose","wp"}        (todo passo)
--   apps[i] = {"a":"<hash>","mesh","parts","mats","statics"}   (so quando a aparencia e nova ou o outro lado pediu em "need")
-- O hash e do CONTEUDO (mesh+partes+materiais+estaticos): aparencia igual = hash igual, mesmo depois de relancar qualquer lado.
-- Quem recebe guarda as aparencias por hash e pede em "need" as que nao tem; o pedido se repete a cada passo ate chegar (perda de pacote
-- nao precisa de confirmacao). "v" diferente (ou ausente) no snapshot remoto: tudo ignorado, com erro no log e em state.json (proto_err).
-- Leitura das poses: lista dos pawns de perto refeita a cada 2 s (1 FindAllOf); a cada passo le ate scanper NPCs em rodizio,
-- parando quando passa de scanms milissegundos. Sempre revalida o pawn por nome antes de tocar nele.
local P2 = {VER = 2, apps = {}, rapps = {}, need = {}, rneed = {}, near = {}, bad = {}, listAt = -10, idx = 0, pend = 0, posed = 0, skipped = 0,
            st = {reads = 0, ms = 0, maxms = 0, logAt = 0, since = 0}}
-- b136head2 (flag headfix, default 1; CLIENT mirrors). User 2026-10-15: "npcs with headgear get their head missing on client" (18:57 all 5
-- bandit mirrors headless). Measured: the NPC face (SK_fac_*, skeleton SKEL_fac: root_head > chest > neck > head > 117 face bones) has NO
-- leader on the host: AnimBP_Fac (CopyAnotherSkeletonBone_GSC) drives chest/neck/head from the body's jnt_spine_03/jnt_neck/jnt_head, and
-- masks/helmets/glasses made for the face lead the FACE (R0 b133: lead=SK_fac_*). SKEL_fac shares NO bone name with the body (jnt_*), so a
-- mirror part led by MAIN mapped nothing and was not drawn on the body. Rule for every part of every mirror: root bone not in MAIN = foreign
-- skeleton; per root name the /_fac/ part (else the first) is the DRIVER (no leader; at every MAIN pose its P2.HCOPY bones get MAIN's bone
-- in component space times the ref-pose offset, so it moves rigidly with the body head), the others lead the driver. No pair = old way.
P2.HCOPY = {{"jnt_spine_03", "chest"}, {"jnt_neck", "neck"}, {"jnt_head", "head"}}   -- gen_head2_table.py (AnimBP_Fac + same ref spot); parent first
P2.hfx = {n = 0, err = 0}
function P2.headSort(m, pc, pth)          -- true = pc kept aside for headWire (its root bone is not a bone of MAIN)
    if (FL.headfix or 1) ~= 1 then return false end
    local root
    local ok = pcall(function() local r = pc:GetBoneName(0); if m.comp:GetBoneIndex(r) < 0 then root = r:ToString() end end)
    if not (ok and root) then return false end
    m.hg = m.hg or {}
    local g
    for _, x in ipairs(m.hg) do if x.n == root then g = x end end
    if not g then g = {n = root, l = {}}; m.hg[#m.hg + 1] = g end
    if tostring(pth):find("/_fac/", 1, true) and not g.fac then table.insert(g.l, 1, pc); g.fac = true else g.l[#g.l + 1] = pc end
    return true
end
function P2.tcopy(t)                      -- plain Lua copy of an FTransform return value (kept across calls)
    local r, l, k = t.Rotation, t.Translation, t.Scale3D
    return {Rotation = {X = r.X, Y = r.Y, Z = r.Z, W = r.W}, Translation = {X = l.X, Y = l.Y, Z = l.Z}, Scale3D = {X = k.X, Y = k.Y, Z = k.Z}}
end
function P2.headWire(m, id)               -- after the parts loop (MAIN and parts still in their ref pose): driver + followers per foreign root
    local gs = m.hg; m.hg = nil
    if not gs then return end
    local kml = StaticFindObject("/Script/Engine.Default__KismetMathLibrary")
    if not (kml and kml:IsValid()) then kml = nil end
    P2.kml = kml
    m.parts = m.parts or {}; m.pl = m.pl or {}
    for _, g in ipairs(gs) do
        local d, prs = g.l[1], {}
        for _, e in ipairs(P2.HCOPY) do
            local okE, eE = pcall(function()
                local sb, tb = FName(e[1]), FName(e[2])
                if kml and m.comp:GetBoneIndex(sb) >= 0 and d:GetBoneIndex(tb) >= 0 then   -- offset = driver ref * inverse(MAIN ref), component space
                    prs[#prs + 1] = {s = sb, t = tb, o = P2.tcopy(kml:MakeRelativeTransform(d:GetBoneTransformByName(tb, 1), m.comp:GetBoneTransformByName(sb, 1)))}
                end
            end)
            if not okE then P2.hfx.err = P2.hfx.err + 1; if P2.hfx.err <= 5 then log("MIRROR cabeca erro " .. tostring(id) .. ": " .. tostring(eE)) end end
        end
        if #prs == 0 then                     -- unknown skeleton: as before (lead MAIN)
            for _, pc in ipairs(g.l) do pcall(function() pc:SetLeaderPoseComponent(m.comp, true, true) end); m.parts[#m.parts + 1] = pc; m.pl[#m.parts] = m.comp end
            log("MIRROR cabeca " .. tostring(id) .. ": raiz " .. g.n .. " sem par de ossos com o corpo; " .. #g.l .. " parte(s) seguem o corpo como antes")
        else
            m.hd = m.hd or {}
            m.hd[#m.hd + 1] = {c = d, p = prs}
            for k = 2, #g.l do local pc = g.l[k]; pcall(function() pc:SetLeaderPoseComponent(d, true, true) end); m.parts[#m.parts + 1] = pc; m.pl[#m.parts] = d end
            P2.hfx.n = P2.hfx.n + 1
            if P2.hfx.n <= 30 or P2.hfx.n % 50 == 0 then
                local dn = "?"; pcall(function() dn = d:GetSkeletalMeshAsset():GetFName():ToString() end)
                log("MIRROR cabeca " .. tostring(id) .. ": raiz " .. g.n .. " guia=" .. dn .. " ossos=" .. #prs .. " seguem=" .. (#g.l - 1) .. " #" .. P2.hfx.n)
            end
        end
    end
end
function P2.headPose(m)                   -- after every MAIN pose: driver chest/neck/head = offset * MAIN bone (component space), parent first
    local kml = P2.kml
    if not (kml and kml:IsValid()) then return end
    for _, h in ipairs(m.hd) do
        local ok, e = pcall(function()
            if not h.c:IsValid() then return end
            for _, p in ipairs(h.p) do h.c:SetBoneTransformByName(p.t, kml:ComposeTransforms(p.o, m.comp:GetBoneTransformByName(p.s, 1)), 1) end
        end)
        if not ok then P2.hfx.err = P2.hfx.err + 1; if P2.hfx.err <= 5 then log("MIRROR cabeca pose erro: " .. tostring(e)) end end
    end
end
-- b144combat (flag wpnhand, default 1; user 2026-10-15 "the weapons doesnt show in client too, the enemies is without weapon"). v25c placed the
-- weapon part of an NPC mirror at a WORLD offset from the pawn each snapshot (never seen on NPCs, live 20:56: soquete=1, erros=0, no weapon).
-- wpnhand=1: the host sends the weapon transform RELATIVE to the body's jnt_r_hand bone ("R:x,y,z,qx,qy,qz,qw"); the client attaches each
-- weapon part ONCE to its mirror's jnt_r_hand (the mirror's hand is right: bones get the host's world rotations) and sets that relative
-- transform when it changes, so the gun follows the hand with every pose and with the nsmooth mover. Switch / holster / drop: unchanged
-- (scanLook re-scans on a new WeaponInHandsMesh -> new appearance -> mirror recreated; corpses have none). No jnt_r_hand -> the old wp.
P2.wpn = {set = 0, old = 0, fail = 0, at = 0}
function P2.wpRel(mc, w)                  -- HOST: weapon component relative to the body's right hand -> "R:..." (nil = keep the old wp)
    local kml = P2.kml
    if not (kml and kml:IsValid()) then kml = StaticFindObject("/Script/Engine.Default__KismetMathLibrary"); P2.kml = kml end
    if not (kml and kml:IsValid()) then return nil end
    local hb = FName("jnt_r_hand")
    if mc:GetBoneIndex(hb) < 0 then return nil end
    local R = kml:MakeRelativeTransform(w:K2_GetComponentToWorld(), mc:GetSocketTransform(hb, 0))
    local l, r = R.Translation, R.Rotation
    return string.format("R:%.2f,%.2f,%.2f,%.4f,%.4f,%.4f,%.4f", l.X, l.Y, l.Z, r.X, r.Y, r.Z, r.W)
end
function P2.wpApply(m, wp)                -- CLIENT: "R:..." -> weapon parts attached once to the mirror's hand, relative transform on change
    if m.wpS == wp then return true end
    local v = {}
    for num in wp:sub(3):gmatch("[^,]+") do v[#v + 1] = tonumber(num) end
    if #v ~= 7 then return false end
    local hb = FName("jnt_r_hand")
    if m.comp:GetBoneIndex(hb) < 0 then return false end
    local T = {Translation = {X = v[1], Y = v[2], Z = v[3]}, Rotation = {X = v[4], Y = v[5], Z = v[6], W = v[7]}, Scale3D = {X = 1, Y = 1, Z = 1}}
    for _, sc in ipairs(m.socks) do
        if sc:IsValid() then
            if not m.wpA then sc:K2_AttachToComponent(m.comp, hb, 0, 0, 0, false) end
            sc:K2_SetRelativeTransform(T, false, {}, true)
        end
    end
    m.wpA = true; m.wpS = wp
    P2.wpn.set = P2.wpn.set + 1
    return true
end
function P2.wpDiag(m, id)                 -- CLIENT: one line every 10 s (weapon parts seen, visible, distance gun -> hand)
    local now = os.clock()
    if now - P2.wpn.at < 10 then return end
    P2.wpn.at = now
    local vis, d = "?", -1
    pcall(function()
        local sc = m.socks[1]
        vis = tostring(sc:IsVisible())
        local a, b = sc:K2_GetComponentLocation(), m.comp:GetSocketLocation(FName("jnt_r_hand"))
        d = math.sqrt((a.X - b.X) ^ 2 + (a.Y - b.Y) ^ 2 + (a.Z - b.Z) ^ 2)
    end)
    log(string.format("MIRROR arma 10 s: na_mao=%d (falhas %d) caminho_antigo=%d | %s: partes=%d anexada=%s visivel=%s arma_mao=%.0f cm",
        P2.wpn.set, P2.wpn.fail, P2.wpn.old, tostring(id), #m.socks, tostring(m.wpA == true), vis, d))
end
function P2.hash(str)  -- 32 bits em hexadecimal (djb2); so o host calcula, o cliente so compara
    local h = 5381
    for k = 1, #str do h = (h * 33 + str:byte(k)) & 0xFFFFFFFF end
    return string.format("%08x", h)
end
-- le UM pawn da lista de perto: posicao, pose (rotacao mundial de cada osso, graus x10) e aparencia (hash). true = lido
function P2.readOne(e, pawn, p)
    local o = e.o
    if not (o and o:IsValid()) then return false end
    passo("scan: revalidar por nome")
    -- v33: StaticFindObject(<caminho de ator do nivel>) custa 21-25 ms por chamada (medido); a revalidacao agora e "esta na lista viva de
    -- FindAllOf('Pawn')" (0,2 ms, refeita no maximo 1 vez por passo) + mesmo nome completo.
    if not S2_isLivePawn(o) or (o:GetFullName():gsub("^%S+ ", "")) ~= e.path then return false end
    passo("scan: K2_GetActorLocation")
    local q = o:K2_GetActorLocation()
    local d2 = (q.X-p.X)^2 + (q.Y-p.Y)^2
    if d2 > FL.scanr^2 then return false end
    passo("scan: Mesh")
    local mc = o.Mesh
    if not (mc and mc:IsValid()) then return false end
    passo("scan: GetSkeletalMeshAsset")
    local asset = mc:GetSkeletalMeshAsset():GetFullName():gsub("^%S+ ", "")
    local pose = {}
    passo("scan: GetSocketRotation (ossos)")
    for _, n in ipairs(bonesOf(asset, mc)) do
        local r = mc:GetSocketRotation(n)
        pose[#pose + 1] = string.format("%.0f,%.0f,%.0f", r.Pitch * 10, r.Yaw * 10, r.Roll * 10)
    end
    local sid = e.sid
    local lparts, lmats, lstat = "", "", ""
    local young = (FL.scanage or 0) > 0 and (os.clock() - ((P2.seen or {})[e.path] or 0)) < FL.scanage   -- batch 53: no appearance scan of a fresh NPC
    if FL.look == 1 and not young then passo("scan: aparencia"); lparts, lmats, lstat = scanLook(o, mc, asset, sid) end
    lstat = lstat or ""
    local lc = (FL.look == 1) and lookCache[sid] or nil   -- scanLook troca esta tabela quando a aparencia muda: o hash e recalculado
    local h = lc and lc.hmesh == asset and lc.h or nil
    if not h then
        h = P2.hash(asset .. "\1" .. lparts .. "\1" .. lmats .. "\1" .. lstat)
        if lc then lc.h, lc.hmesh = h, asset end
    end
    local app = P2.apps[h]
    if not app then
        app = {sent = -100, js = string.format('{"a":"%s","mesh":"%s","parts":"%s","mats":"%s","statics":"%s"}', h, asset, lparts, lmats, lstat)}
        P2.apps[h] = app
    end
    app.used = os.clock()
    -- v25c: transform mundial da arma na mao relativo ao pawn
    local wp = ""
    pcall(function()
        local w = o:GetWeaponInHandsMeshComponent()
        if w and w:IsValid() then
            local wl, wr = w:K2_GetComponentLocation(), w:K2_GetComponentRotation()
            wp = string.format("%.1f,%.1f,%.1f,%.1f,%.1f,%.1f", wl.X - q.X, wl.Y - q.Y, wl.Z - q.Z, wr.Pitch, wr.Yaw, wr.Roll)
            if FL.wpnhand == 1 then wp = P2.wpRel(mc, w) or wp end   -- b144combat: relative to the right hand bone
        end
    end)
    local poseStr = table.concat(pose, ";")
    if (FL.hipz or 1) == 1 then pcall(function() local t = mc:GetSocketTransform(bonesOf(asset, mc)[2], 2).Translation; poseStr = poseStr .. string.format("|%.0f,%.0f,%.0f", t.X * 10, t.Y * 10, t.Z * 10) end) end   -- b151hips
    local c = scanCache[sid]
    local seq = c and c.s or 0
    if not c or c.pose ~= poseStr then seq = seq + 1 end   -- "s" so muda quando a pose muda: o outro lado nao reaplica 116 ossos iguais
    scanCache[sid] = {id = sid, t = os.clock(), d2 = d2, s = seq, pose = poseStr, a = h,
        js = string.format('{"id":"%s","x":%.1f,"y":%.1f,"z":%.1f,"yaw":%.1f,"a":"%s","s":%d,"pose":"%s","wp":"%s"}', sid, q.X, q.Y, q.Z, o:K2_GetActorRotation().Yaw, h, seq, poseStr, wp)}
    return true
end
-- aparencias que vao neste snapshot: novas (repetidas por 0,25 s) e as pedidas pelo outro lado (cada uma no maximo 1 vez por segundo)
function P2.appsOut()
    local now, out, done = os.clock(), {}, {}
    local function offer(h)
        local a = P2.apps[h]
        if a and not done[h] then
            local burst = now - a.sent < 0.25
            if not burst and #out < FL.appsper and (a.sent < 0 or (P2.rneed[h] and now - a.sent >= 1)) then a.sent = now; burst = true end
            if burst then done[h] = true; out[#out + 1] = a.js end
        end
    end
    if FL.scan == 1 and ROLE == "host" then for _, e in pairs(scanCache) do offer(e.a) end end
    for h, a in pairs(P2.apps) do
        if a.extra and now - (a.used or 0) < 2 then offer(h) end      -- texto de modulo (arma do jogador) em uso: vale nos dois papeis
        if now - (a.used or 0) > 120 then P2.apps[h] = nil end        -- sem uso ha 2 min: sai
    end
    return "[" .. table.concat(out, ",") .. "]"
end
-- Textos de modulos (s2core.lua) pelo mesmo esquema: o modulo chama lookPut(texto) todo passo e manda so o hash devolvido;
-- o texto viaja em "apps" uma vez (ou a pedido) como {"a":hash,"mesh":"@x","parts":texto}. Do outro lado lookGet(hash)
-- devolve o texto, ou nil enquanto ele nao chegou (e ja o pede em "need"). O texto nao pode ter aspas.
function P2.lookPut(text)
    local c = P2.putCache
    if not (c and c.text == text) then
        local h = P2.hash("@x\1" .. text)
        c = {text = text, h = h, app = P2.apps[h] or {sent = -100, extra = true, js = string.format('{"a":"%s","mesh":"@x","parts":"%s","mats":"","statics":""}', h, text)}}
        P2.putCache = c
    end
    c.app.used = os.clock()
    P2.apps[c.h] = c.app
    return c.h
end
function P2.lookGet(h)
    local a = P2.rapps[h]
    if a then a.at = os.clock(); return a.parts end
    P2.need[h] = os.clock()
end
-- hashes que este lado ainda nao tem (vistos em npcs do remoto no ultimo segundo)
function P2.needOut()
    local now, t = os.clock(), {}
    for h, at in pairs(P2.need) do
        if now - at > 1 or P2.rapps[h] then P2.need[h] = nil else t[#t + 1] = h end
    end
    table.sort(t)
    return table.concat(t, ",", 1, math.min(#t, 16))
end
function P2.scanWork(pawn, p, now, budget, per)   -- b143perf: list rebuild (every 2 s) + up to per pose reads, stopping past budget ms; returns the reads
    if now - P2.listAt >= 2 then
        P2.listAt = now
        passo("scan: FindAllOf Pawn")
        local list = FindAllOf("Pawn") or {}
        S2_pawnN = #list
        local near = {}
        for _, o in ipairs(list) do
            pcall(function()
                if not (o and o:IsValid()) or (o == pawn and FL.scanself ~= 1) or (o == avatar and not SCAN_AVATAR) or S2_coreCall("isOwnBody", false, o) then return end
                local q = o:K2_GetActorLocation()
                local d2 = (q.X-p.X)^2 + (q.Y-p.Y)^2
                if d2 > FL.scanr^2 then return end
                local full = o:GetFullName()
                local path = (full:gsub("^%S+ ", ""))
                if P2.bad[path] and now - P2.bad[path] < 10 then return end   -- leitura falhou ha pouco (sem Mesh, invalido): nao ocupa vaga
                P2.seen = P2.seen or {}; P2.seen[path] = P2.seen[path] or now   -- batch 53: first time this NPC entered the near list
                near[#near + 1] = {o = o, d2 = d2, path = path, sid = full:match("[^%.]+$")}
            end)
        end
        if FL.mb == 1 then P2.mbHys(near, now) end   -- b149hitch: incumbents keep their place (hk)
        table.sort(near, function(a, b) local ka, kb = a.hk or a.d2, b.hk or b.d2; if ka ~= kb then return ka < kb end; return a.sid < b.sid end)
        for k = #near, FL.maxnpcs + 1, -1 do near[k] = nil end
        P2.near = near
        if FL.mb == 1 then P2.mbInc(near, now) end
        for path, at in pairs(P2.bad) do if now - at >= 10 then P2.bad[path] = nil end end
        for path, at in pairs(P2.seen or {}) do if now - at >= 600 then P2.seen[path] = nil end end   -- batch 53
    end
    local near, t0, n = P2.near, os.clock(), 0
    local maxN = math.min(#near, math.max(1, per))
    while n < maxN do
        P2.idx = P2.idx % #near + 1
        local e = near[P2.idx]
        local ok, r = pcall(P2.readOne, e, pawn, p)
        if not ok then
            P2.bad[e.path] = now
            if not scanErrLogged then scanErrLogged = true; log("SCAN erro: " .. tostring(r)) end
        elseif not r then P2.bad[e.path] = now end
        n = n + 1
        if (os.clock() - t0) * 1000 >= budget then break end
    end
    local st, used = P2.st, (os.clock() - t0) * 1000
    st.reads, st.ms = st.reads + n, st.ms + used
    if used > st.maxms then st.maxms = used end
    return n
end
local function scanNpcs(pawn, p)
    local now = os.clock()
    if FL.pslot == 1 then P2.psArm(pawn, p) else P2.scanWork(pawn, p, now, FL.scanms, FL.scanper) end   -- b143perf: pslot=1 reads in the slots
    local near, st = P2.near, P2.st
    if now - st.logAt >= 10 then
        if st.reads > 0 then log(string.format("SCAN v2: perto=%d leituras/s=%.1f ms/leitura=%.2f max_ms_passo=%.1f (scanper=%s scanms=%s)", #near, st.reads / math.max(now - st.since, 0.1), st.ms / st.reads, st.maxms, tostring(FL.scanper), tostring(FL.scanms))) end
        st.reads, st.ms, st.maxms, st.logAt, st.since = 0, 0, 0, now, now
    end
    -- v16: os maxnpcs mais proximos, em ordem estavel; sem leitura ha 15 s = fora
    local parts, live = {}, {}
    for id, e in pairs(scanCache) do
        if now - e.t > 15 then scanCache[id] = nil; lookCache[id] = nil else live[#live + 1] = e end
    end
    table.sort(live, function(a, b) if a.d2 ~= b.d2 then return a.d2 < b.d2 end; return a.id < b.id end)
    for k = 1, math.min(#live, FL.maxnpcs) do parts[k] = live[k].js end
    S2_scanN = #parts
    return "[" .. table.concat(parts, ",") .. "]"
end
-- cria o espelho: Actor + PoseableMeshComponent com o mesh do NPC
local function newMirror(asset, loc, yaw)
    local a = UEHelpers.GetWorld():SpawnActor(StaticFindObject("/Script/Engine.Actor"), loc, {Pitch = 0, Yaw = yaw, Roll = 0})
    local c = a:AddComponentByClass(StaticFindObject("/Script/Engine.PoseableMeshComponent"), false, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
    c:SetSkinnedAssetAndUpdate(asset, true)
    return {actor = a, comp = c}
end
-- cria/move/pose/destroi espelhos conforme a lista recebida do host. v12 (roteiro b, NAO PROVADO): so com mirrors=1 no cliente (e scan=1 no host);
-- cada espelho em pcall proprio; so K2_SetActorLocation (K2_SetActorLocationAndRotation derrubou o cliente no avatar, 4baf), rotacao so com rotate=1;
-- no maximo 4 espelhos, so cria a ate maxdist do jogador local; LoadAsset se o mesh nao esta carregado; sem raios; batimento MIRROR vivo a cada 10 s.
-- b143perf (pslot=1, default): the heavy NPC parts of the bridge run in FL.pslotn one-shot slots BETWEEN two bridge runs (game thread,
-- ExecuteInGameThreadWithDelay at k*100/(n+1) ms), each slot stopping past FL.pslotms: host = pose reads (P2.scanWork, at most
-- FL.scanper per bridge run, ready for the next snapshot), client = bone sets of mirrors whose pose number changed (newest pose wins,
-- oldest waiting first; a NEW mirror is posed at once). Both stay within one bridge tick. pslot=0: all inside the bridge run (old path).
-- No ExecuteInGameThreadWithDelay of its own (C function) or 3 bridge runs whose slots never ran -> the work runs inside the bridge run.
P2.ps = {gen = 0, sched = 0, ran = 0, miss = 0, late = 0, errs = 0, runs = 0, inline = 0}
function P2.psArm(pawn, p) P2.ps.arm = {pawn = pawn, p = {X = p.X, Y = p.Y, Z = p.Z}} end
function P2.psAny()
    if FL.mb == 1 and next(P2.mb.jobs) then return true end   -- b149hitch: open builds need the slots
    for _, m in pairs(mirrors) do if m.pp then return true end end
    return false
end
function P2.poseSet(m, pose)                 -- b148pose: the old P2.poseNow body (116 SetBoneRotationByName + the b136 head); P2.poseNow = router
    if not (m.actor:IsValid() and m.comp:IsValid()) then return end
    local names, i = bonesOf(m.mesh, m.comp), 0
    for pp, py, pr in pose:gmatch("(%-?%d+),(%-?%d+),(%-?%d+)") do
        i = i + 1
        if names[i] then m.comp:SetBoneRotationByName(names[i], {Pitch = tonumber(pp) / 10, Yaw = tonumber(py) / 10, Roll = tonumber(pr) / 10}, 0) end
    end
    m.bones = i
    if m.hip and names[2] then pcall(function() m.comp:SetBoneLocationByName(names[2], m.hip, 1) end) end   -- b151hips
    if m.hd then P2.headPose(m) end   -- b136 (headfix)
    P2.posed = P2.posed + 1
end
function P2.psPose(budget)                   -- waiting poses, oldest first, until budget ms
    local t0 = os.clock()
    while true do
        local best
        for _, m in pairs(mirrors) do if m.pp and (not best or m.ppAt < best.ppAt) then best = m end end
        if not best then return end
        local pose = best.pp
        best.pp, best.ppAt = nil, nil
        local ok, e = pcall(P2.poseNow, best, pose)
        if not ok then P2.ps.errs = P2.ps.errs + 1; if P2.ps.errs <= 5 then log("PSLOT pose erro: " .. tostring(e)) end end
        if (os.clock() - t0) * 1000 >= budget then return end
    end
end
function P2.psWork(g, budget, poseBudget)
    local t0 = os.clock()
    local J = P2.ps.job
    if J and J.g == g and J.left > 0 and J.pawn:IsValid() then J.left = J.left - P2.scanWork(J.pawn, J.p, os.clock(), budget, J.left) end
    P2.psPose(poseBudget or budget)
    if FL.mb == 1 then P2.mbWork(t0) end   -- b149hitch: mirror build steps while the slot is under FL.mbms
end
function P2.psRun(k, g)                      -- one slot (one-shot on the game thread)
    local S = P2.ps
    if g ~= S.gen then S.late = S.late + 1; return end
    if not bridgeOn or staleGen() then return end
    local t0 = os.clock()
    S2.ffSeq = (S2.ffSeq or 0) + 1; S2.ffBody = S2.ffSeq
    S.ran = g; S.runs = S.runs + 1
    local ok, e = pcall(P2.psWork, g, FL.pslotms or 3)
    S2.ffBody = nil
    if not ok then S.errs = S.errs + 1; if S.errs <= 5 then log("PSLOT erro: " .. tostring(e)) end end
    profAdd("pslot", (os.clock() - t0) * 1000)
end
function P2.psKick()                         -- end of every bridge run: schedule this run's slots
    local S = P2.ps
    if S.sched > 0 and S.ran < S.sched then S.miss = S.miss + 1 elseif S.sched > 0 then S.miss = 0 end
    if S.miss >= 3 and not S.dead then S.dead = true; log("PSLOT: os slots nao rodaram em 3 passos seguidos -> leituras/poses de volta dentro da ponte") end
    S.gen = S.gen + 1
    local g = S.gen
    S.job = S.arm and {g = g, pawn = S.arm.pawn, p = S.arm.p, left = math.max(1, FL.scanper)} or nil
    S.arm = nil
    S.sched = 0
    if not (S.job or P2.psAny()) then return end
    local ex, dg = rawget(_G, "ExecuteInGameThreadWithDelay"), rawget(_G, "debug")
    local native = type(ex) == "function" and dg and dg.getinfo and dg.getinfo(ex, "S").what == "C"
    if not native or S.dead then
        if not native and not S.inLog then S.inLog = true; log("PSLOT: sem ExecuteInGameThreadWithDelay nativo -> leituras/poses dentro da ponte") end
        S.inline = S.inline + 1
        P2.psWork(g, FL.scanms, 1e9)
        return
    end
    local n = math.max(1, math.min(6, math.floor(FL.pslotn or 3)))
    S.sched = g
    for k = 1, n do ex(math.floor(k * 100 / (n + 1)), function() P2.psRun(k, g) end) end
end
-- ===== b148pose (flag pb, default 1; CLIENT NPC mirrors, every kind: humans and mutants). User 2026-10-15 21:47/22:00 "enemies move like
-- they are rollbacking" / "animations restarting". Measured 22:02-22:04 (rollback_probe.lua, per frame 3 s): every moving mirror had 4-8
-- frame-to-frame jnt_r_hand jumps of 20-37 cm = the pose is a stop-motion of snaps (host SCAN v2 reads ~5 poses/s per NPC; the client set
-- the bones once per new pose and held them). pb=1: a new pose of a mirror that is on screen and among the FL.pbn nearest is BLENDED: a
-- per-frame one-shot chain (ExecuteInGameThreadWithDelay, re-armed each frame only while a blend runs; no fast loop) slerps every bone's
-- LOCAL rotation from the pose shown to the new one over the measured interval between pose arrivals (EMA, FL.pbtmin..FL.pbtmax s), so
-- the newest pose is shown one interval after it arrived (never extrapolated). Host poses are WORLD rotations; local = inverse(parent
-- world) * world (parents by GetParentBone, once per mesh). Per frame: forward kinematics in Lua; a bone is set (world space, as before)
-- only when its local moved more than FL.pbeps deg since it was last set, the root every frame; still bones are never touched (children
-- follow in the engine). Location: the pose carries rotations only (bone offsets are the mesh's own); the actor is moved by the core
-- (b148 nsip). Cost: FL.pbms ms per frame (round robin), FL.pbn mirrors; everything else (far, off screen, over pbn, unknown parents,
-- chain not running) = the old path P2.poseSet (the old P2.poseNow body). pb=0 = old path.
P2.pb = {gen = 0, on = false, last = -10, rankAt = -10, list = {}, rr = 0, dead = false, restarts = 0,
         st = {at = 0, steps = 0, ms = 0, msmax = 0, sets = 0, loads = 0, arr = 0, direct = 0, cut = 0, errs = 0, tsum = 0, mir = 0}}
P2.pbPar = {}                                -- mesh path -> parent index per bone (0 = root), false = unsupported (old path)
function P2.qOf(p, y, r)                     -- FRotator::Quaternion (degrees) -> x, y, z, w
    local h = math.pi / 360
    local sp, cp, sy, cy, sr, cr = math.sin(p * h), math.cos(p * h), math.sin(y * h), math.cos(y * h), math.sin(r * h), math.cos(r * h)
    return cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy
end
function P2.nAx(a) a = a % 360; if a > 180 then a = a - 360 end; return a end
function P2.rOf(x, y, z, w)                  -- FQuat::Rotator -> pitch, yaw, roll (degrees)
    local s = z * x - w * y
    local yaw = math.deg(math.atan(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)))
    if s < -0.4999995 then return -90, yaw, P2.nAx(-yaw - 2 * math.deg(math.atan(x, w))) end
    if s > 0.4999995 then return 90, yaw, P2.nAx(yaw - 2 * math.deg(math.atan(x, w))) end
    return math.deg(math.asin(2 * s)), yaw, math.deg(math.atan(-2 * (w * x + y * z), 1 - 2 * (x * x + y * y)))
end
function P2.qMul(ax, ay, az, aw, bx, by, bz, bw)   -- FQuat A * B (B first, then A)
    return aw * bx + ax * bw + ay * bz - az * by, aw * by - ax * bz + ay * bw + az * bx,
           aw * bz + ax * by - ay * bx + az * bw, aw * bw - ax * bx - ay * by - az * bz
end
function P2.pbParOf(m, names)                -- parents of the mesh's bones (once per mesh); false = a parent after its child / unknown
    local P = P2.pbPar[m.mesh]
    if P ~= nil then return P end
    local idx, par = {}, {}
    for i, n in ipairs(names) do idx[n:ToString()] = i end
    P = par
    for i, n in ipairs(names) do
        local pn = m.comp:GetParentBone(n)
        local s = pn and pn:ToString() or "None"
        local j = idx[s]
        if j == nil then
            if s ~= "None" and s ~= "" then P = false; break end
            j = 0
        end
        if j >= i then P = false; break end
        par[i] = j
    end
    P2.pbPar[m.mesh] = P
    log("MIRROR pose: pais dos ossos de " .. tostring(m.mesh):match("[^/%.]+$") .. " " .. (P and (#names .. " ossos") or "NAO servem (caminho antigo)"))
    return P
end
function P2.pbParse(pose, n, par)            -- "p,y,r;..." world rotators (deg x10) -> flat local quats, nil when the count differs
    local W, L, i = {}, {}, 0
    for pp, py, pr in pose:gmatch("(%-?%d+),(%-?%d+),(%-?%d+)") do
        i = i + 1
        if i > n then return nil end
        local x, y, z, w = P2.qOf(tonumber(pp) / 10, tonumber(py) / 10, tonumber(pr) / 10)
        local k = 4 * i
        W[k - 3], W[k - 2], W[k - 1], W[k] = x, y, z, w
        local j = 4 * par[i]
        if j > 0 then x, y, z, w = P2.qMul(-W[j - 3], -W[j - 2], -W[j - 1], W[j], x, y, z, w) end
        L[k - 3], L[k - 2], L[k - 1], L[k] = x, y, z, w
    end
    if i ~= n then return nil end
    return L
end
function P2.pbLoad(m, pose, now)             -- a new pose -> blend from the locals shown (S.ls) to it; false = use the old path
    local names = bonesOf(m.mesh, m.comp)
    local par = P2.pbParOf(m, names)
    if not par then return false end
    local n = #names
    local S = m.pbS
    if not (S and S.n == n) then
        local L0 = m.pbStr and P2.pbParse(m.pbStr, n, par)   -- shown = the last pose the old path set
        if not L0 then return false end
        S = {n = n, ls = L0, w = {}}
        m.pbS = S
    end
    local L1 = P2.pbParse(pose, n, par)
    if not L1 then return false end
    local ls, l0, l1, th, si, mv, need, nl = S.ls, {}, {}, {}, {}, {}, {}, {}
    for i = 1, n do
        local k = 4 * i
        local ax, ay, az, aw = ls[k - 3], ls[k - 2], ls[k - 1], ls[k]
        local bx, by, bz, bw = L1[k - 3], L1[k - 2], L1[k - 1], L1[k]
        local d = ax * bx + ay * by + az * bz + aw * bw
        if d < 0 then d, bx, by, bz, bw = -d, -bx, -by, -bz, -bw end   -- shortest way
        if d > 1 then d = 1 end
        l0[k - 3], l0[k - 2], l0[k - 1], l0[k] = ax, ay, az, aw
        l1[k - 3], l1[k - 2], l1[k - 1], l1[k] = bx, by, bz, bw
        local t = math.acos(d)
        th[i], si[i] = t, math.sin(t)
        mv[i] = t > 0.0004                   -- ~0.05 deg
        need[i] = mv[i] or par[i] == 0
    end
    for i = n, 1, -1 do if need[i] and par[i] > 0 then need[par[i]] = true end end
    for i = 1, n do if need[i] then nl[#nl + 1] = i end end
    local T = math.min(math.max(m.pbT or 0.2, FL.pbtmin or 0.08), FL.pbtmax or 0.5)
    S.l0, S.l1, S.th, S.si, S.mv, S.nl, S.names, S.par, S.t0, S.T, S.fin = l0, l1, th, si, mv, nl, names, par, now, T, false
    m.pbStr = pose
    local st = P2.pb.st
    st.loads, st.tsum = st.loads + 1, st.tsum + T
    P2.posed = P2.posed + 1
    return true
end
function P2.pbBlend(m, S, now)               -- one frame of one mirror: slerp + FK, set the bones that moved more than FL.pbeps
    local f = (now - S.t0) / S.T
    if f >= 1 then f = 1 elseif f < 0 then f = 0 end
    local names, par, ls, l0, l1, th, si, mv, w = S.names, S.par, S.ls, S.l0, S.l1, S.th, S.si, S.mv, S.w
    local ceps = math.cos(math.rad(FL.pbeps or 0.3) / 2)
    local comp, sets = m.comp, 0
    for _, i in ipairs(S.nl) do
        local k = 4 * i
        local ex, ey, ez, ew = ls[k - 3], ls[k - 2], ls[k - 1], ls[k]
        local set = par[i] == 0
        if mv[i] then
            local bx, by, bz, bw
            if f >= 1 then bx, by, bz, bw = l1[k - 3], l1[k - 2], l1[k - 1], l1[k]
            else
                local a, b
                if si[i] > 0.001 then a, b = math.sin((1 - f) * th[i]) / si[i], math.sin(f * th[i]) / si[i] else a, b = 1 - f, f end
                bx, by, bz, bw = a * l0[k - 3] + b * l1[k - 3], a * l0[k - 2] + b * l1[k - 2], a * l0[k - 1] + b * l1[k - 1], a * l0[k] + b * l1[k]
                local q = math.sqrt(bx * bx + by * by + bz * bz + bw * bw)
                bx, by, bz, bw = bx / q, by / q, bz / q, bw / q
            end
            local d = math.abs(bx * ex + by * ey + bz * ez + bw * ew)
            if set or d < ceps or (f >= 1 and d < 0.9999999) then set = true; ex, ey, ez, ew = bx, by, bz, bw end
        end
        local p = par[i]
        local wx, wy, wz, ww = ex, ey, ez, ew
        if p > 0 then local j = 4 * p; wx, wy, wz, ww = P2.qMul(w[j - 3], w[j - 2], w[j - 1], w[j], ex, ey, ez, ew) end
        w[k - 3], w[k - 2], w[k - 1], w[k] = wx, wy, wz, ww
        if set then
            local rp, ry, rr = P2.rOf(wx, wy, wz, ww)
            comp:SetBoneRotationByName(names[i], {Pitch = rp, Yaw = ry, Roll = rr}, 0)
            ls[k - 3], ls[k - 2], ls[k - 1], ls[k] = ex, ey, ez, ew
            sets = sets + 1
        end
    end
    if m.hip and sets > 0 and names[2] then pcall(function() comp:SetBoneLocationByName(names[2], m.hip, 1) end) end   -- b151hips
    if f >= 1 then S.fin = true end
    if m.hd and sets > 0 and (S.fin or now - (S.hAt or -10) >= 1 / 30) then S.hAt = now; P2.headPose(m) end   -- b136 head, <= 30/s
    P2.pb.st.sets = P2.pb.st.sets + sets
end
function P2.poseDirect(m, pose)              -- the old path (bones set at once); the blend restarts from this pose
    m.pbS, m.pbStr, m.pbP = nil, pose, nil
    if FL.pb == 1 then P2.pb.st.direct = P2.pb.st.direct + 1 end
    return P2.poseSet(m, pose)
end
function P2.pbRank(now)                      -- every 0.25 s: the FL.pbn nearest mirrors on screen get m.pbOn
    local B = P2.pb
    B.rankAt = now
    local lp = S2 and S2.api and S2.api.getLocal and S2.api.getLocal()
    local C = {}
    for id, m in pairs(mirrors) do
        if type(m) == "table" then
            m.pbOn = false
            if m.bones and m.x and m.actor:IsValid() and m.comp:IsValid() and P2.pbPar[m.mesh] ~= false then
                local vis = true
                pcall(function() vis = m.actor:WasRecentlyRendered(0.2) end)
                if vis ~= false then C[#C + 1] = {id = id, m = m, d2 = (lp and lp.X) and ((m.x - lp.X) ^ 2 + (m.y - lp.Y) ^ 2) or 0} end
            end
        end
    end
    table.sort(C, function(a, b) return a.d2 < b.d2 end)
    local L = {}
    for k = 1, math.min(#C, math.max(0, math.floor(FL.pbn or 4))) do C[k].m.pbOn = true; L[k] = C[k] end
    B.list = L
    for _, m in pairs(mirrors) do if type(m) == "table" and not m.pbOn then P2.pbDrop(m) end end   -- dropped out: finish now
end
function P2.pbDrop(m)                        -- a waiting pose or an unfinished blend -> its target pose at once (old path)
    local p = m.pbP or (m.pbS and m.pbS.nl and not m.pbS.fin and m.pbStr)
    if p then m.pbP = nil; pcall(P2.poseDirect, m, p) end
end
function P2.pbFlush()                        -- chain ends / dead / pb=0: nothing stays half-way
    for _, m in pairs(mirrors) do if type(m) == "table" then P2.pbDrop(m) end end
end
function P2.pbMirror(m, now)                 -- true = this mirror needs more frames
    if m.pbP then
        local pose = m.pbP
        m.pbP = nil
        local ok, r = pcall(P2.pbLoad, m, pose, now)
        if not (ok and r) then
            if not ok then P2.pb.st.errs = P2.pb.st.errs + 1; if P2.pb.st.errs <= 5 then log("MIRROR pose erro (carga): " .. tostring(r)) end end
            P2.poseDirect(m, pose)
            return false
        end
    end
    local S = m.pbS
    if not (S and S.nl) or S.fin then return false end
    P2.pbBlend(m, S, now)
    return not S.fin
end
function P2.pbStep(t0)                       -- one frame: rank (0.25 s), then the mirrors round robin until FL.pbms ms
    local B = P2.pb
    local st = B.st
    if t0 - B.rankAt >= 0.25 then P2.pbRank(t0) end
    local L, more, nm = B.list, false, 0
    local n = #L
    if n > 0 then
        B.rr = B.rr % n + 1
        for c = 0, n - 1 do
            local e = L[(B.rr + c - 1) % n + 1]
            local m = e.m
            if mirrors[e.id] == m and m.actor:IsValid() and m.comp:IsValid() and (m.pbP or (m.pbS and m.pbS.nl and not m.pbS.fin)) then
                if nm > 0 and (os.clock() - t0) * 1000 >= (FL.pbms or 1.5) then more = true; st.cut = st.cut + 1; break end
                nm = nm + 1
                local ok, r = pcall(P2.pbMirror, m, os.clock())
                if not ok then
                    st.errs = st.errs + 1; m.pbS = nil
                    if st.errs <= 5 then log("MIRROR pose erro: " .. tostring(r)) end
                elseif r then more = true end
            end
        end
    end
    if nm > st.mir then st.mir = nm end
    return more
end
function P2.pbLog(now)                       -- 10 s line
    local B, st = P2.pb, P2.pb.st
    if now - st.at < 10 then return end
    if st.steps > 0 or st.direct > 0 then
        log(string.format("MIRROR pose 10 s (pb=%s): quadros=%d (%.0f/s) espelhos_max=%d chegadas=%d misturas=%d diretas=%d ossos/quadro=%.1f ms_med=%.2f ms_max=%.1f corte=%d intervalo_med=%.0f ms erros=%d reinicios=%d%s",
            tostring(FL.pb), st.steps, st.steps / math.max(0.1, now - st.at), st.mir, st.arr, st.loads, st.direct, st.sets / math.max(1, st.steps),
            st.ms / math.max(1, st.steps), st.msmax, st.cut, st.tsum / math.max(1, st.loads) * 1000, st.errs, B.restarts, B.dead and " MORTO" or ""))
    end
    B.st = {at = now, steps = 0, ms = 0, msmax = 0, sets = 0, loads = 0, arr = 0, direct = 0, cut = 0, errs = 0, tsum = 0, mir = 0}
    if B.restarts > 0 and not B.dead then B.restarts = B.restarts - 1 end
end
function P2.pbRun(g)                         -- one link of the chain (game thread, once per frame)
    local B = P2.pb
    if g ~= B.gen then return end
    local t0 = os.clock()
    B.last = t0
    if FL.pb ~= 1 or B.dead or not bridgeOn or staleGen() then B.on = false; pcall(P2.pbFlush); return end
    local ok, more = pcall(P2.pbStep, t0)
    local st = B.st
    if not ok then st.errs = st.errs + 1; if st.errs <= 5 then log("MIRROR pose erro (passo): " .. tostring(more)) end; more = true end
    local ms = (os.clock() - t0) * 1000
    st.steps, st.ms = st.steps + 1, st.ms + ms
    if ms > st.msmax then st.msmax = ms end
    profAdd("pb", ms)
    if st.errs > 50 then B.dead = true; log("MIRROR pose: mais de 50 erros em 10 s -> poses como antes (pb desligado ate reiniciar)") end
    pcall(P2.pbLog, t0)
    if more and not B.dead then
        local ex = rawget(_G, "ExecuteInGameThreadWithDelay")
        local okE = pcall(ex, 1, function() P2.pbRun(g) end)
        if okE then return end
    end
    B.on = false
    if B.dead then pcall(P2.pbFlush) end
end
function P2.pbKick(now)                      -- true = the chain runs (started now or still alive)
    local B = P2.pb
    if B.dead then return false end
    if B.on and now - B.last < 0.3 then return true end
    local ex, dg = rawget(_G, "ExecuteInGameThreadWithDelay"), rawget(_G, "debug")
    local native = type(ex) == "function" and dg and dg.getinfo and dg.getinfo(ex, "S").what == "C"
    if not native then B.dead = true; log("MIRROR pose: sem ExecuteInGameThreadWithDelay nativo -> poses como antes"); return false end
    if B.on then                             -- the chain stopped without ending: restart (3 = dead)
        B.restarts = B.restarts + 1
        if B.restarts >= 3 then B.dead = true; B.on = false; pcall(P2.pbFlush); log("MIRROR pose: a corrente por quadro parou 3 vezes -> poses como antes"); return false end
    end
    B.gen = B.gen + 1
    local g = B.gen
    B.on, B.last = true, now
    if S2 then S2.pb = B end                 -- probes read S2.pb
    local ok = pcall(ex, 1, function() P2.pbRun(g) end)
    if not ok then B.on = false end
    return ok
end
function P2.poseNow(m, pose)                 -- b148pose router (called by applyMirrors / the pslot slots for a new pose number)
    local hz; pose = pose:gsub("|(%-?%d+),(%-?%d+),(%-?%d+)$", function(a, b, c) hz = {X = tonumber(a) / 10, Y = tonumber(b) / 10, Z = tonumber(c) / 10}; return "" end)   -- b151hips
    m.hip = (FL.hipz or 1) == 1 and hz or nil
    local now = os.clock()
    if m.pbAt then
        local g = now - m.pbAt
        if g > 0.03 and g < 1 then m.pbT = m.pbT and (m.pbT * 0.7 + g * 0.3) or g end
    end
    m.pbAt = now
    local B = P2.pb
    if FL.pb == 1 and m.bones and not B.dead then
        if now - B.rankAt >= 0.25 then pcall(P2.pbRank, now) end
        if m.pbOn and P2.pbPar[m.mesh] ~= false and P2.pbKick(now) then
            m.pbP = pose                     -- newest wins; the chain loads it next frame
            B.st.arr = B.st.arr + 1
            return
        end
    end
    return P2.poseDirect(m, pose)
end
-- ===== end b148pose (main)
local mirSlot = -1
local mirStat = {made = 0, errs = 0, lastErr = "-", logAt = 0, recv = 0}
-- ===== b149hitch (main; flag mb, default 1; CLIENT NPC mirrors of every kind + the HOST near list). User 2026-10-15 22:05 "the client
-- sometimes get stuters in fps". Measured 22:06 on the client: frames of 365 / 405 / 788 / 324 / 150 ms; b143 profiler: the worst bridge
-- run (774 ms) was mirror CREATION: StaticFindObject of the mesh 546 ms + parts / materials 9-98 ms each (avg 9-39 ms per call), and the
-- mirrors were created over and over (criados=145): every new NPC twice (the host sends a fresh NPC "young" = no parts for scanage 3 s,
-- then the full look -> "MIRROR recria"), a weapon switch = a full rebuild, the nearest-N set churns and a mirror that left was destroyed.
-- mb=1:
--  * every asset path of a build (mesh, parts, materials, statics, classes) is looked up ONCE per session (P2.asset: IsValid + same class
--    before use; a miss is kept 10 s), at most FL.mblook uncached lookups per slot;
--  * a mirror is built by a JOB in the b143 pslot slots (FL.mbms ms per slot, at least one step): actor + main mesh (hidden, no
--    collision), main materials, one part per step, head wiring (b136), one static per step, then shown in one step with the newest
--    position / pose / gun; a new look of a shown mirror is built the same way while the old one stays on screen, then swapped;
--  * a mirror that leaves the snapshot (or is swapped out) is HIDDEN and kept FL.mbhold s (at most FL.mbpark): the same NPC with the same
--    look comes back at once (no build); a look with no parts/materials/statics (the young copy) is never kept;
--  * HOST (main's own near list, mslot=0; the core does the same for mslot=1): an NPC already in the set keeps its place for FL.mbstay s
--    and then unless a newcomer is FL.mbhys cm nearer.
-- mb=0: the old code path (created inside the bridge run, destroyed when it leaves).
P2.mb = {jobs = {}, park = {}, fail = {}, ac = {}, inc = {}, seq = 0, look = 0, logAt = 0,
         st = {at = 0, jobs = 0, done = 0, cancel = 0, errs = 0, reuse = 0, park = 0, kill = 0, look = 0, lookMs = 0, lookMax = 0, lookP = "-",
               step = 0, stepMax = 0, stepK = "-", slotMax = 0, hit = 0, k = {}}}
function P2.asset(path)                   -- b149: asset / class by path, looked up once per session (nil = not available)
    local B, now = P2.mb, os.clock()
    local e = B.ac[path]
    if e then
        if e.o then
            local ok = false
            pcall(function() ok = e.o:IsValid() and e.o:GetClass():GetAddress() == e.ca end)
            if ok then B.st.hit = B.st.hit + 1; return e.o end
        elseif now - e.at < 10 then return nil end
        B.ac[path] = nil
    end
    return P2.assetFind(path) or P2.assetLoad(path)
end
function P2.assetKeep(path, o, t0)        -- after a lookup: keep the object (or a miss), count the time
    local B, ca = P2.mb, nil
    local ms, st = (os.clock() - t0) * 1000, P2.mb.st
    st.look, st.lookMs = st.look + 1, st.lookMs + ms
    if ms > st.lookMax then st.lookMax, st.lookP = ms, tostring(path):match("[^/]+$") or "?" end
    if o then pcall(function() ca = o:GetClass():GetAddress() end) end
    if o and ca then B.ac[path] = {o = o, ca = ca} elseif o == nil then B.ac[path] = {at = os.clock()} end
    return o
end
function P2.assetFind(path)               -- StaticFindObject only (loaded already); nil = not loaded (nothing kept yet)
    local t0, o = os.clock(), nil
    pcall(function() o = StaticFindObject(path) end)
    if not (o and o:IsValid()) then P2.assetKeep(path, false, t0); return nil end
    return P2.assetKeep(path, o, t0)
end
function P2.assetLoad(path)               -- LoadAsset (a synchronous load when it is not loaded); nil = not available, kept 10 s
    local t0, o = os.clock(), nil
    pcall(function() o = LoadAsset(path) end)
    if not (o and o:IsValid()) then o = nil end
    return P2.assetKeep(path, o, t0)
end
function P2.acHas(path)                   -- true = a lookup of this path costs nothing now
    local e = P2.mb.ac[path]
    if not e then return false end
    if e.o then local ok = false; pcall(function() ok = e.o:IsValid() end); return ok end
    return os.clock() - e.at < 10
end
function P2.mbMats(comp, list, sep)       -- applyMats with the cached lookups
    local n, i = 0, 0
    for mp in (list .. sep):gmatch("(.-)" .. sep) do
        if mp ~= "" then pcall(function()
            local mo = P2.asset(mp)
            if mo then comp:SetMaterial(i, mo); n = n + 1 end
        end) end
        i = i + 1
    end
    return n
end
function P2.mbJob(id, pa, app, old)       -- a build job for (id, look pa); the paths it needs, in order
    local B = P2.mb
    B.seq = B.seq + 1
    local J = {id = id, pa = pa, n = B.seq, t0 = os.clock(), mesh = app.mesh, lparts = app.parts or "", lmats = app.mats or "", lstat = app.statics or "",
               ph = 1, ni = 1, need = {}, parts = {}, slot = old and old.slot or nil, steps = 0, ms = 0, max = 0}
    J.bare = J.lparts == "" and J.lmats == "" and J.lstat == ""
    local need, had = J.need, {}
    local function add(p) if p and p ~= "" and not had[p] then had[p] = true; need[#need + 1] = p end end
    add(J.mesh); add("/Script/Engine.Actor"); add("/Script/Engine.PoseableMeshComponent")
    for mp in (J.lmats .. "|"):gmatch("(.-)|") do add(mp) end
    for ent in J.lparts:gmatch("[^|]+") do
        J.parts[#J.parts + 1] = ent
        local pth, pm, _, psock = ent:match("^(.-)@(.-)@([^@]*)@?(.*)$")
        if not pth then pth, pm, psock = ent, "", "" end
        add(pth)
        if psock ~= "" and (FL.wpnskel or 0) == 1 then add("/Script/Engine.SkeletalMeshComponent") end
        for mp in (pm .. ","):gmatch("(.-),") do add(mp) end
    end
    for ent in J.lstat:gmatch("[^|]+") do
        local spath = select(4, ent:match("^(.-)>(.-)>(.-)>(.-)>(.*)$"))
        if spath then add("/Script/Engine.StaticMeshComponent"); add(spath) end
    end
    B.st.jobs = B.st.jobs + 1
    return J
end
function P2.mbStill(m)                    -- a kept mirror stays where it is (the core's movers keep their last speed otherwise)
    pcall(function()
        local c = P2.asset("/Script/Engine.ProjectileMovementComponent")
        local pm = c and m.actor:GetComponentByClass(c)
        if pm and pm:IsValid() then pm:StopMovementImmediately() end
    end)
    pcall(function()
        local c = P2.asset("/Script/Engine.RotatingMovementComponent")
        local rm = c and m.actor:GetComponentByClass(c)
        if rm and rm:IsValid() then rm.RotationRate = {Pitch = 0, Yaw = 0, Roll = 0} end
    end)
end
function P2.mbKill(m)
    pcall(function() if m.actor:IsValid() then m.actor:K2_DestroyActor() end end)
    P2.mb.st.kill = P2.mb.st.kill + 1
end
function P2.mbRetire(id, m)               -- a mirror off screen for good: hidden and kept (same look comes back at once), or destroyed
    local B, now = P2.mb, os.clock()
    m.pp, m.ppAt, m.pbP, m.pbOn, m.mbShow = nil, nil, nil, false, nil
    local okA = false
    pcall(function() okA = m.actor:IsValid() end)
    if not okA or m.bare or (FL.mbhold or 0) <= 0 or (FL.mbpark or 0) <= 0 then P2.mbKill(m); return end
    pcall(function() m.actor:SetActorHiddenInGame(true); m.actor:SetActorEnableCollision(false) end)
    P2.mbStill(m)
    local P = B.park[id]
    if P and P.m ~= m then P2.mbKill(P.m) end
    B.park[id] = {m = m, a = m.a, at = now}
    B.st.park = B.st.park + 1
    local n, oldId, oldAt = 0, nil, 1e18
    for pid, q in pairs(B.park) do n = n + 1; if q.at < oldAt then oldId, oldAt = pid, q.at end end
    if n > (FL.mbpark or 8) and oldId then P2.mbKill(B.park[oldId].m); B.park[oldId] = nil end
end
function P2.mbShow(m)                     -- after its first pose: on screen
    m.mbShow = nil
    pcall(function() m.actor:SetActorEnableCollision(true); m.actor:SetActorHiddenInGame(false) end)
end
function P2.mbCancel(J, why)
    local B = P2.mb
    if B.jobs[J.id] == J then B.jobs[J.id] = nil end
    if J.m then P2.mbKill(J.m); J.m = nil end
    B.st.cancel = B.st.cancel + 1
    if B.st.cancel <= 20 then log("MIRROR obra " .. tostring(J.id) .. " cancelada (" .. why .. ")") end
end
function P2.mbPre(id, m, pa, app, loc, yaw, ps, pose, wp, lp)   -- applyMirrors (mb=1): the mirror to move / pose now, or nil
    local B, now = P2.mb, os.clock()
    local J = B.jobs[id]
    local raw = {X = loc.X - ((m and m.slot) or 0) * FL.mirrorspread, Y = loc.Y, Z = loc.Z}   -- loc already has the shown mirror's spread
    if m then
        local okA = false
        pcall(function() okA = m.actor:IsValid() end)
        if not okA then mirrors[id] = nil; m = nil end
    end
    if m and (m.a == pa or not app) then              -- the shown mirror fits (or the new look has not arrived yet)
        if J and m.a == pa then P2.mbCancel(J, "aparencia voltou") end
        return m
    end
    if not app then return nil end
    local P = B.park[id]
    if P and P.a == pa then                           -- kept mirror, same look: back at once
        B.park[id] = nil
        local okP = false
        pcall(function() okP = P.m.actor:IsValid() end)
        if okP then
            local pm = P.m
            if J then P2.mbCancel(J, "guardado voltou") end
            if m then mirrors[id] = nil; P2.mbRetire(id, m) end
            pm.s, pm.bones, pm.pbS, pm.pbP, pm.pbStr, pm.pbAt, pm.pp, pm.ppAt, pm.mbShow = nil, nil, nil, nil, nil, nil, nil, nil, true
            loc.X = raw.X + (pm.slot or 0) * FL.mirrorspread
            mirrors[id] = pm
            B.st.reuse = B.st.reuse + 1
            log("MIRROR +" .. id .. " (guardado, sem obra)")
            return pm
        end
    end
    if not m and lp and FL.maxdist > 0 and ((raw.X - lp.X) ^ 2 + (raw.Y - lp.Y) ^ 2) > FL.maxdist ^ 2 then
        if J then P2.mbCancel(J, "longe") end
        return nil
    end
    if J and J.pa ~= pa then P2.mbCancel(J, "aparencia mudou"); J = nil end
    if not J then
        local f = B.fail[id .. "\1" .. pa]
        if f and now - f < 10 then return m end
        J = P2.mbJob(id, pa, app, m)
        B.jobs[id] = J
    end
    J.loc, J.yaw, J.ps, J.pose, J.wp, J.seen = raw, tonumber(yaw) or 0, ps, pose, wp, now
    return m                                          -- the old mirror (if any) stays on screen until the new one is complete
end
function P2.mbStep(J)                     -- one unit of a build: true = more, false = done, "look" = no lookup left in this slot
    local B, m, kind = P2.mb, J.m, nil
    local t0 = os.clock()
    if J.ph == 1 then                                 -- the paths, one uncached lookup at a time (find and load in separate slots)
        local p = J.need[J.ni]
        if not p then J.ph = 2; return true end
        local o
        if P2.acHas(p) then kind = "cache"; J.ni = J.ni + 1; o = P2.asset(p)
        else
            if B.look >= math.max(1, FL.mblook or 1) then return "look" end
            B.look = B.look + 1
            passo("mirror obra: busca")
            if not J.fw then
                kind = "busca"; o = P2.assetFind(p)
                if o then J.ni = J.ni + 1 else J.fw = true; o = true end   -- not loaded: LoadAsset in a later step
            else
                kind = "carga"; J.fw = nil; J.ni = J.ni + 1; o = P2.assetLoad(p)
            end
        end
        if not o and p == J.mesh then error("mesh indisponivel " .. tostring(p)) end
    elseif J.ph == 2 then                             -- actor + main mesh, hidden and without collision from its first frame
        kind = "ator"
        passo("mirror obra: ator")
        local asset = P2.asset(J.mesh)
        if not asset then error("mesh indisponivel " .. tostring(J.mesh)) end
        meshCache[J.mesh] = asset
        local slot = J.slot
        if not slot then mirSlot = (mirSlot + 1) % 8; slot = mirSlot end
        local loc = {X = J.loc.X + slot * FL.mirrorspread, Y = J.loc.Y, Z = J.loc.Z}
        local a = UEHelpers.GetWorld():SpawnActor(P2.asset("/Script/Engine.Actor"), loc, {Pitch = 0, Yaw = J.yaw, Roll = 0})
        pcall(function() a:SetActorHiddenInGame(true) end)
        pcall(function() a:SetActorEnableCollision(false) end)
        m = {actor = a, slot = slot, lparts = J.lparts, a = J.pa, mesh = J.mesh, bare = J.bare}
        J.m = m
        local c = a:AddComponentByClass(P2.asset("/Script/Engine.PoseableMeshComponent"), false, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
        c:SetSkinnedAssetAndUpdate(asset, true)
        m.comp = c
        J.byName, J.nm, J.np, J.nsock, J.ns, J.pi = {CharacterMesh0 = c}, 0, 0, 0, 0, 1
        J.ph = 3
    elseif J.ph == 3 then
        kind = "materiais"
        J.nm = J.nm + P2.mbMats(m.comp, J.lmats, "|")
        J.ph = 4
    elseif J.ph == 4 then                             -- one part (v15 leader pose / v25c weapon / b136 head / b146 wpnskel+wpnmat when set)
        local ent = J.parts[J.pi]
        if not ent then J.ph = 5; return true end
        J.pi = J.pi + 1
        kind = "parte"
        local pth, pm, pname, psock = ent:match("^(.-)@(.-)@([^@]*)@?(.*)$")
        if not pth then pth, pm, pname, psock = ent, "", nil, "" end
        local okP, eP = pcall(function()
            local pa = P2.asset(pth)
            if not pa then error("mesh indisponivel") end
            local wsk = psock ~= "" and (FL.wpnskel or 0) == 1
            local pc = m.actor:AddComponentByClass(P2.asset(wsk and "/Script/Engine.SkeletalMeshComponent" or "/Script/Engine.PoseableMeshComponent"), false, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
            pc:SetSkinnedAssetAndUpdate(pa, true)
            if wsk then pcall(function() pc:SetCollisionEnabled(0); pc.VisibilityBasedAnimTickOption = 0 end); P2.wpn.skel = (P2.wpn.skel or 0) + 1 end
            if psock ~= "" then
                m.socks = m.socks or {}
                m.socks[#m.socks + 1] = pc
                J.nsock = J.nsock + 1
            elseif not P2.headSort(m, pc, pth) then
                pc:SetLeaderPoseComponent(m.comp, true, true)
                m.parts = m.parts or {}
                m.parts[#m.parts + 1] = pc
            end
            if psock == "" or FL.wpnmat == nil or FL.wpnmat == 1 then J.nm = J.nm + P2.mbMats(pc, pm, ",") end
            if pname then J.byName[pname] = pc end
            J.np = J.np + 1
        end)
        if not okP then log("MIRROR parte erro " .. tostring(pth) .. ": " .. tostring(eP)) end
    elseif J.ph == 5 then
        kind = "cabeca"
        P2.headWire(m, J.id)
        J.pend, J.rest, J.si, J.pass = {}, {}, 1, 1
        for ent in J.lstat:gmatch("[^|]+") do J.pend[#J.pend + 1] = ent end
        J.ph = 6
    elseif J.ph == 6 then                             -- one static (several passes: one can hang on another, NVG chain)
        local ent = J.pend[J.si]
        if not ent then
            J.pend, J.rest, J.si, J.pass = J.rest, {}, 1, J.pass + 1
            if #J.pend == 0 or J.pass > 6 then J.ph = 7 end
            return true
        end
        J.si = J.si + 1
        kind = "estatico"
        local snm, spar, ssock, spath, tr = ent:match("^(.-)>(.-)>(.-)>(.-)>(.*)$")
        if snm and J.byName[spar] then
            local okS, eS = pcall(function()
                local sa = P2.asset(spath)
                if not sa then error("mesh indisponivel") end
                local v = {}
                for num in tr:gmatch("[^,]+") do v[#v + 1] = tonumber(num) end
                local sc = m.actor:AddComponentByClass(P2.asset("/Script/Engine.StaticMeshComponent"), true, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
                sc:SetStaticMesh(sa)
                sc:SetCollisionEnabled(0)
                sc:K2_AttachToComponent(J.byName[spar], FName(ssock), 2, 2, 2, false)
                sc:K2_SetRelativeLocationAndRotation({X = v[1], Y = v[2], Z = v[3]}, {Pitch = v[4], Yaw = v[5], Roll = v[6]}, false, {}, true)
                sc:SetRelativeScale3D({X = v[7], Y = v[8], Z = v[9]})
                J.byName[snm] = sc
                J.ns = J.ns + 1
            end)
            if not okS then log("MIRROR estatico erro " .. tostring(snm) .. ": " .. tostring(eS)) end
        elseif snm then J.rest[#J.rest + 1] = ent end
    else                                              -- 7: newest position + pose + gun, then on screen (the old mirror goes)
        kind = "mostra"
        P2.mbDone(J)
    end
    local ms = (os.clock() - t0) * 1000
    local st = B.st
    J.steps, J.ms = J.steps + 1, J.ms + ms
    if ms > J.max then J.max = ms end
    st.step = st.step + 1
    if ms > st.stepMax then st.stepMax, st.stepK = ms, kind end
    local k = st.k[kind]
    if not k then k = {n = 0, ms = 0, max = 0}; st.k[kind] = k end
    k.n, k.ms = k.n + 1, k.ms + ms
    if ms > k.max then k.max = ms end
    return J.ph ~= 8
end
function P2.mbDone(J)
    local B, m, id = P2.mb, J.m, J.id
    J.ph = 8
    local loc = {X = J.loc.X + m.slot * FL.mirrorspread, Y = J.loc.Y, Z = J.loc.Z}
    m.actor:K2_SetActorLocation(loc, false, {}, true)
    if FL.rotate == 1 then m.actor:K2_SetActorRotation({Pitch = 0, Yaw = J.yaw, Roll = 0}, true) end
    m.x, m.y, m.z = loc.X, loc.Y, loc.Z
    if J.pose then local okQ, eQ = pcall(P2.poseDirect or P2.poseNow, m, J.pose); if okQ then m.s = J.ps else log("MIRROR obra pose erro: " .. tostring(eQ)) end end
    if m.socks and J.wp and J.wp:sub(1, 2) == "R:" and FL.wpnhand == 1 then pcall(P2.wpApply, m, J.wp) end
    local old = mirrors[id]
    mirrors[id] = m
    if B.jobs[id] == J then B.jobs[id] = nil end
    J.m = nil
    P2.mbShow(m)
    if old and old ~= m then P2.mbRetire(id, old); log("MIRROR troca " .. id .. " (aparencia nova pronta)") end
    log("MIRROR aparencia " .. id .. ": materiais=" .. J.nm .. " partes=" .. J.np .. " soquete=" .. J.nsock .. " estaticos=" .. J.ns .. " sem_pai=" .. #(J.pend or {})
        .. string.format(" | obra passos=%d ms=%.1f max=%.1f em %.2f s", J.steps, J.ms, J.max, os.clock() - J.t0))
    mirStat.made = mirStat.made + 1
    B.st.done = B.st.done + 1
    log("MIRROR +" .. id)
end
function P2.mbWork(t0)                    -- pslot slot (or the bridge run): build steps, oldest job first, while the slot is under FL.mbms
    local B = P2.mb
    if not next(B.jobs) then return end
    t0 = t0 or os.clock()
    B.look = 0
    local L = {}
    for _, J in pairs(B.jobs) do L[#L + 1] = J end
    table.sort(L, function(a, b) return a.n < b.n end)
    local steps, budget = 0, FL.mbms or 4
    for _, J in ipairs(L) do
        while B.jobs[J.id] == J do
            if steps > 0 and (os.clock() - t0) * 1000 >= budget then
                local s = (os.clock() - t0) * 1000; if s > B.st.slotMax then B.st.slotMax = s end
                return
            end
            local ok, r = pcall(P2.mbStep, J)
            if not ok then
                B.st.errs = B.st.errs + 1
                mirStat.errs = mirStat.errs + 1; mirStat.lastErr = tostring(r):sub(1, 160)
                if B.st.errs <= 5 then log("MIRROR obra erro " .. tostring(J.id) .. ": " .. tostring(r)) end
                B.fail[J.id .. "\1" .. J.pa] = os.clock()
                P2.mbCancel(J, "erro")
                break
            end
            if r == "look" then break end
            steps = steps + 1
            if not r then break end
        end
    end
    local s = (os.clock() - t0) * 1000
    if s > B.st.slotMax then B.st.slotMax = s end
end
function P2.mbEnd(seen)                   -- applyMirrors end (mb=1): left the snapshot -> kept hidden; jobs of absent NPCs stop; kept ones age
    local B, now = P2.mb, os.clock()
    if S2 then S2.mb = B end                          -- probes read S2.mb
    for id, m in pairs(mirrors) do
        if not seen[id] then mirrors[id] = nil; P2.mbRetire(id, m); log("MIRROR -" .. id .. (B.park[id] and " (guardado)" or "")) end
    end
    for id, J in pairs(B.jobs) do if not seen[id] then P2.mbCancel(J, "saiu") end end
    for id, P in pairs(B.park) do
        local okA, hid = false, true
        pcall(function() okA = P.m.actor:IsValid(); if okA then hid = P.m.actor.bHidden end end)
        if not okA or now - P.at > (FL.mbhold or 15) then
            if okA then P2.mbKill(P.m) end
            B.park[id] = nil
        elseif hid == false then pcall(function() P.m.actor:SetActorHiddenInGame(true) end) end   -- someone showed it again (cloot)
    end
    for k, at in pairs(B.fail) do if now - at > 10 then B.fail[k] = nil end end
    if FL.pslot ~= 1 then P2.mbWork() end             -- no slots: the steps run in the bridge run
    if now - B.st.at >= 10 then
        local st, np, nj = B.st, 0, 0
        for _ in pairs(B.park) do np = np + 1 end
        for _ in pairs(B.jobs) do nj = nj + 1 end
        if st.jobs + st.reuse + st.park + st.look > 0 then
            local ks = {}
            for kname, k in pairs(st.k) do ks[#ks + 1] = string.format("%s=%d/%.1f/%.1f", kname, k.n, k.ms / math.max(1, k.n), k.max) end
            table.sort(ks)
            log(string.format("MIRROR obra 10 s (mb=1): obras=%d prontas=%d canceladas=%d erros=%d reusos=%d guardados=%d (agora %d, obras abertas %d) destruidos=%d buscas=%d (ms_med=%.1f max=%.1f %s) cache=%d passo_max=%.1f ms (%s) slot_max=%.1f ms | n/ms_med/ms_max %s",
                st.jobs, st.done, st.cancel, st.errs, st.reuse, st.park, np, nj, st.kill, st.look, st.lookMs / math.max(1, st.look), st.lookMax, st.lookP,
                st.hit, st.stepMax, st.stepK, st.slotMax, table.concat(ks, " ")))
        end
        B.st = {at = now, jobs = 0, done = 0, cancel = 0, errs = 0, reuse = 0, park = 0, kill = 0, look = 0, lookMs = 0, lookMax = 0, lookP = "-",
                step = 0, stepMax = 0, stepK = "-", slotMax = 0, hit = 0, k = {}}
    end
end
function P2.mbHys(near, now)              -- HOST near list (mslot=0): incumbents keep their place FL.mbstay s, then unless FL.mbhys cm farther
    local inc, h, stay = P2.mb.inc, FL.mbhys or 500, FL.mbstay or 4
    for _, e in ipairs(near) do
        local at = inc[e.path]
        if at and now - at < stay then e.hk = -1
        elseif at then local d = math.max(0, math.sqrt(e.d2) - h); e.hk = d * d
        else e.hk = e.d2 end
    end
end
function P2.mbInc(near, now)
    local old, new = P2.mb.inc, {}
    for _, e in ipairs(near) do new[e.path] = old[e.path] or now end
    P2.mb.inc = new
end
-- ===== end b149hitch (main)
local function applyMirrors(t, lp)  -- lp = posicao do jogador local
    if rawget(_G, "s2n_nat_filt") and type(t) == "string" then local okF, tF = pcall(s2n_nat_filt, t); if okF and type(tF) == "string" then t = tF end end   -- b158natnpc
    if S2 and S2.mod and S2.mod.applyMirrors then return S2.mod.applyMirrors(S2.state, t, lp) end
    local seen, count = {}, 0
    -- v2: aparencias que vieram neste snapshot entram no cache por hash; cada NPC diz qual hash usa ("a") e o numero da pose ("s")
    local nowA, posN = os.clock(), t:find('"npcs"', 1, true) or (#t + 1)   -- as aparencias ("apps") ja entraram no cache no passo da ponte
    P2.pend = 0
    for id, x, y, z, yaw, pa, ps, pose, wp in t:sub(posN):gmatch('{"id":%s*"(.-)",%s*"x":%s*([%-%d%.]+),%s*"y":%s*([%-%d%.]+),%s*"z":%s*([%-%d%.]+),%s*"yaw":%s*([%-%d%.]+),%s*"a":%s*"(.-)",%s*"s":%s*(%d+),%s*"pose":%s*"(.-)",%s*"wp":%s*"(.-)"}') do
        local app = P2.rapps[pa]
        local mesh, lparts, lmats, lstat = "", "", "", ""
        if app then app.at = nowA; mesh, lparts, lmats, lstat = app.mesh, app.parts, app.mats, app.statics
        else P2.need[pa] = nowA; P2.pend = P2.pend + 1 end   -- aparencia desconhecida: pede em "need"; o espelho so nasce quando ela chegar
        mirStat.recv = mirStat.recv + 1
        count = count + 1
        if count <= FL.maxnpcs then
            seen[id] = true
            local ok, e = pcall(function()
                local loc = {X = tonumber(x), Y = tonumber(y), Z = tonumber(z) - 90}
                local m = mirrors[id]
                if m and m.slot then loc.X = loc.X + m.slot * FL.mirrorspread end
                local mbOn = FL.mb == 1   -- b149hitch: builds / kept mirrors / swaps go through P2.mbPre; the old create + recreate below are skipped
                if mbOn then m = P2.mbPre(id, m, pa, app, loc, yaw, ps, pose, wp, lp) end
                local keepSlot = nil
                if not mbOn and m and m.actor:IsValid() and app and m.a ~= pa then  -- v25b/v2: aparencia mudou (troca de arma): recria no mesmo lugar, quando a nova aparencia ja chegou
                    keepSlot = m.slot
                    loc.X = loc.X - m.slot * FL.mirrorspread
                    m.actor:K2_DestroyActor()
                    log("MIRROR recria " .. id .. " (partes mudaram)")
                end
                if not mbOn and (not (m and m.actor:IsValid()) or keepSlot) then
                    mirrors[id] = nil; m = nil
                    if not app then return end  -- v2: sem aparencia ainda
                    local far = false
                    if lp and FL.maxdist > 0 then far = ((loc.X - lp.X)^2 + (loc.Y - lp.Y)^2) > FL.maxdist^2 end
                    if far then return end
                    local asset = meshCache[mesh] or StaticFindObject(mesh)
                    if not (asset and asset:IsValid()) then pcall(function() asset = LoadAsset(mesh) end) end
                    if asset and asset:IsValid() then
                        meshCache[mesh] = asset
                        passo("mirror: SpawnActor + PoseableMeshComponent")
                        local slot = keepSlot
                        if not slot then mirSlot = (mirSlot + 1) % 8; slot = mirSlot end
                        loc.X = loc.X + slot * FL.mirrorspread
                        m = newMirror(asset, loc, tonumber(yaw))
                        m.slot = slot
                        m.lparts = lparts
                        m.a = pa
                        m.mesh = mesh
                        mirrors[id] = m
                        -- v15: materiais do mesh principal + partes extras seguindo a pose do principal (leader pose)
                        local nm, np, nsock = applyMats(m.comp, lmats, "|"), 0, 0
                        local byName = {CharacterMesh0 = m.comp}
                        for ent in lparts:gmatch("[^|]+") do
                            local pth, pm, pname, psock = ent:match("^(.-)@(.-)@([^@]*)@?(.*)$")
                            if not pth then pth, pm, pname, psock = ent, "", nil, "" end
                            local okP, eP = pcall(function()
                                local pa = StaticFindObject(pth)
                                if not (pa and pa:IsValid()) then pcall(function() pa = LoadAsset(pth) end) end
                                if not (pa and pa:IsValid()) then error("mesh indisponivel") end
                                local pc = m.actor:AddComponentByClass(StaticFindObject("/Script/Engine.PoseableMeshComponent"), false, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
                                pc:SetSkinnedAssetAndUpdate(pa, true)
                                if psock ~= "" then  -- v25c: arma: pose propria (referencia), transform mundial vindo do campo wp a cada snapshot
                                    m.socks = m.socks or {}
                                    m.socks[#m.socks + 1] = pc
                                    nsock = nsock + 1
                                elseif not P2.headSort(m, pc, pth) then   -- b136: foreign skeleton (face) -> wired by P2.headWire below
                                    pc:SetLeaderPoseComponent(m.comp, true, true)
                                    m.parts = m.parts or {}
                                    m.parts[#m.parts + 1] = pc
                                end
                                nm = nm + applyMats(pc, pm, ",")
                                if pname then byName[pname] = pc end
                                np = np + 1
                            end)
                            if not okP then log("MIRROR parte erro " .. pth .. ": " .. tostring(eP)) end
                        end
                        P2.headWire(m, id)   -- b136 (headfix)
                        -- v17: acessorios estaticos; varias passadas porque um acessorio pode pendurar em outro (cadeia do NVG)
                        local ns, pend = 0, {}
                        for ent in lstat:gmatch("[^|]+") do pend[#pend + 1] = ent end
                        for _ = 1, 6 do
                            local rest = {}
                            for _, ent in ipairs(pend) do
                                local snm, spar, ssock, spath, tr = ent:match("^(.-)>(.-)>(.-)>(.-)>(.*)$")
                                if snm and byName[spar] then
                                    local okS, eS = pcall(function()
                                        local sa = StaticFindObject(spath)
                                        if not (sa and sa:IsValid()) then pcall(function() sa = LoadAsset(spath) end) end
                                        if not (sa and sa:IsValid()) then error("mesh indisponivel") end
                                        local v = {}
                                        for num in tr:gmatch("[^,]+") do v[#v + 1] = tonumber(num) end
                                        local sc = m.actor:AddComponentByClass(StaticFindObject("/Script/Engine.StaticMeshComponent"), true, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
                                        sc:SetStaticMesh(sa)
                                        sc:SetCollisionEnabled(0)
                                        sc:K2_AttachToComponent(byName[spar], FName(ssock), 2, 2, 2, false)
                                        sc:K2_SetRelativeLocationAndRotation({X = v[1], Y = v[2], Z = v[3]}, {Pitch = v[4], Yaw = v[5], Roll = v[6]}, false, {}, true)
                                        sc:SetRelativeScale3D({X = v[7], Y = v[8], Z = v[9]})
                                        byName[snm] = sc
                                        ns = ns + 1
                                    end)
                                    if not okS then log("MIRROR estatico erro " .. tostring(snm) .. ": " .. tostring(eS)) end
                                elseif snm then rest[#rest + 1] = ent end
                            end
                            pend = rest
                            if #pend == 0 then break end
                        end
                        log("MIRROR aparencia " .. id .. ": materiais=" .. nm .. " partes=" .. np .. " soquete=" .. nsock .. " estaticos=" .. ns .. " sem_pai=" .. #pend)
                        mirStat.made = mirStat.made + 1
                        log("MIRROR +" .. id)
                    end
                end
                if m then
                    m.actor:K2_SetActorLocation(loc, false, {}, true)
                    if FL.rotate == 1 then m.actor:K2_SetActorRotation({Pitch = 0, Yaw = tonumber(yaw), Roll = 0}, true) end
                    if m.s ~= ps then  -- v2: so reaplica os ossos quando o numero da pose mudou
                        m.s = ps
                        if FL.pslot == 1 and m.bones then m.pp = pose; m.ppAt = m.ppAt or os.clock()   -- b143perf: posed in a pslot slot (newest pose wins)
                        else m.pp, m.ppAt = nil, nil; P2.poseNow(m, pose) end                          -- new mirror / pslot=0: at once (old path)
                    else P2.skipped = P2.skipped + 1 end
                    m.x, m.y, m.z = loc.X, loc.Y, loc.Z
                    if m.mbShow then P2.mbShow(m) end   -- b149hitch: a kept mirror back and posed -> on screen
                    if m.socks and wp ~= "" and FL.wpnhand == 1 and wp:sub(1, 2) == "R:" then   -- b144combat (wpnhand): gun on the hand bone
                        if not P2.wpApply(m, wp) then P2.wpn.fail = P2.wpn.fail + 1 end
                        P2.wpDiag(m, id)
                    elseif m.socks and wp ~= "" and wp:sub(1, 2) ~= "R:" then   -- (a "R:" text never goes to the 6-number world path)
                        P2.wpn.old = P2.wpn.old + 1
                        local v = {}
                        for num in wp:gmatch("[^,]+") do v[#v + 1] = tonumber(num) end
                        if #v == 6 then for _, sc in ipairs(m.socks) do
                            sc:K2_SetWorldLocationAndRotation({X = loc.X + v[1], Y = loc.Y + v[2], Z = loc.Z + 90 + v[3]}, {Pitch = v[4], Yaw = v[5], Roll = v[6]}, false, {}, true)
                        end end
                    end
                end
            end)
            if not ok then mirStat.errs = mirStat.errs + 1; mirStat.lastErr = tostring(e):sub(1, 160); if mirStat.errs <= 5 then log("MIRROR erro: " .. id .. ": " .. tostring(e)) end end
        end
    end
    if FL.mb == 1 then local okE, eE = pcall(P2.mbEnd, seen); if not okE then log("MIRROR obra erro (fim): " .. tostring(eE)) end end   -- b149hitch
    for id, m in pairs(mirrors) do
        if not seen[id] then
            pcall(function() if m.actor:IsValid() then m.actor:K2_DestroyActor() end end)
            mirrors[id] = nil
            log("MIRROR -" .. id)
        end
    end
    local now = os.clock()
    S2_mirN, S2_mirErr = 0, mirStat.errs
    for _ in pairs(mirrors) do S2_mirN = S2_mirN + 1 end
    if now >= mirStat.logAt then
        mirStat.logAt = now + 10
        local n, d = 0, {}
        for id, m in pairs(mirrors) do
            n = n + 1
            -- v15e: reafirma o leader pose das partes a cada 10 s (1 espelho em 5 apareceu sem cabeca apos um travamento do jogo)
            for k, pc in ipairs(m.parts or {}) do pcall(function() if pc:IsValid() then pc:SetLeaderPoseComponent((m.pl and m.pl[k]) or m.comp, true, true) end end) end
            if m.x then d[#d + 1] = string.format("%s@%.0f,%.0f,%.0f/ossos=%.0f", id, m.x, m.y, m.z, m.bones or 0) end
        end
        log(string.format("MIRROR vivo: espelhos=%.0f recebidos_no_snapshot=%.0f criados=%.0f erros=%.0f ultimo_erro=%s %s", n, count, mirStat.made, mirStat.errs, mirStat.lastErr, table.concat(d, " ")))
    end
end
-- Teleporte seguro da coleira: cria um centro de streaming no destino, espera o terreno carregar (5 s), teleporta com o pawn em modo voo
-- (nao cai atraves do terreno ainda nao carregado) e volta ao modo andando 4 s depois.
local tpBusy = false
-- true se um raio vertical encontra chao (colisao carregada) perto de (x, y)
local function groundBelow(x, y, z)
    local ok, res = pcall(function()
        local ksl = StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
        local out = {}
        local h = ksl:LineTraceSingle(UEHelpers.GetWorld(), {X = x, Y = y, Z = z + 2000}, {X = x, Y = y, Z = z - 6000}, 0, false, {}, 0, out, true, {R = 1, G = 0, B = 0, A = 1}, {R = 0, G = 1, B = 0, A = 1}, 0)
        local iz = out.ImpactPoint and out.ImpactPoint.Z
        local actor = out.HitObjectHandle and out.HitObjectHandle.Actor
        return {hit = h == true, z = iz}
    end)
    if ok and res then
        log(string.format("TRACE x=%.0f y=%.0f hit=%s impactZ=%s (alvo z=%.0f)", x, y, tostring(res.hit), tostring(res.z), z))
        local good = res.hit and res.z ~= nil and math.abs(res.z - z) < 3000
        return good, good and res.z or nil
    end
    log("TRACE erro: " .. tostring(res))
    return false
end
-- Teleporte seguro: streaming no destino, espera ate o raio vertical achar chao (ate 40 s), teleporta em modo voo e
-- so volta a andar quando houver chao abaixo; se cair mesmo assim, repete o voo.
local function safeTeleport(pawn, x, y, z)
    if tpBusy then return end
    tpBusy = true
    local src
    pcall(function()
        src = UEHelpers.GetWorld():SpawnActor(StaticFindObject("/Script/Engine.TargetPoint"), {X = x, Y = y, Z = z + 500}, {Pitch = 0, Yaw = 0, Roll = 0})
        local c = src:AddComponentByClass(StaticFindObject("/Script/Engine.WorldPartitionStreamingSourceComponent"), false, {Rotation = {X = 0, Y = 0, Z = 0, W = 1}, Translation = {X = 0, Y = 0, Z = 0}, Scale3D = {X = 1, Y = 1, Z = 1}}, false)
        c.bStreamingSourceEnabled = true
        c.TargetState = 1
    end)
    log("COLEIRA: streaming no destino")
    local waited = 0
    local function walkWhenGround(tries)
        ExecuteInGameThread(function()
            if not pawn:IsValid() then tpBusy = false; return end
            local p = pawn:K2_GetActorLocation()
            if p.Z < z - 3000 then
                pawn:K2_SetActorLocation({X = x, Y = y, Z = z + 100}, false, {}, true)
                pcall(function() pawn.CharacterMovement:SetMovementMode(5, 0) end)
                log("COLEIRA: pawn caiu, voo de novo")
            end
            if groundBelow(p.X, p.Y, math.max(p.Z, z)) or tries >= 60 then
                pcall(function() pawn.CharacterMovement:SetMovementMode(1, 0) end)
                if src and src:IsValid() then src:K2_DestroyActor() end
                tpBusy = false
                log("COLEIRA: modo andando restaurado (chao=" .. tostring(tries < 60) .. ")")
            else
                ExecuteInGameThreadWithDelay(1000, function() walkWhenGround(tries + 1) end)
            end
        end)
    end
    local function waitStreaming()
        ExecuteInGameThread(function()
            waited = waited + 1
            local g, gz = groundBelow(x, y, z)
            if g or waited >= 40 then
                if gz then z = gz + 100 end
                if pawn:IsValid() then
                    pawn:K2_SetActorLocation({X = x, Y = y, Z = z + 100}, false, {}, true)
                    pcall(function() pawn.CharacterMovement:SetMovementMode(5, 0) end)
                end
                log(string.format("COLEIRA: teleportado apos %d s (chao=%s), modo voo", waited, tostring(waited < 40)))
                ExecuteInGameThreadWithDelay(1000, function() walkWhenGround(0) end)
            else
                ExecuteInGameThreadWithDelay(1000, waitStreaming)
            end
        end)
    end
    ExecuteInGameThreadWithDelay(1000, waitStreaming)
end
-- Presenca visivel do outro jogador:
--  host: o jogador 2 vira um NPC neutro (avatar) criado por XSpawnObjBySID perto da posicao recebida e movido a cada 100 ms;
--  cliente: o host vira um espelho (SkeletalMeshActor) com um mesh de stalker, movido a cada 100 ms.
local avatarSpawning, hostProxy = false, nil
-- Avatar do outro jogador (NPC neutro). Fluxo: o laco da ponte (100 ms) le in.json e atualiza `remote` (ultimo snapshot com tempo do remetente,
-- posicao, velocidade real e yaw); o laco de suavizacao (30 Hz, startSmooth) faz dead-reckoning (posicao + velocidade * idade, no maximo 0,3 s),
-- persegue esse alvo com suavizacao exponencial, usa a velocidade REAL do remetente (nao a derivada) para a animacao e o yaw do remetente
-- (ou a direcao do movimento quando anda). O CharacterMovement fica em MOVE_None (a IA nao o move); nunca destruir o controller (derruba o jogo).
AVATAR_DZ = 0          -- ajuste fino do z do avatar em relacao ao z do pawn remoto (ambos sao centro de capsula)
-- Flags de bisect: bridge\flags.txt (linhas chave=0|1). Padrao conservador: so o que ja foi provado fica ligado.
--  anim: reflexao + escrita no AnimInstance | ctrlrot: bUseControllerRotationYaw/GetController/SetControlRotation | disablemove: DisableMovement
--  velwrite: CharacterMovement.Velocity | compvel: CapsuleComponent.ComponentVelocity | readopt: readotar avatar.txt | diag: leituras de diagnostico
FL = {scanage = 3, prof = 1, avdebug = 0, avground = 1, avatarmode = "poseable", god = 1, nodamage = 0, scan = 0, avatar = 1, anim = 0, ctrlrot = 0, disablemove = 1, clientalife = 0, playergod = 1, godtoggle = 0, godtest = 0, godlayers = "none", mirrors = 0, mirrortest = 0, mirrorspread = 0, maxnpcs = 4, scanmove = 0, npcdmg = 0, npcdmgtype = 6, shottest = 0, shotdmg = 25, shotdetect = 0, scanself = 0, avoffx = 0, difficulty = "Medium", settime = 0, luacmd = 0, syncweather = 0, wxtest = 99, synctime = 0, synctimetest = 0, syncthresh = 600, syncgap = 30, look = 1, lookdiag = 0, scanr = 2000, autocontinue = 0, autobridge = 0, autowalk = 0, beacon = 1, velwrite = 0, compvel = 0, readopt = 0, diag = 0, revalidate = 1, maxdist = 7000, rotate = 0, hz = 10, leashm = 350, headfix = 1}
--  revalidate: revalida o avatar por nome (StaticFindObject) a cada ~1 s | maxdist: cm; avatar nunca fica alem disso do jogador local (0 = sem limite); so spawna se o remoto esta dentro
-- v14: flags.txt e relido a cada 5 s pelo farol (S2_loadFlags), para trocar flags de teste sem relancar o jogo
function S2_loadFlags(quiet)
    local f = io.open(BR .. "flags.txt", "r")
    if f then
        for line in f:lines() do
            local ln = line:gsub("^\239\187\191", "")  -- BOM (Set-Content -Encoding utf8 no PowerShell)
            local k, v = ln:match("^%s*([%w_]+)%s*=%s*([%w%.%-]+)")
            if k and FL[k] ~= nil then
                local nv
                if type(FL[k]) == "string" then nv = v else nv = tonumber(v) or FL[k] end
                if nv ~= FL[k] then
                    if quiet then log("AVATAR flags: " .. k .. " " .. tostring(FL[k]) .. " -> " .. tostring(nv)) end
                    FL[k] = nv
                end
            end
        end
        f:close()
    end
    if quiet then return end
    local t = {}
    for _, k in ipairs({"clientalife", "playergod", "godtoggle", "godtest", "godlayers", "mirrors", "scanr", "autocontinue", "autobridge", "autowalk", "beacon", "anim", "ctrlrot", "disablemove", "velwrite", "compvel", "readopt", "diag", "revalidate", "maxdist", "rotate", "hz", "scan", "avatar", "god", "nodamage", "avatarmode", "avdebug", "avground"}) do t[#t + 1] = k .. "=" .. FL[k] end
    log("AVATAR flags: " .. table.concat(t, " "))
end
FL.scanper = 3   -- v2: no maximo quantos NPCs tem a pose lida por passo de 100 ms
FL.scanms = 3    -- v2: para de ler poses no passo quando passa disto (ms); o custo real por NPC so se mede no jogo (linha de log SCAN v2)
FL.appsper = 2   -- v2: no maximo quantas aparencias novas/pedidas entram num snapshot
FL.pslot = 1      -- b143perf: 1 = NPC pose reads (host) / bone sets (client) run in FL.pslotn slots between bridge runs; 0 = inside the bridge run (old)
FL.pslotms = 3    -- b143perf: a slot stops past this (ms)
FL.pslotn = 3     -- b143perf: slots per bridge run (1-6)
FL.pb = 1         -- b148pose: 1 = a new pose of a near on-screen mirror is blended per frame (slerp of bone locals over the pose interval); 0 = set at once (old)
FL.pbms = 1.5     -- b148pose: per-frame budget of the blend chain (ms; at least one mirror per frame)
FL.pbn = 4        -- b148pose: mirrors blended (nearest on screen); the others as before
FL.pbeps = 0.3    -- b148pose: a bone is set when its local rotation moved more than this (deg) since it was last set
FL.pbtmin = 0.08  -- b148pose: blend time = measured interval between two poses of the mirror, clamped to pbtmin..pbtmax s
FL.pbtmax = 0.5
FL.mb = 1         -- b149hitch: 1 = mirrors built in the pslot slots (cached lookups, hidden until complete), kept hidden when they leave, host near list with hysteresis; 0 = old
FL.mbms = 4       -- b149hitch: build steps per slot stop past this (ms; at least one step)
FL.mblook = 1     -- b149hitch: asset lookups not yet cached per slot
FL.mbhold = 15    -- b149hitch: a mirror that left is kept hidden this long (s) for the same NPC + look
FL.mbpark = 8     -- b149hitch: at most this many kept mirrors
FL.mbhys = 500    -- b149hitch (host, mslot=0): a near-set NPC is pushed out only by one this much nearer (cm)
FL.mbstay = 4     -- b149hitch (host, mslot=0): a new near-set NPC stays at least this long (s)
FL.debugkeys = 0  -- fixC: 1 = liga as teclas de depuracao (F1-F12, PAUSE, HOME, END, INS, DEL, NUM1/2/3/5/7/9)
FL.hipz = 1      -- b151hips: 1 = host sends the hips offset with the pose, client sets it (crouch no longer floats); 0 = rotations only
FL.wpnhand = 1   -- b144combat: 1 = the NPC weapon rides the mirror's right hand bone (host sends it relative to jnt_r_hand, "R:"); 0 = v25c world wp
S2_loadFlags()
-- Laco local de 1 s (independe da ponte/NUM0; sai com staleGen/Ctrl+R). Game thread: a cada 1 s se playergod=1 (como antes), senao so a cada 5 s.
-- playergod=1 (padrao): jogador LOCAL invulneravel (host e cliente), 5 s apos o pawn existir e de novo a cada 30 s.
-- Farol de estado: a cada 5 s grava <ponte>\state.json (tmp + rename): t, role, in_world, dead, health, level, paused, mod_ver, autowalk, god.
-- health/dead (v8): BP_Stalker2Character_C -> PC -> Obj (ObjectDump), logo o pawn local tem Obj:GetHP()/GetMaxHP()/IsAlive().
MOD_VER = "v37-b53"
local godTestActive, godTestJson = false, "null"  -- godtest=1: prova no jogo (dano real + queda); resultado em state.json
local godInfo = "-"  -- metodos de invulnerabilidade aplicados ao pawn atual (vai para state.json)
local function readVitals(pawn)  -- hp, maxhp, alive (nil se a leitura falhar)
    local hp, mx, alive
    pcall(function() hp = pawn:GetHP() end)
    pcall(function() mx = pawn:GetMaxHP() end)
    pcall(function() alive = pawn:IsAlive() and true or false end)
    if type(hp) ~= "number" then hp = nil end
    if type(mx) ~= "number" then mx = nil end
    return hp, mx, alive
end
local function jstr(s) if s == nil then return "null" end; return '"' .. (tostring(s):gsub('[%c"%' .. string.char(92) .. ']', "_")) .. '"' end
local function jval(v) if v == nil then return "null" end; if type(v) == "string" then return jstr(v) end; return tostring(v) end
local gamePaused, levelCache = nil, nil  -- gamePaused: lido pelo farol (5 s); nil = desconhecido (beacon=0)
S2_gameSecs, S2_gameHours, S2_setTimeDone = nil, nil, 0
local function readGameTime()
    local tm = FindFirstOf("TimeManager")
    if not (tm and tm:IsValid()) then S2_gameSecs = nil; return end
    -- medido 2026-10-10: ha DOIS relogios. Jogo (quests/A-Life): hora = (GameTimeSeconds/150 + 9) % 24 (1 h de jogo = 150 s reais), muda com XSetGameTime.
    -- Clima (sol/luz, visual): GameHours/GameMinutes/GameSecondsClamped e WeatherController.SolarTime, muda com XSetWeatherTime.
    pcall(function() S2_gameSecs = tm.GameTimeSeconds end)
    pcall(function() S2_gameHours = tm.GameHours end)
    pcall(function() S2_wtod = tm.GameHours * 3600 + tm.GameMinutes * 60 + tm.GameSecondsClamped end)
    if type(S2_gameSecs) == "number" then S2_gtod = ((S2_gameSecs / 150 + 9) % 24) * 3600 end
    -- clima (EWeather: 0 Clearly 1 Cloudy 2 Fogy 3 Stormy 4 LightRainy 5 Rainy 6 Thundery 7 Emission 8 CalmBeforeEmission 9 Underground)
    S2_wx = nil
    pcall(function() local w = FindFirstOf("WeatherController"); if w and w:IsValid() then S2_wx = w.CurrentWeatherData.TargetWeather end end)
    if type(S2_wx) ~= "number" then S2_wx = nil end
    if type(S2_gameSecs) ~= "number" then S2_gameSecs = nil end
    if type(S2_gameHours) ~= "number" then S2_gameHours = nil end
end
-- v35b: fresh clocks for every snapshot / comparison (readGameTime only ran every 5 s = up to 120 game-s of noise in the sync)
local fastTimeObj
local function fastTime()
    local tm = fastTimeObj
    if not (tm and tm:IsValid()) then tm = FindFirstOf("TimeManager"); fastTimeObj = tm end
    if not (tm and tm:IsValid()) then return end
    pcall(function() S2_gameSecs = tm.GameTimeSeconds end)
    if type(S2_gameSecs) == "number" then S2_gtod = ((S2_gameSecs / 150 + 9) % 24) * 3600 end
    pcall(function() S2_wtod = tm.GameHours * 3600 + tm.GameMinutes * 60 + tm.GameSecondsClamped end)
end
function S2_setGameTime(h, m, sec, why, weather)
    local cm = FindFirstOf("CustomConsoleManagerAA")
    if not (cm and cm:IsValid()) then log("TIME: CustomConsoleManagerAA nao achado"); return false end
    local bg, bw = S2_gtod, S2_wtod
    local okT, eT = pcall(function() if weather then cm:XSetWeatherTime(h, m, sec) else cm:XSetGameTime(h, m, sec) end end)
    readGameTime()
    log(string.format("TIME: %s(%d,%d,%d) %s ok=%s %s jogo %.0f -> %.0f s do dia, clima %.0f -> %.0f s do dia", weather and "XSetWeatherTime" or "XSetGameTime", h, m, sec, why, tostring(okT), okT and "" or tostring(eT), bg or -1, S2_gtod or -1, bw or -1, S2_wtod or -1))
    return okT
end
local function runLuaCmd()
    local f = io.open(BR .. "lua_cmd.txt", "r")
    if not f then return end
    local src = f:read("*a"); f:close()
    os.remove(BR .. "lua_cmd.txt")
    local out = {}
    local env = setmetatable({log = log, FL = FL, out = function(...) local t = {} for i = 1, select("#", ...) do t[#t + 1] = tostring((select(i, ...))) end out[#out + 1] = table.concat(t, " ") end}, {__index = _G})
    local fn, err = load(src, "lua_cmd", "t", env)
    local res
    if not fn then res = "ERRO compilacao: " .. tostring(err)
    else
        local r = table.pack(pcall(fn))
        local t = {}
        for i = 1, r.n do t[#t + 1] = tostring(r[i]) end
        res = "pcall: " .. table.concat(t, " ")
    end
    out[#out + 1] = res
    local g = io.open(BR .. "lua_out.txt", "w")
    if g then g:write(table.concat(out, "\n")); g:close() end
    log("LUACMD " .. res:sub(1, 200))
end
local function writeState()
    pcall(S2_loadFlags, true)
    pcall(readGameTime)
    if FL.luacmd == 1 then pcall(runLuaCmd) end
    if FL.settime ~= 0 and FL.settime ~= S2_setTimeDone then
        S2_setTimeDone = FL.settime
        pcall(S2_setGameTime, math.floor(FL.settime / 100) % 24, FL.settime % 100, 0, "flag settime")
    end
    local st = {in_world = false}
    local pawn = FindFirstOf("BP_Stalker2Character_C")
    st.in_world = (pawn and pawn:IsValid()) and true or false
    if not st.in_world then levelCache = nil; gamePaused = nil end
    if st.in_world then  -- v6: so le o mundo com pawn valido; nome do nivel lido UMA vez por entrada no mundo (cache)
        local okW, w = pcall(function() return UEHelpers.GetWorld() end)
        if okW and w and w:IsValid() then
            local gs = StaticFindObject("/Script/Engine.Default__GameplayStatics")
            if gs and gs:IsValid() then
                local okP, pv = pcall(function() return gs:IsGamePaused(w) end)
                if okP then st.paused = pv and true or false end
                gamePaused = st.paused
                if not levelCache then
                    pcall(function()
                        local n = gs:GetCurrentLevelName(w, true)
                        levelCache = (type(n) == "userdata" and n.ToString) and n:ToString() or tostring(n)
                    end)
                end
            end
        end
    end
    st.level = levelCache
    local hp, mx, alive
    if st.in_world then hp, mx, alive = readVitals(pawn) end
    local dead = nil
    if alive ~= nil then dead = not alive end
    local body = string.format('{"t":%d,"role":%s,"in_world":%s,"dead":%s,"health":%s,"max_health":%s,"level":%s,"paused":%s,"mod_ver":%s,"autowalk":%s,"god":%s,"god_methods":%s,"god_layers":%s,"scan":%s,"mirrortest":%s,"pawns":%s,"npcs_scan":%s,"mirrors":%s,"mirror_errs":%s,"game_tod":%s,"weather_tod":%s,"weather":%s,"godtest":%s,"proto_err":%s}',
        os.time(), jstr(ROLE), tostring(st.in_world), jval(dead), hp and string.format("%.1f", hp) or "null", mx and string.format("%.1f", mx) or "null",
        jval(st.level), jval(st.paused), jstr(MOD_VER), jval(FL.autowalk), jval(FL.playergod), jstr(godInfo), jstr(FL.godlayers), jval(FL.scan), jval(FL.mirrortest), jval(S2_pawnN), jval(S2_scanN), jval(S2_mirN), jval(S2_mirErr), S2_gtod and string.format("%.0f", S2_gtod) or "null", S2_wtod and string.format("%.0f", S2_wtod) or "null", jval(S2_wx), godTestJson, jval(S2_protoErr))
    local tmp, dst = BR .. "state.json.tmp", BR .. "state.json"
    local f = io.open(tmp, "w")
    if not f then return end
    f:write(body); f:close()
    if not os.rename(tmp, dst) then os.remove(dst); os.rename(tmp, dst) end  -- Windows: rename nao sobrescreve
end
do
    local godAt, n, beaconErrs = nil, 0, 0
    local godPawn, toggleDone, healN, lowLogged = nil, false, 0, false
    local fullDone, jLoop, resN = false, false, 0
    local acDone, abDone, abAt, acNext, acSeen, acKeys, acLoads, acMenuAt, acErr = false, false, nil, 0, false, 0, 0, nil, 0
    -- playergod v8 (XSetGodMode sozinho NAO protegeu: o jogador do cliente morreu com ok=true). Camadas, aplicadas 5 s apos o pawn existir e de novo
    -- SO quando o objeto do pawn muda (respawn/load): a) CppMediator:MakeInvulnerable(pawn)  b) Actor.bCanBeDamaged=false (propriedade; SetCanBeDamaged nao e refletida)
    -- c) XSetGodModeByUID: sem leitor de EntityUID no ObjectDump -> nao usado  d) XSetGodMode(true) UMA vez, so com godtoggle=1 (pode ser alternancia)
    -- h) rede de seguranca a cada 1 s: HP < 50% do maximo -> Obj:ForceSetHP(maximo).
    -- godtest=1: UMA vez, 30 s apos o pawn existir. Fase 2: UGameplayStatics:ApplyDamage(40% do max). Fase 3: sobe o pawn 1500 cm e deixa cair,
    -- amostrando o HP a cada 0,5 s por 8 s. Rodar tambem com playergod=0 (controle): se o HP nao cair no controle, a fonte de dano nao prova nada.
    local gtAt, gtDone, gtManual = nil, false, false
    local function runGodTest(pawn0)
        gtDone = true; godTestActive = true
        local r = {h0 = healN}
        local function f(v) return v and string.format("%.1f", v) or "null" end
        local hp, mx, alive = readVitals(pawn0)
        r.ini, r.max = hp, mx
        log("AVATAR godtest: inicio hp=" .. f(hp) .. " max=" .. f(mx) .. " vivo=" .. tostring(alive) .. " camadas=" .. tostring(FL.godlayers) .. " playergod=" .. tostring(FL.playergod) .. " metodos=" .. godInfo)
        -- v12: dano fixo de 40 pela funcao do jogo: Strike (6) agora, Burn (1) 1 s depois; queda de 15 m 1 s depois, amostrada por 4 s
        local function hit(pw, typ, name)
            local okD, eD = pcall(function() pw:ReceiveDamage(40.0, typ, 0.0, 0.0, 0.0, 0.0, FName("None")) end)
            log("AVATAR godtest: Obj:ReceiveDamage(40.0, " .. name .. ") ok=" .. tostring(okD) .. " " .. tostring(eD))
            return okD
        end
        r.dmg_ok = hit(pawn0, 6, "Strike")
        local k = 0
        gameLoop(500, "godtest", function()
            k = k + 1
            local pawn = FindFirstOf("BP_Stalker2Character_C")
            if not (pawn and pawn:IsValid()) then
                log("AVATAR godtest: FIM pawn invalido (morreu/recarregou?) na amostra " .. k); godTestActive = false
                godTestJson = '{"done":true,"pawn_lost":true}'; return true
            end
            local h, m, a = readVitals(pawn)
            if a == false then r.morreu = true end
            if k == 2 then
                r.dano = h
                log("AVATAR godtest: hp apos Strike=" .. f(h) .. " vivo=" .. tostring(a))
                r.burn_ok = hit(pawn, 1, "Burn")
            elseif k == 4 then
                r.burn = h
                log("AVATAR godtest: hp apos Burn=" .. f(h) .. " vivo=" .. tostring(a))
                local q = pawn:K2_GetActorLocation()
                r.z0 = q.Z
                local okT, eT = pcall(function() pawn:K2_SetActorLocation({X = q.X, Y = q.Y, Z = q.Z + 1500}, false, {}, true) end)
                log(string.format("AVATAR godtest: queda de 1500 cm (z %.0f -> %.0f) ok=%s %s", q.Z, q.Z + 1500, tostring(okT), tostring(eT)))
            elseif k > 4 then
                if h and (not r.min or h < r.min) then r.min = h end
                if k >= 12 then  -- 4 s de amostras
                    local q = pawn:K2_GetActorLocation()
                    r.fim, r.vivo = h, a
                    local used = healN - r.h0
                    log("AVATAR godtest: FIM hp_ini=" .. f(r.ini) .. " hp_dano=" .. f(r.dano) .. " hp_burn=" .. f(r.burn) .. " hp_min_queda=" .. f(r.min) .. " hp_fim=" .. f(r.fim) .. " vivo=" .. tostring(r.vivo)
                        .. " metodo_h_usado=" .. used .. string.format(" max=%s max_fim=%s z_ini=%.0f z_fim=%.0f dano_ok=%s burn_ok=%s morreu=%s", f(r.max), f(m), r.z0 or 0, q.Z, tostring(r.dmg_ok), tostring(r.burn_ok), tostring(r.morreu == true)))
                    godTestJson = string.format('{"done":true,"hp_ini":%s,"hp_dano":%s,"hp_burn":%s,"hp_min_queda":%s,"hp_fim":%s,"max":%s,"vivo":%s,"morreu":%s,"metodo_h_usado":%d,"dano_ok":%s,"burn_ok":%s,"z_ini":%.0f,"z_fim":%.0f,"camadas":"%s","playergod":%s}',
                        f(r.ini), f(r.dano), f(r.burn), f(r.min), f(r.fim), f(r.max), tostring(r.vivo == true), tostring(r.morreu == true), used, tostring(r.dmg_ok == true), tostring(r.burn_ok == true), r.z0 or 0, q.Z, tostring(FL.godlayers):gsub("[^%w]", ""), tostring(FL.playergod))
                    godTestActive = false
                    return true
                end
            end
            return false
        end)
    end
    local function applyGod(pawn)
        local done = {}
        local function try(m, f)
            local ok, e = pcall(f)
            if ok then done[#done + 1] = m end
            log("AVATAR playergod: metodo=" .. m .. " ok=" .. tostring(ok) .. " " .. tostring(e))
        end
        local function layer(c) return tostring(FL.godlayers):find(c, 1, true) ~= nil end
        if layer("a") then try("a", function()
            local med = StaticFindObject("/Script/Stalker2.Default__CppMediator")
            if not (med and med:IsValid()) then error("CppMediator invalido") end
            med:MakeInvulnerable(pawn)
        end) end
        if layer("b") then try("b", function() pawn.bCanBeDamaged = false; if pawn.bCanBeDamaged ~= false then error("bCanBeDamaged continua " .. tostring(pawn.bCanBeDamaged)) end end) end
        log("AVATAR playergod: metodo=c ok=false sem EntityUID legivel (nao usado)")
        if FL.godtoggle == 1 and not toggleDone then
            toggleDone = true
            try("d", function()
                local mgr = FindFirstOf("CustomConsoleManagerRK")
                if not (mgr and mgr:IsValid()) then error("CustomConsoleManagerRK invalido") end
                mgr:XSetGodMode(true)
            end)
        end
        -- v10 (a e b medidos: NAO bloqueiam queda). Camadas novas, cada uma sozinha por godlayers; todas NAO PROVADAS:
        -- d) XSetGodMode(true) 1x  f) XSetFullGodMode(true) 1x  m) Obj:SetDamageMultiplier(0)  x) Obj:SetMaxHP(100000)+ForceSetHP
        -- j) sem dano de queda: laco de 100 ms (game thread) chama Obj:ResetLastJumpHeight() enquanto o pawn cai  r) morto -> Obj:Resurrect(max,false)
        if layer("d") and not toggleDone then
            toggleDone = true
            try("d", function()
                local mgr = FindFirstOf("CustomConsoleManagerRK")
                if not (mgr and mgr:IsValid()) then error("CustomConsoleManagerRK invalido") end
                mgr:XSetGodMode(true)
            end)
        end
        if layer("f") and not fullDone then
            fullDone = true
            try("f", function()
                local mgr = FindFirstOf("CustomConsoleManagerRK")
                if not (mgr and mgr:IsValid()) then error("CustomConsoleManagerRK invalido") end
                mgr:XSetFullGodMode(true)
            end)
        end
        if layer("m") then try("m", function()
            local before = pawn:GetDamageMultiplier()
            pawn:SetDamageMultiplier(0.0)
            log("AVATAR playergod: metodo=m DamageMultiplier " .. tostring(before) .. " -> " .. tostring(pawn:GetDamageMultiplier()))
        end) end
        if layer("x") then try("x", function()
            local before = pawn:GetMaxHP()
            pawn:SetMaxHP(100000.0); pawn:ForceSetHP(100000)
            log("AVATAR playergod: metodo=x MaxHP " .. tostring(before) .. " -> " .. tostring(pawn:GetMaxHP()) .. " hp=" .. tostring(pawn:GetHP()))
        end) end
        if layer("j") and not jLoop then
            jLoop = true
            done[#done + 1] = "j"
            local jn, jerr = 0, 0
            gameLoop(100, "semqueda", function()
                local pw = FindFirstOf("BP_Stalker2Character_C")
                if not (pw and pw:IsValid()) then return false end
                local mm = 0
                pcall(function() mm = pw.CharacterMovement.MovementMode end)
                if mm == 3 then
                    local okJ, eJ = pcall(function() pw:ResetLastJumpHeight() end)
                    jn = jn + 1
                    if not okJ then jerr = jerr + 1 end
                    if jn == 1 or jn % 50 == 0 then log("AVATAR playergod: metodo=j ResetLastJumpHeight ok=" .. tostring(okJ) .. " " .. tostring(eJ) .. " chamadas=" .. jn .. " erros=" .. jerr) end
                end
                return false
            end)
            log("AVATAR playergod: metodo=j ok=true laco de 100 ms iniciado")
        end
        godInfo = table.concat(done, "")
        local hp, mx, alive = readVitals(pawn)
        log("AVATAR playergod: aplicado metodos=" .. godInfo .. " hp=" .. tostring(hp) .. " max=" .. tostring(mx) .. " vivo=" .. tostring(alive))
    end
    gameLoop(1000, "local", function()
        n = n + 1
        local doBeacon = (FL.beacon == 1 and n % 5 == 0)
        if n % 60 == 0 then log("AVATAR laco local vivo: n=" .. n .. " godtest=" .. tostring(FL.godtest) .. " feito=" .. tostring(gtDone) .. " ativo=" .. tostring(godTestActive)) end
        -- v11 (NAO PROVADO): arranque sem teclas. autocontinue=1: sem pawn local, dispensa a tela "pressione qualquer tecla"
        -- (IdleScreenView:OnAnyKeyIsPressed) e carrega o ultimo save (CustomConsoleManagerMP:XLoadLastSave) quando o menu principal existe
        -- (MainMenuView/MainMenuGameMode); uma vez por lancamento (para ao existir pawn). autobridge=1: liga a ponte 5 s apos o pawn existir (1x).
        if (FL.autocontinue == 1 and not acDone) or (FL.autobridge == 1 and not abDone) then
            local okA, eA = pcall(function()
                local now = os.clock()
                local pawn = FindFirstOf("BP_Stalker2Character_C")
                local inWorld = pawn and pawn:IsValid()
                if inWorld then
                    if FL.autocontinue == 1 and not acDone then acDone = true; log("AVATAR autocontinue: pawn existe, fim (carregamentos=" .. acLoads .. " teclas=" .. acKeys .. ")") end
                    if FL.autobridge == 1 and not abDone then
                        abAt = abAt or (now + 5)
                        if now >= abAt then
                            abDone = true
                            if bridgeOn then log("AVATAR autobridge: ponte ja ligada")
                            elseif type(S2_toggleBridge) == "function" then S2_toggleBridge(); log("AVATAR autobridge: ponte ligada=" .. tostring(bridgeOn))
                            else log("AVATAR autobridge: erro S2_toggleBridge ausente") end
                        end
                    end
                    return
                end
                abAt = nil
                if FL.autocontinue ~= 1 or acDone then return end
                if now < acNext then return end
                local idle = FindFirstOf("IdleScreenView")
                local menu = FindFirstOf("MainMenuView")
                local gm = FindFirstOf("MainMenuGameMode")
                local hasIdle, hasMenu, hasGm = (idle and idle:IsValid()) and true or false, (menu and menu:IsValid()) and true or false, (gm and gm:IsValid()) and true or false
                if not acSeen then acSeen = true; log("AVATAR autocontinue: sem pawn; idle=" .. tostring(hasIdle) .. " menu=" .. tostring(hasMenu) .. " gamemode_menu=" .. tostring(hasGm)) end
                -- v13: a tela ociosa so aceita a tecla depois que o carregamento inicial termina (~100 s): repetir a cada 3 s, sem limite, ate o menu existir
                if hasIdle and not (hasMenu or hasGm) then
                    acKeys = acKeys + 1; acNext = now + 3
                    local okK, eK = pcall(function() idle:OnAnyKeyIsPressed() end)
                    if acKeys <= 3 or acKeys % 10 == 0 then log("AVATAR autocontinue: OnAnyKeyIsPressed ok=" .. tostring(okK) .. " " .. tostring(eK) .. " (#" .. acKeys .. ")") end
                    return
                end
                if (hasMenu or hasGm) then
                    if not acMenuAt then acMenuAt = now; log("AVATAR autocontinue: menu detectado (teclas=" .. acKeys .. " menu=" .. tostring(hasMenu) .. " gamemode_menu=" .. tostring(hasGm) .. " idle=" .. tostring(hasIdle) .. ")") end
                    if now - acMenuAt >= 3 and acLoads < 3 then  -- menu estavel ha 3 s
                        acLoads = acLoads + 1; acNext = now + 60
                        local okL2, eL2 = pcall(function()
                            local mgr = FindFirstOf("CustomConsoleManagerMP")
                            if not (mgr and mgr:IsValid()) then error("CustomConsoleManagerMP invalido") end
                            mgr:XLoadLastSave()
                        end)
                        log("AVATAR autocontinue: XLoadLastSave ok=" .. tostring(okL2) .. " " .. tostring(eL2) .. " (#" .. acLoads .. ") menu=" .. tostring(hasMenu) .. " gamemode_menu=" .. tostring(hasGm))
                    elseif acLoads >= 3 then acDone = true; log("AVATAR autocontinue: desisti apos 3 carregamentos sem pawn") end
                end
            end)
            if not okA then acErr = acErr + 1; if acErr <= 5 then log("AVATAR autocontinue: erro " .. tostring(eA)) end end
        end
        if S2_godtestRearm then S2_godtestRearm = false; gtManual = true; gtDone = false; gtAt = os.clock(); log("AVATAR godtest: rearmado por NUM7") end
        if (FL.godtest == 1 or gtManual) and not gtDone then
            local okT, eT = pcall(function()
                local pawn = FindFirstOf("BP_Stalker2Character_C")
                if not (pawn and pawn:IsValid()) then gtAt = nil; return end
                gtAt = gtAt or (os.clock() + 10)
                if os.clock() >= gtAt then runGodTest(pawn) end
            end)
            if not okT then gtDone = true; godTestActive = false; log("AVATAR godtest: erro " .. tostring(eT)) end
        end
        if FL.playergod ~= 1 and not doBeacon then return false end
        do
            do
                if FL.playergod == 1 then
                    local ok, e = pcall(function()
                        local pawn = FindFirstOf("BP_Stalker2Character_C")
                        if not (pawn and pawn:IsValid()) then godAt = nil; godPawn = nil; return end
                        local addr = pawn:GetAddress()
                        if addr ~= godPawn then godPawn = addr; godAt = os.clock() + 5; godInfo = "-"; lowLogged = false end  -- pawn novo: reaplica em 5 s
                        if godAt and os.clock() >= godAt then godAt = nil; applyGod(pawn) end
                        if godAt then return end
                        local hp, mx, alive = readVitals(pawn)
                        if tostring(FL.godlayers):find("h", 1, true) and hp and mx and mx > 0 and alive ~= false and hp < mx * 0.5 then
                            healN = healN + 1
                            local okH, eH = pcall(function() pawn:ForceSetHP(math.floor(mx)) end)
                            if healN <= 10 or healN % 50 == 0 then log(string.format("AVATAR playergod: metodo=h hp=%.1f/%.1f -> ForceSetHP ok=%s %s (#%.0f)", hp, mx, tostring(okH), tostring(eH), healN)) end
                        end
                        if alive == false and not lowLogged then lowLogged = true; log("AVATAR playergod: JOGADOR MORTO hp=" .. tostring(hp) .. " metodos=" .. godInfo) end
                        if alive == false and tostring(FL.godlayers):find("r", 1, true) and resN < 20 then
                            resN = resN + 1
                            local okR, eR = pcall(function() pawn:Resurrect(mx or 100.0, false) end)
                            local h2, _, a2 = readVitals(pawn)
                            log("AVATAR playergod: metodo=r Resurrect ok=" .. tostring(okR) .. " " .. tostring(eR) .. " hp=" .. tostring(h2) .. " vivo=" .. tostring(a2) .. " (#" .. resN .. ")")
                            if a2 == true then lowLogged = false end
                        end
                    end)
                    if not ok then log("AVATAR playergod: erro laco " .. tostring(e)) end
                end
                if doBeacon then
                    local okB, eB = pcall(writeState)
                    if not okB then beaconErrs = beaconErrs + 1; if beaconErrs <= 5 then log("AVATAR estado: erro " .. tostring(eB)) end end
                end
            end
        end
        return false
    end)
end
-- autowalk=1 (so teste): pawn local anda sozinho, chamado pelo laco da ponte (100 ms, NUM0 ligado). Sem laco proprio: v1 (LoopAsync 16 ms) corrompeu
-- o estado Lua no cliente; v2 (SimpleMoveToLocation) nao anda (PlayerController nao segue caminho). Modo "input": AddMovementInput(dir,1) a 10 Hz;
-- se em 5 s a velocidade medida < 100 cm/s, modo "passo": K2_SetActorLocation 30 cm por chamada com raio de chao (ignora o proprio pawn).
-- Linha fixa (yaw de controle inicial), sentido invertido a cada 20 s, nunca alem de 150 m da origem. Log a cada 10 s.
local aw = {yaw = nil}
local function autowalkTick(pawn, p)
    if godTestActive then aw.lastZ = nil; return end  -- nao mexer no pawn durante a prova de queda
    local now = os.clock()
    if not aw.yaw then
        local y = 0
        pcall(function() y = pawn:GetControlRotation().Yaw end)
        local hh = 90
        pcall(function() hh = pawn.CapsuleComponent:GetScaledCapsuleHalfHeight() end)
        aw = {yaw = y, o = {X = p.X, Y = p.Y}, sign = 1, flipAt = now + 20, logAt = now + 10, mode = "input", chkAt = now + 5,
              chkP = {X = p.X, Y = p.Y}, chkT = now, lastP = {X = p.X, Y = p.Y}, lastT = now, hh = hh, errs = 0, semchao = 0}
        log(string.format("AVATAR autowalk: inicio yaw=%.1f origem=%.0f,%.0f,%.0f meia_altura=%.0f", y, p.X, p.Y, p.Z, hh))
    end
    local dist = math.sqrt((p.X - aw.o.X)^2 + (p.Y - aw.o.Y)^2)
    local r = math.rad(aw.yaw)
    local dx, dy = math.cos(r), math.sin(r)
    if now >= aw.flipAt then aw.sign = -aw.sign; aw.flipAt = now + 20 end
    if dist > 15000 then  -- coleira de 150 m: sempre de volta para a origem
        aw.sign = (((p.X - aw.o.X) * dx + (p.Y - aw.o.Y) * dy) > 0) and -1 or 1
    end
    if gamePaused == true then aw.pausedWin = true end  -- so informativo (log pausado=); o autowalk nao depende do farol
    if aw.mode == "input" then
        pawn:AddMovementInput({X = dx * aw.sign, Y = dy * aw.sign, Z = 0}, 1.0, false)
        if now >= aw.chkAt then
            local v = math.sqrt((p.X - aw.chkP.X)^2 + (p.Y - aw.chkP.Y)^2) / math.max(now - aw.chkT, 0.001)
            if v < 100 then aw.mode = "passo" end
            aw.chkAt = math.huge
            log(string.format("AVATAR autowalk: teste input vel=%.0f -> modo=%s", v, aw.mode))
        end
    else
        -- v4: SEM LineTraceSingle (v3 caiu com AV 0xffff... no UE4SS.dll; suspeita: HitResult de saida a 10 Hz com ponteiros fracos soltos).
        -- Passo de 25 cm, 10 cm acima; o CharacterMovement (andando) acha o chao sozinho. Caindo (MovementMode 3) = nao empurra; se cai >300 cm, volta.
        local mm = 1
        pcall(function() mm = pawn.CharacterMovement.MovementMode end)
        if mm == 3 then
            aw.semchao = aw.semchao + 1
            if aw.lastZ and aw.lastZ - p.Z > 300 then aw.sign = -aw.sign; aw.flipAt = now + 20; aw.lastZ = nil end
        else
            -- v7: sem elevacao (v6 subiu ate Z=16697 com o jogo pausado); Z atual, o CharacterMovement acha o chao; Z sobe no maximo 30 cm por passo
            local nz = p.Z
            if aw.lastZ and nz > aw.lastZ + 30 then nz = aw.lastZ + 30; aw.zclamp = (aw.zclamp or 0) + 1 end
            aw.lastZ = nz
            pawn:K2_SetActorLocation({X = p.X + dx * aw.sign * 25, Y = p.Y + dy * aw.sign * 25, Z = nz}, false, {}, true)
        end
    end
    if now >= aw.logAt then
        local spd = math.sqrt((p.X - aw.lastP.X)^2 + (p.Y - aw.lastP.Y)^2) / math.max(now - aw.lastT, 0.001)
        aw.logAt, aw.lastP, aw.lastT = now + 10, {X = p.X, Y = p.Y}, now
        local pw = aw.pausedWin and true or false
        aw.pausedWin = false
        log(string.format("AVATAR autowalk: modo=%s pos=%.0f,%.0f,%.0f vel=%.0f dist_origem=%.0f sentido=%.0f caindo=%.0f erros=%.0f pausado=%s zclamp=%.0f", aw.mode, p.X, p.Y, p.Z, spd, dist, aw.sign, aw.semchao, aw.errs, tostring(pw), aw.zclamp or 0))
        -- v6: o input pode parar de funcionar depois (foco/Ctrl+R); se a janela inteira sem pausa ficou lenta, passa para o modo passo
        if aw.mode == "input" and aw.chkAt == math.huge and spd < 100 then
            aw.mode = "passo"; log(string.format("AVATAR autowalk: input parou (vel=%.0f) -> modo=passo", spd))
        end
    end
end
local stepsSeen = {}
local clientAlifeDone, clientAlifeAt, clientAlifeChk = false, nil, nil
function passo(n) if not stepsSeen[n] then stepsSeen[n] = true; log("AVATAR passo: " .. n) end end  -- uma vez por avatar; a ultima linha antes de uma queda aponta o culpado
local remote, avatarPos, avYaw = nil, nil, nil
local smoothOn, smoothPending = false, false
local lastT = os.clock()
local avDiagT, avErrAt, avLastSet, aiMoves, avSpeedCmd = 0, -100, nil, 0, 0
local avatarRetryAt, adoptTried = 0, false
local farLogAt = nil
local animInst, animTargets, animRetryAt = nil, nil, 0
local godNextAt, godListed, godLogged = 0, false, 0
local hbMesh, remoteMesh, localYaw = "-", nil, 0
local avatarOk, avatarPath, avatarFar, revalCnt, localP, iaLogged = false, nil, false, 0, nil, 0
local HB = {bridge = 0, nopawn = 0, noin = 0, parsed = 0, snaps = 0, loop = 0, steps = 0, errs = 0, last = -100, lastErr = "-"}
local smoothPendingAt = 0
local goneLogged = false
local function hbErr(where, e)
    HB.errs = HB.errs + 1; HB.lastErr = where .. ": " .. tostring(e):sub(1, 160)
    if HB.errs <= 5 or HB.errs % 50 == 0 then log("AVATAR erro (" .. where .. ") #" .. HB.errs .. ": " .. tostring(e)) end
end
-- batimento a cada 10 s (chamado pelo laco da ponte e pelo de suavizacao): prova se os lacos estao vivos e o que eles veem
local function heartbeat(src)
    local now = os.clock()
    if now - HB.last < 10 then return end
    HB.last = now
    local valid = avatarOk  -- o batimento NAO toca no avatar (so le o estado guardado pela suavizacao)
    log(string.format("AVATAR vivo(%s): avatar_valido=%s longe=%s idade_amostra=%s ultimo_t=%s ponte=%d sem_pawn=%d sem_in=%d parse_ok=%d snapshots=%d smooth_loop=%d smooth_passos=%d pend=%s spawning=%s erros=%d ultimo_erro=%s modo=%s mesh=%s",
        src, tostring(valid), tostring(avatarFar), remote and string.format("%.2fs", now - remote.recv) or "-", remote and tostring(remote.t) or "-", HB.bridge, HB.nopawn, HB.noin,
        HB.parsed, HB.snaps, HB.loop, HB.steps, tostring(smoothPending), tostring(avatarSpawning), HB.errs, HB.lastErr, tostring(FL.avatarmode), tostring(hbMesh)))
end
local function normKey(n) return (n:lower():gsub("[^%a%d]", "")) end
local ANIM_FLOAT = {speed=1, groundspeed=1, movespeed=1, movementspeed=1, velocitylength=1, velocitysize=1, currentspeed=1, charspeed=1, speed2d=1, horizontalspeed=1, walkspeed=1}
local ANIM_BOOL = {ismoving=1, bismoving=1, moving=1, bmoving=1, shouldmove=1, bshouldmove=1, hasvelocity=1, bhasvelocity=1}
-- Descobre por reflexao o que o AnimBP do avatar le: lista (uma vez) as propriedades candidatas no log e guarda as que vamos preencher.
local function animInit()
    animTargets = {}
    local ok, inst = pcall(function() return avatar.Mesh:GetAnimInstance() end)
    if not (ok and inst and inst:IsValid()) then animTargets = nil; animRetryAt = os.clock() + 2; return end
    animInst = inst
    local cands = {}
    local c = inst:GetClass()
    while c and c:IsValid() do
        local cname = c:GetFullName()
        pcall(function()
            c:ForEachProperty(function(prop)
                local n = prop:GetFName():ToString()
                local okc, kind = pcall(function() return prop:GetClass():GetFName():ToString() end)
                kind = okc and kind or "?"
                local k = normKey(n)
                if k:find("speed") or k:find("veloc") or k:find("moving") or k:find("gait") or k:find("accel") or k:find("walk") or k:find("sprint") or k:find("locomo") then
                    cands[#cands + 1] = n .. ":" .. (kind:gsub("Property", ""))
                end
                if (kind == "FloatProperty" or kind == "DoubleProperty") and ANIM_FLOAT[k] then animTargets[#animTargets + 1] = {name = n, kind = "num"}
                elseif kind == "BoolProperty" and ANIM_BOOL[k] then animTargets[#animTargets + 1] = {name = n, kind = "bool"}
                elseif kind == "StructProperty" and (k == "velocity" or k == "movementvelocity") then animTargets[#animTargets + 1] = {name = n, kind = "vec"} end
            end)
        end)
        if #cands > 0 then log("AVATAR anim candidatos em " .. cname .. ": " .. table.concat(cands, ", "):sub(1, 1500)); cands = {} end
        c = c:GetSuperStruct()
    end
    local names = {}
    for _, t in ipairs(animTargets) do names[#names + 1] = t.name .. "(" .. t.kind .. ")" end
    local okA, ac = pcall(function() return avatar.Mesh.AnimClass:GetFullName() end)
    log("AVATAR anim: instancia=" .. tostring(inst:GetFullName()) .. " AnimClass=" .. tostring(okA and ac or "?") .. " preenchidos=[" .. table.concat(names, ",") .. "]")
end
local function animFill(vx, vy, speed)
    if FL.anim ~= 1 then return end
    if animTargets == nil then
        if os.clock() < animRetryAt then return end
        passo("anim: reflexao no AnimInstance (animInit)")
        animInit()
        if animTargets == nil then return end
    end
    if not (animInst and animInst:IsValid()) then animTargets = nil; animRetryAt = 0; return end
    passo("anim: escrita no AnimInstance")
    for _, t in ipairs(animTargets) do
        pcall(function()
            if t.kind == "num" then animInst[t.name] = speed
            elseif t.kind == "bool" then animInst[t.name] = speed > 30
            else animInst[t.name] = {X = vx, Y = vy, Z = 0} end
        end)
    end
end
local function angDiff(a, b) local d = (a - b) % 360; if d > 180 then d = d - 360 end; return d end
-- Imortalidade do avatar (god=1): CppMediator:MakeInvulnerable(Obj) (funcao do jogo, achada em UE4SS_ObjectDump), reaplicada a cada 15 s com revalidacao por nome
-- imediatamente antes; nodamage=1 escreve bCanBeDamaged=false (bitfield, suspeito: so apos provar). Lista uma vez as propriedades de vida da CLASSE (sem ler a instancia).
local function godStep(now)
    if now < godNextAt then return end
    godNextAt = now + 15
    if not godListed then
        godListed = true
        passo("god: candidatos (reflexao de classe)")
        pcall(function()
            local c, found = avatar:GetClass(), {}
            while c and c:IsValid() and #found < 60 do
                pcall(function()
                    c:ForEachProperty(function(prop)
                        local n = prop:GetFName():ToString()
                        local k = normKey(n)
                        if k:find("health") or k:find("immort") or k:find("invuln") or k:find("god") or k:find("unkill") or k:find("damage") or k:find("vital") or k:find("hp") then found[#found + 1] = n end
                    end)
                end)
                c = c:GetSuperStruct()
            end
            log("AVATAR god candidatos: " .. table.concat(found, ", "):sub(1, 1500))
        end)
    end
    -- revalida por nome logo antes de tocar o objeto
    local okF, found = pcall(function()
        local o = avatarPath and StaticFindObject(avatarPath)
        return o ~= nil and o:IsValid() and o:GetAddress() == avatar:GetAddress()
    end)
    if not (okF and found) then return end
    if FL.god == 1 then
        passo("god: CppMediator:MakeInvulnerable")
        local okG, eG = pcall(function()
            local m = StaticFindObject("/Script/Stalker2.Default__CppMediator")
            if not (m and m:IsValid()) then error("Default__CppMediator ausente") end
            m:MakeInvulnerable(avatar)
        end)
        if godLogged < 3 then godLogged = godLogged + 1; log("AVATAR god: MakeInvulnerable ok=" .. tostring(okG) .. (okG and "" or (" " .. tostring(eG)))) end
    end
    if FL.nodamage == 1 then
        passo("nodamage: bCanBeDamaged=false")
        pcall(function() avatar.bCanBeDamaged = false end)
    end
end
-- v28 avatarmode=npcpuppet: o outro jogador e um NPC REAL do jogo (S2Coop_RemotePlayer, pak zzzzzz_S2CoopAvatar_P; Faction = Player, alvo da IA),
-- movido por K2_SetActorLocation e animado por Mesh:PlayAnimation (modo de animacao unica: a IA do jogo nao consegue mais anda-lo).
-- Medido 2026-10-10: balanco do pe 71 cm com AS_tp_bh_walk_fwd (IA nativa 78 cm, deslize puro 0 cm).
local PUP = {state = nil, rate = nil, hidden = false, seqs = {}}
local PUP_ANIM = {
    idle = "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/idles/AS_tp_bh_idle.AS_tp_bh_idle",
    walk = "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/locomotion/stand/AS_tp_bh_walk_fwd.AS_tp_bh_walk_fwd",
    run = "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/locomotion/stand/AS_tp_bh_run_fwd.AS_tp_bh_run_fwd",
}
local PUP_NAT = {walk = 114, run = 285}  -- cm/s medidos com a IA nativa (andar / correr)
local function npcMode() return FL.avatarmode == "npc" or FL.avatarmode == "npcpuppet" end
local function pupSeq(st)
    local q = PUP.seqs[st]
    if q and q:IsValid() then return q end
    q = StaticFindObject(PUP_ANIM[st])
    if not (q and q:IsValid()) then pcall(function() q = LoadAsset(PUP_ANIM[st]) end) end
    if q and q:IsValid() then PUP.seqs[st] = q; return q end
end
local function puppetAnim(speed)
    local st = (speed < 30 and "idle") or (speed < 230 and "walk") or "run"
    if st ~= PUP.state then  -- PlayAnimation so na troca de estado
        local q = pupSeq(st)
        if not q then if PUP.missing ~= st then PUP.missing = st; log("AVATAR puppet: animacao indisponivel " .. st) end; return end
        local ok, e = pcall(function() avatar.Mesh:PlayAnimation(q, true) end)
        log(string.format("AVATAR puppet: anim %s -> %s (v=%.0f) ok=%s %s", tostring(PUP.state), st, speed, tostring(ok), ok and "" or tostring(e)))
        if not ok then return end
        PUP.state = st; PUP.rate = nil
    end
    local r = 1.0
    if PUP_NAT[st] then r = math.max(0.6, math.min(1.8, speed / PUP_NAT[st])) end
    if not PUP.rate or math.abs(r - PUP.rate) > 0.15 then PUP.rate = r; pcall(function() avatar.Mesh:SetPlayRate(r) end) end
end
-- volta o NPC para o AnimBP do jogo (sair do modo puppet sem relancar)
local function puppetRestore()
    local ok, e = pcall(function() avatar.Mesh:SetAnimationMode(0, true) end)
    log("AVATAR puppet: AnimBP restaurado ok=" .. tostring(ok) .. " " .. (ok and "" or tostring(e)))
    PUP.state = nil; PUP.rate = nil
    if PUP.hidden then PUP.hidden = false; pcall(function() avatar:SetActorHiddenInGame(false) end) end
end
local function smoothStep()
    HB.steps = HB.steps + 1
    heartbeat("suavizacao")
    if not avatar then avatarOk = false; return end
    -- o UObject pode morrer (despawn do A-Life) e IsValid() ainda dizer true por um tempo: revalida por nome a cada ~1 s e trata como possivelmente morto
    revalCnt = revalCnt + 1
    local alive = true
    local okI, valid = pcall(function() return avatar:IsValid() end)
    if not (okI and valid) then alive = false
    elseif FL.revalidate == 1 and avatarPath and revalCnt >= 30 then
        revalCnt = 0
        local okF, found = pcall(function()
            local o = StaticFindObject(avatarPath)
            return o ~= nil and o:IsValid() and o:GetAddress() == avatar:GetAddress()
        end)
        if not (okF and found) then alive = false end
    end
    if not alive then
        if not goneLogged then goneLogged = true; log("AVATAR invalido (destruido/despawnado pelo jogo?); sera recriado") end
        avatar = nil; avatarOk = false; avatarPos = nil; avLastSet = nil; avatarRetryAt = os.clock() + 2
        return
    end
    avatarOk = true
    if (FL.god == 1 or FL.nodamage == 1) and FL.avatarmode ~= "npcpuppet" then godStep(os.clock()) end
    if FL.avatarmode ~= "npcpuppet" and PUP.state then puppetRestore() end
    if not remote then return end
    local now = os.clock(); local dt = math.min(math.max(now - lastT, 0.001), 0.25); lastT = now
    local age = now - remote.recv
    if FL.avatarmode == "npcpuppet" then  -- remoto sumiu (sem snapshot ha 5 s) ou avatar=0: esconde o NPC (nao destroi: K2_DestroyActor em NPC do jogo ja derrubou o jogo)
        local away = age > 5 or FL.avatar ~= 1
        if away ~= PUP.hidden then
            PUP.hidden = away
            local okH, eH = pcall(function() avatar:SetActorHiddenInGame(away) end)
            log("AVATAR puppet: escondido=" .. tostring(away) .. " ok=" .. tostring(okH) .. " " .. (okH and "" or tostring(eH)))
        end
        if away then return end
    end
    local rvx, rvy = remote.vx, remote.vy
    if age > 0.6 then rvx, rvy = 0, 0 end
    local ex = math.min(age, 0.3)
    local tx, ty, tz = remote.x + rvx * ex + FL.avoffx, remote.y + rvy * ex, remote.z + AVATAR_DZ  -- avoffx: deslocamento de TESTE (loopback), 0 em producao
    -- A-Life so mantem NPC dentro da bolha: o avatar nunca fica alem de maxdist do jogador local (fica na borda, na direcao do remoto)
    avatarFar = false
    if FL.maxdist > 0 and localP then
        local ddx, ddy = tx - localP.X, ty - localP.Y
        local dd = math.sqrt(ddx * ddx + ddy * ddy)
        if dd > FL.maxdist then
            local k = FL.maxdist / dd
            tx, ty, tz = localP.X + ddx * k, localP.Y + ddy * k, localP.Z; avatarFar = true
        end
    end
    if not avatarPos then avatarPos = {X = tx, Y = ty, Z = tz} end
    -- (so com diag=1) algo moveu o avatar entre dois passos? compara com o ultimo ponto escrito; os 5 primeiros casos sao logados com a distancia
    if FL.diag == 1 and avLastSet then
        passo("diag: leitura K2_GetActorLocation")
        pcall(function()
            local cur = avatar:K2_GetActorLocation()
            local d2 = (cur.X - avLastSet.X)^2 + (cur.Y - avLastSet.Y)^2 + (cur.Z - avLastSet.Z)^2
            if d2 > 40^2 then
                aiMoves = aiMoves + 1
                if iaLogged < 5 then iaLogged = iaLogged + 1; log(string.format("AVATAR ia: movido %.0f cm entre passos; lido=(%.0f,%.0f,%.0f) escrito=(%.0f,%.0f,%.0f)", math.sqrt(d2), cur.X, cur.Y, cur.Z, avLastSet.X, avLastSet.Y, avLastSet.Z)) end
            end
        end)
    end
    local dx, dy = tx - avatarPos.X, ty - avatarPos.Y
    local nx, ny, nz
    if dx * dx + dy * dy > 2000 * 2000 then nx, ny, nz = tx, ty, tz
    else
        local k = 1 - math.exp(-dt * 12)
        nx, ny, nz = avatarPos.X + dx * k, avatarPos.Y + dy * k, avatarPos.Z + (tz - avatarPos.Z) * k
    end
    avatarPos = {X = nx, Y = ny, Z = nz}
    local speed = math.sqrt(rvx * rvx + rvy * rvy)
    avSpeedCmd = speed
    -- yaw: direcao do movimento quando anda; senao o yaw do remetente
    local want = (speed > 100 and math.deg(math.atan(rvy, rvx))) or remote.yaw or avYaw or 0
    if not avYaw then avYaw = want end
    avYaw = avYaw + angDiff(want, avYaw) * (1 - math.exp(-dt * 10))
    passo("mover: K2_SetActorLocation")
    local okS, eS = pcall(function()
        avatar:K2_SetActorLocation({X = nx, Y = ny, Z = nz}, false, {}, true)  -- unica chamada de movimento provada sem crash
    end)
    if FL.rotate == 1 then
        passo("rotate: K2_SetActorRotation")
        pcall(function() avatar:K2_SetActorRotation({Pitch = 0, Yaw = avYaw, Roll = 0}, false) end)
    end
    if not okS and now - avErrAt > 10 then avErrAt = now; log("AVATAR erro ao mover: " .. tostring(eS)) end
    avLastSet = {X = nx, Y = ny, Z = nz}
    if FL.velwrite == 1 then
        passo("velwrite: CharacterMovement.Velocity")
        pcall(function() avatar.CharacterMovement.Velocity = {X = rvx, Y = rvy, Z = 0} end)
    end
    if FL.compvel == 1 then
        passo("compvel: CapsuleComponent.ComponentVelocity")
        pcall(function() avatar.CapsuleComponent.ComponentVelocity = {X = rvx, Y = rvy, Z = 0} end)
    end
    if FL.ctrlrot == 1 then  -- a IA nao pode girar o pawn pelo controller
        passo("ctrlrot: GetController/SetControlRotation")
        pcall(function()
            local ctl = avatar:GetController()
            if ctl and ctl:IsValid() then ctl:SetControlRotation({Pitch = 0, Yaw = avYaw, Roll = 0}) end
        end)
    end
    animFill(rvx, rvy, speed)
    if FL.avatarmode == "npcpuppet" then puppetAnim(speed) end
    if FL.diag == 1 and now - avDiagT >= 2 then
        avDiagT = now
        passo("diag: leituras MovementMode/Velocity")
        local mm, real, vv = "?", nil, "?"
        pcall(function() mm = avatar.CharacterMovement.MovementMode end)
        if FL.disablemove == 1 and tonumber(mm) ~= 0 then pcall(function() avatar.CharacterMovement:DisableMovement() end) end
        pcall(function() real = avatar:K2_GetActorLocation() end)
        pcall(function() local v = avatar.CharacterMovement.Velocity; vv = string.format("%.0f,%.0f", v.X, v.Y) end)
        log(string.format("AVATAR diag: alvo=(%.0f,%.0f,%.0f) pos=(%s) vel=%.0f cm/s (cmd=%.0f,%.0f lido=%s) movimento=%s(mode, 0=None) ia_moveu=%d idade_amostra=%.2fs yaw=%.0f remetente_t=%s",
            tx, ty, tz, real and string.format("%.0f,%.0f,%.0f", real.X, real.Y, real.Z) or "?", speed, rvx, rvy, vv, tostring(mm), aiMoves, age, avYaw, tostring(remote.t)))
    end
end
-- ===== avatarmode=poseable: o outro jogador e um ator PROPRIO (Actor + PoseableMeshComponent, como os espelhos), sem NPC do A-Life.
-- Cada lado captura UMA vez (jogador parado) o mesh e a pose idle (transformas de componente de todos os ossos) de um NPC humano proximo e manda o caminho do mesh em out.json (`avmesh`).
local myMesh, myPose, meshTryAt = nil, nil, 0
local myParts, myPartsStr, remoteParts = {}, "", nil  -- partes extras do NPC (cabeca, maos...): {path=, pose=}
local pav, pavNextTry, pavLastT, pavPos, pavYaw, pavNoLog = nil, 0, os.clock(), nil, nil, -100
local function pavValid()
    if not pav then return false end
    local ok, v = pcall(function() return pav.actor:IsValid() and pav.comp:IsValid() end)
    return ok and v
end
local function capturePose(path, comp)
    local pose = {}
    for _, n in ipairs(bonesOf(path, comp)) do
        local t = comp:GetSocketTransform(n, 2)
        pose[#pose + 1] = {n = n,
            r = {X = t.Rotation.X, Y = t.Rotation.Y, Z = t.Rotation.Z, W = t.Rotation.W},
            t = {X = t.Translation.X, Y = t.Translation.Y, Z = t.Translation.Z},
            s = {X = t.Scale3D.X, Y = t.Scale3D.Y, Z = t.Scale3D.Z}}
    end
    return pose
end
-- lista os SkeletalMeshComponent do NPC alem do principal (cabeca, maos, capacete...) e captura a pose de cada um
local function captureParts(o, mc, mainAsset)
    passo("poseable: partes (Actor:K2_GetComponentsByClass)")
    local comps = compList(o, "/Script/Engine.SkeletalMeshComponent")
    local list, descr, paths = {}, {}, {}
    for _, c in ipairs(comps) do
        if #list >= 8 then break end
        pcall(function()
            if not (c and c:IsValid()) or c:GetAddress() == mc:GetAddress() then return end
            passo("poseable: parte GetSkeletalMeshAsset")
            local a = c:GetSkeletalMeshAsset()
            if not (a and a:IsValid()) then return end
            local path = (a:GetFullName():gsub("^%S+ ", ""))
            if path == mainAsset then return end
            passo("poseable: parte GetSocketTransform (ossos)")
            list[#list + 1] = {path = path, pose = capturePose(path, c)}
            paths[#paths + 1] = path
            descr[#descr + 1] = c:GetFName():ToString() .. "=" .. path
        end)
    end
    myParts, myPartsStr = list, table.concat(paths, "|")
    log("AVATAR poseable: partes (" .. #list .. " de " .. #comps .. " SkeletalMeshComponent): " .. table.concat(descr, " "):sub(1, 1500))
end
local function captureSource(pawn, p)
    local now = os.clock()
    if myMesh or now < meshTryAt then return end
    meshTryAt = now + 10
    passo("poseable: captura de mesh/pose (FindAllOf Pawn)")
    local checked = 0
    for _, o in ipairs(FindAllOf("Pawn") or {}) do
        checked = checked + 1
        if checked > 12 or myMesh then break end
        local ok, err = pcall(function()
            if not o:IsValid() or o == pawn or o == avatar then return end
            passo("poseable: K2_GetActorLocation")
            local q = o:K2_GetActorLocation()
            if (q.X - p.X)^2 + (q.Y - p.Y)^2 > 2000^2 then return end
            passo("poseable: revalidar por nome")
            local full = o:GetFullName()
            if full:find("Mutant", 1, true) then return end
            local f = StaticFindObject((full:gsub("^%S+ ", "")))
            if not (f and f:IsValid() and f:GetAddress() == o:GetAddress()) then return end
            passo("poseable: Mesh")
            local mc = o.Mesh
            if not (mc and mc:IsValid()) then return end
            passo("poseable: GetSkeletalMeshAsset")
            local asset = (mc:GetSkeletalMeshAsset():GetFullName():gsub("^%S+ ", ""))
            if not asset:find("/characters/", 1, true) or asset:lower():find("mutant", 1, true) then return end
            passo("poseable: GetSocketTransform (ossos)")
            local pose = {}
            for _, n in ipairs(bonesOf(asset, mc)) do
                local t = mc:GetSocketTransform(n, 2)
                pose[#pose + 1] = {n = n,
                    r = {X = t.Rotation.X, Y = t.Rotation.Y, Z = t.Rotation.Z, W = t.Rotation.W},
                    t = {X = t.Translation.X, Y = t.Translation.Y, Z = t.Translation.Z},
                    s = {X = t.Scale3D.X, Y = t.Scale3D.Y, Z = t.Scale3D.Z}}
            end
            myPose, myMesh, hbMesh = pose, asset, asset
            log("AVATAR poseable: mesh=" .. asset .. " ossos=" .. #pose .. " fonte=" .. full)
            local okP, eP = pcall(captureParts, o, mc, asset)
            if not okP then log("AVATAR poseable: partes erro: " .. tostring(eP)) end
        end)
        if not ok then hbErr("poseable captura", err) end
    end
end
-- remoto "efetivo": com avdebug=1 e sem remoto, usa o proprio jogador local (teste sem o outro lado)
local pavPoseAt, pavDiagAt, pavDbgUntil, pavGroundZ, pavGroundAt = 0, nil, 0, nil, 0
local function curRemote()
    if remote then return remote end
    if FL.avdebug == 1 and localP then return {x = localP.X, y = localP.Y, z = localP.Z, vx = 0, vy = 0, yaw = localYaw, recv = os.clock()} end
end
-- pose idle capturada (espaco de componente, como NUM5: GetSocketTransform(n,2) -> SetBoneTransformByName(n,t,1)); reaplicada a cada 1 s
local function applyPoseTo(comp, pose)
    local n = 0
    for _, e in ipairs(pose) do
        if pcall(function() comp:SetBoneTransformByName(e.n, {Rotation = e.r, Translation = e.t, Scale3D = e.s}, 1) end) then n = n + 1 end
    end
    return n
end
local function applyPose()
    if not (myPose and pavValid()) then return 0 end
    local n = applyPoseTo(pav.comp, myPose)
    for _, pt in ipairs(pav.parts or {}) do  -- partes com pose propria (fallback se o leader pose nao pegar)
        if pt.pose then pcall(applyPoseTo, pt.comp, pt.pose) end
    end
    return n
end
-- forca visibilidade/refresh (cada chamada em pcall; passo uma vez)
local function forceVisible()
    local c, a = pav.comp, pav.actor
    for _, pt in ipairs(pav.parts or {}) do
        local pc = pt.comp
        pcall(function() pc:SetVisibility(true, true) end)
        pcall(function() pc:SetHiddenInGame(false, true) end)
        pcall(function() pc:SetComponentTickEnabled(true) end)
        pcall(function() pc.BoundsScale = 8 end)
        pcall(function() pc:RefreshBoneTransforms(nil) end)
    end
    passo("poseable: visibilidade (SetVisibility/SetHiddenInGame/SetActorHiddenInGame/tick/BoundsScale)")
    pcall(function() c:SetVisibility(true, true) end)
    pcall(function() c:SetHiddenInGame(false, true) end)
    pcall(function() a:SetActorHiddenInGame(false) end)
    pcall(function() c:SetComponentTickEnabled(true) end)
    pcall(function() c.BoundsScale = 8 end)
    passo("poseable: RefreshBoneTransforms/MarkRefreshTransformDirty")
    pcall(function() c:RefreshBoneTransforms(nil) end)
    pcall(function() c:MarkRefreshTransformDirty() end)
end
-- diag unico: localizacao mundial do ator/componente, escala, visibilidade, bounds e transform de ossos lido de volta
local function diagPose()
    passo("poseable: diag")
    local c, a = pav.comp, pav.actor
    local function v3(v) return v and string.format("(%.0f,%.0f,%.0f)", v.X, v.Y, v.Z) or "?" end
    local parts = {}
    local function try(name, f) local ok, r = pcall(f); parts[#parts + 1] = name .. "=" .. (ok and tostring(r) or "erro") end
    try("ator", function() return v3(a:K2_GetActorLocation()) end)
    try("comp", function() return v3(c:K2_GetComponentLocation()) end)
    try("escala", function() return v3(c:K2_GetComponentScale()) end)
    try("visivel", function() return c:IsVisible() end)
    try("partes", function() local t = {}; for _, pt in ipairs(pav.parts or {}) do t[#t + 1] = (pt.leader and "L" or "-") .. (pt.pose and "P" or "-") .. (pt.comp:IsValid() and "v" or "x") end; return #(pav.parts or {}) .. "[" .. table.concat(t, ",") .. "]" end)
    try("malha", function() return c:GetSkinnedAsset():GetFullName() end)
    try("ossos", function() return c:GetNumBones() end)
    if myPose and myPose[1] then
        for _, idx in ipairs({1, math.min(#myPose, 3)}) do
            local e = myPose[idx]
            try("osso" .. idx, function()
                local t = c:GetBoneTransformByName(e.n, 1)
                return tostring(e.n:ToString()) .. " lido T" .. v3(t.Translation) .. " S" .. v3(t.Scale3D) .. " capt T" .. v3(e.t)
            end)
        end
    end
    log("AVATAR poseable: diag " .. table.concat(parts, " "))
end
local function traceGroundQuiet(x, y, feet)
    local ok, res = pcall(function()
        local ksl = StaticFindObject("/Script/Engine.Default__KismetSystemLibrary")
        local out = {}
        local h = ksl:LineTraceSingle(UEHelpers.GetWorld(), {X = x, Y = y, Z = feet + 300}, {X = x, Y = y, Z = feet - 600}, 0, false, {}, 0, out, true, {R = 1, G = 0, B = 0, A = 1}, {R = 0, G = 1, B = 0, A = 1}, 0)
        return {hit = h == true, z = out.ImpactPoint and out.ImpactPoint.Z}
    end)
    if ok and res and res.hit and res.z and math.abs(res.z - feet) < 150 then return res.z end
end
local function ensurePoseAvatar()
    if pavValid() then return end
    local r = curRemote()
    if not r then return end
    local now = os.clock()
    if now < pavNextTry then return end
    local path = remoteMesh or myMesh
    if not path then
        if now - pavNoLog > 10 then pavNoLog = now; log("AVATAR poseable: aguardando mesh (nenhum lado achou um NPC humano parado perto)") end
        return
    end
    pavNextTry = now + 10
    pav = nil
    passo("poseable: StaticFindObject/LoadAsset do mesh")
    local asset = StaticFindObject(path)
    if not (asset and asset:IsValid()) then pcall(function() asset = LoadAsset(path) end) end
    if not (asset and asset:IsValid()) then log("AVATAR poseable: mesh indisponivel " .. path); return end
    passo("poseable: newMirror (SpawnActor Actor + PoseableMeshComponent)")
    local ok, m = pcall(newMirror, asset, {X = r.x, Y = r.y, Z = r.z - 90}, r.yaw or 0)
    if not (ok and m) then hbErr("poseable criar", m); return end
    pav, pavPos, pavYaw, hbMesh = m, nil, nil, path
    pav.parts = {}
    -- partes extras (cabeca/maos...): um PoseableMeshComponent por parte no MESMO ator, seguindo a pose do principal (leader pose) e com pose propria de reserva
    local plist = {}
    for pth in (remoteParts or myPartsStr or ""):gmatch("[^|]+") do plist[#plist + 1] = pth end
    for _, pth in ipairs(plist) do
        if #pav.parts >= 8 then break end
        passo("poseable: parte AddComponentByClass/SetSkinnedAssetAndUpdate/SetLeaderPoseComponent")
        local okP, eP = pcall(function()
            local pa = StaticFindObject(pth)
            if not (pa and pa:IsValid()) then pcall(function() pa = LoadAsset(pth) end) end
            if not (pa and pa:IsValid()) then error("mesh indisponivel " .. pth) end
            local pc = m.actor:AddComponentByClass(StaticFindObject("/Script/Engine.PoseableMeshComponent"), false, {Translation = {X = 0, Y = 0, Z = 0}, Rotation = {X = 0, Y = 0, Z = 0, W = 1}, Scale3D = {X = 1, Y = 1, Z = 1}}, false)
            pc:SetSkinnedAssetAndUpdate(pa, true)
            local entry = {comp = pc, path = pth}
            entry.leader = pcall(function() pc:SetLeaderPoseComponent(m.comp, true, true) end)
            for _, mp in ipairs(myParts) do if mp.path == pth then entry.pose = mp.pose end end
            pav.parts[#pav.parts + 1] = entry
        end)
        if not okP then log("AVATAR poseable: parte erro " .. pth .. ": " .. tostring(eP)) end
    end
    log("AVATAR poseable: partes criadas=" .. #pav.parts .. " de " .. #plist)
    passo("poseable: SetBoneTransformByName (pose idle)")
    local n = applyPose()
    forceVisible()
    pavPoseAt, pavDiagAt, pavDbgUntil = now, now + 2, now + 10
    log("AVATAR poseable: criado mesh=" .. path .. " pose_ossos=" .. n)
    log("AVATAR mesh parcial? so o mesh principal do NPC e usado (cabeca/maos podem ser outros componentes); confirmar por captura de tela")
end
local function poseStep()
    HB.steps = HB.steps + 1
    heartbeat("suavizacao")
    local rm = curRemote()
    if not (pavValid() and rm) then avatarOk = false; return end
    avatarOk = true
    local now = os.clock(); local dt = math.min(math.max(now - pavLastT, 0.001), 0.25); pavLastT = now
    local age = now - rm.recv
    local rvx, rvy = rm.vx, rm.vy
    if age > 0.6 then rvx, rvy = 0, 0 end
    local ex = math.min(age, 0.3)
    local tx, ty, tz, want = rm.x + rvx * ex, rm.y + rvy * ex, rm.z, rm.yaw or pavYaw or 0
    -- avdebug=1: por 10 s apos criar, o avatar fica 200 cm a frente do jogador local
    if FL.avdebug == 1 and localP and now < pavDbgUntil then
        local yr = math.rad(localYaw or 0)
        tx, ty, tz, want = localP.X + math.cos(yr) * 200, localP.Y + math.sin(yr) * 200, localP.Z, (localYaw or 0) + 180
    end
    if not pavPos then pavPos = {X = tx, Y = ty, Z = tz} end
    local dx, dy = tx - pavPos.X, ty - pavPos.Y
    local nx, ny, nz
    if dx * dx + dy * dy > 2000 * 2000 or (FL.avdebug == 1 and now < pavDbgUntil) then nx, ny, nz = tx, ty, tz
    else
        local k = 1 - math.exp(-dt * 12)
        nx, ny, nz = pavPos.X + dx * k, pavPos.Y + dy * k, pavPos.Z + (tz - pavPos.Z) * k
    end
    pavPos = {X = nx, Y = ny, Z = nz}
    if not pavYaw then pavYaw = want end
    pavYaw = pavYaw + angDiff(want, pavYaw) * (1 - math.exp(-dt * 10))
    local feet = nz - 90
    if FL.avground == 1 and now - pavGroundAt > 1 then  -- raio vertical a cada 1 s: se o corpo ficaria dentro do chao, apoia no chao
        pavGroundAt = now
        passo("poseable: LineTraceSingle (chao)")
        pavGroundZ = traceGroundQuiet(nx, ny, feet)
    end
    if pavGroundZ and feet < pavGroundZ then feet = pavGroundZ end
    passo("poseable: mover K2_SetActorLocationAndRotation")
    local ok, e = pcall(function() pav.actor:K2_SetActorLocationAndRotation({X = nx, Y = ny, Z = feet}, {Pitch = 0, Yaw = pavYaw, Roll = 0}, false, {}, true) end)
    if not ok then hbErr("poseable mover", e) end
    if now - pavPoseAt > 1 then  -- reaplica pose e visibilidade a cada 1 s
        pavPoseAt = now
        applyPose(); forceVisible()
    end
    if pavDiagAt and now >= pavDiagAt then pavDiagAt = nil; pcall(diagPose) end
end
local function startSmooth()
    if smoothOn then return end
    smoothOn = true
    gameLoop(math.floor(1000 / math.max(1, math.min(30, FL.hz))), "suavizacao", function()  -- hz do movimento do avatar (padrao 10); game thread
        if not bridgeOn then smoothOn = false; return true end
        HB.loop = HB.loop + 1
        if S2 and S2.mod and S2.mod.ownsAvatar and S2.mod.ownsAvatar(S2.state) then smoothPending = false; return false end
        local ok, e = pcall(npcMode() and smoothStep or poseStep)
        smoothPending = false
        if not ok then hbErr("smooth erro", e) end
        return false
    end)
end
local function isPlayerPawn(o)
    local n = o:GetFullName()
    if n:find("BP_Stalker2Character", 1, true) then return true end
    local pc = FindFirstOf("PlayerController")
    return pc and pc:IsValid() and pc.Pawn and pc.Pawn:IsValid() and pc.Pawn:GetAddress() == o:GetAddress()
end
local function setupAvatar(o)
    avatar = o; avatarPos = nil; avLastSet = nil; avYaw = nil; animTargets = nil; animRetryAt = 0; stepsSeen = {}; goneLogged = false; godNextAt = 0; godListed = false; revalCnt = 0; avatarOk = true; avatarFar = false
    avatarPath = nil
    pcall(function() avatarPath = (o:GetFullName():gsub("^%S+ ", "")) end)
    passo("setup: avatar adotado")
    PUP.state = nil; PUP.rate = nil; PUP.hidden = false
    if FL.disablemove == 1 and FL.avatarmode ~= "npcpuppet" then
        passo("disablemove: CharacterMovement:DisableMovement")
        pcall(function() o.CharacterMovement:DisableMovement() end)  -- a IA nao consegue mais move-lo; o mod o move por teleporte (destruir o controller derrubou o jogo)
    end
    if FL.ctrlrot == 1 then
        passo("ctrlrot: bUseControllerRotationYaw")
        pcall(function() o.bUseControllerRotationYaw = false end)
    end
    passo("setup: gravar avatar.txt")
    local f = io.open(BR .. "avatar.txt", "w"); if f then f:write(o:GetFullName()); f:close() end
end
local function ensureAvatar(x, y, z)
    if S2 and S2.mod and S2.mod.ownsAvatar and S2.mod.ownsAvatar(S2.state) then return end  -- v29: o modulo recarregavel cuida do corpo do remoto
    if avatarSpawning or (avatar and avatar:IsValid()) then return end
    if os.clock() < avatarRetryAt then return end
    local pawn = FindFirstOf("BP_Stalker2Character_C")
    if not (pawn and pawn:IsValid()) then return end
    -- apos Ctrl+R o avatar anterior continua no mundo: readota em vez de criar outro
    if not adoptTried then
        adoptTried = true
        if FL.readopt == 1 then
            log("AVATAR passo: readopt")
        local f = io.open(BR .. "avatar.txt", "r")
        if f then
            local nm = f:read("*l"); f:close()
            local okA, o = pcall(function() return StaticFindObject(((nm or ""):gsub("^%S+ ", ""))) end)
            if okA and o and o:IsValid() and not isPlayerPawn(o) then setupAvatar(o); log("AVATAR: readotado " .. o:GetFullName()); return end
        end
        end
    end
    local pl = pawn:K2_GetActorLocation()
    if FL.maxdist > 0 and (x - pl.X)^2 + (y - pl.Y)^2 > FL.maxdist^2 then
        if os.clock() - (farLogAt or -100) > 10 then farLogAt = os.clock(); log(string.format("AVATAR: remoto a %.0f m (> %.0f m, fora da bolha); sem avatar por enquanto", math.sqrt((x - pl.X)^2 + (y - pl.Y)^2) / 100, FL.maxdist / 100)) end
        return
    end
    local sx, sy, sz = x, y, z
    -- XSpawnObjBySID so funciona dentro da bolha do jogador local: se o remoto esta longe, cria perto do jogador e o laco o teleporta
    if (x - pl.X)^2 + (y - pl.Y)^2 > 3000^2 then sx, sy, sz = pl.X + 300, pl.Y, pl.Z end
    avatarSpawning = true
    log("AVATAR passo: pawnSet antes do spawn")
    local before = pawnSet()
    log("AVATAR passo: XSpawnObjBySID")
    local mgr = FindFirstOf("CustomConsoleManagerRK")
    local ok, e = pcall(function() mgr:XSpawnObjBySID(FL.avatarmode == "npcpuppet" and "S2Coop_RemotePlayer" or "GeneralNPC_Neutral_CloseCombat", 1, false, sx, sy, sz + 50, 0, 0, 0) end)
    log("AVATAR: spawn do jogador 2 ok=" .. tostring(ok) .. " " .. tostring(e))
    ExecuteInGameThreadWithDelay(1500, function()
        ExecuteInGameThread(function()
            log("AVATAR passo: escolha do pawn novo (FindAllOf Pawn)")
            pcall(function()
                local best, bd
                for _, o in ipairs(FindAllOf("Pawn") or {}) do
                    if o:IsValid() and not before[o:GetFullName()] and not isPlayerPawn(o) then
                        local q = o:K2_GetActorLocation()
                        local d = (q.X - sx)^2 + (q.Y - sy)^2
                        if d < 800^2 and (not bd or d < bd) then best, bd = o, d end
                    end
                end
                if best then setupAvatar(best); log("AVATAR: " .. best:GetFullName())
                else avatarRetryAt = os.clock() + 4; log("AVATAR: nenhum pawn novo perto do ponto de spawn; nova tentativa em 4 s") end
            end)
            avatarSpawning = false
        end)
    end)
end
local function ensureHostProxy(x, y, z, yaw)
    if hostProxy and hostProxy:IsValid() then
        hostProxy:K2_SetActorLocationAndRotation({X = x, Y = y, Z = z - 90}, {Pitch = 0, Yaw = yaw or 0, Roll = 0}, false, {}, true)
        return
    end
    local path = "/Game/_STALKER2/SkeletalMeshes/characters/sta/sta_02/SK_ful_sta_02.SK_ful_sta_02"
    local asset = StaticFindObject(path)
    if not (asset and asset:IsValid()) then pcall(function() asset = LoadAsset(path) end) end
    if not (asset and asset:IsValid()) then log("HOSTPROXY: mesh indisponivel"); return end
    local ok, a = pcall(function()
        local act = UEHelpers.GetWorld():SpawnActor(StaticFindObject("/Script/Engine.SkeletalMeshActor"), {X = x, Y = y, Z = z - 90}, {Pitch = 0, Yaw = yaw or 0, Roll = 0})
        act.SkeletalMeshComponent:SetSkeletalMeshAsset(asset)
        return act
    end)
    if ok and a then hostProxy = a; log("HOSTPROXY: criado") else log("HOSTPROXY: erro " .. tostring(a)) end
end

function S2_isLivePawn(o)
    local now = os.clock()
    local s = S2_live
    if not s or now - s.at > 0.09 then
        s = {at = now}
        for _, x in ipairs(FindAllOf("Pawn") or {}) do s[x:GetAddress()] = true end
        S2_live = s
    end
    return s[o:GetAddress()] == true
end
function S2_coreCall(name, default, ...)
    local m = S2 and S2.mod
    if not (m and m[name]) then return default end
    local ok, r = pcall(m[name], S2.state, ...)
    if ok and r ~= nil then return r end
    return default
end
S2_toggleBridge = function()  -- v11: tambem chamado por autobridge=1 (sem tecla)
    bridgeOn = not bridgeOn
    log("NUM0: ponte " .. tostring(bridgeOn))
    if not bridgeOn then return end
    gameLoop(100, "ponte", function()
        if not bridgeOn then return true end
        do
            do
                local ok, e = pcall(function()
                    HB.bridge = HB.bridge + 1
                    heartbeat("ponte")
                    local pawn = FindFirstOf("BP_Stalker2Character_C")
                    if not (pawn and pawn:IsValid()) then HB.nopawn = HB.nopawn + 1; return end
                    local p = pawn:K2_GetActorLocation()
                    localP = {X = p.X, Y = p.Y, Z = p.Z}
                    -- clientalife=1: desliga o A-Life no CLIENTE (uma vez, 5 s depois de o pawn existir); o A-Life autoritativo e o do host
                    if FL.clientalife == 1 and ROLE == "client" and not clientAlifeDone then
                        clientAlifeAt = clientAlifeAt or (os.clock() + 5)
                        if os.clock() >= clientAlifeAt then
                            clientAlifeDone = true
                            local np = FindAllOf("Pawn")
                            passo("clientalife: antes de XALifeDisable (pawns=" .. tostring(np and #np or 0) .. ")")
                            local okC, eC = pcall(function()
                                local mgr = FindFirstOf("CustomConsoleManagerSS")
                                if not (mgr and mgr:IsValid()) then error("CustomConsoleManagerSS invalido") end
                                mgr:XALifeDisable()
                            end)
                            log("AVATAR clientalife: XALifeDisable ok=" .. tostring(okC) .. " " .. tostring(eC))
                            clientAlifeChk = os.clock() + 45
                        end
                    end
                    if clientAlifeChk and os.clock() >= clientAlifeChk then
                        clientAlifeChk = nil
                        local okP, nP = pcall(function() local a = FindAllOf("Pawn"); return a and #a or 0 end)
                        log("AVATAR clientalife: pawns 45s depois=" .. tostring(okP and nP or ("erro " .. tostring(nP))))
                    end
                    if FL.autowalk == 1 then
                        local okW, eW = pcall(autowalkTick, pawn, p)
                        if not okW then aw.errs = (aw.errs or 0) + 1; if (aw.errs or 0) <= 5 then log("AVATAR autowalk: erro " .. tostring(eW)) end end
                    end
                    tick = tick + 1  -- fixB: out.json nao e mais aberto (truncado) aqui; ver a gravacao mais abaixo
                    local vx, vy, yaw = 0, 0, 0
                    pcall(function() local v = pawn.CharacterMovement.Velocity; vx, vy = v.X, v.Y end)
                    pcall(function() yaw = pawn:K2_GetActorRotation().Yaw end)
                    localYaw = yaw
                    -- scan dos NPCs (so host, scan=1): 1 pawn a cada 200 ms, so com o jogador local parado (<30 cm/s) (World Partition descarrega celulas)
                    if ROLE == "host" and FL.npcdmg ~= 0 and FL.npcdmg ~= npcDmgDone then
                        local okX, eX = pcall(npcDmgTest, pawn, p)
                        if not okX then log("NPCDMG: erro " .. tostring(eX)) end
                    end
                    if FL.shotdetect == 1 then
                        if not shotDet.hooks then pcall(shotDetectInit) end
                        shotDetectPoll(pawn)
                        local okE, eE = pcall(shotEmitReal, pawn, p)
                        if not okE then log("SHOT real erro " .. tostring(eE)) end
                    end
                    if FL.shottest ~= 0 and FL.shottest ~= shotTestDone then
                        local okS, eS = pcall(shotTestFire, pawn, p)
                        if not okS then log("SHOT teste erro " .. tostring(eS)) end
                    end
                    if shotStr ~= "" and os.clock() > shotUntil then shotStr = "" end
                    if FL.scan ~= 1 then npcJson = "[]"
                    elseif ROLE == "host" and ((vx * vx + vy * vy) <= 30 * 30 or FL.scanmove == 1) then  -- v2: todo passo (antes: passo sim, passo nao), com limite de tempo
                        local okN, eN = pcall(function() npcJson = scanNpcs(pawn, p) end)
                        if not okN then hbErr("scan", eN) end
                    end
                    if not npcMode() and FL.avatar == 1 and (vx * vx + vy * vy) < 30 * 30 then
                        local okC, eC = pcall(captureSource, pawn, p)
                        if not okC then hbErr("poseable captura", eC) end
                    end
                    fastTime()
                    do  -- fixB: grava em out.json.tmp e renomeia (como o state.json): o relay nunca le um arquivo truncado ou pela metade
                        local body = string.format('{"v":%d,"sid":"%s","f":%d,"t":%.3f,"x":%.1f,"y":%.1f,"z":%.1f,"vx":%.1f,"vy":%.1f,"yaw":%.1f,"gt":%.1f,"wt":%.1f,"wx":%d,"avmesh":"%s","avparts":"%s","x1":"%s","shots":"%s","need":"%s","apps":%s,"npcs":%s}', P2.VER, MYGEN, tick, os.clock(), p.X, p.Y, p.Z, vx, vy, yaw, S2_gtod or -1, S2_wtod or -1, S2_wx or -1, myMesh or "", myPartsStr, S2_coreCall("snapExtra", "", pawn, p), shotOutStr(), P2.needOut(), P2.appsOut(), npcJson)
                        local tmpO, dstO = BR .. "out.json.tmp", BR .. "out.json"
                        local f = io.open(tmpO, "w")
                        if f then
                            f:write(body); f:close()
                            if not os.rename(tmpO, dstO) then  -- Windows: rename nao sobrescreve
                                os.remove(dstO)
                                if not os.rename(tmpO, dstO) then HB.outfail = (HB.outfail or 0) + 1 end  -- relay com o arquivo aberto: fica o snapshot anterior
                            end
                        else HB.outfail = (HB.outfail or 0) + 1 end
                    end
                    local g = io.open(BR .. "in.json", "r")
                    if not g then HB.noin = HB.noin + 1; return end
                    local t = g:read("*a"); g:close()
                    -- cabecalho (antes de "npcs"): t, x, y, z, vx, vy, yaw; tolera espacos (o relay re-grava com json.dump: `"t": 1.0`)
                    local head = t:sub(1, (t:find('"apps"', 1, true) or t:find('"npcs"', 1, true) or (#t + 1)) - 1)
                    local function num(k) return tonumber(head:match('"' .. k .. '"%s*:%s*([%-%d%.eE%+]+)')) end
                    -- v2: versao do formato. Remoto com outra versao (ou sem "v" = build antigo): o snapshot inteiro e ignorado e o erro aparece no log e em state.json
                    local rv = num("v")
                    if rv ~= P2.VER then
                        S2_protoErr = string.format("remoto v=%s local v=%d", tostring(rv), P2.VER)
                        if os.clock() - (P2.protoLogAt or -100) >= 10 then
                            P2.protoLogAt = os.clock()
                            log("PROTO ERRO: formato de snapshot incompativel (" .. S2_protoErr .. "); snapshot remoto IGNORADO. Ponha o mesmo main.lua e o mesmo relay nos dois PCs.")
                        end
                        return
                    end
                    S2_protoErr = nil
                    local rsid = head:match('"sid"%s*:%s*"(.-)"')
                    if rsid ~= P2.rsid then P2.rsid = rsid; for _, m in pairs(mirrors) do m.s = nil end end  -- o outro lado relancou: os numeros de pose recomecam
                    P2.rneed = {}
                    for h in (head:match('"need"%s*:%s*"(.-)"') or ""):gmatch("[^,]+") do P2.rneed[h] = true end
                    do  -- v2: aparencias/textos deste snapshot -> cache por hash (espelhos e modulos), nos dois papeis
                        local posA, posN, nowA = t:find('"apps"', 1, true), t:find('"npcs"', 1, true) or (#t + 1), os.clock()
                        if posA and posA < posN then
                            for pa, pmesh, pparts, pmats, pstat in t:sub(posA, posN - 1):gmatch('{"a":%s*"(.-)",%s*"mesh":%s*"(.-)",%s*"parts":%s*"(.-)",%s*"mats":%s*"(.-)",%s*"statics":%s*"(.-)"}') do
                                P2.rapps[pa] = {mesh = pmesh, parts = pparts, mats = pmats, statics = pstat, at = nowA}
                            end
                        end
                        for h, ap in pairs(P2.rapps) do if nowA - ap.at > 120 then P2.rapps[h] = nil end end
                    end
                    if S2 then S2.io = S2.io or {}; S2.io.head = head; S2.io.pawn = pawn; S2.io.p = p; S2.io.at = os.clock() end
                    -- synctime=1 (cliente): acerta os dois relogios pelo host quando a diferenca passa de 600 s de jogo (25 s reais); no maximo 1 ajuste por relogio a cada 30 s.
                    -- synctimetest=1: teste em loopback, finge que o remoto esta 3 h adiante.
                    if FL.synctime == 1 and (ROLE == "client" or FL.synctimetest == 1) then fastTime() end
                    if FL.synctime == 1 and (ROLE == "client" or FL.synctimetest == 1) and S2_gtod and S2_wtod then
                        for _, c in ipairs({{k = "gt", mine = S2_gtod, w = false}, {k = "wt", mine = S2_wtod, w = true}}) do
                            local r = num(c.k)
                            if r and r >= 0 then
                                local tod = (r + FL.synctimetest * 3 * 3600) % 86400
                                local diff = math.abs(tod - c.mine); if diff > 43200 then diff = 86400 - diff end
                                S2_syncAt = S2_syncAt or {}
                                if diff > FL.syncthresh and os.clock() > (S2_syncAt[c.k] or 0) then
                                    S2_syncAt[c.k] = os.clock() + FL.syncgap
                                    pcall(S2_setGameTime, math.floor(tod / 3600), math.floor(tod % 3600 / 60), math.floor(tod % 60), string.format("sync %s (diferenca %.0f s)", c.k, diff), c.w)
                                end
                            end
                        end
                    end
                    -- syncweather=1 (cliente): segue o tipo de clima do host com XSwitchToWeather (transicao de 45 s do proprio jogo); 1 troca por minuto no maximo.
                    -- wxtest=N (N<99): teste em loopback, finge que o clima remoto e N.
                    if FL.syncweather == 1 and (ROLE == "client" or FL.wxtest < 99) and S2_wx then
                        local rwx = num("wx")
                        if FL.wxtest < 99 then rwx = FL.wxtest end
                        if rwx and rwx >= 0 and rwx <= 9 and rwx ~= S2_wx and os.clock() > (S2_wxAt or 0) then
                            S2_wxAt = os.clock() + 60
                            local was = S2_wx
                            local okW, eW = pcall(function() FindFirstOf("CustomConsoleManagerDR"):XSwitchToWeather(rwx) end)
                            pcall(readGameTime)
                            log(string.format("WEATHER: XSwitchToWeather(%d) ok=%s %s alvo %s -> %s", rwx, tostring(okW), okW and "" or tostring(eW), tostring(was), tostring(S2_wx)))
                        end
                    end
                    if ROLE == "host" then
                        local sh = head:match('"shots"%s*:%s*"(.-)"')
                        if sh and sh ~= "" then
                            for one in sh:gmatch("[^|]+") do
                                local okS, eS = pcall(shotResolve, one, pawn)
                                if not okS then log("SHOT erro " .. tostring(eS)) end
                            end
                        end
                    end
                    local x, y, z = num("x"), num("y"), num("z")
                    if x and y and z then
                        HB.parsed = HB.parsed + 1
                        -- novo snapshot? (com "t" do remetente; sem ele, quando a posicao muda). Antes da coleira: um erro dela nao pode congelar o avatar.
                        local rt = num("t")
                        local changed = (not remote) or (rt and rt ~= remote.t) or (not rt and (x ~= remote.x or y ~= remote.y or z ~= remote.z))
                        if changed then
                            HB.snaps = HB.snaps + 1
                            local nowc = os.clock()
                            local am = head:match('"avmesh"%s*:%s*"(.-)"')
                            if am and am ~= "" then remoteMesh = am end
                            local ap = head:match('"avparts"%s*:%s*"(.-)"')
                            if am and am ~= "" then
                                local newParts = ap or ""
                                -- v15e: o avatar foi criado antes de as partes (cabeca/maos) chegarem -> recria com elas
                                if newParts ~= "" and newParts ~= (remoteParts or "") and pav and #(pav.parts or {}) == 0 then
                                    pcall(function() if pav.actor:IsValid() then pav.actor:K2_DestroyActor() end end)
                                    pav, pavNextTry = nil, 0
                                    log("AVATAR poseable: partes chegaram depois; recriando o avatar")
                                end
                                remoteParts = newParts
                            end
                            local s = {t = rt, x = x, y = y, z = z, vx = num("vx"), vy = num("vy"), yaw = num("yaw"), recv = nowc}
                            if not s.vx then  -- remetente antigo: deriva a velocidade da diferenca entre snapshots
                                s.vx, s.vy = 0, 0
                                if remote then
                                    local dtt = math.max(nowc - remote.recv, 0.05)
                                    if dtt < 1 then s.vx, s.vy = (x - remote.x) / dtt, (y - remote.y) / dtt end
                                end
                            end
                            s.vy = s.vy or 0
                            remote = s
                        end
                        local d = math.sqrt((x - p.X)^2 + (y - p.Y)^2) / 100
                        local st = d > FL.leashm and "FORA" or (d > FL.leashm - 50 and "AVISO" or "ok")   -- v36: leash distance is a flag (m)
                        if st ~= leash then leash = st; log(string.format("COLEIRA %s d=%.0fm", st, d)) end
                        if st == "FORA" and ROLE == "client" then
                            local okT, eT = pcall(safeTeleport, pawn, x + 300, y, z)
                            if not okT then hbErr("coleira", eT) end
                        end
                    end
                    if ((ROLE == "client" and FL.mirrors == 1) or MIRROR_TEST or FL.mirrortest == 1) and t ~= lastIn then
                        lastIn = t
                        local okM, eM = pcall(applyMirrors, t, localP)
                        if not okM and not scanErrLogged then scanErrLogged = true; log("MIRROR erro: " .. tostring(eM)) end
                    end
                    if ((x and y and z) or (FL.avdebug == 1 and not npcMode())) and FL.avatar == 1 then
                        local okA, eA
                        if npcMode() then okA, eA = pcall(ensureAvatar, x, y, z) else okA, eA = pcall(ensurePoseAvatar) end
                        if not okA then avatarSpawning = false; hbErr("ensureAvatar", eA) end
                        startSmooth()
                    end
                end)
                if not ok then hbErr("ponte", e) end
                if FL.pslot == 1 then local okK, eK = pcall(P2.psKick); if not okK then hbErr("pslot", eK) end end   -- b143perf
            end
        end
        return false
    end)
end
S2_RegisterKeyBindRaw(Key.NUM_ZERO, S2_toggleBridge)  -- fixC: tecla de producao
-- NUM7: roda o godtest de novo, na hora (teste manual com o mundo ja rodando)
RegisterKeyBind(Key.NUM_SEVEN, function() S2_godtestRearm = true end)

do  -- fixD: teclas de teste NUM1/2/3/5/9; os locais mirror, poseOn, poseSrc, poseComp, poseNames ficam so aqui dentro
-- NUM_ONE: lista as propriedades (nome:tipo) da classe do NPC mais proximo e de seus pais, e o valor das que parecem SID/faccao
RegisterKeyBind(Key.NUM_ONE, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local h = pawn:K2_GetActorLocation()
        local best, bd
        for _, o in ipairs(FindAllOf("Pawn") or {}) do
            if o:IsValid() and o ~= pawn then
                local p = o:K2_GetActorLocation()
                local d = (p.X-h.X)^2 + (p.Y-h.Y)^2
                if d < 100000000 and (not bd or d < bd) then best, bd = o, d end
            end
        end
        if not best then log("NUM1: nenhum NPC perto"); return end
        log("NUM1: alvo " .. best:GetFullName())
        local okm, info = pcall(function()
            local m = best.Mesh
            return "mesh=" .. tostring(m:GetSkeletalMeshAsset():GetFullName()) .. " anim=" .. tostring(m.AnimClass:GetFullName())
        end)
        log("NUM1 " .. tostring(okm) .. " " .. tostring(info))
        local c = best:GetClass()
        while c and c:IsValid() do
            local names = {}
            c:ForEachProperty(function(prop)
                local n = prop:GetFName():ToString()
                names[#names + 1] = n
                if n:find("SID") or n:find("Faction") or n:find("Proto") or n:find("Archetype") then
                    local ok, v = pcall(function() return best[n] end)
                    names[#names] = n .. "=" .. tostring(ok and v or "?")
                end
            end)
            log("NUM1 classe " .. c:GetFullName() .. " props(" .. #names .. "): " .. table.concat(names, ", "):sub(1, 1500))
            c = c:GetSuperStruct()
        end
    end)
end)

-- NUM_TWO: cria um espelho (SkeletalMeshActor) com o mesh do NPC mais proximo, 4 m a frente do jogador
local mirror
RegisterKeyBind(Key.NUM_TWO, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local h = pawn:K2_GetActorLocation()
        local best, bd
        for _, o in ipairs(FindAllOf("Pawn") or {}) do
            if o:IsValid() and o ~= pawn then
                local p = o:K2_GetActorLocation()
                local d = (p.X-h.X)^2 + (p.Y-h.Y)^2
                if d < 100000000 and (not bd or d < bd) then best, bd = o, d end
            end
        end
        if not best then log("NUM2: nenhum NPC perto"); return end
        local ok, e = pcall(function()
            local asset = best.Mesh:GetSkeletalMeshAsset()
            local cls = StaticFindObject("/Script/Engine.SkeletalMeshActor")
            local a = UEHelpers.GetWorld():SpawnActor(cls, {X = h.X + 400, Y = h.Y, Z = h.Z - 80}, {Pitch = 0, Yaw = 0, Roll = 0})
            a.SkeletalMeshComponent:SetSkeletalMeshAsset(asset)
            local okA, eA = pcall(function() a.SkeletalMeshComponent:SetAnimInstanceClass(best.Mesh.AnimClass) end)
            log("NUM2 anim ok=" .. tostring(okA) .. " " .. tostring(eA))
            mirror = a
            return a:GetFullName()
        end)
        log("NUM2: espelho ok=" .. tostring(ok) .. " " .. tostring(e))
    end)
end)

-- NUM_THREE: empurra o NPC mais proximo 3 m para leste (teste do movimento do espelho)
RegisterKeyBind(Key.NUM_THREE, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local h = pawn:K2_GetActorLocation()
        local best, bd
        for _, o in ipairs(FindAllOf("Pawn") or {}) do
            if o:IsValid() and o ~= pawn and (SCAN_AVATAR or o ~= avatar) then
                local p = o:K2_GetActorLocation()
                local d = (p.X-h.X)^2 + (p.Y-h.Y)^2
                if d < 36000000 and (not bd or d < bd) then best, bd = o, d end
            end
        end
        if not best then log("NUM3: nenhum NPC"); return end
        local p = best:K2_GetActorLocation()
        best:K2_SetActorLocation({X = p.X + 300, Y = p.Y, Z = p.Z + 20}, false, {}, true)
        log("NUM3: NPC " .. best:GetFullName():match("[^%.]+$") .. " movido para X=" .. string.format("%.0f", p.X + 300))
    end)
end)

-- NUM_FIVE: espelho posavel (PoseableMeshComponent) 4 m a frente que copia, a cada 100 ms, a pose dos ossos do NPC mais proximo.
-- Prova local da replicacao de animacao por osso (o host leria os ossos e mandaria pela rede).
local poseOn, poseSrc, poseComp, poseNames = false, nil, nil, {}
RegisterKeyBind(Key.NUM_FIVE, function()
    ExecuteInGameThread(function()
        if poseOn then poseOn = false; log("NUM5: pose desligada"); return end
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        local h = pawn:K2_GetActorLocation()
        local best, bd
        for _, o in ipairs(FindAllOf("Pawn") or {}) do
            if o:IsValid() and o ~= pawn then
                local p = o:K2_GetActorLocation()
                local d = (p.X-h.X)^2 + (p.Y-h.Y)^2
                if d < 100000000 and (not bd or d < bd) then best, bd = o, d end
            end
        end
        if not best then log("NUM5: nenhum NPC"); return end
        local ok, e = pcall(function()
            local a = UEHelpers.GetWorld():SpawnActor(StaticFindObject("/Script/Engine.Actor"), {X = h.X + 400, Y = h.Y, Z = h.Z - 90}, {Pitch = 0, Yaw = 0, Roll = 0})
            local c = a:AddComponentByClass(StaticFindObject("/Script/Engine.PoseableMeshComponent"), false, {Translation = {X=0,Y=0,Z=0}, Rotation = {X=0,Y=0,Z=0,W=1}, Scale3D = {X=1,Y=1,Z=1}}, false)
            c:SetSkinnedAssetAndUpdate(best.Mesh:GetSkeletalMeshAsset(), true)
            poseComp, poseSrc = c, best
            best:SetActorHiddenInGame(true)
            poseNames = {}
            for i = 0, best.Mesh:GetNumBones() - 1 do poseNames[#poseNames + 1] = best.Mesh:GetBoneName(i) end
            return #poseNames .. " ossos"
        end)
        log("NUM5: ok=" .. tostring(ok) .. " " .. tostring(e))
        if not ok then return end
        poseOn = true
        gameLoop(100, "NUM5", function()
            if not poseOn then return true end
            do
                local okp, ep = pcall(function()
                    for _, n in ipairs(poseNames) do
                        poseComp:SetBoneTransformByName(n, poseSrc.Mesh:GetSocketTransform(n, 2), 1)
                    end
                end)
                if not okp then log("NUM5 loop: " .. tostring(ep)); poseOn = false end
            end
            return false
        end)
    end)
end)

-- NUM_NINE: resgate; volta o pawn para o ponto de teste (692626, 551624, 600) e restaura o modo andando
RegisterKeyBind(Key.NUM_NINE, function()
    ExecuteInGameThread(function()
        local pawn = FindFirstOf("BP_Stalker2Character_C")
        if not (pawn and pawn:IsValid()) then log("NUM9: sem pawn"); return end
        pawn:K2_SetActorLocation({X = 692626, Y = 551624, Z = 600}, false, {}, true)
        pcall(function() pawn.CharacterMovement:SetMovementMode(1, 0) end)
        log("NUM9: pawn resgatado")
    end)
end)
end  -- fixD: fim do bloco das teclas de teste

-- ===== S2 LIVE RELOAD BOOTSTRAP (offline agent, 2026-10-10; tested only offline with stubs, NOT in game) =====
-- Appended at the END of main.lua, inside one do-block (adds 0 top-level locals; main.lua has ~168 of Lua's 200).
-- Idea: main.lua stays the part that is loaded once per game launch. New/iterated logic lives in
-- Scripts\s2core.lua, a file that returns a module table M and can be re-read without relaunching:
--     bash /d/modding/luacmd.sh 'return S2.reload("why")'
--
-- WHAT IS REGISTERED ONCE AND NEVER AGAIN (cannot be undone in this UE4SS build):
--   * two permanent tickers (100 ms and 1000 ms, game thread). They only dispatch to the CURRENT module
--     (S2.mod.tick100 / S2.mod.tick1000), so a reload creates no new loop. Interval changes need a relaunch.
--   * RegisterKeyBind / RegisterHook: one trampoline per key / per UFunction path, created the first time a
--     module asks for it (M.keys / M.hooks). The trampoline looks the handler up at call time, so a reload
--     swaps the behaviour and never registers twice. A handler removed from the module becomes a no-op.
-- WHAT A RELOAD REPLACES: every function of the module (S2.mod), its file-local caches, its flag defaults.
-- WHAT SURVIVES A RELOAD: S2.state (the module must keep there anything that owns something in the world or
--   must not repeat): bridge on/off, spawned actors (mirrors, poseable avatar, TargetPoints), the remote-player
--   NPC ref + path + UID, shot sequence number and the host's seen-shots high-water mark, hook counters,
--   "done once" marks (XALifeDisable, XSetGodMode toggle, autobridge), last time/weather sync times.
--   Drop on reload (rebuild lazily): bone-name lists, mesh/asset caches, appearance cache, anim reflection.
-- OLD CODE STILL IN FLIGHT: loops made with S2.loop() and delays made with S2.delay() capture the generation
--   number and turn into no-ops when S2.gen changes (LoopInGameThreadWithDelay does not stop on `return true`
--   here, so "stopped" always means "body skipped"). Never call LoopInGameThreadWithDelay directly in s2core.
-- NOT RELOADABLE: this block and the rest of main.lua, the ticker intervals, UEHelpers (cached by require).
-- FAILURE MODES: compile error or runtime error while loading -> the old module keeps running, S2.reload
--   returns "ERR ..." (shown in lua_out.txt). Error inside a tick -> counted, logged 5 times, loop goes on.
do
    S2 = rawget(_G, "S2") or {}
    S2.gen     = S2.gen or 0          -- generation of the module that is running (changes on every successful reload)
    S2.state   = S2.state or {}       -- survives reloads
    S2.keys    = S2.keys or {}        -- key name -> true (trampoline registered)
    S2.hooks   = S2.hooks or {}       -- UFunction path -> "ok" | error text
    S2.errs    = S2.errs or {n = 0, last = "-"}
    S2.api     = {log = log, FL = FL, BR = BR, ROLE = ROLE, UEHelpers = UEHelpers, compList = compList, bonesOf = bonesOf,
                  matsOf = matsOf, applyMats = applyMats, angDiff = angDiff,
                  getRemote = function() return remote end,                 -- ultimo snapshot do outro jogador {x,y,z,vx,vy,yaw,t,recv}
                  getLocal = function() return localP, localYaw end,
                  getAvatar = function() return avatar end,
                  mirrorsClear = function() return applyMirrors('{"npcs": []}', nil) end,   -- batch 6: peer silent -> no frozen mirrors
                  lookPut = P2.lookPut, lookGet = P2.lookGet,                -- v2: textos de modulo por hash (ver P2.lookPut)
                  setAvatar = function(o) avatar = o end}                   -- scan/tiros do main.lua ignoram este pawn
    local src  = debug and debug.getinfo and debug.getinfo(1, "S").source or ""
    S2.corePath = os.getenv("S2_CORE") or ((src:match("^@(.*[\\/])") or BR) .. "s2core.lua")

    local function fail(where, e)
        S2.errs.n = S2.errs.n + 1; S2.errs.last = where .. ": " .. tostring(e):sub(1, 200)
        if S2.errs.n <= 5 or S2.errs.n % 100 == 0 then log("S2CORE erro (" .. where .. ") #" .. S2.errs.n .. ": " .. tostring(e)) end
    end
    local function call(where, f, ...)
        if type(f) ~= "function" then return end
        local t0 = os.clock()
        local ok, e = pcall(f, ...)
        profAdd(where, (os.clock() - t0) * 1000)   -- batch 53: core tickers in the profile too
        if not ok then fail(where, e) end
        return ok, e
    end

    -- generation-aware loop / delay for module code
    function S2.loop(ms, name, fn)
        local gen, dead = S2.gen, false
        LoopGT(math.max(100, ms), function()
            if dead or gen ~= S2.gen then return true end      -- stale: no-op forever
            local ok, r = pcall(fn, S2.state)
            if not ok then fail("loop " .. name, r) elseif r == true then dead = true end
            return dead
        end)
    end
    function S2.delay(ms, name, fn)
        local gen = S2.gen
        ExecuteInGameThreadWithDelay(ms, function()
            if gen ~= S2.gen then return end
            call("delay " .. name, fn, S2.state)
        end)
    end

    local function bind(mod)
        for name in pairs(mod.keys or {}) do
            if not S2.keys[name] then
                if Key[name] == nil then fail("key", "Key." .. tostring(name) .. " nao existe")
                else
                    S2.keys[name] = true
                    (rawget(_G, "S2_RegisterKeyBindRaw") or RegisterKeyBind)(Key[name], function()  -- raw: with fix_c the plain name is the debug-key gate
                        ExecuteInGameThread(function()
                            local m = S2.mod
                            call("key " .. name, m and m.keys and m.keys[name], S2.state)
                        end)
                    end)
                end
            end
        end
        for path in pairs(mod.hooks or {}) do
            if not S2.hooks[path] then
                S2_hookReg(path)
                local ok, e = pcall(RegisterHook, path, function(...)
                    if not S2_hookGT(path) then return end   -- b139 hookgt: off the game thread = counted; the ticker runs handler(ST, nil, n)
                    local m = S2.mod
                    call("hook " .. path, m and m.hooks and m.hooks[path], S2.state, ...)
                end)
                S2.hooks[path] = ok and "ok" or tostring(e)
                log("S2CORE RegisterHook " .. path .. " -> " .. S2.hooks[path])
            end
        end
    end

    function S2.reload(why)
        local f, eo = io.open(S2.corePath, "r")
        if not f then return "ERR abrir " .. S2.corePath .. ": " .. tostring(eo) end
        local code = f:read("*a"); f:close()
        local chunk, ec = load(code, "@s2core.lua", "t")
        if not chunk then log("S2CORE reload: erro de compilacao " .. tostring(ec)); return "ERR compilacao: " .. tostring(ec) end
        -- Generation numbers are never reused: the new one is active while the file runs (so S2.loop at file level
        -- belongs to the new module); if the file fails, the old number comes back and the failed one stays dead.
        local prev = S2.gen
        S2.seq = math.max(S2.seq or 0, prev) + 1
        S2.gen = S2.seq
        local okR, mod = pcall(chunk)
        if not okR or type(mod) ~= "table" then
            S2.gen = prev
            log("S2CORE reload: erro ao executar " .. tostring(mod)); return "ERR execucao: " .. tostring(mod)
        end
        local old = S2.mod
        if old then call("onUnload", old.onUnload, S2.state) end   -- old S2.loop/S2.delay are already dead here
        for k, v in pairs(mod.FLAGS or {}) do if FL[k] == nil then FL[k] = v end end   -- new flags get their defaults
        S2.mod = mod
        bind(mod)
        call("onLoad", mod.onLoad, S2.state, S2.api)
        local msg = string.format("OK gen=%d ver=%s motivo=%s bytes=%d", S2.gen, tostring(mod.VERSION), tostring(why or "-"), #code)
        log("S2CORE reload: " .. msg)
        return msg
    end

    if not S2.tickers then                                     -- once per Lua state
        S2.tickers = true
        local n = 0
        LoopGT(100, function()
            S2.ffSeq = (S2.ffSeq or 0) + 1; S2.ffBody = S2.ffSeq   -- b143perf: one body = one frame (core ffc)
            local kp = rawget(_G, "S2_KEYPOLL"); if kp then call("keypoll", kp.poll) end                                   -- b137
            local H = rawget(_G, "S2_HOOKT")                                                                               -- b139 hookgt
            if H and H.mode == false then                     -- once, on the game thread: which thread check works here
                if FL.hookgt == nil then FL.hookgt = 1 end
                local okI, gi = pcall(function() return IsInGameThread() end)
                local okT, ts = pcall(S2_tidStr)
                local okG, gs = pcall(function() return GetGameThreadId():ToString() end)
                H.tid = okT and tostring(ts) or "-"; H.gtid = okG and tostring(gs) or "-"
                H.mode = (okI and gi == true) and 1 or (okT and ts ~= nil) and 2 or 0
                log("S2KEY hookgt: modo=" .. ({[0] = "nenhum", "IsInGameThread", "ThreadId:ToString"})[H.mode] .. " IsInGameThread=" .. tostring(okI and gi)
                    .. " tid=" .. H.tid .. " GetGameThreadId=" .. H.gtid .. " hookgt=" .. tostring(FL.hookgt) .. " antes_calibrar=" .. H.uncal)
            end
            if H and H.pend then
                H.pend = false
                for path, k in pairs(H.q) do
                    if k > 0 then
                        H.q[path] = 0
                        if not H.seen[path] then
                            H.seen[path] = true
                            if H.logged < 5 then H.logged = H.logged + 1; log("S2KEY offthread: hook " .. path .. " fora da game thread n=" .. k .. " (total " .. H.off .. ", hookgt=" .. tostring(FL.hookgt) .. ")") end
                        end
                        if FL.hookgt ~= 0 then
                            local mm = S2.mod
                            local hf = mm and mm.hooks and mm.hooks[path]
                            if hf then call("hook " .. path .. " (adiado)", hf, S2.state, nil, k) end
                        end
                    end
                end
            end
            local m = S2.mod
            if m and m.tick100 then call("tick100", m.tick100, S2.state) end
            S2.ffBody = nil   -- b143perf
            return false
        end)
        LoopGT(1000, function()
            n = n + 1
            local m = S2.mod
            S2.ffSeq = (S2.ffSeq or 0) + 1; S2.ffBody = S2.ffSeq   -- b143perf
            if m and m.tick1000 then call("tick1000", m.tick1000, S2.state, n) end
            S2.ffBody = nil   -- b143perf
            if n % 10 == 0 and FL.prof == 1 then   -- batch 53: stutter study
                local parts, tot = {}, 0
                for name, e in pairs(S2PROF) do if e.n > 0 then parts[#parts + 1] = string.format("%s n=%d med=%.1f max=%.1f >8ms=%d", name, e.n, e.ms / e.n, e.max, e.over8); tot = tot + e.ms end end
                table.sort(parts)
                log(string.format("S2PROF 10 s: total=%.0f ms (%.0f%% de 10 s) | %s", tot, tot / 100, table.concat(parts, " | ")))
                for _, e in pairs(S2PROF) do e.n, e.ms, e.max, e.over8 = 0, 0, 0, 0 end
            end
            return false
        end)
        local first = S2.reload("boot")                         -- a missing s2core.lua is fine: nothing runs
        log("S2CORE boot: " .. tostring(first) .. " arquivo=" .. S2.corePath)
    end
end
