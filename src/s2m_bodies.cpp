#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "s2m_bodies.h"
#include "s2m_bodies_logic.h"
#include "s2m_bodies_ro.h"
#include "s2m_avbones_logic.h"
#include "s2m_ixtap.h"

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"
#include "s2m_damage.h"
#include "s2m_items.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#undef far
#undef near

extern "C" uint64_t s2x_load_asset(const wchar_t* path);
extern "C" uint64_t s2m_bodies_load_asset_none(const wchar_t*) { return 0; }
extern "C" uint64_t s2x_spawn_actor(const wchar_t* cls, double x, double y, double z, double pitch, double yaw, double roll);
extern "C" uint64_t s2m_bodies_spawn_actor_none(const wchar_t*, double, double, double, double, double, double) { return 0; }
#pragma comment(linker, "/alternatename:s2x_load_asset=s2m_bodies_load_asset_none")
#pragma comment(linker, "/alternatename:s2x_spawn_actor=s2m_bodies_spawn_actor_none")

namespace
{
    using x0::Call;
    using x0::Ref;
    using x0::Rot;
    using x0::Vec3;
    namespace lg = m01::lg;
    using lg::fmt;
    using lg::num;
    using lg::tf;
    constexpr x0::Mod MOD = x0::Mod::bodies;

    double FL(const char* k, double d) { return x0::fd(k, d); }
    std::string FS(const char* k)
    {
        x0::Val v = x0::flag(k);
        if (v.k == x0::Val::nil) return "nil";
        if (v.k == x0::Val::f) return num(v.fv);
        return v.str();
    }
    int g_cv = 0;
    int g_shN[x0::kMaxClients] = {};
    bool g_inv = false;
    bool multi() { return x0::max_clients() > 1; }
    void LOG(const std::string& s)
    {
        if (g_inv && multi()) x0::log(s + " [pid " + std::to_string(x0::view_pid(g_cv)) + "]");
        else x0::log(s);
    }
    const lg::Ctx C{&FL, &FS, &LOG};

    std::wstring W(const std::string& s)
    {
        std::wstring o;
        size_t i = 0;
        while (i < s.size())
        {
            unsigned char c = (unsigned char)s[i];
            uint32_t cp = c;
            int n = 0;
            if (c >= 0xF0) { cp = c & 0x07; n = 3; }
            else if (c >= 0xE0) { cp = c & 0x0F; n = 2; }
            else if (c >= 0xC0) { cp = c & 0x1F; n = 1; }
            i++;
            for (int k = 0; k < n && i < s.size(); k++, i++) cp = (cp << 6) | ((unsigned char)s[i] & 0x3F);
            if (cp >= 0x10000) { cp -= 0x10000; o += (wchar_t)(0xD800 + (cp >> 10)); o += (wchar_t)(0xDC00 + (cp & 0x3FF)); }
            else o += (wchar_t)cp;
        }
        return o;
    }
    std::string N8(const std::wstring& s)
    {
        std::string o;
        for (size_t i = 0; i < s.size(); i++)
        {
            uint32_t cp = (uint16_t)s[i];
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size()) { cp = 0x10000 + ((cp - 0xD800) << 10) + ((uint16_t)s[i + 1] - 0xDC00); i++; }
            if (cp < 0x80) o += (char)cp;
            else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
            else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
            else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
        }
        return o;
    }

    bool has(const Ref& r) { return r.raw != nullptr; }
    bool valid(const Ref& r) { return r.valid(); }
    std::string strip_full(const std::string& f)
    {
        size_t k = f.find(' ');
        if (k == std::string::npos || k == 0) return f;
        return f.substr(k + 1);
    }
    std::string sfull(const Ref& r) { return strip_full(x0::full_name(r)); }
    std::string nm(const Ref& r) { return valid(r) ? x0::obj_name(r) : std::string("nulo"); }
    std::string leaf_dot(const std::string& s) { return lg::leaf_of(s, '.'); }
    std::string ostr(const std::optional<int64_t>& v) { return v ? lg::inum(*v) : std::string("nil"); }
    std::string ostrd(const std::optional<double>& v) { return v ? num(*v) : std::string("nil"); }

    bool loc(const Ref& r, Vec3& v) { return valid(r) && x0::call_v(r, L"K2_GetActorLocation", v); }
    bool rot(const Ref& r, Rot& o)
    {
        if (!valid(r)) return false;
        Call c(r, L"K2_GetActorRotation");
        return c.run() && c.rr(o);
    }
    bool hide_actor(const Ref& r, bool h) { return valid(r) && Call(r, L"SetActorHiddenInGame").b(L"bNewHidden", h).run(); }
    bool set_loc(const Ref& r, const Vec3& v) { return valid(r) && Call(r, L"K2_SetActorLocation").v(L"NewLocation", v).b(L"bSweep", false).b(L"bTeleport", true).run(); }
    bool set_yaw(const Ref& r, double yaw) { return valid(r) && Call(r, L"K2_SetActorRotation").r(L"NewRotation", Rot{0, yaw, 0}).b(L"bTeleportPhysics", false).run(); }
    bool no_collision(const Ref& r) { return valid(r) && Call(r, L"SetActorEnableCollision").b(L"bNewActorEnableCollision", false).run(); }
    bool destroy_actor(const Ref& r) { return valid(r) && x0::call0(r, L"K2_DestroyActor"); }
    bool fcall(const Ref& r, const wchar_t* fn, float& out)
    {
        if (!valid(r)) return false;
        Call c(r, fn);
        if (!c.run()) return false;
        out = c.rf();
        return true;
    }
    bool bcall(const Ref& r, const wchar_t* fn, bool& out)
    {
        if (!valid(r)) return false;
        Call c(r, fn);
        if (!c.run()) return false;
        out = c.rb();
        return true;
    }
    Ref ocall(const Ref& r, const wchar_t* fn) { return valid(r) ? x0::call_o(r, fn) : Ref{}; }
    Ref pref(const Ref& r, const char* p)
    {
        Ref o;
        if (valid(r) && x0::prop<Ref>(r, p, o) && valid(o)) return o;
        return Ref{};
    }
    Ref mediator() { return x0::find_path(L"/Script/Stalker2.Default__CppMediator"); }
    std::optional<int64_t> guid_of(const Ref& o)
    {
        Ref med = mediator();
        if (!valid(med) || !valid(o)) return std::nullopt;
        Call c(med, L"GetGUID");
        c.o(L"Actor", o);
        if (!c.run()) return std::nullopt;
        return c.ri();
    }
    bool destroy_model(int64_t uid)
    {
        Ref ai2 = x0::first_of(L"CustomConsoleManagerAI2");
        return valid(ai2) && Call(ai2, L"XDestroyModel").i(L"UID", uid).run();
    }
    std::vector<Ref> comps(const Ref& o, const wchar_t* cls)
    {
        Ref c = x0::find_path(cls);
        if (!valid(c) || !valid(o)) return {};
        Call k(o, L"K2_GetComponentsByClass");
        k.o(L"ComponentClass", c);
        if (!k.run()) return {};
        return k.rarr();
    }
    void t0_fix(const Ref& c)
    {
        Call(c, L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", Vec3{0, 0, 0}).r(L"NewRotation", Rot{0, 0, 0}).b(L"bSweep", false).b(L"bTeleport", true).run();
        Call(c, L"SetRelativeScale3D").v(L"NewScale3D", Vec3{1, 1, 1}).run();
    }
    Ref add_comp(const Ref& actor, const wchar_t* cls, bool manual)
    {
        Ref k = x0::find_path(cls);
        if (!valid(k) || !valid(actor)) return Ref{};
        Call c(actor, L"AddComponentByClass");
        c.o(L"Class", k).b(L"bManualAttachment", manual).b(L"bDeferredFinish", false);
        if (!c.run()) return Ref{};
        Ref r = c.ro();
        if (valid(r) && x0::is_a(r, L"SceneComponent")) t0_fix(r);
        return r;
    }
    bool attach(const Ref& c, const Ref& parent, const std::string& sock)
    {
        std::wstring ws = W(sock);
        return Call(c, L"K2_AttachToComponent").o(L"Parent", parent).n(L"SocketName", ws.c_str()).i(L"LocationRule", 2).i(L"RotationRule", 2).i(L"ScaleRule", 2).b(L"bWeldSimulatedBodies", false).run();
    }

    std::unordered_map<std::string, Ref> g_seqs;
    std::unordered_set<std::string> g_paMiss;
    Ref asset(const std::string& path)
    {
        std::wstring w = W(path);
        Ref a = x0::find_path(w.c_str());
        if (valid(a)) return a;
        uint64_t p = s2x_load_asset(w.c_str());
        if (p) a = Ref::addr(p);
        if (!valid(a)) a = x0::find_path(w.c_str());
        return valid(a) ? a : Ref{};
    }
    Ref seq_asset(const std::string& key, const std::string& path)
    {
        auto it = g_seqs.find(key);
        if (it != g_seqs.end() && valid(it->second)) return it->second;
        Ref q = asset(path);
        g_seqs[key] = q;
        return q;
    }

    struct Pm
    {
        bool on = false;
        Ref pawn;
        int rm = 0, mm = 0, snaps = 0, clipN = 0, clipBad = 0;
        std::optional<int64_t> rm0, mm0;
        std::optional<std::string> clip;
        uint64_t clipAI = 0;
        Ref mon;
        std::optional<double> rate;
        lg::PaState pa;
        std::optional<std::string> paLog;
        struct Ov
        {
            std::string key;
            Ref mon;
            uint64_t ai = 0;
            int bad = 0;
        };
        std::map<std::string, Ov> ov;
    };
    struct Cyc
    {
        double t;
        std::string why;
        double hp;
    };
    struct KickD
    {
        int n = 0, s = 0;
        double until = 0, mHA = 0, mHH = 0, mAng = 0, t0 = 0, tPk = 0;
        Vec3 bHA{}, bHH{}, bF{};
    };
    struct Body
    {
        Ref pawn;
        std::optional<std::string> path;
        bool setup = false;
        std::optional<double> ref;
        std::optional<std::string> state;
        lg::PupPos pos;
        std::vector<Ref> wcomps;
        std::vector<Ref> oldW;
        std::optional<std::string> wstr;
        std::optional<std::string> det;
        std::optional<double> spawnAt;
        bool hasBefore = false;
        std::unordered_set<std::string> before;
        std::string pendSeen;
        int asks = 0;
        std::optional<int> rounds;
        bool gaveUp = false;
        double retryAt = 0;
        bool adoptOnly = false;
        double findAt = 0;
        std::optional<double> askedAt;
        bool hasSx = false;
        double sx = 0, sy = 0;
        double hpLo = 0, hpHi = 0;
        std::optional<double> lastSpawn;
        std::optional<int> ses;
        bool force = false;
        std::optional<int64_t> uid;
        bool hidden = false;
        Ref abpFor, naFor, uroFor;
        bool uroBool = true;
        uint8_t uroVis = 1;
        std::optional<double> yaw, lastT;
        std::optional<double> yawF;
        double yawFT = 0, hbAt = -10;
        Ref yawFor;
        std::optional<lg::PupPos> pmW;
        bool nsYaw = false;
        bool broken = false;
        std::unordered_set<std::string> bad;
        std::optional<double> pfH;
        std::string paFace;
        std::optional<std::string> paSt;
        std::optional<std::string> eqSt;
        double eqUntil = 0;
        std::optional<std::string> acSt, acTok;
        double acUntil = 0;
        Ref vzMesh;
        Vec3 vzBase{};
        double vzCur = 0, vzUntil = 0, vzAt = 0, vzMax = 0, vzHh = 90, vzOx = 0, vzOy = 0, vzDx = 1, vzDy = 0;
        int vzLog = 0;
        bool vzOn = false;
        lg::VzScan vzS;
        std::optional<std::string> vzTok;
        lg::AcRx acRx;
        lg::LhRx lh;
        std::optional<std::string> hitTok;
        int hitSeq = 0;
        double hitAt = -100;
        Ref lnFor;
        std::optional<lg::LRot> lnBase;
        double lnCur = 0, lnSet = 0, lnAt = 0;
        int lnWant = 0, lnN = 0, lnLogW = 0;
        lg::YawHold yh;
        lg::LadSt lad;
        std::vector<uint64_t> ladHid;
        Pm pm;
        bool wd = false, wdOut = false, wdGod = false;
        Ref wdFor;
        int wdTry = 0;
        double wdAt = 0, wwAt = 0;
        lg::WdWatch ww;
        double wsnap = 0;
        std::string wsock;
        uint64_t wsComp = 0;
        bool wsHand = false, rlWant = false, wBolt = false, wKnife = false;
        std::string bKey;
        Vec3 rlLast{};
        Rot rlLastR{};
        bool rlCan = false;
        std::string rlBaseKey;
        const char* acRel = nullptr;
        bool rlKeep = false, wrlAdd = false;
        Vec3 rlKL{};
        Rot rlKR{};
        double rlKT0 = -1;
        std::string wskel, wrlPath;
        double wrlT0 = 0, wrlUntil = 0;
        int shSeen = -1, shN = 0;
        KickD shd;
        int rdN = 0;
        double rdAt = 0;
        bool rlAmmo = false;
        std::map<std::string, double> roA;
        double roAt = 0, roSampAt = 0;
        bool roPt = false;
        std::string roLog, roSamp;
        uint64_t kidMesh = 0;
        bool god = false;
        double godAt = 0;
        std::optional<double> deadAt;
        int held = 0;
        double ignored = 0;
        std::optional<double> realAt;
        int bleedCleared = 0;
        std::optional<std::string> parkOn;
        int parkDrop = 0;
        std::deque<Cyc> cyc;
        int cycN = 0;
        Ref rmFor;
        std::optional<int64_t> rm0;
        int rmN = 0, stopN = 0;
        Ref walkFor;
        int walkN = 0;
        Ref cmcOff;
        int cmcN = 0;
    };
    struct LookE
    {
        std::string path;
        std::vector<std::string> mats;
        bool main = false;
    };
    struct Far
    {
        bool on = false;
        Ref actor, main;
        std::optional<bool> hidden;
        std::optional<std::string> state;
        lg::FarPos fp;
        double retryAt = 0;
    };
    struct Gate
    {
        std::optional<std::string> key;
        std::optional<double> since;
        std::optional<std::pair<double, double>> lp;
        bool open = false;
        lg::Hold hold;
    };
    struct Cmo
    {
        uint64_t addr = 0;
        Ref a;
        double at = 0;
        int back = 0;
    };
    struct GM
    {
        uint64_t addr = 0;
        Ref a, pmc;
        lg::Mover k;
    };
    struct Ns
    {
        bool made = false;
        std::map<std::string, GM> k;
        lg::NsS s;
        bool live = false;
        int logMk = 0;
    };
    struct Abg
    {
        bool reg = false;
        double next = 0;
        std::unordered_set<std::string> seen;
        int nseen = 0;
        std::map<std::string, std::vector<double>> kill;
        std::map<std::string, double> cap;
        int lastBad = 0;
    };
    struct Pai
    {
        Ref body;
        bool hasBody = false;
        std::optional<double> t0;
        std::optional<bool> on;
        int n = 0;
        std::optional<double> logAt, capAt;
        std::vector<double> kill;
    };
    struct CvSaved
    {
        double v;
        bool isint;
    };
    struct Offt
    {
        int set = 0, again = 0;
        bool saved = false;
        std::map<std::string, CvSaved> sv;
    };
    struct SelfLook
    {
        bool has = false;
        std::string wname, str;
        double at = 0;
        int64_t eqType = -2;
    };
    struct Pl
    {
        Body pup, tgt;
        Far far;
        bool hasCmo = false;
        Cmo cmo;
        lg::NB nb;
        lg::RVel rvel;
        lg::SmStat sm;
        lg::PfState pf;
        lg::PfxH pfxH;
        std::vector<double> pfxR;
        std::optional<double> jumpUntil;
        m01::Dmg dmg;
        Pai pai;
        bool kzOut = false;
        int kzN = 0;
        int pid = -1;
        bool gone = false;
    };
    struct St
    {
        Pl pl[x0::kMaxClients];
        Gate wg;
        int cmoN = 0, cmoBack = 0;
        Ns ns;
        int dgN = 0;
        double dgSweepAt = 0;
        std::unordered_set<uint64_t> nocol;
        std::optional<lg::Quiet> quiet;
        std::optional<lg::Shot> shot;
        int niN = 0;
        Abg abg;
        Offt offt;
        bool afOff = false;
        bool pmVelLog = false;
        bool hasFarLook = false;
        std::vector<LookE> farLook;
        SelfLook self;
        std::vector<std::pair<x0::Mod, m01::OwnCheck>> own;
        m01::Ext ext;
    };
    St& S()
    {
        static St s;
        return s;
    }
    Pl& V() { return S().pl[g_cv]; }
    Pl& VW(int v) { return S().pl[v >= 0 && v < x0::kMaxClients ? v : 0]; }
    struct CvScope
    {
        int was;
        bool in;
        explicit CvScope(int v) : was(g_cv), in(g_inv) { g_cv = v >= 0 && v < x0::kMaxClients ? v : 0; g_inv = true; }
        ~CvScope() { g_cv = was; g_inv = in; }
        CvScope(const CvScope&) = delete;
        CvScope& operator=(const CvScope&) = delete;
    };
    std::string pup_key(int v) { return v == 0 ? std::string("pup") : "pup" + std::to_string(v); }
    bool is_body(const Ref& o)
    {
        if (!has(o)) return false;
        for (const Pl& p : S().pl)
            if ((has(p.pup.pawn) && o.same(p.pup.pawn)) || (has(p.tgt.pawn) && o.same(p.tgt.pawn))) return true;
        return false;
    }
    int body_view(const Ref& o, bool& isTgt)
    {
        isTgt = false;
        if (!has(o)) return -1;
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            const Pl& p = S().pl[v];
            if (has(p.pup.pawn) && o.same(p.pup.pawn)) return v;
            if (has(p.tgt.pawn) && o.same(p.tgt.pawn)) { isTgt = true; return v; }
        }
        return -1;
    }
    bool other_spawning()
    {
        bool sp[x0::kMaxClients];
        for (int v = 0; v < x0::kMaxClients; ++v) sp[v] = S().pl[v].pup.spawnAt.has_value() || S().pl[v].tgt.spawnAt.has_value();
        return !lg::spawn_turn(g_cv, sp, x0::kMaxClients);
    }

    bool owns_avatar() { return x0::fs("avatarmode") == "npcpuppet" && FL("corepuppet", 0) == 1; }
    bool pm_vel()
    {
        double m = FL("pupmove", 0);
        return (m == 1 || m == 3) && FL("pupanimbp", 0) == 1 && FL("nsmooth", 0) != 1;
    }
    Ref local_pawn() { return x0::first_of(L"BP_Stalker2Character_C"); }
    bool local_pos(Vec3& v) { return loc(local_pawn(), v); }

    bool remote_snap(lg::RSnap& r)
    {
        const x0::Snap& s = x0::remote(g_cv);
        if (!s.ok || !s.has_pos) return false;
        r.seq = s.seq;
        r.x = s.x;
        r.y = s.y;
        r.z = s.z;
        r.vx = s.vx;
        r.vy = s.vy;
        r.yaw = s.yaw;
        if (s.t >= 0) r.t = s.t;
        else r.t.reset();
        r.recv = s.recv;
        return true;
    }
    std::optional<std::string> rpart(const char* tag)
    {
        std::string o;
        if (x0::rpart(tag, o, g_cv)) return o;
        return std::nullopt;
    }
    std::optional<std::string> remote_life() { return lg::remote_life(rpart("L")); }
    bool remote_down()
    {
        auto st = remote_life();
        return st && *st != "a";
    }
    bool remote_cine()
    {
        auto& fv = S().ext.remote_cine_v;
        if (fv && (g_cv > 0 || multi())) return fv(g_cv);
        if (g_cv > 0) return false;
        auto& f = S().ext.remote_cine;
        return f ? f() : false;
    }
    bool loot_pending() { auto& f = S().ext.loot_pending; return f ? f() : false; }
    bool loot_body(const Ref& o) { auto& f = S().ext.loot_body; return f ? f(o) : false; }

    bool body_ok(const Body& B)
    {
        if (!valid(B.pawn)) return false;
        if (!B.path) return true;
        return sfull(B.pawn) == *B.path;
    }
    bool body_held(const Body& B)
    {
        if (FL("bodysafe", 1) == 1) return body_ok(B);
        return valid(B.pawn);
    }
    bool gate_open() { return !(FL("bodysafe", 1) == 1 && !S().wg.open); }
    void body_lost()
    {
        auto& w = S().wg;
        if (FL("bodysafe", 1) == 1 && w.since)
        {
            w.since = std::max(*w.since, x0::now());
            w.open = false;
        }
    }

    bool in_world(double x, double y, double z) { return FL("kzsafe", 0) != 1 || x0::in_world(x, y, z); }
    bool remote_in(const lg::RSnap& r, const char* what)
    {
        Pl& s = V();
        if (FL("kzsafe", 0) != 1 || x0::in_world(r.x, r.y, r.z))
        {
            if (s.kzOut)
            {
                s.kzOut = false;
                LOG(fmt("NOLA kzsafe: remoto de volta ao mundo (z=%.0f) depois de %d recusas", r.z, s.kzN));
                s.kzN = 0;
            }
            return true;
        }
        s.kzN++;
        if (!s.kzOut)
        {
            s.kzOut = true;
            LOG(fmt("NOLA kzsafe: remoto FORA DO MUNDO (%s,%s,%s; killz=%s) -> nada nosso vai para la (%s); boneco/alvo parados e escondidos",
                    num(r.x).c_str(), num(r.y).c_str(), num(r.z).c_str(), FS("killz").c_str(), what));
        }
        return false;
    }
    bool pawn_out(const Ref& o)
    {
        if (FL("kzsafe", 0) != 1 || !x0::is_host() || !valid(o)) return false;
        Vec3 q;
        if (!loc(o, q)) return false;
        return !x0::in_world(q.x, q.y, q.z);
    }

    void far_hide(Far& F)
    {
        if (valid(F.actor)) hide_actor(F.actor, true);
        F.hidden = true;
    }
    void kz_hold()
    {
        Pl& s = V();
        Body& P = s.pup;
        if (has(P.pawn) && !P.hidden && valid(P.pawn)) { P.hidden = true; hide_actor(P.pawn, true); }
        if (has(s.far.actor) && !s.far.hidden.value_or(false)) far_hide(s.far);
    }

    void tg_note(const char* why, double hp)
    {
        Body& T = V().tgt;
        T.cyc.push_back(Cyc{x0::now(), why, hp});
        if (T.cyc.size() > 10) T.cyc.pop_front();
        T.cycN++;
        if (T.cycN <= 20 || T.cycN % 50 == 0) LOG(fmt("NOLA tgtcyc: %s hp=%.1f (#%d)", why, hp, T.cycN));
    }
    void tg_park(const char* why)
    {
        Body& T = V().tgt;
        if (FL("tgtpark", 0) != 1) return;
        if (!T.parkOn) { T.parkOn = why; tg_note((std::string("parado:") + why).c_str(), -1); }
        if (T.ref) T.parkDrop++;
        T.ref.reset();
    }
    void tg_back()
    {
        Body& T = V().tgt;
        if (T.parkOn) { T.parkOn.reset(); tg_note("volta", -1); }
    }

    bool human_class(const Ref& ai) { return valid(ai) && x0::is_a(ai, L"AnimInstanceHuman"); }
    bool human_anim(const Ref& pawn)
    {
        Ref mesh = pref(pawn, "Mesh");
        if (!valid(mesh)) return false;
        Call m(mesh, L"GetAnimationMode");
        if (!m.run() || m.ri() != 0) return false;
        Ref ai = ocall(mesh, L"GetAnimInstance");
        return human_class(ai);
    }
    Ref pm_ai(const Body& P)
    {
        Ref mesh = pref(P.pawn, "Mesh");
        Ref ai = ocall(mesh, L"GetAnimInstance");
        return valid(ai) ? ai : Ref{};
    }

    void drop_body(Body& B)
    {
        B.pawn.reset();
        B.path.reset();
        B.setup = false;
        B.ref.reset();
        B.state.reset();
        B.pos = lg::PupPos{};
        if (FL("pupwfree", 1) == 1) B.oldW.insert(B.oldW.end(), B.wcomps.begin(), B.wcomps.end());
        B.rlAmmo = false;
        B.wcomps.clear();
        B.wstr.reset();
        B.wskel.clear();
        B.wrlPath.clear();
        B.wrlUntil = 0;
        B.det.reset();
        B.spawnAt.reset();
        B.hasBefore = false;
        B.before.clear();
        B.asks = 0;
        B.rounds.reset();
        B.gaveUp = false;
        B.retryAt = 0;
        B.adoptOnly = false;
        B.findAt = 0;
    }

    int quests_pending()
    {
        x0::Ref m = x0::first_of(L"ScenarioManager");
        auto* p = reinterpret_cast<const uint8_t*>(m.get());
        if (!p) return -1;
        if (!*reinterpret_cast<void* const*>(p + 0x580)) return 0;
        return *reinterpret_cast<const int32_t*>(p + 0x588) - *reinterpret_cast<const int32_t*>(p + 0x5b4);
    }

    bool body_gate()
    {
        St& s = S();
        if (FL("bodysafe", 1) != 1) return true;
        Gate& G = s.wg;
        double now = x0::now();
        std::optional<std::string> key;
        Ref pw = local_pawn();
        if (valid(pw)) key = lg::inum((int64_t)pw.address()) + "|" + x0::full_name(pw);
        auto drop = [&](const std::string& why) {
            bool had = false;
            for (Pl& p : s.pl)
            {
                Body& P = p.pup;
                Body& T = p.tgt;
                Far& F = p.far;
                had = had || has(P.pawn) || has(T.pawn) || P.spawnAt || T.spawnAt || has(F.actor);
                drop_body(P);
                drop_body(T);
                F.actor.reset();
                F.hidden.reset();
            }
            LOG("NOLA corpos: " + why + " -> referencias de boneco/alvo largadas sem tocar (havia=" + tf(had) + "); corpos parados");
        };
        if (key != G.key)
        {
            if (G.key) drop(std::string("jogador local ") + (key ? "trocado" : "sumiu") + " (load?)");
            G.key = key;
            if (key) G.since = now;
            else G.since.reset();
            G.lp.reset();
            lg::hold_arm(G.hold, key.has_value() && FL("qhold", 1) == 1, now);
        }
        double tp = FL("tpdrop", 5000);
        if (key && tp > 0)
        {
            Vec3 q;
            if (loc(pw, q))
            {
                if (G.lp)
                {
                    double dx = q.x - G.lp->first, dy = q.y - G.lp->second;
                    if (dx * dx + dy * dy > tp * tp)
                    {
                        drop(fmt("jogador local saltou %.0f m (teleporte)", std::sqrt(dx * dx + dy * dy) / 100));
                        G.since = now;
                    }
                }
                G.lp = std::make_pair(q.x, q.y);
            }
        }
        bool stable = key.has_value() && now - G.since.value_or(now) >= FL("bodywait", 5);
        if (G.hold.held)
        {
            std::string m = lg::hold_step(G.hold, FL("qhold", 1) == 1, stable, stable ? quests_pending() : 0, now, FL("qholdmax", 300));
            if (!m.empty()) LOG(m);
        }
        bool open = stable && !G.hold.held;
        if (open && !G.open) LOG(fmt("NOLA corpos: jogador local estavel ha %d s -> corpos liberados", (int)FL("bodywait", 5)));
        G.open = open;
        return open;
    }

    int pup_num(const Ref& o)
    {
        std::string n = x0::obj_name(o);
        size_t k = n.size();
        while (k > 0 && n[k - 1] >= '0' && n[k - 1] <= '9') k--;
        if (k == n.size()) return 0;
        return std::atoi(n.c_str() + k);
    }
    bool max_hp(const Ref& o, double& mh)
    {
        float f = 0;
        if (!fcall(o, L"GetMaxHP", f)) return false;
        mh = f;
        return true;
    }
    Ref pup_find(Body& P, const Vec3& lp, double lo, double hi)
    {
        Ref best;
        bool bNew = false;
        int bNum = 0;
        double r = FL("pupadoptr", 5000);
        for (const Ref& o : x0::all_of(L"Pawn"))
        {
            if (!valid(o) || o.same(P.pawn) || is_body(o)) continue;
            std::string full = x0::full_name(o);
            if (full.find("BP_AI_Human_C") == std::string::npos) continue;
            double mh;
            if (!max_hp(o, mh) || mh < lo || mh > hi) continue;
            bool alive = false;
            if (!bcall(o, L"IsAlive", alive) || !alive) continue;
            if (P.bad.count(strip_full(full))) continue;
            if (!valid(pref(o, "Mesh"))) continue;
            Vec3 q;
            if (!loc(o, q)) continue;
            if ((q.x - lp.x) * (q.x - lp.x) + (q.y - lp.y) * (q.y - lp.y) > r * r) continue;
            bool isNew = !(P.hasBefore && P.before.count(full));
            int n = pup_num(o);
            if (!has(best) || (isNew && !bNew) || (isNew == bNew && n < bNum)) { best = o; bNew = isNew; bNum = n; }
        }
        return best;
    }

    void pup_spawn(Body& P, const lg::RSnap& r, const Vec3& lp, double now, const char* sid, double lo, double hi, const char* tag)
    {
        bool isPup = std::string(tag) == "puppet";
        bool adopt = isPup && FL("pupadopt", 1) == 1;
        if (P.spawnAt)
        {
            if (now < *P.spawnAt) return;
            P.spawnAt = now + 1;
            Ref best;
            bool bdSet = false;
            double bd = 0;
            if (adopt) { best = pup_find(P, lp, lo, hi); bdSet = true; bd = 0; }
            if (!bdSet)
            {
                for (const Ref& o : x0::all_of(L"Pawn"))
                {
                    if (!valid(o) || is_body(o)) continue;
                    std::string full = x0::full_name(o);
                    if (P.before.count(full) || full.find("BP_AI_Human_C") == std::string::npos) continue;
                    double mh;
                    if (!max_hp(o, mh) || mh < lo || mh > hi) continue;
                    Vec3 q;
                    if (!loc(o, q)) continue;
                    double d = (q.x - P.sx) * (q.x - P.sx) + (q.y - P.sy) * (q.y - P.sy);
                    if (d < 800.0 * 800.0 && (!bdSet || d < bd)) { best = o; bd = d; bdSet = true; }
                }
            }
            if (!has(best) || !valid(best))
            {
                if (P.adoptOnly) { P.adoptOnly = false; P.spawnAt.reset(); P.hasBefore = false; P.before.clear(); return; }
                double win = adopt ? FL("pupwin", 60) : 20;
                if (now - P.askedAt.value_or(now) < win) return;
                P.spawnAt.reset();
                P.hasBefore = false;
                P.before.clear();
                P.retryAt = now + 10;
                LOG(std::string("NOLA ") + tag + ": nenhum pawn novo em " + (adopt ? FS("pupwin") : std::string("20")) + " s (pedidos=" + lg::inum(P.asks) + ")");
                return;
            }
            bool adoptOnly = P.adoptOnly;
            std::optional<double> askedAt = P.askedAt;
            P.adoptOnly = false;
            P.spawnAt.reset();
            P.hasBefore = false;
            P.before.clear();
            P.asks = 0;
            P.gaveUp = false;
            P.rounds.reset();
            std::string full = x0::full_name(best);
            P.path = strip_full(full);
            P.pawn = best;
            P.state.reset();
            P.hidden = false;
            P.pos = lg::PupPos{};
            P.yaw.reset();
            P.wstr.reset();
            P.det.reset();
            int freed = 0;
            if (FL("pupwfree", 1) == 1) P.oldW.insert(P.oldW.end(), P.wcomps.begin(), P.wcomps.end());
            P.rlAmmo = false;
            for (const Ref& c : P.oldW)
                if (valid(c) && ocall(c, L"GetOwner").same(best)) { Call(c, L"K2_DestroyComponent").o(L"Object", c).run(); freed++; }
            if (freed) LOG("NOLA puppet: armas velhas do mesmo corpo removidas=" + lg::inum(freed));
            P.oldW.clear();
            P.wcomps.clear();
            P.wskel.clear();
            P.wrlUntil = 0;
            P.ref.reset();
            P.uid = guid_of(best);
            P.pendSeen.clear();
            if (!isPup) hide_actor(best, true);
            if (isPup)
            {
                hide_actor(best, false);
                bool okC = no_collision(best);
                LOG(std::string("NOLA puppet: colisao desligada ok=") + tf(okC));
            }
            double mh = 0;
            std::string mhs = max_hp(best, mh) ? num(mh) : std::string("nil");
            std::string extra;
            if (adopt) extra = fmt(" (%s %.1f s)", adoptOnly ? "corpo ja existente, sem pedido novo;" : "depois do pedido:", now - askedAt.value_or(now));
            bool col = false;
            std::string cols = bcall(best, L"GetActorEnableCollision", col) ? tf(col) : "nil";
            LOG(std::string("NOLA ") + tag + ": adotado " + leaf_dot(full) + " uid=" + ostr(P.uid) + " maxhp=" + mhs + " colisao=" + cols + extra);
            return;
        }
        int ses = x0::peer(g_cv).sessions;
        if (FL("pupretry", 1) == 1 && (!P.ses || *P.ses != ses))
        {
            if (P.ses && (P.asks > 0 || P.rounds || P.gaveUp)) LOG(std::string("NOLA ") + tag + ": nova sessao do outro lado; pedidos de spawn zerados");
            P.ses = ses;
            P.asks = 0;
            P.rounds.reset();
            P.gaveUp = false;
            P.retryAt = 0;
        }
        if (other_spawning()) return;
        if (adopt && now >= P.findAt)
        {
            P.findAt = now + 1;
            if (has(pup_find(P, lp, lo, hi)))
            {
                P.hasBefore = true;
                P.before.clear();
                P.hasSx = true;
                P.sx = lp.x;
                P.sy = lp.y;
                P.hpLo = lo;
                P.hpHi = hi;
                P.askedAt = now;
                P.spawnAt = now;
                P.adoptOnly = true;
                return;
            }
        }
        if (now < P.retryAt) return;
        if (FL("pupretry", 1) == 1 && P.asks >= 4)
        {
            P.rounds = P.rounds.value_or(0) + 1;
            double w = FL("pupback", 60) * (*P.rounds > 1 ? 2 : 1);
            P.asks = 0;
            P.gaveUp = false;
            P.retryAt = now + w;
            LOG(fmt("NOLA %s: 4 pedidos de spawn sem sucesso; nova rodada em %d s (rodada %d)", tag, (int)w, *P.rounds));
            return;
        }
        if (P.asks >= 4)
        {
            if (!P.gaveUp)
            {
                P.gaveUp = true;
                LOG(std::string("NOLA ") + tag + ": 4 pedidos de spawn sem sucesso; parei (S2.state." + (isPup ? "pup" : "tgt") + ".asks = 0 para tentar de novo)");
            }
            return;
        }
        double md = FL("maxdist", 7000);
        if (!P.force && md > 0 && (r.x - lp.x) * (r.x - lp.x) + (r.y - lp.y) * (r.y - lp.y) > md * md) return;
        if (loot_pending()) return;
        if (FL("bodysafe", 1) == 1 && now - P.lastSpawn.value_or(-1e9) < FL("spawngap", 10)) return;
        double sx = r.x, sy = r.y, sz = r.z;
        if ((sx - lp.x) * (sx - lp.x) + (sy - lp.y) * (sy - lp.y) > 3000.0 * 3000.0) { sx = lp.x + 300; sy = lp.y; sz = lp.z; }
        P.hasBefore = true;
        P.before.clear();
        for (const Ref& o : x0::all_of(L"Pawn"))
            if (valid(o)) P.before.insert(x0::full_name(o));
        P.hasSx = true;
        P.sx = sx;
        P.sy = sy;
        P.hpLo = lo;
        P.hpHi = hi;
        P.lastSpawn = now;
        P.adoptOnly = false;
        Ref rk = x0::first_of(L"CustomConsoleManagerRK");
        bool ok = valid(rk) && Call(rk, L"XSpawnObjBySID").s(L"PrototypeSID", W(sid)).i(L"Rank", 1).b(L"bDestroyOnOffline", false)
                                   .f(L"Px", (float)sx).f(L"Py", (float)sy).f(L"Pz", (float)(sz + 50)).f(L"Pitch", 0.f).f(L"Yaw", 0.f).f(L"Roll", 0.f).run();
        LOG(std::string("NOLA ") + tag + ": XSpawnObjBySID " + sid + " ok=" + tf(ok) + " " + (ok ? "" : "chamada falhou"));
        P.spawnAt = now + 1.5;
        P.askedAt = now;
        P.asks++;
        P.gaveUp = false;
    }

    std::wstring far_file() { return x0::bridge_file(L"farlook.txt"); }
    bool far_capture(const Ref& pawn)
    {
        St& s = S();
        std::vector<LookE> list;
        for (const Ref& c : comps(pawn, L"/Script/Engine.SkeletalMeshComponent"))
        {
            if (!valid(c)) continue;
            Ref a = ocall(c, L"GetSkeletalMeshAsset");
            if (!valid(a)) continue;
            LookE e;
            for (int i = 0; i <= 15; i++)
            {
                Call g(c, L"GetMaterial");
                g.i(L"ElementIndex", i);
                if (!g.run()) break;
                Ref m = g.ro();
                if (!valid(m)) break;
                e.mats.push_back(sfull(m));
            }
            e.path = sfull(a);
            e.main = x0::obj_name(c) == "CharacterMesh0";
            list.push_back(e);
        }
        if (list.empty()) return false;
        std::stable_sort(list.begin(), list.end(), [](const LookE& x, const LookE& y) { return x.main && !y.main; });
        std::string text;
        for (size_t i = 0; i < list.size(); i++)
        {
            if (i) text += "\n";
            text += list[i].path + "|";
            for (size_t j = 0; j < list[i].mats.size(); j++) text += (j ? "," : "") + list[i].mats[j];
        }
        x0::write_text(far_file(), text, false);
        LOG(fmt("NOLA farbody: aparencia capturada do boneco: %d malhas", (int)list.size()));
        s.farLook = list;
        s.hasFarLook = true;
        return true;
    }
    bool far_load()
    {
        St& s = S();
        std::string t;
        if (!x0::read_text(far_file(), t)) return false;
        std::vector<LookE> list;
        for (std::string ln : x0::split(t, '\n', true))
        {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (ln.empty() || ln[0] == '|') continue;
            size_t k = ln.find('|');
            LookE e;
            e.path = ln.substr(0, k);
            if (k != std::string::npos) e.mats = lg::split_nonempty(ln.substr(k + 1), ',');
            list.push_back(e);
        }
        if (list.empty()) return false;
        s.farLook = list;
        s.hasFarLook = true;
        return true;
    }
    bool far_build(Far& F, const Vec3& pos)
    {
        St& s = S();
        uint64_t p = s2x_spawn_actor(L"/Script/Engine.Actor", pos.x, pos.y, pos.z, 0, 0, 0);
        Ref a = p ? Ref::addr(p) : Ref{};
        if (!valid(a)) return false;
        Ref main;
        for (const LookE& e : s.farLook)
        {
            Ref sk = asset(e.path);
            if (!valid(sk)) continue;
            Ref c = add_comp(a, L"/Script/Engine.SkeletalMeshComponent", false);
            if (!valid(c)) continue;
            Call(c, L"SetSkinnedAssetAndUpdate").o(L"NewMesh", sk).b(L"bReinitPose", true).run();
            for (size_t mi = 0; mi < e.mats.size(); mi++)
            {
                Ref m = asset(e.mats[mi]);
                if (valid(m)) Call(c, L"SetMaterial").i(L"ElementIndex", (int64_t)mi).o(L"Material", m).run();
            }
            Call(c, L"SetCollisionEnabled").i(L"NewType", 0).run();
            x0::prop_set<uint8_t>(c, "VisibilityBasedAnimTickOption", (uint8_t)0);
            if (!has(main)) main = c;
            else Call(c, L"SetLeaderPoseComponent").o(L"NewLeaderBoneComponent", main).b(L"bForceUpdate", true).b(L"bInFollowerShouldTickPose", true).run();
        }
        if (!has(main)) { destroy_actor(a); return false; }
        F.actor = a;
        F.main = main;
        F.state.reset();
        F.fp.has = false;
        LOG(fmt("NOLA farbody: corpo distante criado (%d malhas)", (int)s.farLook.size()));
        return true;
    }
    void far_tick(Far& F, const lg::RSnap& r, const Vec3& lp, double now)
    {
        St& g = S();
        Pl& s = V();
        Body& P = s.pup;
        if (body_ok(P))
        {
            if (!g.hasFarLook) far_capture(P.pawn);
            if (!P.hidden) { P.hidden = true; hide_actor(P.pawn, true); }
            P.pawn.reset();
            P.path.reset();
            P.state.reset();
            P.hidden = false;
            Body& T = s.tgt;
            T.pawn.reset();
            T.path.reset();
            T.setup = false;
            LOG("NOLA farbody: referencias do boneco e do alvo largadas (corpo escondido)");
        }
        if (!g.hasFarLook)
        {
            if (far_load()) LOG("NOLA farbody: aparencia lida de farlook.txt");
        }
        if (!g.hasFarLook)
        {
            if (!body_ok(P) && FL("avatar", 1) == 1 && now - r.recv < 5)
            {
                P.force = true;
                pup_spawn(P, r, lp, now, "Nola_RemotePlayer", 90000, 1e9, "puppet");
            }
            return;
        }
        double age = now - r.recv;
        auto life = remote_life();
        bool away = age > 5 || FL("avatar", 1) != 1 || (life && *life == "d") || remote_cine();
        if (!valid(F.actor))
        {
            F.actor.reset();
            if (away || now < F.retryAt) return;
            F.retryAt = now + 5;
            if (!far_build(F, Vec3{r.x, r.y, r.z - 90})) return;
        }
        if (away != F.hidden.value_or(false)) { F.hidden = away; hide_actor(F.actor, away); }
        if (away) return;
        auto v = lg::r_vel(C, s.rvel, r, now);
        lg::FarOut o = lg::far_step(C, F.fp, now, r.recv, r.x, r.y, r.z, r.yaw, v.first, v.second);
        if (!set_loc(F.actor, Vec3{o.x, o.y, o.z})) return;
        set_yaw(F.actor, o.yaw - 90);
        std::string st = lg::speed_state(o.speed);
        if (!F.state || *F.state != st)
        {
            Ref q = seq_asset(st, lg::pup_anim(st));
            if (valid(q) && valid(F.main) && Call(F.main, L"PlayAnimation").o(L"NewAnimToPlay", q).b(L"bLooping", true).run()) F.state = st;
        }
    }

    Ns& ns_st()
    {
        Ns& N = S().ns;
        if (!N.made) { N.made = true; N.s = lg::ns_s0(x0::now()); }
        return N;
    }
    void ns_stop(GM& g)
    {
        if (valid(g.pmc)) x0::prop_set<Vec3>(g.pmc, "Velocity", Vec3{0, 0, 0});
        g.k.stopped = true;
    }
    GM* ns_mover(Ns& N, const Ref& a, const std::string& key, bool& made_new)
    {
        made_new = false;
        uint64_t addr = valid(a) ? a.address() : 0;
        auto it = N.k.find(key);
        if (it != N.k.end() && it->second.addr == addr && valid(it->second.pmc))
        {
            it->second.a = a;
            return &it->second;
        }
        Ref cls = x0::find_path(L"/Script/Engine.ProjectileMovementComponent");
        if (!valid(cls)) { N.s.fail++; return nullptr; }
        Ref pmc;
        bool made = false;
        {
            Call g(a, L"GetComponentByClass");
            g.o(L"ComponentClass", cls);
            if (g.run()) pmc = g.ro();
        }
        if (!valid(pmc)) { pmc = add_comp(a, L"/Script/Engine.ProjectileMovementComponent", false); made = true; }
        if (!valid(pmc)) { N.s.fail++; return nullptr; }
        std::vector<std::string> bad;
        if (!x0::prop_set<float>(pmc, "ProjectileGravityScale", 0.f)) bad.push_back("ProjectileGravityScale");
        if (!x0::prop_set<bool>(pmc, "bSweepCollision", false)) bad.push_back("bSweepCollision");
        if (!x0::prop_set<bool>(pmc, "bShouldBounce", false)) bad.push_back("bShouldBounce");
        if (!x0::prop_set<bool>(pmc, "bRotationFollowsVelocity", false)) bad.push_back("bRotationFollowsVelocity");
        if (!x0::prop_set<bool>(pmc, "bInitialVelocityInLocalSpace", false)) bad.push_back("bInitialVelocityInLocalSpace");
        if (!x0::prop_set<float>(pmc, "MaxSpeed", 0.f)) bad.push_back("MaxSpeed");
        if (!x0::prop_set<bool>(pmc, "bSimulationEnabled", true)) bad.push_back("bSimulationEnabled");
        x0::prop_set<Vec3>(pmc, "Velocity", Vec3{0, 0, 0});
        bool act = true;
        if (bcall(pmc, L"IsActive", act) && !act) Call(pmc, L"Activate").b(L"bReset", true).run();
        Ref u;
        bool upd = false;
        if (!(x0::prop<Ref>(pmc, "UpdatedComponent", u) && valid(u)))
        {
            Ref root = ocall(a, L"K2_GetRootComponent");
            Call(pmc, L"SetUpdatedComponent").o(L"NewUpdatedComponent", root).run();
            u = Ref{};
            x0::prop<Ref>(pmc, "UpdatedComponent", u);
        }
        upd = valid(u);
        GM g;
        g.addr = addr;
        g.a = a;
        g.pmc = pmc;
        g.k.st = x0::now();
        N.k[key] = g;
        if (made) N.s.mk++;
        N.logMk++;
        if (N.logMk <= 10 || N.logMk % 100 == 0)
        {
            std::string cn = valid(a) ? x0::obj_name(a) : std::string("?");
            std::string b;
            if (!bad.empty())
            {
                b = " falhas=";
                for (size_t i = 0; i < bad.size(); i++) b += (i ? "," : "") + bad[i];
            }
            LOG(fmt("NOLA nsmooth: movedor %s em %s (%s) UpdatedComponent=%s yaw=%s%s", made ? "criado" : "reusado", key.c_str(), cn.c_str(), tf(upd), "10 Hz", b.c_str()));
        }
        made_new = true;
        return &N.k[key];
    }
    bool ns_drive(Ns& N, GM& g, const Ref& a, double now)
    {
        if (!g.k.has) return false;
        Vec3 A;
        if (!loc(a, A)) return false;
        std::optional<double> cur;
        if (g.k.yaw)
        {
            Rot R;
            if (rot(a, R)) cur = R.yaw;
        }
        lg::DriveOut o = lg::ns_drive(C, V().nb, N.s, g.k, A.x, A.y, A.z, cur, now);
        if (!o.ok) return false;
        if (o.snap) set_loc(a, Vec3{o.sx, o.sy, o.sz});
        if (!x0::prop_set<Vec3>(g.pmc, "Velocity", Vec3{o.vx, o.vy, o.vz})) return false;
        if (o.rot) set_yaw(a, o.yaw);
        return true;
    }

    double pf_z(Body& P, const lg::RSnap& r)
    {
        std::optional<double> own;
        Ref cap = pref(P.pawn, "CapsuleComponent");
        float h = 0;
        if (valid(cap) && fcall(cap, L"GetScaledCapsuleHalfHeight", h)) own = h;
        return lg::pf_z(C, r.z, rpart("FH"), own, P.pfH, V().pf);
    }
    std::optional<double> pa_yaw(Body& P, const lg::RSnap& r, double speed, double now)
    {
        double mv = FL("pupmove", 0);
        if (FL("pupanim", 0) != 1 || FL("pupanimbp", 0) != 1 || (mv != 2 && mv != 3)) { P.yh = lg::YawHold{}; return std::nullopt; }
        if (P.paFace == "look" && r.yaw) return lg::yaw_hold(C, P.yh, speed < 30 && lg::pa_still(P.paSt), r.yaw, P.yaw, now);
        P.yh = lg::YawHold{};
        return std::nullopt;
    }

    bool ns_pup(Body& P, const lg::RSnap* rp, double now)
    {
        Pl& s = V();
        P.nsYaw = false;
        if (FL("nsmooth", 0) != 1) return false;
        if (!(rp && valid(P.pawn))) return false;
        const lg::RSnap& r = *rp;
        Ns& N = ns_st();
        bool made = false;
        GM* g = ns_mover(N, P.pawn, pup_key(g_cv), made);
        if (!g) return false;
        auto v = lg::r_vel(C, s.rvel, r, now);
        if (made || !g->k.rt || *g->k.rt != r.recv)
        {
            g->k.rt = r.recv;
            N.s.smp++;
            lg::ns_sample(C, g->k, r.x + FL("avoffx", 0), r.y, pf_z(P, r), std::nullopt, r.t, r.recv, v.first, v.second, 0.0, false);
        }
        g->k.yaw.reset();
        g->k.av = true;
        P.hbAt = now;
        bool ok;
        if (FL("avframe", 0) == 1 && g->k.at && now - *g->k.at <= 0.1 && !g->k.stopped) ok = true;
        else ok = ns_drive(N, *g, P.pawn, now);
        if (ok) N.s.pup++;
        P.nsYaw = false;
        return ok;
    }

    void cm_off(Body& P, double now)
    {
        St& g = S();
        Pl& s = V();
        bool want = FL("pupcmoff", 0) == 1 && !pm_vel();
        if (!valid(P.pawn)) return;
        uint64_t addr = P.pawn.address();
        if (s.hasCmo && s.cmo.addr != addr) s.hasCmo = false;
        if (!want)
        {
            if (s.hasCmo)
            {
                Ref cm = pref(P.pawn, "CharacterMovement");
                bool ok = valid(cm) && Call(cm, L"SetComponentTickEnabled").b(L"bEnabled", true).run();
                LOG(std::string("NOLA pupcmoff: desligado -> tick do CharacterMovement devolvido ok=") + tf(ok));
                s.hasCmo = false;
            }
            return;
        }
        Ref cm = pref(P.pawn, "CharacterMovement");
        if (!valid(cm)) return;
        bool on = true;
        bcall(cm, L"IsComponentTickEnabled", on);
        if (s.hasCmo) { s.cmo.a = P.pawn; s.cmo.at = now; }
        if (s.hasCmo && !on) return;
        x0::call0(cm, L"StopMovementImmediately");
        bool ok = Call(cm, L"SetComponentTickEnabled").b(L"bEnabled", false).run();
        if (!s.hasCmo)
        {
            s.hasCmo = true;
            s.cmo = Cmo{addr, P.pawn, now, 0};
            std::string cn = x0::class_name(cm);
            g.cmoN++;
            LOG(fmt("NOLA pupcmoff: boneco #%d: %s StopMovementImmediately + tick desligado ok=%s (tick antes=%s)", g.cmoN, cn.c_str(), tf(ok), tf(on)));
        }
        else
        {
            s.cmo.back++;
            g.cmoBack++;
            if (s.cmo.back <= 5 || s.cmo.back % 50 == 0)
                LOG(fmt("NOLA pupcmoff: o jogo religou o tick do CharacterMovement (vez %d, total %d); desligado de novo ok=%s", s.cmo.back, g.cmoBack, tf(ok)));
        }
    }

    void pup_uro(Body& P)
    {
        bool want = FL("avuro", 1) == 1;
        bool have = P.uroFor.same(P.pawn);
        if (want == have) return;
        Ref m = pref(P.pawn, "Mesh");
        if (!valid(m)) return;
        if (want)
        {
            P.uroBool = true;
            P.uroVis = 1;
            x0::prop(m, "bEnableUpdateRateOptimizations", P.uroBool);
            x0::prop(m, "VisibilityBasedAnimTickOption", P.uroVis);
            P.uroFor = P.pawn;
            bool a = x0::prop_set<bool>(m, "bEnableUpdateRateOptimizations", false);
            bool b = x0::prop_set<uint8_t>(m, "VisibilityBasedAnimTickOption", (uint8_t)0);
            LOG(fmt("NOLA puppet: malha anima todo quadro (URO %d->0 ok=%d, vis %d->0 ok=%d)", (int)P.uroBool, (int)a, (int)P.uroVis, (int)b));
        }
        else
        {
            x0::prop_set<bool>(m, "bEnableUpdateRateOptimizations", P.uroBool);
            x0::prop_set<uint8_t>(m, "VisibilityBasedAnimTickOption", P.uroVis);
            P.uroFor.reset();
            LOG(fmt("NOLA puppet: malha devolvida (URO=%d vis=%d)", (int)P.uroBool, (int)P.uroVis));
        }
    }

    void pup_anim(Body& P)
    {
        if (FL("pupanimbp", 0) != 1) return;
        if (!P.abpFor.same(P.pawn) && !P.state && FL("pupnoreinit", 0) == 1 && human_anim(P.pawn))
        {
            P.abpFor = P.pawn;
            LOG("NOLA puppet: AnimBP ja ativo (pupnoreinit) -> sem SetAnimationMode");
        }
        if (!P.abpFor.same(P.pawn) || P.state)
        {
            LOG(std::string("NOLA puppet: AnimBP do jogo mantido (b112: sem SetAnimationMode) humano=") + tf(human_anim(P.pawn)) + " estava=" + (P.state ? *P.state : std::string("nil")));
            P.abpFor = P.pawn;
            P.state.reset();
        }
        if (FL("pupabpstill", 0) == 1 && !pm_vel())
        {
            Ref cm = pref(P.pawn, "CharacterMovement");
            uint8_t mm = 0;
            if (valid(cm) && x0::prop<uint8_t>(cm, "MovementMode", mm) && mm != 0) x0::call0(cm, L"DisableMovement");
        }
    }

    void pm_clip_stop(Pm& Q, const Ref& ai)
    {
        if (valid(ai))
        {
            std::wstring slot = W(x0::fs("pupmvslot", "DefaultSlot"));
            Call(ai, L"StopSlotAnimation").f(L"InBlendOutTime", (float)FL("pablendout", 0.25)).n(L"SlotNodeName", slot.c_str()).run();
        }
        Q.clip.reset();
        Q.mon.reset();
        Q.clipAI = 0;
    }
    void pm_off(Body& P)
    {
        Pm Q = P.pm;
        P.pm = Pm{};
        if (!(Q.on && Q.pawn.same(P.pawn) && valid(P.pawn))) return;
        Ref ai = pm_ai(P);
        std::vector<std::string> r;
        if (Q.clip) { pm_clip_stop(Q, ai); r.push_back("clipe parado"); }
        if (Q.rm > 0 && valid(ai) && Q.rm0)
        {
            bool ok = x0::prop_set<uint8_t>(ai, "RootMotionMode", (uint8_t)*Q.rm0);
            r.push_back("RootMotionMode=" + lg::inum(*Q.rm0) + " ok=" + tf(ok));
        }
        std::string j;
        for (size_t i = 0; i < r.size(); i++) j += (i ? " " : "") + r[i];
        LOG("NOLA pupmove: desligado -> boneco devolvido (" + j + ")");
    }

    Ref pa_asset(const std::string& rel);
    Ref seq_asset(const std::string& full)
    {
        auto it = g_seqs.find(full);
        if (it != g_seqs.end() && valid(it->second)) return it->second;
        if (g_paMiss.count(full)) return Ref{};
        Ref q = asset(full);
        if (valid(q)) g_seqs[full] = q;
        else { g_paMiss.insert(full); LOG("NOLA pupanim: clipe nao carregou " + full); }
        return q;
    }
    bool wrl_play(Body& P, double at)
    {
        if (P.wcomps.empty() || !valid(P.wcomps[0]) || !x0::is_a(P.wcomps[0], L"SkeletalMeshComponent")) return false;
        Ref wq = seq_asset(P.wrlPath);
        if (!valid(wq)) return false;
        if (FL("pwrlgun", 1) != 1) return true;
        Call pc(P.wcomps[0], L"PlayAnimation");
        pc.o(L"NewAnimToPlay", wq).b(L"bLooping", false);
        bool ok = pc.run();
        if (ok) Call(P.wcomps[0], L"SetPlayRate").f(L"Rate", 1.0f).run();
        if (ok && at > 0.05) Call(P.wcomps[0], L"SetPosition").f(L"InPos", (float)at).b(L"bFireNotifies", false).run();
        return ok;
    }
    bool wsh_gun_play(Body& P, const char* path, double rate)
    {
        if (P.wcomps.empty() || !valid(P.wcomps[0]) || !x0::is_a(P.wcomps[0], L"SkeletalMeshComponent")) return false;
        Ref wq = seq_asset(path);
        if (!valid(wq)) return false;
        Call pc(P.wcomps[0], L"PlayAnimation");
        pc.o(L"NewAnimToPlay", wq).b(L"bLooping", false);
        bool ok = pc.run();
        if (ok && std::abs(rate - 1.0) > 1e-3) Call(P.wcomps[0], L"SetPlayRate").f(L"Rate", (float)rate).run();
        return ok;
    }
    void wrl_start(Body& P, const lg::WrlEnt& e, const std::string& tok, const std::string& fam)
    {
        double now = x0::now();
        P.wrlPath = e.wpn;
        P.wrlT0 = now;
        P.wrlUntil = now + e.len / (e.rate > 0 ? e.rate : 1.0);
        bool ok = wrl_play(P, 0);
        if (!ok) P.wrlUntil = 0;
        P.wrlAdd = lg::wrl_chr(C, &e) != nullptr || lg::wrl_ms(C, &e, fam) != nullptr;
        P.rdN = 0;
        LOG(fmt("NOLA pupanim: recarga da arma %s (esqueleto %s) -> %s por %.2f s ok=%s", tok.c_str(), e.skel, lg::leaf_of(e.wpn, '.').c_str(), e.len, tf(ok)));
    }

    Ref pa_asset(const std::string& rel)
    {
        std::string full = lg::pa_path(rel);
        auto it = g_seqs.find(full);
        if (it != g_seqs.end() && valid(it->second)) return it->second;
        if (g_paMiss.count(full)) return Ref{};
        Ref q = asset(full);
        if (valid(q)) g_seqs[full] = q;
        else { g_paMiss.insert(full); LOG("NOLA pupanim: clipe nao carregou " + full); }
        return q;
    }
    std::string ostrs(const std::optional<std::string>& s) { return s ? *s : std::string("nil"); }

    void pa_overlay(Pm& Q, const Ref& ai, const std::vector<lg::PaOv>& want)
    {
        std::set<std::string> keep;
        for (const auto& w : want)
        {
            keep.insert(w.slot);
            Pm::Ov& o = Q.ov[w.slot];
            std::string key = w.rel + "|" + w.prof;
            if (o.bad >= 5 || (o.key == key && o.ai == ai.address() && valid(o.mon))) continue;
            Ref q = pa_asset(w.rel);
            Ref bp = asset("/Game/_STALKER2/SkeletalMeshes/SK_stalker_Skeleton.SK_stalker_Skeleton:" + w.prof);
            if (!valid(q) || !valid(bp)) { o.bad = 5; LOG(fmt("NOLA pupanim: camada %s sem clipe ou perfil %s (clipe=%s perfil=%s)", w.slot.c_str(), w.prof.c_str(), tf(valid(q)), tf(valid(bp)))); continue; }
            uint8_t in[0x20] = {}, out[0x20] = {};
            uint64_t bpa = bp.address();
            float bt = (float)FL("padetovbt", 0.25);
            std::memcpy(in, &bpa, 8);
            std::memcpy(in + 0x10, &bt, 4);
            std::memcpy(out, &bpa, 8);
            std::memcpy(out + 0x10, &bt, 4);
            std::wstring ws = W(w.slot);
            Call c(ai, L"PlaySlotAnimationAsDynamicMontage_WithBlendSettings");
            c.o(L"Asset", q).n(L"SlotNodeName", ws.c_str()).raw(L"BlendInSettings", in, 0x20).raw(L"BlendOutSettings", out, 0x20).f(L"InPlayRate", 1.0f)
                .i(L"LoopCount", 100000).f(L"BlendOutTriggerTime", -1.0f).f(L"InTimeToStartMontageAt", 0.0f);
            bool ok = c.run();
            Ref mon = ok ? c.ro() : Ref{};
            if (!(ok && valid(mon))) o.bad++;
            o.key = key;
            o.mon = mon;
            o.ai = ai.address();
            LOG(fmt("NOLA pupanim: camada %s -> %s mascara %s ok=%s montagem=%s", w.slot.c_str(), lg::leaf_of(w.rel, '/').c_str(), w.prof.c_str(), tf(ok), tf(valid(mon))));
        }
        for (auto it = Q.ov.begin(); it != Q.ov.end();)
        {
            if (keep.count(it->first)) { ++it; continue; }
            if (valid(ai) && !it->second.key.empty())
            {
                std::wstring ws = W(it->first);
                Call(ai, L"StopSlotAnimation").f(L"InBlendOutTime", (float)FL("padetovbt", 0.25)).n(L"SlotNodeName", ws.c_str()).run();
                LOG("NOLA pupanim: camada " + it->first + " parada");
            }
            it = Q.ov.erase(it);
        }
    }

    namespace ro = m01::ro;
    struct RoArr
    {
        ro::Tf* p;
        int32_t n, max;
    };
    struct RoHook
    {
        std::mutex mu;
        uint64_t mesh = 0;
        int n = 0;
        uint64_t at = 0;
        std::vector<int> par;
        std::vector<ro::Tgt> tg;
        bool capReq = false, capDone = false;
        ro::Pick capPick;
        std::vector<ro::Loc> cap;
        std::atomic<uint64_t> hits{0}, done{0};
    };
    RoHook& roh()
    {
        static RoHook h;
        return h;
    }
    constexpr uintptr_t kFinBone = 0x96a08;
    const uint8_t kFinBoneSig[] = {0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0xe8};
    uint64_t g_finTramp = 0;
    x0::hot::Detour* g_finDet = nullptr;
    bool g_finTried = false;
    void ro_post(uint8_t* m);
    void ln_post(uint8_t* m);
    void ab_post(uint8_t* m);
    bool fin_install();
    bool fin_sig_ok(const uint8_t* p)
    {
        __try { return std::memcmp(p, kFinBoneSig, sizeof kFinBoneSig) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    std::atomic<uint64_t> g_abSrc{0}, g_abSrcCalls{0};
    void fin_detour(uint8_t* m)
    {
        if ((uint64_t)m == g_abSrc.load(std::memory_order_relaxed)) g_abSrcCalls.fetch_add(1, std::memory_order_relaxed);
        bool flip = (m[0x771] & 2) != 0;
        reinterpret_cast<void (*)(uint8_t*)>(g_finTramp)(m);
        if (!flip) return;
        ro_post(m);
        ln_post(m);
        ab_post(m);
    }
    bool fin_install()
    {
        if (g_finDet) return true;
        if (g_finTried) return false;
        g_finTried = true;
        uint8_t* b = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        if (!fin_sig_ok(b + kFinBone))
        {
            LOG("NOLA pupanim: SkeletalMeshComponent::FinalizeBoneTransform exe+0x96a08 com bytes inesperados: pose do braco e inclinacao NAO instaladas");
            return false;
        }
        g_finDet = new x0::hot::Detour((uint64_t)(b + kFinBone), (uint64_t)&fin_detour, &g_finTramp);
        if (!g_finDet->hook())
        {
            delete g_finDet;
            g_finDet = nullptr;
            g_finTramp = 0;
            LOG("NOLA pupanim: desvio de FinalizeBoneTransform exe+0x96a08 falhou: pose do braco e inclinacao NAO instaladas");
            return false;
        }
        LOG("NOLA pupanim: FinalizeBoneTransform exe+0x96a08 desviado (pose do braco por item + inclinacao do tronco)");
        return true;
    }
    struct RoSkel
    {
        uint64_t mesh = 0;
        ro::Skel k;
        bool bad = false;
    };
    RoSkel g_roSk;
    std::map<std::string, std::map<int, ro::Loc>> g_roCap;
    int g_roCapGen = 0;

    ro::Tf* ro_buf(uint8_t* m, int n)
    {
        int ri = *reinterpret_cast<int32_t*>(m + 0x5e0);
        if (ri < 0 || ri > 1) return nullptr;
        RoArr* a = reinterpret_cast<RoArr*>(m + 0x598 + ri * 0x10);
        if (!a->p || a->n != n) return nullptr;
        return a->p;
    }
    bool ro_cap_do(uint8_t* m, RoHook& h)
    {
        ro::Tf* b = ro_buf(m, h.n);
        if (!b) return false;
        h.cap = ro::capture(b, h.n, h.capPick);
        return true;
    }
    bool ro_cap_seh(uint8_t* m, RoHook& h)
    {
        __try
        {
            return ro_cap_do(m, h);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool ro_apply_seh(uint8_t* m, RoHook& h)
    {
        __try
        {
            ro::Tf* b = ro_buf(m, h.n);
            if (!b) return false;
            ro::apply(b, h.n, h.par, h.tg);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void ro_post(uint8_t* m)
    {
        RoHook& h = roh();
        if ((uint64_t)m != h.mesh) return;
        std::unique_lock<std::mutex> g(h.mu, std::try_to_lock);
        if (!g.owns_lock() || (uint64_t)m != h.mesh || GetTickCount64() - h.at > 500) return;
        if (h.capReq && !h.capDone && ro_cap_seh(m, h)) h.capDone = true;
        if (h.tg.empty()) return;
        h.hits++;
        if (ro_apply_seh(m, h)) h.done++;
    }
    const ro::Skel* ro_skel(const Ref& mesh)
    {
        uint64_t a = mesh.address();
        if (g_roSk.mesh == a) return g_roSk.bad ? nullptr : &g_roSk.k;
        g_roSk = RoSkel{};
        g_roSk.mesh = a;
        g_roSk.bad = true;
        Call cn(mesh, L"GetNumBones");
        int n = cn.run() ? (int)cn.ri() : 0;
        if (n <= 0 || n > 1024) return nullptr;
        ro::Skel k;
        k.names.resize(n);
        k.par.assign(n, -1);
        for (int i = 0; i < n; i++)
        {
            Call c(mesh, L"GetBoneName");
            c.i(L"BoneIndex", i);
            if (c.run()) k.names[i] = c.rn();
        }
        for (int i = 0; i < n; i++)
        {
            if (k.names[i].empty()) continue;
            std::wstring wn = W(k.names[i]);
            Call c(mesh, L"GetParentBone");
            c.n(L"BoneName", wn.c_str());
            std::string pn = c.run() ? c.rn() : std::string();
            if (pn.empty() || pn == "None") continue;
            k.par[i] = k.idx(pn);
        }
        int hr = ro::hand_r(k), cr = ro::clav(k, hr), hl = hr >= 0 ? k.idx(ro::mirror(k.names[hr])) : -1, cl = ro::clav(k, hl);
        int w = k.idx("jnt_r_weapon"), it = k.idx("jnt_item");
        auto nm = [&](int i) { return i >= 0 ? k.names[i] + "<" + (k.par[i] >= 0 ? k.names[k.par[i]] : std::string("-")) : std::string("?"); };
        LOG(fmt("NOLA pupanim: braco do boneco %d ossos, mao dir %s, clav dir %s, mao esq %s, clav esq %s, arma %s, item %s", n, nm(hr).c_str(), nm(cr).c_str(), nm(hl).c_str(), nm(cl).c_str(), nm(w).c_str(), nm(it).c_str()));
        if (FL("parobones", 0) == 1)
            for (int i = 0; i < n; i++) LOG(fmt("NOLA pupanim: osso %d %s pai %d", i, k.names[i].c_str(), k.par[i]));
        if (hr < 0 || cr < 0) return nullptr;
        g_roSk.k = std::move(k);
        g_roSk.bad = false;
        return &g_roSk.k;
    }
    void ro_tick(Body& P, const std::string& rh, bool detBase, const std::string& use, bool isAc)
    {
        double now = x0::now();
        double dt = P.roAt > 0 ? std::min(0.2, now - P.roAt) : 0;
        P.roAt = now;
        std::vector<std::string> want = ro::fams(C, rh, P.det.has_value(), use, isAc, detBase);
        std::string sf;
        std::string samp = FS("parosamp");
        if (ro::samp_clip(samp))
        {
            sf = ro::samp_fam(samp);
            want.clear();
        }
        P.roPt = std::find(want.begin(), want.end(), "pt") != want.end() && FL("parosock", 1) == 1;
        ro::weights(P.roA, want, dt, FL("parobt", 0.2));
        if (P.roA.empty() && sf.empty() && !g_finDet) return;
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh) || !fin_install()) return;
        const ro::Skel* sk = ro_skel(mesh);
        if (!sk) return;
        RoHook& h = roh();
        std::vector<ro::Tgt> tg;
        int cm = (int)FL("paroclav", 2);
        for (int ord = 0; ord < 2; ord++)
            for (const auto& [f, a] : P.roA)
            {
                if ((f == "pt") != (ord == 0)) continue;
                auto ci = g_roCap.find(f);
                std::vector<ro::Tgt> t = ro::targets(*sk, f, ci != g_roCap.end() ? &ci->second : nullptr, cm);
                if (ro::item_free(C, f, P.det.has_value())) ro::drop_bone(t, sk->idx("jnt_item"));
                for (auto& x : t) x.w = a;
                tg.insert(tg.end(), t.begin(), t.end());
            }
        std::string lk = ro::key(want) + "#" + std::to_string(tg.size());
        if (lk != P.roLog)
        {
            P.roLog = lk;
            LOG(fmt("NOLA pupanim: pose do braco item %s detector %s estado %s -> familias [%s] ossos %d (aplicado %llu de %llu)", rh.empty() ? "-" : rh.c_str(), tf(P.det.has_value()), use.c_str(), ro::key(want).c_str(), (int)tg.size(), (unsigned long long)h.done.load(), (unsigned long long)h.hits.load()));
        }
        std::lock_guard<std::mutex> g(h.mu);
        if (h.mesh != mesh.address()) { h.capReq = false; h.capDone = false; }
        h.mesh = mesh.address();
        h.n = (int)sk->names.size();
        h.par = sk->par;
        h.tg = std::move(tg);
        h.at = GetTickCount64();
        if (sf.empty()) { P.roSamp.clear(); h.capReq = false; return; }
        if (P.roSamp != samp) { P.roSamp = samp; P.roSampAt = now + FL("parosampt", 1.5); h.capReq = false; h.capDone = false; return; }
        if (!h.capReq && now >= P.roSampAt && P.roSampAt > 0)
        {
            h.capPick = ro::pick(*sk, sf, (int)FL("paroclav", 2));
            h.capReq = true;
            h.capDone = false;
            return;
        }
        if (h.capReq && h.capDone)
        {
            std::map<int, ro::Loc> m;
            for (size_t i = 0; i < h.capPick.bones.size() && i < h.cap.size(); i++)
            {
                int b = h.capPick.bones[i];
                const ro::Loc& l = h.cap[i];
                m[b] = l;
                LOG(fmt("NOLA pupanim: pose %s {\"%s%s\", \"%s\", {%.6f, %.6f, %.6f, %.6f}, {%.4f, %.4f, %.4f}},", samp.c_str(), sf.c_str(), b == h.capPick.rot ? "h" : "", sk->names[b].c_str(), l.q[0], l.q[1], l.q[2], l.q[3], l.t[0], l.t[1], l.t[2]));
            }
            LOG(fmt("NOLA pupanim: pose %s capturada %d ossos", samp.c_str(), (int)m.size()));
            g_roCap[sf] = std::move(m);
            g_roCapGen++;
            h.capReq = false;
            P.roSampAt = 0;
        }
    }

    void lad_hide(Body& P, bool on)
    {
        std::vector<uint64_t> now;
        if (on) for (const Ref& c : P.wcomps) if (valid(c)) now.push_back(c.address());
        if (now == P.ladHid) return;
        for (const Ref& c : P.wcomps)
        {
            if (!valid(c)) continue;
            bool was = std::find(P.ladHid.begin(), P.ladHid.end(), c.address()) != P.ladHid.end();
            bool want = std::find(now.begin(), now.end(), c.address()) != now.end();
            if (was != want) Call(c, L"SetHiddenInGame").b(L"NewHidden", want).b(L"bPropagateToChildren", true).run();
        }
        if (on != !P.ladHid.empty()) LOG(fmt("NOLA pupanim: arma %s na escada (%d)", on ? "escondida" : "de volta", (int)(on ? now.size() : P.ladHid.size())));
        P.ladHid = now;
    }
    void sh_diag(Body& P, bool start);
    double lh_clip(const char* st, const std::string& fam, double def)
    {
        const lg::PaEnt* e = lg::ac_ent(st, fam);
        Ref q = e ? pa_asset(e->rel) : Ref{};
        float len = 0;
        return valid(q) && fcall(q, L"GetPlayLength", len) && len > 0.1f ? (double)len : def;
    }
    void pa_clips(Body& P, Pm& Q, const Ref& ai, double speed, double vx, double vy, const lg::RSnap& r)
    {
        if (Q.clipBad >= 5) return;
        double now = x0::now();
        lg::pa_turn_feed(C, Q.pa, r.yaw, r.t, r.recv, now);
        auto turn = lg::yaw_hold_turn(C, P.yh, lg::pa_turn_now(Q.pa, now));
        lg::Mv mv = lg::mv_parse(rpart("MV").value_or(""));
        std::string sec = lg::pa_sector(r.yaw, vx, vy, speed);
        std::string fam = lg::pa_famc(C, P.wstr.value_or(""));
        auto pk = lg::pa_pick(C, speed, sec, mv.c && *mv.c == "1", lg::pa_aim_mv(C, mv.a, fam, speed), mv.f && *mv.f == "1", turn, mv.l && *mv.l == "1");
        std::string st = lg::pa_pistol_run(C, pk.first, sec, speed, fam);
        lg::pa_hold(C, Q.pa, st, pk.second);
        P.paFace = *Q.pa.face;
        P.paSt = Q.pa.st;
        std::optional<std::string> atok = mv.act;
        auto id = lg::ac_rx(C, P.acRx, mv.act);
        if (!id && (id = lg::hit_local(C, P.hitTok))) atok = std::string(*id) + "h" + lg::inum(P.hitSeq);
        P.hitTok.reset();
        bool lightHit = id && id->size() == 2 && (*id)[0] == 'q' && FL("pahitup", 1) == 1;
        if (lightHit)
        {
            std::string as = "ac" + *id;
            const lg::PaEnt* ae = lg::ac_ent(as, fam);
            Ref aq = ae ? pa_asset(ae->rel) : Ref{};
            if (valid(aq))
            {
                std::wstring hs = W(x0::fs("pahitslot", "ContextualSlot"));
                Call hc(ai, L"PlaySlotAnimationAsDynamicMontage");
                hc.o(L"Asset", aq).n(L"SlotNodeName", hs.c_str()).f(L"BlendInTime", (float)FL("pahitbi", 0.1)).f(L"BlendOutTime", (float)FL("pahitbo", 0.3)).f(L"InPlayRate", 1.0f)
                    .i(L"LoopCount", 1).f(L"BlendOutTriggerTime", -1.0f).f(L"InTimeToStartMontageAt", 0.0f);
                bool hok = hc.run();
                LOG(fmt("NOLA pupanim: golpe leve %s (familia %s) na faixa %s -> %s ok=%s montagem=%s", atok->c_str(), fam.c_str(), x0::fs("pahitslot", "ContextualSlot").c_str(), lg::leaf_of(ae->rel, '/').c_str(), tf(hok), tf(hok && valid(hc.ro()))));
            }
            else LOG(fmt("NOLA pupanim: golpe leve %s familia %s sem clipe 3P", atok->c_str(), fam.c_str()));
            id.reset();
        }
        if (id && *id == "lh")
        {
            double il = lh_clip("acli", fam, 2.4);
            lg::lh_go(P.lh, atok.value_or(""), now, lg::lh_len(C, il));
            LOG(fmt("NOLA pupanim: acao remota %s: caixa aberta, busca segura (entrada %.2f s, depois laco ate fechar, familia %s)", atok->c_str(), il, fam.c_str()));
            id.reset();
        }
        else if (id && P.lh.ph)
        {
            LOG(fmt("NOLA pupanim: busca na caixa %s cortada pela acao %s", P.lh.tok.c_str(), atok.value_or("?").c_str()));
            P.lh = lg::LhRx{};
        }
        const lg::WrlEnt* we = id ? lg::wrl_pick(C, *id, P.wskel) : nullptr;
        if (we) wrl_start(P, *we, atok.value_or(""), fam);
        if (id)
        {
            const lg::PaEnt* le = lg::ac_ent("ac" + *id, fam);
            const char* ms = lg::wrl_ms(C, we, fam);
            const char* wc = ms ? ms : lg::wrl_chr(C, we);
            Ref lq = wc ? seq_asset(wc) : Ref{};
            if (!valid(lq)) { ms = nullptr; lq = le ? pa_asset(le->rel) : Ref{}; }
            else le = nullptr;
            std::string lname = le ? lg::leaf_of(le->rel, '/') : lg::leaf_of(wc ? wc : "", '.');
            uint8_t at = 0;
            bool hasAt = valid(lq) && x0::prop<uint8_t>(lq, "AdditiveAnimType", at);
            if (valid(lq) && (ms || lg::ac_layer(C, hasAt ? (int)at : 0)))
            {
                std::string ls = ms ? x0::fs("pwrlslot", "FullBody") : x0::fs("paaddslot", "ContextualSlot");
                std::wstring lw = W(ls);
                Call lc(ai, L"PlaySlotAnimationAsDynamicMontage");
                lc.o(L"Asset", lq).n(L"SlotNodeName", lw.c_str()).f(L"BlendInTime", (float)FL("paaddbi", 0.2)).f(L"BlendOutTime", (float)FL("paaddbo", 0.25)).f(L"InPlayRate", 1.0f)
                    .i(L"LoopCount", 1).f(L"BlendOutTriggerTime", -1.0f).f(L"InTimeToStartMontageAt", 0.0f);
                bool lok = lc.run();
                LOG(fmt("NOLA pupanim: acao remota %s (ac%s, familia %s) aditiva (tipo %d) na faixa %s sobre a locomocao -> %s ok=%s montagem=%s", atok->c_str(), id->c_str(), fam.c_str(), (int)at, ls.c_str(), lname.c_str(), tf(lok), tf(lok && valid(lc.ro()))));
                id.reset();
            }
        }
        if (id)
        {
            std::string as = "ac" + *id;
            const lg::PaEnt* ae = lg::ac_ent(as, fam);
            const char* br = lg::wrl_body(C, we, fam);
            Ref bq = br ? seq_asset(br) : Ref{};
            if (!valid(bq)) br = nullptr;
            Ref aq = br ? bq : (ae ? pa_asset(ae->rel) : Ref{});
            if (valid(aq))
            {
                float len = 0;
                double d = (fcall(aq, L"GetPlayLength", len) && len > 0.1f) ? (double)len : FL("pacmax", 3.5);
                P.acSt = as;
                P.acTok = atok;
                P.acRel = br;
                double bo = FL("pablendout", 0.25);
                double dd = lg::ac_dur(C, *id, ro::rh_fam(P.wstr.value_or("")) == "kn", std::min(d, FL("pacmax", 3.5)));
                P.acUntil = now + (FL("pablendearly", 1) == 1 && dd > 3.0 * bo ? dd - bo : dd);
                LOG(fmt("NOLA pupanim: acao remota %s (%s, familia %s) -> %s por %.2f s%s", atok->c_str(), as.c_str(), fam.c_str(), br ? lg::leaf_of(br, '.').c_str() : lg::leaf_of(ae->rel, '/').c_str(), P.acUntil - now, br ? " (clipe do corpo da tabela da arma)" : ""));
            }
            else LOG(fmt("NOLA pupanim: acao remota %s familia %s sem clipe 3P", atok->c_str(), fam.c_str()));
        }
        if (P.lh.ph)
        {
            int ph0 = P.lh.ph;
            const char* why = lg::lh_rx(C, P.lh, mv.act, now - r.recv, now, lg::lh_len(C, lh_clip("aclo", fam, 2.67)));
            const char* hs = lg::lh_st(P.lh.ph);
            if (hs && !lg::pa_ac_live(C, hs, fam, *Q.pa.st))
            {
                why = "escada ou queda";
                P.lh.ph = 0;
                hs = nullptr;
            }
            if (why) LOG(fmt("NOLA pupanim: busca na caixa %s fase %d -> %d (%s, %.1f s)", P.lh.tok.c_str(), ph0, P.lh.ph, why, now - P.lh.t0));
            if (hs)
            {
                P.acSt = std::string(hs);
                P.acTok = P.lh.tok;
                P.acUntil = P.lh.ph == 2 ? now + 1 : P.lh.until;
                P.acRel = nullptr;
            }
            else
            {
                P.lh = lg::LhRx{};
                if (P.acSt && lg::lh_is(*P.acSt)) { P.acSt.reset(); P.acTok.reset(); P.acUntil = 0; P.acRel = nullptr; }
            }
        }
        if (P.acSt && !lg::pa_ac_live(C, *P.acSt, fam, *Q.pa.st)) { P.acSt.reset(); P.acTok.reset(); P.acUntil = 0; P.acRel = nullptr; }
        std::string use = lg::pa_use(lg::pa_rl_base(C, *Q.pa.st, P.wrlAdd && x0::now() < P.wrlUntil), P.eqSt, P.eqUntil, P.acSt, P.acUntil, now);
        bool isAc = P.acSt && use == *P.acSt;
        bool lad = use == "ladder" && FL("paladder", 1) == 1 && FL("paladclip", 1) == 1;
        if (lad)
        {
            int n0 = P.lad.n;
            use = lg::lad_step(C, P.lad, r.seq, r.z, r.t.value_or(r.recv), now);
            if (P.lad.n != n0 && (P.lad.n <= 60 || P.lad.n % 100 == 0))
                LOG(fmt("NOLA pupanim: escada passo %d %s vz=%.1f ritmo=%.2f", P.lad.n, use.c_str(), P.lad.vz, P.lad.rate));
        }
        else P.lad = lg::LadSt{};
        lad_hide(P, lad && FL("paladhide", 1) == 1);
        bool shNew = lg::wsh_new(P.shSeen, g_shN[g_cv]);
        sh_diag(P, shNew);
        if (shNew && FL("pashoot", 1) == 1)
        {
            const lg::WshEnt* se = lg::wsh_pick(C, P.wskel, fam);
            bool go = se && lg::wsh_go(now, P.wrlUntil, isAc, lad);
            Ref sq = go ? seq_asset(se->chr) : Ref{};
            bool sok = false, gok = false;
            double srate = (se && se->rate > 0 ? se->rate : 1.0) * FL("pashrk", 1.0);
            if (valid(sq))
            {
                std::wstring sw = W(x0::fs("pashslot", "FullBody"));
                Call sc(ai, L"PlaySlotAnimationAsDynamicMontage");
                sc.o(L"Asset", sq).n(L"SlotNodeName", sw.c_str()).f(L"BlendInTime", (float)lg::wsh_blend(C, "pashbi", se->bi)).f(L"BlendOutTime", (float)lg::wsh_blend(C, "pashbo", se->bo)).f(L"InPlayRate", (float)srate)
                    .i(L"LoopCount", 1).f(L"BlendOutTriggerTime", -1.0f).f(L"InTimeToStartMontageAt", 0.0f);
                sok = sc.run() && valid(sc.ro());
            }
            const char* gp = go ? lg::wsh_gun(C, se, P.wskel) : nullptr;
            if (gp) gok = wsh_gun_play(P, gp, srate);
            P.shN++;
            if (P.shN <= 40 || P.shN % 100 == 0)
                LOG(fmt("NOLA pupanim: tiro do parceiro %d (esqueleto %s, familia %s) -> %s na faixa %s ok=%s arma %s ok=%s%s", P.shN, P.wskel.c_str(), fam.c_str(), se ? lg::leaf_of(se->chr, '.').c_str() : "sem clipe", x0::fs("pashslot", "FullBody").c_str(), tf(sok),
                        gp ? lg::leaf_of(gp, '.').c_str() : "-", tf(gok), !se ? "" : !go ? " (recarga/acao/escada ativa)" : se == lg::wsh_ent(P.wskel) ? "" : " (clipe da familia)"));
        }
        Ref q;
        P.rlWant = lg::w_rl_hand(C, P.acSt, isAc, fam);
        std::optional<lg::ClipPick> pick;
        if (const lg::PaEnt* de = isAc ? nullptr : lg::pa_det(C, use, P.det))
        {
            q = pa_asset(de->rel);
            if (valid(q)) pick = lg::ClipPick{de, de->st};
        }
        static lg::PaEnt sEnt{"samp", "any", "", 0.0, false};
        if (const char* sc = ro::samp_clip(FS("parosamp")))
        {
            sEnt.rel = sc;
            Ref sq = pa_asset(sc);
            if (valid(sq)) { q = sq; pick = lg::ClipPick{&sEnt, "samp"}; isAc = false; }
            else LOG(fmt("NOLA pupanim: clipe de amostra %s nao carregou", sc));
        }
        static lg::PaEnt rEnt{"acrl", "pt", "", 0.0, false};
        if (!pick && isAc && P.acRel)
        {
            Ref rq = seq_asset(P.acRel);
            if (valid(rq)) { rEnt.rel = P.acRel; q = rq; pick = lg::ClipPick{&rEnt, use}; }
        }
        if (!pick) pick = lg::pa_clip(C, use, fam, [&](const lg::PaEnt& e) { q = pa_asset(e.rel); return valid(q); });
        if (!pick && isAc)
        {
            P.acSt.reset(); P.acTok.reset(); P.acUntil = 0; P.acRel = nullptr;
            isAc = false;
            use = lg::pa_use(*Q.pa.st, P.eqSt, P.eqUntil, P.acSt, P.acUntil, now);
            pick = lg::pa_clip(C, use, fam, [&](const lg::PaEnt& e) { q = pa_asset(e.rel); return valid(q); });
        }
        if (!pick)
        {
            Q.clipBad++;
            LOG(fmt("NOLA pupanim: estado %s familia %s sem clipe (falha %d)%s", Q.pa.st->c_str(), fam.c_str(), Q.clipBad, Q.clipBad == 5 ? " -> clipes desligados para este corpo" : ""));
            return;
        }
        const lg::PaEnt& e = *pick->e;
        pa_overlay(Q, ai, lg::pa_ov(C, use, fam, P.det, isAc, pick->st == "dtidle"));
        ro_tick(P, ro::rh_fam(P.wstr.value_or("")), pick->st == "dtidle", use, isAc);
        double rate = lg::pa_rate(C, e, speed);
        if (isAc) rate = 1.0;
        if (lad) rate = std::max(0.01, P.lad.rate);
        std::string ck = std::string("pa:") + e.rel + (isAc ? "#" + P.acTok.value_or("") : std::string());
        uint64_t aa = ai.address();
        if (!Q.clip || ck != *Q.clip || aa != Q.clipAI || (!isAc && !valid(Q.mon)))
        {
            std::wstring slot = W(x0::fs("pupmvslot", "DefaultSlot"));
            Call c(ai, L"PlaySlotAnimationAsDynamicMontage");
            c.o(L"Asset", q).n(L"SlotNodeName", slot.c_str()).f(L"BlendInTime", (float)(lad ? FL("paladbi", 0.1) : FL("pablendin", 0.2))).f(L"BlendOutTime", (float)FL("pablendout", 0.25)).f(L"InPlayRate", (float)rate)
                .i(L"LoopCount", isAc && !lg::ac_loop(use) ? 1 : 100000).f(L"BlendOutTriggerTime", -1.0f).f(L"InTimeToStartMontageAt", 0.0f);
            bool ok = c.run();
            Ref mon = ok ? c.ro() : Ref{};
            Q.clipN++;
            if (!(ok && valid(mon))) Q.clipBad++;
            if (Q.clipN <= 30 || Q.clipN % 50 == 0 || Q.clipBad == 5)
            {
                LOG(fmt("NOLA pupanim: estado %s -> %s (clipe de %s, familia %s, v=%.0f dir=%s agach=%s mira=%s queda=%s giro=%s olhar=%s ritmo=%.2f) %s ok=%s montagem=%s%s%s",
                        ostrs(Q.paLog).c_str(), Q.pa.st->c_str(), pick->st.c_str(), fam.c_str(), speed, sec.c_str(), ostrs(mv.c).c_str(), ostrs(mv.a).c_str(), ostrs(mv.f).c_str(),
                        ostrs(turn).c_str(), Q.pa.face->c_str(), rate, lg::leaf_of(e.rel, '/').c_str(), tf(ok), tf(valid(mon)), ok ? "" : " chamada falhou",
                        Q.clipBad == 5 ? " -> clipes desligados para este corpo" : ""));
            }
            Q.clip = ck;
            Q.clipAI = aa;
            Q.mon = mon;
            Q.rate = rate;
            Q.paLog = Q.pa.st;
        }
        else if (std::fabs(rate - Q.rate.value_or(1)) > 0.15)
        {
            Q.rate = rate;
            Call(ai, L"Montage_SetPlayRate").o(L"Montage", Q.mon).f(L"NewPlayRate", (float)rate).run();
        }
    }

    struct LnArr
    {
        lg::LnTf* p;
        int32_t n, max;
    };
    struct LnHook
    {
        std::mutex mu;
        uint64_t mesh = 0;
        int n = 0;
        double deg = 0;
        uint64_t at = 0;
        double ax[3] = {1, 0, 0};
        std::vector<lg::LnStage> st;
        std::atomic<uint64_t> hits{0}, done{0};
    };
    LnHook& lnh()
    {
        static LnHook h;
        return h;
    }
    uint64_t g_lnBad = 0;

    bool ln_apply_seh(uint8_t* m, const std::vector<lg::LnStage>& st, const double* ax, double deg, int n)
    {
        __try
        {
            int ri = *reinterpret_cast<int32_t*>(m + 0x5e0);
            if (ri < 0 || ri > 1) return false;
            LnArr* a = reinterpret_cast<LnArr*>(m + 0x598 + ri * 0x10);
            if (!a->p || a->n != n) return false;
            lg::ln_apply(a->p, a->n, st, ax, deg);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void ln_post(uint8_t* m)
    {
        LnHook& h = lnh();
        if ((uint64_t)m != h.mesh) return;
        std::unique_lock<std::mutex> g(h.mu, std::try_to_lock);
        if (!g.owns_lock() || (uint64_t)m != h.mesh || std::fabs(h.deg) < 0.05 || GetTickCount64() - h.at > 500) return;
        h.hits++;
        if (ln_apply_seh(m, h.st, h.ax, h.deg, h.n)) h.done++;
    }
    void ln_set(uint64_t mesh, double deg)
    {
        LnHook& h = lnh();
        std::lock_guard<std::mutex> g(h.mu);
        if (h.mesh == mesh) { h.deg = deg; h.at = GetTickCount64(); }
    }
    bool ln_bind(Body& P, const Ref& mesh)
    {
        LnHook& h = lnh();
        uint64_t a = mesh.address();
        {
            std::lock_guard<std::mutex> g(h.mu);
            if (h.mesh == a && !h.st.empty()) return true;
        }
        if (g_lnBad == a) return false;
        g_lnBad = a;
        Call cn(mesh, L"GetNumBones");
        int n = cn.run() ? (int)cn.ri() : 0;
        if (n <= 0 || n > 1024) return false;
        std::vector<std::string> names(n);
        for (int i = 0; i < n; i++)
        {
            Call c(mesh, L"GetBoneName");
            c.i(L"BoneIndex", i);
            if (c.run()) names[i] = c.rn();
        }
        std::vector<int> par(n, -1);
        for (int i = 0; i < n; i++)
        {
            if (names[i].empty()) continue;
            std::wstring wn = W(names[i]);
            Call c(mesh, L"GetParentBone");
            c.n(L"BoneName", wn.c_str());
            std::string pn = c.run() ? c.rn() : std::string();
            if (pn.empty() || pn == "None") continue;
            for (int k = 0; k < n; k++)
                if (names[k] == pn) { par[i] = k; break; }
        }
        std::vector<lg::LnStage> st = lg::ln_stages(par, names);
        size_t nb = st.empty() ? 0 : st[0].bones.size();
        LOG(fmt("NOLA pupanim: tronco do boneco %d ossos, %d etapas, %d ossos na etapa 1", n, (int)st.size(), (int)nb));
        if (st.empty()) return false;
        g_lnBad = 0;
        double ax[3];
        lg::ln_axis(*P.lnBase, ax);
        std::lock_guard<std::mutex> g(h.mu);
        h.mesh = a;
        h.n = n;
        h.st = std::move(st);
        h.deg = 0;
        for (int k = 0; k < 3; k++) h.ax[k] = ax[k];
        return true;
    }

    namespace ab = m01::ab;
    struct AbArr
    {
        ab::Tf* p;
        int32_t n, max;
    };
    struct AbSk
    {
        int n = 0;
        bool ok = false;
        Ref m;
        std::vector<std::string> names;
        std::vector<int> par;
        ab::Sel sel;
    };
    struct AbRx
    {
        ab::Sel sel;
        std::vector<int> par;
        int n = 0;
        std::deque<ab::Pose> ring;
        ab::Pose tmp;
        double first = 0, last = 0;
        int seq = -1;
        uint64_t pk = 0, calls = 0, hits = 0, done = 0;
        double logAt = 0;
    };
    struct AbCap
    {
        uint64_t mesh = 0;
        Ref ref;
        int n = 0;
        ab::Sel sel;
        std::vector<ab::Tf> buf, tmp;
        double at = 0, hookAt = 0, bindAt = 0;
        uint64_t cnt = 0, miss = 0, direct = 0, dchg = 0, sent = 0;
        double logAt = 0;
        bool noMesh = false;
        bool forced = false;
        uint8_t vbt0 = 0;
    };
    std::mutex g_abm;
    std::map<uint64_t, AbSk> g_abSk;
    std::map<uint64_t, AbRx> g_abRx;
    AbCap g_abCap;
    std::atomic<bool> g_abOn{false};
    std::atomic<double> g_abYaw{0}, g_abDelay{0.25};
    std::set<uint64_t> g_abBadHash;

    const AbSk* ab_skel(const Ref& mesh)
    {
        uint64_t a = mesh.address();
        auto it = g_abSk.find(a);
        if (it != g_abSk.end() && it->second.n > 0 && valid(it->second.m)) return it->second.ok ? &it->second : nullptr;
        if (g_abSk.size() > 16) g_abSk.clear();
        AbSk& S = g_abSk[a];
        S = AbSk{};
        S.m = mesh;
        Call cn(mesh, L"GetNumBones");
        int n = cn.run() ? (int)cn.ri() : 0;
        if (n <= 0 || n > 1024) return nullptr;
        S.n = n;
        ro::Skel k;
        k.names.resize(n);
        k.par.assign(n, -1);
        for (int i = 0; i < n; i++)
        {
            Call c(mesh, L"GetBoneName");
            c.i(L"BoneIndex", i);
            if (c.run()) k.names[i] = c.rn();
        }
        for (int i = 0; i < n; i++)
        {
            if (k.names[i].empty()) continue;
            std::wstring wn = W(k.names[i]);
            Call c(mesh, L"GetParentBone");
            c.n(L"BoneName", wn.c_str());
            std::string pn = c.run() ? c.rn() : std::string();
            if (pn.empty() || pn == "None") continue;
            k.par[i] = k.idx(pn);
        }
        int hr = ro::hand_r(k);
        int hl = hr >= 0 ? k.idx(ro::mirror(k.names[hr])) : -1;
        if (hr < 0) { LOG("NOLA avbones: right hand bone not found, bone streaming off for this mesh"); return nullptr; }
        S.names = k.names;
        S.par = k.par;
        S.sel = ab::select(S.names, S.par, hr, hl);
        S.ok = !S.sel.idx.empty();
        LOG(fmt("NOLA avbones: skeleton of %s: %d bones, %d streamed, packet %d bytes, hash %08x", nm(mesh).c_str(), n, (int)S.sel.idx.size(), (int)ab::payload_bytes(S.sel), (unsigned)S.sel.hash));
        return S.ok ? &S : nullptr;
    }

    bool ab_cap_seh(uint8_t* m, ab::Tf* dst, int n)
    {
        __try
        {
            int ri = *reinterpret_cast<int32_t*>(m + 0x5e0);
            if (ri < 0 || ri > 1) return false;
            AbArr* a = reinterpret_cast<AbArr*>(m + 0x598 + ri * 0x10);
            if (!a->p || a->n != n) return false;
            std::memcpy(dst, a->p, sizeof(ab::Tf) * (size_t)n);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool ab_apply_seh(uint8_t* m, int n, const std::vector<int>& par, const ab::Sel& sel, const ab::Pose& p, double w, double yaw)
    {
        __try
        {
            int ri = *reinterpret_cast<int32_t*>(m + 0x5e0);
            if (ri < 0 || ri > 1) return false;
            AbArr* a = reinterpret_cast<AbArr*>(m + 0x598 + ri * 0x10);
            if (!a->p || a->n != n) return false;
            ab::apply(a->p, n, par, sel, p, w, yaw);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void ab_post(uint8_t* m)
    {
        if (!g_abOn.load()) return;
        uint64_t a = (uint64_t)m;
        std::unique_lock<std::mutex> g(g_abm, std::try_to_lock);
        if (!g.owns_lock()) return;
        if (a == g_abCap.mesh && g_abCap.n > 0)
        {
            if (ab_cap_seh(m, g_abCap.buf.data(), g_abCap.n)) { g_abCap.at = g_abCap.hookAt = x0::now(); g_abCap.cnt++; }
            else g_abCap.miss++;
            return;
        }
        auto it = g_abRx.find(a);
        if (it == g_abRx.end()) return;
        AbRx& R = it->second;
        R.calls++;
        if (R.ring.empty()) return;
        double now = x0::now();
        double w = ab::weight(now, R.first, R.last);
        if (w <= 0) return;
        if (!ab::sample(R.ring, now - g_abDelay.load(), R.tmp)) return;
        R.hits++;
        if (ab_apply_seh(m, R.n, R.par, R.sel, R.tmp, w, g_abYaw.load())) R.done++;
    }

    uint64_t ab_leader_seh(uint64_t m)
    {
        __try { return *reinterpret_cast<uint64_t*>(m + 0x530); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    void ab_restore(AbCap& C)
    {
        if (!C.forced) return;
        C.forced = false;
        bool ok = valid(C.ref) && x0::prop_set<uint8_t>(C.ref, "VisibilityBasedAnimTickOption", C.vbt0);
        LOG(fmt("NOLA avbones: source VisibilityBasedAnimTickOption restored to %d ok=%s", (int)C.vbt0, tf(ok)));
    }

    std::string ab_str()
    {
        bool on = FL("avbones", 0) == 1;
        g_abOn = on;
        if (!on)
        {
            if (g_abCap.mesh)
            {
                std::lock_guard<std::mutex> g(g_abm);
                ab_restore(g_abCap);
                g_abCap.mesh = 0;
                g_abCap.ref = Ref{};
                g_abSrc = 0;
            }
            return "";
        }
        Ref pawn = local_pawn();
        if (!valid(pawn)) return "";
        Ref sm = pref(pawn, "ShadowMeshComponent");
        double now = x0::now();
        if (!valid(sm))
        {
            if (!g_abCap.noMesh) { g_abCap.noMesh = true; LOG("NOLA avbones: local pawn has no ShadowMeshComponent, nothing to stream"); }
            return "";
        }
        if (!fin_install()) return "";
        const AbSk* sk = ab_skel(sm);
        if (!sk) return "";
        std::string out;
        std::lock_guard<std::mutex> g(g_abm);
        AbCap& C = g_abCap;
        if (C.mesh != sm.address() || !valid(C.ref))
        {
            ab_restore(C);
            C.mesh = sm.address();
            C.ref = sm;
            C.n = sk->n;
            C.sel = sk->sel;
            C.buf.assign((size_t)sk->n, ab::Tf{});
            C.tmp.assign((size_t)sk->n, ab::Tf{});
            C.at = C.hookAt = 0;
            C.bindAt = now;
            C.logAt = now - 8;
            C.cnt = C.miss = C.direct = C.dchg = C.sent = 0;
            g_abSrcCalls = 0;
            g_abSrc = C.mesh;
            uint8_t vbt = 255;
            x0::prop<uint8_t>(sm, "VisibilityBasedAnimTickOption", vbt);
            LOG(fmt("NOLA avbones: source mesh %s bound (bones %d, tick option %d, leader %s)", nm(sm).c_str(), sk->n, (int)vbt, ab_leader_seh(C.mesh) ? "set" : "none"));
        }
        if (now - C.hookAt > 1.0 && now - C.bindAt > 1.0)
        {
            if (FL("avbonesforce", 1) == 1)
            {
                uint8_t vbt = 255;
                if (x0::prop<uint8_t>(sm, "VisibilityBasedAnimTickOption", vbt) && vbt != 0)
                {
                    bool ok = x0::prop_set<uint8_t>(sm, "VisibilityBasedAnimTickOption", (uint8_t)0);
                    if (!C.forced) C.vbt0 = vbt;
                    C.forced = C.forced || ok;
                    LOG(fmt("NOLA avbones: no hook pose for 1 s, source VisibilityBasedAnimTickOption %d -> 0 (AlwaysTickPoseAndRefreshBones) ok=%s", (int)vbt, tf(ok)));
                }
            }
            if (ab_cap_seh((uint8_t*)C.mesh, C.tmp.data(), C.n))
            {
                C.direct++;
                if (std::memcmp(C.tmp.data(), C.buf.data(), sizeof(ab::Tf) * (size_t)C.n) != 0) { C.dchg++; C.buf.swap(C.tmp); }
                C.at = now;
            }
            else C.miss++;
        }
        bool fresh = now - C.at <= 0.5;
        static uint8_t seq = 0;
        if (fresh) out = ab::encode(C.sel, ++seq, C.buf.data(), C.n);
        if (!out.empty()) C.sent++;
        if (now - C.logAt > 10)
        {
            C.logAt = now;
            LOG(fmt("NOLA avbones: source %s: hook calls %llu, hook captures %llu, buffer misses %llu, direct reads %llu (changed %llu), sent %llu (%d bones, %d bytes base64), tick option forced %s",
                    fresh ? "fresh" : "stale", (unsigned long long)g_abSrcCalls.load(), (unsigned long long)C.cnt, (unsigned long long)C.miss, (unsigned long long)C.direct, (unsigned long long)C.dchg,
                    (unsigned long long)C.sent, (int)C.sel.idx.size(), (int)out.size(), tf(C.forced)));
        }
        return out;
    }

    void ab_tick(Body& P)
    {
        bool on = FL("avbones", 0) == 1;
        if (!on)
        {
            if (g_abOn.exchange(false))
            {
                std::lock_guard<std::mutex> g(g_abm);
                g_abRx.clear();
            }
            return;
        }
        g_abOn = true;
        g_abYaw = FL("avbonesyaw", 0);
        g_abDelay = FL("avbonesdelay", 0.25);
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh)) return;
        uint64_t a = mesh.address();
        auto s = rpart("AB");
        if (!s || s->empty()) return;
        uint8_t seq;
        int cnt;
        uint32_t h;
        if (!ab::header(*s, seq, cnt, h)) return;
        {
            std::lock_guard<std::mutex> g(g_abm);
            auto f = g_abRx.find(a);
            if (f != g_abRx.end() && f->second.seq == (int)seq) return;
        }
        if (!fin_install()) return;
        const AbSk* sk = ab_skel(mesh);
        if (!sk) return;
        if (sk->sel.hash != h || (int)sk->sel.idx.size() != cnt)
        {
            if (g_abBadHash.insert(a).second) LOG(fmt("NOLA avbones: stream skeleton mismatch for %s (local hash %08x bones %d, remote hash %08x bones %d), clips stay", nm(mesh).c_str(), (unsigned)sk->sel.hash, (int)sk->sel.idx.size(), (unsigned)h, cnt));
            return;
        }
        ab::Pose p;
        if (!ab::decode(*s, sk->sel, p)) return;
        double now = x0::now();
        std::lock_guard<std::mutex> g(g_abm);
        for (auto it = g_abRx.begin(); it != g_abRx.end();)
        {
            if (it->first != a && now - it->second.last > 30) it = g_abRx.erase(it);
            else ++it;
        }
        bool fresh = g_abRx.find(a) == g_abRx.end();
        AbRx& R = g_abRx[a];
        if (fresh)
        {
            R.sel = sk->sel;
            R.par = sk->par;
            R.n = sk->n;
            R.logAt = now - 8;
        }
        if (R.seq == (int)seq) return;
        R.seq = seq;
        if (R.ring.empty() || now - R.last > 0.5)
        {
            R.ring.clear();
            R.first = now;
            LOG(fmt("NOLA avbones: receiving bones for %s (view %d), %d streamed", nm(mesh).c_str(), g_cv, (int)R.sel.idx.size()));
        }
        R.last = now;
        p.at = now;
        R.ring.push_back(std::move(p));
        while (R.ring.size() > 8) R.ring.pop_front();
        R.pk++;
        if (now - R.logAt > 10)
        {
            R.logAt = now;
            LOG(fmt("NOLA avbones: view %d packets %llu, hook calls %llu, sampled %llu, applied %llu", g_cv, (unsigned long long)R.pk, (unsigned long long)R.calls, (unsigned long long)R.hits, (unsigned long long)R.done));
        }
    }

    void pa_lean(Body& P, double now)
    {
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh)) return;
        if (!P.lnFor.same(mesh)) { P.lnFor = mesh; P.lnBase.reset(); P.lnCur = 0; P.lnSet = 0; P.lnAt = 0; }
        int mode = (int)FL("palean", 1);
        int want = (!P.wd && (mode == 1 || mode == 2)) ? lg::mv_lean(lg::mv_parse(rpart("MV").value_or(""))) : 0;
        double deg = FL("paleandeg", 15);
        double dt = P.lnAt > 0 ? std::min(0.2, now - P.lnAt) : 0;
        P.lnAt = now;
        if (!P.lnBase)
        {
            if (want == 0) return;
            Rot b;
            if (!x0::prop<Rot>(mesh, "RelativeRotation", b)) return;
            P.lnBase = lg::LRot{b.pitch, b.yaw, b.roll};
        }
        double cur = lg::lean_step(P.lnCur, want * deg, dt, deg * 5);
        P.lnCur = cur;
        if (want != P.lnWant)
        {
            P.lnWant = want;
            P.lnN++;
            if (P.lnN <= 50 || P.lnN % 50 == 0)
                LOG(fmt("NOLA pupanim: inclinacao remota %s alvo %.1f graus (base %.1f,%.1f,%.1f, vez %d)", want < 0 ? "esquerda" : want > 0 ? "direita" : "nenhuma", want * deg, P.lnBase->p, P.lnBase->y, P.lnBase->r, P.lnN));
        }
        if (mode == 1)
        {
            if (P.lnSet != 0)
            {
                if (Call(mesh, L"K2_SetRelativeRotation").r(L"NewRotation", Rot{P.lnBase->p, P.lnBase->y, P.lnBase->r}).b(L"bSweep", false).b(L"bTeleport", true).run()) P.lnSet = 0;
            }
            if (cur != 0 && fin_install()) ln_bind(P, mesh);
            ln_set(mesh.address(), cur);
            if (want != P.lnLogW)
            {
                P.lnLogW = want;
                LOG(fmt("NOLA pupanim: tronco inclinado %.1f graus (ossos aplicados %llu de %llu chamadas)", cur, (unsigned long long)lnh().done.load(), (unsigned long long)lnh().hits.load()));
            }
            return;
        }
        ln_set(mesh.address(), 0);
        if (std::fabs(cur - P.lnSet) < 0.05) return;
        lg::LRot q = cur == 0 ? *P.lnBase : lg::lean_rot(*P.lnBase, cur);
        if (Call(mesh, L"K2_SetRelativeRotation").r(L"NewRotation", Rot{q.p, q.y, q.r}).b(L"bSweep", false).b(L"bTeleport", true).run()) P.lnSet = cur;
    }
    bool yaw_frame(const Body& P, double now) { return FL("avframe", 0) == 1 && FL("avyawf", 0) > 0 && FL("nsmooth", 0) == 1 && now - P.hbAt <= 0.25; }

    void pup_move(Body& P, double vx, double vy, double speed, double now, const lg::RSnap& r)
    {
        St& s = S();
        double mv = FL("pupmove", 0);
        bool vel = pm_vel(), clips = (mv == 2 || mv == 3);
        pup_anim(P);
        if (P.broken || !valid(P.pawn)) return;
        Pm& Q = P.pm;
        if (!Q.on || !Q.pawn.same(P.pawn))
        {
            Q = Pm{};
            Q.on = true;
            Q.pawn = P.pawn;
            Ref ai0 = pm_ai(P);
            uint8_t b = 0;
            if (valid(ai0) && x0::prop<uint8_t>(ai0, "RootMotionMode", b)) Q.rm0 = b;
            Ref cm0 = pref(P.pawn, "CharacterMovement");
            if (valid(cm0) && x0::prop<uint8_t>(cm0, "MovementMode", b)) Q.mm0 = b;
            std::string n0 = x0::obj_name(P.pawn);
            if (n0.empty()) n0 = "?";
            LOG(fmt("NOLA pupmove: boneco %s pupmove=%d (velocidade=%s clipes=%s) RootMotionMode=%s MovementMode=%s uid=%s",
                    n0.c_str(), (int)mv, tf(vel), tf(clips), ostr(Q.rm0).c_str(), ostr(Q.mm0).c_str(), ostr(P.uid).c_str()));
        }
        Ref ai = pm_ai(P);
        if (valid(ai) && FL("pupmvrm", 1) == 1)
        {
            uint8_t rm = 0;
            bool got = x0::prop<uint8_t>(ai, "RootMotionMode", rm);
            if (!got || rm != 1)
            {
                bool ok = x0::prop_set<uint8_t>(ai, "RootMotionMode", (uint8_t)1);
                Q.rm++;
                if (Q.rm <= 3 || Q.rm % 50 == 0) LOG("NOLA pupmove: RootMotionMode " + (got ? lg::inum(rm) : std::string("nil")) + " -> 1 (IgnoreRootMotion) ok=" + tf(ok) + " vez=" + lg::inum(Q.rm));
            }
        }
        if (vel && !s.pmVelLog)
        {
            s.pmVelLog = true;
            LOG(fmt("NOLA pupmove: pupmove=%d com velocidade nao portado em C++ -> movedor/teleporte", (int)mv));
        }
        bool nsOn = ns_pup(P, &r, now);
        if (nsOn) P.pmW.reset();
        else { set_loc(P.pawn, Vec3{P.pos.x, P.pos.y, P.pos.z}); P.pmW = P.pos; }
        if (!P.nsYaw && P.yaw && !yaw_frame(P, now)) set_yaw(P.pawn, *P.yaw);
        pa_lean(P, now);
        if (clips && valid(ai) && !P.wd)
        {
            if (FL("pupanim", 0) == 1) pa_clips(P, Q, ai, speed, vx, vy, r);
        }
        else if (Q.clip) pm_clip_stop(Q, ai);
    }

    bool pup_recycle(const char* why)
    {
        Body& P = V().pup;
        if (!has(P.pawn)) return false;
        std::string how = "ja invalido", name = "?";
        bool ok = true;
        double now = x0::now();
        if (valid(P.pawn))
        {
            name = x0::obj_name(P.pawn);
            hide_actor(P.pawn, true);
            ok = false;
            if (P.uid && *P.uid > 0) { how = "XDestroyModel(" + lg::inum(*P.uid) + ")"; ok = destroy_model(*P.uid); }
            if (!ok) { how = "K2_DestroyActor"; ok = destroy_actor(P.pawn); }
        }
        if (P.path) P.bad.insert(*P.path);
        LOG(std::string("NOLA pupfix: corpo ") + name + " largado (" + why + ") -> escondido + " + how + " ok=" + tf(ok) + "; corpo novo sera adotado/pedido");
        P.pawn.reset();
        P.path.reset();
        P.state.reset();
        P.hidden = false;
        P.abpFor.reset();
        P.naFor.reset();
        P.uid.reset();
        P.pm = Pm{};
        P.pmW.reset();
        P.pos = lg::PupPos{};
        P.retryAt = now + 2;
        return ok;
    }
    bool pfx_tick(Body& P, double speed, double now)
    {
        if (!(P.pmW && valid(P.pawn))) return false;
        Vec3 A;
        if (!loc(P.pawn, A)) return false;
        double f = std::sqrt((A.x - P.pmW->x) * (A.x - P.pmW->x) + (A.y - P.pmW->y) * (A.y - P.pmW->y));
        Pl& s = V();
        if (lg::pfx_tick(C, s.pfxH, s.pfxR, P.path, P.uid, f, A.x, A.y, A.z, speed, now) == lg::Pfx::recycle)
        {
            pup_recycle("pupfix");
            return true;
        }
        return false;
    }

    std::string lead_at(const std::string& part)
    {
        auto in = [](char ch) { return ch == '/' || ch == '.' || ch == '@'; };
        size_t i = 0;
        while (i < part.size())
        {
            if (in(part[i])) { i++; continue; }
            size_t e = i;
            while (e < part.size() && !in(part[e])) e++;
            if (e < part.size() && part[e] == '@') return part.substr(i, e - i + 1);
            i = e;
        }
        return "?";
    }

    Vec3 rl_loc()
    {
        bool t = FL("pwtrig", 1) == 1;
        return Vec3{FL("pwrlx", 7.15) + (t ? FL("pwtx", 2.5) : 0), FL("pwrly", 2.4) + (t ? FL("pwty", -1.3) : 0), FL("pwrlz", -1.8) + (t ? FL("pwtz", -0.6) : 0)};
    }
    Rot rl_rot() { return Rot{FL("pwrlp", 2.4), FL("pwrlw", -0.9), FL("pwrlr", -168.2)}; }
    void rl_set(Body& P, bool force)
    {
        if (P.wcomps.empty() || !valid(P.wcomps[0])) return;
        const Ref& wc = P.wcomps[0];
        Vec3 l = rl_loc();
        Rot r = rl_rot();
        std::string key = fmt("%.2f,%.2f,%.2f,%.2f,%.2f,%.2f", l.x, l.y, l.z, r.pitch, r.yaw, r.roll);
        bool newBase = key != P.rlBaseKey;
        if (P.rlCan)
        {
            ro::Tf off{};
            Call g(wc, L"GetSocketTransform");
            g.n(L"InSocketName", L"jnt_offset").i(L"TransformSpace", 2);
            if (g.ok() && g.run() && g.rraw(&off, sizeof off))
            {
                ro::Loc gp;
                ro::r2q(r.pitch, r.yaw, r.roll, gp.q);
                gp.t[0] = l.x; gp.t[1] = l.y; gp.t[2] = l.z;
                ro::Loc c = ro::grip_cancel(gp, off);
                l = Vec3{c.t[0], c.t[1], c.t[2]};
                ro::q2r(c.q, r.pitch, r.yaw, r.roll);
            }
        }
        auto nd = [](double a, double b) { return std::fabs(a - b) < 0.01; };
        if (!force && !newBase && nd(l.x, P.rlLast.x) && nd(l.y, P.rlLast.y) && nd(l.z, P.rlLast.z) && nd(r.pitch, P.rlLastR.pitch) && nd(r.yaw, P.rlLastR.yaw) && nd(r.roll, P.rlLastR.roll)) return;
        Call(wc, L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", l).r(L"NewRotation", r).b(L"bSweep", false).b(L"bTeleport", true).run();
        P.rlLast = l;
        P.rlLastR = r;
        if (newBase)
        {
            P.rlBaseKey = key;
            LOG("NOLA pupanim: pistola offset -> " + key + (P.rlCan ? " (movimento do jnt_offset cancelado)" : ""));
        }
    }
    void rl_hand(Body& P)
    {
        const Ref& wc = P.wcomps[0];
        if (wc.address() != P.wsComp) { P.wsComp = wc.address(); P.wsHand = false; }
        std::string fam = lg::pa_famc(C, P.wstr.value_or(""));
        bool live = x0::now() < P.wrlUntil;
        bool stk = lg::w_rl_stock(C, live, fam);
        bool ar = !stk && lg::w_rl_ar(C, fam, live);
        bool want = !stk && (ar || (P.rlWant && fam == "pt" && FL("pwrlhand", 1) >= 1) || (lg::w_det_hand(C, P.det.has_value(), fam) && !P.roPt));
        if (want && P.wsHand && P.rlKeep && !ar) P.wsHand = false;
        if (want == P.wsHand && ar != P.rlKeep && want) P.wsHand = false;
        if (ar && want == P.wsHand) return;
        if (ar)
        {
            Ref mesh = pref(P.pawn, "Mesh");
            if (!valid(mesh)) return;
            bool fresh = P.rlKT0 != P.wrlT0;
            std::wstring rw = L"jnt_r_weapon";
            bool ok = Call(wc, L"K2_AttachToComponent").o(L"Parent", mesh).n(L"SocketName", rw.c_str()).i(L"LocationRule", fresh ? 1 : 2).i(L"RotationRule", fresh ? 1 : 2).i(L"ScaleRule", 1).b(L"bWeldSimulatedBodies", false).run();
            if (fresh)
            {
                Vec3 l{};
                Rot r{};
                if (x0::prop<Vec3>(wc, "RelativeLocation", l) && x0::prop<Rot>(wc, "RelativeRotation", r)) { P.rlKL = l; P.rlKR = r; P.rlKT0 = P.wrlT0; }
            }
            else Call(wc, L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", P.rlKL).r(L"NewRotation", P.rlKR).b(L"bSweep", false).b(L"bTeleport", true).run();
            P.wsHand = true;
            P.rlKeep = true;
            LOG(fmt("NOLA pupanim: fuzil na mao na recarga -> jnt_r_weapon (ok=%s novo=%s rel=%.1f,%.1f,%.1f rot=%.1f,%.1f,%.1f)", tf(ok), tf(fresh), P.rlKL.x, P.rlKL.y, P.rlKL.z, P.rlKR.pitch, P.rlKR.yaw, P.rlKR.roll));
            return;
        }
        P.rlKeep = false;
        bool can = lg::w_rl_cancel(C, fam, want);
        if (can != P.rlCan) { P.rlCan = can; P.rlBaseKey.clear(); }
        if (want && P.wsHand) rl_set(P, false);
        if (want == P.wsHand) return;
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh)) return;
        bool ok = attach(wc, mesh, want ? std::string("jnt_r_weapon") : P.wsock);
        if (want) rl_set(P, true);
        P.wsHand = want;
        Vec3 l = rl_loc();
        LOG(fmt("NOLA pupanim: pistola na mao -> %s (ok=%s rel=%.1f,%.1f,%.1f)", want ? "jnt_r_weapon" : P.wsock.c_str(), tf(ok), l.x, l.y, l.z));
    }

    void pw_snap(Body& P)
    {
        if (P.wcomps.empty() || !valid(P.wcomps[0])) return;
        P.wsnap = (P.wsnap == 0.001) ? 0 : 0.001;
        if (P.wsHand)
        {
            Vec3 l = P.rlKeep ? P.rlKL : rl_loc();
            l.x += P.wsnap;
            Call(P.wcomps[0], L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", l).r(L"NewRotation", P.rlKeep ? P.rlKR : rl_rot()).b(L"bSweep", false).b(L"bTeleport", true).run();
        }
        else Call(P.wcomps[0], L"K2_SetRelativeLocation").v(L"NewLocation", Vec3{P.wsnap, 0, 0}).b(L"bSweep", false).b(L"bTeleport", true).run();
    }

    void tick_frame()
    {
        bool snap = FL("pwsnap", 0) == 2;
        double now = x0::now();
        int nv = multi() ? x0::kMaxClients : 1;
        for (int v = 0; v < nv; ++v)
        {
            CvScope cv(v);
            if (V().gone || !owns_avatar()) continue;
            Body& P = V().pup;
            if (!body_ok(P)) continue;
            if (snap) pw_snap(P);
            else if (P.rlCan && P.wsHand && now < P.wrlUntil + FL("pwrlcant", 0.3)) rl_set(P, false);
        }
    }

    int mesh_kids_gate(uint64_t m, bool clear)
    {
        __try
        {
            volatile uint8_t* g = reinterpret_cast<volatile uint8_t*>(m + 0xA3B);
            const uint8_t hi = *reinterpret_cast<volatile uint8_t*>(m + 0xA3A);
            if (hi & 0x80) return 2;
            if (!(*g & 1)) return 0;
            if (clear) *g = (uint8_t)(*g & 0xFE);
            return 1;
        }
        __except (1) { return -1; }
    }

    void pup_kids(Body& P)
    {
        if (FL("pupkids", 1) != 1 || !valid(P.pawn)) return;
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh) || !x0::is_a(mesh, L"SkeletalMeshComponent")) return;
        int r = mesh_kids_gate(mesh.address(), true);
        if (P.kidMesh != mesh.address())
        {
            P.kidMesh = mesh.address();
            LOG(fmt("NOLA pupkids: malha 0x%llx portao de filhos (A3B bit0) estava=%d (1 = limpo agora)", (unsigned long long)mesh.address(), r));
        }
    }

    void bolt_place(Body& P)
    {
        if (!P.wBolt || P.wcomps.empty() || !valid(P.wcomps[0])) return;
        Vec3 l{FL("pabx", 0), FL("paby", 0), FL("pabz", 0)};
        Rot r{FL("pabp", 0), FL("pabw", 0), FL("pabr", 0)};
        double s = FL("pabs", 1);
        const char* src = "pab";
        if (FL("pabarm", 1) == 1)
        {
            ro::Loc it;
            bool got = false;
            auto ci = g_roCap.find("bo");
            if (ci != g_roCap.end() && !g_roSk.bad)
            {
                auto f = ci->second.find(g_roSk.k.idx("jnt_item"));
                if (f != ci->second.end()) { it = f->second; got = true; src = "captura"; }
            }
            if (!got)
                if (const ro::Row* rw = ro::row("bo", "jnt_item"))
                {
                    for (int q = 0; q < 4; q++) it.q[q] = rw->q[q];
                    for (int q = 0; q < 3; q++) it.t[q] = rw->t[q];
                    got = true;
                    src = "tabela";
                }
            if (got)
            {
                l = Vec3{it.t[0], it.t[1], it.t[2]};
                ro::q2r(it.q, r.pitch, r.yaw, r.roll);
            }
        }
        std::string key = fmt("%s %d %.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f", src, g_roCapGen, l.x, l.y, l.z, r.pitch, r.yaw, r.roll, s);
        if (key == P.bKey) return;
        const Ref& wc = P.wcomps[0];
        Call(wc, L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", l).r(L"NewRotation", r).b(L"bSweep", false).b(L"bTeleport", true).run();
        Call(wc, L"SetRelativeScale3D").v(L"NewScale3D", Vec3{s, s, s}).run();
        P.bKey = key;
        LOG("NOLA puppet: parafuso na mao " + P.wsock + " rel=" + key);
    }
    void knife_place(Body& P)
    {
        if (!P.wKnife || P.wcomps.empty() || !valid(P.wcomps[0])) return;
        Vec3 l{FL("pakx", 8.8), FL("paky", 0.7), FL("pakz", -5.04)};
        Rot r{FL("pakp", 10.77), FL("pakw", 23.71), FL("pakr", -152.91)};
        std::string key = fmt("k %.2f,%.2f,%.2f,%.2f,%.2f,%.2f", l.x, l.y, l.z, r.pitch, r.yaw, r.roll);
        if (key == P.bKey) return;
        Call(P.wcomps[0], L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", l).r(L"NewRotation", r).b(L"bSweep", false).b(L"bTeleport", true).run();
        P.bKey = key;
        LOG("NOLA puppet: faca na mao " + P.wsock + " rel=" + key);
    }
    void pup_gun(Body& P, const std::string& x1);
    void pup_det(Body& P, const std::string& det);
    bool rd_sock(const Ref& c, const char* s, Vec3& v)
    {
        std::wstring w = W(s);
        Call d(c, L"GetSocketLocation");
        d.n(L"InSocketName", w.c_str());
        return d.run() && d.rv(v);
    }
    static double rd_dist(const Vec3& a, const Vec3& b)
    {
        double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
        return std::sqrt(x * x + y * y + z * z);
    }
    bool kick_rd(const Ref& pawn, Vec3& ha, Vec3& hh, Vec3& f)
    {
        Ref mesh = pref(pawn, "Mesh");
        Vec3 a, h, d;
        if (!(valid(mesh) && loc(pawn, a) && rd_sock(mesh, "jnt_r_hand", h) && rd_sock(mesh, "jnt_head", d))) return false;
        Call rc(mesh, L"GetSocketRotation");
        std::wstring w = W("jnt_r_hand");
        rc.n(L"InSocketName", w.c_str());
        Rot r;
        if (!(rc.run() && rc.rr(r))) return false;
        double p = r.pitch * 3.14159265358979 / 180, y = r.yaw * 3.14159265358979 / 180;
        ha = Vec3{h.x - a.x, h.y - a.y, h.z - a.z};
        hh = Vec3{h.x - d.x, h.y - d.y, h.z - d.z};
        f = Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)};
        return true;
    }
    void kick_step(KickD& K, const Ref& pawn, bool start, const char* who, const std::string& tag, double win)
    {
        double now = x0::now();
        if (!start && K.until <= 0) return;
        Vec3 ha, hh, f;
        bool ok = valid(pawn) && kick_rd(pawn, ha, hh, f);
        if (K.until > 0 && (start || now > K.until))
        {
            LOG(fmt("NOLA kickdiag: %s tiro %d %s pashoot=%d mao_ator max %.2f cm mao_cabeca max %.2f cm giro_mao max %.2f graus pico %.3f s em %d amostras", who, K.n, tag.c_str(), (int)FL("pashoot", 1), K.mHA, K.mHH, K.mAng, K.tPk, K.s));
            K.until = 0;
        }
        if (start && ok)
        {
            K.n++;
            K.bHA = ha;
            K.bHH = hh;
            K.bF = f;
            K.mHA = K.mHH = K.mAng = K.tPk = 0;
            K.s = 0;
            K.t0 = now;
            K.until = now + win;
            return;
        }
        if (!ok || K.until <= 0) return;
        K.s++;
        double dha = rd_dist(ha, K.bHA), dhh = rd_dist(hh, K.bHH);
        double c = std::clamp(f.x * K.bF.x + f.y * K.bF.y + f.z * K.bF.z, -1.0, 1.0), ang = std::acos(c) * 180 / 3.14159265358979;
        if (dhh > K.mHH) K.tPk = now - K.t0;
        K.mHA = std::max(K.mHA, dha);
        K.mHH = std::max(K.mHH, dhh);
        K.mAng = std::max(K.mAng, ang);
    }
    Ref g_kN;
    double g_kNAt = 0, g_kNPos = -1;
    bool g_kNSh = false;
    KickD g_kND;
    std::string g_kNTag;
    void npc_diag()
    {
        if (FL("pashdiagn", 0) != 1) return;
        double now = x0::now();
        if (now - g_kNAt > 2 || !valid(g_kN))
        {
            g_kNAt = now;
            Ref lp = local_pawn();
            Vec3 me;
            Ref best;
            double bd = FL("pashdiagnr", 40) * 100;
            if (loc(lp, me))
                for (const Ref& o : x0::all_of(L"BP_AI_Human_C"))
                {
                    Vec3 q;
                    if (!valid(o) || is_body(o) || loot_body(o) || !loc(o, q)) continue;
                    double dd = rd_dist(q, me);
                    if (dd < bd) { bd = dd; best = o; }
                }
            if (valid(best) && !best.same(g_kN)) LOG(fmt("NOLA kickdiag: NPC de referencia %s a %.1f m", x0::obj_name(best).c_str(), bd / 100));
            if (valid(best)) x0::prop_set<float>(best, "CustomTimeDilation", (float)FL("pashdiagnd", 1));
            if (!best.same(g_kN)) { g_kNSh = false; g_kNPos = -1; g_kND = KickD{}; }
            g_kN = best;
        }
        if (!valid(g_kN)) return;
        Ref ai = ocall(pref(g_kN, "Mesh"), L"GetAnimInstance");
        bool start = false;
        if (valid(ai))
        {
            Call mc(ai, L"GetCurrentActiveMontage");
            Ref m = mc.run() ? mc.ro() : Ref{};
            std::string mn = valid(m) ? x0::obj_name(m) : std::string();
            bool sh = mn.find("shoot") != std::string::npos;
            double pos = -1;
            if (sh)
            {
                Call pc(ai, L"Montage_GetPosition");
                pc.o(L"Montage", m);
                if (pc.run()) pos = pc.rf();
                if (!g_kNSh || pos + 1e-3 < g_kNPos) { start = true; g_kNTag = mn; }
            }
            g_kNSh = sh;
            g_kNPos = pos;
        }
        kick_step(g_kND, g_kN, start, "npc", g_kNTag, FL("pashdiagt", 0.45) / std::max(0.05, FL("pashdiagnd", 1)));
    }
    void sh_diag(Body& P, bool start)
    {
        if (FL("pashdiag", 0) != 1) return;
        npc_diag();
        kick_step(P.shd, P.pawn, start, "boneco", P.wskel, FL("pashdiagt", 0.45));
    }
    void rl_diag(Body& P)
    {
        double now = x0::now();
        if (FL("pwrldiag", 0) != 1 || P.wcomps.empty() || !valid(P.wcomps[0]) || now > P.wrlUntil + 0.3 || now < P.rdAt) return;
        P.rdAt = now + FL("pwrldiagdt", 0.12);
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh)) return;
        const Ref& wc = P.wcomps[0];
        Vec3 br, bl, gr, gl, bw, go;
        bool ok = rd_sock(mesh, "jnt_r_hand", br) && rd_sock(mesh, "jnt_l_hand", bl) && rd_sock(wc, "jnt_r_hand", gr) && rd_sock(wc, "jnt_l_hand", gl);
        rd_sock(mesh, "jnt_weapon", bw);
        rd_sock(wc, "jnt_offset", go);
        std::string mg;
        std::vector<Ref> all(P.wcomps.begin() + 1, P.wcomps.end());
        for (size_t i = 0; i < all.size() && mg.size() < 900; i++)
        {
            const Ref& sc = all[i];
            if (!valid(sc)) continue;
            Call ks(sc, L"GetAttachSocketName");
            std::string sn = ks.run() ? ks.rn() : std::string();
            if (sn.find("magaz") == std::string::npos && sn.find("clip") == std::string::npos && sn.find("belt") == std::string::npos && sn.find("shell") == std::string::npos && sn.find("bullet") == std::string::npos) continue;
            Vec3 cl{}, sl{};
            Call g(sc, L"K2_GetComponentLocation");
            if (!(g.run() && g.rv(cl)) || !rd_sock(wc, sn.c_str(), sl)) continue;
            bool vis = false;
            bcall(sc, L"IsVisible", vis);
            mg += fmt(" %s:siga=%.1f mao_esq=%.1f vis=%d", sn.c_str(), rd_dist(cl, sl), rd_dist(cl, bl), vis ? 1 : 0);
        }
        int gate = x0::is_a(wc, L"SkeletalMeshComponent") ? mesh_kids_gate(wc.address(), false) : -2;
        LOG(fmt("NOLA rrdiag: t=%.2f n=%d ok=%s dR=%.1f dL=%.1f maos=%.1f arma_osso=%.1f offset_mao=%.1f portao=%d%s", now - P.wrlT0, P.rdN++, tf(ok), rd_dist(br, gr), rd_dist(bl, gl), rd_dist(br, bl), rd_dist(bw, go), rd_dist(go, br), gate, mg.c_str()));
    }
    static bool rl_ammo_part(const std::string& path)
    {
        return path.find("bullet") != std::string::npos && path.find("shell") == std::string::npos;
    }
    void rl_ammo(Body& P)
    {
        double now = x0::now();
        bool live = now < P.wrlUntil && !P.wcomps.empty() && valid(P.wcomps[0]);
        std::string fam = lg::pa_famc(C, P.wstr.value_or(""));
        bool rifle = live && !fam.empty() && fam != "pt";
        bool wantAmmo = rifle && FL("pwrlammo", 0) == 1 && FL("pupammo", 1) == 1;
        if (wantAmmo != P.rlAmmo)
        {
            int n = 0;
            for (size_t i = 1; i < P.wcomps.size(); i++)
            {
                const Ref& sc = P.wcomps[i];
                if (!valid(sc)) continue;
                Ref sm = pref(sc, "StaticMesh");
                if (!valid(sm) || !rl_ammo_part(sfull(sm))) continue;
                Call(sc, L"SetVisibility").b(L"bNewVisibility", wantAmmo).b(L"bPropagateToChildren", false).run();
                n++;
            }
            P.rlAmmo = wantAmmo;
            if (n) LOG(fmt("NOLA pupanim: municao da arma na recarga %s (%d pecas)", wantAmmo ? "visivel" : "escondida", n));
        }
    }
    void pup_weapon(Body& P)
    {
        pup_kids(P);
        if (!P.wcomps.empty() && valid(P.wcomps[0])) rl_hand(P);
        rl_ammo(P);
        rl_diag(P);
        bolt_place(P);
        knife_place(P);
        if (FL("pwsnap", 0) == 1) pw_snap(P);
        std::string x1;
        x0::rpart("@W", x1);
        size_t h = x1.find('#');
        if (h != std::string::npos) x1 = x1.substr(0, h);
        if (x1.compare(0, 3, "W2~") == 0)
        {
            std::string t;
            x1 = x0::look_get(x1.substr(3), t) ? t : std::string();
        }
        if (FL("pupweapon", 1) != 1) x1 = "W1~~";
        if (x1.empty() || (P.wstr && x1 == *P.wstr)) return;
        double wnow = x0::now();
        bool wlive = wnow < P.wrlUntil;
        if (P.wstr && lg::wrl_hold(C, *P.wstr, x1, wlive)) return;
        if (wlive && !(P.wstr && lg::w_same_part(*P.wstr, x1))) P.wrlUntil = 0;
        if (P.wstr)
        {
            if (auto e = lg::pa_equip_swap(C, *P.wstr, x1))
            {
                P.eqSt = e;
                P.eqUntil = x0::now() + FL("paeqt", 0.8);
                LOG("NOLA pupanim: troca de arma " + lg::pa_famc(C, *P.wstr) + " -> " + lg::pa_famc(C, x1) + " -> " + *P.eqSt);
            }
        }
        P.wstr = x1;
        for (const Ref& c : P.wcomps)
            if (valid(c)) Call(c, L"K2_DestroyComponent").o(L"Object", c).run();
        P.wcomps.clear();
        P.rlAmmo = false;
        std::string det;
        bool hasDet = lg::w_det(x1, det) && FL("padet", 1) == 1;
        if (hasDet) P.det = det;
        else P.det.reset();
        pup_gun(P, x1);
        if (wnow < P.wrlUntil) wrl_play(P, wnow - P.wrlT0);
        if (hasDet) pup_det(P, det);
    }

    void pup_det(Body& P, const std::string& det)
    {
        std::string path = det.empty() ? std::string(lg::kDetDef) : det;
        Ref da = asset(path);
        if (!valid(da)) { LOG("NOLA puppet: mesh do detector indisponivel " + path); return; }
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh)) return;
        std::string sock = FS("padetsock");
        if (sock.empty() || sock == "nil") sock = "jnt_item";
        bool hasSock = false;
        {
            std::wstring ws = W(sock);
            Call d(mesh, L"DoesSocketExist");
            d.n(L"InSocketName", ws.c_str());
            if (d.run()) hasSock = d.rb();
        }
        Ref dc = add_comp(P.pawn, L"/Script/Engine.PoseableMeshComponent", true);
        if (!valid(dc)) { LOG("NOLA puppet: AddComponentByClass do detector falhou"); return; }
        Call(dc, L"SetSkinnedAssetAndUpdate").o(L"NewMesh", da).b(L"bReinitPose", true).run();
        attach(dc, mesh, sock);
        Call(dc, L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", Vec3{FL("padetx", 0), FL("padety", 0), FL("padetz", 0)})
            .r(L"NewRotation", Rot{FL("padetp", 0), FL("padetw", 0), FL("padetr", 0)}).b(L"bSweep", false).b(L"bTeleport", true).run();
        P.wcomps.push_back(dc);
        LOG(fmt("NOLA puppet: detector remoto %s modelo=%s soquete=%s (existe=%s)", leaf_dot(path).c_str(), lg::det_model(path).c_str(), sock.c_str(), tf(hasSock)));
    }

    void pup_gun(Body& P, const std::string& x1)
    {
        std::string part, statics;
        P.wBolt = false;
        P.wKnife = false;
        if (!lg::w1_split(x1, part, statics) || part.empty()) { LOG("NOLA puppet: arma remota = nenhuma"); return; }
        lg::WPart wp = lg::w_part(part);
        if (!wp.ok) return;
        Ref wa = asset(wp.pth);
        if (!valid(wa)) { LOG("NOLA puppet: mesh da arma indisponivel " + wp.pth); return; }
        Ref mesh = pref(P.pawn, "Mesh");
        if (!valid(mesh)) return;
        std::string sock = lg::w_sock(C, wp.pth, wp.psock);
        bool hasSock = false;
        {
            std::wstring ws = W(sock);
            Call d(mesh, L"DoesSocketExist");
            d.n(L"InSocketName", ws.c_str());
            if (d.run()) hasSock = d.rb();
        }
        std::wstring wcls = W(lg::w_cls(C));
        Ref wc = add_comp(P.pawn, wcls.c_str(), true);
        if (!valid(wc)) { LOG("NOLA puppet: AddComponentByClass da arma falhou (corpo invalido?)"); return; }
        Call(wc, L"SetSkinnedAssetAndUpdate").o(L"NewMesh", wa).b(L"bReinitPose", true).run();
        if (x0::is_a(wc, L"SkeletalMeshComponent")) Call(wc, L"SetCollisionEnabled").i(L"NewType", 0).run();
        Ref wsk = ocall(wa, L"GetSkeleton");
        P.wskel = valid(wsk) ? x0::obj_name(wsk) : std::string();
        attach(wc, mesh, sock);
        P.wBolt = lg::w_bolt(C, wp.pth);
        P.wKnife = lg::w_knife(C, wp.pth);
        P.bKey.clear();
        P.wcomps.push_back(wc);
        P.wsock = sock;
        bolt_place(P);
        knife_place(P);
        P.wsComp = wc.address();
        P.wsHand = false;
        std::map<std::string, Ref> byName;
        byName[wp.pname] = wc;
        int ns = 0;
        std::vector<std::string> pend = lg::split_nonempty(statics, '|');
        for (int round = 0; round < 5; round++)
        {
            std::vector<std::string> rest;
            for (const std::string& ent : pend)
            {
                lg::WStatic sd = lg::w_static(ent);
                if (sd.ok && byName.count(sd.par))
                {
                    Ref sa = asset(sd.path);
                    if (!valid(sa)) continue;
                    if (sd.v.size() < 9) continue;
                    bool allv = true;
                    for (int i = 0; i < 9; i++) if (!sd.v[i]) allv = false;
                    if (!allv) continue;
                    Ref sc = add_comp(P.pawn, L"/Script/Engine.StaticMeshComponent", true);
                    if (!valid(sc)) continue;
                    Call(sc, L"SetStaticMesh").o(L"NewMesh", sa).run();
                    Call(sc, L"SetCollisionEnabled").i(L"NewType", 0).run();
                    attach(sc, byName[sd.par], sd.sock);
                    auto& v = sd.v;
                    Call(sc, L"K2_SetRelativeLocationAndRotation").v(L"NewLocation", Vec3{*v[0], *v[1], *v[2]}).r(L"NewRotation", Rot{*v[3], *v[4], *v[5]}).b(L"bSweep", false).b(L"bTeleport", true).run();
                    Call(sc, L"SetRelativeScale3D").v(L"NewScale3D", Vec3{*v[6], *v[7], *v[8]}).run();
                    if (FL("pupammo", 1) == 1 && (sd.path.find("bullet") != std::string::npos || sd.path.find("shell") != std::string::npos))
                        Call(sc, L"SetVisibility").b(L"bNewVisibility", false).b(L"bPropagateToChildren", false).run();
                    byName[sd.nm] = sc;
                    ns++;
                    P.wcomps.push_back(sc);
                }
                else if (sd.ok) rest.push_back(ent);
            }
            pend = rest;
            if (pend.empty()) break;
        }
        LOG(fmt("NOLA puppet: arma remota %s soquete=%s (existe=%s) estaticos=%d sem_pai=%d classe=%s esqueleto=%s recarga=%s", leaf_dot(wp.pth).c_str(), sock.c_str(), tf(hasSock), ns, (int)pend.size(),
                lg::leaf_of(lg::w_cls(C), '.').c_str(), P.wskel.c_str(), tf(lg::wrl_ent(P.wskel) != nullptr)));
        rl_hand(P);
    }

    int wd_state(const Ref& pawn)
    {
        Call c(pawn, L"GetWoundedState_BP");
        return c.run() ? (int)c.ri() : -1;
    }
    bool pup_god(Body& P, bool on)
    {
        if (!P.uid) P.uid = guid_of(P.pawn);
        Ref b = x0::first_of(L"CustomConsoleManagerBuild");
        bool ok = P.uid && valid(b) && Call(b, L"XSetEntityUnkillableByUID").i(L"EntityUID", *P.uid).b(L"bGodModeState", on).run();
        if (ok) P.wdGod = on;
        return ok;
    }
    float pup_hp(const Ref& pawn)
    {
        float f = -1;
        fcall(pawn, L"GetHP", f);
        return f;
    }
    void pup_down(Body& P, bool down, const char* why, double now)
    {
        if (!P.wdFor.same(P.pawn))
        {
            P.wdFor = P.pawn;
            P.wd = P.wdOut = P.wdGod = false;
            P.wdTry = 0;
        }
        if (!valid(P.pawn)) return;
        bool want = down && FL("downanim", 1) == 1;
        if (want && !P.wd)
        {
            P.wd = true;
            P.wdOut = false;
            int ty = (int)FL("downanimtype", 2);
            bool god = pup_god(P, true);
            bool ok = Call(P.pawn, L"FallWounded").i(L"NewWoundedType", ty).run();
            if (P.pm.clip) pm_clip_stop(P.pm, pm_ai(P));
            LOG(fmt("NOLA puppet: outro jogador caido -> FallWounded(%d) ok=%s estado=%d hp=%.0f imortal=%s uid=%s", ty, tf(ok), wd_state(P.pawn), pup_hp(P.pawn), tf(god), ostr(P.uid).c_str()));
            lg::wd_arm(P.ww, now);
            P.wwAt = now + 0.5;
            return;
        }
        if (want && P.wd)
        {
            if (FL("downwatch", 1) == 1 && now >= P.wwAt)
            {
                P.wwAt = now + 0.5;
                std::string m = lg::wd_watch(P.ww, wd_state(P.pawn), pup_hp(P.pawn), now, 35);
                if (!m.empty()) LOG(m);
            }
            return;
        }
        if (!want && P.wd)
        {
            P.wd = false;
            P.wdOut = true;
            P.wdTry = 0;
            P.wdAt = 0;
            double mh = 0;
            bool hpOk = max_hp(P.pawn, mh) && Call(P.pawn, L"ForceSetHP").i(L"HP", (int64_t)mh).run();
            bool god = P.wdGod ? pup_god(P, false) : true;
            LOG(fmt("NOLA puppet: fim do caido (%s) -> HP %.0f ok=%s imortal desligado ok=%s", why, mh, tf(hpOk), tf(god)));
        }
        if (!P.wdOut || now < P.wdAt) return;
        int st = wd_state(P.pawn);
        if (st == 0 || st == 3 || st < 0 || P.wdTry >= 10)
        {
            P.wdOut = false;
            LOG(fmt("NOLA puppet: levantar concluido estado=%d tentativas=%d", st, P.wdTry));
            return;
        }
        bool ok = Call(P.pawn, L"WakeUpWounded").b(L"bHeal", false).run();
        P.wdTry++;
        P.wdAt = now + 0.5;
        LOG(fmt("NOLA puppet: WakeUpWounded ok=%s estado antes=%d tentativa=%d", tf(ok), st, P.wdTry));
    }

    std::optional<double> vz_trace(double x, double y, double z0, double z1)
    {
        Ref ksl = x0::find_path(L"/Script/Engine.Default__KismetSystemLibrary");
        Ref w = x0::world();
        if (!valid(ksl) || !valid(w)) return std::nullopt;
        std::vector<void*> skip;
        Ref lp = local_pawn();
        if (valid(lp)) skip.push_back(lp.get());
        for (const Pl& p : S().pl)
        {
            if (valid(p.tgt.pawn)) skip.push_back(p.tgt.pawn.get());
            if (valid(p.pup.pawn)) skip.push_back(p.pup.pawn.get());
        }
        struct Arr
        {
            void* d;
            int32_t n, m;
        } ign{skip.empty() ? nullptr : skip.data(), static_cast<int32_t>(skip.size()), static_cast<int32_t>(skip.size())};
        Call c(ksl, L"LineTraceSingle");
        c.o(L"WorldContextObject", w).v(L"Start", Vec3{x, y, z0}).v(L"End", Vec3{x, y, z1}).i(L"TraceChannel", 0).b(L"bTraceComplex", false)
            .raw(L"ActorsToIgnore", &ign, static_cast<int>(sizeof ign)).i(L"DrawDebugType", 0).b(L"bIgnoreSelf", true).f(L"DrawTime", 0.0f);
        if (!c.run() || !c.rb()) return std::nullopt;
        Vec3 ip;
        if (!c.rv(ip, L"OutHit.ImpactPoint")) return std::nullopt;
        return ip.z;
    }

    void vz_set(Body& P, double nc)
    {
        if (nc != P.vzCur && valid(P.vzMesh))
            Call(P.vzMesh, L"K2_SetRelativeLocation").v(L"NewLocation", Vec3{P.vzBase.x, P.vzBase.y, P.vzBase.z + nc}).b(L"bSweep", false).b(L"bTeleport", true).run();
        P.vzCur = nc;
    }

    void vz_begin(Body& P, double now)
    {
        Ref mesh = pref(P.pawn, "Mesh");
        Vec3 A;
        Rot R;
        if (!valid(mesh) || !loc(P.pawn, A) || !rot(P.pawn, R)) return;
        if (!P.vzMesh.same(mesh) || P.vzCur == 0)
        {
            Vec3 b;
            if (!x0::prop<Vec3>(mesh, "RelativeLocation", b)) return;
            P.vzMesh = mesh;
            P.vzBase = b;
            P.vzCur = 0;
        }
        Ref cap = pref(P.pawn, "CapsuleComponent");
        float h = 0;
        P.vzHh = (valid(cap) && fcall(cap, L"GetScaledCapsuleHalfHeight", h) && h > 10) ? (double)h : P.pfH.value_or(90);
        double feet = A.z - P.vzHh, up = FL("pavtzup", 150), yr = R.yaw * 3.14159265358979 / 180;
        P.vzOx = A.x;
        P.vzOy = A.y;
        P.vzDx = std::cos(yr);
        P.vzDy = std::sin(yr);
        std::vector<std::pair<double, std::optional<double>>> pts;
        double scan = FL("pavtzscan", 250);
        for (double d = 0; d <= scan; d += 10) pts.push_back({d, vz_trace(A.x + P.vzDx * d, A.y + P.vzDy * d, feet + up, feet - 10)});
        P.vzS = lg::vz_scan(pts, feet, up, FL("pavtzmin", 25));
        P.vzOn = true;
        P.vzAt = now;
        P.vzMax = 0;
        if (++P.vzLog <= 50)
        {
            if (P.vzS.ok) LOG(fmt("NOLA vaultz: boneco salta: obstaculo de %.0f a %.0f cm a frente (yaw %.0f), topo %.0f cm acima dos pes -> corpo passa por cima", P.vzS.d0, P.vzS.d1, R.yaw, P.vzS.top - feet));
            else LOG(fmt("NOLA vaultz: boneco salta: nenhum obstaculo ate %.0f cm a frente (yaw %.0f) -> sem elevacao", scan, R.yaw));
        }
    }

    void vault_lift(Body& P, double now)
    {
        if (FL("pavtz", 1) != 1)
        {
            if (P.vzCur != 0) vz_set(P, 0);
            P.vzOn = false;
            return;
        }
        bool vt = P.acSt && *P.acSt == "acvt";
        if (vt && P.acTok && P.acTok != P.vzTok)
        {
            P.vzTok = P.acTok;
            vz_begin(P, now);
        }
        if (vt) P.vzUntil = std::max(P.vzUntil, P.acUntil + FL("pavtzt", 0.4));
    }

    void vz_frame()
    {
        double now = x0::now();
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            Body& P = S().pl[v].pup;
            if (!P.vzOn && P.vzCur == 0) continue;
            CvScope cv(v);
            if (!valid(P.pawn) || !valid(P.vzMesh) || !P.vzMesh.same(pref(P.pawn, "Mesh")))
            {
                P.vzOn = false;
                P.vzCur = 0;
                P.vzMesh = Ref{};
                continue;
            }
            bool on = P.vzOn && now < P.vzUntil && FL("pavtz", 1) == 1;
            double want = 0;
            Vec3 A;
            if (on && loc(P.pawn, A))
            {
                double s = (A.x - P.vzOx) * P.vzDx + (A.y - P.vzOy) * P.vzDy;
                want = lg::vz_lift(P.vzS, s, A.z - P.vzHh, FL("pavtzr", 40), FL("pavtzramp", 60), FL("pavtzm", 5));
            }
            double dt = P.vzAt > 0 ? now - P.vzAt : 0;
            P.vzAt = now;
            vz_set(P, lg::vz_step(P.vzCur, want, dt, FL("pavtzrate", 800)));
            P.vzMax = std::max(P.vzMax, P.vzCur);
            if (!on && P.vzOn)
            {
                P.vzOn = false;
                if (P.vzLog <= 50) LOG(fmt("NOLA vaultz: boneco salto terminou: elevacao do corpo max %.0f cm", P.vzMax));
            }
        }
    }

    void pup_tick()
    {
        Pl& s = V();
        Body& P = s.pup;
        lg::RSnap r;
        bool hr = remote_snap(r);
        Vec3 lp;
        bool hl = local_pos(lp);
        if (hr && FL("kzsafe", 0) == 1 && !remote_in(r, "boneco")) { kz_hold(); return; }
        if (!(hr && hl)) return;
        double now = x0::now();
        Far& F = s.far;
        if (FL("farbody", 0) == 1)
        {
            double d2 = (r.x - lp.x) * (r.x - lp.x) + (r.y - lp.y) * (r.y - lp.y);
            if (FL("kzsafe", 0) == 1) d2 += (r.z - lp.z) * (r.z - lp.z);
            double pf = FL("pupfar", 6000);
            if (!F.on && d2 > pf * pf)
            {
                F.on = true;
                LOG(fmt("NOLA farbody: remoto a %.0f m -> corpo distante", std::sqrt(d2) / 100));
            }
            else if (F.on && d2 < (pf - 1000) * (pf - 1000))
            {
                F.on = false;
                far_hide(F);
                P.force = false;
                LOG(fmt("NOLA farbody: remoto a %.0f m -> boneco NPC de volta", std::sqrt(d2) / 100));
            }
        }
        else if (F.on) { F.on = false; far_hide(F); }
        if (F.on) { far_tick(F, r, lp, now); return; }
        bool meshOk = true;
        if (FL("bodysafe", 1) == 1 && has(P.pawn) && (FL("kzsafe", 0) != 1 || valid(P.pawn)))
        {
            meshOk = valid(pref(P.pawn, "Mesh"));
            if (P.broken) meshOk = false;
        }
        if (!meshOk)
        {
            LOG("NOLA puppet: malha nula ou chamada nullptr (corpo guardado pelo jogo?); referencia largada, sera recriado");
            if (P.path) P.bad.insert(*P.path);
            P.pawn.reset();
            P.path.reset();
            P.broken = false;
            P.state.reset();
            P.retryAt = now + 2;
            body_lost();
        }
        if (!body_ok(P))
        {
            if (has(P.pawn))
            {
                LOG("NOLA puppet: pawn invalido; sera recriado");
                P.pawn.reset();
                P.path.reset();
                P.retryAt = now + 2;
                body_lost();
            }
            if (FL("avatar", 1) == 1 && now - r.recv < 5) pup_spawn(P, r, lp, now, "Nola_RemotePlayer", 90000, 1e9, "puppet");
            return;
        }
        pup_uro(P);
        cm_off(P, now);
        double age = now - r.recv;
        auto life = remote_life();
        bool away = age > 5 || FL("avatar", 1) != 1 || (life && *life == "d") || remote_cine();
        if (away != P.hidden)
        {
            P.hidden = away;
            bool ok = hide_actor(P.pawn, away);
            LOG(std::string("NOLA puppet: escondido=") + tf(away) + " ok=" + tf(ok));
        }
        {
            bool dn = life && *life == "w";
            const char* why = FL("downanim", 1) != 1 ? "downanim desligado" : (life && *life == "d") ? "morte" : age > 5 ? "sem dados do outro jogador" : remote_cine() ? "cinematica" : FL("avatar", 1) != 1 ? "avatar desligado" : "revivido";
            pup_down(P, dn && !away, why, now);
        }
        if (away) return;
        ab_tick(P);
        auto v = lg::r_vel(C, s.rvel, r, now);
        lg::PupStepIn in;
        in.now = now;
        in.recv = r.recv;
        in.rx = r.x;
        in.ry = r.y;
        in.ryaw = r.yaw;
        in.vx = v.first;
        in.vy = v.second;
        in.tz = pf_z(P, r);
        in.pa_yaw = pa_yaw(P, r, now - r.recv > 0.6 ? 0.0 : std::sqrt(v.first * v.first + v.second * v.second), now);
        lg::PupStepOut o = lg::pup_step(C, P.pos, P.yaw, P.lastT, in);
        if (o.jump)
        {
            s.jumpUntil = now + 1.5;
            LOG(fmt("NOLA puppet: salto de %.0f m; corpos movidos de uma vez, leitura de dano suspensa 1.5 s", o.jump_m));
        }
        lg::sm_stat(C, s.sm, r.recv, now, o.step);
        if (FL("pupfix", 0) == 1 && pfx_tick(P, o.speed, now)) return;
        if (FL("pupmove", 0) > 0 && FL("pupanimbp", 0) == 1) pup_move(P, o.vx, o.vy, o.speed, now, r);
        else
        {
            if (P.pm.on) pm_off(P);
            bool nsOn = ns_pup(P, &r, now);
            if (!nsOn && !set_loc(P.pawn, Vec3{P.pos.x, P.pos.y, P.pos.z})) return;
            if (!P.nsYaw && P.yaw && !yaw_frame(P, now)) set_yaw(P.pawn, *P.yaw);
            pup_anim(P);
            if (!nsOn) P.pmW = P.pos;
            else P.pmW.reset();
        }
        vault_lift(P, now);
        pup_weapon(P);
    }

    int g_hitN = 0;
    std::optional<double> hit_rel(const Ref& pawn, std::string& why)
    {
        Ref ai;
        uint8_t e = 0;
        for (const Ref& c : comps(pawn, L"/Script/Engine.SkeletalMeshComponent"))
        {
            if (!valid(c)) continue;
            Ref a = ocall(c, L"GetAnimInstance");
            if (valid(a) && x0::prop<uint8_t>(a, "HitData.HitAnimDirection", e)) { ai = a; break; }
        }
        float hd = 0, cd = 0;
        bool he = valid(ai);
        bool hdok = he && x0::prop<float>(ai, "HitData.HitDirection", hd);
        bool cdok = he && x0::prop<float>(ai, "HitData.CalculatedHitDirection", cd);
        why = fmt("enum=%s dir=%s calc=%s", he ? lg::inum(e).c_str() : "nil", hdok ? num(hd).c_str() : "nil", cdok ? num(cd).c_str() : "nil");
        int src = (int)FL("pahitsrc", 1);
        if (src == 1 && he) return lg::hit_enum_deg(e);
        if (src == 2 && hdok) return (double)hd;
        if (src == 3 && cdok) return (double)cd;
        return std::nullopt;
    }
    void hit_tgt(Body& P, const Body& T, const lg::RSnap& r, double amount, double now, const std::optional<double>& relO = std::nullopt, int zoneO = -1)
    {
        if (!lg::hit_big(C, amount))
        {
            if (!lg::hit_gate(C, P.hitAt, amount, now, FL("pahitltcd", 0.4))) return;
            Rot tr;
            std::string why;
            auto rel = relO ? relO : hit_rel(T.pawn, why);
            uint8_t zone = 1;
            if (zoneO >= 0) zone = (uint8_t)zoneO;
            else { Ref tai = pm_ai(T); if (valid(tai)) x0::prop<uint8_t>(tai, "HitData.LastHittedBodyPart", zone); }
            const char* side = (rel && rot(T.pawn, tr) && r.yaw) ? lg::hit_partner(*rel, tr.yaw, *r.yaw) : nullptr;
            std::string id = side ? lg::hit_light_id(zone, side) : std::string();
            if (side)
            {
                P.hitSeq = P.hitSeq % 999 + 1;
                P.hitTok = id + lg::inum(P.hitSeq);
            }
            g_hitN++;
            if (g_hitN <= 50 || g_hitN % 50 == 0)
                LOG(fmt("NOLA pupanim: golpe leve no alvo %.1f zona %d lado %s %s -> %s (vez %d)", amount, (int)zone, side ? side : "nil", why.c_str(), side ? P.hitTok->c_str() : "nada", g_hitN));
            return;
        }
        if (!lg::hit_gate(C, P.hitAt, amount, now)) return;
        std::string why;
        Rot tr;
        auto rel = relO ? relO : hit_rel(T.pawn, why);
        bool hr = rot(T.pawn, tr);
        const char* side = (rel && hr && r.yaw) ? lg::hit_partner(*rel, tr.yaw, *r.yaw) : nullptr;
        if (side)
        {
            P.hitSeq = P.hitSeq % 999 + 1;
            P.hitTok = std::string("g") + side + lg::inum(P.hitSeq);
        }
        g_hitN++;
        if (g_hitN <= 50 || g_hitN % 50 == 0)
            LOG(fmt("NOLA pupanim: golpe no alvo %.1f lado %s %s alvo_yaw=%s parceiro_yaw=%s -> %s (fonte %d, vez %d)", amount, side ? side : "nil", why.c_str(),
                    hr ? num(tr.yaw).c_str() : "nil", r.yaw ? num(*r.yaw).c_str() : "nil", P.hitTok && side ? P.hitTok->c_str() : "nada", (int)FL("pahitsrc", 1), g_hitN));
    }
    void tgt_still(Body& T)
    {
        Ref ai = pm_ai(T);
        if (FL("tgtstill", 1) != 1)
        {
            if (T.rmFor.same(T.pawn) && T.rm0 && valid(ai))
            {
                bool ok = x0::prop_set<uint8_t>(ai, "RootMotionMode", (uint8_t)*T.rm0);
                LOG("NOLA tgtstill: desligado -> RootMotionMode do alvo " + lg::inum(*T.rm0) + " devolvido ok=" + tf(ok));
            }
            T.rmFor.reset();
            T.rm0.reset();
            return;
        }
        if (!valid(ai)) return;
        uint8_t rm = 0;
        bool got = x0::prop<uint8_t>(ai, "RootMotionMode", rm);
        if (!T.rmFor.same(T.pawn))
        {
            T.rmFor = T.pawn;
            T.rm0.reset();
            if (got) T.rm0 = rm;
        }
        if (!got || rm != 1)
        {
            bool ok = x0::prop_set<uint8_t>(ai, "RootMotionMode", (uint8_t)1);
            T.rmN++;
            if (T.rmN <= 3 || T.rmN % 50 == 0) LOG("NOLA tgtstill: alvo " + x0::obj_name(T.pawn) + " RootMotionMode " + (got ? lg::inum(rm) : std::string("nil")) + " -> 1 (IgnoreRootMotion) ok=" + tf(ok) + " vez=" + lg::inum(T.rmN));
        }
        Ref cm = pref(T.pawn, "CharacterMovement");
        Vec3 v;
        if (!valid(cm) || !x0::prop<Vec3>(cm, "Velocity", v) || v.x * v.x + v.y * v.y + v.z * v.z < 1) return;
        x0::call0(cm, L"StopMovementImmediately");
        T.stopN++;
        if (T.stopN <= 3 || T.stopN % 50 == 0) LOG(fmt("NOLA tgtstill: alvo %s andava parado no lugar a %.0f cm/s -> StopMovementImmediately (vez %d)", x0::obj_name(T.pawn).c_str(), std::sqrt(v.x * v.x + v.y * v.y), T.stopN));
    }

    void tgt_walk(Body& T)
    {
        Ref cm = pref(T.pawn, "CharacterMovement");
        uint8_t mm = 0;
        std::optional<int> cur;
        if (valid(cm) && x0::prop<uint8_t>(cm, "MovementMode", mm)) cur = mm;
        auto nm = lg::tgt_mode(C, cur, T.walkFor.same(T.pawn));
        if (!nm) return;
        bool ok = Call(cm, L"SetMovementMode").i(L"NewMovementMode", *nm).i(L"NewCustomMode", 0).run();
        if (*nm == 1) T.walkFor = T.pawn; else T.walkFor.reset();
        T.walkN++;
        if (T.walkN <= 3 || T.walkN % 50 == 0) LOG(fmt("NOLA tgtwalk: alvo %s MovementMode %d -> %d ok=%s (vez %d)", x0::obj_name(T.pawn).c_str(), (int)mm, *nm, tf(ok), T.walkN));
    }

    void tgt_cmc(Body& T)
    {
        Ref cm = pref(T.pawn, "CharacterMovement");
        bool a = false;
        std::optional<bool> act;
        if (valid(cm) && x0::prop<bool>(cm, "bIsActive", a)) act = a;
        auto on = lg::tgt_cmc(C, act, T.cmcOff.same(T.pawn));
        if (!on) return;
        bool ok = *on ? Call(cm, L"Activate").b(L"bReset", true).run() : x0::call0(cm, L"Deactivate");
        if (*on) T.cmcOff.reset(); else T.cmcOff = T.pawn;
        T.cmcN++;
        if (T.cmcN <= 3 || T.cmcN % 50 == 0) LOG(fmt("NOLA tgtnone: alvo %s CharacterMovement %s ok=%s (vez %d)", x0::obj_name(T.pawn).c_str(), *on ? "Activate" : "Deactivate", tf(ok), T.cmcN));
    }

    void tgt_tick()
    {
        St& g = S();
        Pl& s = V();
        Body& T = s.tgt;
        Body& P = s.pup;
        lg::RSnap r;
        bool hr = remote_snap(r);
        Vec3 lp;
        bool hl = local_pos(lp);
        if (hr && FL("kzsafe", 0) == 1 && !remote_in(r, "alvo")) { tg_park("kz"); return; }
        if (!(hr && hl)) return;
        double now = x0::now();
        if (s.far.on) { tg_park("longe"); return; }
        tg_back();
        if (!body_ok(T))
        {
            if (has(T.pawn))
            {
                LOG("NOLA target: pawn invalido; sera recriado");
                tg_note("invalido", -1);
                T.pawn.reset();
                T.path.reset();
                T.setup = false;
                T.retryAt = now + 2;
                body_lost();
            }
            if (P.spawnAt || !body_ok(P)) return;
            if (FL("avatar", 1) == 1 && now - r.recv < 5) pup_spawn(T, r, lp, now, "Nola_RemoteTarget", 4000, 6000, "target");
            return;
        }
        if (!T.setup)
        {
            T.setup = true;
            T.god = false;
            T.godAt = 0;
            T.deadAt.reset();
            tg_note("pronto", -1);
            bool okH = hide_actor(T.pawn, true);
            LOG(std::string("NOLA target: escondido ok=") + tf(okH));
        }
        if (FL("tgtgod", 0) == 1 && now >= T.godAt)
        {
            T.godAt = now + 5;
            bool ok = false;
            if (!T.uid) T.uid = guid_of(T.pawn);
            if (T.uid)
            {
                Ref b = x0::first_of(L"CustomConsoleManagerBuild");
                ok = valid(b) && Call(b, L"XSetEntityUnkillableByUID").i(L"EntityUID", *T.uid).b(L"bGodModeState", true).run();
            }
            if (!T.god || !ok) LOG("NOLA target: Unkillable uid=" + ostr(T.uid) + " ok=" + tf(ok) + " " + (ok ? "" : "chamada falhou"));
            T.god = ok;
        }
        tgt_still(T);
        tgt_walk(T);
        tgt_cmc(T);
        lg::PupPos pos = P.pos;
        if (!pos.has) { pos.x = r.x; pos.y = r.y; pos.z = r.z; }
        if (!set_loc(T.pawn, Vec3{pos.x, pos.y, pos.z})) return;
        float hpf = 0;
        if (!fcall(T.pawn, L"GetHP", hpf)) return;
        double hp = hpf;
        bool alive = true;
        if (!bcall(T.pawn, L"IsAlive", alive)) return;
        bool dead = !alive;
        if (dead && T.god && FL("tgtgod", 0) == 1)
        {
            bool first = !T.deadAt;
            if (first) T.deadAt = now;
            if (now - *T.deadAt < FL("tgtgodwait", 2))
            {
                dead = false;
                if (first)
                {
                    T.held++;
                    if (T.held <= 5 || T.held % 50 == 0) LOG(fmt("NOLA target: golpe letal segurado (Unkillable) hp %.1f ref %s -> refill (%d vezes)", hp, ostrd(T.ref).c_str(), T.held));
                }
            }
        }
        else T.deadAt.reset();
        if (dead)
        {
            LOG(fmt("NOLA target: MORREU (hp %.1f, ref %s); sera recriado em 5 s", hp, ostrd(T.ref).c_str()));
            tg_note("morreu", hp);
            if (FL("bodysafe", 1) != 1) hide_actor(T.pawn, true);
            T.pawn.reset();
            T.path.reset();
            T.setup = false;
            T.ref.reset();
            T.retryAt = now + 5;
            return;
        }
        if (T.ref && hp < *T.ref - 0.05 && remote_down()) T.ignored += *T.ref - hp;
        else if (T.ref && hp < *T.ref - 0.05)
        {
            m01::Dmg& D = s.dmg;
            double amount = (*T.ref - hp) * FL("dmgscale", 1);
            lg::HitIn hi;
            hi.ref = *T.ref;
            hi.hp = hp;
            hi.now = now;
            hi.px = pos.x;
            hi.py = pos.y;
            hi.pz = pos.z;
            hi.jump_until = s.jumpUntil;
            hi.quiet = g.quiet;
            hi.shot = g.shot;
            auto why = lg::hit_why(C, hi, amount, T.realAt);
            if (why)
            {
                D.dropped++;
                if (D.dropped <= 20 || D.dropped % 50 == 0) LOG(fmt("NOLA target: dano %.1f IGNORADO (%s) ignorados=%d", amount, why->c_str(), (int)D.dropped));
            }
            else
            {
                D.seq++;
                D.total += amount;
                D.list.push_back(m01::DmgEvent{D.seq, amount, now});
                hit_tgt(P, T, r, amount, now);
                bool al = false;
                bcall(T.pawn, L"IsAlive", al);
                LOG(fmt("NOLA target: DANO seq=%d %.1f (hp %.1f -> %.1f) total=%.1f vivo=%s", (int)D.seq, amount, *T.ref, hp, D.total, tf(al)));
            }
        }
        if (hp < lg::kTgtHp - 0.05)
        {
            Call(T.pawn, L"ForceSetHP").i(L"HP", (int64_t)lg::kTgtHp).run();
            if (fcall(T.pawn, L"GetHP", hpf)) hp = hpf;
            float b = 0;
            if (fcall(T.pawn, L"GetBleeding", b) && b > 0 && Call(T.pawn, L"ForceSetBleeding").f(L"Bleeding", 0.0f).run())
            {
                T.bleedCleared++;
                float b2 = 0;
                fcall(T.pawn, L"GetBleeding", b2);
                if (T.bleedCleared <= 5 || T.bleedCleared % 50 == 0) LOG(fmt("NOLA target: sangramento %.2f -> %.2f (limpo %d vezes)", (double)b, (double)b2, T.bleedCleared));
            }
        }
        T.ref = hp;
    }

    void dup_hide(const Ref& o, const std::string& what)
    {
        St& s = S();
        bool h = true;
        if (!x0::prop<bool>(o, "bHidden", h)) h = true;
        if (h) return;
        bool ok = hide_actor(o, true);
        s.dgN++;
        if (s.dgN <= 20 || s.dgN % 50 == 0) LOG(fmt("NOLA AVATAR dup: %s estava visivel; escondido ok=%s (correcoes=%d)", what.c_str(), tf(ok), s.dgN));
    }
    void dup_guard()
    {
        St& s = S();
        if (FL("bodysafe", 1) == 1 && !s.wg.open) return;
        int nv = multi() ? x0::kMaxClients : 1;
        for (int v = 0; v < nv; ++v)
        {
            CvScope cv(v);
            Pl& p = V();
            Body& T = p.tgt;
            Body& P = p.pup;
            Far& F = p.far;
            if (body_held(T)) dup_hide(T.pawn, "alvo oculto " + x0::obj_name(T.pawn));
            for (Body* Q : {&T, &P})
            {
                if (!(Q->spawnAt && Q->hasBefore && Q->hasSx && !(FL("pupretry", 1) == 1 && Q == &P))) continue;
                for (const Ref& o : x0::all_of(L"Pawn"))
                {
                    if (!valid(o) || is_body(o)) continue;
                    std::string full = x0::full_name(o);
                    if (Q->before.count(full) || full.find("BP_AI_Human_C") == std::string::npos || loot_body(o)) continue;
                    double mh;
                    if (!max_hp(o, mh) || mh < Q->hpLo || mh > Q->hpHi) continue;
                    Vec3 q;
                    if (!loc(o, q)) continue;
                    if ((q.x - Q->sx) * (q.x - Q->sx) + (q.y - Q->sy) * (q.y - Q->sy) < 800.0 * 800.0) dup_hide(o, "corpo novo antes da adocao " + x0::obj_name(o));
                }
            }
            bool farOn = F.on;
            if (farOn && body_held(P)) dup_hide(P.pawn, "boneco NPC (modo distante)");
            if (!farOn && valid(F.actor)) dup_hide(F.actor, "corpo distante (modo perto)");
        }
        double now = x0::now();
        if (now < s.dgSweepAt) return;
        s.dgSweepAt = now + 2;
        std::vector<lg::RSnap> rs;
        bool allHeld = true;
        for (int v = 0; v < nv; ++v)
        {
            CvScope cv(v);
            lg::RSnap r;
            if (V().gone || !remote_snap(r)) continue;
            rs.push_back(r);
            if (!body_held(V().pup)) allHeld = false;
        }
        if (rs.empty()) return;
        for (const Ref& o : x0::all_of(L"BP_AI_Human_C"))
        {
            if (!valid(o)) continue;
            if (loot_body(o))
            {
                auto& f = s.ext.loot_reshow;
                if (f) f(o);
                continue;
            }
            if (is_body(o)) continue;
            if (FL("pupretry", 1) == 1)
            {
                bool fresh = false;
                for (const Pl& p : s.pl)
                    if (p.pup.spawnAt && p.pup.hasBefore && !p.pup.before.count(x0::full_name(o))) fresh = true;
                if (fresh) continue;
            }
            double mh;
            if (!max_hp(o, mh)) continue;
            if (mh == 100000 || mh == 5000)
            {
                Vec3 q;
                if (!loc(o, q)) continue;
                bool near = false;
                for (const lg::RSnap& r : rs)
                    if ((q.x - r.x) * (q.x - r.x) + (q.y - r.y) * (q.y - r.y) < 300.0 * 300.0) near = true;
                if (near) dup_hide(o, "corpo extra " + x0::obj_name(o) + " maxhp=" + num(mh));
                else if (FL("pupadopt", 1) == 1 && mh == 100000 && allHeld) dup_hide(o, "boneco sobrando " + x0::obj_name(o));
                if (FL("pupadopt", 1) == 1 && mh == 100000)
                {
                    uint64_t a = o.address();
                    if (!s.nocol.count(a)) { s.nocol.insert(a); no_collision(o); }
                }
            }
        }
    }

    void ns_tick()
    {
        St& s = S();
        double t0 = x0::now(), now = t0;
        for (Pl& p : s.pl)
        {
            if (!p.hasCmo) continue;
            const Ref& cur = p.pup.pawn;
            if (!(valid(cur) && cur.address() == p.cmo.addr))
            {
                if (now - p.cmo.at < 0.25 && valid(p.cmo.a))
                {
                    Ref cm = pref(p.cmo.a, "CharacterMovement");
                    if (valid(cm)) Call(cm, L"SetComponentTickEnabled").b(L"bEnabled", true).run();
                }
                p.hasCmo = false;
            }
        }
        Ns& N0 = s.ns;
        if (FL("nsmooth", 0) != 1)
        {
            if (N0.made && N0.live)
            {
                for (Pl& p : s.pl) p.pup.nsYaw = false;
                for (auto& kv : N0.k)
                    if (valid(kv.second.a) && kv.second.a.address() == kv.second.addr) ns_stop(kv.second);
                N0.k.clear();
                N0.live = false;
                LOG("NOLA nsmooth: desligado -> movedores zerados, espelhos voltam ao main.lua");
            }
            return;
        }
        Ns& N = ns_st();
        N.live = true;
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            Body& P = s.pl[v].pup;
            std::string key = pup_key(v);
            auto it = N.k.find(key);
            if (it != N.k.end())
            {
                GM& K = it->second;
                const Ref& cur = P.pawn;
                if (!(valid(cur) && cur.address() == K.addr))
                {
                    if (now - K.k.at.value_or(0) < 0.25 && !K.k.stopped && valid(K.a)) ns_stop(K);
                    N.k.erase(it);
                }
                else if (now - K.k.at.value_or(0) > 0.15 && !K.k.stopped) ns_stop(K);
            }
            auto ip = N.k.find(key);
            if (!(ip != N.k.end() && now - ip->second.k.at.value_or(0) <= 0.15)) P.nsYaw = false;
        }
        lg::NsS& Sx = N.s;
        Sx.n++;
        double ms = (x0::now() - t0) * 1000;
        Sx.ms += ms;
        if (ms > Sx.msmax) Sx.msmax = ms;
        if (now - Sx.at >= 10)
        {
            if (FL("nslog", 1) == 1 && Sx.dr > 0)
            {
                LOG(fmt("NOLA nsmooth 10 s: boneco_comandos=%d comandos=%d amostras=%d erro_med=%.1f erro_max=%.0f cm snaps=%d criados=%d falhas=%d yaw=%s uro=%s cmoff_religado=%d | custo med=%.2f max=%.2f ms/tick",
                        Sx.pup, Sx.dr, Sx.smp, Sx.es / std::max(1, Sx.en), Sx.emax, Sx.sn, Sx.mk, Sx.fail, FL("nsyaw", 0) == 1 ? "quadro" : "10Hz",
                        "false", s.cmoBack, Sx.ms / std::max(1, Sx.n), Sx.msmax));
            }
            N.s = lg::ns_s0(now);
        }
    }

    void pup_frame()
    {
        if (FL("avframe", 0) != 1 || FL("nsmooth", 0) != 1) return;
        St& s = S();
        Ns& N = ns_st();
        double now = x0::now();
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            Body& P = s.pl[v].pup;
            if (!valid(P.pawn) || P.hidden || now - P.hbAt > 0.25) { P.yawF.reset(); continue; }
            CvScope cv(v);
            auto it = N.k.find(pup_key(v));
            if (it != N.k.end())
            {
                GM& g = it->second;
                if (g.addr == P.pawn.address() && g.k.has && !g.k.stopped && g.k.at && now - *g.k.at <= 0.15) ns_drive(N, g, P.pawn, now);
            }
            if (yaw_frame(P, now) && P.yaw && !P.nsYaw)
            {
                if (!P.yawFor.same(P.pawn) || !P.yawF) { P.yawFor = P.pawn; P.yawF = *P.yaw; }
                else
                {
                    double dt = std::min(std::max(now - P.yawFT, 0.001), 0.1);
                    P.yawF = *P.yawF + lg::ang_diff(*P.yaw, *P.yawF) * (1 - std::exp(-dt * FL("avyawf", 20)));
                }
                P.yawFT = now;
                set_yaw(P.pawn, *P.yawF);
            }
            else P.yawF.reset();
        }
    }

    bool body_remove(Body& B, const char* what)
    {
        if (!has(B.pawn)) return false;
        std::string how = "ja invalido", name = "?";
        bool ok = true;
        if (valid(B.pawn))
        {
            name = x0::obj_name(B.pawn);
            hide_actor(B.pawn, true);
            ok = false;
            if (B.uid && *B.uid > 0) { how = "XDestroyModel(" + lg::inum(*B.uid) + ")"; ok = destroy_model(*B.uid); }
            if (!ok) { how = "K2_DestroyActor"; ok = destroy_actor(B.pawn); }
        }
        LOG(std::string("NOLA corpos: ") + what + " " + name + " -> escondido + " + how + " ok=" + tf(ok));
        return true;
    }
    void view_gone(const char* why)
    {
        Pl& p = V();
        bool had = has(p.pup.pawn) || has(p.tgt.pawn) || has(p.far.actor) || p.pup.spawnAt || p.tgt.spawnAt;
        if (had) LOG(std::string("NOLA corpos: jogador fora (") + why + ") -> boneco e alvo dele removidos");
        body_remove(p.tgt, "alvo");
        body_remove(p.pup, "boneco");
        if (valid(p.far.actor)) destroy_actor(p.far.actor);
        p = Pl{};
        p.gone = true;
    }
    bool view_check()
    {
        Pl& p = V();
        const x0::Snap& sn = x0::remote(g_cv);
        const x0::Peer& pe = x0::peer(g_cv);
        const char* why = lg::view_why(multi(), g_cv, x0::views(), sn.ok, pe.left, pe.bye);
        if (!why)
        {
            if (p.gone)
            {
                p.gone = false;
                LOG("NOLA corpos: jogador de volta -> boneco e alvo serao adotados/pedidos");
            }
            return false;
        }
        if (!p.gone) view_gone(why);
        return true;
    }

    void tick100()
    {
        bool gateOk = body_gate();
        dup_guard();
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            CvScope cv(v);
            Pl& p = V();
            if ((v > 0 || multi()) && view_check()) continue;
            if (owns_avatar() && FL("coretarget", 0) == 1 && gateOk) tgt_tick();
            if (owns_avatar())
            {
                if (gateOk) pup_tick();
            }
            else if (gateOk && p.pup.state && body_held(p.pup))
            {
                p.pup.state.reset();
                LOG("NOLA puppet: modo desligado (b112: sem SetAnimationMode)");
            }
        }
        ns_tick();
    }

    void no_interact()
    {
        St& s = S();
        if (FL("tgtnoint", 0) != 1) return;
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            CvScope cv(v);
            Pl& p = V();
            for (Body* B : {&p.tgt, &p.pup})
            {
                if (!valid(B->pawn)) continue;
                Ref c = pref(B->pawn, "NPCComponent");
                bool act = false;
                if (!valid(c) || !x0::prop<bool>(c, "bIsInteractionActive", act) || !act) continue;
                if (!Call(c, L"SetInteractionActive").b(L"bActive", false).run()) continue;
                s.niN++;
                bool now = false;
                std::string ns = x0::prop<bool>(c, "bIsInteractionActive", now) ? tf(now) : "nil";
                if (s.niN <= 5 || s.niN % 100 == 0) LOG(fmt("NOLA noint: interacao desligada no %s (#%d; agora ativa=%s)", B == &p.tgt ? "alvo" : "boneco", s.niN, ns.c_str()));
            }
        }
    }

    std::string emission_str()
    {
        Ref med = mediator();
        bool on = false;
        if (bcall(med, L"IsEmissionActive", on)) return tf(on);
        return "?";
    }
    bool raw_i64(const Ref& o, size_t off, int64_t& out)
    {
        if (!x0::is_a(o, L"Agent")) return false;
        x0::UObject* p = o.get();
        if (!p) return false;
        out = *reinterpret_cast<const int64_t*>(reinterpret_cast<const uint8_t*>(p) + off);
        return true;
    }
    std::string cap_str(const char* key, int def)
    {
        x0::Val v = x0::flag(key);
        if (v.k == x0::Val::nil) return lg::inum(def);
        return FS(key);
    }

    void abp_guard()
    {
        St& s = S();
        double md = FL("abpguard", 0);
        if (md == 0) return;
        Abg& G = s.abg;
        double now = x0::now();
        if (!G.reg)
        {
            G.reg = true;
            LOG("NOLA abpguard: propriedades do controlador de animacao (Agent+0xAC8/+0xAD0) registradas ok=true");
        }
        if (now < G.next) return;
        G.next = now + std::max(1.0, FL("abpsec", 2));
        std::string em = emission_str();
        auto addr_view = [&](uint64_t ad, bool& isT) -> int {
            isT = false;
            for (int v = 0; v < x0::kMaxClients; ++v)
            {
                const Pl& p = s.pl[v];
                if (valid(p.pup.pawn) && p.pup.pawn.address() == ad) return v;
                if (valid(p.tgt.pawn) && p.tgt.pawn.address() == ad) { isT = true; return v; }
            }
            return -1;
        };
        Vec3 lpos;
        bool hasL = local_pos(lpos);
        bool host = x0::is_host();
        bool gateOk = gate_open();
        int n = 0, bad = 0;
        std::vector<std::string> names;
        struct Act
        {
            Ref o;
            std::string tag, name;
            int v;
        };
        std::vector<Act> acts;
        for (const Ref& o : x0::all_of(L"BP_AI_Human_C"))
        {
            bool alive = false;
            if (!valid(o) || !bcall(o, L"IsAlive", alive) || !alive) continue;
            n++;
            int64_t h = 0, m = 0;
            bool hm = raw_i64(o, 0xAC8, h) && raw_i64(o, 0xAD0, m);
            Ref mesh = pref(o, "Mesh");
            Ref ai = ocall(mesh, L"GetAnimInstance");
            bool hum = human_class(ai);
            bool noCtl = hm ? (h == 0 && m == 0) : !hum;
            if (!noCtl) continue;
            bad++;
            std::string name = nm(o);
            uint64_t ad = o.address();
            bool isT = false;
            int bv = addr_view(ad, isT);
            std::string tag = bv < 0 ? "outro" : isT ? "alvo" : "boneco";
            bool gate = gateOk && !s.pl[bv < 0 ? 0 : bv].far.on;
            names.push_back(name + "/" + tag);
            std::string key = name + "|" + tf(hum);
            if (!G.seen.count(key))
            {
                if (G.nseen > 500) { G.seen.clear(); G.nseen = 0; }
                G.seen.insert(key);
                G.nseen++;
                double d = -1;
                Vec3 q;
                if (hasL && loc(o, q)) d = std::sqrt((q.x - lpos.x) * (q.x - lpos.x) + (q.y - lpos.y) * (q.y - lpos.y) + (q.z - lpos.z) * (q.z - lpos.z)) / 100;
                bool hb = false;
                std::string hid = x0::prop<bool>(o, "bHidden", hb) ? tf(hb) : "?";
                std::string mode = "?";
                if (valid(mesh))
                {
                    Call gm(mesh, L"GetAnimationMode");
                    if (gm.run()) mode = lg::inum(gm.ri());
                }
                LOG(fmt("NOLA abpguard: humano SEM controlador de animacao %s (%s) ctl=%s/%s instancia=%s humana=%s modo=%s escondido=%s dist=%.0f m emissao=%s",
                        name.c_str(), tag.c_str(), hm ? lg::inum(h).c_str() : "nil", hm ? lg::inum(m).c_str() : "nil", nm(ai).c_str(), tf(hum), mode.c_str(), hid.c_str(), d, em.c_str()));
            }
            if ((tag != "outro" && md >= 2 && gate && (tag == "alvo" || owns_avatar())) || (tag == "outro" && md >= 3 && host && gate)) acts.push_back(Act{o, tag, name, bv});
        }
        if (bad != G.lastBad)
        {
            G.lastBad = bad;
            std::string j;
            for (size_t i = 0; i < names.size() && i < 12; i++) j += (i ? " " : "") + names[i];
            LOG(fmt("NOLA abpguard: censo humanos vivos=%d sem controlador=%d emissao=%s %s", n, bad, em.c_str(), j.c_str()));
        }
        for (const Act& a : acts)
        {
            const std::string& role = a.tag;
            std::vector<double> q;
            for (double at : G.kill[role]) if (now - at < 600) q.push_back(at);
            G.kill[role] = q;
            bool outro = role == "outro";
            double cap = outro ? FL("abpmax3", 6) : FL("abpmax", 4);
            if ((double)q.size() >= cap)
            {
                auto c = G.cap.find(role);
                if (c == G.cap.end() || now - c->second > 600)
                {
                    G.cap[role] = now;
                    LOG("NOLA abpguard: limite de " + (outro ? cap_str("abpmax3", 6) : cap_str("abpmax", 4)) + " reciclagens em 600 s (" + role + "); so log ate la");
                }
            }
            else
            {
                G.kill[role].push_back(now);
                hide_actor(a.o, true);
                bool okD = destroy_actor(a.o);
                CvScope cv(a.v < 0 ? g_cv : a.v);
                if (a.v < 0) g_inv = false;
                Body& P = s.pl[a.v < 0 ? 0 : a.v].pup;
                Body& T = s.pl[a.v < 0 ? 0 : a.v].tgt;
                if (role == "boneco")
                {
                    P.pawn.reset();
                    P.path.reset();
                    P.state.reset();
                    P.hidden = false;
                    P.abpFor.reset();
                    P.retryAt = now + 2;
                    body_lost();
                }
                else if (role == "alvo")
                {
                    T.pawn.reset();
                    T.path.reset();
                    T.setup = false;
                    T.retryAt = now + 2;
                    body_lost();
                }
                LOG(fmt("NOLA abpguard: %s %s sem controlador -> escondido + K2_DestroyActor ok=%s (%d/%d em 600 s; corpo novo = BeginPlay novo)", role.c_str(), a.name.c_str(), tf(okD), (int)G.kill[role].size(), (int)cap));
            }
        }
    }

    void pup_ctl()
    {
        Pl& s = V();
        double na = FL("pupnoai", 0);
        if (na == 0 || !owns_avatar()) return;
        Body& P = s.pup;
        if (na >= 1 && body_ok(P) && gate_open() && !s.far.on && !P.naFor.same(P.pawn))
        {
            P.naFor = P.pawn;
            Ref c = ocall(P.pawn, L"GetController");
            std::vector<std::string> r;
            auto st = [&](const char* k, bool ok, const char* err) { r.push_back(std::string(k) + "=" + (ok ? "ok" : std::string("erro ") + err)); };
            if (valid(c))
            {
                Ref b = pref(c, "BrainComponent");
                if (!valid(b)) st("brain", false, "sem BrainComponent");
                else st("brain", Call(b, L"StopLogic").s(L"Reason", L"nola").run(), "chamada falhou");
                st("tickctl", Call(c, L"SetActorTickEnabled").b(L"bEnabled", false).run(), "chamada falhou");
            }
            st("path", Call(P.pawn, L"TogglePathFollowingComponent").b(L"bEnable", false).run(), "chamada falhou");
            std::string j;
            for (size_t i = 0; i < r.size(); i++) j += (i ? " " : "") + r[i];
            LOG("NOLA pupnoai: boneco " + nm(P.pawn) + " controlador=" + nm(c) + " -> " + j);
        }
    }

    void pup_ai()
    {
        Pl& s = V();
        double md = FL("pupai", 1);
        if (md == 0 || !owns_avatar()) return;
        Body& P = s.pup;
        Pai& Wp = s.pai;
        double now = x0::now();
        if (!body_ok(P) || !gate_open() || s.far.on) { Wp.hasBody = false; Wp.t0.reset(); Wp.on.reset(); return; }
        if (!Wp.hasBody || !Wp.body.same(P.pawn)) { Wp.body = P.pawn; Wp.hasBody = true; Wp.t0.reset(); Wp.on.reset(); }
        bool thr = false;
        bcall(P.pawn, L"IsInThreat", thr);
        std::string name = valid(P.pawn) ? x0::obj_name(P.pawn) : std::string("?");
        if (thr != Wp.on.value_or(false))
        {
            Wp.on = thr;
            Wp.n++;
            if (now - Wp.logAt.value_or(-1e9) >= 5 || Wp.n <= 20)
            {
                Wp.logAt = now;
                LOG(fmt("NOLA pupai: boneco %s em ameaca=%s (#%d)%s", name.c_str(), tf(thr), Wp.n, thr ? " - com o pak v4 isto nao deve acontecer" : ""));
            }
        }
        if (!thr) { Wp.t0.reset(); return; }
        if (!Wp.t0) Wp.t0 = now;
        if (md != 1 || now - *Wp.t0 < FL("pupais", 90)) return;
        std::vector<double> q;
        for (double at : Wp.kill) if (now - at < 600) q.push_back(at);
        Wp.kill = q;
        double cap = FL("pupaimax", 2);
        if ((double)q.size() >= cap)
        {
            if (!Wp.capAt || now - *Wp.capAt > 600)
            {
                Wp.capAt = now;
                LOG(fmt("NOLA pupai: boneco %s em ameaca ha %.0f s mas ja houve %d reciclagens em 600 s -> so log (pak v4 instalado nos DOIS?)", name.c_str(), now - *Wp.t0, (int)q.size()));
            }
            return;
        }
        Wp.kill.push_back(now);
        std::string full = sfull(P.pawn);
        hide_actor(P.pawn, true);
        std::string how = "K2_DestroyActor";
        bool ok = false;
        if (P.uid && *P.uid > 0) { how = "XDestroyModel(" + lg::inum(*P.uid) + ")"; ok = destroy_model(*P.uid); }
        if (!ok) { how = "K2_DestroyActor"; ok = destroy_actor(P.pawn); }
        if (!full.empty()) P.bad.insert(full);
        LOG(fmt("NOLA pupai: boneco %s em ameaca ha %.0f s -> escondido + %s ok=%s (%d/%d em 600 s; corpo novo = dados novos)", name.c_str(), now - *Wp.t0, how.c_str(), tf(ok), (int)Wp.kill.size(), (int)cap));
        P.pawn.reset();
        P.path.reset();
        P.state.reset();
        P.hidden = false;
        P.abpFor.reset();
        P.naFor.reset();
        P.uid.reset();
        P.retryAt = now + 2;
        body_lost();
        Wp.hasBody = false;
        Wp.t0.reset();
        Wp.on.reset();
    }

    void anim_fix()
    {
        St& s = S();
        double fx = FL("pupanimfix", 0);
        if (fx != 0 && !s.afOff)
        {
            s.afOff = true;
            LOG("NOLA animfix: pupanimfix=" + FS("pupanimfix") + " ignorado (b112: SetAnimationMode quebra o controlador, D10; abpguard cuida)");
        }
    }

    void off_tick()
    {
        St& s = S();
        Offt& O = s.offt;
        x0::Val fv = x0::flag("offtick");
        double k = fv.k == x0::Val::nil ? 0 : (fv.k == x0::Val::s ? lg::tonum(fv.sv).value_or(0) : fv.num());
        if (k <= 0 && !O.saved) return;
        Ref KS = x0::find_path(L"/Script/Engine.Default__KismetSystemLibrary");
        Ref pw = local_pawn();
        if (!(valid(KS) && valid(pw))) return;
        auto get = [&](const char* n, double& v) -> bool {
            Call c(KS, L"GetConsoleVariableFloatValue");
            c.s(L"VariableName", W(n));
            if (!c.run()) return false;
            v = c.rf();
            return true;
        };
        auto put = [&](const char* n, const std::string& v) -> bool {
            return Call(KS, L"ExecuteConsoleCommand").o(L"WorldContextObject", pw).s(L"Command", W(std::string(n) + " " + v)).run();
        };
        auto gs = [&](const char* n) -> std::string {
            double v;
            return get(n, v) ? num(v) : std::string("nil");
        };
        if (k > 0)
        {
            int want = lg::offt_want(k);
            if (!O.saved)
            {
                O.saved = true;
                O.sv.clear();
                for (const auto& cv : lg::kOfftCv)
                {
                    double v = 0;
                    bool got = get(cv.name, v);
                    if (!got || v == want) O.sv[cv.name] = CvSaved{cv.def, true};
                    else O.sv[cv.name] = CvSaved{v, false};
                }
                auto fmtv = [](const CvSaved& c) { return c.isint ? lg::inum((int64_t)c.v) : num(c.v); };
                LOG(fmt("NOLA offtick: valores do jogo guardados Mid=%s Far=%s; alvo %s/s -> cvar %d", fmtv(O.sv[lg::kOfftCv[0].name]).c_str(), fmtv(O.sv[lg::kOfftCv[1].name]).c_str(),
                        FS("offtick").c_str(), want));
            }
            for (const auto& cv : lg::kOfftCv)
            {
                double cur = 0;
                bool hc = get(cv.name, cur);
                if (hc && cur == want) continue;
                put(cv.name, lg::inum(want));
                double nw = 0;
                bool hn = get(cv.name, nw);
                if (O.set >= (int)(sizeof lg::kOfftCv / sizeof lg::kOfftCv[0])) O.again++;
                O.set++;
                if (O.set <= 20 || O.set % 100 == 0)
                    LOG(fmt("NOLA offtick: %s %s -> %s (pedido %d; %s; religado=%d)", cv.name, hc ? num(cur).c_str() : "nil", hn ? num(nw).c_str() : "nil", want, (hn && nw == want) ? "ok" : "NAO MUDOU", O.again));
            }
        }
        else
        {
            for (const auto& cv : lg::kOfftCv)
            {
                auto it = O.sv.find(cv.name);
                if (it == O.sv.end()) continue;
                std::string v = it->second.isint ? lg::inum((int64_t)it->second.v) : num(it->second.v);
                put(cv.name, v);
                LOG(fmt("NOLA offtick: flag 0 -> %s devolvido %s (agora %s)", cv.name, v.c_str(), gs(cv.name).c_str()));
            }
            O.saved = false;
            O.sv.clear();
            O.set = 0;
        }
    }

    void tick1000()
    {
        no_interact();
        abp_guard();
        int nv = multi() ? x0::kMaxClients : 1;
        for (int v = 0; v < nv; ++v)
        {
            CvScope cv(v);
            if (V().gone) continue;
            pup_ctl();
            pup_ai();
        }
        anim_fix();
        off_tick();
    }

    std::string g_detPath;
    bool g_detOn = false;
    double g_detAt = 0;
    std::string det_find(const Ref& pawn)
    {
        bool on = false;
        if (FL("padet", 1) != 1 || !bcall(pawn, L"IsDetectorInHands", on)) on = false;
        double now = x0::now();
        if (on && (!g_detOn || now >= g_detAt))
        {
            std::string found;
            for (const Ref& sc : comps(pawn, L"/Script/Engine.SkeletalMeshComponent"))
            {
                if (!valid(sc)) continue;
                Ref a = ocall(sc, L"GetSkeletalMeshAsset");
                if (!valid(a)) continue;
                std::string p = sfull(a);
                if (lg::det_path(p)) { found = p; break; }
            }
            if (found != g_detPath || !g_detOn) LOG("NOLA snapExtra: detector local na mao " + (found.empty() ? std::string("(mesh nao achado, padrao)") : found));
            g_detPath = found;
            g_detAt = now + 2;
        }
        if (!on && g_detOn) LOG("NOLA snapExtra: detector local guardado");
        g_detOn = on;
        return lg::det_tag(on, g_detPath);
    }

    bool hr_rd(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    int g_hrF = -1;
    bool hr_safe(uint64_t pawn)
    {
        uint64_t rec = 0;
        uint16_t f = 0;
        bool ok = pawn && hr_rd(reinterpret_cast<void*>(pawn + 0x650), &rec, 8) && rec && hr_rd(reinterpret_cast<void*>(rec + 0x260), &f, 2);
        g_hrF = ok ? f : -1;
        return ok && (f & 0x20);
    }
    bool g_hRelax = false;
    std::string weapon_str()
    {
        St& s = S();
        if (FL("pupweapon", 1) != 1) return "";
        Ref pawn = local_pawn();
        if (!valid(pawn)) return "";
        std::string dt = det_find(pawn);
        if (FL("pathold", 1) == 1)
        {
            Call et(pawn, L"GetMainHandEquipType");
            int64_t t = et.run() ? et.ri() : -1;
            if (t != s.self.eqType)
            {
                s.self.eqType = t;
                LOG("NOLA pupanim: mao principal tipo " + lg::inum(t));
            }
            if (lg::pa_throwable(t) && !(t == 6 && FL("pabolt", 1) == 1)) { g_hRelax = false; return "W1~~"; }
        }
        Ref w = ocall(pawn, L"GetWeaponInHandsMeshComponent");
        if (valid(w) && !dt.empty())
        {
            Ref wa0 = ocall(w, L"GetSkeletalMeshAsset");
            if (valid(wa0) && lg::det_path(sfull(wa0))) w = Ref{};
        }
        if (!valid(w))
        {
            bool rx = false;
            if (dt.empty() && FL("pahrelax", 1) == 1 && s.self.has)
            {
                Call et(pawn, L"GetMainHandEquipType");
                bool blk = false;
                rx = et.run() && et.ri() == 0 && !(bcall(pawn, L"ShouldBlockEquipMainHandActions", blk) && blk) && !(FL("pahrsafe", 1) == 1 && hr_safe(pawn.address()));
            }
            if (rx != g_hRelax) LOG(std::string("NOLA pupanim: arma guardada (H) ") + (rx ? "sim, mostra abaixada" : "nao") + " zona=" + lg::inum(g_hrF));
            g_hRelax = rx;
            if (rx) return "W2~" + x0::look_put(s.self.str);
            return dt.empty() ? std::string("W1~~") : "W2~" + x0::look_put("W1~~" + dt);
        }
        g_hRelax = false;
        std::string wname = x0::obj_name(w);
        SelfLook& c = s.self;
        double now = x0::now();
        if (!(c.has && c.wname == wname) || (FL("pwrescan", 0) == 1 && now >= c.at))
        {
            std::string part;
            std::vector<std::string> statics;
            Ref wa = ocall(w, L"GetSkeletalMeshAsset");
            if (valid(wa))
            {
                Call k(w, L"GetAttachSocketName");
                std::string sock;
                if (k.run()) sock = k.rn();
                part = sfull(wa) + "@@" + wname + "@" + sock;
            }
            std::set<std::string> known{wname};
            std::vector<Ref> list = comps(pawn, L"/Script/Engine.StaticMeshComponent");
            for (int pass = 0; pass < 4; pass++)
            {
                for (const Ref& sc : list)
                {
                    bool vis = false;
                    if (!valid(sc) || !bcall(sc, L"IsVisible", vis) || !vis) continue;
                    std::string scn = x0::obj_name(sc);
                    if (known.count(scn)) continue;
                    Ref par = ocall(sc, L"GetAttachParent");
                    if (!valid(par)) continue;
                    std::string pn = x0::obj_name(par);
                    if (!known.count(pn)) continue;
                    Ref sm = pref(sc, "StaticMesh");
                    if (!valid(sm)) continue;
                    Vec3 l, k;
                    Rot rr;
                    if (!x0::prop<Vec3>(sc, "RelativeLocation", l) || !x0::prop<Rot>(sc, "RelativeRotation", rr) || !x0::prop<Vec3>(sc, "RelativeScale3D", k)) continue;
                    known.insert(scn);
                    Call ks(sc, L"GetAttachSocketName");
                    std::string ssock;
                    if (ks.run()) ssock = ks.rn();
                    if (statics.size() < 14)
                        statics.push_back(fmt("%s>%s>%s>%s>%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f", scn.c_str(), pn.c_str(), ssock.c_str(), sfull(sm).c_str(), l.x, l.y, l.z, rr.pitch, rr.yaw, rr.roll, k.x, k.y, k.z));
                }
            }
            if (FL("pwrescan", 0) == 1) std::sort(statics.begin(), statics.end());
            std::string old = c.has ? c.str : std::string();
            bool hadOld = c.has;
            std::string j;
            for (size_t i = 0; i < statics.size(); i++) j += (i ? "|" : "") + statics[i];
            c.has = true;
            c.wname = wname;
            c.str = "W1~" + part + "~" + j;
            c.at = now + ((statics.empty() && !part.empty()) ? 1 : FL("pwscan", 3));
            if (!(hadOld && old == c.str)) LOG("NOLA snapExtra: arma local " + lead_at(part) + " estaticos=" + lg::inum((int64_t)statics.size()) + " bytes=" + lg::inum((int64_t)c.str.size()));
        }
        return "W2~" + x0::look_put(c.str + dt);
    }
    lg::AcTx g_acTx;
    Ref g_acPawn;
    std::vector<Ref> g_acComps;
    double g_acCompAt = 0;
    int g_acN = 0;
    lg::VzRange g_vzLocal;
    int g_vzLocalN = 0;
    lg::HitSt g_hit;
    void hit_poll(const Ref& pawn, double now)
    {
        float hp = 0;
        if (x0::is_client() || !fcall(pawn, L"GetHP", hp)) return;
        double prev = g_hit.hp;
        if (!lg::hit_feed(C, g_hit, hp, now)) return;
        std::string why;
        auto rel = hit_rel(pawn, why);
        const char* side = rel ? lg::hit_side(*rel) : nullptr;
        g_hitN++;
        bool sent = lg::ac_hit(C, g_acTx, side, now);
        if (g_hitN <= 50 || g_hitN % 50 == 0)
            LOG(fmt("NOLA pupanim: golpe local hp %.1f -> %.1f lado %s %s%s (fonte %d, vez %d)", prev, (double)hp, side ? side : "nil", why.c_str(),
                    sent ? (" -> acao " + *g_acTx.id + lg::inum(g_acTx.seq)).c_str() : " -> nada enviado", (int)FL("pahitsrc", 1), g_hitN));
    }
    std::string ix_probe()
    {
        Ref a = ocall(local_pawn(), L"GetInteractedActor");
        if (!valid(a)) return std::string();
        bool obj = x0::is_a(a, L"Obj");
        return lg::ix_body(C, x0::obj_name(a), obj, obj ? x0::call_b(a, L"IsAlive", true) : true);
    }
    std::string ac_poll(const Ref& pawn)
    {
        double now = x0::now();
        if (FL("paix", 1) == 1 && s2m_ixtap::install(&ix_probe))
        {
            std::string act;
            if (s2m_ixtap::take(act) && lg::ac_ix(C, g_acTx, act, now))
            {
                g_acN++;
                if (g_acN <= 50 || g_acN % 50 == 0) LOG(fmt("NOLA pupanim: acao local %s%d de toque em %s (vez %d, %s)", g_acTx.id->c_str(), g_acTx.seq, act.c_str(), g_acN, s2m_ixtap::stat().c_str()));
            }
        }
        if (g_acTx.lhOn)
        {
            bool open = s2m_items::lockMine().has_value() || s2m_items::stashOpen();
            Vec3 lp{};
            bool hasPos = loc(pawn, lp);
            float hp = -1;
            if (!fcall(pawn, L"GetHP", hp)) hp = -1;
            if (const char* why = lg::lh_tick(C, g_acTx, open, hasPos, lp.x, lp.y, hp, now))
                LOG(fmt("NOLA pupanim: busca local na caixa lh%d terminou: %s (%.1f s, janela vista=%s)", g_acTx.seq, why, now - g_acTx.at, tf(g_acTx.lhSeen)));
        }
        if (!g_acPawn.same(pawn) || now >= g_acCompAt)
        {
            g_acComps = comps(pawn, L"/Script/Engine.SkeletalMeshComponent");
            g_acPawn = pawn;
            g_acCompAt = now + 5;
        }
        hit_poll(pawn, now);
        bool v = false;
        if (FL("pavt", 1) == 1 && bcall(pawn, L"IsVaulting", v) && lg::ac_vault(C, g_acTx, v, now))
        {
            g_acN++;
            if (g_acN <= 50 || g_acN % 50 == 0) LOG(fmt("NOLA pupanim: acao local %s%d de salto por cima (IsVaulting, vez %d)", g_acTx.id->c_str(), g_acTx.seq, g_acN));
        }
        Vec3 vp;
        if (FL("pavt", 1) == 1 && loc(pawn, vp) && lg::vz_track(g_vzLocal, v, vp.z, now) && ++g_vzLocalN <= 50)
            LOG(fmt("NOLA vaultz: salto local terminou: z inicio %.0f min %.0f max %.0f (subida %.0f cm) em %.2f s", g_vzLocal.z0, g_vzLocal.lo, g_vzLocal.hi, g_vzLocal.hi - g_vzLocal.z0, now - g_vzLocal.t0));
        std::string key, name;
        double rvAt = s2m::damage::revive_start_at();
        if (rvAt > 0 && now - rvAt < 1)
        {
            key = "rv" + lg::inum((int64_t)(rvAt * 1000));
            name = "revive";
        }
        for (const Ref& c : g_acComps)
        {
            if (!key.empty()) break;
            if (!valid(c)) continue;
            Ref ai = ocall(c, L"GetAnimInstance");
            if (!valid(ai)) continue;
            Ref mo = ocall(ai, L"GetCurrentActiveMontage");
            if (!valid(mo)) continue;
            name = x0::obj_name(mo);
            if (!lg::ac_of(C, name)) continue;
            key = lg::inum((int64_t)mo.address()) + ":" + name;
            break;
        }
        if (lg::ac_feed(C, g_acTx, key, name, now))
        {
            g_acN++;
            if (g_acN <= 50 || g_acN % 50 == 0) LOG(fmt("NOLA pupanim: acao local %s%d de %s (vez %d)", g_acTx.id->c_str(), g_acTx.seq, name.c_str(), g_acN));
        }
        return lg::ac_token(C, g_acTx, now);
    }
    int g_lnTx = 0, g_lnN = 0;
    std::string mv_str()
    {
        if (FL("pupmove", 0) <= 0) return "";
        Ref pawn = local_pawn();
        if (!valid(pawn)) return "";
        Ref cm = pref(pawn, "CharacterMovement");
        bool b = false;
        int c = 0, a = 0;
        if (bcall(pawn, L"IsCrouching", b)) c = b ? 1 : 0;
        else if (bcall(cm, L"IsCrouching", b)) c = b ? 1 : 0;
        if (bcall(pawn, L"IsAiming", b)) a = b ? 1 : 0;
        if (!a && g_hRelax) a = 2;
        if (FL("pupanim", 0) == 1)
        {
            int f = 0;
            bool got = false;
            if (bcall(pawn, L"IsFalling", b))
            {
                if (b) { f = 1; got = true; }
                else if (bcall(pawn, L"IsJumping", b)) { f = b ? 1 : 0; got = true; }
            }
            if (!got && bcall(cm, L"IsFalling", b)) f = b ? 1 : 0;
            std::string at = ac_poll(pawn);
            int ln = 0;
            if (FL("palean", 1) == 1)
            {
                if (bcall(pawn, L"IsLeaningLeft", b) && b) ln = -1;
                else if (bcall(pawn, L"IsLeaningRight", b) && b) ln = 1;
            }
            if (ln != g_lnTx)
            {
                g_lnTx = ln;
                g_lnN++;
                if (g_lnN <= 50 || g_lnN % 50 == 0) LOG(fmt("NOLA pupanim: inclinacao local %s (vez %d)", ln < 0 ? "esquerda" : ln > 0 ? "direita" : "nenhuma", g_lnN));
            }
            int l = (FL("paladder", 1) == 1 && bcall(pawn, L"IsClimbing", b) && b) ? 1 : 0;
            if (l) f = 0;
            int lc = lg::mv_lcode(l == 1, ln);
            if (!at.empty()) return fmt("#MV:%d,%d,%d,%d,%s", c, a, f, lc, at.c_str());
            if (FL("paladder", 1) == 1 || lc) return fmt("#MV:%d,%d,%d,%d", c, a, f, lc);
            return fmt("#MV:%d,%d,%d", c, a, f);
        }
        return fmt("#MV:%d,%d", c, a);
    }
    std::string fh_str()
    {
        if (FL("pupfeet", 0) != 1) return "";
        Ref cap = pref(local_pawn(), "CapsuleComponent");
        float h = 0;
        if (valid(cap) && fcall(cap, L"GetScaledCapsuleHalfHeight", h) && h > 1) return fmt("#FH:%.1f", (double)h);
        return "";
    }
}

namespace m01
{
    x0::Ref puppet() { return S().pl[0].pup.pawn; }
    x0::Ref target() { return S().pl[0].tgt.pawn; }
    x0::Ref far_body() { return S().pl[0].far.actor; }
    x0::Ref puppet(int view) { return VW(view).pup.pawn; }
    x0::Ref puppet_weapon(int view)
    {
        const auto& w = VW(view).pup.wcomps;
        return !w.empty() && w[0].valid() ? w[0] : x0::Ref{};
    }
    x0::Ref target(int view) { return VW(view).tgt.pawn; }
    void ff_flinch(int view, double amount, const std::string& bone)
    {
        if (FL("pahitff", 1) != 1) return;
        CvScope cv(view);
        Pl& s = V();
        lg::RSnap r;
        Vec3 lp, tp;
        Rot tr;
        if (!remote_snap(r) || !body_ok(s.pup) || !body_ok(s.tgt) || !local_pos(lp) || !loc(s.tgt.pawn, tp) || !rot(s.tgt.pawn, tr)) return;
        double bearing = std::atan2(lp.y - tp.y, lp.x - tp.x) * 180.0 / 3.14159265358979323846;
        double rel = lg::ang_diff(bearing, tr.yaw);
        int zone = bone.find("head") != std::string::npos || bone.find("neck") != std::string::npos ? 0
                   : (bone.find("spine_01") != std::string::npos || bone.find("pelvis") != std::string::npos || bone.find("thigh") != std::string::npos || bone.find("calf") != std::string::npos || bone.find("foot") != std::string::npos) ? 3
                   : (rel > 0 ? 2 : 1);
        hit_tgt(s.pup, s.tgt, r, amount, x0::now(), rel, zone);
    }
    void note_throw()
    {
        if (lg::ac_force(C, g_acTx, "th", x0::now(), FL("pathgap", 2.5)))
        {
            g_acN++;
            LOG(fmt("NOLA pupanim: acao local th%d do objeto lancado (vez %d)", g_acTx.seq, g_acN));
        }
    }
    double last_th_at() { return lg::ac_th_at(C, g_acTx, x0::now()); }
    void note_partner_shot(int view)
    {
        if (view >= 0 && view < x0::kMaxClients) g_shN[view]++;
    }
    x0::Ref far_body(int view) { return VW(view).far.actor; }
    int view_of_body(const x0::Ref& r)
    {
        bool t = false;
        return body_view(r, t);
    }
    void bodies(std::vector<x0::Ref>& out)
    {
        for (const Pl& p : S().pl)
        {
            if (has(p.tgt.pawn)) out.push_back(p.tgt.pawn);
            if (has(p.pup.pawn)) out.push_back(p.pup.pawn);
        }
    }
    bool view_live(int view) { return view >= 0 && view < x0::kMaxClients && (view == 0 || (multi() && view < x0::views())) && !S().pl[view].gone; }
    bool is_m1_body(const x0::Ref& r) { return is_body(r); }
    bool pend_body(const x0::Ref& r)
    {
        if (!valid(r)) return false;
        std::string full;
        bool named = false;
        for (Pl& p : S().pl)
            for (Body* Q : {&p.tgt, &p.pup})
            {
                if (!(Q->spawnAt && Q->hasBefore && Q->hasSx)) continue;
                if (!named)
                {
                    named = true;
                    full = x0::full_name(r);
                    if (full.find("BP_AI_Human_C") == std::string::npos) return false;
                }
                bool isPup = Q == &p.pup;
                double mh;
                Vec3 q;
                if (!max_hp(r, mh) || !loc(r, q)) continue;
                if (!lg::pend_hit(!isPup, Q->before.count(full) > 0, mh, Q->hpLo, Q->hpHi, q.x - Q->sx, q.y - Q->sy, isPup ? FL("pupadoptr", 5000) : 800)) continue;
                if (Q->pendSeen != full)
                {
                    Q->pendSeen = full;
                    LOG(std::string("NOLA ") + (isPup ? "puppet" : "target") + ": " + leaf_dot(full) + " e corpo nosso antes da adocao (maxhp " + num(mh) + ")");
                }
                return true;
            }
        return false;
    }
    bool is_own_body(const x0::Ref& r)
    {
        if (is_m1_body(r)) return true;
        if (pawn_out(r)) return true;
        if (pend_body(r)) return true;
        for (auto& c : S().own)
            if (c.second && c.second(r)) return true;
        return false;
    }
    bool body_gate() { return FL("bodysafe", 1) != 1 || S().wg.open; }
    bool far_on() { return S().pl[0].far.on; }
    bool far_on(int view) { return VW(view).far.on; }
    bool world_held() { return FL("bodysafe", 1) == 1 && FL("qhold", 1) == 1 && S().wg.hold.held; }
    int quests_held() { return quests_pending(); }
    bool sm_tick_off(const uint8_t* p)
    {
        __try { return p[0x83] == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool quest_graph_held()
    {
        x0::Ref m = x0::first_of(L"ScenarioManager");
        auto* p = reinterpret_cast<const uint8_t*>(m.get());
        if (!p) return false;
        return sm_tick_off(p);
    }
    bool spawn_pending()
    {
        for (const Pl& p : S().pl)
            if (p.pup.spawnAt.has_value() || p.tgt.spawnAt.has_value()) return true;
        return false;
    }
    bool adopt_pending()
    {
        for (const Pl& p : S().pl)
            if (p.pup.hasBefore || p.tgt.hasBefore) return true;
        return false;
    }
    std::optional<int64_t> puppet_uid() { return S().pl[0].pup.uid; }
    std::optional<int64_t> target_uid() { return S().pl[0].tgt.uid; }
    std::optional<int64_t> puppet_uid(int view) { return VW(view).pup.uid; }
    std::optional<int64_t> target_uid(int view) { return VW(view).tgt.uid; }
    std::string puppet_state() { return S().pl[0].pup.state ? *S().pl[0].pup.state : std::string("nil"); }
    bool target_parked() { return S().pl[0].tgt.parkOn.has_value(); }
    bool target_parked(int view) { return VW(view).tgt.parkOn.has_value(); }
    void target_note(const char* why, double hp) { CvScope cv(0); g_inv = false; tg_note(why, hp); }
    void target_park(const char* why) { CvScope cv(0); g_inv = false; tg_park(why); }
    void target_back() { CvScope cv(0); g_inv = false; tg_back(); }
    void target_note(int view, const char* why, double hp) { CvScope cv(view); tg_note(why, hp); }
    void target_park(int view, const char* why) { CvScope cv(view); tg_park(why); }
    void target_back(int view) { CvScope cv(view); tg_back(); }
    Dmg& dmg() { return S().pl[0].dmg; }
    Dmg& dmg(int view) { return VW(view).dmg; }
    void note_local_shot(const x0::Vec3& o, const x0::Vec3& d) { S().shot = lg::Shot{x0::now(), o.x, o.y, o.z, d.x, d.y, d.z}; }
    void note_anom_quiet(double x, double y, double r, double until_t) { S().quiet = lg::Quiet{x, y, r, until_t}; }
    double jump_until() { return S().pl[0].jumpUntil.value_or(0); }
    double jump_until(int view) { return VW(view).jumpUntil.value_or(0); }
    void add_own_check(x0::Mod m, OwnCheck f) { S().own.emplace_back(m, std::move(f)); }
    Ext& ext() { return S().ext; }
}

X0_MODULE(bodies)
{
    auto& I = x0::iface();
    I.puppet = [] { return m01::puppet(); };
    I.target = [] { return m01::target(); };
    I.is_own_body = [](const x0::Ref& r) { return m01::is_own_body(r); };
    I.body_gate = [] { return m01::body_gate(); };
    x0::own_tag(MOD, "@W", [] { return weapon_str(); });
    x0::own_tag(MOD, "MV", [] { return mv_str(); });
    x0::own_tag(MOD, "FH", [] { return fh_str(); });
    x0::own_tag(MOD, "AB", [] { return ab_str(); });
    x0::every(MOD, "bodies100", x0::Rate::t100, [] { tick100(); });
    x0::every(MOD, "bodies1000", x0::Rate::t1000, [] { tick1000(); });
    x0::every(MOD, "bodiesFrame", x0::Rate::frame, [] { tick_frame(); pup_frame(); vz_frame(); });
    x0::bodies_v().puppet = [](int v) { return m01::puppet(v); };
    x0::bodies_v().target = [](int v) { return m01::target(v); };
    x0::status(MOD, [] {
        St& s = S();
        Pl& p = s.pl[0];
        std::string o = fmt("bodies: puppet=%s estado=%s target=%s dano_total=%.1f far=%s gate=%s", tf(valid(p.pup.pawn)), m01::puppet_state().c_str(), tf(valid(p.tgt.pawn)), p.dmg.total, tf(p.far.on), tf(s.wg.open));
        if (multi())
        {
            for (int v = 0; v < x0::views(); ++v)
            {
                Pl& q = s.pl[v];
                o += fmt(" | pid %d: puppet=%s target=%s dano=%.1f%s", x0::view_pid(v), tf(valid(q.pup.pawn)), tf(valid(q.tgt.pawn)), q.dmg.total, q.gone ? " fora" : "");
            }
        }
        return o;
    });
}
