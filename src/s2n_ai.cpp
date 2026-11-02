#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <Windows.h>
#include "s2n_peframe.h"
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <Unreal/FMemory.hpp>

using namespace RC;

namespace
{
    struct FP { uint32_t off; uint16_t b[12]; uint32_t n; };
    constexpr uint16_t W = 0x100;
    const FP kFp[] = {
        {0x000, {0x41,0x57,0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xEC,0x60,0x03}, 12},
        {0x00e, {0x0F,0x29,0xB4,0x24,0x50,0x03,0x00,0x00,0x48,0x8B,0x05}, 11},
        {0x01d, {0x48,0x31,0xE0,0x48,0x89,0x84,0x24,0x48,0x03,0x00,0x00}, 11},
        {0x028, {0x83,0xFA,0xEF,0x76}, 4},
        {0x056, {0xE8}, 1},
        {0x0b7, {0x44,0x88,0xBF,0x60,0x01,0x00,0x00}, 7},
        {0x0c6, {0xC6,0x83,0x08,0xFF,0xFF,0xFF,0x18}, 7},
        {0x103, {0xE8}, 1},
        {0x131, {0x48,0xB8,0x00,0x00,0x16,0x44,0x00,0x00,0x7A,0x44}, 10},
        {0x142, {0xE8}, 1},
        {0x155, {0xE8}, 1},
        {0x15a, {0xC6,0x87,0x60,0x01,0x00,0x00,0x0B}, 7},
        {0x164, {0xE8}, 1},
        {0x169, {0x49,0x69,0xC6,0x00,0x07,0x00,0x00}, 7},
        {0x170, {0x48,0x8B,0x8C,0x06,0xA8,0x00,0x00,0x00}, 8},
        {0x186, {0xE8}, 1},
        {0x198, {0xE8}, 1},
        {0x1a2, {0xE8}, 1},
        {0x1af, {0x0F,0xB6,0x81,0x60,0x01,0x00,0x00,0x48,0x8D,0x15}, 10},
        {0x1bd, {0xFF,0x14,0xC2}, 3},
    };
    constexpr uint32_t kImplRva = 0x6bbadfa;
    constexpr uint32_t kFeatOff = 0x2a0;

    constexpr size_t kCompMembers = 0x80;
    constexpr size_t kCompGoal = 0x290;
    constexpr size_t kGMembers = 0x88;
    constexpr size_t kGType = 0x220, kGHas = 0x228;
    constexpr size_t kMemberAI = 0x660;

    using FnStorage = void* (*)();
    using FnV1 = void (*)(void*);
    using FnMove = void* (*)(void*, void*);
    using FnAddGoal = void* (*)(void*, void*, void*, bool);

    struct Natives
    {
        bool tried = false, ok = false;
        std::string why;
        uint8_t* impl = nullptr;
        FnStorage storage = nullptr;
        FnV1 killDtor = nullptr, moveDtor = nullptr, freeMem = nullptr, outDtor = nullptr;
        FnMove moveCtor = nullptr;
        FnAddGoal addGoal = nullptr;
        void** dtorTable = nullptr;
        uint8_t** featGlobal = nullptr;
    } N;

    uint8_t g_lastOut[0x58] = {};
    bool g_hasLastOut = false;
    uint32_t g_lastUid = 0;

    auto fp_match(const uint8_t* p) -> bool
    {
        __try
        {
            for (const auto& f : kFp)
                for (uint32_t i = 0; i < f.n; ++i)
                    if (f.b[i] != W && p[f.off + i] != (uint8_t)f.b[i]) return false;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    auto rel_call(uint8_t* p, uint32_t off) -> uint8_t* { return p + off + 5 + *reinterpret_cast<int32_t*>(p + off + 1); }

    auto text_range(uint8_t*& lo, size_t& n) -> bool
    {
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        auto* s = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
            if (std::memcmp(s->Name, ".text", 5) == 0) { lo = base + s->VirtualAddress; n = s->Misc.VirtualSize; return true; }
        return false;
    }

    auto feat_match(uint8_t* p, uint8_t**& global) -> bool
    {
        __try
        {
            if (!(p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x0D && p[7] == 0xE9 && p[12] == 0x48 && p[13] == 0x8B && p[14] == 0x0D && p[19] == 0xE9))
                return false;
            auto* g1 = p + 7 + *reinterpret_cast<int32_t*>(p + 3);
            auto* g2 = p + 19 + *reinterpret_cast<int32_t*>(p + 15);
            if (g1 != g2) return false;
            const uint8_t* orf = rel_call(p, 7);
            const uint8_t* andf = rel_call(p, 19);
            static const uint8_t kOr[] = {0x09, 0x51, 0x78, 0xC3};
            static const uint8_t kAnd[] = {0xF7, 0xD2, 0x21, 0x51, 0x78, 0xC3};
            if (std::memcmp(orf, kOr, sizeof kOr) != 0 || std::memcmp(andf, kAnd, sizeof kAnd) != 0) return false;
            global = reinterpret_cast<uint8_t**>(g1);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    auto resolve_feat(uint8_t* impl) -> const char*
    {
        uint8_t** g = nullptr;
        if (feat_match(impl + kFeatOff, g)) { N.featGlobal = g; return "rva"; }
        uint8_t* lo; size_t n;
        if (text_range(lo, n))
            for (size_t i = 0; i + 24 < n; ++i)
                if (lo[i] == 0x48 && lo[i + 1] == 0x8B && lo[i + 2] == 0x0D && lo[i + 7] == 0xE9 && lo[i + 12] == 0x48 && feat_match(lo + i, g))
                { N.featGlobal = g; return "aob"; }
        return "nao achado";
    }

    auto same_dtor(void* a, void* b) -> bool
    {
        if (a == b) return true;
        static const uint8_t kHead[] = {0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0x80, 0xB9, 0x48, 0x01, 0x00, 0x00, 0x00};
        __try { return std::memcmp(a, kHead, sizeof kHead) == 0 && std::memcmp(b, kHead, sizeof kHead) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    auto resolve() -> bool
    {
        if (N.tried) return N.ok;
        N.tried = true;
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        uint8_t* p = base + kImplRva;
        std::string via = "rva";
        if (!fp_match(p))
        {
            p = nullptr; via = "aob";
            uint8_t* lo; size_t n;
            if (text_range(lo, n))
                for (size_t i = 0; i + 0x1c0 < n; ++i)
                    if (lo[i] == 0x41 && lo[i + 1] == 0x57 && lo[i + 0x28] == 0x83 && lo[i + 0x29] == 0xFA && fp_match(lo + i)) { p = lo + i; break; }
        }
        if (!p) { N.why = "XAddMoveTo impl nao achado (rva+aob)"; return false; }
        N.impl = p;
        N.storage = reinterpret_cast<FnStorage>(rel_call(p, 0x056));
        N.killDtor = reinterpret_cast<FnV1>(rel_call(p, 0x142));
        N.moveCtor = reinterpret_cast<FnMove>(rel_call(p, 0x155));
        N.moveDtor = reinterpret_cast<FnV1>(rel_call(p, 0x164));
        N.addGoal = reinterpret_cast<FnAddGoal>(rel_call(p, 0x186));
        N.freeMem = reinterpret_cast<FnV1>(rel_call(p, 0x198));
        N.outDtor = reinterpret_cast<FnV1>(rel_call(p, 0x1a2));
        N.dtorTable = reinterpret_cast<void**>(p + 0x1b6 + 7 + *reinterpret_cast<int32_t*>(p + 0x1b6 + 3));
        if (!same_dtor(N.dtorTable[11], reinterpret_cast<void*>(N.moveDtor)) || N.dtorTable[0] != reinterpret_cast<void*>(N.killDtor))
        { N.why = "tabela de destrutores nao confere"; return false; }
        const char* fv = resolve_feat(p);
        char b[220];
        snprintf(b, sizeof b, "via=%s impl=+%#llx addGoal=+%#llx moveCtor=+%#llx feat=%s%s%#llx", via.c_str(), (unsigned long long)(p - base),
                 (unsigned long long)((uint8_t*)N.addGoal - base), (unsigned long long)((uint8_t*)N.moveCtor - base), fv,
                 N.featGlobal ? " g=+" : " g=", (unsigned long long)(N.featGlobal ? (uint8_t*)N.featGlobal - base : 0));
        N.why = b;
        N.ok = true;
        return true;
    }

    auto read_mask_seh(uint32_t& mask) -> int
    {
        if (!N.featGlobal) return 4;
        __try
        {
            uint8_t* m = *N.featGlobal;
            if (!m) return 5;
            mask = *reinterpret_cast<uint32_t*>(m + 0x78);
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 9; }
    }

    constexpr uint32_t kAll = 4194303;
    const char* const kFeatNames[22] = {"Tick", "UpdateDecision", "Threat", "Combat", "UpdateSensors", "Vision", "Hearing", "Hit", "Flair",
        "EmissionThreatAndCombat", "Sanity", "MovementDetection", "ALifeDirector", "LeaveRestrictedArea", "WoundedWeapon", "MotionWarpingNPC",
        "ALifePopulationManager", "SeePlayer", "LuminanceManager", "FlashlightUseManager", "ProcessCorpse", "NeedsIncreasing"};

    auto off_names(uint32_t mask) -> std::string
    {
        std::string s;
        for (int i = 0; i < 22; ++i)
            if (!(mask & (1u << i))) { if (!s.empty()) s += ","; s += kFeatNames[i]; }
        return s.empty() ? "-" : s;
    }

    auto slot_of(uint32_t uid) -> uint8_t*
    {
        auto* chunk = reinterpret_cast<uint8_t*>(N.storage());
        uint32_t i = uid & 0x7ffffff;
        while (i >= 0x400) { chunk = *reinterpret_cast<uint8_t**>(chunk); if (!chunk) return nullptr; i -= 0x400; }
        return chunk + (size_t)i * 0x700;
    }

    struct GoalSnap
    {
        int e = -1;
        uint8_t* slot = nullptr; uint8_t* comp = nullptr;
        int memb = -1, gmemb = -1, has = -1, type = -1;
        int self = 0, withAI = 0;
        void* m0 = nullptr; void* ai0 = nullptr; void* beh0 = nullptr;
        void* slotAI = nullptr; void* slot670 = nullptr;
    };

    auto snap_seh(uint32_t uid, GoalSnap& s) -> int
    {
        __try
        {
            s.slot = slot_of(uid); if (!s.slot) return s.e = 2;
            s.comp = *reinterpret_cast<uint8_t**>(s.slot + 0xa8); if (!s.comp) return s.e = 3;
            s.slotAI = *reinterpret_cast<void**>(s.slot + kMemberAI);
            s.slot670 = *reinterpret_cast<void**>(s.slot + 0x670);
            s.memb = *reinterpret_cast<int32_t*>(s.comp + kCompMembers + 8);
            auto* data = *reinterpret_cast<uint8_t**>(s.comp + kCompMembers);
            for (int i = 0; data && i < s.memb && i < 16; ++i)
            {
                auto* m = *reinterpret_cast<uint8_t**>(data + (size_t)i * 0x18 + 8);
                if (!m) continue;
                if (m == s.slot) ++s.self;
                auto* ai = *reinterpret_cast<uint8_t**>(m + kMemberAI);
                void* beh = ai ? *reinterpret_cast<void**>(ai + 0x38) : nullptr;
                if (ai && beh) ++s.withAI;
                if (i == 0) { s.m0 = m; s.ai0 = ai; s.beh0 = beh; }
            }
            uint8_t* G = s.comp + kCompGoal;
            s.gmemb = *reinterpret_cast<int32_t*>(G + kGMembers + 8);
            s.has = G[kGHas];
            s.type = G[kGType];
            return s.e = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return s.e = 9; }
    }

    auto snap_text(const GoalSnap& s) -> std::string
    {
        char b[360];
        snprintf(b, sizeof b, "e=%d memb=%d proprio=%d com_ia=%d goal_has=%d goal_tipo=%d goal_memb=%d slot_ia=%s slot670=%s m0=%p ia0=%p beh0=%p",
                 s.e, s.memb, s.self, s.withAI, s.has, s.type, s.gmemb, s.slotAI ? "sim" : "NULL", s.slot670 ? "sim" : "NULL", s.m0, s.ai0, s.beh0);
        return b;
    }

    struct Order { uint32_t uid; double x, y, z; uint8_t gait, prio, flags, wstate; float succ; bool hasRot = false; double yaw = 0; };
    constexpr uint8_t kToPlayer = 32;

    auto push_moveto_seh(const Order& o, void* vec) -> int
    {
        uintptr_t pf0 = s2n_pf::save();
        __try
        {
            uint8_t* slot = slot_of(o.uid);
            if (!slot) return 2;
            void* comp = *reinterpret_cast<void**>(slot + 0xa8);
            if (!comp) return 3;
            const bool toPlayer = (o.flags & kToPlayer) != 0;
            alignas(16) uint8_t P[0x160]; alignas(16) uint8_t V[0x170]; alignas(16) uint8_t gout[0x60];
            std::memset(P, 0, sizeof P); std::memset(V, 0, sizeof V); std::memset(gout, 0, sizeof gout);
            V[0x10] = 1; V[0x14] = 1; *reinterpret_cast<int32_t*>(V + 0x38) = -1; V[0x160] = 0;
            P[0x00] = o.prio; P[0x08] = 1; P[0x0d] = 1; P[0x0f] = 1; P[0x14] = o.wstate;
            *reinterpret_cast<float*>(P + 0xf8) = toPlayer ? 1.0f : (o.succ < 1.0f ? 1.0f : o.succ);
            if (toPlayer) std::memset(P + 0x104, 0xAA, 16);
            else std::memset(P + 0x104, 0, 16);
            P[0x150] = o.gait;
            if (o.hasRot) { auto* q = reinterpret_cast<double*>(P + 0x130); q[0] = 0; q[1] = o.yaw; q[2] = 0; P[0x148] = 1; }
            *reinterpret_cast<float*>(P + 0x158) = 600.0f; *reinterpret_cast<float*>(P + 0x15c) = 1000.0f;
            P[0x12] = (o.flags & 1) ? 1 : 0; P[0x13] = (o.flags & 2) ? 1 : 0; P[0x10] = (o.flags & 4) ? 1 : 0;
            P[0x15] = (o.flags & 8) ? 1 : 0; P[0x11] = (o.flags & 16) ? 1 : 0;
            if (!toPlayer)
            {
                *reinterpret_cast<void**>(P + 0x120) = vec;
                *reinterpret_cast<int32_t*>(P + 0x128) = 1; *reinterpret_cast<int32_t*>(P + 0x12c) = 1;
            }
            N.killDtor(V);
            N.moveCtor(V, P);
            V[0x160] = 11;
            N.moveDtor(P);
            N.addGoal(comp, gout, V, true);
            std::memcpy(g_lastOut, gout, sizeof g_lastOut); g_hasLastOut = true; g_lastUid = o.uid;
            *reinterpret_cast<int32_t*>(gout + 0x50) = 0;
            if (void* q = *reinterpret_cast<void**>(gout + 0x48)) N.freeMem(q);
            N.outDtor(gout);
            reinterpret_cast<FnV1>(N.dtorTable[V[0x160]])(V);
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return 9; }
    }

    auto out_text() -> std::string
    {
        if (!g_hasLastOut) return "plan=-";
        auto i32 = [](size_t o) { return *reinterpret_cast<const int32_t*>(g_lastOut + o); };
        char b[160];
        snprintf(b, sizeof b, "plan(uid %u)=[n8=%d n0c=%d n30=%d n34=%d n38=%d n3c=%d n50=%d p0=%s p48=%s]", g_lastUid, i32(0x08), i32(0x0c),
                 i32(0x30), i32(0x34), i32(0x38), i32(0x3c), i32(0x50), *reinterpret_cast<const void* const*>(g_lastOut) ? "sim" : "0",
                 *reinterpret_cast<const void* const*>(g_lastOut + 0x48) ? "sim" : "0");
        return b;
    }

    auto peek_seh(uint32_t uid, uint8_t*& slot, void*& comp) -> int
    {
        __try
        {
            slot = slot_of(uid); if (!slot) return 2;
            comp = *reinterpret_cast<void**>(slot + 0xa8);
            return comp ? 0 : 3;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 9; }
    }

    auto ret2(const LuaMadeSimple::Lua& lua, bool ok, const std::string& s) -> int { lua.set_bool(ok); lua.set_string(s); return 2; }

    auto get_uid(const LuaMadeSimple::Lua& lua, uint32_t& uid) -> bool
    {
        if (!lua.is_number()) return false;
        double d = lua.get_number();
        if (d < 0 || d > 4294967279.0) return false;
        uid = static_cast<uint32_t>(d);
        return true;
    }

    auto mask_text() -> std::string
    {
        uint32_t m = 0;
        int e = read_mask_seh(m);
        char b[48];
        if (e) { snprintf(b, sizeof b, "mask=? (e=%d)", e); return b; }
        snprintf(b, sizeof b, "mask=%#x", m);
        return b;
    }

    auto lua_status(const LuaMadeSimple::Lua& lua) -> int { bool ok = resolve(); return ret2(lua, ok, N.why); }

    auto lua_features(const LuaMadeSimple::Lua& lua) -> int
    {
        if (!resolve()) { lua.set_bool(false); lua.set_integer(-1); lua.set_string("natives nao resolvidos: " + N.why); return 3; }
        uint32_t m = 0;
        int e = read_mask_seh(m);
        if (e) { lua.set_bool(false); lua.set_integer(-1); lua.set_string("mascara ilegivel e=" + std::to_string(e)); return 3; }
        char b[64];
        snprintf(b, sizeof b, "mask=%#x (%u) All=%s desligados=", m, m, (m & kAll) == kAll ? "sim" : "nao");
        lua.set_bool(true);
        lua.set_integer(static_cast<int64_t>(m));
        lua.set_string(std::string(b) + off_names(m));
        return 3;
    }

    auto lua_goal(const LuaMadeSimple::Lua& lua) -> int
    {
        uint32_t uid;
        if (!get_uid(lua, uid)) return ret2(lua, false, "uid invalido");
        if (!resolve()) return ret2(lua, false, "natives nao resolvidos: " + N.why);
        GoalSnap s;
        snap_seh(uid, s);
        return ret2(lua, s.e == 0, snap_text(s) + " " + mask_text() + " " + out_text());
    }

    auto lua_peek(const LuaMadeSimple::Lua& lua) -> int
    {
        uint32_t uid;
        if (!get_uid(lua, uid)) return ret2(lua, false, "uid invalido");
        if (!resolve()) return ret2(lua, false, "natives nao resolvidos: " + N.why);
        uint8_t* slot = nullptr; void* comp = nullptr;
        int e = peek_seh(uid, slot, comp);
        char b[128];
        snprintf(b, sizeof b, "e=%d slot=%p goalcomp=%p", e, (void*)slot, comp);
        return ret2(lua, e == 0, b);
    }

    auto lua_moveto(const LuaMadeSimple::Lua& lua) -> int
    {
        Order o{};
        if (!get_uid(lua, o.uid)) return ret2(lua, false, "uid invalido");
        if (!lua.is_number()) return ret2(lua, false, "x"); o.x = lua.get_number();
        if (!lua.is_number()) return ret2(lua, false, "y"); o.y = lua.get_number();
        if (!lua.is_number()) return ret2(lua, false, "z"); o.z = lua.get_number();
        auto opt = [&](double def) { return lua.is_number() ? lua.get_number() : def; };
        o.gait = static_cast<uint8_t>(opt(3));
        o.succ = static_cast<float>(opt(50));
        o.prio = static_cast<uint8_t>(opt(24));
        o.flags = static_cast<uint8_t>(opt(0));
        o.wstate = static_cast<uint8_t>(opt(1));
        if (o.gait < 1 || o.gait > 5) return ret2(lua, false, "gait fora de 1..5");
        if (o.prio > 26) return ret2(lua, false, "prio fora de 0..26");
        if (o.wstate > 2) return ret2(lua, false, "wstate fora de 0..2");
        if (o.flags > 63) return ret2(lua, false, "flags fora de 0..63");
        if (!resolve()) return ret2(lua, false, "natives nao resolvidos: " + N.why);
        const bool toPlayer = (o.flags & kToPlayer) != 0;
        double* vec = nullptr;
        if (!toPlayer)
        {
            vec = static_cast<double*>((*RC::Unreal::GMalloc)->Malloc(3 * sizeof(double), 16));
            if (!vec) return ret2(lua, false, "malloc");
            vec[0] = o.x; vec[1] = o.y; vec[2] = o.z;
        }
        int e = push_moveto_seh(o, vec);
        if (vec && (e == 2 || e == 3)) (*RC::Unreal::GMalloc)->Free(vec);
        GoalSnap s;
        if (e == 0) snap_seh(o.uid, s);
        char b[200];
        if (toPlayer)
            snprintf(b, sizeof b, "e=%d uid=%u alvo=JOGADOR (XAddMoveTo exato) gait=%u prio=%u flags=%u ws=%u", e, o.uid, o.gait, o.prio, o.flags, o.wstate);
        else
            snprintf(b, sizeof b, "e=%d uid=%u alvo=%.0f,%.0f,%.0f gait=%u succ=%.0f prio=%u flags=%u ws=%u", e, o.uid, o.x, o.y, o.z,
                     o.gait, o.succ, o.prio, o.flags, o.wstate);
        std::string t = b;
        if (e == 0) t += " | " + snap_text(s) + " " + mask_text() + " " + out_text();
        return ret2(lua, e == 0, t);
    }
}

int s2n_moveto_native(uint32_t uid, double x, double y, double z, int gait, float succ, int prio, int flags, int wstate)
{
    Order o{};
    o.uid = uid; o.x = x; o.y = y; o.z = z; o.gait = static_cast<uint8_t>(gait); o.succ = succ;
    o.prio = static_cast<uint8_t>(prio); o.flags = static_cast<uint8_t>(flags & 31); o.wstate = static_cast<uint8_t>(wstate);
    if (!uid || o.gait < 1 || o.gait > 5 || o.prio > 26 || o.wstate > 2) return 5;
    if (!resolve()) return 1;
    double* vec = static_cast<double*>((*RC::Unreal::GMalloc)->Malloc(3 * sizeof(double), 16));
    if (!vec) return 4;
    vec[0] = x; vec[1] = y; vec[2] = z;
    int e = push_moveto_seh(o, vec);
    if (e == 2 || e == 3) (*RC::Unreal::GMalloc)->Free(vec);
    return e;
}
int s2n_moveto_native_rot(uint32_t uid, double x, double y, double z, int gait, float succ, int prio, int flags, int wstate, double yaw)
{
    Order o{};
    o.uid = uid; o.x = x; o.y = y; o.z = z; o.gait = static_cast<uint8_t>(gait); o.succ = succ;
    o.prio = static_cast<uint8_t>(prio); o.flags = static_cast<uint8_t>(flags & 31); o.wstate = static_cast<uint8_t>(wstate);
    o.hasRot = true; o.yaw = yaw;
    if (!uid || o.gait < 1 || o.gait > 5 || o.prio > 26 || o.wstate > 2) return 5;
    if (!resolve()) return 1;
    double* vec = static_cast<double*>((*RC::Unreal::GMalloc)->Malloc(3 * sizeof(double), 16));
    if (!vec) return 4;
    vec[0] = x; vec[1] = y; vec[2] = z;
    int e = push_moveto_seh(o, vec);
    if (e == 2 || e == 3) (*RC::Unreal::GMalloc)->Free(vec);
    return e;
}
int s2n_goal_snap(uint32_t uid, char* out, int n)
{
    if (!out || n <= 0) return 5;
    out[0] = 0;
    if (!uid) return 5;
    if (!resolve()) return 1;
    GoalSnap s;
    int e = snap_seh(uid, s);
    snprintf(out, (size_t)n, "goal_has=%d goal_tipo=%d goal_memb=%d e=%d", s.has, s.type, s.gmemb, e);
    return e;
}

void s2n_ai_register(RC::LuaMadeSimple::Lua& l)
{
    l.register_function("s2n_ai_status", lua_status);
    l.register_function("s2n_ai_peek", lua_peek);
    l.register_function("s2n_moveto", lua_moveto);
    l.register_function("s2n_ai_features", lua_features);
    l.register_function("s2n_ai_goal", lua_goal);
}
