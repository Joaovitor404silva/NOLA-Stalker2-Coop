#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#include <Windows.h>
#include "s2x_int.h"
#include "s2x_hot.h"
#include "s2n_peframe.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <unordered_map>

#include <DynamicOutput/DynamicOutput.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FField.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealFlags.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace x0::in
{
    static uint64_t name_key(const FName& n) { return (uint64_t)n.GetComparisonIndex().ToUnstableInt() | ((uint64_t)n.GetNumber() << 32); }

    static int seh_index(const UObject* o, int32_t* out)
    {
        __try
        {
            *out = o->GetInternalIndex();
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    static SlotView slot(int32_t idx)
    {
        SlotView s;
        if (idx < 0 || idx >= FUObjectArray::GetNumElements()) return s;
        FUObjectItem* it = FUObjectArray::IndexToObject(idx);
        if (!it) return s;
        UObject* o = it->GetUObject();
        if (!o) return s;
        s.ok = true;
        s.obj = o;
        s.serial = it->GetSerialNumber();
        s.dead = it->HasAnyFlags(static_cast<EInternalObjectFlags>((1 << 28) | (1 << 21)));
        s.cls = o->GetClassPrivate();
        s.name = name_key(o->GetNamePrivate());
        return s;
    }

    static Ref weak_ref(const uint8_t* d)
    {
        Ref r;
        int32_t wi = *reinterpret_cast<const int32_t*>(d);
        int32_t ws = *reinterpret_cast<const int32_t*>(d + 4);
        if (ws == 0) return r;
        SlotView s = slot(wi);
        if (!s.ok || s.serial != ws || s.dead || !s.cls) return r;
        r.idx = wi;
        r.raw = const_cast<void*>(s.obj);
        r.serial = ws;
        r.name = s.name;
        return r;
    }

    static int seh_pe(UObject* o, UFunction* f, void* p)
    {
        uintptr_t pf0 = s2n_pf::save();
        __try
        {
            o->ProcessEvent(f, p);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            s2n_pf::fix(pf0);
            return 0;
        }
    }

    static bool is_kind(FProperty* p, const wchar_t* k)
    {
        static std::unordered_map<uint64_t, std::wstring> cache;
        FName cn = p->GetClass().GetFName();
        uint64_t c = name_key(cn);
        auto it = cache.find(c);
        if (it == cache.end()) it = cache.emplace(c, std::wstring(cn.ToString())).first;
        return it->second == k;
    }

    struct Hook
    {
        Mod m;
        std::wstring path;
        bool post;
        std::function<void(const Ref&)> cb;
        bool done = false;
        double next = 0, wait = 1;
        int errs = 0;
        bool off = false;
    };
    static std::deque<Hook>& hooks()
    {
        static std::deque<Hook> h;
        return h;
    }
    static std::mutex g_hmu;
    static std::vector<std::pair<int, Ref>> g_hq;
    static int64_t g_hdrop = 0;

    static void hook_fire(UnrealScriptFunctionCallableContext& ctx, void* data)
    {
        int id = (int)(intptr_t)data;
        Ref r = Ref::of(ctx.Context);
        std::lock_guard<std::mutex> g(g_hmu);
        if (g_hq.size() >= 4096) { ++g_hdrop; return; }
        g_hq.push_back({id, r});
    }

    void hooks_tick()
    {
        double t = now();
        auto& H = hooks();
        for (size_t i = 0; i < H.size(); ++i)
        {
            Hook& h = H[i];
            if (h.done || t < h.next) continue;
            if (!port(h.m) && h.m != Mod::x0) continue;
            UFunction* f = UObjectGlobals::StaticFindObject<UFunction*>(nullptr, nullptr, h.path);
            if (!f)
            {
                h.next = t + h.wait;
                h.wait = std::min(h.wait * 2, 30.0);
                continue;
            }
            UnrealScriptFunctionCallable none = [](UnrealScriptFunctionCallableContext&, void*) {};
            UnrealScriptFunctionCallable fire = [](UnrealScriptFunctionCallableContext& c, void* d) { hook_fire(c, d); };
            x0::hot::ufn(f, UObjectGlobals::RegisterHook(f, h.post ? none : fire, h.post ? fire : none, (void*)(intptr_t)i));
            h.done = true;
            log("NOLA x0 hook: " + w2u(h.path) + (h.post ? " (post)" : " (pre)") + " registered for " + mod_name(h.m));
        }
        std::vector<std::pair<int, Ref>> q;
        {
            std::lock_guard<std::mutex> g(g_hmu);
            q.swap(g_hq);
        }
        for (auto& [id, r] : q)
        {
            if (id < 0 || id >= (int)H.size()) continue;
            Hook& h = H[id];
            if (!port(h.m) && h.m != Mod::x0) continue;
            sched().guard("hook", h.m, h.errs, h.off, [&] { h.cb(r); });
        }
    }
}

namespace x0
{
    using namespace x0::in;

    Ref Ref::of(const UObject* o)
    {
        Ref r;
        if (!o) return r;
        int32_t i = -1;
        if (!seh_index(o, &i)) return r;
        SlotView s = slot(i);
        if (!s.ok || s.obj != o) return r;
        r.idx = i;
        r.raw = const_cast<UObject*>(o);
        r.serial = s.serial;
        r.name = s.name;
        return r;
    }
    Ref Ref::addr(uint64_t a)
    {
        if (a < 0x10000) return Ref{};
        return of(reinterpret_cast<const UObject*>(a));
    }
    UObject* Ref::get() const
    {
        if (!raw || idx < 0) return nullptr;
        return ref_check(*this, slot(idx)) ? static_cast<UObject*>(raw) : nullptr;
    }

    namespace
    {
        struct AllEnt { std::vector<Ref> refs; double at = -1e9; };
        std::unordered_map<std::wstring, AllEnt>& all_cache()
        {
            static std::unordered_map<std::wstring, AllEnt> m;
            return m;
        }
        struct FirstEnt { Ref r; double miss = -1e9; };
        std::unordered_map<std::wstring, FirstEnt>& first_cache()
        {
            static std::unordered_map<std::wstring, FirstEnt> m;
            return m;
        }
        std::unordered_map<std::wstring, std::pair<Ref, double>>& path_cache()
        {
            static std::unordered_map<std::wstring, std::pair<Ref, double>> m;
            return m;
        }
        bool is_cdo(UObject* o) { return o->HasAnyFlags(static_cast<EObjectFlags>(0x00000010)); }
    }

    std::vector<Ref> all_of(const wchar_t* cls, double ttl_s)
    {
        if (ttl_s < 0) ttl_s = fd("x0ttl", 1.0);
        auto& e = all_cache()[cls];
        double t = now();
        if (t - e.at >= ttl_s)
        {
            e.at = t;
            e.refs.clear();
            std::vector<UObject*> v;
            UObjectGlobals::FindAllOf(std::wstring(cls), v);
            for (UObject* o : v)
                if (o && !is_cdo(o))
                {
                    Ref r = Ref::of(o);
                    if (r.raw) e.refs.push_back(r);
                }
        }
        std::vector<Ref> out;
        out.reserve(e.refs.size());
        for (auto& r : e.refs) if (r.get()) out.push_back(r);
        return out;
    }
    Ref first_of(const wchar_t* cls)
    {
        auto& e = first_cache()[cls];
        if (e.r.get()) return e.r;
        double t = now();
        if (t - e.miss < 0.5) return Ref{};
        UObject* o = UObjectGlobals::FindFirstOf(std::wstring(cls));
        if (o && is_cdo(o))
        {
            o = nullptr;
            auto v = all_of(cls, 0);
            if (!v.empty()) o = v[0].get();
        }
        e.r = Ref::of(o);
        if (!e.r.raw) e.miss = t;
        return e.r;
    }
    Ref find_path(const wchar_t* path)
    {
        auto& e = path_cache()[path];
        if (e.first.get()) return e.first;
        double t = now();
        if (t - e.second < 5 && e.second > -1) return Ref{};
        e.second = t;
        UObject* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, std::wstring(path));
        e.first = Ref::of(o);
        return e.first;
    }
    std::string class_name(const Ref& r)
    {
        UObject* o = r.get();
        if (!o || !o->GetClassPrivate()) return {};
        return w2u(o->GetClassPrivate()->GetName());
    }
    std::string full_name(const Ref& r)
    {
        UObject* o = r.get();
        return o ? w2u(o->GetFullName()) : std::string();
    }
    std::string obj_name(const Ref& r)
    {
        UObject* o = r.get();
        return o ? w2u(o->GetName()) : std::string();
    }
    bool is_a(const Ref& r, const wchar_t* cls)
    {
        UObject* o = r.get();
        if (!o) return false;
        static std::unordered_map<const void*, std::unordered_map<std::wstring, bool>> cache;
        UClass* c = o->GetClassPrivate();
        auto& m = cache[c];
        auto it = m.find(cls);
        if (it != m.end()) return it->second;
        bool hit = false;
        for (UStruct* s = c; s; s = s->GetSuperStruct())
            if (s->GetName() == cls) { hit = true; break; }
        m[cls] = hit;
        return hit;
    }

    namespace
    {
        std::unordered_map<const void*, std::unordered_map<std::wstring, UFunction*>>& fn_cache()
        {
            static std::unordered_map<const void*, std::unordered_map<std::wstring, UFunction*>> m;
            return m;
        }
        UFunction* fn_of(UObject* o, const wchar_t* name)
        {
            UClass* c = o->GetClassPrivate();
            auto& m = fn_cache()[c];
            auto it = m.find(name);
            if (it != m.end()) return it->second;
            UFunction* f = o->GetFunctionByNameInChain(name);
            m[name] = f;
            return f;
        }
        int g_noFn = 0, g_peFail = 0;
        FProperty* sub_in(UStruct* st, std::wstring_view seg)
        {
            static std::unordered_map<const void*, std::unordered_map<std::wstring, FProperty*>> cache;
            auto& m = cache[st];
            std::wstring k(seg);
            auto it = m.find(k);
            if (it != m.end()) return it->second;
            FProperty* found = nullptr;
            for (UStruct* s = st; s && !found; s = s->GetSuperStruct())
                for (FProperty* q : s->ForEachProperty())
                    if (q->GetName() == k) { found = q; break; }
            m[k] = found;
            return found;
        }
        FProperty* PO(void* fn, const wchar_t* n, int& off)
        {
            off = 0;
            if (!fn || !n) return nullptr;
            std::wstring_view pv(n);
            size_t e = pv.find(L'.');
            FProperty* pr = static_cast<UFunction*>(fn)->FindProperty(FName(std::wstring(pv.substr(0, e)).c_str(), FNAME_Add));
            if (!pr) return nullptr;
            off = pr->GetOffset_Internal();
            while (e != std::wstring_view::npos)
            {
                auto* sp = CastField<FStructProperty>(pr);
                if (!sp || !sp->GetStruct()) return nullptr;
                size_t b = e + 1;
                e = pv.find(L'.', b);
                pr = sub_in(sp->GetStruct(), pv.substr(b, e == std::wstring_view::npos ? std::wstring_view::npos : e - b));
                if (!pr) return nullptr;
                off += pr->GetOffset_Internal();
            }
            if (off < 0 || off + pr->GetSize() > 0x600) return nullptr;
            return pr;
        }
    }

    Call::Call(const Ref& self, const wchar_t* fn) : self_(self)
    {
        std::memset(buf_, 0, sizeof buf_);
        UObject* o = self_.get();
        if (!o) { bad_ = true; return; }
        UFunction* f = fn_of(o, fn);
        if (!f) { bad_ = true; if (++g_noFn <= 5) log("NOLA x0 call: no function " + w2u(fn) + " on " + class_name(self_)); return; }
        if (f->GetPropertiesSize() > (int32_t)sizeof buf_) { bad_ = true; return; }
        fn_ = f;
    }
    Call::~Call()
    {
        for (void* d : strs_) reinterpret_cast<FString*>(d)->~FString();
    }
    void* Call::at(const wchar_t* p, int need, bool* isBool) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) return nullptr;
        if (isBool) *isBool = is_kind(pr, L"BoolProperty");
        if (need > 0 && pr->GetSize() != need) return nullptr;
        return const_cast<uint8_t*>(buf_) + off;
    }
    Call& Call::i(const wchar_t* p, int64_t v)
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) { bad_ = true; return *this; }
        uint8_t* d = buf_ + off;
        switch (pr->GetSize())
        {
        case 1: *d = (uint8_t)v; break;
        case 2: *reinterpret_cast<int16_t*>(d) = (int16_t)v; break;
        case 4: *reinterpret_cast<int32_t*>(d) = (int32_t)v; break;
        case 8: *reinterpret_cast<int64_t*>(d) = v; break;
        default: bad_ = true;
        }
        return *this;
    }
    Call& Call::f(const wchar_t* p, float v)
    {
        if (auto* d = at(p, 4)) *reinterpret_cast<float*>(d) = v;
        else bad_ = true;
        return *this;
    }
    Call& Call::d(const wchar_t* p, double v)
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) { bad_ = true; return *this; }
        uint8_t* q = buf_ + off;
        if (pr->GetSize() == 8) *reinterpret_cast<double*>(q) = v;
        else if (pr->GetSize() == 4) *reinterpret_cast<float*>(q) = (float)v;
        else bad_ = true;
        return *this;
    }
    Call& Call::b(const wchar_t* p, bool v)
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) { bad_ = true; return *this; }
        if (is_kind(pr, L"BoolProperty")) static_cast<FBoolProperty*>(pr)->SetPropertyValue(buf_ + off, v);
        else buf_[off] = v ? 1 : 0;
        return *this;
    }
    Call& Call::o(const wchar_t* p, const Ref& v)
    {
        if (auto* d = at(p, 8)) *reinterpret_cast<UObject**>(d) = v.get();
        else bad_ = true;
        return *this;
    }
    Call& Call::n(const wchar_t* p, const wchar_t* fname)
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() < 8) { bad_ = true; return *this; }
        FName nm(fname, FNAME_Add);
        std::memcpy(buf_ + off, &nm, std::min<size_t>(sizeof nm, (size_t)pr->GetSize()));
        return *this;
    }
    Call& Call::v(const wchar_t* p, const Vec3& v)
    {
        if (auto* d = at(p, 24)) { auto* q = reinterpret_cast<double*>(d); q[0] = v.x; q[1] = v.y; q[2] = v.z; }
        else bad_ = true;
        return *this;
    }
    Call& Call::r(const wchar_t* p, const Rot& v)
    {
        if (auto* d = at(p, 24)) { auto* q = reinterpret_cast<double*>(d); q[0] = v.pitch; q[1] = v.yaw; q[2] = v.roll; }
        else bad_ = true;
        return *this;
    }
    Call& Call::s(const wchar_t* p, const std::wstring& v)
    {
        if (auto* d = at(p, (int)sizeof(FString))) { new (d) FString(v.c_str()); strs_.push_back(d); }
        else bad_ = true;
        return *this;
    }
    Call& Call::raw(const wchar_t* p, const void* bytes, int size)
    {
        if (!bytes || size <= 0) { bad_ = true; return *this; }
        if (auto* d = at(p, size)) std::memcpy(d, bytes, (size_t)size);
        else bad_ = true;
        return *this;
    }
    Call& Call::xf(const wchar_t* p, const Vec3& loc, const Rot& rot, const Vec3& scale)
    {
        const double h = 3.14159265358979323846 / 360.0;
        double sp = std::sin(rot.pitch * h), cp = std::cos(rot.pitch * h);
        double sy = std::sin(rot.yaw * h), cy = std::cos(rot.yaw * h);
        double sr = std::sin(rot.roll * h), cr = std::cos(rot.roll * h);
        alignas(16) double t[12] = {cr * sp * sy - sr * cp * cy, -cr * sp * cy - sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy,
                                    loc.x, loc.y, loc.z, 0, scale.x, scale.y, scale.z, 0};
        return raw(p, t, (int)sizeof t);
    }
    bool Call::rraw(void* out, int size, const wchar_t* p) const
    {
        if (!out || size <= 0) return false;
        auto* d = at(p, size);
        if (!d) return false;
        std::memcpy(out, d, (size_t)size);
        return true;
    }
    bool Call::run()
    {
        if (bad_ || !fn_ || ran_) return false;
        UObject* o = self_.get();
        if (!o) return false;
        ran_ = true;
        if (fi("x0seh", 0) == 1)
        {
            if (!seh_pe(o, static_cast<UFunction*>(fn_), buf_)) { ++g_peFail; bad_ = true; return false; }
            return true;
        }
        o->ProcessEvent(static_cast<UFunction*>(fn_), buf_);
        return true;
    }
    bool Call::rb(const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) return false;
        const uint8_t* d = buf_ + off;
        if (is_kind(pr, L"BoolProperty")) return static_cast<FBoolProperty*>(pr)->GetPropertyValue(const_cast<uint8_t*>(d));
        return *d != 0;
    }
    int64_t Call::ri(const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) return 0;
        const uint8_t* d = buf_ + off;
        switch (pr->GetSize())
        {
        case 1: return *d;
        case 2: return *reinterpret_cast<const int16_t*>(d);
        case 4: return *reinterpret_cast<const int32_t*>(d);
        case 8: return *reinterpret_cast<const int64_t*>(d);
        }
        return 0;
    }
    float Call::rf(const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) return 0.f;
        const uint8_t* d = buf_ + off;
        if (pr->GetSize() == 4) return *reinterpret_cast<const float*>(d);
        if (pr->GetSize() == 8) return (float)*reinterpret_cast<const double*>(d);
        return 0.f;
    }
    double Call::rd(const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr) return 0;
        const uint8_t* d = buf_ + off;
        if (pr->GetSize() == 8) return *reinterpret_cast<const double*>(d);
        if (pr->GetSize() == 4) return *reinterpret_cast<const float*>(d);
        return 0;
    }
    Ref Call::ro(const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() != 8) return Ref{};
        return Ref::of(*reinterpret_cast<UObject* const*>(buf_ + off));
    }
    bool Call::rv(Vec3& out, const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() != 24) return false;
        auto* q = reinterpret_cast<const double*>(buf_ + off);
        out = {q[0], q[1], q[2]};
        return true;
    }
    bool Call::rr(Rot& out, const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() != 24) return false;
        auto* q = reinterpret_cast<const double*>(buf_ + off);
        out = {q[0], q[1], q[2]};
        return true;
    }
    std::wstring Call::rs(const wchar_t* p) const
    {
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() != (int)sizeof(FString)) return {};
        auto* s = reinterpret_cast<FString*>(const_cast<uint8_t*>(buf_) + off);
        const wchar_t* c = **s;
        std::wstring r = c ? std::wstring(c) : std::wstring();
        s->~FString();
        new (s) FString();
        return r;
    }
    std::string fname_str(uint64_t raw)
    {
        FName n((int64_t)raw);
        return in::w2u(n.ToString());
    }
    std::string Call::rn(const wchar_t* p) const
    {
        uint64_t raw = 0;
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() != 8) return {};
        std::memcpy(&raw, buf_ + off, 8);
        return fname_str(raw);
    }
    std::vector<Ref> Call::rarr(const wchar_t* p) const
    {
        std::vector<Ref> out;
        int off = 0;
        FProperty* pr = PO(fn_, p, off);
        if (!pr || pr->GetSize() != 16) return out;
        uint8_t* d = const_cast<uint8_t*>(buf_) + off;
        auto* data = *reinterpret_cast<UObject***>(d);
        int32_t num = *reinterpret_cast<int32_t*>(d + 8);
        for (int32_t k = 0; data && k < num && k < 4096; ++k)
        {
            Ref r = Ref::of(data[k]);
            if (r.raw) out.push_back(r);
        }
        if (data) FMemory::Free(data);
        *reinterpret_cast<void**>(d) = nullptr;
        *reinterpret_cast<int32_t*>(d + 8) = 0;
        return out;
    }

    bool call0(const Ref& self, const wchar_t* fn) { Call c(self, fn); return c.run(); }
    bool call_b(const Ref& self, const wchar_t* fn, bool def) { Call c(self, fn); return c.run() ? c.rb() : def; }
    double call_d(const Ref& self, const wchar_t* fn, double def) { Call c(self, fn); return c.run() ? c.rd() : def; }
    Ref call_o(const Ref& self, const wchar_t* fn) { Call c(self, fn); return c.run() ? c.ro() : Ref{}; }
    bool call_v(const Ref& self, const wchar_t* fn, Vec3& out) { Call c(self, fn); return c.run() && c.rv(out); }

    Ref world()
    {
        Ref pc = first_of(L"PlayerController");
        UObject* o = pc.get();
        if (!o) return Ref{};
        return Ref::of(reinterpret_cast<UObject*>(o->GetWorld()));
    }

    bool trace_line(const Vec3& a, const Vec3& b, bool& hit, Vec3& impact)
    {
        hit = false;
        impact = {};
        Ref ksl = find_path(L"/Script/Engine.Default__KismetSystemLibrary");
        Ref ctx = first_of(L"PlayerController");
        if (!ksl || !ctx) return false;
        Call c(ksl, L"LineTraceSingle");
        c.o(L"WorldContextObject", ctx).v(L"Start", a).v(L"End", b).i(L"TraceChannel", 0).b(L"bTraceComplex", false).i(L"DrawDebugType", 0).b(L"bIgnoreSelf", true).f(L"DrawTime", 0.f);
        if (!c.run()) return false;
        hit = c.rb();
        if (hit && !c.rv(impact, L"OutHit.ImpactPoint")) return false;
        return true;
    }

    namespace
    {
        struct PropAt
        {
            uint8_t* base = nullptr;
            FProperty* p = nullptr;
        };
        FProperty* find_in(UStruct* st, const std::string& seg)
        {
            static std::unordered_map<const void*, std::unordered_map<std::string, FProperty*>> cache;
            auto& m = cache[st];
            auto it = m.find(seg);
            if (it != m.end()) return it->second;
            FProperty* found = nullptr;
            std::wstring w = u2w(seg);
            for (UStruct* s = st; s && !found; s = s->GetSuperStruct())
                for (FProperty* p : s->ForEachProperty())
                    if (p->GetName() == w) { found = p; break; }
            m[seg] = found;
            return found;
        }
        bool resolve(const Ref& self, const char* path, PropAt& out)
        {
            UObject* o = self.get();
            if (!o) return false;
            uint8_t* base = reinterpret_cast<uint8_t*>(o);
            UStruct* st = o->GetClassPrivate();
            std::string_view pv(path);
            size_t b = 0;
            while (true)
            {
                size_t e = pv.find('.', b);
                std::string seg(pv.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b));
                FProperty* p = find_in(st, seg);
                if (!p) return false;
                if (e == std::string_view::npos)
                {
                    out.base = base;
                    out.p = p;
                    return true;
                }
                uint8_t* at = base + p->GetOffset_Internal();
                if (auto* sp = CastField<FStructProperty>(p))
                {
                    base = at;
                    st = sp->GetStruct();
                    if (!st) return false;
                }
                else if (is_kind(p, L"ObjectProperty") || is_kind(p, L"ObjectPtrProperty") || is_kind(p, L"ClassProperty") || is_kind(p, L"WeakObjectProperty"))
                {
                    Ref r = is_kind(p, L"WeakObjectProperty") ? weak_ref(at) : Ref::of(*reinterpret_cast<UObject**>(at));
                    UObject* g = r.get();
                    if (!g) return false;
                    base = reinterpret_cast<uint8_t*>(g);
                    st = g->GetClassPrivate();
                }
                else return false;
                b = e + 1;
            }
        }
    }

    template <class T> bool prop(const Ref& self, const char* path, T& out)
    {
        PropAt a;
        if (!resolve(self, path, a)) return false;
        uint8_t* d = a.base + a.p->GetOffset_Internal();
        int sz = a.p->GetSize();
        if constexpr (std::is_same_v<T, bool>)
        {
            if (is_kind(a.p, L"BoolProperty")) out = static_cast<FBoolProperty*>(a.p)->GetPropertyValue(d);
            else out = *d != 0;
            return true;
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            if (sz == 4) { out = *reinterpret_cast<float*>(d); return true; }
            if (sz == 8 && is_kind(a.p, L"DoubleProperty")) { out = (float)*reinterpret_cast<double*>(d); return true; }
            return false;
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            if (sz == 8 && is_kind(a.p, L"DoubleProperty")) { out = *reinterpret_cast<double*>(d); return true; }
            if (sz == 4 && is_kind(a.p, L"FloatProperty")) { out = *reinterpret_cast<float*>(d); return true; }
            return false;
        }
        else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>)
        {
            switch (sz)
            {
            case 1: out = (T)*d; return true;
            case 2: out = (T)*reinterpret_cast<int16_t*>(d); return true;
            case 4: out = (T)*reinterpret_cast<int32_t*>(d); return true;
            case 8: out = (T)*reinterpret_cast<int64_t*>(d); return true;
            }
            return false;
        }
        else if constexpr (std::is_same_v<T, Vec3>)
        {
            if (sz != 24) return false;
            auto* q = reinterpret_cast<double*>(d);
            out = {q[0], q[1], q[2]};
            return true;
        }
        else if constexpr (std::is_same_v<T, Rot>)
        {
            if (sz != 24) return false;
            auto* q = reinterpret_cast<double*>(d);
            out = {q[0], q[1], q[2]};
            return true;
        }
        else if constexpr (std::is_same_v<T, Ref>)
        {
            if (sz != 8) return false;
            if (is_kind(a.p, L"WeakObjectProperty"))
            {
                out = weak_ref(d);
                return true;
            }
            out = Ref::of(*reinterpret_cast<UObject**>(d));
            return true;
        }
        else if constexpr (std::is_same_v<T, std::wstring>)
        {
            if (is_kind(a.p, L"StrProperty"))
            {
                const wchar_t* c = **reinterpret_cast<FString*>(d);
                out = c ? c : L"";
                return true;
            }
            if (is_kind(a.p, L"NameProperty"))
            {
                out = reinterpret_cast<FName*>(d)->ToString();
                return true;
            }
            return false;
        }
        else return false;
    }

    template <class T> bool prop_set(const Ref& self, const char* path, const T& v)
    {
        PropAt a;
        if (!resolve(self, path, a)) return false;
        uint8_t* d = a.base + a.p->GetOffset_Internal();
        int sz = a.p->GetSize();
        if constexpr (std::is_same_v<T, bool>)
        {
            if (is_kind(a.p, L"BoolProperty")) static_cast<FBoolProperty*>(a.p)->SetPropertyValue(d, v);
            else *d = v ? 1 : 0;
            return true;
        }
        else if constexpr (std::is_same_v<T, float>)
        {
            if (sz == 4) { *reinterpret_cast<float*>(d) = v; return true; }
            if (sz == 8 && is_kind(a.p, L"DoubleProperty")) { *reinterpret_cast<double*>(d) = v; return true; }
            return false;
        }
        else if constexpr (std::is_same_v<T, double>)
        {
            if (sz == 8 && is_kind(a.p, L"DoubleProperty")) { *reinterpret_cast<double*>(d) = v; return true; }
            if (sz == 4 && is_kind(a.p, L"FloatProperty")) { *reinterpret_cast<float*>(d) = (float)v; return true; }
            return false;
        }
        else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>)
        {
            switch (sz)
            {
            case 1: *d = (uint8_t)v; return true;
            case 2: *reinterpret_cast<int16_t*>(d) = (int16_t)v; return true;
            case 4: *reinterpret_cast<int32_t*>(d) = (int32_t)v; return true;
            case 8: *reinterpret_cast<int64_t*>(d) = (int64_t)v; return true;
            }
            return false;
        }
        else if constexpr (std::is_same_v<T, Vec3>)
        {
            if (sz != 24) return false;
            auto* q = reinterpret_cast<double*>(d);
            q[0] = v.x; q[1] = v.y; q[2] = v.z;
            return true;
        }
        else if constexpr (std::is_same_v<T, Rot>)
        {
            if (sz != 24) return false;
            auto* q = reinterpret_cast<double*>(d);
            q[0] = v.pitch; q[1] = v.yaw; q[2] = v.roll;
            return true;
        }
        else return false;
    }

    template bool prop<bool>(const Ref&, const char*, bool&);
    template bool prop<uint8_t>(const Ref&, const char*, uint8_t&);
    template bool prop<int32_t>(const Ref&, const char*, int32_t&);
    template bool prop<int64_t>(const Ref&, const char*, int64_t&);
    template bool prop<float>(const Ref&, const char*, float&);
    template bool prop<double>(const Ref&, const char*, double&);
    template bool prop<Vec3>(const Ref&, const char*, Vec3&);
    template bool prop<Rot>(const Ref&, const char*, Rot&);
    template bool prop<Ref>(const Ref&, const char*, Ref&);
    template bool prop<std::wstring>(const Ref&, const char*, std::wstring&);
    template bool prop_set<bool>(const Ref&, const char*, const bool&);
    template bool prop_set<uint8_t>(const Ref&, const char*, const uint8_t&);
    template bool prop_set<int32_t>(const Ref&, const char*, const int32_t&);
    template bool prop_set<int64_t>(const Ref&, const char*, const int64_t&);
    template bool prop_set<float>(const Ref&, const char*, const float&);
    template bool prop_set<double>(const Ref&, const char*, const double&);
    template bool prop_set<Vec3>(const Ref&, const char*, const Vec3&);
    template bool prop_set<Rot>(const Ref&, const char*, const Rot&);

    int64_t prop_off(const Ref& self, const char* path)
    {
        PropAt a;
        UObject* o = self.get();
        if (!o || !resolve(self, path, a)) return -1;
        int64_t off = (a.base + a.p->GetOffset_Internal()) - reinterpret_cast<uint8_t*>(o);
        return off >= 0 && off < 0x100000 ? off : -1;
    }

    int64_t field_off(const Ref& st, const char* path)
    {
        UObject* o = st.get();
        if (!o || !is_a(st, L"Struct")) return -1;
        UStruct* s = static_cast<UStruct*>(o);
        int64_t off = 0;
        std::string_view pv(path);
        size_t b = 0;
        while (true)
        {
            size_t e = pv.find('.', b);
            FProperty* p = find_in(s, std::string(pv.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b)));
            if (!p) return -1;
            off += p->GetOffset_Internal();
            if (e == std::string_view::npos) return off;
            auto* sp = CastField<FStructProperty>(p);
            if (!sp || !(s = sp->GetStruct())) return -1;
            b = e + 1;
        }
    }

    int struct_size(const Ref& st)
    {
        UObject* o = st.get();
        if (!o || !is_a(st, L"Struct")) return -1;
        return static_cast<UStruct*>(o)->GetStructureSize();
    }

    int array_stride(const Ref& st, const char* name)
    {
        UObject* o = st.get();
        if (!o || !is_a(st, L"Struct")) return -1;
        auto* ap = CastField<FArrayProperty>(find_in(static_cast<UStruct*>(o), name));
        FProperty* in = ap ? ap->GetInner() : nullptr;
        return in ? in->GetElementSize() : -1;
    }

    bool sparse_of(const Ref& cls, uint64_t& data, Ref& st)
    {
        data = 0;
        st.reset();
        UObject* o = cls.get();
        if (!o || !is_a(cls, L"Class")) return false;
        UClass* c = static_cast<UClass*>(o);
        try
        {
            st = Ref::of(reinterpret_cast<UObject*>(c->GetSparseClassDataStruct().Get()));
            data = reinterpret_cast<uint64_t>(c->GetSparseClassData());
        }
        catch (...)
        {
            data = 0;
            st.reset();
            return false;
        }
        return st.valid() && data > 0x10000;
    }

    std::vector<std::pair<uint64_t, int64_t>> struct_props(const Ref& cls, const wchar_t* structName)
    {
        std::vector<std::pair<uint64_t, int64_t>> out;
        UObject* o = cls.get();
        if (!o || !is_a(cls, L"Struct")) return out;
        std::wstring want = structName;
        for (UStruct* s = static_cast<UStruct*>(o); s; s = s->GetSuperStruct())
            for (FProperty* p : s->ForEachProperty())
            {
                auto* sp = CastField<FStructProperty>(p);
                UScriptStruct* ss = sp ? sp->GetStruct() : nullptr;
                if (ss && ss->GetName() == want) out.emplace_back(reinterpret_cast<uint64_t>(p), (int64_t)p->GetOffset_Internal());
            }
        return out;
    }

    int hook(Mod m, const wchar_t* ufunc, bool post_hook, std::function<void(const Ref& self)> cb)
    {
        auto& H = in::hooks();
        in::Hook h;
        h.m = m;
        h.path = ufunc ? ufunc : L"";
        h.post = post_hook;
        h.cb = std::move(cb);
        H.push_back(std::move(h));
        return (int)H.size() - 1;
    }
}
