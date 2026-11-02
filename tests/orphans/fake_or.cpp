#include "fake_or.h"
#include "s2m_bodies.h"
#include "s2n_nat_api.h"
#include "s2m_quest.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unordered_map>

using fo::Obj;

namespace
{
    struct P
    {
        char k = 0;
        int64_t i = 0;
        double d = 0;
        bool b = false;
        x0::Ref o;
        x0::Vec3 v;
        x0::Rot r;
        std::wstring s;
    };
    struct Impl
    {
        std::wstring fn;
        std::map<std::wstring, P> in, out;
    };
    Impl& I(void* p) { return *static_cast<Impl*>(p); }
    std::set<const void*>& known()
    {
        static std::set<const void*> s;
        return s;
    }
    Obj* O(const x0::Ref& r)
    {
        if (!r.raw || !known().count(r.raw)) return nullptr;
        Obj* o = static_cast<Obj*>(r.raw);
        if (!o->alive || o->serial != r.serial || o->idx != r.idx) return nullptr;
        return o;
    }
    std::string u8(const std::wstring& w)
    {
        std::string s;
        for (wchar_t c : w) s.push_back(c < 128 ? (char)c : '?');
        return s;
    }
    std::wstring wd(const std::string& s)
    {
        std::wstring w;
        for (char c : s) w.push_back((wchar_t)(unsigned char)c);
        return w;
    }
    std::string F(const char* f, ...)
    {
        char b[1024];
        va_list a;
        va_start(a, f);
        std::vsnprintf(b, sizeof b, f, a);
        va_end(a);
        return b;
    }
    P ob(Obj* o) { P p; p.k = 'O'; p.o = fo::ref(o); return p; }
    P bo(bool b) { P p; p.k = 'B'; p.b = b; return p; }
    P in(int64_t i) { P p; p.k = 'I'; p.i = i; return p; }
    P vv(const x0::Vec3& v) { P p; p.k = 'V'; p.v = v; return p; }
    P rr(const x0::Rot& r) { P p; p.k = 'R'; p.r = r; return p; }
    Obj* arg_o(const Impl& c, const wchar_t* n)
    {
        auto it = c.in.find(n);
        return it == c.in.end() ? nullptr : O(it->second.o);
    }
    std::string arg_n(const Impl& c, const wchar_t* n)
    {
        auto it = c.in.find(n);
        return it == c.in.end() ? std::string() : u8(it->second.s);
    }
    bool arg_b(const Impl& c, const wchar_t* n)
    {
        auto it = c.in.find(n);
        return it != c.in.end() && it->second.b;
    }
    std::string nm(Obj* o) { return o ? o->name : std::string("nil"); }
    Obj* top(Obj* s) { return s->owner ? top(s->owner) : s; }
    using H = std::function<bool(Obj&, Impl&)>;
    const std::unordered_map<std::wstring, H>& handlers()
    {
        static const std::unordered_map<std::wstring, H> h = {
            {L"K2_GetActorLocation", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = vv(s.loc); return true; }},
            {L"K2_GetActorRotation", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = rr(s.rot); return true; }},
            {L"K2_SetActorLocation", [](Obj& s, Impl& c) {
                 s.loc = c.in[L"NewLocation"].v;
                 fo::tr(F("call %s K2_SetActorLocation %.1f %.1f %.1f sweep=%d tp=%d", s.name.c_str(), s.loc.x, s.loc.y, s.loc.z, (int)arg_b(c, L"bSweep"), (int)arg_b(c, L"bTeleport")));
                 return true;
             }},
            {L"K2_SetActorRotation", [](Obj& s, Impl& c) {
                 s.rot = c.in[L"NewRotation"].r;
                 fo::tr(F("call %s K2_SetActorRotation %.1f %.1f %.1f", s.name.c_str(), s.rot.pitch, s.rot.yaw, s.rot.roll));
                 return true;
             }},
            {L"IsAlive", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = bo(s.isAlive); return true; }},
            {L"IsInThreat", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = bo(s.threat); return true; }},
            {L"IsSleeping", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = bo(s.sleeping); return true; }},
            {L"GetVelocity", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = vv(s.vel); return true; }},
            {L"GetGUID", [](Obj&, Impl& c) { Obj* a = arg_o(c, L"Actor"); if (!a) return false; c.out[L"ReturnValue"] = in(a->uid); return true; }},
            {L"GetPrototypeID", [](Obj&, Impl& c) { Obj* a = arg_o(c, L"Actor"); if (!a) return false; c.out[L"ReturnValue"] = in(a->pid); return true; }},
            {L"GetController", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.ctl); return true; }},
            {L"StopLogic", [](Obj& s, Impl& c) { fo::tr(F("call %s StopLogic %s", s.name.c_str(), u8(c.in[L"Reason"].s).c_str())); return true; }},
            {L"RestartLogic", [](Obj& s, Impl&) { fo::tr(F("call %s RestartLogic", s.name.c_str())); return true; }},
            {L"SetActorTickEnabled", [](Obj& s, Impl& c) { fo::tr(F("call %s SetActorTickEnabled %d", s.name.c_str(), (int)arg_b(c, L"bEnabled"))); return true; }},
            {L"TogglePathFollowingComponent", [](Obj& s, Impl& c) { fo::tr(F("call %s TogglePathFollowingComponent %d", s.name.c_str(), (int)arg_b(c, L"bEnable"))); return true; }},
            {L"StopMovementImmediately", [](Obj& s, Impl&) { fo::tr(F("call %s StopMovementImmediately", s.name.c_str())); return true; }},
            {L"SetComponentTickEnabled", [](Obj& s, Impl& c) {
                 s.tickOn = arg_b(c, L"bEnabled");
                 fo::tr(F("call %s SetComponentTickEnabled %d", s.name.c_str(), (int)s.tickOn));
                 return true;
             }},
            {L"IsComponentTickEnabled", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = bo(s.tickOn); return true; }},
            {L"GetAnimInstance", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.ai); return true; }},
            {L"GetCurrentActiveMontage", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.mont); return true; }},
            {L"GetFirstAnimReference", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.animRef); return true; }},
            {L"XActivateAIFeature", [](Obj& s, Impl& c) { fo::tr(F("call %s XActivateAIFeature %lld", s.name.c_str(), (long long)c.in[L"Feature"].i)); return true; }},
            {L"XALifeEnable", [](Obj& s, Impl&) { fo::tr(F("call %s XALifeEnable", s.name.c_str())); return true; }},
            {L"GetOwner", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.owner); return true; }},
            {L"GetWeaponInHandsMeshComponent", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.weapon); return true; }},
            {L"GetSkeletalMeshAsset", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = ob(s.skel); return true; }},
            {L"DoesSocketExist", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = bo(s.socks.count(arg_n(c, L"InSocketName")) != 0); return true; }},
            {L"GetSocketLocation", [](Obj& s, Impl& c) {
                 Obj* a = top(&s);
                 double k = (double)arg_n(c, L"InSocketName").size();
                 c.out[L"ReturnValue"] = vv(x0::Vec3{a->loc.x + 5, a->loc.y + k, a->loc.z + 100});
                 return true;
             }},
            {L"GetSocketRotation", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = rr(x0::Rot{3, top(&s)->rot.yaw, 0}); return true; }},
            {L"K2_GetComponentLocation", [](Obj& s, Impl& c) { Obj* a = top(&s); c.out[L"ReturnValue"] = vv(x0::Vec3{a->loc.x + 30, a->loc.y + 10, a->loc.z + 120}); return true; }},
            {L"K2_GetComponentRotation", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = rr(x0::Rot{1, top(&s)->rot.yaw, 2}); return true; }},
            {L"PostEventAtLocation", [](Obj& s, Impl& c) {
                 x0::Vec3 l = c.in[L"Location"].v;
                 fo::tr(F("call %s PostEventAtLocation %s %.1f %.1f %.1f at=%s", s.name.c_str(), nm(arg_o(c, L"AkEvent")).c_str(), l.x, l.y, l.z, nm(arg_o(c, L"WorldContextObject")).c_str()));
                 c.out[L"ReturnValue"] = in(1);
                 return true;
             }},
            {L"SpawnSystemAtLocation", [](Obj& s, Impl& c) {
                 x0::Vec3 l = c.in[L"Location"].v;
                 x0::Rot r = c.in[L"Rotation"].r;
                 fo::tr(F("call %s SpawnSystemAtLocation %s %.1f %.1f %.1f %.1f", s.name.c_str(), nm(arg_o(c, L"SystemTemplate")).c_str(), l.x, l.y, l.z, r.yaw));
                 Obj* k = fo::make("NiagaraComponent", "Nc_" + nm(arg_o(c, L"SystemTemplate")) + "_" + std::to_string(fo::W().objs.size()));
                 k->loc = l;
                 c.out[L"ReturnValue"] = ob(k);
                 return true;
             }},
            {L"GetControlRotation", [](Obj& s, Impl& c) { c.out[L"ReturnValue"] = rr(s.rot); return true; }},
            {L"K2_SetWorldLocation", [](Obj& s, Impl& c) {
                 s.loc = c.in[L"NewLocation"].v;
                 fo::tr(F("call %s K2_SetWorldLocation %.1f %.1f %.1f", s.name.c_str(), s.loc.x, s.loc.y, s.loc.z));
                 return true;
             }},
            {L"K2_DestroyComponent", [](Obj& s, Impl&) {
                 fo::tr(F("call %s K2_DestroyComponent", s.name.c_str()));
                 s.alive = false;
                 return true;
             }},
            {L"SpawnAkComponentAtLocation", [](Obj& s, Impl& c) {
                 x0::Vec3 l = c.in[L"Location"].v;
                 Obj* k = fo::make("AkComponent", "AkComp_" + std::to_string(fo::W().objs.size()));
                 fo::tr(F("call %s SpawnAkComponentAtLocation %s %.1f %.1f %.1f auto=%d,%d", s.name.c_str(), nm(arg_o(c, L"AkEvent")).c_str(), l.x, l.y, l.z, (int)arg_b(c, L"AutoPost"),
                          (int)arg_b(c, L"AutoDestroy")));
                 c.out[L"ReturnValue"] = ob(k);
                 return true;
             }},
            {L"SetSwitch", [](Obj& s, Impl& c) { fo::tr(F("call AkComp SetSwitch %s", nm(arg_o(c, L"SwitchValue")).c_str())); (void)s; return true; }},
            {L"PostAkEventAndWaitForEnd", [](Obj&, Impl& c) {
                 fo::tr(F("call AkComp PostAkEventAndWaitForEnd %s", nm(arg_o(c, L"AkEvent")).c_str()));
                 c.out[L"ReturnValue"] = in(7);
                 return true;
             }},
        };
        return h;
    }
    std::string tsv_unescape(std::string_view s)
    {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '\\' && i + 1 < s.size())
            {
                char n = s[++i];
                o.push_back(n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n);
            }
            else o.push_back(s[i]);
        }
        return o;
    }
}

namespace fo
{
    World& W()
    {
        static World w;
        return w;
    }
    Obj* make(const std::string& cls, const std::string& name, bool path, const std::string& full)
    {
        auto o = std::make_unique<Obj>();
        Obj* p = o.get();
        p->idx = (int)W().objs.size() + 1;
        p->serial = 1000 + p->idx;
        p->cls = cls;
        p->name = name;
        p->full = full.empty() ? cls + " /Game/Map.Map:PersistentLevel." + name : cls + " " + full;
        known().insert(p);
        W().objs.push_back(std::move(o));
        W().byName[name] = p;
        if (path) W().paths[wd(full)] = p;
        return p;
    }
    x0::Ref ref(Obj* o)
    {
        x0::Ref r;
        if (!o) return r;
        r.idx = o->idx;
        r.serial = o->serial;
        r.raw = o;
        return r;
    }
    void tr(const std::string& s) { W().trace.push_back(s); }
    void init_modules()
    {
        std::stable_sort(W().inits.begin(), W().inits.end(), [](auto& a, auto& b) { return (int)a.first < (int)b.first; });
        for (auto& [m, f] : W().inits) f();
    }
    void advance(double t)
    {
        auto& w = W();
        while (true)
        {
            auto it = std::min_element(w.timers.begin(), w.timers.end(), [](const Timer& a, const Timer& b) { return a.at != b.at ? a.at < b.at : a.seq < b.seq; });
            if (it == w.timers.end() || it->at > t + 1e-9) break;
            Timer tm = *it;
            w.timers.erase(it);
            w.t = tm.at;
            tm.fn();
        }
        w.t = t;
    }
}

extern "C" uint64_t s2x_load_asset(const wchar_t*) { return 0; }

namespace m01
{
    x0::Ref puppet() { return {}; }
    void note_partner_shot(int) {}
    x0::Ref puppet(int v)
    {
        auto it = fo::W().pup.find(v);
        return it == fo::W().pup.end() ? x0::Ref{} : fo::ref(it->second);
    }
    x0::Ref puppet_weapon(int v)
    {
        auto it = fo::W().pupw.find(v);
        return it == fo::W().pupw.end() ? x0::Ref{} : fo::ref(it->second);
    }
}

namespace s2nat
{
    uint64_t actor(const std::string& n, uint64_t& weapon, uint64_t& mesh)
    {
        weapon = mesh = 0;
        auto it = fo::W().nat.find(n);
        if (it == fo::W().nat.end() || !it->second->alive) return 0;
        weapon = reinterpret_cast<uint64_t>(it->second->weapon);
        return reinterpret_cast<uint64_t>(it->second);
    }
}

std::string s2m_travel_ft_lock() { return fo::W().lock; }

namespace x0
{
    ModReg::ModReg(Mod m, const char*, void (*init)()) { fo::W().inits.push_back({m, init}); }
    bool port(Mod) { return true; }
    double now() { return fo::W().t; }
    const std::string& role() { return fo::W().role; }
    bool is_host() { return fo::W().role == "host"; }
    bool is_client() { return fo::W().role == "client"; }
    void log(std::string_view text) { fo::tr("log " + std::string(text)); }

    double Val::num() const { return k == i ? (double)iv : k == f ? fv : k == s ? std::strtod(sv.c_str(), nullptr) : 0; }
    int64_t Val::integer() const { return k == i ? iv : (int64_t)num(); }
    std::string Val::str() const { return k == i ? std::to_string(iv) : k == s ? sv : std::to_string(fv); }
    bool Val::operator==(const Val& o) const { return k == o.k && iv == o.iv && fv == o.fv && sv == o.sv; }
    Val Val::I(int64_t v) { Val r; r.k = i; r.iv = v; return r; }
    Val Val::F(double v) { Val r; r.k = f; r.fv = v; return r; }
    Val Val::S(std::string v) { Val r; r.k = s; r.sv = std::move(v); return r; }
    Val flag(std::string_view key)
    {
        auto it = fo::W().flags.find(std::string(key));
        return it == fo::W().flags.end() ? Val{} : it->second;
    }
    double fd(std::string_view key, double def)
    {
        Val v = flag(key);
        return v.k == Val::nil ? def : v.num();
    }
    int64_t fi(std::string_view key, int64_t def)
    {
        Val v = flag(key);
        return v.k == Val::nil ? def : v.integer();
    }

    Ref Ref::of(const UObject* o)
    {
        Obj* p = const_cast<Obj*>(reinterpret_cast<const Obj*>(o));
        if (!p || !known().count(p)) return Ref{};
        return fo::ref(p);
    }
    Ref Ref::addr(uint64_t a)
    {
        Obj* p = reinterpret_cast<Obj*>(a);
        if (!p || !known().count(p) || !p->alive) return Ref{};
        return fo::ref(p);
    }
    UObject* Ref::get() const { return reinterpret_cast<UObject*>(O(*this)); }

    std::vector<Ref> all_of(const wchar_t* cls, double)
    {
        std::vector<Ref> v;
        bool pawn = std::wcscmp(cls, L"Pawn") == 0;
        for (auto& o : fo::W().objs)
            if (o->alive && ((pawn && o->pawn) || wd(o->cls) == cls)) v.push_back(fo::ref(o.get()));
        return v;
    }
    Ref first_of(const wchar_t* cls)
    {
        for (auto& o : fo::W().objs)
            if (o->alive && wd(o->cls) == cls) return fo::ref(o.get());
        return Ref{};
    }
    Ref find_path(const wchar_t* path)
    {
        auto it = fo::W().paths.find(path);
        if (it == fo::W().paths.end() || !it->second->alive) return Ref{};
        return fo::ref(it->second);
    }
    std::string class_name(const Ref& r) { Obj* o = O(r); return o ? o->cls : std::string(); }
    std::string full_name(const Ref& r) { Obj* o = O(r); return o ? o->full : std::string(); }
    std::string obj_name(const Ref& r) { Obj* o = O(r); return o ? o->name : std::string(); }
    bool is_own_body(const Ref&) { return false; }

    Call::Call(const Ref& self, const wchar_t* fn) : self_(self) { fn_ = new Impl{fn ? fn : L"", {}, {}}; }
    Call::~Call() { delete static_cast<Impl*>(fn_); }
    Call& Call::i(const wchar_t* p, int64_t v) { P x; x.k = 'I'; x.i = v; I(fn_).in[p] = x; return *this; }
    Call& Call::f(const wchar_t* p, float v) { P x; x.k = 'F'; x.d = v; I(fn_).in[p] = x; return *this; }
    Call& Call::d(const wchar_t* p, double v) { P x; x.k = 'F'; x.d = v; I(fn_).in[p] = x; return *this; }
    Call& Call::b(const wchar_t* p, bool v) { P x; x.k = 'B'; x.b = v; I(fn_).in[p] = x; return *this; }
    Call& Call::o(const wchar_t* p, const Ref& v) { P x; x.k = 'O'; x.o = v; I(fn_).in[p] = x; return *this; }
    Call& Call::n(const wchar_t* p, const wchar_t* fname) { P x; x.k = 'N'; x.s = fname ? fname : L""; I(fn_).in[p] = x; return *this; }
    Call& Call::v(const wchar_t* p, const Vec3& v) { P x; x.k = 'V'; x.v = v; I(fn_).in[p] = x; return *this; }
    Call& Call::r(const wchar_t* p, const Rot& v) { P x; x.k = 'R'; x.r = v; I(fn_).in[p] = x; return *this; }
    Call& Call::s(const wchar_t* p, const std::wstring& v) { P x; x.k = 'S'; x.s = v; I(fn_).in[p] = x; return *this; }
    Call& Call::raw(const wchar_t* p, const void* bytes, int size)
    {
        P x;
        x.k = 'X';
        if (size >= 8) std::memcpy(&x.i, bytes, 8);
        I(fn_).in[p] = x;
        return *this;
    }
    Call& Call::xf(const wchar_t* p, const Vec3&, const Rot&, const Vec3&) { P x; x.k = 'X'; I(fn_).in[p] = x; return *this; }
    bool Call::run()
    {
        Impl& c = I(fn_);
        Obj* s = O(self_);
        ran_ = true;
        if (!s || bad_) return false;
        auto it = handlers().find(c.fn);
        if (it == handlers().end()) return false;
        return it->second(*s, c);
    }
    bool Call::rb(const wchar_t* p) const { auto& m = I(fn_).out; auto it = m.find(p); return it != m.end() && it->second.k == 'B' && it->second.b; }
    int64_t Call::ri(const wchar_t* p) const { auto& m = I(fn_).out; auto it = m.find(p); return it != m.end() && it->second.k == 'I' ? it->second.i : 0; }
    float Call::rf(const wchar_t* p) const { return (float)rd(p); }
    double Call::rd(const wchar_t* p) const { auto& m = I(fn_).out; auto it = m.find(p); return it != m.end() && it->second.k == 'F' ? it->second.d : 0; }
    Ref Call::ro(const wchar_t* p) const
    {
        auto& m = I(fn_).out;
        auto it = m.find(p);
        if (it == m.end() || it->second.k != 'O' || !O(it->second.o)) return Ref{};
        return it->second.o;
    }
    bool Call::rv(Vec3& out, const wchar_t* p) const
    {
        auto& m = I(fn_).out;
        auto it = m.find(p);
        if (it == m.end() || it->second.k != 'V') return false;
        out = it->second.v;
        return true;
    }
    bool Call::rr(Rot& out, const wchar_t* p) const
    {
        auto& m = I(fn_).out;
        auto it = m.find(p);
        if (it == m.end() || it->second.k != 'R') return false;
        out = it->second.r;
        return true;
    }
    std::wstring Call::rs(const wchar_t*) const { return {}; }
    std::string Call::rn(const wchar_t*) const { return {}; }
    std::vector<Ref> Call::rarr(const wchar_t*) const { return {}; }
    bool Call::rraw(void*, int, const wchar_t*) const { return false; }
    bool call0(const Ref& self, const wchar_t* fn) { Call c(self, fn); return c.run(); }
    bool call_b(const Ref& self, const wchar_t* fn, bool def) { Call c(self, fn); return c.run() ? c.rb() : def; }
    double call_d(const Ref& self, const wchar_t* fn, double def) { Call c(self, fn); return c.run() ? c.rd() : def; }
    Ref call_o(const Ref& self, const wchar_t* fn) { Call c(self, fn); return c.run() ? c.ro() : Ref{}; }
    bool call_v(const Ref& self, const wchar_t* fn, Vec3& out) { Call c(self, fn); return c.run() && c.rv(out); }

    template <class T> bool prop(const Ref& self, const char* path, T& out)
    {
        Obj* o = O(self);
        if (!o) return false;
        std::string p(path);
        if constexpr (std::is_same_v<T, Ref>)
        {
            Obj* r = nullptr;
            if (p == "CharacterMovement") r = o->cm;
            else if (p == "Mesh") r = o->mesh;
            else if (p == "BrainComponent") r = o->brain;
            else if (p == "FaceData.FaceMeshComponent") r = o->face;
            else if (p == "AkVoiceoverSpeakerComponent") r = o->spk;
            else if (p == "AkAudioEvent") r = o->ev;
            else return false;
            if (!r) return false;
            out = fo::ref(r);
            return true;
        }
        else return false;
    }
    template <class T> bool prop_set(const Ref& self, const char* path, const T& v)
    {
        Obj* o = O(self);
        if (!o) return false;
        std::string p(path);
        if constexpr (std::is_same_v<T, Vec3>)
        {
            if (p == "Velocity") o->cmVel = v;
            else if (p == "Acceleration") o->cmAcc = v;
            else return false;
            fo::tr(F("set %s %s %.1f %.1f %.1f", o->name.c_str(), path, v.x, v.y, v.z));
            return true;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            if (p != "bHasRequestedVelocity") return false;
            o->cmReq = v;
            fo::tr(F("set %s %s %d", o->name.c_str(), path, (int)v));
            return true;
        }
        else return false;
    }
    template bool prop<Ref>(const Ref&, const char*, Ref&);
    template bool prop_set<Vec3>(const Ref&, const char*, const Vec3&);
    template bool prop_set<bool>(const Ref&, const char*, const bool&);

    const Snap& remote() { return fo::W().snap; }
    int views() { return fo::W().nviews; }
    int my_pid() { return fo::W().role == "host" ? 0 : fo::W().you; }
    int view_pid(int v)
    {
        if (v < 0 || v >= fo::W().nviews) return -1;
        if (fo::W().role == "host") return v + 1;
        if (v == 0) return 0;
        return v < fo::W().you ? v : v + 1;
    }
    int view_of_pid(int pid)
    {
        for (int v = 0; v < fo::W().nviews; ++v)
            if (view_pid(v) == pid) return v;
        return -1;
    }
    const Snap& remote(int v)
    {
        static Snap none;
        return v == 0 ? fo::W().snap : none;
    }
    bool rpart(std::string_view tag, std::string& out)
    {
        auto& w = fo::W();
        if (!w.snap.ok) return false;
        std::string k = "#" + std::string(tag) + ":";
        size_t p = w.x1.find(k);
        if (p == std::string::npos) return false;
        size_t b = p + k.size(), e = w.x1.find('#', b);
        out = w.x1.substr(b, e == std::string::npos ? std::string::npos : e - b);
        return true;
    }
    bool rpart(std::string_view tag, std::string& out, int view)
    {
        auto& w = fo::W();
        if (view < 0 || view >= w.nviews) return false;
        if (view == 0) return rpart(tag, out);
        std::string k = "#" + std::string(tag) + ":";
        const std::string& x = w.vx1[view];
        size_t p = x.find(k);
        if (p == std::string::npos) return false;
        size_t b = p + k.size(), e = x.find('#', b);
        out = x.substr(b, e == std::string::npos ? std::string::npos : e - b);
        return true;
    }
    bool own_tag(Mod, std::string_view tag, Producer p)
    {
        fo::W().tags[std::string(tag)] = std::move(p);
        return true;
    }
    void on_snap(Mod, std::function<void(const Snap& s)> c) { fo::W().snaps.push_back(std::move(c)); }
    int every(Mod, const char* name, Rate, std::function<void()> fn, int)
    {
        fo::W().rows[name] = std::move(fn);
        return (int)fo::W().rows.size();
    }
    int every(Mod m, const char* name, Rate r, std::function<void()> fn) { return every(m, name, r, std::move(fn), ord::none); }
    void after(Mod, double secs, std::function<void()> fn)
    {
        auto& w = fo::W();
        w.timers.push_back({w.t + secs, ++w.tseq, std::move(fn)});
    }
    int hook(Mod, const wchar_t* ufunc, bool, std::function<void(const Ref& self)> cb)
    {
        fo::W().hooks[ufunc] = std::move(cb);
        return 1;
    }
    bool data_tsv(std::string_view name, std::vector<TsvRow>& rows, std::string* err)
    {
        std::ifstream f("D:\\modding\\cpp\\S2CoopPort\\data\\" + std::string(name), std::ios::binary);
        if (!f) { if (err) *err = "no data " + std::string(name); return false; }
        std::stringstream ss;
        ss << f.rdbuf();
        std::string text = ss.str();
        rows.clear();
        size_t pos = 0;
        int ln = 0, kcols = -1;
        while (pos < text.size())
        {
            size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
            pos = nl == std::string::npos ? text.size() : nl + 1;
            ++ln;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            std::vector<std::string> c;
            size_t b = 0;
            while (true)
            {
                size_t t = line.find('\t', b);
                c.push_back(line.substr(b, t == std::string::npos ? std::string::npos : t - b));
                if (t == std::string::npos) break;
                b = t + 1;
            }
            if (ln == 1) { kcols = (int)c.size() - 2; continue; }
            if ((int)c.size() != kcols + 2) { if (err) *err = "cells"; return false; }
            TsvRow r;
            for (int k = 0; k < kcols; ++k)
            {
                if (c[k].empty()) break;
                r.ktype += c[k][0];
                r.key.push_back(tsv_unescape(std::string_view(c[k]).substr(1)));
            }
            r.t = c[kcols][0];
            r.v = tsv_unescape(c[kcols + 1]);
            rows.push_back(r);
        }
        return kcols >= 0;
    }
}

namespace s2m_quest
{
    void console(std::string_view full) { fo::tr("quest console " + std::string(full)); }
}
