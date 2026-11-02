#include "s2m_throw_logic.h"
#include "s2m_bodies.h"
#include "s2x.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <initializer_list>
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" uint64_t s2x_load_asset(const wchar_t* path);
extern "C" uint64_t s2x_spawn_actor(const wchar_t* cls, double x, double y, double z, double pitch, double yaw, double roll);

namespace s2m_throw
{
    namespace
    {
        using x0::Call;
        using x0::Ref;
        using x0::Vec3;
        constexpr x0::Mod MOD = x0::Mod::bodies;
        const char* const kSfx = "/Game/_STALKER2/Audio/WwiseAudio/Events/Weapons/Grenades/Default_Grenade/Grenade_Explosion/SFX_Grenade_Explosion.SFX_Grenade_Explosion";
        const char* const kNs = "/Game/_Stalker_2/VFX/Explosions/Niagara/NS_GrenadeRGD5_Explo.NS_GrenadeRGD5_Explo";

        bool valid(const Ref& r) { return r.valid(); }
        std::wstring wide(const std::string& s) { return std::wstring(s.begin(), s.end()); }
        bool loc(const Ref& a, Vec3& v) { return valid(a) && x0::call_v(a, L"K2_GetActorLocation", v); }
        Ref pref(const Ref& r, const char* p)
        {
            Ref o;
            if (valid(r) && x0::prop<Ref>(r, p, o) && valid(o)) return o;
            return Ref{};
        }
        Ref asset(const std::string& path)
        {
            if (path.empty()) return Ref{};
            std::wstring w = wide(path);
            Ref a = x0::find_path(w.c_str());
            if (valid(a)) return a;
            uint64_t p = s2x_load_asset(w.c_str());
            if (p) a = Ref::addr(p);
            return valid(a) ? a : Ref{};
        }
        Ref local_pawn() { return x0::first_of(L"BP_Stalker2Character_C"); }

        struct Own
        {
            Ref a;
            lg::Ent e;
            Vec3 prev;
            std::string ns;
            double prevAt = 0, t0 = 0, stillAt = 0, endAt = -1;
            bool ready = false;
        };

        struct St
        {
            std::set<uint64_t> seen;
            bool primed = false;
            std::vector<Own> own;
            int seq = 0;
            double scanAt = 0, hotUntil = 0;
            std::map<int, lg::RxView> rx;
            std::map<int, std::map<int, Ref>> actors;
            int sent = 0, spawned = 0, flown = 0, fx = 0, noMesh = 0, err = 0, scans = 0, foreign = 0;
            double scanMs = 0, scanMax = 0, logAt = 0;
        };
        St g;

        void err(const char* where, const char* what)
        {
            if (++g.err <= 10) x0::log(std::string("NOLA throw: erro ") + where + ": " + what);
        }

        std::string mesh_of(const Ref& a, char k)
        {
            Ref c = pref(a, k == 'b' ? "BoltComponent" : "StaticMeshComponent");
            if (!valid(c)) c = x0::call_o(a, L"K2_GetRootComponent");
            Ref sm = pref(c, "StaticMesh");
            return valid(sm) ? lg::asset_path(x0::full_name(sm)) : std::string();
        }

        std::string ns_of(const Ref& a)
        {
            Ref ns = pref(pref(a, "ExplosionComponent"), "ExplosionParticles");
            return valid(ns) ? lg::asset_path(x0::full_name(ns)) : std::string();
        }

        int owner_match(const Ref& r, const Ref& pawn)
        {
            Ref pc = x0::call_o(pawn, L"GetController");
            bool any = false;
            for (const wchar_t* fn : {L"GetInstigator", L"GetOwner"})
            {
                Ref o = x0::call_o(r, fn);
                if (!valid(o)) continue;
                any = true;
                if (o.address() == pawn.address() || (valid(pc) && o.address() == pc.address())) return 1;
            }
            return any ? 0 : -1;
        }

        void scan(double now)
        {
            auto t0 = std::chrono::steady_clock::now();
            std::vector<std::pair<Ref, char>> found;
            for (const wchar_t* c : {L"Bolt", L"BP_Bolt_C"})
                for (auto& r : x0::all_of(c, 0)) found.push_back({r, 'b'});
            for (const wchar_t* c : {L"Grenade", L"BP_Grenade_C"})
                for (auto& r : x0::all_of(c, 0)) found.push_back({r, 'g'});
            double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            g.scans++;
            g.scanMs += ms;
            if (ms > g.scanMax) g.scanMax = ms;
            Ref pawn = local_pawn();
            Vec3 pp;
            bool hasP = loc(pawn, pp);
            for (auto& [r, k] : found)
            {
                if (!g.seen.insert(r.address()).second) continue;
                if (!g.primed || !hasP) continue;
                Vec3 v;
                if (!loc(r, v)) continue;
                if (!lg::mine(pp.x, pp.y, pp.z, v.x, v.y, v.z, x0::fd("thrnear", 400))) continue;
                int om = owner_match(r, pawn);
                if (x0::fi("thrown", 1) == 1 && !lg::own_ok(om, now, m01::last_th_at(), x0::fd("thrhot", 3)))
                {
                    if (++g.foreign <= 20) x0::log(lg::fmt_line("NOLA throw: %s alheio ignorado em %.0f,%.0f,%.0f dono=%d", k == 'b' ? "parafuso" : "granada", v.x, v.y, v.z, om));
                    continue;
                }
                Own o;
                o.a = r;
                g.seq = g.seq % 9999 + 1;
                o.e.seq = g.seq;
                o.e.k = k;
                o.e.st = 'f';
                o.e.x = v.x, o.e.y = v.y, o.e.z = v.z;
                Vec3 vel;
                if (x0::call_v(r, L"GetVelocity", vel)) o.e.vx = vel.x, o.e.vy = vel.y, o.e.vz = vel.z;
                o.e.path = mesh_of(r, k);
                o.ready = lg::tx_ready(o.e, x0::fi("thrvel", 1) == 1);
                o.prev = v;
                o.prevAt = now;
                o.t0 = now;
                o.stillAt = now;
                g.own.push_back(o);
                g.sent++;
                m01::note_throw();
                if (g.sent <= 40)
                    x0::log(lg::fmt_line("NOLA throw: meu %s seq=%d em %.0f,%.0f,%.0f vel=%.0f,%.0f,%.0f dono=%d malha=%s", k == 'b' ? "parafuso" : "granada", o.e.seq, v.x, v.y, v.z, o.e.vx, o.e.vy, o.e.vz,
                                         om, o.e.path.c_str()));
            }
            g.primed = true;
            if (g.seen.size() > 4000)
            {
                std::set<uint64_t> keep;
                for (auto& [r, k] : found) keep.insert(r.address());
                g.seen.swap(keep);
            }
        }

        void track(double now)
        {
            for (auto& o : g.own)
            {
                if (o.e.st != 'f') continue;
                Vec3 v;
                if (!loc(o.a, v))
                {
                    o.e.st = o.e.k == 'g' ? 'x' : 'r';
                    o.e.vx = o.e.vy = o.e.vz = 0;
                    o.endAt = now;
                    x0::log(lg::fmt_line("NOLA throw: meu seq=%d acabou (%s) em %.0f,%.0f,%.0f apos %.1f s", o.e.seq, o.e.st == 'x' ? "explodiu" : "sumiu", o.e.x, o.e.y, o.e.z, now - o.t0));
                    continue;
                }
                double dt = now - o.prevAt;
                Vec3 vel;
                bool hv = x0::call_v(o.a, L"GetVelocity", vel);
                double mv = std::sqrt((v.x - o.prev.x) * (v.x - o.prev.x) + (v.y - o.prev.y) * (v.y - o.prev.y) + (v.z - o.prev.z) * (v.z - o.prev.z));
                if ((!hv || std::fabs(vel.x) + std::fabs(vel.y) + std::fabs(vel.z) < 1) && dt > 0.01 && mv > 2)
                    vel = Vec3{(v.x - o.prev.x) / dt, (v.y - o.prev.y) / dt, (v.z - o.prev.z) / dt}, hv = true;
                if (!hv) vel = Vec3{};
                o.e.x = v.x, o.e.y = v.y, o.e.z = v.z;
                o.e.vx = vel.x, o.e.vy = vel.y, o.e.vz = vel.z;
                o.prev = v;
                o.prevAt = now;
                if (!o.ready && lg::tx_ready(o.e, x0::fi("thrvel", 1) == 1))
                {
                    o.ready = true;
                    x0::log(lg::fmt_line("NOLA throw: meu seq=%d pronto apos %.2f s em %.0f,%.0f,%.0f vel=%.0f,%.0f,%.0f", o.e.seq, now - o.t0, v.x, v.y, v.z, vel.x, vel.y, vel.z));
                }
                if (o.e.k == 'g' && o.ns.empty()) o.ns = ns_of(o.a);
                if (mv > 3) o.stillAt = now;
                bool rest = o.e.k == 'b' && now - o.stillAt > 1.0;
                if (rest || now - o.t0 > x0::fd("thrlife", 8))
                {
                    o.e.st = 'r';
                    o.e.vx = o.e.vy = o.e.vz = 0;
                    o.endAt = now;
                    x0::log(lg::fmt_line("NOLA throw: meu seq=%d parou em %.0f,%.0f,%.0f apos %.1f s", o.e.seq, o.e.x, o.e.y, o.e.z, now - o.t0));
                }
            }
            std::vector<Own> keep;
            for (auto& o : g.own)
                if (o.endAt < 0 || now - o.endAt < 2.5) keep.push_back(o);
            g.own.swap(keep);
        }

        void tick_tx()
        {
            if (x0::fi("thr", 1) != 1) return;
            double now = x0::now();
            g.hotUntil = std::max(g.hotUntil, m01::last_th_at() + x0::fd("thrhot", 3));
            bool hot = now < g.hotUntil || !g.own.empty();
            if (now >= g.scanAt)
            {
                g.scanAt = now + (hot ? 0.09 : x0::fd("thridle", 0.5));
                try { scan(now); }
                catch (const std::exception& ex) { err("scan", ex.what()); }
            }
            try { track(now); }
            catch (const std::exception& ex) { err("track", ex.what()); }
        }

        std::string out_th()
        {
            if (x0::fi("thr", 1) != 1 || g.own.empty()) return "";
            std::vector<lg::Ent> v;
            for (auto& o : g.own)
            {
                if (!o.ready && o.e.st == 'f') continue;
                lg::Ent e = o.e;
                if (e.k == 'g' && e.st == 'x')
                {
                    e.path = o.ns;
                }
                v.push_back(e);
            }
            return lg::tag_str(v);
        }

        void kill(Ref& a)
        {
            if (valid(a)) x0::call0(a, L"K2_DestroyActor");
            a.reset();
        }

        void boom(const Ref& a, const lg::Ent& e)
        {
            Ref ctx = valid(a) ? a : local_pawn();
            Vec3 L{e.x, e.y, e.z};
            x0::Rot R{};
            Ref ns = asset(e.path.find("NS_") != std::string::npos ? e.path : std::string(kNs));
            Ref nf = x0::find_path(L"/Script/Niagara.Default__NiagaraFunctionLibrary");
            if (valid(ns) && valid(nf))
            {
                Call s(nf, L"SpawnSystemAtLocation");
                s.o(L"WorldContextObject", ctx).o(L"SystemTemplate", ns).v(L"Location", L).r(L"Rotation", R);
                s.v(L"Scale", Vec3{1, 1, 1}).b(L"bAutoDestroy", true).b(L"bAutoActivate", true).i(L"PoolingMethod", 0).b(L"bPreCullCheck", true);
                s.run();
            }
            Ref ev = asset(kSfx);
            Ref ak = x0::find_path(L"/Script/AkAudio.Default__AkGameplayStatics");
            if (valid(ev) && valid(ak)) Call(ak, L"PostEventAtLocation").o(L"AkEvent", ev).v(L"Location", L).r(L"Orientation", R).o(L"WorldContextObject", ctx).run();
            g.fx++;
            x0::log(lg::fmt_line("NOLA throw: granada do parceiro seq=%d explodiu em %.0f,%.0f,%.0f fx=%s som=%s", e.seq, e.x, e.y, e.z, valid(ns) ? "ok" : "falta", valid(ev) ? "ok" : "falta"));
        }

        Ref spawn_copy(const lg::Ent& e)
        {
            Ref mesh = asset(e.path);
            if (!valid(mesh)) { g.noMesh++; return Ref{}; }
            uint64_t p = s2x_spawn_actor(L"/Script/Engine.StaticMeshActor", e.x, e.y, e.z, 0, 0, 0);
            Ref a = p ? Ref::addr(p) : Ref{};
            if (!valid(a)) return Ref{};
            Ref c = pref(a, "StaticMeshComponent");
            if (valid(c))
            {
                Call(c, L"SetMobility").i(L"NewMobility", 2).run();
                Call(c, L"SetCollisionEnabled").i(L"NewType", 0).run();
                Call(c, L"SetStaticMesh").o(L"NewMesh", mesh).run();
            }
            g.spawned++;
            return a;
        }

        void place(const Ref& a, const lg::RxCopy& c, double now)
        {
            if (!valid(a)) return;
            double x, y, z;
            lg::fly(c.e, now - c.rxAt, 0.3, x, y, z);
            Call(a, L"K2_SetActorLocation").v(L"NewLocation", Vec3{x, y, z}).b(L"bSweep", false).b(L"bTeleport", true).run();
        }

        void rx_view(int view, const std::string& part, double now)
        {
            auto& V = g.rx[view];
            auto& act = g.actors[view];
            lg::RxOut o = lg::rx_step(V, lg::parse(part), now, x0::fd("thrkeep", 6), x0::fd("threset", 3));
            for (int s : o.spawn)
            {
                const lg::Ent& e = V.cm[s].e;
                Ref a = spawn_copy(e);
                act[s] = a;
                g.flown++;
                x0::log(lg::fmt_line("NOLA throw: %s do parceiro seq=%d voando de %.0f,%.0f,%.0f vel=%.0f,%.0f,%.0f copia=%s malha=%s", e.k == 'b' ? "parafuso" : "granada", e.seq, e.x, e.y, e.z, e.vx, e.vy, e.vz,
                                     valid(a) ? "ok" : "falhou", e.path.c_str()));
            }
            for (int s : o.boom)
            {
                Ref& a = act[s];
                place(a, V.cm[s], now);
                boom(a, V.cm[s].e);
                kill(a);
            }
            for (int s : o.land)
            {
                const lg::RxCopy& c = V.cm[s];
                place(act[s], c, now);
                x0::log(lg::fmt_line("NOLA throw: parafuso do parceiro seq=%d caiu em %.0f,%.0f,%.0f", s, c.e.x, c.e.y, c.e.z));
            }
            for (int s : o.despawn)
            {
                auto it = act.find(s);
                if (it != act.end()) { kill(it->second); act.erase(it); }
            }
            for (auto& [s, c] : V.cm)
                if (c.endAt < 0) place(act[s], c, now);
        }

        void clear_all()
        {
            for (auto& [v, act] : g.actors)
                for (auto& [s, a] : act) kill(a);
            g.actors.clear();
            g.rx.clear();
        }

        void tick_rx()
        {
            if (x0::fi("thr", 1) != 1)
            {
                if (!g.actors.empty() || !g.rx.empty()) clear_all();
                return;
            }
            double now = x0::now();
            bool host = x0::is_host();
            int nv = host ? std::min(x0::views(), (int)x0::kMaxClients) : 1;
            for (int v = 0; v < nv; ++v)
            {
                std::string part;
                bool got = host ? x0::rpart("TH", part, v) : x0::rpart("TH", part);
                if (!got) part.clear();
                try { rx_view(v, part, now); }
                catch (const std::exception& ex) { err("rx", ex.what()); }
            }
            if (now - g.logAt >= 10)
            {
                if (g.sent + g.flown + g.err > 0)
                    x0::log(lg::fmt_line("NOLA throw 10 s: meus=%d | parceiro: voando=%d copias=%d explosoes=%d sem_malha=%d erros=%d | busca n=%d media=%.2f ms max=%.2f ms", g.sent, g.flown, g.spawned, g.fx, g.noMesh, g.err,
                                         g.scans, g.scans ? g.scanMs / g.scans : 0.0, g.scanMax));
                g.sent = g.flown = g.spawned = g.fx = g.noMesh = g.err = g.scans = 0;
                g.scanMs = g.scanMax = 0;
                g.logAt = now;
            }
        }
    }


}

namespace
{
    void throw_init()
    {
        constexpr x0::Mod M = x0::Mod::bodies;
        x0::own_tag(M, "TH", [] { return s2m_throw::out_th(); });
        x0::every(M, "throwTx", x0::Rate::t100, [] { s2m_throw::tick_tx(); });
        x0::every(M, "throwRx", x0::Rate::frame, [] { s2m_throw::tick_rx(); });
    }
    ::x0::ModReg g_throw_reg(::x0::Mod::bodies, "throw", &throw_init);
}
