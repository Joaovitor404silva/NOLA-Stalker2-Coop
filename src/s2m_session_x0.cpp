#include "s2x.h"
#include "s2m_session.h"
#include "s2m_travel.h"
#include "s2m_damage.h"

#include <map>
#include <memory>
#include <string>

namespace
{
using namespace s2m;
using x0::Ref;

std::wstring W(const char* s)
{
    std::wstring r;
    while (*s) r += (wchar_t)(unsigned char)*s++;
    return r;
}

class XEnv : public Env
{
  public:
    double clock() override { return x0::now(); }
    long long unixTime() override { return x0::wall_ms() / 1000; }
    void log(const std::string& l) override { x0::log(l); }
    void screen(const char* id, const std::string& t, double s) override { x0::ui_msg(id, t, s); }
    void screenClear(const char* id) override { x0::screen_text_clear(id); }
    bool host() override { return x0::is_host(); }
    bool coop() override { return x0::coop(); }

    double fnum(const char* k, double d) override
    {
        x0::Val v = x0::flag(k);
        return v.k == x0::Val::i || v.k == x0::Val::f ? v.num() : d;
    }
    std::string fstr(const char* k, const std::string& d) override
    {
        x0::Val v = x0::flag(k);
        return v.k == x0::Val::nil ? d : v.str();
    }
    int ftype(const char* k) override
    {
        x0::Val v = x0::flag(k);
        return v.k == x0::Val::nil ? 0 : v.k == x0::Val::s ? 2 : 1;
    }
    Val fget(const char* k) override
    {
        x0::Val v = x0::flag(k);
        Val r;
        if (v.k == x0::Val::s) { r.t = 2; r.s = v.sv; }
        else if (v.k != x0::Val::nil) { r.t = 1; r.n = v.num(); }
        return r;
    }
    void fset(const char* k, const Val& v) override
    {
        if (v.t == 2) x0::fset(k, x0::Val::S(v.s), "hostcfg");
        else if (v.n == (double)(int64_t)v.n) x0::fset(k, x0::Val::I((int64_t)v.n), "hostcfg");
        else x0::fset(k, x0::Val::F(v.n), "hostcfg");
    }

    bool x1part(const char* tag, std::string& out) override { return x0::rpart(tag, out); }
    int remoteWx() override
    {
        const x0::Snap& s = x0::remote();
        return s.ok ? s.wx : -1;
    }
    bool remoteTod(const char* key, double& out) override
    {
        const x0::Snap& s = x0::remote();
        if (!s.ok) return false;
        out = std::string(key) == "gt" ? s.gt : s.wt;
        return true;
    }
    bool remote(RemoteInfo& o) override
    {
        const x0::Snap& s = x0::remote();
        if (!s.ok) return false;
        o.recv = s.recv;
        if (s.t >= 0) o.t = s.t; else o.t.reset();
        o.sid = s.sid;
        o.left = x0::peer().left;
        o.bye = x0::peer().bye;
        o.local = x0::peer().local;
        o.hello.clear();
        size_t p = s.x1raw.find("#H:");
        if (p != std::string::npos)
        {
            size_t e = s.x1raw.find('#', p + 3);
            o.hello = s.x1raw.substr(p + 3, e == std::string::npos ? std::string::npos : e - p - 3);
        }
        return true;
    }
    bool remotePos(RemotePos& o) override
    {
        const x0::Snap& s = x0::remote();
        if (!s.ok || !s.has_pos) return false;
        o.x = s.x; o.y = s.y; o.z = s.z; o.yaw = s.yaw; o.recv = s.recv;
        return true;
    }
    int views() override { return x0::views(); }
    bool x1partView(int v, const char* tag, std::string& out) override { return x0::rpart(tag, out, v); }
    bool remotePosView(int v, RemotePos& o) override
    {
        if (v == 0) return remotePos(o);
        const x0::Snap& s = x0::remote(v);
        if (!s.ok || !s.has_pos) return false;
        o.x = s.x; o.y = s.y; o.z = s.z; o.yaw = s.yaw; o.recv = s.recv;
        return true;
    }
    int viewState(int v) override { return static_cast<int>(x0::peer(v).state); }
    int myPid() override { return x0::my_pid(); }
    int maxClients() override { return x0::max_clients(); }
    int viewPid(int v) override
    {
        if (x0::views() <= 1 && v == 0) return x0::is_host() ? 1 : 0;
        return x0::view_pid(v);
    }
    bool remoteView(int v, RemoteInfo& o) override
    {
        if (v == 0) return remote(o);
        const x0::Snap& s = x0::remote(v);
        if (!s.ok) return false;
        o.recv = s.recv;
        if (s.t >= 0) o.t = s.t; else o.t.reset();
        o.sid = s.sid;
        o.left = x0::peer(v).left;
        o.bye = x0::peer(v).bye;
        o.local = x0::peer(v).local;
        o.hello.clear();
        size_t p = s.x1raw.find("#H:");
        if (p != std::string::npos)
        {
            size_t e = s.x1raw.find('#', p + 3);
            o.hello = s.x1raw.substr(p + 3, e == std::string::npos ? std::string::npos : e - p - 3);
        }
        return true;
    }
    void onPeerSilent() override {}
    void onNewSession() override {}
    std::string myNonce() override { return x0::my_nonce(); }
    std::string modVer() override { return x0::fs("main_ver", "v37-b53"); }
    std::string coreVer() override { return x0::fs("core_ver", "b205"); }
    std::string buildMd5() override { return x0::fs("build_md5", ""); }

    bool readClock(Clock& c) override
    {
        Ref tm = x0::first_of(L"TimeManager");
        if (!tm) return false;
        double gs;
        if (!x0::prop(tm, "GameTimeSeconds", gs)) return false;
        c.gs = gs;
        int h, m;
        float s;
        if (x0::prop(tm, "GameHours", h) && x0::prop(tm, "GameMinutes", m) && x0::prop(tm, "GameSecondsClamped", s)) c.wt = h * 3600.0 + m * 60.0 + s;
        else c.wt.reset();
        return true;
    }
    int targetWeather() override
    {
        Ref w = x0::first_of(L"WeatherController");
        if (!w) return -1;
        uint8_t t;
        return x0::prop(w, "CurrentWeatherData.TargetWeather", t) ? t : -1;
    }
    bool getReqDur(double& d) override
    {
        Ref w = x0::first_of(L"WeatherController");
        float f;
        if (!w || !x0::prop(w, "CurrentWeatherData.RequestedWeatherDuration", f)) return false;
        d = f;
        return true;
    }
    bool setReqDur(double d) override
    {
        Ref w = x0::first_of(L"WeatherController");
        return w && x0::prop_set(w, "CurrentWeatherData.RequestedWeatherDuration", (float)d);
    }
    SetTime cmdSetTime(int h, int m, int s, bool weather) override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerAA");
        if (!mgr) return SetTime::NoMgr;
        x0::Call c(mgr, weather ? L"XSetWeatherTime" : L"XSetGameTime");
        c.i(L"Hour", h).i(L"Minute", m).i(L"Second", s);
        return c.run() ? SetTime::Ok : SetTime::Fail;
    }
    bool cmdWeather(bool force, int w) override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerDR");
        if (!mgr) return false;
        x0::Call c(mgr, force ? L"XForceWeather" : L"XSwitchToWeather");
        c.i(L"WeatherType", w);
        return c.run();
    }
    bool cmdEm(const char* fn) override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerSM");
        return mgr && x0::call0(mgr, W(fn).c_str());
    }
    bool cmdStartSleep() override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerMP");
        return mgr && x0::call0(mgr, L"XStartSleep");
    }
    bool cmdSkipHours(int h) override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerIK");
        if (!mgr) return false;
        x0::Call c(mgr, L"XSkipTimeHours");
        c.f(L"Hours", (float)h);
        return c.run();
    }
    bool cmdUnkillable(const std::string& uid, bool on) override
    {
        Ref mgr = x0::first_of(L"CustomConsoleManagerBuild");
        if (!mgr) return false;
        x0::Call c(mgr, L"XSetEntityUnkillableByUID");
        c.i(L"EntityUID", atoll(uid.c_str())).b(L"bGodModeState", on);
        return c.run();
    }
    int emissionActive() override
    {
        Ref med = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
        if (!med) return -1;
        x0::Call c(med, L"IsEmissionActive");
        return c.run() ? (c.rb() ? 1 : 0) : -1;
    }

    Ref ioRef() { return x0::first_of(L"BP_Stalker2Character_C"); }
    Ref pcRef() { return x0::first_of(L"PlayerController"); }
    Ref anyRef()
    {
        Ref p = ioRef();
        if (p) return p;
        Ref pc = pcRef();
        return pc ? x0::call_o(pc, L"K2_GetPawn") : Ref{};
    }
    bool ioPawn(uintptr_t& a) override
    {
        Ref p = ioRef();
        if (!p) return false;
        a = (uintptr_t)p.address();
        return true;
    }
    bool anyPawn(uintptr_t& a) override
    {
        Ref p = anyRef();
        if (!p) return false;
        a = (uintptr_t)p.address();
        return true;
    }
    bool callF(const wchar_t* fn, double& out)
    {
        Ref p = ioRef();
        if (!p) return false;
        x0::Call c(p, fn);
        if (!c.run()) return false;
        out = c.rf();
        return true;
    }
    bool pawnSleeping() override
    {
        Ref p = ioRef();
        return p && x0::call_b(p, L"IsSleeping", false);
    }
    bool pawnAlive(bool& v) override
    {
        Ref p = ioRef();
        if (!p) return false;
        x0::Call c(p, L"IsAlive");
        if (!c.run()) return false;
        v = c.rb();
        return true;
    }
    bool pawnHp(double& v) override { return callF(L"GetHP", v); }
    bool pawnGuid(std::string& uid) override
    {
        Ref p = ioRef();
        Ref med = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
        if (!p || !med) return false;
        x0::Call c(med, L"GetGUID");
        c.o(L"Actor", p);
        if (!c.run()) return false;
        uid = std::to_string(c.ri());
        return true;
    }
    bool getSleepiness(double& v) override { return callF(L"GetCurrentSleepinessPoints", v); }
    bool setSleepiness(double v) override
    {
        Ref p = ioRef();
        if (!p) return false;
        x0::Call c(p, L"SetCurrentSleepinessPoints");
        c.f(L"SleepinessPoints", (float)v);
        return c.run();
    }
    bool getHunger(double& v) override { return callF(L"GetCurrentHungerPoints", v); }
    bool forceSetHp(double v) override
    {
        Ref p = ioRef();
        if (!p) return false;
        x0::Call c(p, L"ForceSetHP");
        c.i(L"HP", (int64_t)v);
        return c.run();
    }
    bool localPos(double& x, double& y, double& z) override
    {
        Ref p = ioRef();
        x0::Vec3 v;
        if (!p || !x0::call_v(p, L"K2_GetActorLocation", v)) return false;
        x = v.x; y = v.y; z = v.z;
        return true;
    }
    bool tpfNow(double, double, double, double, const char*) override { return false; }

    std::map<std::string, Ref> bedRefs_;
    void beds(std::vector<Bed>& out) override
    {
        bedRefs_.clear();
        for (const Ref& b : x0::all_of(L"BedView"))
        {
            std::string key = x0::full_name(b);
            if (key.empty()) key = x0::obj_name(b);
            Ref mesh;
            if (key.empty() || !x0::prop(b, "BedMeshComponent", mesh) || !mesh) continue;
            Bed e;
            e.key = key;
            x0::Call c(mesh, L"GetCollisionEnabled");
            e.cur = c.run() ? (int)c.ri() : -1;
            bedRefs_[key] = mesh;
            out.push_back(e);
        }
    }
    bool bedSet(const std::string& key, int v) override
    {
        auto it = bedRefs_.find(key);
        if (it == bedRefs_.end() || !it->second) return false;
        x0::Call c(it->second, L"SetCollisionEnabled");
        c.i(L"NewType", v);
        return c.run();
    }

    bool nodeathDead() override { return false; }
    bool lifeDown() override { return x0::iface().local_downed && x0::iface().local_downed(); }
    std::string ftLock() override { return x0::story_locked() ? "dialogo" : ""; }
    bool cineGodHeld() override { return x0::fi("godown", 1) == 1 && ((x0::port(x0::Mod::travel) && s2m_travel_god_held()) || s2m_damage_god_held()); }

    Ref worldRef()
    {
        return x0::world();
    }
    bool gamePaused(bool& paused) override
    {
        Ref w = worldRef();
        Ref gs = x0::find_path(L"/Script/Engine.Default__GameplayStatics");
        if (!w || !gs) return false;
        x0::Call c(gs, L"IsGamePaused");
        c.o(L"WorldContextObject", w);
        if (!c.run()) return false;
        paused = c.rb();
        return true;
    }
    bool setGamePaused(bool paused, bool& ret) override
    {
        Ref w = worldRef();
        Ref gs = x0::find_path(L"/Script/Engine.Default__GameplayStatics");
        if (!w || !gs) return false;
        x0::Call c(gs, L"SetGamePaused");
        c.o(L"WorldContextObject", w).b(L"bPaused", paused);
        if (!c.run()) return false;
        ret = c.rb();
        return true;
    }
    bool inCinematic() override
    {
        Ref pc = pcRef(), ch = x0::first_of(L"PC");
        bool cin = false;
        return pc && ch && x0::prop(ch, "bInCinematicSequence", cin) && cin && x0::call_b(pc, L"IsMoveInputIgnored", false);
    }
};

XEnv g_env;
std::unique_ptr<Session> g_session;
}

extern "C" bool s2m_session_em_nosave() { return g_session && x0::port(x0::Mod::session) && g_session->emNoSave(); }
extern "C" bool s2m_session_em_god() { return g_session && x0::port(x0::Mod::session) && g_session->emGod(); }

X0_MODULE(session)
{
    g_session = session_create(g_env);
    Session* s = g_session.get();
    x0::every(x0::Mod::session, "session100", x0::Rate::t100, [s] { if (x0::port(x0::Mod::session)) s->tick100(); });
    x0::every(x0::Mod::session, "session1000", x0::Rate::t1000, [s] { if (x0::port(x0::Mod::session)) s->tick1000(); });
    x0::on_snap(x0::Mod::session, [s](const x0::Snap&) { if (x0::port(x0::Mod::session)) s->onSnapshot(); });
    x0::own_tag(x0::Mod::session, "H", [s] { return s->outHello().substr(3); });
    x0::own_tag(x0::Mod::session, "@time", [s] { return s->headTimeFields(); });
    x0::own_tag(x0::Mod::session, "E", [s] { std::string o = s->outEm(); return o.size() > 3 ? o.substr(3) : std::string(); });
    x0::own_tag(x0::Mod::session, "SL", [s] { std::string o = s->outTail(); size_t p = o.find("#SL:"); if (p == std::string::npos) return std::string(); size_t e = o.find('#', p + 4); return o.substr(p + 4, e == std::string::npos ? std::string::npos : e - p - 4); });
    x0::own_tag(x0::Mod::session, "SB", [s] { std::string o = s->outTail(); size_t p = o.find("#SB:"); if (p == std::string::npos) return std::string(); size_t e = o.find('#', p + 4); return o.substr(p + 4, e == std::string::npos ? std::string::npos : e - p - 4); });
    x0::own_tag(x0::Mod::session, "HC", [s] { std::string o = s->outTail(); size_t p = o.find("#HC:"); if (p == std::string::npos) return std::string(); return o.substr(p + 4); });
    const char* keys = Session::kHcKeys;
    std::string k(keys);
    size_t i = 0;
    while (i < k.size())
    {
        size_t j = k.find(' ', i);
        if (j == std::string::npos) j = k.size();
        x0::on_flag(x0::Mod::session, k.substr(i, j - i), [s](const std::string& key, const x0::Val&, const x0::Val& after) {
            Val v;
            if (after.k == x0::Val::s) { v.t = 2; v.s = after.sv; }
            else if (after.k != x0::Val::nil) { v.t = 1; v.n = after.num(); }
            s->onFlagChanged(key, v);
        });
        i = j + 1;
    }
}
