#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#include <Windows.h>
#include "s2x_int.h"
#include "s2r_net.h"
#include "s2r_x0.h"
#include <cstring>
#include <optional>
#include <unordered_map>

#include <Unreal/AActor.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/Rotator.hpp>
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UAssetRegistryHelpers.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealCoreStructs.hpp>
#include <Unreal/UnrealInitializer.hpp>
#include <Unreal/World.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace x0::in
{
    namespace
    {
        int g_ueErr = 0;

        UClass* class_at(const wchar_t* path)
        {
            Ref r = find_path(path);
            UObject* o = r.get();
            return o ? static_cast<UClass*>(o) : nullptr;
        }

        UWorld* world_raw()
        {
            Ref w = world();
            return static_cast<UWorld*>(w.get());
        }

        uint64_t spawn_raw(const wchar_t* cls, double x, double y, double z, double pitch, double yaw, double roll)
        {
            if (!on_game_thread()) return 0;
            UWorld* w = world_raw();
            UClass* c = class_at(cls);
            if (!w || !c) return 0;
            FVector loc{};
            loc.SetX(x);
            loc.SetY(y);
            loc.SetZ(z);
            FRotator rot{};
            rot.SetPitch(pitch);
            rot.SetYaw(yaw);
            rot.SetRoll(roll);
            AActor* a = w->SpawnActor(c, &loc, &rot);
            return reinterpret_cast<uint64_t>(a);
        }

        bool g_on = false;
        bool g_abDone = false;
        double g_abAt = -1;
        int64_t g_n = 0;
        int g_stErr = 0;
        std::string g_level;
        bool g_hasLevel = false;
        bool g_keyed = false;

        Ref local_pawn() { return first_of(L"BP_Stalker2Character_C"); }

        void toggle()
        {
            g_on = !g_on;
            log(std::string("NUM0: ponte ") + (g_on ? "true" : "false"));
        }

        void setup()
        {
            if (g_keyed) return;
            g_keyed = true;
            key(Mod::x0, VK_NUMPAD0, [] { toggle(); });
            auto& B = bridge();
            B.mbox.on = [] { return s2r::x0_mailbox(); };
            B.mbox.put = [](std::string s) { s2r::x0_out(std::move(s)); };
            B.mbox.get = [](std::string& s) { return s2r::x0_in(s); };
            B.mbox.bye = [](uint64_t& seen, std::string& sid) { return s2r::x0_take_bye(seen, sid); };
            B.mbox.vget = [](int v, std::string& s) { return s2r::x0_in_view(v, s); };
            B.mbox.vbye = [](int v, uint64_t& seen, std::string& sid) { return s2r::x0_take_bye_view(v, seen, sid); };
            B.mbox.roster = [](Roster& r)
            {
                s2r::Roster n;
                if (!s2r::x0_roster(n)) return false;
                r.on = n.hub;
                r.full = n.full;
                r.you = n.you;
                r.max = n.max;
                r.seq = n.seq;
                r.peers.clear();
                for (auto& e : n.peers) r.peers.push_back({e.pid, e.sid, e.st});
                return true;
            };
        }

        void autobridge(const Ref& pawn)
        {
            if (fi("autobridge", 0) != 1 || g_abDone) return;
            double t = now();
            if (!pawn) { g_abAt = -1; return; }
            if (g_abAt < 0) g_abAt = t + 5;
            if (t < g_abAt) return;
            g_abDone = true;
            if (g_on) { log("AVATAR autobridge: ponte ja ligada"); return; }
            toggle();
            log(std::string("AVATAR autobridge: ponte ligada=") + (g_on ? "true" : "false"));
        }

        void write_state(const Ref& pawn)
        {
            bool inw = pawn.valid();
            std::optional<bool> paused;
            if (!inw) { g_hasLevel = false; g_level.clear(); }
            else
            {
                Ref w = world();
                Ref gs = find_path(L"/Script/Engine.Default__GameplayStatics");
                if (w && gs)
                {
                    {
                        Call c(gs, L"IsGamePaused");
                        c.o(L"WorldContextObject", w);
                        if (c.run()) paused = c.rb();
                    }
                    if (!g_hasLevel)
                    {
                        Call c(gs, L"GetCurrentLevelName");
                        c.o(L"WorldContextObject", w).b(L"bRemovePrefixString", true);
                        if (c.run()) { g_level = w2u(c.rs()); g_hasLevel = true; }
                    }
                }
            }
            std::optional<double> hp, mx;
            std::optional<bool> alive;
            if (inw)
            {
                { Call c(pawn, L"GetHP"); if (c.run()) hp = c.rd(); }
                { Call c(pawn, L"GetMaxHP"); if (c.run()) mx = c.rd(); }
                { Call c(pawn, L"IsAlive"); if (c.run()) alive = c.rb(); }
            }
            std::string body = state_json(inw, hp ? &*hp : nullptr, mx ? &*mx : nullptr, alive ? &*alive : nullptr, g_hasLevel ? &g_level : nullptr, paused ? &*paused : nullptr);
#ifndef S2X_RELEASE
            write_atomic(bridge_file(L"state.json"), body);
#endif
        }
    }

    void io_tick()
    {
        if (!port(Mod::x0)) return;
        setup();
        if (!g_on) return;
        auto& B = bridge();
        static double io_last = -1, io_max = 0, io_stat = -1;
        static int64_t io_n = 0, io_np0 = 0, io_rd0 = 0;
        double io_t = now();
        if (io_last > 0) io_max = std::max(io_max, io_t - io_last);
        io_last = io_t;
        ++io_n;
        if (io_stat < 0) { io_stat = io_t; io_np0 = B.nopawn; io_rd0 = B.reads; }
        else if (io_t - io_stat >= 10)
        {
            logf("NOLA iogap 10 s: io=%lld io_gap_max=%.0f ms sem_pawn=%lld lidos_novos=%lld", (long long)io_n, io_max * 1000, (long long)(B.nopawn - io_np0), (long long)(B.reads - io_rd0));
            io_stat = io_t; io_max = 0; io_n = 0; io_np0 = B.nopawn; io_rd0 = B.reads;
        }
        Ref pawn = local_pawn();
        Bridge::LocalIn L;
        if (pawn)
        {
            Vec3 p;
            if (call_v(pawn, L"K2_GetActorLocation", p))
            {
                L.ok = true;
                L.x = p.x;
                L.y = p.y;
                L.z = p.z;
                Vec3 v;
                if (prop(pawn, "CharacterMovement.Velocity", v)) { L.vx = v.x; L.vy = v.y; }
                Call c(pawn, L"K2_GetActorRotation");
                Rot r;
                if (c.run() && c.rr(r)) L.yaw = r.yaw;
                Call cr(pawn, L"GetControlRotation");
                Rot q;
                if (cr.run() && cr.rr(q)) L.pt = q.pitch > 180 ? q.pitch - 360 : q.pitch;
            }
        }
        if (L.ok) B.run_producers();
        int64_t r0 = B.reads;
        B.io_step(L, now());
        B.dispatch();
        if (B.reads != r0) nat_after_read();
    }

    void state_tick()
    {
        if (!port(Mod::x0)) return;
        setup();
        ++g_n;
        if (coop_lua_seen()) log_every("x0_lua_dup", 60, "NOLA x0 AVISO: cpp_port=1 e o mod Lua NolaAlife tambem carregou: Lua e C++ rodam juntos. Tire NolaAlife de Mods/mods.txt (ou ponha cpp_port=0).");
        Ref pawn = local_pawn();
        autobridge(pawn);
        if (fi("beacon", 0) != 1 || g_n % 5 != 0) return;
        try { write_state(pawn); }
        catch (...) { if (++g_stErr <= 5) log("AVATAR estado: erro exception"); }
    }
}

namespace x0
{
    bool bridge_on() { return in::g_on && port(Mod::x0); }
}

extern "C" uint64_t s2x_load_asset(const wchar_t* path)
{
    using namespace x0::in;
    try
    {
        if (!path || !IsInGameThread()) return 0;
        FName n(path, FNAME_Add);
        auto* reg = static_cast<UAssetRegistry*>(UAssetRegistryHelpers::GetAssetRegistry().ObjectPointer);
        if (!reg) return 0;
        FAssetData ad = reg->GetAssetByObjectPath(n);
        if (!((Version::IsAtMost(5, 0) && ad.ObjectPath().GetComparisonIndex()) || ad.PackageName().GetComparisonIndex())) return 0;
        return reinterpret_cast<uint64_t>(UAssetRegistryHelpers::GetAsset(ad));
    }
    catch (...)
    {
        if (++g_ueErr <= 5) x0::log("NOLA x0 load_asset: exception");
        return 0;
    }
}

extern "C" uint64_t s2x_spawn_actor(const wchar_t* cls, double x, double y, double z, double pitch, double yaw, double roll)
{
    using namespace x0::in;
    try { return spawn_raw(cls, x, y, z, pitch, yaw, roll); }
    catch (...)
    {
        if (++g_ueErr <= 5) x0::log("NOLA x0 spawn_actor: exception");
        return 0;
    }
}

extern "C" bool s2m_trace_line(double ax, double ay, double az, double bx, double by, double bz, bool* hit, double* impactZ)
{
    try
    {
        bool h = false;
        x0::Vec3 imp;
        if (!x0::trace_line({ax, ay, az}, {bx, by, bz}, h, imp)) return false;
        if (hit) *hit = h;
        if (impactZ) *impactZ = imp.z;
        return true;
    }
    catch (...) { return false; }
}

extern "C" unsigned long long s2m_travel_spawn_source(double x, double y, double z)
{
    using namespace x0::in;
    try
    {
        uint64_t a = spawn_raw(L"/Script/Engine.TargetPoint", x, y, z, 0, 0, 0);
        if (!a) return 0;
        x0::Ref ar = x0::Ref::addr(a);
        UObject* cc = x0::find_path(L"/Script/Engine.WorldPartitionStreamingSourceComponent").get();
        if (!ar || !cc) return a;
        x0::Call c(ar, L"AddComponentByClass");
        c.o(L"Class", x0::Ref::of(cc)).b(L"bManualAttachment", false).xf(L"RelativeTransform", {}, {}).b(L"bDeferredFinish", false);
        if (!c.run()) return a;
        x0::Ref comp = c.ro();
        if (comp)
        {
            x0::prop_set<bool>(comp, "bStreamingSourceEnabled", true);
            x0::prop_set<uint8_t>(comp, "TargetState", 1);
        }
        return a;
    }
    catch (...)
    {
        if (++g_ueErr <= 5) x0::log("NOLA x0 spawn_source: exception");
        return 0;
    }
}
