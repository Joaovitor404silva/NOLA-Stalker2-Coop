#include "s2x.h"
#include "s2m_travel.h"
#include "s2m_damage.h"
#include "s2m_lock.h"
#include "s2m_dlgview.h"
#include "s2m_bodies.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

extern "C" {
unsigned long long s2m_travel_spawn_source(double x, double y, double z);
bool s2m_damage_life_frozen();
bool s2m_session_em_god();
unsigned long long s2m_travel_false_spawn(double, double, double) { return 0; }
bool s2m_travel_false_bool() { return false; }
}
#pragma comment(linker, "/alternatename:s2m_travel_spawn_source=s2m_travel_false_spawn")
#pragma comment(linker, "/alternatename:s2m_damage_life_frozen=s2m_travel_false_bool")

namespace
{
using x0::Ref;

class XEnv : public s2t::Env
{
  public:
    bool isHost() override { return x0::is_host(); }
    double now() override { return x0::now(); }
    long long unixTime() override { return x0::wall_ms() / 1000; }
    void log(const std::string& line) override { x0::log(line); }
    bool flag(const char* name, double& out) override
    {
        x0::Val v = x0::flag(name);
        if (v.k == x0::Val::nil || v.k == x0::Val::s) return false;
        out = v.num();
        return true;
    }
    bool peerUp() override { return x0::peer().state == x0::Peer::up; }
    bool x1part(const char* tag, std::string& out) override { return x0::rpart(tag, out); }
    int views() override { return x0::views(); }
    bool hub() override { return x0::hub(); }
    bool x1partV(int v, const char* tag, std::string& out) override { return x0::rpart(tag, out, v); }
    bool remoteInV(int v, const char* what, s2t::Remote& out) override
    {
        if (v == 0) return remoteIn(what, out);
        const x0::Snap& s = x0::remote(v);
        if (!s.ok || !s.has_pos) return false;
        if (x0::fi("kzsafe", 0) == 1 && !x0::in_world(s.x, s.y, s.z)) return false;
        fill(s, out);
        return true;
    }
    int viewPid(int v) override { return x0::view_pid(v); }
    int myPid() override { return x0::my_pid(); }

    bool remoteIn(const char* what, s2t::Remote& out) override
    {
        const x0::Snap& s = x0::remote();
        if (!s.ok || !s.has_pos) return false;
        bool safe = x0::fi("kzsafe", 0) == 1;
        if (!safe || x0::in_world(s.x, s.y, s.z)) {
            if (kzOut_) {
                kzOut_ = false;
                x0::logf("NOLA kzsafe: remoto de volta ao mundo (z=%.0f) depois de %d recusas", s.z, kzN_);
                kzN_ = 0;
            }
            fill(s, out);
            return true;
        }
        ++kzN_;
        if (!kzOut_) {
            kzOut_ = true;
            x0::logf("NOLA kzsafe: remoto FORA DO MUNDO (%.0f,%.0f,%.0f; killz=%s) -> nada nosso vai para la (%s); boneco/alvo parados e escondidos",
                     s.x, s.y, s.z, x0::fs("killz", "nil").c_str(), what);
        }
        return false;
    }
    bool inWorld(double x, double y, double z) override { return x0::in_world(x, y, z); }

    Ref pawn() { return x0::first_of(L"BP_Stalker2Character_C"); }
    Ref pcRef()
    {
        Ref pc = x0::first_of(L"PC");
        return pc ? pc : x0::first_of(L"PlayerController");
    }
    Ref moveComp()
    {
        Ref p = pawn(), cm;
        if (!p || !x0::prop<Ref>(p, "CharacterMovement", cm)) return {};
        return cm;
    }

    bool pawnValid() override { return pawn().valid(); }
    bool pawnPos(s2t::Vec3& o) override
    {
        Ref p = pawn();
        x0::Vec3 v;
        if (!p || !x0::call_v(p, L"K2_GetActorLocation", v)) return false;
        o = {v.x, v.y, v.z};
        return true;
    }
    bool pawnYaw(double& o) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"K2_GetActorRotation");
        x0::Rot r;
        if (!c.run() || !c.rr(r)) return false;
        o = r.yaw;
        return true;
    }
    std::uint64_t pawnAddr() override { return pawn().address(); }
    bool pawnSetLocation(double x, double y, double z) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"K2_SetActorLocation");
        c.v(L"NewLocation", {x, y, z}).b(L"bSweep", false).b(L"bTeleport", true);
        return c.run();
    }
    bool pawnTeleportTo(double x, double y, double z, double yaw) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"K2_TeleportTo");
        c.v(L"DestLocation", {x, y, z}).r(L"DestRotation", {0, yaw, 0});
        return c.run() && c.rb();
    }
    bool setMovementMode(int mode) override
    {
        Ref cm = moveComp();
        if (!cm) return false;
        x0::Call c(cm, L"SetMovementMode");
        c.i(L"NewMovementMode", mode).i(L"NewCustomMode", 0);
        return c.run();
    }
    int movementMode() override
    {
        Ref cm = moveComp();
        uint8_t m = 0;
        if (!cm || !x0::prop<uint8_t>(cm, "MovementMode", m)) return -1;
        return m;
    }
    bool disableMovement() override
    {
        Ref cm = moveComp();
        return cm && x0::call0(cm, L"DisableMovement");
    }
    bool stopMovement() override
    {
        Ref cm = moveComp();
        return cm && x0::call0(cm, L"StopMovementImmediately");
    }
    bool isFalling(bool& o) override
    {
        Ref cm = moveComp();
        if (!cm) return false;
        x0::Call c(cm, L"IsFalling");
        if (!c.run()) return false;
        o = c.rb();
        return true;
    }
    bool getHP(double& o) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"GetHP");
        if (!c.run()) return false;
        o = c.rf();
        return true;
    }
    bool forceSetHP(double hp) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"ForceSetHP");
        c.i(L"HP", (int64_t)hp);
        return c.run();
    }
    bool getBleeding(double& o) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"GetBleeding");
        if (!c.run()) return false;
        o = c.rf();
        return true;
    }
    bool forceSetBleeding(double v) override
    {
        Ref p = pawn();
        if (!p) return false;
        x0::Call c(p, L"ForceSetBleeding");
        c.f(L"Bleeding", (float)v);
        return c.run();
    }
    bool enableInput() override { return input(L"EnableInput"); }
    bool disableInput() override { return input(L"DisableInput"); }
    bool input(const wchar_t* fn)
    {
        Ref p = pawn(), pc = x0::first_of(L"PlayerController");
        if (!p || !pc) return false;
        x0::Call c(p, fn);
        c.o(L"PlayerController", pc);
        return c.run();
    }
    std::string unkillableOn() override
    {
        Ref p = pawn();
        Ref med = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
        Ref mgr = x0::first_of(L"CustomConsoleManagerBuild");
        if (!p || !med || !mgr) return {};
        x0::Call g(med, L"GetGUID");
        g.o(L"Actor", p);
        if (!g.run()) return {};
        int64_t uid = g.ri();
        x0::Call c(mgr, L"XSetEntityUnkillableByUID");
        c.i(L"EntityUID", uid).b(L"bGodModeState", true);
        return c.run() ? std::to_string(uid) : std::string();
    }
    bool unkillableOff(const std::string& uid) override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerBuild");
        if (!mgr || uid.empty()) return false;
        x0::Call c(mgr, L"XSetEntityUnkillableByUID");
        c.i(L"EntityUID", atoll(uid.c_str())).b(L"bGodModeState", false);
        return c.run();
    }

    bool pcCinematicLocked() override
    {
        Ref pc = pcRef();
        bool cin = false;
        return pc && x0::prop(pc, "bInCinematicSequence", cin) && cin && x0::call_b(pc, L"IsMoveInputIgnored", false);
    }
    bool pawnSleeping() override
    {
        Ref p = pawn();
        return p && x0::call_b(p, L"IsSleeping", false);
    }
    void dialogRescan() override
    {
        dlg_ = s2m_lock::dialog_on_screen();
    }
    bool dialogValid() override { return dlg_.valid(); }
    bool dialogVisible() override { return dlg_ && s2m_dlgview::shown(x0::call_b(dlg_, L"IsInViewport", false), x0::call_b(dlg_, L"IsVisible", false)); }

    s2t::TraceHit trace(double ax, double ay, double az, double bx, double by, double bz) override
    {
        s2t::TraceHit r;
        Ref ksl = x0::find_path(L"/Script/Engine.Default__KismetSystemLibrary");
        Ref p = pawn();
        Ref w = x0::world();
        if (!ksl || !w) return r;
        std::vector<void*> skip;
        if (p) skip.push_back(p.get());
        std::vector<Ref> bs;
        m01::bodies(bs);
        for (const Ref& b : bs)
            if (b) skip.push_back(b.get());
        struct Arr
        {
            void* d;
            int32_t n, m;
        } ign{skip.empty() ? nullptr : skip.data(), static_cast<int32_t>(skip.size()), static_cast<int32_t>(skip.size())};
        x0::Call c(ksl, L"LineTraceSingle");
        c.o(L"WorldContextObject", w)
            .v(L"Start", {ax, ay, az})
            .v(L"End", {bx, by, bz})
            .i(L"TraceChannel", 0)
            .b(L"bTraceComplex", false)
            .raw(L"ActorsToIgnore", &ign, static_cast<int>(sizeof ign))
            .i(L"DrawDebugType", 0)
            .b(L"bIgnoreSelf", true)
            .f(L"DrawTime", 0.0f);
        if (!c.run()) return r;
        r.ok = true;
        r.hit = c.rb();
        x0::Vec3 ip;
        if (r.hit && c.rv(ip, L"OutHit.ImpactPoint")) {
            r.hasZ = true;
            r.z = ip.z;
        }
        return r;
    }
    s2t::ActorRef spawnStreamingSource(double x, double y, double z) override { return s2m_travel_spawn_source(x, y, z); }
    int streamReady(double x, double y, double z, double radius) override
    {
        Ref w = x0::world();
        Ref sbl = x0::find_path(L"/Script/Engine.Default__SubsystemBlueprintLibrary");
        Ref wpc = x0::find_path(L"/Script/Engine.WorldPartitionSubsystem");
        Ref qs = x0::find_path(L"/Script/Engine.WorldPartitionStreamingQuerySource");
        if (!w || !sbl || !wpc || !qs) return -1;
        int qsz = x0::struct_size(qs);
        if (qsz < 0x38 || qsz > 0x400) return -1;
        x0::Call g(sbl, L"GetWorldSubsystem");
        g.o(L"ContextObject", w).o(L"Class", wpc);
        if (!g.run()) return -1;
        Ref sub = g.ro();
        if (!sub) return -1;
        std::vector<uint8_t> q(static_cast<size_t>(qsz), uint8_t{0});
        const double loc[3] = {x, y, z};
        const float rad = static_cast<float>(radius);
        std::memcpy(q.data(), loc, sizeof loc);
        std::memcpy(q.data() + 0x18, &rad, sizeof rad);
        q[0x31] = 1;
        struct Arr
        {
            void* d;
            int32_t n, m;
        } a{q.data(), 1, 1};
        x0::Call c(sub, L"IsStreamingCompleted");
        c.i(L"QueryState", 2).raw(L"QuerySources", &a, static_cast<int>(sizeof a)).b(L"bExactState", false);
        if (!c.run()) return -1;
        return c.rb() ? 1 : 0;
    }
    void destroyActor(s2t::ActorRef ref) override
    {
        Ref a = Ref::addr(ref);
        if (a) x0::call0(a, L"K2_DestroyActor");
    }
    bool legacySafeTeleport(double x, double y, double z) override { return pawnSetLocation(x, y, z); }

    bool lifeDown() override { return s2m::damage::local_downed(); }
    bool lifeDownFrozen() override { return s2m_damage_life_frozen(); }
    bool emGodHeld() override { return x0::fi("godown", 1) == 1 && (s2m_session_em_god() || s2m_damage_god_held()); }
    bool ndDead() override { return false; }
    bool leadLockMine() override { return s2m_lock::lead_mine(); }
    void dmgProtectUntil(double t) override { s2m::damage::protect_until(t); }

  private:
    static void fill(const x0::Snap& s, s2t::Remote& o)
    {
        o.x = s.x;
        o.y = s.y;
        o.z = s.z;
        o.yaw = s.yaw;
        o.hasYaw = true;
        o.recv = s.recv;
    }
    Ref dlg_;
    bool kzOut_ = false;
    int kzN_ = 0;
};

XEnv g_env;
}

X0_MODULE(travel)
{
    std::string tsv;
    x0::data("s2u_cine.tsv", tsv);
    s2m_travel_init(&g_env, tsv);
    x0::every(x0::Mod::travel, "travelA", x0::Rate::t100, [] { if (x0::port(x0::Mod::travel)) s2m_travel_tick_a(); });
    x0::every(x0::Mod::travel, "travelB", x0::Rate::t100, [] { if (x0::port(x0::Mod::travel)) s2m_travel_tick_b(); });
    x0::own_tag(x0::Mod::travel, "V", [] {
        std::string o = s2m_travel_out_v();
        return o.size() > 3 ? o.substr(3) : std::string();
    });
    x0::own_tag(x0::Mod::travel, "FT", [] {
        std::string o = s2m_travel_out_ft();
        return o.size() > 4 ? o.substr(4) : std::string();
    });
    x0::on_snap(x0::Mod::travel, [](const x0::Snap& s) {
        if (!x0::port(x0::Mod::travel) || !x0::is_client() || !s.ok || !s.has_pos) return;
        s2t::Vec3 p;
        if (!g_env.pawnPos(p)) return;
        s2t::Remote r;
        r.x = s.x;
        r.y = s.y;
        r.z = s.z;
        r.yaw = s.yaw;
        r.hasYaw = true;
        r.recv = s.recv;
        s2m_travel_leash_check(r, p);
    });
    m01::ext().remote_cine = [] { return x0::port(x0::Mod::travel) && s2m_travel_remote_cine(); };
    s2m::damage::links().host_cine = [] { return x0::port(x0::Mod::travel) && s2m_travel_cine_on(); };
    s2m::damage::links().cine_frozen = [] { return x0::fi("godown", 1) == 1 && x0::port(x0::Mod::travel) && s2m_travel_frozen(); };
    s2m::damage::links().god_other = [] { return x0::fi("godown", 1) == 1 && ((x0::port(x0::Mod::travel) && s2m_travel_god_held()) || s2m_session_em_god()); };
}
