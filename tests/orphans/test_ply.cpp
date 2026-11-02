#include "fake_or.h"
#include "s2m_wshot.h"

#include <cstdio>
#include <string>

using fo::Obj;
using fo::W;

namespace
{
    int g_fail = 0, g_n = 0;
    void ck(bool ok, const char* name)
    {
        ++g_n;
        if (!ok) ++g_fail;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    }
    int count(const std::string& needle)
    {
        int n = 0;
        for (auto& s : W().trace)
            if (s.find(needle) != std::string::npos) ++n;
        return n;
    }
    std::string out_ws() { return W().tags["WS"](); }
    Obj* pawn(const std::string& cls, const std::string& n)
    {
        Obj* p = fo::make(cls, n);
        p->pawn = cls != "BP_Puppet_C";
        return p;
    }
    void reset(const char* role, int nviews, int you)
    {
        s2m_wshot::reset();
        W().role = role;
        W().nviews = nviews;
        W().you = you;
        W().pup.clear();
        W().pupw.clear();
        for (auto& s : W().vx1) s.clear();
        W().x1.clear();
        W().trace.clear();
        W().flags.clear();
        W().flags["wsply"] = x0::Val::I(2);
        W().flags["wsmax"] = x0::Val::I(30);
        W().flags["wsplynmax"] = x0::Val::I(8);
        W().snap.ok = true;
    }
    double g_t = 1000;
    void tick()
    {
        g_t += 0.3;
        fo::advance(g_t);
        W().rows["wshotWs"]();
    }
    void frame(double dt)
    {
        g_t += dt;
        fo::advance(g_t);
        W().rows["wshotTr"]();
    }
    x0::Vec3 tracer_loc()
    {
        x0::Vec3 v{-1, -1, -1};
        for (auto& o : W().objs)
            if (o->cls == "NiagaraComponent" && o->name.rfind("Nc_NS_Projectile", 0) == 0) v = o->loc;
        return v;
    }
    double tracer_x() { return tracer_loc().x; }
    void shot(Obj* o)
    {
        Obj* k = fo::make("ItemAppearanceComponent", o->name + "_iac");
        k->owner = o;
        W().hooks[L"/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart"](fo::ref(k));
    }
    Obj* with_weapon(Obj* o, const std::string& sk)
    {
        o->weapon = fo::make("SkeletalMeshComponent", o->name + "_wpn");
        o->weapon->owner = o;
        o->weapon->skel = fo::make("SkeletalMesh", sk);
        return o;
    }
}

int main()
{
    fo::init_modules();
    fo::make("AkGameplayStatics", "AkGS", true, "/Script/AkAudio.Default__AkGameplayStatics");
    fo::make("NiagaraFunctionLibrary", "Niagara", true, "/Script/Niagara.Default__NiagaraFunctionLibrary");
    fo::make("AkAudioEvent", "SFX_AK74_Oneshot", true, "/Game/_STALKER2/Audio/WwiseAudio/Events/Weapons/AR/AK74/AK74_Fire/SFX_AK74_Oneshot.SFX_AK74_Oneshot");
    fo::make("NiagaraSystem", "NS_MuzzleTP_AssaultR", true, "/Game/_Stalker_2/VFX/Weapons/MuzzleTP/NS_MuzzleTP_AssaultR.NS_MuzzleTP_AssaultR");
    fo::make("NiagaraSystem", "NS_Projectile_NPC_medium", true, "/Game/_Stalker_2/VFX/Weapons/a_Trace/NS_Projectile_NPC_medium.NS_Projectile_NPC_medium");
    Obj* me = with_weapon(pawn("BP_Stalker2Character_C", "Me"), "SK_AK74");
    me->loc = {0, 0, 0};

    reset("client", 1, 1);
    shot(me);
    tick();
    ck(out_ws().find("PLY,s,SK_AK74,1") != std::string::npos, "local shot is sent as PLY with the weapon mesh");

    reset("client", 1, 1);
    W().flags["wsply"] = x0::Val::I(0);
    shot(me);
    tick();
    ck(out_ws().empty(), "wsply=0 sends nothing");

    reset("host", 3, 0);
    W().flags["wshot"] = x0::Val::I(1);
    W().flags["wsply"] = x0::Val::I(0);
    Obj* p0 = pawn("BP_Puppet_C", "PupA");
    Obj* p1 = pawn("BP_Puppet_C", "PupB");
    W().pup[0] = p0;
    W().pup[1] = p1;
    shot(p0);
    shot(p1);
    tick();
    ck(out_ws().empty(), "avatar shots on the host are not re-sent");

    reset("client", 1, 1);
    Obj* hp = pawn("BP_Puppet_C", "HostAvatar");
    W().pup[0] = hp;
    W().x1 = "#WS:1,PLY,s,SK_AK74,1";
    tick();
    ck(count("PostEventAtLocation") == 1 && count("at=HostAvatar") >= 1, "PLY received plays on the host avatar (view 0)");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().flags["wsplynmax"] = x0::Val::I(3);
    W().x1 = "#WS:1,PLY,s,SK_AK74,50";
    tick();
    tick();
    ck(count("PostEventAtLocation") == 3, "wsplynmax caps the burst");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().flags["wsplynmax"] = x0::Val::I(50);
    W().flags["wsmax"] = x0::Val::I(5);
    W().x1 = "#WS:1,PLY,s,SK_AK74,50";
    tick();
    tick();
    ck(count("PostEventAtLocation") == 5, "wsmax caps the plays per second");

    reset("client", 1, 1);
    W().x1 = "#WS:1,PLY,s,SK_AK74,1";
    tick();
    ck(count("PostEventAtLocation") == 0, "null puppet plays nothing and does not throw");

    reset("host", 3, 0);
    Obj* pa = pawn("BP_Puppet_C", "GuestA");
    Obj* pb = pawn("BP_Puppet_C", "GuestB");
    W().pup[0] = pa;
    W().pup[1] = pb;
    W().x1 = "#WS:1,PLY,s,SK_AK74,1";
    W().vx1[1] = "#WS:1,PLY,s,SK_AK74,1";
    tick();
    ck(count("PostEventAtLocation") == 2 && count("at=GuestA") >= 1 && count("at=GuestB") >= 1, "separate sequence counters: both guests with sq 1 play on their own avatar");
    W().trace.clear();
    tick();
    ck(count("PostEventAtLocation") == 0, "a repeated sequence per view is not played again");
    W().vx1[1] = "#WS:2,PLY,s,SK_AK74,1";
    tick();
    ck(count("PostEventAtLocation") == 1 && count("at=GuestB") >= 1 && count("at=GuestA") == 0, "view 1 advances alone");

    reset("host", 3, 0);
    W().pup[0] = pa;
    W().pup[1] = pb;
    W().x1 = "#WS:1,PLY,s,SK_AK74,2";
    tick();
    tick();
    std::string rel = out_ws();
    ck(rel.find("PLY1,s,SK_AK74,2") != std::string::npos, "host relays guest A's shot under PLY1");
    ck(rel.find(",PLY,s,") == std::string::npos && rel.rfind("1,PLY,s,", 0) != 0, "host does not forward the raw PLY");

    reset("host", 1, 0);
    W().pup[0] = pa;
    W().x1 = "#WS:1,PLY,s,SK_AK74,2";
    tick();
    tick();
    ck(count("at=GuestA") >= 1 && out_ws().find("PLY1") == std::string::npos, "one guest only: plays, no relay");

    reset("client", 3, 2);
    Obj* ha = pawn("BP_Puppet_C", "HostOnB");
    Obj* ga = pawn("BP_Puppet_C", "GuestAOnB");
    W().pup[0] = ha;
    W().pup[1] = ga;
    W().x1 = "#WS:1,PLY1,s,SK_AK74,2";
    tick();
    tick();
    ck(count("PostEventAtLocation") == 2 && count("at=GuestAOnB") >= 1 && count("at=HostOnB") == 0, "guest B plays the relayed PLY1 on guest A's avatar");

    reset("client", 3, 1);
    W().pup[0] = ha;
    W().pup[1] = ga;
    W().x1 = "#WS:1,PLY1,s,SK_AK74,2";
    tick();
    ck(count("PostEventAtLocation") == 0, "guest A does not play its own relayed shot (no echo)");

    reset("client", 3, 2);
    W().pup[0] = ha;
    W().pup[1] = ga;
    W().x1 = "#WS:1,PLY3,s,SK_AK74,1";
    tick();
    ck(count("PostEventAtLocation") == 0, "relayed pid with no matching view plays nothing");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().x1 = "#WS:1,PLY,s,SK_AK74,1";
    tick();
    ck(count("SpawnSystemAtLocation NS_Projectile_NPC_medium") == 1, "one tracer spawned for one shot");
    frame(0.1);
    double tx = tracer_x();
    ck(count("K2_SetWorldLocation") == 1 && tx > 3000 && tx < 3400, "tracer flies along the muzzle forward at wstrspd");
    ck(count("K2_DestroyComponent") == 0, "tracer alive while in flight");
    frame(0.4);
    ck(count("K2_DestroyComponent") == 1, "tracer destroyed after wstrlife");
    W().trace.clear();
    frame(0.1);
    ck(count("K2_SetWorldLocation") == 0 && count("K2_DestroyComponent") == 0, "destroyed tracer is not touched again");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().flags["wstrfly"] = x0::Val::I(0);
    W().x1 = "#WS:1,PLY,s,SK_AK74,1";
    tick();
    frame(0.1);
    frame(0.5);
    ck(count("SpawnSystemAtLocation NS_Projectile_NPC_medium") == 1 && count("K2_SetWorldLocation") == 0 && count("K2_DestroyComponent") == 0, "wstrfly=0 keeps the old static tracer");

    reset("client", 1, 1);
    me->rot = {350, 30, 0};
    shot(me);
    tick();
    ck(out_ws().find("PLY,s,SK_AK74,1,d-10.0:30.0") != std::string::npos, "local shot carries the control rotation as pitch,yaw");
    me->rot = {0, 0, 0};

    reset("client", 1, 1);
    W().flags["wstrdir"] = x0::Val::I(0);
    shot(me);
    tick();
    ck(out_ws().find(",SK_AK74,1,") == std::string::npos && out_ws().find("PLY,s,SK_AK74,1") != std::string::npos, "wstrdir=0 sends no direction");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().x1 = "#WS:1,PLY,s,SK_AK74,1,d0.0:90.0";
    tick();
    frame(0.1);
    x0::Vec3 tl = tracer_loc();
    ck(count("PostEventAtLocation") == 1 && tl.y > 3000 && tl.y < 3400 && std::abs(tl.x) < 50, "received direction steers the tracer, not the muzzle facing");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().flags["wstrdir"] = x0::Val::I(0);
    W().x1 = "#WS:1,PLY,s,SK_AK74,1,d0.0:90.0";
    tick();
    frame(0.1);
    ck(tracer_x() > 3000, "wstrdir=0 keeps the muzzle facing");

    reset("client", 1, 1);
    W().pup[0] = hp;
    W().x1 = "#WS:1,PLY,s,SK_AK74,1,dabc:90;2,PLY,s,SK_AK74,1";
    tick();
    frame(0.1);
    ck(count("PostEventAtLocation") == 2 && tracer_x() > 3000, "bad direction fields are ignored and the next entry still parses");

    reset("host", 3, 0);
    W().pup[0] = pa;
    W().pup[1] = pb;
    W().x1 = "#WS:1,PLY,s,SK_AK74,2,d5.0:45.0";
    tick();
    tick();
    ck(out_ws().find("PLY1,s,SK_AK74,2,d5.0:45.0") != std::string::npos, "host relay keeps the shooter's direction");

    std::printf("%d/%d passed\n", g_n - g_fail, g_n);
    return g_fail ? 1 : 0;
}
