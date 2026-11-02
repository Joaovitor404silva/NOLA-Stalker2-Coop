local OUT = {}
local function out(s) OUT[#OUT + 1] = s end
NOW = 0
local X1 = {}
local REMOTE = nil
local W = {}
local ME, TGT, PUP, PARKED, NEXTADDR = nil, nil, nil, false, 4096
function log(s) out("LOG " .. s) end
os.clock = function() return NOW end
FName = function(s) return s end
local Obj = {}
Obj.__index = Obj
local function mk(id, full, pawn)
    NEXTADDR = NEXTADDR + 16
    local o = setmetatable({id = id, full = full, pawn = pawn, live = true, addr = NEXTADDR, hp = 100.0, mx = 100.0, bleed = 0.0,
        x = 0.0, y = 0.0, z = 0.0, vx = 0.0, vy = 0.0, vz = 0.0, capr = 34.0, caph = 88.0, alive = true, wounded = false, uid = -1}, Obj)
    W[#W + 1] = o
    return o
end
function Obj:IsValid() return self.live end
function Obj:GetAddress() return self.addr end
function Obj:GetFullName() return self.full end
function Obj:GetHP() if not self.pawn then error("sem GetHP") end return self.hp end
function Obj:GetMaxHP() if not self.pawn then error("sem GetMaxHP") end return self.mx end
function Obj:IsAlive() if not self.pawn then error("sem IsAlive") end return self.alive end
function Obj:GetBleeding() if not self.pawn then error("sem GetBleeding") end return self.bleed end
function Obj:IsWounded() return self.wounded end
function Obj:IsFallingWounded() return false end
function Obj:K2_GetActorLocation() return {X = self.x, Y = self.y, Z = self.z} end
function Obj:GetVelocity() return {X = self.vx, Y = self.vy, Z = self.vz} end
function Obj:GetScaledCapsuleRadius() return self.owner.capr end
function Obj:GetScaledCapsuleHalfHeight() return self.owner.caph end
function Obj:ForceSetHP(h)
    local n = h >= 0 and math.floor(h) or math.ceil(h)
    self.hp = n + 0.0
    out(string.format("CALL %s ForceSetHP %d", self.id, n))
end
function Obj:ForceSetBleeding(b)
    self.bleed = b + 0.0
    out(string.format("CALL %s ForceSetBleeding %.2f", self.id, self.bleed))
end
function Obj:DisableMovement() out("CALL " .. self.id .. " DisableMovement") end
function Obj:SetMovementMode(a, b) out(string.format("CALL %s SetMovementMode %d %d", self.id, a, b)) end
function Obj:ReceiveDamageStruct(t)
    out(string.format("CALL %s RDS dmg=%.2f ap=%.2f uid=%s bone=%s type=%d src=%d", self.id, t.Damage, t.ArmorPiercing,
        t.AttackerUID and tostring(t.AttackerUID.UID) or "-", tostring(t.BoneHit), t.DamageType, t.DamageSource))
    self.hp = math.max(0.0, self.hp - t.Damage)
    if self.hp <= 0 then self.alive = false end
end
function Obj:ReceiveDamage(d, ty, ad, ap, b, bc, bone)
    out(string.format("CALL %s RD dmg=%.2f bone=%s", self.id, d, tostring(bone)))
    self.hp = math.max(0.0, self.hp - d)
    if self.hp <= 0 then self.alive = false end
end
function Obj:GetGUID(a)
    if not a or not a.live or a.uid < 0 then error("uid ilegivel") end
    return a.uid
end
function Obj:GetFocusedEnemy(o) return o and o.focus or nil end
local MED = mk("med", "CppMediator /Script/Stalker2.Default__CppMediator", false)
local PCO = mk("pc", "PlayerController /Game:PersistentLevel.pc", false)
function StaticFindObject(p) if p == "/Script/Stalker2.Default__CppMediator" then return MED end return nil end
function FindAllOf(c)
    local r = {}
    for _, o in ipairs(W) do if o.live and c == "Pawn" and o.pawn then r[#r + 1] = o end end
    return r
end
local function x1part(tag) return X1[tag] end
local function valid(o)
    if not o then return false end
    local ok, v = pcall(function() return o:IsValid() end)
    return ok and v
end
local M, S2L = {}, {}
S2L.chShotDeny = function() return false end
S2 = {io = {pawn = nil}}
api = {ROLE = "client", UEHelpers = {GetPlayerController = function() return nil end}}
api.getRemote = function() return REMOTE end
api.getLocal = function() local p = S2.io.pawn; if not valid(p) then return nil end return {X = p.x, Y = p.y, Z = p.z} end
local ST = {din = {seen = {}, n = {ok = 0, cap = 0, dead = 0, prot = 0, down = 0}}, life = {}, dmg = {seq = 0, total = 0, list = {}}, shotSeen = {}, cv = {on = false}}
