#include "s2x.h"
#include "s2m_bodies.h"
#include "s2m_damage.h"
#include "s2m_lock.h"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    struct Obj
    {
        std::string id, full;
        int kind = 0;
        double hp = 100, mx = 100, bleed = 0, x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, capr = 34, caph = 88;
        bool alive = true, wounded = false, live = true, pawn = false, god = false;
        int64_t uid = -1;
        int focus = -1, cm = -1, cap = -1, pwn = -1;
    };
    std::vector<Obj> W;
    double NOW = 0;
    int LHC = 0;
    double LHAT = 0;
    std::string ROLE = "client";
    std::map<std::string, x0::Val> FL;
    std::map<std::string, std::string> X1V[x0::kMaxClients];
    x0::Snap SNAPV[x0::kMaxClients];
    x0::Peer PEERV[x0::kMaxClients];
    std::map<std::string, std::string>& X1 = X1V[0];
    x0::Snap& SNAP = SNAPV[0];
    x0::Peer& PEER = PEERV[0];
    int NVIEWS = 1, MAXC = 1, MYPID = -1;
    bool HUB = false;
    int TGTV[x0::kMaxClients] = {-1, -1, -1, -1}, PUPV[x0::kMaxClients] = {-1, -1, -1, -1};
    bool PARKV[x0::kMaxClients] = {};
    m01::Dmg DMGV[x0::kMaxClients];
    std::map<std::string, x0::ProducerV> TAGV;
    x0::Iface IF;
    int MED = -1, PC = -1, ME = -1, TGT = -1, PUP = -1, BLD = -1, RK = -1, MH = -1;
    bool OTHERGOD = false;
    std::map<std::string, int> INV;
    std::function<void()> KEYF;
    bool KEYDOWN = false;
    bool TAKEOK = true;
    bool PARKED = false, CINE = false, CINEFZ = false;
    m01::Dmg DMG;
    std::map<std::string, std::function<void()>> ROWS;
    std::map<std::string, x0::Producer> TAGS;
    std::vector<std::pair<void (*)(), std::string>> INITS;
    bool LOGON = false;
    int LOADTRIES = 0;
    int LOADBUILDS = 0;
    bool LOADBLOCK = false;
    bool GT = true;
    int KDHOLD = 1;
    double KDOUT = -1;
    double LOWHP = -1;
    std::vector<s2m::damage::FfHit> FFQ;
    std::string FFB = "?";
    bool FFHOOK = true;
    std::vector<s2m::damage::MdHit> MDQ;
    bool MDHOOK = true;

    std::string ws2s(const wchar_t* w)
    {
        std::string s;
        for (; w && *w; ++w) s += (char)*w;
        return s;
    }

    int idx_of(const x0::Ref& r)
    {
        if (r.idx < 0 || r.idx >= (int)W.size() || !W[r.idx].live) return -1;
        return r.idx;
    }

    x0::Ref ref_of(int i)
    {
        x0::Ref r;
        if (i < 0) return r;
        r.idx = i;
        r.serial = 1;
        r.raw = reinterpret_cast<void*>((uintptr_t)(0x1000 + i * 16));
        return r;
    }

    void out(const std::string& s) { std::printf("%s\n", s.c_str()); }

    std::string f2(const char* f, double v)
    {
        char b[64];
        std::snprintf(b, sizeof b, f, v);
        return b;
    }

    struct CD
    {
        std::string fn;
        std::map<std::string, double> num;
        std::map<std::string, std::string> nm;
        std::map<std::string, int> ob;
        double rnum = 0;
        bool rbool = false;
        int robj = -1;
        x0::Vec3 rvec;
        x0::Rot rrot;
    };

    bool dispatch(int i, CD& c)
    {
        if (i < 0) return false;
        Obj& o = W[i];
        const std::string& f = c.fn;
        if (f == "GetHP") { if (!o.pawn) return false; c.rnum = o.hp; return true; }
        if (f == "GetMaxHP") { if (!o.pawn) return false; c.rnum = o.mx; return true; }
        if (f == "IsAlive") { if (!o.pawn) return false; c.rbool = o.alive; return true; }
        if (f == "GetBleeding") { if (!o.pawn) return false; c.rnum = o.bleed; return true; }
        if (f == "IsWounded") { c.rbool = o.wounded; return true; }
        if (f == "IsFallingWounded") { c.rbool = false; return true; }
        if (f == "K2_GetActorLocation") { c.rvec = {o.x, o.y, o.z}; return true; }
        if (f == "GetVelocity") { c.rvec = {o.vx, o.vy, o.vz}; return true; }
        if (f == "GetScaledCapsuleRadius") { c.rnum = W[o.pwn].capr; return true; }
        if (f == "GetScaledCapsuleHalfHeight") { c.rnum = W[o.pwn].caph; return true; }
        if (f == "IsLocalPlayerController") { c.rbool = i == PC; return true; }
        if (f == "ForceSetHP")
        {
            o.hp = (double)(int64_t)c.num["HP"];
            out("CALL " + o.id + " ForceSetHP " + std::to_string((long long)(int64_t)c.num["HP"]));
            return true;
        }
        if (f == "ForceSetBleeding")
        {
            o.bleed = c.num["Bleeding"];
            out("CALL " + o.id + " ForceSetBleeding " + f2("%.2f", o.bleed));
            return true;
        }
        if (f == "DisableMovement") { out("CALL " + o.id + " DisableMovement"); return true; }
        if (f == "SetMovementMode")
        {
            out("CALL " + o.id + " SetMovementMode " + std::to_string((int)c.num["NewMovementMode"]) + " " + std::to_string((int)c.num["NewCustomMode"]));
            return true;
        }
        if (f == "ReceiveDamageStruct")
        {
            double d = c.num["BriefDamageData.Damage"];
            auto u = c.num.find("BriefDamageData.AttackerUID.UID");
            std::string us = u == c.num.end() ? "-" : std::to_string((long long)u->second);
            out("CALL " + o.id + " RDS dmg=" + f2("%.2f", d) + " ap=" + f2("%.2f", c.num["BriefDamageData.ArmorPiercing"]) + " uid=" + us + " bone=" +
                c.nm["BriefDamageData.BoneHit"] + " type=" + std::to_string((int)c.num["BriefDamageData.DamageType"]) + " src=" +
                std::to_string((int)c.num["BriefDamageData.DamageSource"]));
            o.hp = std::max(0.0, o.hp - d);
            if (o.hp <= 0 && !o.god) o.alive = false;
            return true;
        }
        if (f == "ReceiveDamage")
        {
            double d = c.num["InDamage"];
            out("CALL " + o.id + " RD dmg=" + f2("%.2f", d) + " bone=" + c.nm["BoneHit"]);
            o.hp = std::max(0.0, o.hp - d);
            if (o.hp <= 0 && !o.god) o.alive = false;
            return true;
        }
        if (f == "GetGUID")
        {
            int a = c.ob.count("Actor") ? c.ob["Actor"] : -1;
            if (a < 0 || W[a].uid < 0) return false;
            c.rnum = (double)W[a].uid;
            return true;
        }
        if (f == "XSetEntityUnkillableByUID")
        {
            int64_t u = (int64_t)c.num["EntityUID"];
            bool on = c.num["bGodModeState"] != 0;
            for (auto& w : W)
                if (w.live && w.pawn && w.uid == u) w.god = on;
            out("CALL " + o.id + " XSetEntityUnkillableByUID " + std::to_string((long long)u) + " " + (on ? "true" : "false"));
            return true;
        }
        if (f == "XApplyEffectOnPlayer" || f == "XRemoveEffectFromPlayer")
        {
            out("CALL " + o.id + " " + f + " " + c.nm["EffectPrototypeSID"]);
            return true;
        }
        if (f == "GetFocusedEnemy")
        {
            int a = c.ob.count("Obj") ? c.ob["Obj"] : -1;
            c.robj = a >= 0 ? W[a].focus : -1;
            return true;
        }
        if (f == "GetOwner") { c.robj = -1; return true; }
        if (f == "GetItemsCountInInventory" && i == MED)
        {
            std::string sid = c.nm["ItemName"];
            c.rnum = INV.count(sid) ? INV[sid] : 0;
            return true;
        }
        return false;
    }

    int add(const std::string& id, const std::string& full, bool pawn)
    {
        Obj o;
        o.id = id;
        o.full = full;
        o.pawn = pawn;
        W.push_back(o);
        return (int)W.size() - 1;
    }

    int find(const std::string& id)
    {
        if (id == "-") return -1;
        for (int i = (int)W.size() - 1; i >= 0; --i)
            if (W[i].live && W[i].id == id) return i;
        return -1;
    }

    x0::Val parse_val(const std::string& v)
    {
        char* e = nullptr;
        long long n = std::strtoll(v.c_str(), &e, 10);
        if (!v.empty() && *e == 0) return x0::Val::I(n);
        double d = std::strtod(v.c_str(), &e);
        if (!v.empty() && *e == 0) return x0::Val::F(d);
        return x0::Val::S(v);
    }

    void load_flags(const std::string& path)
    {
        std::ifstream f(path);
        std::string line;
        bool head = true;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (head) { head = false; continue; }
            std::vector<std::string> c;
            std::stringstream ss(line);
            std::string cell;
            while (std::getline(ss, cell, '\t')) c.push_back(cell);
            if (c.size() < 3 || c[0].empty()) continue;
            std::string k = c[0].substr(1);
            if (c[1] == "i") FL[k] = x0::Val::I(std::strtoll(c[2].c_str(), nullptr, 10));
            else if (c[1] == "f") FL[k] = x0::Val::F(std::strtod(c[2].c_str(), nullptr));
            else FL[k] = x0::Val::S(c[2]);
        }
    }

    std::string data_dir;
}

namespace x0
{
    const char* mod_name(Mod) { return "damage"; }
    bool port(Mod) { return true; }
    ModReg::ModReg(Mod, const char* id, void (*init)()) { INITS.push_back({init, id}); }
    double now() { return NOW; }
    const std::string& role() { return ROLE; }
    bool is_host() { return ROLE == "host"; }
    bool on_game_thread() { return GT; }
    bool is_client() { return ROLE == "client"; }
    void log(std::string_view text)
    {
        if (LOGON) out("LOG " + std::string(text));
    }
    std::vector<std::string> UIREC;
    void ui_msg(std::string_view id, std::string_view text, double secs)
    {
        UIREC.push_back("MSG " + std::string(id) + " " + std::string(text) + " " + std::to_string((int)secs));
    }
    void screen_text_clear(std::string_view id)
    {
        UIREC.push_back("CLEAR " + std::string(id));
    }
    void ui_top(std::string_view id, std::string_view text, double secs)
    {
        UIREC.push_back("TOP " + std::string(id) + " " + std::string(text) + " " + std::to_string((int)secs));
    }
    void ui_top_clear(std::string_view id)
    {
        UIREC.push_back("TOPCLEAR " + std::string(id));
    }
    void logf(const char* fmt, ...)
    {
        char b[4096];
        va_list a;
        va_start(a, fmt);
        vsnprintf(b, sizeof b, fmt, a);
        va_end(a);
        log(b);
    }
    double Val::num() const { return k == i ? (double)iv : k == f ? fv : k == s ? std::strtod(sv.c_str(), nullptr) : 0; }
    int64_t Val::integer() const { return k == i ? iv : (int64_t)num(); }
    std::string Val::str() const
    {
        if (k == i) return std::to_string((long long)iv);
        if (k == s) return sv;
        if (k == f)
        {
            char b[64];
            std::snprintf(b, sizeof b, "%.14g", fv);
            std::string r = b;
            if (r.find_first_of(".eEn") == std::string::npos) r += ".0";
            return r;
        }
        return "nil";
    }
    bool Val::operator==(const Val& o) const { return k == o.k && iv == o.iv && fv == o.fv && sv == o.sv; }
    Val Val::I(int64_t v) { Val x; x.k = i; x.iv = v; return x; }
    Val Val::F(double v) { Val x; x.k = f; x.fv = v; return x; }
    Val Val::S(std::string v) { Val x; x.k = s; x.sv = std::move(v); return x; }
    Val flag(std::string_view key)
    {
        auto it = FL.find(std::string(key));
        return it == FL.end() ? Val{} : it->second;
    }
    int64_t fi(std::string_view key, int64_t def)
    {
        auto it = FL.find(std::string(key));
        return it == FL.end() ? def : it->second.integer();
    }
    const Val* Flag::res() const
    {
        static Val nil;
        auto it = FL.find(key_);
        return it == FL.end() ? &nil : &it->second;
    }
    int64_t Flag::i() const
    {
        return res()->integer();
    }
    double Flag::d() const { return res()->num(); }
    std::string Flag::s() const { return res()->str(); }

    UObject* Ref::get() const
    {
        if (idx_of(*this) < 0) return nullptr;
        return reinterpret_cast<UObject*>(raw);
    }
    std::vector<Ref> all_of(const wchar_t* cls, double)
    {
        std::string c = ws2s(cls);
        std::vector<Ref> r;
        for (int i = 0; i < (int)W.size(); ++i)
        {
            if (!W[i].live) continue;
            if (c == "Pawn" && W[i].pawn) r.push_back(ref_of(i));
            if (c == "PlayerController" && i == PC) r.push_back(ref_of(i));
        }
        return r;
    }
    Ref first_of(const wchar_t* cls)
    {
        if (ws2s(cls) == "CustomConsoleManagerBuild") return ref_of(BLD);
        if (ws2s(cls) == "CustomConsoleManagerRK") return ref_of(RK);
        if (ws2s(cls) == "CustomConsoleManagerMH") return ref_of(MH);
        return {};
    }
    Ref find_path(const wchar_t* path)
    {
        if (ws2s(path) == "/Script/Stalker2.Default__CppMediator") return ref_of(MED);
        return {};
    }
    std::string full_name(const Ref& r)
    {
        int i = idx_of(r);
        return i < 0 ? "" : W[i].full;
    }
    std::string class_name(const Ref& r)
    {
        std::string f = full_name(r);
        return f.substr(0, f.find(' '));
    }

    std::string obj_name(const Ref& r)
    {
        std::string f = full_name(r);
        size_t d = f.find_last_of(". ");
        return d == std::string::npos ? f : f.substr(d + 1);
    }
    bool is_a(const Ref& r, const wchar_t*) { return false; }

    Call::Call(const Ref& self, const wchar_t* fn) : self_(self)
    {
        auto* c = new CD;
        c->fn = ws2s(fn);
        fn_ = c;
    }
    Call::~Call() { delete static_cast<CD*>(fn_); }
    Call& Call::i(const wchar_t* p, int64_t v) { static_cast<CD*>(fn_)->num[ws2s(p)] = (double)v; return *this; }
    Call& Call::f(const wchar_t* p, float v) { static_cast<CD*>(fn_)->num[ws2s(p)] = (double)v; return *this; }
    Call& Call::d(const wchar_t* p, double v) { static_cast<CD*>(fn_)->num[ws2s(p)] = v; return *this; }
    Call& Call::b(const wchar_t* p, bool v) { static_cast<CD*>(fn_)->num[ws2s(p)] = v ? 1 : 0; return *this; }
    Call& Call::o(const wchar_t* p, const Ref& v) { static_cast<CD*>(fn_)->ob[ws2s(p)] = idx_of(v); return *this; }
    Call& Call::n(const wchar_t* p, const wchar_t* fname) { static_cast<CD*>(fn_)->nm[ws2s(p)] = ws2s(fname); return *this; }
    Call& Call::s(const wchar_t* p, const std::wstring& v) { static_cast<CD*>(fn_)->nm[ws2s(p)] = ws2s(v.c_str()); return *this; }
    bool Call::run()
    {
        ran_ = dispatch(idx_of(self_), *static_cast<CD*>(fn_));
        return ran_;
    }
    bool Call::rb(const wchar_t*) const { return static_cast<CD*>(fn_)->rbool; }
    int64_t Call::ri(const wchar_t*) const { return (int64_t)static_cast<CD*>(fn_)->rnum; }
    float Call::rf(const wchar_t*) const { return (float)static_cast<CD*>(fn_)->rnum; }
    double Call::rd(const wchar_t*) const { return static_cast<CD*>(fn_)->rnum; }
    Ref Call::ro(const wchar_t*) const { return ref_of(static_cast<CD*>(fn_)->robj); }
    std::vector<Ref> Call::rarr(const wchar_t*) const { return {}; }
    bool Call::rv(Vec3& o, const wchar_t*) const { o = static_cast<CD*>(fn_)->rvec; return ran_; }
    bool Call::rr(Rot& o, const wchar_t*) const { o = static_cast<CD*>(fn_)->rrot; return ran_; }
    bool call0(const Ref& self, const wchar_t* fn) { Call c(self, fn); return c.run(); }
    bool call_b(const Ref& self, const wchar_t* fn, bool def) { Call c(self, fn); return c.run() ? c.rb() : def; }
    double call_d(const Ref& self, const wchar_t* fn, double def) { Call c(self, fn); return c.run() ? c.rd() : def; }
    Ref call_o(const Ref& self, const wchar_t* fn) { Call c(self, fn); return c.run() ? c.ro() : Ref{}; }
    bool call_v(const Ref& self, const wchar_t* fn, Vec3& out) { Call c(self, fn); return c.run() && c.rv(out); }

    template <class T> bool prop(const Ref& self, const char* path, T& out);
    template <> bool prop<Ref>(const Ref& self, const char* path, Ref& out)
    {
        int i = idx_of(self);
        if (i < 0) return false;
        std::string p = path;
        if (p == "Pawn" && i == PC) { out = ref_of(ME); return true; }
        if (p == "CharacterMovement" && W[i].cm >= 0) { out = ref_of(W[i].cm); return true; }
        if (p == "CapsuleComponent" && W[i].cap >= 0) { out = ref_of(W[i].cap); return true; }
        return false;
    }

    const Snap& remote() { return SNAP; }
    bool rpart(std::string_view tag, std::string& o)
    {
        auto it = X1.find(std::string(tag));
        if (it == X1.end()) return false;
        o = it->second;
        return true;
    }
    const Peer& peer() { return PEER; }
    bool coop()
    {
        for (int v = 0; v < NVIEWS; ++v)
        {
            const Peer& p = PEERV[v];
            if (!p.loopback && (p.state == Peer::up || (p.state == Peer::silent && !p.left))) return true;
        }
        return false;
    }
    int views() { return NVIEWS; }
    int max_clients() { return MAXC; }
    bool hub() { return HUB; }
    const Snap& remote(int v)
    {
        static Snap none;
        return v >= 0 && v < NVIEWS ? SNAPV[v] : none;
    }
    const Peer& peer(int v)
    {
        static Peer none;
        return v >= 0 && v < NVIEWS ? PEERV[v] : none;
    }
    bool rpart(std::string_view tag, std::string& o, int v)
    {
        if (v < 0 || v >= NVIEWS) return false;
        auto it = X1V[v].find(std::string(tag));
        if (it == X1V[v].end()) return false;
        o = it->second;
        return true;
    }
    int view_pid(int v)
    {
        if (v < 0 || v >= NVIEWS) return -1;
        if (is_host()) return v + 1;
        return v == 0 ? 0 : SNAPV[v].pid;
    }
    int my_pid() { return MYPID >= 0 ? MYPID : is_host() ? 0 : -1; }
    void each_view(const std::function<void(int view, const Snap& s, const Peer& p)>& fn)
    {
        for (int v = 0; v < NVIEWS; ++v) fn(v, SNAPV[v], PEERV[v]);
    }
    bool own_tag_view(Mod, std::string_view tag, ProducerV p) { TAGV[std::string(tag)] = std::move(p); return true; }
    bool own_tag(Mod, std::string_view tag, Producer p) { TAGS[std::string(tag)] = std::move(p); return true; }
    int every(Mod, const char* name, Rate, std::function<void()> fn) { ROWS[name] = std::move(fn); return 1; }
    int hook(Mod, const wchar_t*, bool, std::function<void(const Ref&)>) { return 1; }
    int key(Mod, int vk, std::function<void()> on_press, std::initializer_list<int>, bool)
    {
        if (vk == 'F') KEYF = std::move(on_press);
        return 1;
    }
    bool key_down(int vk) { return vk == 'F' && KEYDOWN; }
    std::function<std::string()> STATUSF;
    void status(Mod, std::function<std::string()> f) { STATUSF = std::move(f); }
    Iface& iface() { return IF; }
    bool data_tsv(std::string_view name, std::vector<TsvRow>& rows, std::string* err)
    {
        if (name == "revive_items.tsv" || name == "revive_heal.tsv")
        {
            std::ifstream f(data_dir + "\\" + std::string(name));
            std::string line;
            bool head = true;
            int kcols = 0;
            while (std::getline(f, line))
            {
                if (!line.empty() && line.back() == '') line.pop_back();
                std::vector<std::string> c;
                size_t b = 0;
                while (true)
                {
                    size_t t = line.find('	', b);
                    c.push_back(line.substr(b, t == std::string::npos ? std::string::npos : t - b));
                    if (t == std::string::npos) break;
                    b = t + 1;
                }
                if (head) { head = false; kcols = (int)c.size() - 2; continue; }
                if ((int)c.size() != kcols + 2) continue;
                TsvRow r;
                for (int k = 0; k < kcols; ++k)
                {
                    if (c[k].empty()) break;
                    r.ktype += c[k][0];
                    r.key.push_back(c[k].substr(1));
                }
                r.t = c[kcols].empty() ? 's' : c[kcols][0];
                r.v = c[kcols + 1];
                rows.push_back(r);
            }
            if (!rows.empty()) return true;
        }
        if (err) *err = "teste";
        return false;
    }
}

namespace s2m_lock
{
    bool shot_deny(const x0::Ref&) { return false; }
}

namespace m01
{
    x0::Ref puppet() { return ref_of(PUP); }
    x0::Ref target() { return ref_of(TGT); }
    std::optional<int64_t> target_uid()
    {
        if (TGT < 0 || W[TGT].uid < 0) return std::nullopt;
        return W[TGT].uid;
    }
    bool target_parked() { return PARKED; }
    Dmg& dmg() { return DMG; }
    x0::Ref puppet(int v) { return v == 0 ? ref_of(PUP) : v > 0 && v < x0::kMaxClients ? ref_of(PUPV[v]) : x0::Ref{}; }
    x0::Ref target(int v) { return v == 0 ? ref_of(TGT) : v > 0 && v < x0::kMaxClients ? ref_of(TGTV[v]) : x0::Ref{}; }
    std::optional<int64_t> target_uid(int v)
    {
        x0::Ref t = target(v);
        int i = idx_of(t);
        if (i < 0 || W[i].uid < 0) return std::nullopt;
        return W[i].uid;
    }
    bool target_parked(int v) { return v == 0 ? PARKED : v > 0 && v < x0::kMaxClients && PARKV[v]; }
    Dmg& dmg(int v) { return v <= 0 || v >= x0::kMaxClients ? DMG : DMGV[v]; }
    bool is_m1_body(const x0::Ref& r)
    {
        int i = idx_of(r);
        if (i < 0) return false;
        for (int v = 0; v < x0::kMaxClients; ++v)
            if (idx_of(target(v)) == i || idx_of(puppet(v)) == i) return true;
        return false;
    }
    void note_local_shot(const x0::Vec3&, const x0::Vec3&) {}
    void ff_flinch(int, double, const std::string&) {}
}

extern "C" bool s2m_life_gm_keep(bool on)
{
    out(std::string("CALL gm_keep ") + (on ? "1" : "0"));
    return true;
}

extern "C" bool s2m_life_load_block(bool on)
{
    LOADBLOCK = on;
    out(std::string("CALL load_block ") + (on ? "1" : "0"));
    return true;
}

extern "C" int s2m_life_knock(uint64_t, int on)
{
    if (on >= 0) out(std::string("CALL knock ") + (on ? "1" : "0"));
    return 1;
}

extern "C" int s2m_life_kdhold(uint64_t, int on)
{
    if (on >= 0) out("CALL kdhold " + std::to_string(on));
    return on == 1 ? KDHOLD : 1;
}

extern "C" double s2m_life_kdout_left(uint64_t) { return KDOUT; }

extern "C" double s2m_life_lowhp() { return LOWHP; }

extern "C" int s2m_life_load_tries() { return LOADTRIES; }

extern "C" int s2m_life_load_hidden(int* builds)
{
    if (builds) *builds = LOADBUILDS;
    return LOADBUILDS * 2;
}

int main(int argc, char** argv)
{
    if (argc < 3) return 2;
    data_dir = argv[1];
    load_flags(data_dir + "\\flags_s2core.tsv");
    load_flags(data_dir + "\\flags_main.tsv");
    MED = add("med", "CppMediator /Script/Stalker2.Default__CppMediator", false);
    PC = add("pc", "PlayerController /Game:PersistentLevel.pc", false);
    BLD = add("build", "CustomConsoleManagerBuild /Game:PersistentLevel.build", false);
    RK = add("rk", "CustomConsoleManagerRK /Game:PersistentLevel.rk", false);
    MH = add("mh", "CustomConsoleManagerMH /Game:PersistentLevel.mh", false);
    for (auto& r : INITS) r.first();
    s2m::damage::links().host_cine = [] { return CINE; };
    s2m::damage::links().cine_frozen = [] { return CINEFZ; };
    s2m::damage::links().god_other = [] { return OTHERGOD; };
    s2m::damage::links().nd_on = [] { return x0::fi("natdmg", 0) == 1; };
    s2m::damage::links().item_take = [](const std::string& sid) {
        out("CALL itemRemove P " + sid + " 1 -> " + (TAKEOK ? "true" : "false"));
        if (TAKEOK && INV[sid] > 0) INV[sid]--;
        return TAKEOK;
    };
    s2m::damage::links().shot_deny = [](const x0::Ref&) { return false; };
    s2m::damage::links().last_cause = [](double& age) { int c = LHC; LHC = 0; age = c ? NOW - LHAT : 1e9; return c; };
    s2m::damage::links().die_follow = [] { out("CALL die_follow"); };
    s2m::damage::links().ff_bodies = [](bool on, const std::vector<s2m::damage::FfBody>& b) {
        std::string t = on ? "on" : "off";
        for (const auto& x : b) t += " v" + std::to_string(x.view) + ":p" + std::to_string(x.pid) + ":" + (idx_of(x.body) >= 0 ? W[idx_of(x.body)].id : std::string("?"));
        if (t == FFB) return;
        FFB = t;
        out("CALL ff_bodies " + t);
    };
    s2m::damage::links().ff_take = [] {
        std::vector<s2m::damage::FfHit> q;
        q.swap(FFQ);
        return q;
    };
    s2m::damage::links().ff_apply = [](const x0::Ref& pw, const s2m::damage::FfHit& h) {
        int i = idx_of(pw);
        if (i < 0 || !FFHOOK) return 0;
        Obj& o = W[i];
        out("CALL " + o.id + " ff_apply dmg=" + f2("%.2f", h.f[0]) + " arm=" + f2("%.2f", h.f[1]) + " ap=" + f2("%.2f", h.f[2]) + " bleed=" + f2("%.2f", h.f[3]) + "/" +
            f2("%.2f", h.f[4]) + " bone=" + h.bone + " bn=" + std::to_string((long long)h.bn) + " type=" + std::to_string(h.ty) + " src=" + std::to_string(h.src) +
            " flags=" + std::to_string(h.b7a) + "/" + std::to_string(h.b7b) + " v=" + f2("%.1f", h.v[0]) + "," + f2("%.1f", h.v[5]));
        o.hp = std::max(0.0, o.hp - h.f[0]);
        if (o.hp <= 0 && !o.god) o.alive = false;
        return 1;
    };
    s2m::damage::links().ff_stat = [](bool& any) { any = false; return std::string(); };
    s2m::damage::links().md_cfg = [](bool on) { out(std::string("CALL md_cfg ") + (on ? "on" : "off")); };
    s2m::damage::links().md_take = [] {
        std::vector<s2m::damage::MdHit> q;
        q.swap(MDQ);
        return q;
    };
    s2m::damage::links().md_find = [](int64_t uid) {
        for (size_t i = 0; i < W.size(); ++i)
            if (W[i].live && W[i].pawn && W[i].uid == uid) return ref_of((int)i);
        return x0::Ref{};
    };
    s2m::damage::links().md_apply = [](const x0::Ref& npc, const s2m::damage::MdHit& h, int64_t atk) {
        int i = idx_of(npc);
        if (i < 0 || !MDHOOK) return 0;
        Obj& o = W[i];
        out("CALL " + o.id + " md_apply dmg=" + f2("%.2f", h.f[0]) + " arm=" + f2("%.2f", h.f[1]) + " bone=" + h.bone + " type=" + std::to_string(h.ty) + " src=" + std::to_string(h.src) +
            " atk=" + std::to_string((long long)atk));
        o.hp = std::max(0.0, o.hp - h.f[0]);
        if (o.hp <= 0) o.alive = false;
        return 1;
    };
    s2m::damage::links().md_stat = [](bool& any) { any = false; return std::string(); };
    PEER.state = x0::Peer::up;
    SNAP.ok = true;
    LOGON = true;
    std::ifstream f(argv[2]);
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string cmd;
        ss >> cmd;
        std::vector<std::string> a;
        std::string w;
        while (ss >> w) a.push_back(w);
        auto D = [&](size_t k) { return std::strtod(a[k].c_str(), nullptr); };
        if (cmd == "role") ROLE = a[0];
        else if (cmd == "flag") FL[a[0]] = parse_val(a[1]);
        else if (cmd == "t") NOW = D(0);
        else if (cmd == "uidump")
        {
            for (auto& s : x0::UIREC) out("OUT ui " + s);
            x0::UIREC.clear();
        }
        else if (cmd == "me" || cmd == "newpawn")
        {
            Obj keep;
            if (ME >= 0) { keep = W[ME]; W[ME].live = false; if (W[ME].cm >= 0) W[W[ME].cm].live = false; }
            ME = add("me", "BP_Stalker2Character_C /Game:PersistentLevel.me", true);
            int cm = add("me.cm", "CharacterMovementComponent /Game:PersistentLevel.me.cm", false);
            W[ME].cm = cm;
            if (cmd == "newpawn") { W[ME].hp = keep.hp; W[ME].alive = keep.alive; W[ME].x = keep.x; W[ME].y = keep.y; W[ME].z = keep.z; W[ME].uid = keep.uid; }
            else { W[ME].hp = D(0); W[ME].alive = a[1] == "1"; W[ME].x = D(2); W[ME].y = D(3); W[ME].z = D(4); }
        }
        else if (cmd == "set")
        {
            int i = find(a[0]);
            std::string k = a[1];
            if (k == "hp") W[i].hp = D(2);
            else if (k == "alive") W[i].alive = a[2] == "1";
            else if (k == "bleed") W[i].bleed = D(2);
            else if (k == "uid") W[i].uid = std::strtoll(a[2].c_str(), nullptr, 10);
            else if (k == "focus") W[i].focus = find(a[2]);
            else if (k == "wounded") W[i].wounded = a[2] == "1";
            else if (k == "vel") { W[i].vx = D(2); W[i].vy = D(3); W[i].vz = D(4); }
            else if (k == "pos") { W[i].x = D(2); W[i].y = D(3); W[i].z = D(4); }
        }
        else if (cmd == "npc")
        {
            std::string full = "BP_AI_" + a[1] + "_C /Game/Maps/World:PersistentLevel.BP_AI_" + a[1] + "_C_" + a[0];
            int i = add(a[0], full, true);
            W[i].x = D(2); W[i].y = D(3); W[i].z = D(4); W[i].hp = D(5); W[i].mx = D(5); W[i].alive = a[6] == "1";
            int c = add(a[0] + ".cap", "CapsuleComponent /Game:PersistentLevel." + a[0] + ".cap", false);
            W[i].cap = c;
            W[c].pwn = i;
            if (a.size() >= 9) { W[i].capr = D(7); W[i].caph = D(8); }
        }
        else if (cmd == "cause") { LHC = 0x10000 | (int)D(0) | ((int)D(1) << 8); LHAT = NOW; }
        else if (cmd == "tgt") TGT = find(a[0]);
        else if (cmd == "pup") PUP = find(a[0]);
        else if (cmd == "park") PARKED = a[0] == "1";
        else if (cmd == "cine") CINE = a[0] == "1";
        else if (cmd == "cinefz") CINEFZ = a[0] == "1";
        else if (cmd == "othergod") OTHERGOD = a[0] == "1";
        else if (cmd == "peer") { PEER.state = a[0] == "up" ? x0::Peer::up : x0::Peer::silent; PEER.left = a[0] == "left"; }
        else if (cmd == "nopawn") { if (ME >= 0) W[ME].live = false; }
        else if (cmd == "kdout") KDOUT = D(0);
        else if (cmd == "lowhp") LOWHP = D(0);
        else if (cmd == "kdhold") KDHOLD = (int)D(0);
        else if (cmd == "gt") GT = a[0] == "1";
        else if (cmd == "unload") s2m_downview_unload();
        else if (cmd == "inv") INV[a[0]] = (int)D(1);
        else if (cmd == "invq") out("OUT inv " + a[0] + "=" + std::to_string(INV[a[0]]));
        else if (cmd == "takeok") TAKEOK = a[0] == "1";
        else if (cmd == "keyf") { if (KEYF) KEYF(); }
        else if (cmd == "keydown") KEYDOWN = a[0] == "1";
        else if (cmd == "loadtry")
        {
            if (LOADBLOCK) LOADTRIES++;
            out(std::string("GAME load click -> ") + (LOADBLOCK ? "bloqueado" : "carrega"));
        }
        else if (cmd == "deathscreen")
        {
            if (LOADBLOCK) LOADBUILDS++;
            out(std::string("GAME death screen built -> ") + (LOADBLOCK ? "sem carregar" : "4 botoes"));
        }
        else if (cmd == "hit")
        {
            Obj& o = W[find(a[0])];
            o.hp = std::max(0.0, o.hp - D(1));
            if (o.hp <= 0 && !o.god) o.alive = false;
            out("GAME hit " + o.id + " " + f2("%.1f", D(1)) + " -> hp=" + f2("%.1f", o.hp) + " alive=" + (o.alive ? "true" : "false") + " god=" + (o.god ? "true" : "false"));
        }
        else if (cmd == "godq") out(std::string("OUT god_held=") + (s2m_damage_god_held() ? "true" : "false") + " bit=" + (W[ME].god ? "true" : "false"));
        else if (cmd == "views")
        {
            NVIEWS = (int)D(0);
            MAXC = (int)D(1);
            HUB = a[2] == "1";
            MYPID = (int)D(3);
        }
        else if (cmd == "vremote")
        {
            x0::Snap& s = SNAPV[(int)D(0)];
            s.ok = true;
            s.has_pos = true;
            s.x = D(1); s.y = D(2); s.z = D(3); s.recv = D(4);
            if (a.size() > 5) s.pid = (int)D(5);
        }
        else if (cmd == "vnosnap") SNAPV[(int)D(0)] = x0::Snap{};
        else if (cmd == "vpeer")
        {
            x0::Peer& p = PEERV[(int)D(0)];
            p.state = a[1] == "up" ? x0::Peer::up : a[1] == "none" ? x0::Peer::none : x0::Peer::silent;
            p.left = a.size() > 2 && a[2] == "left";
            if (a.size() > 3) { p.id = a[3]; p.sessions = (int)D(4); }
        }
        else if (cmd == "vx1")
        {
            auto& m = X1V[(int)D(0)];
            if (a.size() < 3 || a[2] == "-") m.erase(a[1]);
            else m[a[1]] = a[2] == "=" ? "" : a[2];
        }
        else if (cmd == "vtgt") TGTV[(int)D(0)] = find(a[1]);
        else if (cmd == "vpup") PUPV[(int)D(0)] = find(a[1]);
        else if (cmd == "vdev") DMGV[(int)D(0)].list.push_back({std::strtoll(a[1].c_str(), nullptr, 10), D(2), D(3)});
        else if (cmd == "vshot")
        {
            SNAPV[(int)D(0)].shots = a[1];
            ROWS["shotsIn"]();
            SNAPV[(int)D(0)].shots.clear();
        }
        else if (cmd == "remote") { SNAP.has_pos = true; SNAP.x = D(0); SNAP.y = D(1); SNAP.z = D(2); SNAP.recv = D(3); }
        else if (cmd == "x1")
        {
            if (a.size() < 2 || a[1] == "-") X1.erase(a[0]);
            else X1[a[0]] = a[1] == "=" ? "" : a[1];
        }
        else if (cmd == "dseq") { DMG.seq = std::strtoll(a[0].c_str(), nullptr, 10); DMG.dropped = std::strtoll(a[1].c_str(), nullptr, 10); }
        else if (cmd == "nd")
        {
            if (a[0] == "send") out(std::string("OUT nd_send_allowed=") + (s2m::damage::nd_send_allowed() ? "true" : "false"));
            else if (a[0] == "apply") out(std::string("OUT nd_apply_allowed=") + (s2m::damage::nd_apply_allowed() ? "true" : "false"));
            else if (a[0] == "sent") s2m::damage::nd_sent(D(1));
            else if (a[0] == "applied") s2m::damage::nd_applied(D(1), D(2), D(3));
        }
        else if (cmd == "ffcap")
        {
            s2m::damage::FfHit h;
            h.view = (int)D(0);
            h.pid = (int)D(1);
            h.f[0] = D(2);
            h.f[1] = D(3);
            h.f[2] = D(4);
            h.f[3] = 0.5;
            h.f[4] = 0.25;
            h.bone = a[5];
            h.ty = a.size() > 6 ? (int)D(6) : 6;
            h.src = a.size() > 7 ? (int)D(7) : 3;
            h.puppet = a.size() > 8 && a[8] == "p";
            h.b7a = 1;
            h.b7b = 0;
            h.bn = 0;
            h.v[0] = 100.5;
            h.v[5] = -0.75;
            FFQ.push_back(h);
        }
        else if (cmd == "ffhook") FFHOOK = a[0] == "1";
        else if (cmd == "mdcap")
        {
            s2m::damage::MdHit h;
            h.uid = std::strtoll(a[0].c_str(), nullptr, 10);
            h.f[0] = D(1);
            h.f[1] = D(2);
            h.f[2] = 0.5;
            h.bone = a[3];
            h.ty = a.size() > 4 ? (int)D(4) : 6;
            h.src = a.size() > 5 ? (int)D(5) : 11;
            h.b7a = 1;
            MDQ.push_back(h);
        }
        else if (cmd == "mdhook") MDHOOK = a[0] == "1";
        else if (cmd == "dev") DMG.list.push_back({std::strtoll(a[0].c_str(), nullptr, 10), D(1), D(2)});
        else if (cmd == "do")
        {
            const std::string& n = a[0];
            if (n == "dmgStr") out("OUT " + TAGS["D"]());
            else if (n == "lifeStr") out("OUT " + TAGS["L"]());
            else if (n == "rvStr") out("OUT " + TAGS["RV"]());
            else if (n == "ffStr") out("OUT " + TAGS["FF"]());
            else if (n == "mdStr") out("OUT " + TAGS["MD"]());
            else if (n == "goStr") out("OUT " + TAGS["GO"]());
            else if (n == "statusStr") out("OUT " + x0::STATUSF());
            else if (n == "dvStr") out("OUT v" + a[1] + " " + TAGV["Dv"]((int)D(1)));
            else if (n == "remoteDownV") out(std::string("OUT v") + a[1] + " " + (s2m::damage::remote_down((int)D(1)) ? "true" : "false"));
            else if (n == "ndSendV") out(std::string("OUT v") + a[1] + " nd_send_allowed=" + (s2m::damage::nd_send_allowed((int)D(1)) ? "true" : "false"));
            else if (n == "remoteDown") out(std::string("OUT ") + (s2m::damage::remote_down() ? "true" : "false"));
            else if (n == "listN") out("OUT n=" + std::to_string(DMG.list.size()));
            else ROWS[n]();
        }
        else if (cmd == "shot")
        {
            SNAP.shots = a[0];
            ROWS["shotsIn"]();
            SNAP.shots.clear();
        }
        else if (cmd == "hp") out("OUT " + a[0] + " hp=" + f2("%.1f", W[find(a[0])].hp) + " alive=" + (W[find(a[0])].alive ? "true" : "false"));
    }
    return 0;
}
