#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#include <Windows.h>
#include <intrin.h>
#include "s2n_dsdrop.h"
#include "s2n_peframe.h"
#include "s2n_nat_api.h"
#include "s2n_gait.h"
#include "s2n_mab.h"
#include "s2n_mc.h"
#include "s2n_look.h"
#include "s2m_cgcmc_logic.h"
#include "s2m_inter.h"
#include "s2x_fr.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <LuaType/LuaUObject.hpp>
#include <Unreal/CoreUObject/UObject/Class.hpp>
#include <Unreal/CoreUObject/UObject/UnrealType.hpp>
#include <Unreal/FAssetData.hpp>
#include <Unreal/FField.hpp>
#include <Unreal/FMemory.hpp>
#include <Unreal/FString.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UAssetRegistry.hpp>
#include <Unreal/UAssetRegistryHelpers.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UFunction.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UnrealVersion.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"
#include "s2x_hot_logic.h"

using namespace RC;
using namespace RC::Unreal;

int s2n_moveto_native(uint32_t uid, double x, double y, double z, int gait, float succ, int prio, int flags, int wstate);
int s2n_moveto_native_rot(uint32_t uid, double x, double y, double z, int gait, float succ, int prio, int flags, int wstate, double yaw);
int s2n_goal_snap(uint32_t uid, char* out, int n);

extern "C" bool s2x_nd_send_ok();
extern "C" bool s2x_nd_apply_ok();
extern "C" void s2x_nd_sent(double amount);
extern "C" void s2x_nd_applied(double amount, double hp0, double hp1);

namespace
{
    using Clock = std::chrono::steady_clock;
    auto now_s() -> double
    {
        static const auto t0 = Clock::now();
        return std::chrono::duration<double>(Clock::now() - t0).count();
    }

    auto w2u(const std::wstring& w) -> std::string
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }
    auto u2w(const std::string& s) -> std::wstring
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }
    auto logl(const std::string& s) -> void { S2FR(x0::fr::on_log(s)); Output::send<LogLevel::Default>(STR("{}\n"), u2w(s)); }

    struct Cfg
    {
        double natr = 7000;
        int natmax = 8;
        double natdead = 30;
        int natlookn = 4;
        double natlookp = 2;
        int natlookmax = 1400;
        double natlost = 3;
        int natrhost = 1;
        double natlead = 1.5;
        double natang = 25;
        double natcatch2 = 80;
        double natnudge = 450;
        double natagemax = 0.6;
        double natvendr = 1500;
        int natcorpsemax = 24;
        int natvend = 1;
        int natkept = 2;
        int natfixed = 1;
        double natre = 80;
        double natrate = 3;
        double natkeep = 1.0;
        double natok = 60;
        double natcatch = 200;
        double natsprint = 450;
        int natgaitmax = 4;
        int natgait = 1;
        int natmab = 3;
        int natcloak = 1;
        int natcopy = 1;
        double natsucc = 30;
        double natsnap = 800;
        double natshow = 4;
        double natrun = 230;
        double natstill = 30;
        int natspawn = 1;
        int natadopt = 1;
        int natlook = 1;
        int natlog = 1;
        int natcb = 1;
        int natmstop = 1;
        int natwound = 1, natwoundfix = 1, natdeathfx = 1;
        int natface = 1;
        int natrot = 1;
        int natspd = 1;
        double natmwin = 0.7;
        double natfaceang = 12;
        double natkeepc = 1.5;
        double natnudges = 0;
        double natspdmax = 750;
        int natthr = 0;
        int natforce = 3;
        int natmkmax = 4;
        double natmerr = 150;
        double natsnapt = 1.5;
        double natnudgek = 0.6;
        double natspdw = 0;
        double natspdr = 0;
        double natfacer = 240;
        double natlookbk = 30;
        int natgad = 1;
        double natgadlo = 0.85;
        double natgadhi = 1.2;
        double natgadt = 0.8;
        int natfacemv = 0;
        int natmgate = 1;
        int natwpnlook = 0;
        int natlookpt = 1;
        int natmloadpt = 1;
        int natlookld = 1;
        int natmroot = 1;
        double natidxs = 5;
        int natasync = 1;
        double natasyncto = 5;
        int natasyncpt = 8;
        int natbr = 1;
        int natfind = 1;
        int natpoll = 1;
        int natkin = 1;
        double natkind = 100;
        double natkinext = 250;
        double natking = 10;
        double natkinsnap = 300;
        int natkinreq = 1;
        double natkinmws = 1.1;
        int natkinord = 1;
        int natkinstop = 0;
        int natkinpin = 1;
        int natkinmv = 1;
        double natkinlead = 3.0;
        int nateqsync = 1;
        int natturn = 1;
        int natgaitcp = 1;
        int natwpnlocal = 1;
        int natloco = 1;
        int natlocoprm = 1;
        int natlocofl = 1;
        int nattraj = 1;
        int nattrajv = 1;
        int natlocosf = 1;
        int nattrajown = 0;
        int natcpvel = 1;
        double nattrajvmax = 1800;
        int nattrajn = 30;
        double nattrajh = 1.0;
        double nattrajmin = 50;
        int natprops = 1;
        int natdupfix = 1;
        int natwpnguard = 1;
        int montnull = 1;
        int natthrsrc = 1;
        int nathol = 1;
        int natgen = 1;
        int natcover = 1;
        int natmsec = 1;
        int natmre = 1;
        int natgenmre = 1; int natmloop = 1; int natmkeep = 1; int natmovl = 1; int natmsecq = 1;
        int natrmoff = 1;
        int natnavw = 1;
        int natwpnhid = 1;
        int natlooksec = 1;
        int natmutlook = 1;
        int natgenmk = 1;
        int natmidsc = 1;
        int natgenmkmax = 12;
        double natrmpin = 100;
        int natlookst = 0;
        int natpark = 0;
        double nathys = 1.0;
        double natparkage = 10;
        double natparkr = 15000;
        int natstsrc = 1;
        int natstance = 0;
        int natnoatk = 1;
        int natbullet = 1;
        double natbwin = 0.6;
        double natbdel = 0.15;
        int natdmg = 0;
        double natdwin = 1.0;
        double natdmax = 200;
        double natdcap = 300;
        double natdspawn = 3;
        int aggrop = 1;
        int natmc = 1;
        int invnull = 1;
        double natagek = 1.0;
        int natmut = 1;
        int natadopt_kept = 1;
        int offanim = 1;
        int offvb = 1;
        double natkeptr = 1500;
        int npcvis = 1;
        int natqg = 1;
        int natfd = 1;
        int natdeadall = 1;
        int natdeadpos = 1;
        int natlegs = 1;
        int natmbh = 1;
        int natqgsafe = 1;
        int natlostfix = 1;
        int natlostmax = 3;
        int nateqfix = 1;
        int natfar = 1;
        double natfardead = 4500;
        int nathotkeep = 1;
        int natdeadkeep = 1;
        std::wstring natsid = L"GeneralNPC_Bandit_Stormtrooper";
        std::wstring natcls = L"BP_AI_Human_C";
        std::wstring natcopysid = L"Nola_NpcCopy";
    } C;

    auto pe_seh(UObject* o, UFunction* f, void* p) -> bool
    {
        uintptr_t pf0 = s2n_pf::save();
        __try { o->ProcessEvent(f, p); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return false; }
    }

    std::unordered_map<void*, std::unordered_map<std::wstring, UFunction*>> g_fn;
    auto fn_of(UObject* o, const wchar_t* name) -> UFunction*
    {
        UClass* c = o->GetClassPrivate();
        auto& m = g_fn[c];
        auto it = m.find(name);
        if (it != m.end()) return it->second;
        UFunction* f = o->GetFunctionByNameInChain(name);
        m[name] = f;
        return f;
    }

    auto is_bool(FProperty* p) -> bool { return p && p->GetClass().GetFName().ToString() == STR("BoolProperty"); }

    int g_peFail = 0, g_noFn = 0;
    std::unordered_map<std::string, int> g_peWho;
    auto pe_note(UFunction* f) -> void
    {
        try { ++g_peWho[f ? w2u(f->GetFullName()) : std::string("?")]; } catch (...) {}
    }
    auto pe_last() -> std::string
    {
        std::string s;
        for (auto& [k, v] : g_peWho) s += k + " x" + std::to_string(v) + "; ";
        return s.empty() ? "-" : s;
    }

    struct Call
    {
        UObject* o = nullptr;
        UFunction* f = nullptr;
        bool bad = false;
        std::vector<uint8_t*> strs;
        alignas(16) uint8_t b[0x400];
        Call(UObject* obj, const wchar_t* name) : o(obj)
        {
            std::memset(b, 0, sizeof b);
            if (!o) { bad = true; return; }
            f = fn_of(o, name);
            if (!f) { bad = true; ++g_noFn; return; }
            if (f->GetPropertiesSize() > (int32_t)sizeof b) { f = nullptr; bad = true; }
        }
        ~Call() { for (auto* d : strs) reinterpret_cast<FString*>(d)->~FString(); }
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        auto P(const wchar_t* n) -> FProperty* { return f ? f->FindProperty(FName(n, FNAME_Add)) : nullptr; }
        auto at(const wchar_t* n, int need) -> uint8_t*
        {
            FProperty* p = P(n);
            if (!p || (need > 0 && p->GetSize() != need)) { bad = true; return nullptr; }
            return b + p->GetOffset_Internal();
        }
        auto i(const wchar_t* n, int64_t v) -> Call&
        {
            FProperty* p = P(n);
            if (!p) { bad = true; return *this; }
            uint8_t* d = b + p->GetOffset_Internal();
            switch (p->GetSize())
            {
            case 1: *d = (uint8_t)v; break;
            case 2: *reinterpret_cast<int16_t*>(d) = (int16_t)v; break;
            case 4: *reinterpret_cast<int32_t*>(d) = (int32_t)v; break;
            case 8: *reinterpret_cast<int64_t*>(d) = v; break;
            default: bad = true;
            }
            return *this;
        }
        auto fl(const wchar_t* n, float v) -> Call& { if (auto* d = at(n, 4)) *reinterpret_cast<float*>(d) = v; return *this; }
        auto bl(const wchar_t* n, bool v) -> Call&
        {
            FProperty* p = P(n);
            if (!p) { bad = true; return *this; }
            if (is_bool(p)) static_cast<FBoolProperty*>(p)->SetPropertyValue(b + p->GetOffset_Internal(), v);
            else b[p->GetOffset_Internal()] = v ? 1 : 0;
            return *this;
        }
        auto ob(const wchar_t* n, UObject* v) -> Call& { if (auto* d = at(n, 8)) *reinterpret_cast<UObject**>(d) = v; return *this; }
        auto nm(const wchar_t* n, const wchar_t* v) -> Call&
        {
            FProperty* p = P(n);
            if (!p || p->GetSize() < 8) { bad = true; return *this; }
            FName f(v, FNAME_Add);
            std::memcpy(b + p->GetOffset_Internal(), &f, std::min<size_t>(sizeof f, (size_t)p->GetSize()));
            return *this;
        }
        auto vec(const wchar_t* n, double x, double y, double z) -> Call&
        {
            if (auto* d = at(n, 24)) { auto* q = reinterpret_cast<double*>(d); q[0] = x; q[1] = y; q[2] = z; }
            return *this;
        }
        auto str(const wchar_t* n, const std::wstring& s) -> Call&
        {
            if (auto* d = at(n, (int)sizeof(FString))) { new (d) FString(s.c_str()); strs.push_back(d); }
            return *this;
        }
        auto run() -> bool
        {
            if (bad || !f) return false;
            if (!pe_seh(o, f, b)) { ++g_peFail; pe_note(f); return false; }
            return true;
        }
        auto rb(const wchar_t* n = STR("ReturnValue")) -> bool
        {
            FProperty* p = P(n);
            if (!p) return false;
            if (is_bool(p)) return static_cast<FBoolProperty*>(p)->GetPropertyValue(b + p->GetOffset_Internal());
            return b[p->GetOffset_Internal()] != 0;
        }
        auto rf(const wchar_t* n = STR("ReturnValue")) -> float { FProperty* p = P(n); return (p && p->GetSize() == 4) ? *reinterpret_cast<float*>(b + p->GetOffset_Internal()) : 0.f; }
        auto ri(const wchar_t* n = STR("ReturnValue")) -> int32_t { FProperty* p = P(n); return (p && p->GetSize() == 4) ? *reinterpret_cast<int32_t*>(b + p->GetOffset_Internal()) : 0; }
        auto ro(const wchar_t* n = STR("ReturnValue")) -> UObject* { FProperty* p = P(n); return (p && p->GetSize() == 8) ? *reinterpret_cast<UObject**>(b + p->GetOffset_Internal()) : nullptr; }
        auto rv(double& x, double& y, double& z, const wchar_t* n = STR("ReturnValue")) -> bool
        {
            FProperty* p = P(n);
            if (!p || p->GetSize() != 24) return false;
            auto* q = reinterpret_cast<double*>(b + p->GetOffset_Internal());
            x = q[0]; y = q[1]; z = q[2];
            return true;
        }
        auto rarr(std::vector<UObject*>& out, const wchar_t* n = STR("ReturnValue")) -> void
        {
            FProperty* p = P(n);
            if (!p || p->GetSize() != 16) return;
            uint8_t* d = b + p->GetOffset_Internal();
            auto* data = *reinterpret_cast<UObject***>(d);
            int32_t num = *reinterpret_cast<int32_t*>(d + 8);
            for (int32_t k = 0; data && k < num && k < 256; ++k) out.push_back(data[k]);
            if (data) (*GMalloc)->Free(data);
            *reinterpret_cast<void**>(d) = nullptr;
        }
    };

    auto c_bool(UObject* o, const wchar_t* fnName, bool def) -> bool { Call c(o, fnName); return c.run() ? c.rb() : def; }
    auto c_float(UObject* o, const wchar_t* fnName, float def) -> float { Call c(o, fnName); return c.run() ? c.rf() : def; }
    auto c_obj(UObject* o, const wchar_t* fnName) -> UObject* { Call c(o, fnName); return c.run() ? c.ro() : nullptr; }
    auto c_vec(UObject* o, const wchar_t* fnName, double& x, double& y, double& z) -> bool { Call c(o, fnName); return c.run() && c.rv(x, y, z); }
    auto c_set_bool(UObject* o, const wchar_t* fnName, const wchar_t* arg, bool v) -> bool { Call c(o, fnName); c.bl(arg, v); return c.run(); }

    auto prop_obj(UObject* o, const wchar_t* n) -> UObject*
    {
        if (!o) return nullptr;
        FProperty* p = o->GetPropertyByNameInChain(n);
        if (!p || p->GetSize() != 8) return nullptr;
        return *reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal());
    }
    auto prop_bool(UObject* o, const wchar_t* n, bool def) -> bool
    {
        if (!o) return def;
        FProperty* p = o->GetPropertyByNameInChain(n);
        if (!p) return def;
        uint8_t* d = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
        return is_bool(p) ? static_cast<FBoolProperty*>(p)->GetPropertyValue(d) : (*d != 0);
    }

    auto loc_of(UObject* a, double& x, double& y, double& z) -> bool { return c_vec(a, STR("K2_GetActorLocation"), x, y, z); }
    auto yaw_of(UObject* a) -> double { double p = 0, yw = 0, r = 0; c_vec(a, STR("K2_GetActorRotation"), p, yw, r); return yw; }
    auto alive_of(UObject* a) -> bool { return c_bool(a, STR("IsAlive"), false); }
    auto pooled(double x, double y) -> bool { return std::fabs(x) < 100 && std::fabs(y) < 100; }

    struct NCls { std::wstring cls; bool mut = false; std::wstring sid; double hpmax = 0; s2ngait::Spd spd; bool invis = false; };
    std::vector<NCls> g_cls;
    std::unordered_map<void*, int> g_clsOf;
    auto cls_ready() -> void
    {
        if (!g_cls.empty()) return;
        g_cls.push_back({C.natcls, false, C.natsid, 0});
        g_clsOf.clear();
    }
    auto cls_of(UObject* o) -> int
    {
        if (!o) return -1;
        cls_ready();
        UClass* c = o->GetClassPrivate();
        if (!c) return -1;
        auto it = g_clsOf.find(c);
        if (it != g_clsOf.end()) return it->second;
        int r = -1;
        for (UStruct* s = c; s && r < 0; s = s->GetSuperStruct())
        {
            std::wstring n = s->GetName();
            for (int i = 0; i < (int)g_cls.size(); ++i) if (g_cls[i].cls == n) { r = i; break; }
        }
        g_clsOf[c] = r;
        return r;
    }
    auto cls_mut(int i) -> bool { return i >= 0 && i < (int)g_cls.size() && g_cls[i].mut; }
    auto cls_sid(int i) -> std::wstring
    {
        if (i < 0 || i >= (int)g_cls.size() || g_cls[i].cls == C.natcls) return C.natsid;
        return g_cls[i].sid;
    }
    auto cls_by_sid(const std::wstring& sid) -> int
    {
        if (sid.empty()) return -1;
        for (int i = 0; i < (int)g_cls.size(); ++i) if (cls_sid(i) == sid) return i;
        return -1;
    }
    auto cls_cut(int i) -> double { return std::max(4000.0, (i >= 0 && i < (int)g_cls.size()) ? g_cls[i].hpmax + 1 : 0.0); }
    auto cls_all(std::vector<UObject*>& out) -> void
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("Pawn"), all);
        for (UObject* o : all)
        {
            int ci = cls_of(o);
            if (ci >= 0 && (C.natmut || !g_cls[ci].mut)) out.push_back(o);
        }
    }

    auto wall_ms() -> int64_t
    {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
        return (int64_t)(u.QuadPart / 10000ULL);
    }

    struct VRow { double x, y, z; const char* kind; const char* sid; };
    const VRow kVend[] = {
#include "s2n_vend.inc"
    };
    struct VPos { UObject* o; double x, y; };
    auto vend_pick(const std::vector<VPos>& live, const std::vector<s2nmc::Pt>& cs, double R, std::unordered_set<UObject*>& out) -> int
    {
        return s2nmc::vend_rows(kVend, sizeof kVend / sizeof kVend[0], live, cs, R, C.natvendr, out);
    }

    struct WRef
    {
        UObject* raw = nullptr;
        int32_t idx = -1;
        int32_t serial = 0;
        FName nm{};
        auto set(UObject* o) -> void
        {
            reset();
            if (!o) return;
            int32_t i = o->GetInternalIndex();
            FUObjectItem* it = FUObjectArray::IndexToObject(i);
            if (!it || it->GetUObject() != o) return;
            raw = o; idx = i; serial = it->GetSerialNumber(); nm = o->GetNamePrivate();
        }
        auto reset() -> void { raw = nullptr; idx = -1; serial = 0; nm = FName(); }
        auto get() const -> UObject*
        {
            if (!raw || idx < 0) return nullptr;
            FUObjectItem* it = FUObjectArray::IndexToObject(idx);
            if (!it || it->GetUObject() != raw) return nullptr;
            if (serial != 0 && it->GetSerialNumber() != serial) return nullptr;
            if (it->HasAnyFlags(static_cast<EInternalObjectFlags>((1 << 28) | (1 << 21)))) return nullptr;
            if (!(raw->GetNamePrivate() == nm)) return nullptr;
            return raw;
        }
    };

    struct AddrRef
    {
        uintptr_t addr = 0;
        bool real = false;
        WRef r;
        auto get(double a) -> UObject*
        {
            auto p = static_cast<uintptr_t>(a);
            if (p != addr)
            {
                addr = p; real = false; r.reset();
                if (p && UObject::IsReal(reinterpret_cast<void*>(p))) { real = true; r.set(reinterpret_cast<UObject*>(p)); }
            }
            return real ? r.get() : nullptr;
        }
    };

    WRef g_med, g_ai2, g_dc, g_op, g_vp, g_rk, g_build;
    auto single(WRef& r, const wchar_t* cls) -> UObject*
    {
        if (UObject* o = r.get()) return o;
        UObject* o = UObjectGlobals::FindFirstOf(cls);
        r.set(o);
        return o;
    }
    auto mediator() -> UObject*
    {
        if (UObject* o = g_med.get()) return o;
        UObject* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.Default__CppMediator"));
        g_med.set(o);
        return o;
    }
    auto guid_of(UObject* a) -> int32_t
    {
        UObject* m = mediator();
        if (!m || !a) return 0;
        Call c(m, STR("GetGUID"));
        c.ob(STR("Actor"), a);
        return c.run() ? c.ri() : 0;
    }
    constexpr double kQgWait = 3.0;
    WRef g_qbl;
    int g_qgCalls = 0, g_qgFail = 0, g_qgSkip = 0, g_qgThrow = 0;
    std::string g_qgLast = "-";
    auto qg_of(UObject* a, bool* thr = nullptr) -> std::string
    {
        if (!a) return std::string();
        UObject* l = g_qbl.get();
        if (!l)
        {
            l = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.Default__QuestBindingFunctionLibrary"));
            g_qbl.set(l);
        }
        if (!l) { ++g_qgFail; return std::string(); }
        Call c(l, STR("GetPlaceholderGuidByUObject"));
        c.ob(STR("InObject"), a);
        ++g_qgCalls;
        if (!c.run()) { ++g_qgFail; if (thr) *thr = !c.bad; return std::string(); }
        uint8_t* d = c.at(STR("ReturnValue"), 16);
        if (!d) { ++g_qgFail; return std::string(); }
        int32_t g[4];
        std::memcpy(g, d, sizeof g);
        return s2nmc::qg_hex(g);
    }
    auto pid_of(UObject* a) -> int32_t
    {
        UObject* m = mediator();
        if (!m || !a) return 0;
        Call c(m, STR("GetPrototypeID"));
        c.ob(STR("Actor"), a);
        return c.run() ? c.ri() : 0;
    }
    auto focus_of(UObject* a) -> UObject*
    {
        UObject* m = mediator();
        if (!m || !a) return nullptr;
        Call c(m, STR("GetFocusedEnemy"));
        c.ob(STR("Obj"), a);
        return c.run() ? c.ro() : nullptr;
    }
    auto cc_uid(WRef& r, const wchar_t* cls, const wchar_t* fnName, const wchar_t* arg, int32_t uid) -> bool
    {
        UObject* m = single(r, cls);
        if (!m) return false;
        Call c(m, fnName);
        c.i(arg, uid);
        return c.run();
    }
    long g_killGun = 0, g_killFall = 0;
    thread_local int t_killBypass = 0;
    thread_local uintptr_t t_killEnt = 0;
    struct KillBypass
    {
        uintptr_t prev;
        explicit KillBypass(uintptr_t e) : prev(t_killEnt) { ++t_killBypass; t_killEnt = e; }
        ~KillBypass() { --t_killBypass; t_killEnt = prev; }
    };
    auto kill_ent_seh(UObject* a) -> uintptr_t
    {
        __try { return *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(a) + 0x650); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    auto kill_gun(UObject* a) -> bool
    {
        if (!C.natdeathfx || !a) return false;
        float hp = c_float(a, STR("GetHP"), 0.f);
        Call c(a, STR("ReceiveDamageStruct"));
        uint8_t* d = c.at(STR("BriefDamageData"), 0);
        if (!d) return false;
        *reinterpret_cast<float*>(d) = std::max(hp, 1.f) * 1000.f;
        *reinterpret_cast<uint32_t*>(d + 0x14) = 0xFFFFFFFEu;
        d[0x20] = 6;
        d[0x21] = 2;
        uintptr_t ke = kill_ent_seh(a);
        if (!ke) return false;
        KillBypass kb(ke);
        return c.run();
    }
    auto kill_copy(WRef& r, UObject* a, int32_t uid, bool retry = false) -> bool
    {
        bool shot = !retry && kill_gun(a);
        char kb[128];
        if (shot)
        {
            ++g_killGun;
            snprintf(kb, sizeof kb, "NOLA natdeathfx kill uid=%u via=gun alive_after=%d hp_after=%.1f", (unsigned)uid, alive_of(a) ? 1 : 0, c_float(a, STR("GetHP"), -1.f));
            logl(kb);
            return true;
        }
        if (retry) ++g_killFall;
        cc_uid(r, STR("CustomConsoleManagerDC"), STR("XKillNpcByUID"), STR("NpcUID"), uid);
        snprintf(kb, sizeof kb, "NOLA natdeathfx kill uid=%u via=fallback alive_after=%d", (unsigned)uid, alive_of(a) ? 1 : 0);
        logl(kb);
        return false;
    }
    auto set_relation(int32_t a, int32_t b, int rel) -> bool
    {
        UObject* m = single(g_ai2, STR("CustomConsoleManagerAI2"));
        if (!m || !a || !b) return false;
        Call c(m, STR("XSetRelation"));
        c.i(STR("FirstUID"), a).i(STR("SecondUID"), b).i(STR("Relation"), rel);
        return c.run();
    }
    auto unkillable(int32_t uid, bool on) -> bool
    {
        UObject* m = single(g_build, STR("CustomConsoleManagerBuild"));
        if (!m || !uid) return false;
        Call c(m, STR("XSetEntityUnkillableByUID"));
        c.i(STR("EntityUID"), uid).bl(STR("bGodModeState"), on);
        return c.run();
    }
    auto state_tag(int32_t uid, int64_t tag, bool on) -> bool
    {
        UObject* m = single(g_op, STR("CustomConsoleManagerOP"));
        if (!m || !uid) return false;
        Call c(m, STR("XNpcSetStateTag"));
        c.i(STR("NpcUID"), uid).i(STR("StateTag"), tag).bl(STR("bEnabled"), on);
        return c.run();
    }
    auto look_at(int32_t uid, double x, double y, double z) -> bool
    {
        UObject* m = single(g_vp, STR("CustomConsoleManagerVP"));
        if (!m || !uid) return false;
        Call c(m, STR("XNpcLookAt"));
        c.i(STR("NpcUID"), uid).fl(STR("X"), (float)x).fl(STR("Y"), (float)y).fl(STR("Z"), (float)z);
        return c.run();
    }
    auto spawn_sid(const std::wstring& sid, double x, double y, double z, double yaw) -> bool
    {
        UObject* m = single(g_rk, STR("CustomConsoleManagerRK"));
        if (!m) return false;
        Call c(m, STR("XSpawnObjBySID"));
        c.str(STR("PrototypeSID"), sid).i(STR("Rank"), 1).bl(STR("bDestroyOnOffline"), false)
            .fl(STR("Px"), (float)x).fl(STR("Py"), (float)y).fl(STR("Pz"), (float)z).fl(STR("Pitch"), 0.f).fl(STR("Yaw"), (float)yaw).fl(STR("Roll"), 0.f);
        return c.run();
    }
    auto teleport(UObject* a, double x, double y, double z, double yaw) -> bool
    {
        Call c(a, STR("K2_TeleportTo"));
        c.vec(STR("DestLocation"), x, y, z).vec(STR("DestRotation"), 0, yaw, 0);
        return c.run() && c.rb();
    }
    struct WpnHide { WRef act; WRef root; double sc[3] = {1, 1, 1}; bool have = false; };
    std::unordered_map<uintptr_t, WpnHide> g_wpnHide;
    int g_wpnHideN = 0, g_wpnShowN = 0, g_wpnHideLog = 0;
    int g_wpnWinHide = 0, g_wpnWinShow = 0, g_wpnWinN = 0, g_wpnWinSweeps = 0;
    double g_wpnWinUs = 0, g_wpnWinUsMax = 0;
    auto prop_vec(UObject* o, const wchar_t* n, double& x, double& y, double& z) -> bool
    {
        if (!o) return false;
        FProperty* p = o->GetPropertyByNameInChain(n);
        if (!p || p->GetSize() != 24) return false;
        auto* q = reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal());
        x = q[0]; y = q[1]; z = q[2];
        return true;
    }
    auto set_scale(UObject* c, double x, double y, double z) -> bool
    {
        Call f(c, STR("SetRelativeScale3D"));
        f.vec(STR("NewScale3D"), x, y, z);
        return f.run();
    }
    auto wpn_restore(WpnHide& H, UObject* w) -> bool
    {
        bool mine = H.root.get() == w && (H.sc[0] != 0 || H.sc[1] != 0 || H.sc[2] != 0);
        return H.have && mine ? set_scale(w, H.sc[0], H.sc[1], H.sc[2]) : set_scale(w, 1, 1, 1);
    }
    auto wpn_zero(UObject* w) -> bool
    {
        double x = 0, y = 0, z = 0;
        return prop_vec(w, STR("RelativeScale3D"), x, y, z) && x == 0 && y == 0 && z == 0;
    }
    auto wpn_unhide(UObject* a) -> void
    {
        auto it = g_wpnHide.find(reinterpret_cast<uintptr_t>(a));
        if (it == g_wpnHide.end()) return;
        if (UObject* w = it->second.root.get()) { wpn_restore(it->second, w); ++g_wpnShowN; ++g_wpnWinShow; }
        g_wpnHide.erase(it);
    }
    auto wpn_hide_w(UObject* a, UObject* w) -> bool
    {
        bool zero = wpn_zero(w);
        WpnHide& H = g_wpnHide[reinterpret_cast<uintptr_t>(a)];
        if (H.root.get() == w && zero) return false;
        if (H.root.get() != w)
        {
            if (UObject* old = H.root.get(); old && H.have) set_scale(old, H.sc[0], H.sc[1], H.sc[2]);
            H.act.set(a);
            H.have = prop_vec(w, STR("RelativeScale3D"), H.sc[0], H.sc[1], H.sc[2]);
            H.root.set(w);
        }
        bool ok = set_scale(w, 0, 0, 0);
        ++g_wpnHideN; ++g_wpnWinHide;
        if (g_wpnHideLog < 40)
        {
            ++g_wpnHideLog;
            logl("NOLA natwpnhid: NPC hidden, weapon mesh scaled to 0 ok=" + std::to_string(ok ? 1 : 0) + " #" + std::to_string(g_wpnHideN));
        }
        return ok;
    }
    auto wpn_show_w(UObject* a, UObject* w) -> bool
    {
        auto it = g_wpnHide.find(reinterpret_cast<uintptr_t>(a));
        bool had = it != g_wpnHide.end();
        if (!had && !wpn_zero(w)) return false;
        bool ok = false;
        if (had)
        {
            auto& H = it->second;
            if (UObject* old = H.root.get(); old && old != w && H.have && wpn_zero(old)) set_scale(old, H.sc[0], H.sc[1], H.sc[2]);
            if (wpn_zero(w)) ok = wpn_restore(H, w);
            g_wpnHide.erase(it);
        }
        else ok = set_scale(w, 1, 1, 1);
        if (ok) { ++g_wpnShowN; ++g_wpnWinShow; }
        return ok;
    }
    auto wpn_hide(UObject* a) -> void
    {
        if (UObject* w = c_obj(a, STR("GetWeaponInHandsMeshComponent"))) wpn_hide_w(a, w);
    }
    auto wpn_sync(UObject* a, bool hidden) -> void
    {
        if (!a) return;
        if (hidden && C.natwpnhid) wpn_hide(a); else wpn_unhide(a);
    }
    auto wpn_tick(double now) -> void
    {
        static double at = 0;
        if (!C.natwpnhid)
        {
            for (auto& kv : g_wpnHide) if (UObject* w = kv.second.root.get()) wpn_restore(kv.second, w);
            g_wpnHide.clear();
            return;
        }
        if (now - at < 1.0) return;
        at = now;
        auto t0 = Clock::now();
        std::vector<UObject*> all;
        cls_all(all);
        int n = 0;
        for (UObject* a : all)
        {
            if (!a) continue;
            UObject* w = c_obj(a, STR("GetWeaponInHandsMeshComponent"));
            if (!w) continue;
            ++n;
            if (prop_bool(a, STR("bHidden"), false)) wpn_hide_w(a, w);
            else wpn_show_w(a, w);
        }
        for (auto it = g_wpnHide.begin(); it != g_wpnHide.end();)
        {
            WpnHide& H = it->second;
            UObject* a = H.act.get();
            UObject* w = H.root.get();
            if (!a || !w) { it = g_wpnHide.erase(it); continue; }
            if (!prop_bool(a, STR("bHidden"), false)) { wpn_restore(H, w); ++g_wpnShowN; ++g_wpnWinShow; it = g_wpnHide.erase(it); continue; }
            ++it;
        }
        double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        g_wpnWinUs += us; g_wpnWinUsMax = std::max(g_wpnWinUsMax, us); g_wpnWinN = n; ++g_wpnWinSweeps;
    }
    auto set_hidden(UObject* a, bool h) -> void
    {
        c_set_bool(a, STR("SetActorHiddenInGame"), STR("bNewHidden"), h);
        c_set_bool(a, STR("SetActorEnableCollision"), STR("bNewActorEnableCollision"), !h);
        wpn_sync(a, h);
    }
    auto comps_of(UObject* a, const wchar_t* clsPath, std::vector<UObject*>& out) -> void
    {
        static std::unordered_map<std::wstring, WRef> clsCache;
        auto& cr = clsCache[clsPath];
        UObject* cls = cr.get();
        if (!cls) { cls = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, clsPath); cr.set(cls); }
        if (!cls) return;
        Call c(a, STR("K2_GetComponentsByClass"));
        c.ob(STR("ComponentClass"), cls);
        if (c.run()) c.rarr(out);
    }
    std::unordered_set<uintptr_t> g_clown;
    auto stock_owned(UObject* c) -> bool
    {
        static std::unordered_map<const void*, bool> cache;
        UClass* k = c ? c->GetClassPrivate() : nullptr;
        if (!k) return false;
        auto it = cache.find(k);
        if (it != cache.end()) return it->second;
        bool r = s2inter::stockOwned([&](const char* want) {
            for (UStruct* s = k; s; s = s->GetSuperStruct())
                if (w2u(s->GetName()) == want) return true;
            return false;
        });
        cache[k] = r;
        return r;
    }
    auto interaction_off(UObject* a) -> int
    {
        std::vector<UObject*> l;
        comps_of(a, STR("/Script/Stalker2.InteractionComponent"), l);
        if (l.empty()) if (UObject* n = prop_obj(a, STR("NPCComponent"))) l.push_back(n);
        int n = 0;
        for (UObject* c : l)
            if (c && s2inter::flip(stock_owned(c), true, prop_bool(c, STR("bIsInteractionActive"), true), false)) { if (c_set_bool(c, STR("SetInteractionActive"), STR("bActive"), false)) ++n; }
        return n;
    }
    auto shooter_off(UObject* a) -> int
    {
        static WRef cls;
        UObject* sc = cls.get();
        if (!sc) { sc = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.ShootingWeaponComponent")); cls.set(sc); }
        if (!sc) return -1;
        std::vector<UObject*> hosts{a};
        if (UObject* w = c_obj(a, STR("GetWeaponInHandsMeshComponent")))
            if (UObject* ow = c_obj(w, STR("GetOwner")); ow && ow != a) hosts.push_back(ow);
        int n = 0;
        for (UObject* h : hosts)
        {
            Call c(h, STR("GetComponentByClass"));
            c.ob(STR("ComponentClass"), sc);
            if (!c.run()) continue;
            if (UObject* comp = c.ro())
            {
                c_set_bool(comp, STR("SetComponentTickEnabled"), STR("bEnabled"), false);
                Call d(comp, STR("Deactivate")); d.run();
                ++n;
            }
        }
        return n;
    }
    auto ai_on(UObject* a) -> void
    {
        if (UObject* t = prop_obj(a, STR("AITickComponent")))
        {
            c_set_bool(t, STR("SetComponentTickEnabled"), STR("bEnabled"), true);
            c_set_bool(t, STR("Activate"), STR("bReset"), true);
        }
        c_set_bool(a, STR("TogglePathFollowingComponent"), STR("bEnable"), true);
    }
    auto half_height(UObject* a) -> double
    {
        UObject* cap = prop_obj(a, STR("CapsuleComponent"));
        double h = cap ? c_float(cap, STR("GetScaledCapsuleHalfHeight"), 88.f) : 88.0;
        return (h > 10 && h < 300) ? h : 88.0;
    }

    auto c_u8(UObject* o, const wchar_t* fnName, int def) -> int
    {
        Call c(o, fnName);
        if (!c.run()) return def;
        FProperty* p = c.P(STR("ReturnValue"));
        return p ? (int)c.b[p->GetOffset_Internal()] : def;
    }
    auto anim_of(UObject* comp) -> UObject* { return comp ? c_obj(comp, STR("GetAnimInstance")) : nullptr; }
    std::unordered_map<void*, std::unordered_map<std::wstring, int>> g_off168;
    auto off_of(UObject* o, const wchar_t* n) -> int
    {
        auto& m = g_off168[o->GetClassPrivate()];
        auto it = m.find(n);
        if (it != m.end()) return it->second;
        FProperty* p = o->GetPropertyByNameInChain(n);
        int v = p ? p->GetOffset_Internal() : -1;
        m[n] = v;
        return v;
    }
    auto rd_u8_seh(const uint8_t* p) -> int { __try { return *p; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; } }
    auto rd_f_seh(const uint8_t* p) -> float { __try { return *reinterpret_cast<const float*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0.f; } }
    auto rd_d_seh(const uint8_t* p) -> double { __try { return *reinterpret_cast<const double*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0.0; } }
    auto wr_f_seh(uint8_t* p, float v) -> bool { __try { *reinterpret_cast<float*>(p) = v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    auto wr_u8_seh(uint8_t* p, int v) -> bool { __try { *p = (uint8_t)v; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    constexpr uintptr_t kCoverVtRva = 0x8e76b60;
    int g_covSent = 0, g_covW = 0, g_covClr = 0, g_covBad = 0;
    auto cov_hex(uint8_t* S, int uid, char* vb, size_t n) -> int
    {
        int vn = 0;
        __try
        {
            uint8_t* cbp = *reinterpret_cast<uint8_t**>(S + 0x58);
            if (reinterpret_cast<uintptr_t>(cbp) >= 0x10000 && *reinterpret_cast<uintptr_t*>(cbp) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) == kCoverVtRva && cbp[0x910] == 1)
            {
                vn = snprintf(vb, n, "v%d,", uid);
                for (int q = 0; q < 0xa0; ++q) vn += snprintf(vb + vn, n - vn, "%02x", cbp[0x870 + q]);
                vn += snprintf(vb + vn, n - vn, ",%d,%d", cbp[0x930], cbp[0x931]);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { vn = 0; }
        return vn;
    }
    auto ai_s_of(UObject* a) -> uint8_t*
    {
        if (!a) return nullptr;
        __try
        {
            uint8_t* p = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(a) + 0x650);
            if (reinterpret_cast<uintptr_t>(p) < 0x10000) return nullptr;
            p = *reinterpret_cast<uint8_t**>(p + 0x660);
            if (reinterpret_cast<uintptr_t>(p) < 0x10000) return nullptr;
            p = *reinterpret_cast<uint8_t**>(p + 0x38);
            if (reinterpret_cast<uintptr_t>(p) < 0x10000) return nullptr;
            volatile uint8_t t = p[0x1ad];
            (void)t;
            return p;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    struct AnimSt { int inCombat = -1, hasW = -1, ls = -1, hitPart = -1; float hitDir = 0, hitV = 0; };
    auto anim_state(UObject* ai, AnimSt& s) -> bool
    {
        if (!ai) return false;
        int sd = off_of(ai, STR("StateData")), wd = off_of(ai, STR("WeaponData")), la = off_of(ai, STR("LookAtData")), hd = off_of(ai, STR("HitData"));
        auto* b = reinterpret_cast<const uint8_t*>(ai);
        if (sd >= 0) { int v = rd_u8_seh(b + sd + 0x9); s.inCombat = v < 0 ? -1 : (v != 0); }
        if (wd >= 0) { int v = rd_u8_seh(b + wd); s.hasW = v < 0 ? -1 : (v != 0); }
        if (la >= 0) s.ls = rd_u8_seh(b + la + 0x132);
        if (hd >= 0) { s.hitPart = rd_u8_seh(b + hd + 0x80); s.hitDir = rd_f_seh(b + hd + 0x84); s.hitV = rd_f_seh(b + hd + 0x8C); }
        return true;
    }
    auto shooter_on(UObject* a) -> int
    {
        static WRef cls;
        UObject* sc = cls.get();
        if (!sc) { sc = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.ShootingWeaponComponent")); cls.set(sc); }
        if (!sc || !a) return -1;
        std::vector<UObject*> hosts{a};
        if (UObject* w = c_obj(a, STR("GetWeaponInHandsMeshComponent")))
            if (UObject* ow = c_obj(w, STR("GetOwner")); ow && ow != a) hosts.push_back(ow);
        int n = 0;
        for (UObject* h : hosts)
        {
            Call c(h, STR("GetComponentByClass"));
            c.ob(STR("ComponentClass"), sc);
            if (!c.run()) continue;
            if (UObject* comp = c.ro())
            {
                c_set_bool(comp, STR("SetComponentTickEnabled"), STR("bEnabled"), true);
                c_set_bool(comp, STR("Activate"), STR("bReset"), false);
                ++n;
            }
        }
        return n;
    }
    auto brain_of(UObject* a) -> UObject*
    {
        UObject* ctl = a ? c_obj(a, STR("GetController")) : nullptr;
        return ctl ? prop_obj(ctl, STR("BrainComponent")) : nullptr;
    }
    auto brain_st(UObject* a) -> int
    {
        UObject* bc = brain_of(a);
        if (!bc) return -1;
        return (c_bool(bc, STR("IsRunning"), false) ? 1 : 0) | (c_bool(bc, STR("IsPaused"), false) ? 2 : 0);
    }
    auto mont_cur(UObject* ai) -> UObject* { return ai ? c_obj(ai, STR("GetCurrentActiveMontage")) : nullptr; }
    auto mont_pos(UObject* ai, UObject* m) -> float { Call c(ai, STR("Montage_GetPosition")); c.ob(STR("Montage"), m); return c.run() ? c.rf() : -1.f; }
    auto mont_playing(UObject* ai, UObject* m) -> bool { Call c(ai, STR("Montage_IsPlaying")); c.ob(STR("Montage"), m); return c.run() && c.rb(); }
    auto mont_play(UObject* ai, UObject* m, float at, bool stopAll, float rate = 1.f) -> float
    {
        Call c(ai, STR("Montage_Play"));
        c.ob(STR("MontageToPlay"), m).fl(STR("InPlayRate"), rate).i(STR("ReturnValueType"), 0).fl(STR("InTimeToStartMontageAt"), at).bl(STR("bStopAllMontages"), stopAll);
        return c.run() ? c.rf() : -1.f;
    }
    auto mont_stop(UObject* ai, UObject* m, float blend) -> bool { Call c(ai, STR("Montage_Stop")); c.fl(STR("InBlendOutTime"), blend).ob(STR("Montage"), m); return c.run(); }
    auto mont_setpos(UObject* ai, UObject* m, float p) -> bool { Call c(ai, STR("Montage_SetPosition")); c.ob(STR("Montage"), m).fl(STR("NewPosition"), p); return c.run(); }
    auto mont_inst(UObject* ai, UObject* m) -> uint8_t*
    {
        __try
        {
            uint8_t* arr = reinterpret_cast<uint8_t*>(ai) + 0xa0;
            uint8_t** d = *reinterpret_cast<uint8_t***>(arr);
            int n = *reinterpret_cast<int32_t*>(arr + 8);
            if (!d || n <= 0 || n > 32) return nullptr;
            for (int i = n - 1; i >= 0; --i)
            {
                uint8_t* in = d[i];
                if (reinterpret_cast<uintptr_t>(in) < 0x10000 || *reinterpret_cast<UObject**>(in) != m) continue;
                if (*reinterpret_cast<float*>(in + 0x1c0) != 0.f) return in;
            }
            return nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    auto mont_active(UObject* ai, UObject** out, int cap) -> int
    {
        __try
        {
            uint8_t* arr = reinterpret_cast<uint8_t*>(ai) + 0xa0;
            uint8_t** d = *reinterpret_cast<uint8_t***>(arr);
            int n = *reinterpret_cast<int32_t*>(arr + 8);
            if (!d || n <= 0 || n > 32) return 0;
            int k = 0;
            for (int i = 0; i < n && k < cap; ++i)
            {
                uint8_t* in = d[i];
                if (reinterpret_cast<uintptr_t>(in) < 0x10000) continue;
                UObject* m = *reinterpret_cast<UObject**>(in);
                if (!m || *reinterpret_cast<float*>(in + 0x1c0) == 0.f) continue;
                out[k++] = m;
            }
            return k;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    auto mont_nx_raw(uint8_t* in, float& rate, int32_t* nx, int cap) -> int
    {
        __try
        {
            rate = *reinterpret_cast<float*>(in + 0x19c);
            int32_t* d = *reinterpret_cast<int32_t**>(in + 0x158);
            int n = *reinterpret_cast<int32_t*>(in + 0x160);
            if (!d || n <= 0 || n > cap) return 0;
            for (int i = 0; i < n; ++i) nx[i] = d[i];
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto msec_rd(uint8_t* arr, int i, int es, int so, uint64_t& out) -> bool
    {
        __try
        {
            uint8_t* d = *reinterpret_cast<uint8_t**>(arr);
            int n = *reinterpret_cast<int32_t*>(arr + 8);
            if (!d || i < 0 || i >= n) return false;
            out = *reinterpret_cast<uint64_t*>(d + (size_t)i * es + so);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    int g_msOff = -2, g_msEs = 0, g_msSo = 0;
    int g_msRate = 0, g_msNext = 0, g_msMis = 0, g_msFail = 0;
    int g_mxWait = 0, g_mxEnd = 0, g_mxOther = 0, g_mxLog = 0, g_mRefresh = 0, g_mxShort = 0;
    auto msec_name(UObject* m, int i, uint64_t& out) -> bool
    {
        if (g_msOff == -2)
        {
            g_msOff = -1;
            auto* ap = CastField<FArrayProperty>(m->GetPropertyByNameInChain(STR("CompositeSections")));
            auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
            FProperty* np = sp && sp->GetStruct() ? sp->GetStruct()->FindProperty(FName(STR("SectionName"))) : nullptr;
            if (np && np->GetSize() == 8) { g_msOff = ap->GetOffset_Internal(); g_msEs = sp->GetSize(); g_msSo = np->GetOffset_Internal(); }
        }
        if (g_msOff < 0) return false;
        return msec_rd(reinterpret_cast<uint8_t*>(m) + g_msOff, i, g_msEs, g_msSo, out);
    }
    auto msec_starts_raw(uint8_t* arr, int es, float* st, int cap) -> int
    {
        __try
        {
            uint8_t* d = *reinterpret_cast<uint8_t**>(arr);
            int n = *reinterpret_cast<int32_t*>(arr + 8);
            if (!d || n <= 0 || n > cap) return 0;
            for (int i = 0; i < n; ++i)
            {
                uint8_t* el = d + (size_t)i * es;
                float sb = *reinterpret_cast<float*>(el + 0x1c), sl = *reinterpret_cast<float*>(el + 0x20), lv = *reinterpret_cast<float*>(el + 0x24);
                int lm = el[0x18];
                st[i] = lm == 1 ? sb + lv : lm == 2 ? sb + lv * sl : lv;
            }
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    int g_msKeep = 0, g_msWrap = 0, g_msClamp = 0, g_msLoop = 0;
    auto msec_at(UObject* m, double pos, double at, const std::vector<int>& nx) -> double
    {
        uint64_t nm = 0;
        if (!msec_name(m, 0, nm) || g_msSo != 0x30 || g_msEs < 0x50) return at;
        float st[32];
        int n = msec_starts_raw(reinterpret_cast<uint8_t*>(m) + g_msOff, g_msEs, st, 32);
        float len = c_float(m, STR("GetPlayLength"), -1.f);
        int how = 0;
        double r = s2nmc::sec_at(st, n, len, pos, at, nx, how, C.natmsecq ? 1e-3 : 1e-4);
        if (how == 1) ++g_msWrap; else if (how == 2) ++g_msClamp;
        return r;
    }
    auto mont_msec(UObject* ai, UObject* m, int rate, const std::vector<int>& nx) -> void
    {
        if (rate > 0)
        {
            Call c(ai, STR("Montage_SetPlayRate"));
            c.ob(STR("Montage"), m).fl(STR("NewPlayRate"), rate / 1000.f);
            if (c.run()) ++g_msRate; else ++g_msFail;
        }
        if (nx.empty()) return;
        uint8_t* in = mont_inst(ai, m);
        float r0 = 0;
        int32_t cur[32];
        int n = in ? mont_nx_raw(in, r0, cur, 32) : 0;
        if (n != (int)nx.size()) { ++g_msMis; return; }
        for (int i = 0; i < n; ++i)
        {
            if (cur[i] == nx[i]) continue;
            if (nx[i] == -1 && (!C.natmloop || cur[i] == i)) { ++g_msKeep; continue; }
            int to = nx[i] == -1 ? i : nx[i];
            uint64_t a = 0, b = 0;
            if (to < -1 || to >= n || !msec_name(m, i, a) || (to >= 0 && !msec_name(m, to, b))) { ++g_msMis; return; }
            if (to != nx[i]) ++g_msLoop;
            Call c(ai, STR("Montage_SetNextSection"));
            if (uint8_t* d = c.at(STR("SectionNameToChange"), 8)) std::memcpy(d, &a, 8);
            if (uint8_t* d = c.at(STR("NextSection"), 8)) std::memcpy(d, &b, 8);
            c.ob(STR("Montage"), m);
            if (c.run()) ++g_msNext; else ++g_msFail;
        }
    }

    using FindObjFn = UObject* (*)(UClass*, UObject*, const wchar_t*, bool, void*);
    uint64_t g_foTramp = 0;
    PLH::x64Detour* g_foDet = nullptr;
    DWORD g_gtid = 0;
    WRef g_kslF;
    int g_fFast = 0, g_fFound = 0, g_fSlow = 0, g_foInGt = 0;
    double g_fSlowMs = 0, g_fSlowMax = 0;
    auto fo_fast(const wchar_t* name, UObject*& out) -> bool
    {
        if (!name || name[0] != L'/') return false;
        const wchar_t* sl = wcsrchr(name, L'/');
        const wchar_t* dot = wcschr(sl, L'.');
        if (!dot || !dot[1] || wcschr(dot + 1, L'.') || wcschr(name, L':') || wcschr(name, L' ') || wcschr(name, L'\'')) return false;
        UObject* k = g_kslF.get();
        if (!k)
        {
            k = reinterpret_cast<FindObjFn>(g_foTramp)(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary"), false, nullptr);
            if (!k) return false;
            g_kslF.set(k);
        }
        Call c(k, STR("Conv_SoftObjectReferenceToObject"));
        uint8_t* a = c.at(STR("SoftObject"), 0x28);
        uint8_t* r = c.at(STR("ReturnValue"), 8);
        if (c.bad || !a || !r) return false;
        std::wstring pk(name, (size_t)(dot - name));
        FName fp(pk.c_str(), FNAME_Add), fa(dot + 1, FNAME_Add);
        std::memcpy(a + 8, &fp, std::min<size_t>(8, sizeof fp));
        std::memcpy(a + 0x10, &fa, std::min<size_t>(8, sizeof fa));
        if (!c.run()) return false;
        out = *reinterpret_cast<UObject**>(r);
        return true;
    }
    UObject* fo_detour(UClass* cls, UObject* outer, const wchar_t* name, bool exact, void* searcher)
    {
        auto orig = reinterpret_cast<FindObjFn>(g_foTramp);
        bool gt = g_gtid != 0 && g_gtid == GetCurrentThreadId();
        if (C.natfind && gt && !cls && !outer && !searcher && !g_foInGt)
        {
            ++g_foInGt;
            UObject* o = nullptr;
            bool ok = false;
            try { ok = fo_fast(name, o); } catch (...) { ok = false; }
            --g_foInGt;
            if (ok) { ++g_fFast; if (o) ++g_fFound; return o; }
        }
        if (!gt) return orig(cls, outer, name, exact, searcher);
        auto t0 = Clock::now();
        UObject* r = orig(cls, outer, name, exact, searcher);
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        ++g_fSlow; g_fSlowMs += ms; g_fSlowMax = std::max(g_fSlowMax, ms);
        return r;
    }
    auto fo_install() -> void
    {
        static bool tried = false;
        if (tried) return;
        tried = true;
        HMODULE u = GetModuleHandleW(L"UE4SS.dll");
        void* tg = u ? (void*)GetProcAddress(u, "?FindObject@UObjectGlobals@Unreal@RC@@YAPEAVUObject@23@PEAVUClass@23@PEAV423@PEB_W_NPEAUObjectSearcher@23@@Z") : nullptr;
        if (!tg) { logl("NOLA b176 natfind: FindObject export not found (no detour)"); return; }
        g_foDet = new x0::hot::Detour((uint64_t)tg, (uint64_t)&fo_detour, &g_foTramp);
        bool ok = g_foDet->hook();
        logl(std::string("NOLA b176 natfind: UE4SS FindObject detour ") + (ok ? "on" : "FAILED"));
        if (!ok) { delete g_foDet; g_foDet = nullptr; g_foTramp = 0; }
    }

    std::unordered_map<std::wstring, WRef> g_assets;
    int g_assetMiss = 0, g_assetReload = 0, g_assetRooted = 0;
    auto find_asset(const std::wstring& path) -> UObject*
    {
        auto& r = g_assets[path];
        if (UObject* o = r.get()) return o;
        if (r.raw) ++g_assetReload;
        UObject* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
        if (!o)
        {
            auto* reg = static_cast<UAssetRegistry*>(UAssetRegistryHelpers::GetAssetRegistry().ObjectPointer);
            if (reg)
            {
                FAssetData ad = reg->GetAssetByObjectPath(FName(path.c_str(), FNAME_Add));
                if ((Version::IsAtMost(5, 0) && ad.ObjectPath().GetComparisonIndex()) || ad.PackageName().GetComparisonIndex())
                    o = UAssetRegistryHelpers::GetAsset(ad);
            }
        }
        if (!o) ++g_assetMiss;
        r.set(o);
        return o;
    }
    std::unordered_set<std::wstring> g_assetNeg;
    auto find_asset_once(const std::wstring& path) -> UObject*
    {
        if (g_assetNeg.count(path)) return nullptr;
        UObject* o = find_asset(path);
        if (!o) g_assetNeg.insert(path);
        return o;
    }
    auto asset_ready(const std::wstring& path) -> bool
    {
        if (g_assetNeg.count(path)) return true;
        auto it = g_assets.find(path);
        return it != g_assets.end() && it->second.get() != nullptr;
    }
    std::unordered_map<std::wstring, double> g_asReq;
    std::unordered_set<std::wstring> g_asGive;
    WRef g_asMe, g_ksl;
    int32_t g_asUuid = 0x173000;
    int g_asLeft = 0;
    std::set<std::pair<UObject*, int32_t>> g_dupDeadHid;
    int g_asN = 0, g_asDone = 0, g_asTo = 0, g_asFail = 0, g_asSync = 0, g_asWaitN = 0;
    double g_asWait = 0, g_asWaitMax = 0;
    auto resident(const std::wstring& path) -> UObject*
    {
        UObject* o = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
        if (!o) return nullptr;
        if (o->HasAnyFlags(static_cast<EObjectFlags>(0x400 | 0x1000 | 0x2000))) return nullptr;
        if (o->HasAnyFlags(static_cast<EObjectFlags>(0x80000)) && !o->HasAnyFlags(static_cast<EObjectFlags>(0x200000))) return nullptr;
        FUObjectItem* it = FUObjectArray::IndexToObject(o->GetInternalIndex());
        if (!it || it->GetUObject() != o || it->HasAnyFlags(static_cast<EInternalObjectFlags>((1 << 28) | (1 << 21)))) return nullptr;
        return o;
    }
    auto async_load(const std::wstring& path) -> bool
    {
        UObject* me = g_asMe.get();
        UObject* k = g_ksl.get();
        if (!k) { k = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.Default__KismetSystemLibrary")); g_ksl.set(k); }
        if (!me || !k) return false;
        size_t dot = path.rfind(L'.'), sl = path.rfind(L'/');
        if (dot == std::wstring::npos || sl == std::wstring::npos || dot < sl) return false;
        Call c(k, STR("LoadAsset"));
        c.ob(STR("WorldContextObject"), me);
        uint8_t* a = c.at(STR("Asset"), 0x28);
        uint8_t* li = c.at(STR("LatentInfo"), 0x18);
        if (c.bad || !a || !li) return false;
        FName pk(path.substr(0, dot).c_str(), FNAME_Add), an(path.substr(dot + 1).c_str(), FNAME_Add);
        std::memcpy(a + 8, &pk, std::min<size_t>(8, sizeof pk));
        std::memcpy(a + 0x10, &an, std::min<size_t>(8, sizeof an));
        *reinterpret_cast<int32_t*>(li) = -1;
        *reinterpret_cast<int32_t*>(li + 4) = ++g_asUuid;
        *reinterpret_cast<UObject**>(li + 0x10) = me;
        return c.run();
    }
    auto asset_state(const std::wstring& w) -> int
    {
        if (asset_ready(w)) return 0;
        if (!C.natasync || g_asGive.count(w) || !g_asMe.get()) return 2;
        double now = now_s();
        auto it = g_asReq.find(w);
        if (UObject* o = resident(w))
        {
            g_assets[w].set(o);
            if (it != g_asReq.end()) { double d = now - it->second; g_asWait += d; ++g_asWaitN; g_asWaitMax = std::max(g_asWaitMax, d); ++g_asDone; g_asReq.erase(it); }
            return 0;
        }
        if (it == g_asReq.end())
        {
            if (g_asLeft <= 0) return 1;
            --g_asLeft;
            if (!async_load(w)) { ++g_asFail; g_asGive.insert(w); return 2; }
            g_asReq[w] = now; ++g_asN;
            return 1;
        }
        if (now - it->second < C.natasyncto) return 1;
        ++g_asTo; g_asReq.erase(it); g_asGive.insert(w);
        return 2;
    }
    int g_viaP = 0, g_pollSkip = 0, g_pollRead = 0, g_pollBad = 0;
    double g_pollMs = 0;
    std::unordered_set<UObject*> g_rooted;
    auto keep_loaded(UObject* o) -> void
    {
        if (!o || g_rooted.count(o)) return;
        FUObjectItem* it = FUObjectArray::IndexToObject(o->GetInternalIndex());
        if (!it || it->GetUObject() != o) return;
        it->SetRootSet();
        g_rooted.insert(o); ++g_assetRooted;
    }
    auto root_mont(const std::string& p) -> bool
    {
        std::string l = p;
        for (char& ch : l) if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
        return l.find("cov") != std::string::npos || l.find("contextual") != std::string::npos || l.find("_ca_") != std::string::npos;
    }

    struct Part
    {
        std::string key, mesh;
        std::vector<std::string> mats;
        bool vis = true;
        UObject* comp = nullptr;
        UObject* skel = nullptr;
        bool self = false, made = false;
        bool ext = false;
        int nlod = -1;
        std::vector<uint32_t> hid;
        std::vector<float> cpd;
        std::vector<s2nlook::Sc> sc;
    };
    int g_secSet = 0, g_secDif = 0, g_secFail = 0, g_cpdSet = 0, g_cpdDif = 0, g_cpdFail = 0, g_genMiss = 0, g_mutLookH = 0, g_genMk = 0, g_genMkMade = 0, g_genMkFail = 0, g_genRemk = 0;
    int g_midSet = 0, g_midDif = 0, g_midFail = 0, g_midHost = 0;
    auto sec_read_seh(const void* c, uint32_t* m, int cap) -> int
    {
        __try
        {
            auto* b = reinterpret_cast<const uint8_t*>(c);
            const uint8_t* d = *reinterpret_cast<const uint8_t* const*>(b + 0x738);
            int n = *reinterpret_cast<const int*>(b + 0x740);
            if (!d || n <= 0) return 0;
            if (n > cap) n = cap;
            for (int l = 0; l < n; ++l)
            {
                const uint8_t* e = d + (size_t)l * 0x28;
                const uint8_t* h = *reinterpret_cast<const uint8_t* const*>(e);
                int k = *reinterpret_cast<const int*>(e + 8);
                uint32_t v = 0;
                if (h && k > 0) for (int i = 0; i < k && i < 32; ++i) if (h[i]) v |= 1u << i;
                m[l] = v;
            }
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto cpd_read_seh(const void* c, float* v, int cap) -> int
    {
        __try
        {
            auto* b = reinterpret_cast<const uint8_t*>(c);
            const float* d = *reinterpret_cast<const float* const*>(b + 0x2a8);
            int n = *reinterpret_cast<const int*>(b + 0x2b0);
            if (!d || n <= 0) return 0;
            if (n > cap) n = cap;
            for (int i = 0; i < n; ++i) v[i] = d[i];
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    int g_scAo = -2, g_scEs = 0, g_scIo = 0, g_scNo = 0, g_scVo = 0;
    struct ScRaw { int idx, num; float v; };
    auto sc_layout(UObject* m) -> bool
    {
        if (g_scAo == -2)
        {
            g_scAo = -1;
            auto* ap = CastField<FArrayProperty>(m->GetPropertyByNameInChain(STR("ScalarParameterValues")));
            auto* sp = ap ? CastField<FStructProperty>(ap->GetInner()) : nullptr;
            FProperty* ip = sp && sp->GetStruct() ? sp->GetStruct()->FindProperty(FName(STR("ParameterInfo"))) : nullptr;
            FProperty* vp = sp && sp->GetStruct() ? sp->GetStruct()->FindProperty(FName(STR("ParameterValue"))) : nullptr;
            auto* isp = CastField<FStructProperty>(ip);
            FProperty* np = isp && isp->GetStruct() ? isp->GetStruct()->FindProperty(FName(STR("Name"))) : nullptr;
            if (np && vp && np->GetSize() == 8 && vp->GetSize() == 4)
            { g_scAo = ap->GetOffset_Internal(); g_scEs = sp->GetSize(); g_scIo = ip->GetOffset_Internal(); g_scNo = np->GetOffset_Internal(); g_scVo = vp->GetOffset_Internal(); }
        }
        return g_scAo >= 0;
    }
    auto sc_read_seh(const void* m, ScRaw* out, int cap) -> int
    {
        __try
        {
            const uint8_t* base = reinterpret_cast<const uint8_t*>(m) + g_scAo;
            const uint8_t* d = *reinterpret_cast<const uint8_t* const*>(base);
            int n = *reinterpret_cast<const int*>(base + 8);
            if (!d || n <= 0) return 0;
            if (n > cap) n = cap;
            for (int i = 0; i < n; ++i)
            {
                const uint8_t* el = d + (size_t)i * g_scEs;
                out[i].idx = *reinterpret_cast<const int*>(el + g_scIo + g_scNo);
                out[i].num = *reinterpret_cast<const int*>(el + g_scIo + g_scNo + 4);
                out[i].v = *reinterpret_cast<const float*>(el + g_scVo);
            }
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto is_mid(UObject* m) -> bool
    {
        UClass* k = m ? m->GetClassPrivate() : nullptr;
        return k && k->GetName() == STR("MaterialInstanceDynamic");
    }
    auto mid_read(UObject* comp, std::vector<s2nlook::Sc>& out) -> void
    {
        static std::unordered_map<uint64_t, std::string> names;
        Call nmats(comp, STR("GetNumMaterials"));
        int n = nmats.run() ? nmats.ri() : 0;
        for (int i = 0; i < n && i < 8; ++i)
        {
            Call gm(comp, STR("GetMaterial"));
            gm.i(STR("ElementIndex"), i);
            UObject* m = gm.run() ? gm.ro() : nullptr;
            if (!is_mid(m) || !sc_layout(m)) continue;
            ScRaw raw[96];
            int nr = sc_read_seh(m, raw, 96);
            for (int r = 0; r < nr; ++r)
            {
                uint64_t key = ((uint64_t)(uint32_t)raw[r].idx << 32) | (uint32_t)raw[r].num;
                auto it = names.find(key);
                if (it == names.end())
                {
                    std::string s;
                    try { s = w2u(FName(raw[r].idx, raw[r].num).ToString()); } catch (...) { s.clear(); }
                    if (!s2nlook::sc_name_ok(s) || !s2nlook::sc_name_gore(s)) s.clear();
                    it = names.emplace(key, s).first;
                }
                if (it->second.empty()) continue;
                s2nlook::Sc e;
                e.slot = i; e.name = it->second; e.v = raw[r].v;
                out.push_back(std::move(e));
            }
        }
    }
    auto mid_apply(UObject* comp, const std::vector<s2nlook::Sc>& want) -> int
    {
        if (want.empty()) return 0;
        std::vector<s2nlook::Sc> cur;
        mid_read(comp, cur);
        int set = 0;
        for (const auto& e : want)
        {
            bool same = false;
            for (const auto& c : cur) if (c.slot == e.slot && c.name == e.name) { same = s2nlook::cpd_same(e.v, c.v); break; }
            if (same) continue;
            Call gm(comp, STR("GetMaterial"));
            gm.i(STR("ElementIndex"), e.slot);
            UObject* m = gm.run() ? gm.ro() : nullptr;
            if (m && !is_mid(m))
            {
                Call cm(comp, STR("CreateAndSetMaterialInstanceDynamic"));
                cm.i(STR("ElementIndex"), e.slot);
                m = cm.run() ? cm.ro() : nullptr;
            }
            if (!m) { ++g_midFail; continue; }
            std::wstring wn = u2w(e.name);
            Call s(m, STR("SetScalarParameterValue"));
            s.nm(STR("ParameterName"), wn.c_str()).fl(STR("Value"), e.v);
            s.run() ? ++g_midSet : ++g_midFail;
            ++set;
        }
        return set;
    }
    auto ext_read(UObject* c, Part& p) -> void
    {
        p.ext = true;
        uint32_t hm[8] = {};
        int nl = sec_read_seh(c, hm, 8);
        p.nlod = nl < 0 ? 0 : nl;
        for (int l = 0; l < nl; ++l) p.hid.push_back(hm[l]);
        s2nlook::hid_trim(p.hid);
        float cv[16] = {};
        int nc = cpd_read_seh(c, cv, 16);
        for (int i = 0; i < nc; ++i) p.cpd.push_back(cv[i]);
        if (C.natmidsc) mid_read(c, p.sc);
    }
    auto sec_apply(UObject* comp, const std::vector<uint32_t>& want) -> int
    {
        uint32_t cur[8] = {};
        int nl = sec_read_seh(comp, cur, 8);
        int set = 0;
        for (int l = 0; l < nl; ++l)
        {
            uint32_t w = l < (int)want.size() ? want[l] : 0u;
            uint32_t d = w ^ cur[l];
            for (int i = 0; i < 32 && d; ++i)
            {
                if (!(d & (1u << i))) continue;
                d &= ~(1u << i);
                Call s(comp, STR("ShowMaterialSection"));
                s.i(STR("MaterialID"), i).i(STR("SectionIndex"), -1).bl(STR("bShow"), !(w & (1u << i))).i(STR("LODIndex"), l);
                s.run() ? ++g_secSet : ++g_secFail;
                ++set;
            }
        }
        return set;
    }
    auto cpd_apply(UObject* comp, const std::vector<float>& want) -> int
    {
        float cur[16] = {};
        int n = cpd_read_seh(comp, cur, 16);
        if (n < 0) return 0;
        int set = 0;
        for (int i = (int)std::min<size_t>(want.size(), 16) - 1; i >= 0; --i)
        {
            if (i < n && s2nlook::cpd_same(want[i], cur[i])) continue;
            Call s(comp, STR("SetCustomPrimitiveDataFloat"));
            s.i(STR("DataIndex"), i).fl(STR("Value"), want[i]);
            s.run() ? ++g_cpdSet : ++g_cpdFail;
            ++set;
        }
        return set;
    }
    std::unordered_map<UObject*, WRef> g_made;
    std::unordered_map<UObject*, std::string> g_madeKey;
    std::unordered_map<UObject*, std::vector<std::string>> g_madeEver;
    auto pack_path(std::string p) -> std::string { return s2nmab::pack_path(std::move(p)); }
    auto unpack_path(const std::string& p0) -> std::string { return s2nmab::unpack_path(p0); }
    auto norm_key(std::wstring n) -> std::string
    {
        auto hex = [](wchar_t ch) { return (ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'F') || (ch >= L'a' && ch <= L'f'); };
        if (n.size() > 32 && std::all_of(n.end() - 32, n.end(), hex)) n.resize(n.size() - 32);
        size_t u = n.rfind(L'_');
        if (u != std::wstring::npos && u + 1 < n.size() && std::all_of(n.begin() + (ptrdiff_t)u + 1, n.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) n.resize(u);
        return pack_path(w2u(n));
    }
    auto mat_path(UObject* m) -> std::string
    {
        if (!m) return "";
        std::wstring cn = m->GetClassPrivate() ? m->GetClassPrivate()->GetName() : L"";
        if (cn == STR("MaterialInstanceDynamic")) { m = prop_obj(m, STR("Parent")); if (!m) return ""; }
        return pack_path(w2u(m->GetPathName()));
    }
    int g_wpSet = 0, g_wpFail = 0, g_wpNoCls = 0, g_wpReread = 0;
    auto wpn_key(const std::string& k) -> bool { return k.rfind("WeaponInHandsMesh", 0) == 0; }
    auto wpn_split(const std::string& f, std::string& mesh, std::string& cls) -> void
    {
        size_t h = f.find('!');
        mesh = f.substr(0, h);
        cls = h == std::string::npos ? std::string() : f.substr(h + 1);
    }
    auto parts_of(UObject* a, std::vector<Part>& out, bool slots = false) -> void
    {
        std::vector<UObject*> comps;
        comps_of(a, STR("/Script/Engine.SkeletalMeshComponent"), comps);
        std::unordered_map<std::string, int> cnt;
        size_t natives = 0;
        if (g_made.size() > 512)
            for (auto it = g_made.begin(); it != g_made.end();) { if (!it->second.get()) { g_madeKey.erase(it->first); it = g_made.erase(it); } else ++it; }
        for (UObject* c : comps)
        {
            if (!c) continue;
            std::wstring nm = c->GetName();
            if (nm == STR("ShadowMesh")) continue;
            Part p;
            auto mk = g_made.find(c);
            if (mk != g_made.end() && mk->second.get() == c) { p.key = g_madeKey[c]; p.made = true; }
            else
            {
                if (!s2nlook::part_room(false, natives)) continue;
                ++natives;
                std::string k = norm_key(nm);
                p.key = k + ":" + std::to_string(cnt[k]++);
            }
            UObject* mesh = c_obj(c, STR("GetSkeletalMeshAsset"));
            if (slots)
            {
                p.self = anim_of(c) != nullptr;
                UObject* src = c;
                if (!p.self) { UObject* par = c_obj(c, STR("GetAttachParent")); if (par && c_obj(par, STR("GetSkeletalMeshAsset"))) src = par; }
                UObject* sm = src == c ? mesh : c_obj(src, STR("GetSkeletalMeshAsset"));
                p.skel = sm ? prop_obj(sm, STR("Skeleton")) : nullptr;
            }
            p.mesh = mesh ? pack_path(w2u(mesh->GetPathName())) : "";
            if (mesh && wpn_key(p.key))
            {
                UObject* cls = prop_obj(c, STR("AnimClass"));
                if (!cls) if (UObject* ai = anim_of(c)) cls = ai->GetClassPrivate();
                if (cls) p.mesh += "!" + pack_path(w2u(cls->GetPathName()));
            }
            Call nmats(c, STR("GetNumMaterials"));
            int n = nmats.run() ? nmats.ri() : 0;
            for (int i = 0; i < n && i < 8; ++i)
            {
                Call gm(c, STR("GetMaterial"));
                gm.i(STR("ElementIndex"), i);
                p.mats.push_back(gm.run() ? mat_path(gm.ro()) : "");
            }
            p.vis = c_bool(c, STR("IsVisible"), true);
            p.comp = c;
            if (C.natlooksec) ext_read(c, p);
            out.push_back(std::move(p));
        }
    }
    auto look_str(const std::vector<Part>& ps) -> std::string
    {
        std::string s;
        for (const auto& p : ps)
        {
            if (!s.empty()) s += "|";
            s += p.key + "@" + p.mesh + "@";
            for (size_t i = 0; i < p.mats.size(); ++i) { if (i) s += "+"; s += p.mats[i]; }
            s += "@" + s2nlook::tail_txt(p.vis, p.ext, p.hid, p.cpd, p.sc);
        }
        return s;
    }
    auto parse_look(const std::string& s, std::vector<Part>& out) -> void
    {
        size_t i = 0;
        while (i <= s.size())
        {
            size_t j = s.find('|', i);
            if (j == std::string::npos) j = s.size();
            std::string e = s.substr(i, j - i);
            i = j + 1;
            if (e.empty()) { if (j >= s.size()) break; continue; }
            Part p;
            size_t a = e.find('@'), b = a == std::string::npos ? a : e.find('@', a + 1), c = b == std::string::npos ? b : e.find('@', b + 1);
            if (c == std::string::npos) continue;
            p.key = e.substr(0, a);
            if (p.key.rfind("MeshGen", 0) == 0) { if (j >= s.size()) break; continue; }
            p.mesh = e.substr(a + 1, b - a - 1);
            std::string ms = e.substr(b + 1, c - b - 1);
            size_t k = 0;
            while (k <= ms.size()) { size_t q = ms.find('+', k); if (q == std::string::npos) q = ms.size(); p.mats.push_back(ms.substr(k, q - k)); k = q + 1; if (q >= ms.size()) break; }
            s2nlook::Tail t = s2nlook::tail_parse(e.substr(c + 1));
            p.vis = t.vis; p.ext = t.ext; p.hid = std::move(t.hid); p.cpd = std::move(t.cpd); p.sc = std::move(t.sc);
            out.push_back(std::move(p));
            if (j >= s.size()) break;
        }
    }
    auto fnv(const std::string& s) -> uint32_t { uint32_t h = 2166136261u; for (unsigned char ch : s) { h ^= ch; h *= 16777619u; } return h ? h : 1; }

    auto special_key(const std::string& k) -> bool
    {
        return k.rfind("WeaponInHandsMesh", 0) == 0 || k.rfind("SkeletalMeshComponent", 0) == 0 || k.rfind("CharacterMesh0", 0) == 0;
    }
    auto body_key(const std::string& k) -> bool { return k.rfind("CharacterMesh0", 0) == 0; }
    std::unordered_map<std::string, WRef> g_hostSkel;
    auto host_skel(const std::string& mesh) -> UObject*
    {
        if (mesh.empty()) return nullptr;
        auto it = g_hostSkel.find(mesh);
        if (it != g_hostSkel.end()) return it->second.get();
        UObject* ms = find_asset(u2w(unpack_path(mesh)));
        UObject* sk = ms ? prop_obj(ms, STR("Skeleton")) : nullptr;
        g_hostSkel[mesh].set(sk);
        return sk;
    }
    auto part_pri(const Part& p) -> int
    {
        const std::string& m = p.mesh;
        size_t sl = m.rfind('/');
        std::string n = m.substr(sl == std::string::npos ? 0 : sl + 1);
        if (n.rfind("SK_fac_", 0) == 0 || n.rfind("SM_fac_", 0) == 0) return 0;
        if (n.rfind("SK_mas_", 0) == 0 || n.rfind("SK_hea_", 0) == 0 || n.rfind("SK_hel_", 0) == 0 || n.rfind("SK_fol_hea", 0) == 0) return 1;
        if (n.rfind("SK_han_", 0) == 0 || n.rfind("SK_glo_", 0) == 0) return 2;
        return 3;
    }
    auto face_root(const std::string& m) -> bool
    {
        size_t sl = m.rfind('/');
        std::string n = m.substr(sl == std::string::npos ? 0 : sl + 1);
        if (n.rfind("SK_fac_", 0) != 0) return false;
        bool digits = true;
        for (size_t i = 7; i < n.size(); ++i) if (!(n[i] == '_' || (n[i] >= '0' && n[i] <= '9'))) { digits = false; break; }
        if (digits) return true;
        static std::unordered_map<std::string, bool> memo;
        auto it = memo.find(m);
        if (it != memo.end()) return it->second;
        bool fac = false;
        if (UObject* sk = host_skel(m)) { try { fac = w2u(sk->GetName()) == "SKEL_fac"; } catch (...) { fac = false; } }
        if (host_skel(m)) memo[m] = fac;
        return fac;
    }
    auto compat(UObject* hs, const Part& c) -> bool { return c.made || !hs || !c.skel || hs == c.skel; }
    auto pair_parts(const std::vector<Part>& host, const std::vector<Part>& cl, bool assign, bool natg = false) -> std::vector<int>
    {
        std::vector<UObject*> hs(host.size(), nullptr);
        for (size_t i = 0; i < host.size(); ++i) if (!special_key(host[i].key)) hs[i] = host_skel(host[i].mesh);
        return s2nlook::pair_parts(host, cl, assign, natg, [](const std::string& k) { return special_key(k); }, [&](size_t i, size_t j) { return compat(hs[i], cl[j]); }, [](const Part& p) { return part_pri(p); });
    }

    int g_lookNoSpecial = 0;
    int g_lookFaceMiss = 0;
    auto look_diff(const std::vector<Part>& host, const std::vector<Part>& cl, std::string* why, bool noExtra = false, bool mut = false, bool natg = false) -> int
    {
        int n = 0;
        auto add = [&](const std::string& t) { if (why && why->size() < 300) *why += t; };
        std::vector<int> m = pair_parts(host, cl, false, natg);
        std::vector<char> used(cl.size(), 0);
        g_lookNoSpecial = 0;
        for (size_t i = 0; i < host.size(); ++i)
        {
            const Part& h = host[i];
            if (m[i] < 0)
            {
                if (!h.vis) continue;
                if (special_key(h.key)) { ++g_lookNoSpecial; add(" sem:" + h.key); continue; }
                if (face_root(h.mesh)) ++g_lookFaceMiss;
                ++n; add(" falta:" + h.key); continue;
            }
            used[m[i]] = 1;
            const Part& c = cl[m[i]];
            bool prop = special_key(h.key) && !body_key(h.key);
            if (wpn_key(h.key)) continue;
            if (!h.vis && !c.vis) continue;
            if (prop && (!h.vis || !c.vis)) continue;
            if (h.mesh != c.mesh) { ++n; add(" mesh:" + h.key + "=" + c.mesh + "!=" + h.mesh); continue; }
            if (!mut && h.vis != c.vis) { ++n; add(" vis:" + h.key); continue; }
            for (size_t k = 0; k < h.mats.size(); ++k)
                if (!h.mats[k].empty() && (k >= c.mats.size() || c.mats[k] != h.mats[k]))
                { ++n; add(" mat:" + h.key + "/" + std::to_string(k) + "=" + (k < c.mats.size() ? c.mats[k] : std::string("-")) + "!=" + h.mats[k]); break; }
            if (C.natlooksec && h.ext && c.ext)
            {
                int l = s2nlook::hid_dif(h.hid, c.hid, c.nlod);
                if (l >= 0) { ++n; ++g_secDif; add(" sec:" + h.key + "/lod" + std::to_string(l) + "=" + s2nlook::hid_txt(c.hid) + "!=" + s2nlook::hid_txt(h.hid)); }
                int k = s2nlook::cpd_dif(h.cpd, c.cpd);
                if (k >= 0) { ++n; ++g_cpdDif; add(" cpd:" + h.key + "/" + std::to_string(k) + "=" + s2nlook::cpd_txt(c.cpd) + "!=" + s2nlook::cpd_txt(h.cpd)); }
                if (C.natmidsc)
                {
                    g_midHost += (int)h.sc.size();
                    int q = s2nlook::sc_dif(h.sc, c.sc);
                    if (q >= 0) { ++n; ++g_midDif; add(" mid:" + h.key + "/" + std::to_string(h.sc[q].slot) + ":" + h.sc[q].name + "=" + s2nlook::sc_txt(c.sc) + "!=" + s2nlook::sc_txt(h.sc)); }
                }
            }
        }
        for (size_t j = 0; j < cl.size(); ++j)
            if (!noExtra && !used[j] && cl[j].vis && !special_key(cl[j].key)) { ++n; add(" extra:" + cl[j].key); }
        return n;
    }

    int g_lookSet = 0, g_lookFail = 0, g_lookMade = 0, g_lookMakeFail = 0, g_lookFaceMade = 0;
    WRef g_smcCls, g_facCls;
    int g_faceInit = 0, g_faceRetire = 0, g_faceSkip = 0, g_animLost = 0;
    auto face_offs(UObject* ai, int& fm, int& cm) -> bool
    {
        static void* lastCls = nullptr;
        static int sfm = -1, scm = -1;
        void* cls = ai->GetClassPrivate();
        if (cls != lastCls)
        {
            lastCls = cls; sfm = scm = -1;
            FProperty* p = ai->GetPropertyByNameInChain(STR("FaceData"));
            FStructProperty* sp = p ? CastField<FStructProperty>(p) : nullptr;
            UScriptStruct* ss = sp ? static_cast<UScriptStruct*>(sp->GetStruct()) : nullptr;
            if (ss)
            {
                FProperty* f = ss->FindProperty(FName(STR("FaceMeshComponent")));
                FProperty* c = ss->FindProperty(FName(STR("CharacterSkeletalMeshComponent")));
                if (f && c && f->GetSize() == 8 && c->GetSize() == 8)
                { sfm = p->GetOffset_Internal() + f->GetOffset_Internal(); scm = p->GetOffset_Internal() + c->GetOffset_Internal(); }
            }
        }
        fm = sfm; cm = scm;
        return sfm >= 0 && scm >= 0;
    }
    auto face_setup(UObject* comp, UObject* body) -> bool
    {
        UObject* ai = anim_of(comp);
        if (!ai || !body) return false;
        int fm = -1, cm = -1;
        if (!face_offs(ai, fm, cm)) return false;
        auto** pf = reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(ai) + fm);
        auto** pc = reinterpret_cast<UObject**>(reinterpret_cast<uint8_t*>(ai) + cm);
        if (*pf == comp && *pc == body) return false;
        *pf = comp; *pc = body;
        ++g_faceInit;
        return true;
    }
    auto is_made(UObject* c) -> bool { auto it = g_made.find(c); return c && it != g_made.end() && it->second.get() == c; }
    auto drop_made(UObject* c) -> void
    {
        Call d(c, STR("K2_DestroyComponent"));
        d.ob(STR("Object"), c);
        d.run();
        g_made.erase(c); g_madeKey.erase(c);
    }
    auto face_retire(UObject* a) -> int
    {
        UObject* nf = c_obj(a, STR("GetAnimatedFaceMeshComponent"));
        if (!nf || is_made(nf)) return 0;
        std::vector<UObject*> comps;
        comps_of(a, STR("/Script/Engine.SkeletalMeshComponent"), comps);
        std::vector<UObject*> roots;
        for (UObject* c : comps) if (c && c != nf && is_made(c) && anim_of(c)) roots.push_back(c);
        if (roots.empty()) return 0;
        UObject* body = prop_obj(a, STR("Mesh"));
        for (UObject* r : roots)
        {
            for (UObject* c : comps)
            {
                if (!c || c == r || c_obj(c, STR("GetAttachParent")) != r) continue;
                if (is_made(c)) drop_made(c);
                else if (body)
                {
                    Call t(c, STR("K2_AttachToComponent"));
                    t.ob(STR("Parent"), body).i(STR("LocationRule"), 2).i(STR("RotationRule"), 2).i(STR("ScaleRule"), 2).bl(STR("bWeldSimulatedBodies"), false);
                    t.run();
                }
            }
            drop_made(r);
            ++g_faceRetire;
        }
        return (int)roots.size();
    }
    std::unordered_map<void*, std::string> g_animSt;
    auto anim_state(UObject* a) -> std::string
    {
        std::string s;
        UObject* m = prop_obj(a, STR("Mesh"));
        s += m ? (anim_of(m) ? "corpo=ok" : (prop_obj(m, STR("AnimClass")) ? "corpo=SEM_INST" : "corpo=SEM_CLASSE")) : "corpo=sem_mesh";
        UObject* w = c_obj(a, STR("GetWeaponInHandsMeshComponent"));
        UObject* wm = w ? c_obj(w, STR("GetSkeletalMeshAsset")) : nullptr;
        if (wm) s += anim_of(w) ? " arma=ok" : (prop_obj(w, STR("AnimClass")) ? " arma=SEM_INST" : " arma=SEM_CLASSE");
        else s += " arma=nenhuma";
        return s;
    }
    auto anim_watch(UObject* a, const char* who) -> void
    {
        if (!a) return;
        std::string st = anim_state(a);
        auto& old = g_animSt[a];
        if (old == st) return;
        if (old.find("SEM_") != std::string::npos || st.find("SEM_") != std::string::npos)
        {
            SYSTEMTIME t; GetLocalTime(&t);
            char b[300];
            snprintf(b, sizeof b, "NOLA b191k anim %02d:%02d:%02d.%03d %s %s: %s -> %s", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, who,
                     w2u(a->GetName()).c_str(), old.empty() ? "?" : old.c_str(), st.c_str());
            logl(b);
            if (st.find("SEM_") != std::string::npos) ++g_animLost;
        }
        old = st;
        if (g_animSt.size() > 512) g_animSt.clear();
    }
    auto make_part(UObject* a, UObject* src, bool faceRoot) -> UObject*
    {
        UObject* cls = g_smcCls.get();
        if (!cls) { cls = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Engine.SkeletalMeshComponent")); g_smcCls.set(cls); }
        if (!cls || !a || !src) return nullptr;
        UObject* comp = nullptr;
        {
            Call c(a, STR("AddComponentByClass"));
            c.ob(STR("Class"), cls).bl(STR("bManualAttachment"), true).bl(STR("bDeferredFinish"), false);
            if (uint8_t* t = c.at(STR("RelativeTransform"), 0x60))
            {
                auto* q = reinterpret_cast<double*>(t);
                q[3] = 1.0; q[8] = 1.0; q[9] = 1.0; q[10] = 1.0;
            }
            if (c.run()) comp = c.ro();
        }
        if (!comp) return nullptr;
        {
            Call c(comp, STR("K2_AttachToComponent"));
            c.ob(STR("Parent"), src).i(STR("LocationRule"), 2).i(STR("RotationRule"), 2).i(STR("ScaleRule"), 2).bl(STR("bWeldSimulatedBodies"), false);
            c.run();
        }
        if (faceRoot)
        {
            UObject* fc = g_facCls.get();
            if (!fc) { fc = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Game/_STALKER2/Animations/Fac/AnimBP_Fac.AnimBP_Fac_C")); g_facCls.set(fc); }
            if (fc) { Call c(comp, STR("SetAnimInstanceClass")); c.ob(STR("NewClass"), fc); c.run(); face_setup(comp, src); }
            else faceRoot = false;
        }
        if (!faceRoot)
        {
            Call c(comp, STR("SetLeaderPoseComponent"));
            c.ob(STR("NewLeaderBoneComponent"), src).bl(STR("bForceUpdate"), true).bl(STR("bInFollowerShouldTickPose"), false);
            c.run();
        }
        return comp;
    }
    using GenSidFn = uint32_t (*)(void*, const wchar_t*, char);
    using GenResizeFn = void (*)(uintptr_t, int);
    using GenPickFn = void (*)(uintptr_t, uint32_t*);
    using GenRedressFn = void (*)(void*, uint32_t*);
    GenSidFn g_genSid = nullptr; GenResizeFn g_genResize = nullptr; GenRedressFn g_genRedress = nullptr;
    uint8_t* g_genTab = nullptr;
    uint64_t g_genTramp = 0;
    PLH::x64Detour* g_genDet = nullptr;
    const char* g_genState = "nao instalado";
    uintptr_t g_genPend = 0; uint32_t g_genIds[48] = {}; int g_genN = 0;
    int g_genRe = 0, g_genOk = 0, g_genFail = 0, g_genHook = 0, g_genDrop = 0, g_genSent = 0, g_genMre = 0;
    double g_genMsMax = 0;
    auto gen_of(void* a) -> uintptr_t
    {
        __try { uintptr_t ob = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(a) + 0x650); return ob ? *reinterpret_cast<uintptr_t*>(ob + 0x690) : 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    auto gen_read(uintptr_t gen, uint32_t* s118, uint32_t* ids, int cap) -> int
    {
        __try
        {
            *s118 = *reinterpret_cast<uint32_t*>(gen + 0x118);
            int n = *reinterpret_cast<int*>(gen + 0x20); auto* d = *reinterpret_cast<uint32_t**>(gen + 0x18);
            if (!d || n < 0 || n > cap) return n > cap ? -1 : 0;
            for (int i = 0; i < n; ++i) ids[i] = d[i];
            return n;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto gen_cstr(uint32_t id, char* b, int n) -> void
    {
        b[0] = 0;
        if (!id || (int)id < 0 || !g_genTab) { strcpy_s(b, n, "-"); return; }
        __try
        {
            uintptr_t ch = *reinterpret_cast<uintptr_t*>(g_genTab + (id >> 10) * 8);
            uintptr_t e = ch + (id & 0x3ff) * 0x20;
            const wchar_t* p = *reinterpret_cast<const wchar_t**>(e); int len = *reinterpret_cast<int*>(e + 8);
            if (!p || len <= 0 || len > 200) { strcpy_s(b, n, "-"); return; }
            int k = 0;
            for (int i = 0; i < len && p[i] && k < n - 1; ++i) { wchar_t c = p[i]; b[k++] = (c < 128 && c != '/' && c != '|' && c != '@' && c != '+' && c != ';' && c != '#') ? (char)c : '_'; }
            b[k] = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(b, n, "-"); }
    }
    auto gen_picks(uintptr_t gen, std::string* sid) -> std::string
    {
        uint32_t s118 = 0, ids[48];
        int n = gen ? gen_read(gen, &s118, ids, 48) : -1;
        if (n <= 0) return "";
        char b[256];
        if (sid) { gen_cstr(s118, b, sizeof b); *sid = b; }
        std::string o;
        for (int i = 0; i < n; ++i) { gen_cstr(ids[i], b, sizeof b); if (i) o += "/"; o += b; }
        return o;
    }
    auto gen_str(void* a) -> std::string
    {
        std::string sid;
        std::string p = gen_picks(gen_of(a), &sid);
        if (p.empty() || sid.empty() || sid == "-") return "";
        return sid + "/" + p;
    }
    auto look_gen(const std::string& look) -> std::string
    {
        size_t k = look.find("MeshGen:0@");
        if (k == std::string::npos || (k > 0 && look[k - 1] != '|')) return "";
        size_t a = k + 10, b = look.find('@', a);
        return b == std::string::npos ? "" : look.substr(a, b - a);
    }
    void gen_pick_detour(uintptr_t gen, uint32_t* p2)
    {
        if (gen && gen == g_genPend && g_genN > 0)
        {
            int n = g_genN; g_genN = 0;
            bool ok = false;
            __try
            {
                if (*reinterpret_cast<int*>(gen + 0x24) < n) g_genResize(gen + 0x18, n);
                auto* d = *reinterpret_cast<uint32_t**>(gen + 0x18);
                if (d && *reinterpret_cast<int*>(gen + 0x24) >= n)
                {
                    for (int i = 0; i < n; ++i) d[i] = g_genIds[i];
                    *reinterpret_cast<int*>(gen + 0x20) = n;
                    ok = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
            if (ok) { ++g_genHook; return; }
        }
        reinterpret_cast<GenPickFn>(g_genTramp)(gen, p2);
    }
    auto gen_redress_seh(void* a, uint32_t* sid) -> bool
    {
        uintptr_t pf0 = s2n_pf::save();
        __try { g_genRedress(a, sid); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return false; }
    }
    auto gen_on() -> bool { return C.natgen && g_genDet && g_genTramp; }
    auto gen_apply(UObject* a, const std::string& g) -> int
    {
        if (!gen_on() || !a || g.empty()) return -1;
        uintptr_t gen = gen_of(a);
        if (!gen) return -1;
        auto drop = [&]() {
            std::vector<UObject*> comps;
            comps_of(a, STR("/Script/Engine.SkeletalMeshComponent"), comps);
            for (UObject* c : comps) if (is_made(c)) { drop_made(c); ++g_genDrop; }
        };
        auto redress = [&](const std::string& sid, const std::string& want) -> bool {
            uint32_t ids[48]; int n = 0;
            size_t i = 0;
            while (i <= want.size() && n < 48)
            {
                size_t j = want.find('/', i); if (j == std::string::npos) j = want.size();
                std::string t = want.substr(i, j - i);
                ids[n++] = (t.empty() || t == "-") ? 0u : g_genSid(g_genTab, u2w(t).c_str(), 1);
                i = j + 1;
                if (j >= want.size()) break;
            }
            uint32_t sidId = g_genSid(g_genTab, u2w(sid).c_str(), 1);
            std::memcpy(g_genIds, ids, sizeof(uint32_t) * n); g_genN = n; g_genPend = gen;
            LARGE_INTEGER f, t0, t1; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
            bool ran = gen_redress_seh(a, &sidId);
            QueryPerformanceCounter(&t1);
            g_genPend = 0; g_genN = 0;
            double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
            if (ms > g_genMsMax) g_genMsMax = ms;
            ++g_genRe;
            bool ok = ran && gen_picks(gen_of(a), nullptr) == want;
            ok ? ++g_genOk : ++g_genFail;
            return ok;
        };
        return s2nlook::gen_apply_plan(g, gen_picks(gen, nullptr), drop, redress);
    }
    auto gen_same(UObject* a, const std::string& g) -> bool
    {
        size_t sl = g.find('/');
        return sl != std::string::npos && gen_picks(gen_of(a), nullptr) == g.substr(sl + 1);
    }
    auto gen_keep(std::vector<Part>& host, const std::vector<Part>& cl, std::string* why = nullptr, bool count = false) -> int
    {
        int miss = 0;
        bool mk = C.natgenmk && C.natlooksec;
        host.erase(std::remove_if(host.begin(), host.end(), [&](const Part& p) {
            if (special_key(p.key) && !body_key(p.key)) return false;
            for (const Part& c : cl) if (c.comp && (mk || !c.made) && !c.mesh.empty() && c.mesh == p.mesh) { if (c.made && count) ++g_genMk; return false; }
            if (p.vis && !p.mesh.empty()) { if (mk) { if (count) ++g_genMk; return false; } ++miss; if (why && why->size() < 300) *why += " gerador_falta:" + p.key; }
            return true; }), host.end());
        if (!C.natlooksec) return 0;
        g_genMiss += miss;
        return miss;
    }
    auto apply_look(UObject* a, const std::string& look, int& mism, std::string* why, bool mut = false) -> void
    {
        std::vector<Part> host, cl;
        parse_look(look, host);
        std::string gs = gen_on() && !mut ? look_gen(look) : std::string();
        bool natg = !gs.empty() && gen_apply(a, gs) >= 0;
        if (!mut) face_retire(a);
        UObject* ownFace = mut ? nullptr : c_obj(a, STR("GetAnimatedFaceMeshComponent"));
        parts_of(a, cl, true);
        bool genMk = natg && C.natgenmk && C.natlooksec;
        if (natg)
            for (size_t j = 0; j < cl.size();)
            {
                if (cl[j].made && cl[j].comp && (!genMk || s2nlook::made_stale(cl[j].key, cl[j].mesh, host, cl))) { drop_made(cl[j].comp); ++g_genDrop; cl.erase(cl.begin() + (ptrdiff_t)j); }
                else ++j;
            }
        std::string gmw;
        int gmiss = natg ? gen_keep(host, cl, C.natlooksec ? &gmw : nullptr, true) : 0;
        std::vector<int> m = pair_parts(host, cl, !mut, genMk);
        {
            std::vector<size_t> ord;
            for (size_t i = 0; i < host.size(); ++i)
                if ((!natg || genMk) && !mut && m[i] < 0 && !special_key(host[i].key) && !host[i].mesh.empty() && host[i].vis) ord.push_back(i);
            std::stable_sort(ord.begin(), ord.end(), [&](size_t x, size_t y) { return (face_root(host[x].mesh) ? 0 : 1) < (face_root(host[y].mesh) ? 0 : 1); });
            int made = 0;
            for (size_t i : ord)
            {
                if (made >= (genMk ? C.natgenmkmax : C.natmkmax) || !s2nlook::make_room(cl.size())) break;
                UObject* hs = host_skel(host[i].mesh);
                UObject* body = nullptr; UObject* src = nullptr;
                for (const Part& c : cl)
                {
                    if (!c.comp) continue;
                    if (body_key(c.key) && !body) body = c.comp;
                    if (c.self && hs && c.skel == hs && !src) src = c.comp;
                }
                bool fr = false;
                if (!src)
                {
                    if (face_root(host[i].mesh) && body && ownFace) { ++g_faceSkip; continue; }
                    if (face_root(host[i].mesh) && body) { src = body; fr = true; }
                    else src = body;
                }
                UObject* comp = make_part(a, src, fr);
                if (!comp) { ++g_lookMakeFail; if (genMk) ++g_genMkFail; continue; }
                ++made; ++g_lookMade; if (fr) ++g_lookFaceMade; if (genMk) ++g_genMkMade;
                if (genMk)
                {
                    if (g_madeEver.size() > 512) g_madeEver.clear();
                    std::vector<std::string>& ev = g_madeEver[a];
                    if (std::find(ev.begin(), ev.end(), host[i].key) != ev.end()) ++g_genRemk; else ev.push_back(host[i].key);
                }
                g_made[comp].set(comp);
                g_madeKey[comp] = host[i].key;
                Part p;
                p.key = host[i].key; p.comp = comp; p.made = true; p.self = fr; p.skel = fr ? hs : nullptr; p.vis = true;
                cl.push_back(std::move(p));
                m[i] = (int)cl.size() - 1;
            }
        }
        std::vector<char> used(cl.size(), 0);
        for (size_t i = 0; i < host.size(); ++i)
        {
            if (m[i] < 0) continue;
            used[m[i]] = 1;
            const Part& h = host[i];
            Part& c = cl[m[i]];
            if (!c.comp) continue;
            bool prop = special_key(h.key) && !body_key(h.key);
            if (natg && !prop && h.mesh != c.mesh && !c.made) continue;
            bool meshSet = false;
            if (wpn_key(h.key)) continue;
            if (!h.mesh.empty() && h.mesh != c.mesh)
            {
                if (UObject* ms = find_asset(u2w(unpack_path(h.mesh))))
                {
                    Call s(c.comp, STR("SetSkeletalMeshAsset"));
                    s.ob(STR("NewMesh"), ms);
                    bool ok = s.run();
                    if (!ok) { Call s2(c.comp, STR("SetSkinnedAssetAndUpdate")); s2.ob(STR("NewMesh"), ms).bl(STR("bReinitPose"), true); ok = s2.run(); }
                    ok ? ++g_lookSet : ++g_lookFail;
                    meshSet = ok;
                }
                else ++g_lookFail;
            }
            for (size_t k = 0; k < h.mats.size(); ++k)
            {
                if (h.mats[k].empty() || (!meshSet && k < c.mats.size() && c.mats[k] == h.mats[k])) continue;
                if (UObject* mt = find_asset(u2w(unpack_path(h.mats[k]))))
                {
                    Call s(c.comp, STR("SetMaterial"));
                    s.i(STR("ElementIndex"), (int64_t)k).ob(STR("Material"), mt);
                    s.run() ? ++g_lookSet : ++g_lookFail;
                }
                else ++g_lookFail;
            }
            if (C.natlooksec && h.ext && (h.mesh.empty() || h.mesh == c.mesh || meshSet)) { sec_apply(c.comp, h.hid); cpd_apply(c.comp, h.cpd); if (C.natmidsc) mid_apply(c.comp, h.sc); }
            if (!mut && !prop && (h.vis != c.vis || meshSet || c.made)) { Call s(c.comp, STR("SetVisibility")); s.bl(STR("bNewVisibility"), h.vis).bl(STR("bPropagateToChildren"), false); s.run(); }
            if (c.made && c.self) face_setup(c.comp, prop_obj(a, STR("Mesh")));
        }
        for (size_t j = 0; j < cl.size(); ++j)
            if (!natg && !mut && !used[j] && cl[j].vis && cl[j].comp && !special_key(cl[j].key))
            { Call s(cl[j].comp, STR("SetVisibility")); s.bl(STR("bNewVisibility"), false).bl(STR("bPropagateToChildren"), false); s.run(); }
        std::vector<Part> after;
        parts_of(a, after, true);
        mism = look_diff(host, after, why, natg || mut, mut, genMk);
        if (natg && !gen_same(a, gs)) { ++mism; if (why) *why += " gerador"; }
        if (gmiss > 0) { mism += gmiss; if (why && why->size() < 300) *why += gmw; }
    }

    thread_local int t_wpnOurs = 0;
    volatile LONG g_wpOurs = 0, g_wpBlk = 0, g_wpBlkImpl = 0;
#include "s2n_wpn.inc"
    struct LegSt { double t = -1, ot = -1, w0 = -1, bx = 0, by = 0, fx[2] = {}, fy[2] = {}, body = 0, foot = 0, mmAdv = 0, mmT = 0, mmRate = -1; uintptr_t mmS = 0; int mmN = -1, sk = -1; };
    struct MmRd { int n = -1; uintptr_t s = 0; float t = 0; };
    auto mm_rd_seh(const uint8_t* node, MmRd& r) -> void
    {
        __try
        {
            r.n = *reinterpret_cast<const int*>(node + 0x68);
            const uint8_t* p = *reinterpret_cast<const uint8_t* const*>(node + 0x60);
            if (r.n > 0 && p) { r.s = *reinterpret_cast<const uintptr_t*>(p + 0x70); r.t = *reinterpret_cast<const float*>(p + 0x44); }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { r.n = -1; }
    }
    struct LegH
    {
        int n = 0, b[6] = {};
        std::vector<double> v;
        void add(double r)
        {
            ++n; v.push_back(r);
            ++b[r < 0.1 ? 0 : r < 0.3 ? 1 : r < 0.6 ? 2 : r < 1.0 ? 3 : r < 2.0 ? 4 : 5];
        }
        double med()
        {
            if (v.empty()) return -1;
            std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
            return v[v.size() / 2];
        }
        std::string text(const char* tag)
        {
            char b2[200];
            snprintf(b2, sizeof b2, "%s janelas=%d razao_pe_corpo med=%.2f faixas <0.1=%d 0.1-0.3=%d 0.3-0.6=%d 0.6-1=%d 1-2=%d >2=%d", tag, n, med(), b[0], b[1], b[2], b[3], b[4], b[5]);
            return b2;
        }
        void clear() { n = 0; std::fill(std::begin(b), std::end(b), 0); v.clear(); }
    };
    int g_hMbH[7] = {};
    int g_lgNoSock = 0, g_lgHum = 0, g_lgHumHost = 0, g_lgHumDiff = 0;
    double g_lgUs = 0, g_lgUsMax = 0;
    int g_lgCalls = 0;
    LegH g_lgHost, g_lgHostM;
    int g_koWhy[6] = {}, g_koKeep = 0, g_lgOff = 0, g_lgOffH = 0;
    int g_eqNow = 0, g_mEqSkip = 0, g_mLostCalm = 0, g_turnOrd = 0, g_turnFail = 0, g_yawHold = 0;
    auto st_flags(UObject* a, uint64_t& f250, uint64_t& f258) -> int
    {
        if (!a) return -1;
        __try
        {
            auto* b = reinterpret_cast<uint8_t*>(a);
            int rel = (b[0x5d] & 0x20) ? 1 : 0;
            uint8_t* p = *reinterpret_cast<uint8_t**>(b + 0x650);
            if (reinterpret_cast<uintptr_t>(p) >= 0x10000) { f250 = *reinterpret_cast<uint64_t*>(p + 0x250); f258 = *reinterpret_cast<uint64_t*>(p + 0x258); }
            return rel;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto pelvis_of(UObject* a, double& x, double& y, double& z) -> bool
    {
        UObject* m = a ? prop_obj(a, STR("Mesh")) : nullptr;
        if (!m) return false;
        {
            Call d(m, STR("DoesSocketExist"));
            d.nm(STR("InSocketName"), STR("jnt_hips"));
            if (!d.run() || !d.rb()) return false;
        }
        Call c(m, STR("GetSocketLocation"));
        c.nm(STR("InSocketName"), STR("jnt_hips"));
        return c.run() && c.rv(x, y, z) && !(x == 0 && y == 0);
    }
    auto legs_seen(UObject* a) -> bool
    {
        Call c(a, STR("WasRecentlyRendered"));
        c.fl(STR("Tolerance"), 0.2f);
        return c.run() && c.rb();
    }
    auto legs_rel(UObject* mesh, const wchar_t* sock, double ax, double ay, double cy, double sy, double& rx, double& ry) -> bool
    {
        Call c(mesh, STR("GetSocketLocation"));
        c.nm(STR("InSocketName"), sock);
        double x, y, z;
        if (!c.run() || !c.rv(x, y, z)) return false;
        double dx = x - ax, dy = y - ay;
        rx = dx * cy + dy * sy; ry = -dx * sy + dy * cy;
        return true;
    }
    const wchar_t* const kLegSk[2][2] = { { STR("jnt_l_foot"), STR("jnt_r_foot") }, { STR("jnt_L_F_leg_05"), STR("jnt_R_F_leg_05") } };
    auto legs_step(LegSt& L, UObject* a, double now, double& ratio, int& off) -> bool
    {
        if (L.t >= 0 && now - L.t < 0.1) return false;
        if (L.ot >= 0 && now - L.ot < 0.1) return false;
        auto t0 = Clock::now();
        if (C.natkinmv && !legs_seen(a)) { ++off; L.t = -1; L.ot = now; return false; }
        L.ot = -1;
        UObject* mesh = prop_obj(a, STR("Mesh"));
        double x, y, z, fx[2], fy[2];
        bool ok = mesh && loc_of(a, x, y, z);
        if (ok && L.sk < 0)
        {
            L.sk = 9;
            for (int i = 0; i < 2 && L.sk == 9; ++i)
            {
                Call c(mesh, STR("DoesSocketExist"));
                c.nm(STR("InSocketName"), kLegSk[i][0]);
                if (c.run() && c.rb()) L.sk = i;
            }
        }
        if (ok && L.sk > 1) { ok = false; ++g_lgNoSock; }
        else if (ok)
        {
            double yr = yaw_of(a) * 0.017453292519943295, cy = std::cos(yr), sy = std::sin(yr);
            ok = legs_rel(mesh, kLegSk[L.sk][0], x, y, cy, sy, fx[0], fy[0]) && legs_rel(mesh, kLegSk[L.sk][1], x, y, cy, sy, fx[1], fy[1]);
            if (!ok) ++g_lgNoSock;
        }
        MmRd m;
        if (ok)
            if (UObject* ai = prop_obj(mesh, STR("AnimScriptInstance")))
            {
                int o = off_of(ai, STR("AnimGraphNode_MotionMatching"));
                if (o >= 0) mm_rd_seh(reinterpret_cast<const uint8_t*>(ai) + o, m);
            }
        double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        g_lgUs += us; g_lgUsMax = std::max(g_lgUsMax, us); ++g_lgCalls;
        if (!ok) { L.t = -1; return false; }
        if (L.t >= 0 && now - L.t < 0.5)
        {
            L.body += std::hypot(x - L.bx, y - L.by);
            L.foot += 0.5 * (std::hypot(fx[0] - L.fx[0], fy[0] - L.fy[0]) + std::hypot(fx[1] - L.fx[1], fy[1] - L.fy[1]));
            if (m.n > 0 && L.mmN > 0) L.mmAdv += m.s != L.mmS ? now - L.t : (m.t >= L.mmT ? m.t - L.mmT : (double)m.t);
        }
        else { L.w0 = now; L.body = L.foot = 0; L.mmAdv = 0; }
        L.t = now; L.bx = x; L.by = y;
        L.mmN = m.n; L.mmS = m.s; L.mmT = m.t;
        for (int k = 0; k < 2; ++k) { L.fx[k] = fx[k]; L.fy[k] = fy[k]; }
        if (now - L.w0 < 1.0) return false;
        bool full = L.body >= 50;
        ratio = full ? L.foot / L.body : -1;
        L.mmRate = m.n > 0 ? L.mmAdv / (now - L.w0) : -1;
        L.w0 = now; L.body = L.foot = 0; L.mmAdv = 0;
        return full;
    }

    struct HSel
    {
        WRef r;
        int32_t uid = 0, pid = 0;
        std::string nm;
        double hh = 88;
        double px = 0, py = 0, pt = -1, vx = 0, vy = 0;
        bool dead = false, fd = false;
        double deadAt = 0;
        std::string look;
        uint32_t lk = 0;
        double lookAt = -100;
        UObject* wMesh = nullptr; UObject* wCls = nullptr;
        int lookSent = 0;
        int lookSentV[s2nmc::kViews] = {};
        int own = -1;
        double d2 = 0;
        bool vend = false, fixed = false, hid = false;
        struct HMon { UObject* m = nullptr; int seq = 0; float pos = 0; std::string path; double at = -100; float rate = 1.f; std::string nx; UObject* om = nullptr; float opos = 0; std::string opath; float orate = 1.f; std::string onx; } mon[2];
        int hitN = 0, hitPart = -1; float hitDir = 0, hitV = 0;
        struct HShot { int seq = 0; int64_t ms = 0; int sx = 0, sy = 0, sz = 0, f = 0, n = 0; int d[36] = {}; };
        std::vector<HShot> shots; int shSeq = 0;
        int ci = -1; bool mut = false; std::string sid;
        std::string qg;
        float og0 = -1; int ov0 = -1; WRef ovMesh;
        LegSt lg;
        double prAt = -100; int prN = 0;
    };
    struct HostSt
    {
        std::unordered_map<UObject*, HSel> sel;
        std::vector<UObject*> order;
        double listAt = -100, lookRR = 0;
        size_t rr = 0, mrr = 0;
        AddrRef me, pup, tgt;
        double at = 0;
        int snaps = 0, sent = 0, looks = 0, nall = 0;
        size_t bytes = 0;
        double ms = 0, msmax = 0;
        std::unordered_set<UObject*> vend;
        int vendRows = 0, nFixed = 0, nRand = 0, nVend = 0;
        int combat = 0, monts = 0, montDyn = 0, hits = 0, mRe = 0; size_t mBytes = 0;
        int nMut = 0, nMutSent = 0, offOn = 0, offBack = 0;
        std::string nu;
        bool nuDue = false;
        int nNu = 0, nuSent = 0;
        bool mc = false;
        s2nmc::ViewSt vs[s2nmc::kViews];
        std::vector<UObject*> ordV[s2nmc::kViews];
        std::unordered_set<uintptr_t> perPrev[s2nmc::kViews];
        std::string nuV[s2nmc::kViews];
        bool nuDueV[s2nmc::kViews] = {};
        std::string outV[s2nmc::kViews];
        AddrRef plPup[s2nmc::kViews], plTgt[s2nmc::kViews];
        int nPl = 0, nPlMax = 0, nUnion = 0, mcSnaps = 0;
        int mcN[s2nmc::kViews] = {};
        size_t mcBytes[s2nmc::kViews] = {}, mcMax[s2nmc::kViews] = {};
        struct QgC { int32_t uid = 0; std::string qg; double at = 0; };
        std::unordered_map<UObject*, QgC> qgc;
        s2nmc::FreshDead fd;
        double fdX = 0, fdY = 0;
        bool fdHas = false;
        double qgcAt = 0;
        int nQgRows = 0, nQgNu = 0;
        double nr = 0, rR = 0, rK = 0, rLog = -1;
        int rSrc = 0, rSrcLog = -1;
        double lastGoodR = 0;
    } H;
    auto host_r() -> double
    {
        float r = 0, k = 0;
        if (!C.natrhost) { H.rSrc = 0; H.nr = C.natr; }
        else if (!cgcmc::online_radius(r, k)) { H.rSrc = 2; H.nr = s2nmc::radius_pick(false, 0, 0, C.natr, H.lastGoodR); }
        else { H.rSrc = 1; H.rR = r; H.rK = k; H.nr = s2nmc::radius_pick(true, r, k, C.natr, H.lastGoodR); }
        if (H.rSrc != H.rSrcLog || std::fabs(H.nr - H.rLog) > 1)
        {
            H.rSrcLog = H.rSrc; H.rLog = H.nr;
            char b[300];
            if (H.rSrc == 1) snprintf(b, sizeof b, "NOLA natrhost (host): raio da lista = %.0f cm = raio online do A-Life %.0f cm (exe+0xb10e258) x fator do teste offline %.3f (exe+0x9ede734), centros = host + clientes", H.nr, H.rR, H.rK);
            else snprintf(b, sizeof b, "NOLA natrhost (host): raio da lista = natr %.0f cm (%s), centros = clientes", H.nr, H.rSrc == 0 ? "natrhost=0" : "leitura do raio do A-Life falhou");
            logl(b);
        }
        return H.nr;
    }
    auto host_centre(UObject* me, s2nmc::Pt& p) -> bool
    {
        double z = 0;
        return C.natrhost && me && loc_of(me, p.x, p.y, z);
    }
    auto host_qg(UObject* o, int32_t uid, double now) -> std::string
    {
        if (!C.natqg || !o) return std::string();
        auto& c = H.qgc[o];
        if (c.at <= 0 || c.uid != uid) { c.uid = uid; c.qg = qg_of(o); }
        c.at = now;
        if (now - H.qgcAt >= 30)
        {
            H.qgcAt = now;
            for (auto it = H.qgc.begin(); it != H.qgc.end();) it = now - it->second.at > 30 ? H.qgc.erase(it) : std::next(it);
        }
        return c.qg;
    }
    struct HPl { int view = 0; int pid = 0; double x = 0, y = 0; UObject* pup = nullptr; UObject* tgt = nullptr; };
    int g_lsNone = 0;

    auto host_offa(HSel& s, UObject* o, bool on) -> void
    {
        if (!o) return;
        auto* b = reinterpret_cast<uint8_t*>(o);
        int g = off_of(o, STR("GuaranteedRelevancyUpdateTime"));
        UObject* m = s.ovMesh.get();
        if (!m) { m = prop_obj(o, STR("Mesh")); s.ovMesh.set(m); }
        int v = m ? off_of(m, STR("VisibilityBasedAnimTickOption")) : -1;
        auto* mb = reinterpret_cast<uint8_t*>(m);
        if (on && g >= 0)
        {
            float cur = rd_f_seh(b + g);
            if (s.og0 < 0) { s.og0 = cur; ++H.offOn; }
            if (cur != 0) wr_f_seh(b + g, 0.f);
        }
        if (on && C.offvb && v >= 0)
        {
            int cur = rd_u8_seh(mb + v);
            if (cur >= 0 && s.ov0 < 0) s.ov0 = cur;
            if (cur > 0) wr_u8_seh(mb + v, 0);
        }
        if ((!on || !C.offvb) && s.ov0 >= 0)
        {
            if (v >= 0) wr_u8_seh(mb + v, s.ov0);
            s.ov0 = -1;
        }
        if (!on && s.og0 >= 0)
        {
            if (g >= 0) wr_f_seh(b + g, s.og0);
            s.og0 = -1; ++H.offBack;
        }
    }

    auto host_name_no(UObject* a) -> std::string
    {
        std::string n = w2u(a->GetName());
        size_t u = n.rfind('_');
        if (u == std::string::npos || u + 1 >= n.size()) return "0";
        std::string t = n.substr(u + 1);
        return std::all_of(t.begin(), t.end(), [](char ch) { return ch >= '0' && ch <= '9'; }) ? t : "0";
    }

    auto host_keep(UObject* o, int ci, bool alive, double d2, double now, std::unordered_map<UObject*, HSel>& next) -> bool
    {
        auto it = H.sel.find(o);
        HSel s;
        if (it != H.sel.end() && it->second.r.get() == o) s = std::move(it->second);
        else
        {
            s.r.set(o); s.uid = guid_of(o); s.pid = pid_of(o); s.nm = host_name_no(o); s.hh = half_height(o);
            s.ci = ci; s.mut = cls_mut(ci); s.sid = w2u(cls_sid(ci));
        }
        if (!alive && !s.dead) { s.dead = true; s.deadAt = now; }
        if (s2nmc::host_row_drop(C.natfar != 0, s.dead, now - s.deadAt, C.natdead)) return false;
        s.d2 = d2;
        s.vend = H.vend.count(o) > 0;
        s.qg = host_qg(o, s.uid, now);
        host_offa(s, o, C.offanim && alive && !s.dead && !s.mut);
        next.emplace(o, std::move(s));
        return true;
    }

    auto fd_begin(double now, UObject* me) -> void
    {
        double x = 0, y = 0, z = 0;
        bool hm = me && loc_of(me, x, y, z);
        double m2 = hm && H.fdHas ? (x - H.fdX) * (x - H.fdX) + (y - H.fdY) * (y - H.fdY) : 1e18;
        H.fd.all = C.natdeadall != 0;
        H.fd.begin(now, m2);
        H.fdHas = hm; H.fdX = x; H.fdY = y;
    }

    auto fd_dead_ok(UObject* o, double now, bool inR = true) -> bool
    {
        if (!C.natfd) return false;
        auto k = reinterpret_cast<uintptr_t>(o);
        bool quest = false;
        if (!H.fd.alive.count(k))
        {
            int32_t u = guid_of(o);
            quest = !host_qg(o, u, now).empty();
        }
        if (!H.fd.dead_ok(k, now, quest, inR)) return false;
        logl("NOLA natfd: host NPC " + w2u(o->GetName()) + " uid=" + std::to_string(guid_of(o)) + " died before it was listed: listed dead so every client gets one copy at the host corpse (vivo_visto=" + std::to_string(H.fd.nAlive) + " novo_morto=" + std::to_string(H.fd.nNew) + ")");
        return true;
    }

    auto host_rebuild(double rx, double ry, double now, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        std::vector<UObject*> all;
        cls_all(all);
        H.nall = (int)all.size();
        fd_begin(now, me);
        struct Cand { UObject* o; double d2; bool alive; double rk; int ci; bool fd; };
        std::vector<Cand> cs;
        const double NR = host_r();
        s2nmc::Pt hc;
        const bool hcOn = host_centre(me, hc);
        std::vector<s2nmc::Pt> vc{s2nmc::Pt{rx, ry}};
        if (hcOn) vc.push_back(hc);
        const double R2 = NR * NR;
        const double hy = std::max(1.0, C.nathys), Rh = NR * hy, Rh2 = Rh * Rh;
        struct Near { UObject* o; double d2; };
        std::vector<Near> nearL;
        std::vector<VPos> liveNear;
        for (UObject* o : all)
        {
            if (!o || o == me || o == pup || o == tgt) continue;
            double x, y, z;
            if (!loc_of(o, x, y, z) || pooled(x, y)) continue;
            double d2 = s2nmc::near2(vc, x, y);
            if (d2 > (Rh + C.natvendr) * (Rh + C.natvendr)) { H.fd.seen(reinterpret_cast<uintptr_t>(o), false, now); continue; }
            bool alN = alive_of(o);
            H.fd.seen(reinterpret_cast<uintptr_t>(o), alN && d2 <= Rh2, now);
            if (C.natvend && alN) liveNear.push_back({o, x, y});
            if (d2 <= (H.sel.count(o) ? Rh2 : R2)) nearL.push_back({o, d2});
        }
        H.nu.clear(); H.nNu = 0; H.nuDue = C.natpark != 0;
        if (C.natpark)
        {
            double mx = 0, my = 0, mz = 0;
            bool hm = me && loc_of(me, mx, my, mz);
            const double P2 = C.natparkr * C.natparkr;
            for (UObject* o : all)
            {
                if (!o || o == me || o == pup || o == tgt || !alive_of(o)) continue;
                double x, y, z;
                if (!loc_of(o, x, y, z) || pooled(x, y)) continue;
                double dc = (x - rx) * (x - rx) + (y - ry) * (y - ry);
                double dh = hm ? (x - mx) * (x - mx) + (y - my) * (y - my) : 1e18;
                if (std::min(dc, dh) > P2) continue;
                int32_t u = guid_of(o);
                if (u <= 0) continue;
                std::string g = host_qg(o, u, now);
                if (!g.empty()) ++H.nQgNu;
                s2nmc::nu_add(H.nu, u, g);
                ++H.nNu;
            }
        }
        H.vendRows = C.natvend ? vend_pick(liveNear, vc, NR, H.vend) : 0;
        if (!C.natvend) H.vend.clear();
        for (const Near& nr : nearL)
        {
            UObject* o = nr.o;
            double d2 = nr.d2;
            bool isV = H.vend.count(o) > 0;
            int ci = cls_of(o);
            float mh = c_float(o, STR("GetMaxHP"), 0.f);
            if (mh <= 0 || (mh >= cls_cut(ci) && !isV)) continue;
            bool al = alive_of(o);
            auto it = H.sel.find(o);
            bool listed = it != H.sel.end() && it->second.r.get();
            bool fdr = C.natfd && listed && it->second.fd;
            if (!al && !listed) { if (!fd_dead_ok(o, now)) continue; fdr = true; }
            cs.push_back({o, d2, al, it != H.sel.end() ? d2 / (hy * hy) : d2, ci, fdr});
        }
        H.fd.end(now);
        std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.rk < b.rk; });
        std::unordered_map<UObject*, HSel> next;
        std::vector<UObject*> ord;
        int capped = 0;
        for (const auto& c : cs)
        {
            if (!c.fd && capped >= C.natmax) continue;
            if (!host_keep(c.o, c.ci, c.alive, c.d2, now, next)) continue;
            ord.push_back(c.o);
            if (c.fd) next[c.o].fd = true; else ++capped;
        }
        for (auto& [o, s] : H.sel) if (!next.count(o)) { H.fd.left(reinterpret_cast<uintptr_t>(o), s2nmc::host_row_drop(C.natfar != 0, s.dead, now - s.deadAt, C.natdead)); host_offa(s, s.r.get(), false); }
        H.sel = std::move(next);
        H.order = std::move(ord);
        H.nMut = 0;
        for (auto& [o, s] : H.sel) H.nMut += s.mut;
    }

    auto host_rebuild_mc(const std::vector<HPl>& pl, double now, UObject* me) -> void
    {
        std::vector<UObject*> all;
        cls_all(all);
        H.nall = (int)all.size();
        fd_begin(now, me);
        std::unordered_set<UObject*> ex{me};
        for (const HPl& p : pl) { ex.insert(p.pup); ex.insert(p.tgt); }
        ex.erase(nullptr);
        std::vector<s2nmc::Pt> cs;
        for (const HPl& p : pl) cs.push_back({p.x, p.y});
        const double NR = host_r();
        s2nmc::Pt hc;
        const bool hcOn = host_centre(me, hc);
        std::vector<s2nmc::Pt> csA = cs;
        if (hcOn) csA.push_back(hc);
        const double hy = std::max(1.0, C.nathys), Rh = NR * hy, Rh2 = Rh * Rh, VR2 = (Rh + C.natvendr) * (Rh + C.natvendr);
        const double P2 = C.natparkr * C.natparkr, R2 = NR * NR;
        double mx = 0, my = 0, mz = 0;
        bool hm = C.natpark && me && loc_of(me, mx, my, mz);
        struct Near { UObject* o; double x, y; };
        std::vector<Near> nearL;
        std::vector<VPos> liveNear;
        H.nu.clear(); H.nNu = 0; H.nuDue = false;
        for (int v = 0; v < s2nmc::kViews; ++v) { H.nuV[v].clear(); H.nuDueV[v] = false; }
        for (UObject* o : all)
        {
            if (!o || ex.count(o)) continue;
            double x, y, z;
            if (!loc_of(o, x, y, z) || pooled(x, y)) continue;
            double d2 = s2nmc::near2(csA, x, y);
            bool parkTest = C.natpark != 0;
            if (d2 > VR2 && !parkTest) { H.fd.seen(reinterpret_cast<uintptr_t>(o), false, now); continue; }
            bool al = alive_of(o);
            H.fd.seen(reinterpret_cast<uintptr_t>(o), al && d2 <= Rh2, now);
            if (parkTest && al)
            {
                double dh = hm ? (x - mx) * (x - mx) + (y - my) * (y - my) : 1e18;
                int32_t u = 0;
                std::string g;
                bool any = false;
                for (const HPl& p : pl)
                {
                    double dc = (x - p.x) * (x - p.x) + (y - p.y) * (y - p.y);
                    if (std::min(dc, dh) > P2) continue;
                    if (!any) { u = guid_of(o); any = true; if (u > 0) g = host_qg(o, u, now); }
                    if (u <= 0) break;
                    s2nmc::nu_add(H.nuV[p.view], u, g);
                }
                if (any && u > 0) { ++H.nNu; if (!g.empty()) ++H.nQgNu; }
            }
            if (d2 > VR2) continue;
            if (C.natvend && al) liveNear.push_back({o, x, y});
            if (d2 <= Rh2) nearL.push_back({o, x, y});
        }
        H.vendRows = C.natvend ? vend_pick(liveNear, csA, NR, H.vend) : 0;
        if (!C.natvend) H.vend.clear();
        std::vector<s2nmc::Item> items;
        std::unordered_map<uintptr_t, std::pair<int, bool>> meta;
        std::unordered_set<uintptr_t> fdk;
        for (const Near& nr : nearL)
        {
            UObject* o = nr.o;
            bool isV = H.vend.count(o) > 0;
            int ci = cls_of(o);
            float mh = c_float(o, STR("GetMaxHP"), 0.f);
            if (mh <= 0 || (mh >= cls_cut(ci) && !isV)) continue;
            bool al = alive_of(o);
            auto it = H.sel.find(o);
            bool listed = it != H.sel.end() && it->second.r.get();
            bool fdr = C.natfd && listed && it->second.fd;
            if (!al && !listed) { if (!fd_dead_ok(o, now, s2nmc::near2(csA, nr.x, nr.y) <= R2)) continue; fdr = true; }
            if (fdr) fdk.insert(reinterpret_cast<uintptr_t>(o));
            items.push_back({reinterpret_cast<uintptr_t>(o), nr.x, nr.y});
            meta[reinterpret_cast<uintptr_t>(o)] = {ci, al};
        }
        H.fd.end(now);
        std::vector<std::unordered_set<uintptr_t>> prev;
        for (const HPl& p : pl) prev.push_back(H.perPrev[p.view]);
        auto drop = [&](uintptr_t k) {
            auto it = H.sel.find(reinterpret_cast<UObject*>(k));
            return it != H.sel.end() && it->second.r.get() == reinterpret_cast<UObject*>(k) && s2nmc::host_row_drop(C.natfar != 0, it->second.dead, now - it->second.deadAt, C.natdead);
        };
        s2nmc::Pick pk = s2nmc::pick(cs, items, NR, C.nathys, C.natmax, prev, drop, &fdk, hcOn ? &hc : nullptr);
        std::unordered_map<UObject*, HSel> next;
        std::vector<UObject*> ord;
        for (size_t i = 0; i < pk.all.size(); ++i)
        {
            UObject* o = reinterpret_cast<UObject*>(pk.all[i]);
            const auto& m = meta[pk.all[i]];
            if (!host_keep(o, m.first, m.second, pk.d2[i], now, next)) continue;
            if (fdk.count(pk.all[i])) next[o].fd = true;
            int at = pk.own[i];
            next[o].own = at >= 0 && at < (int)pl.size() ? pl[(size_t)at].view : -1;
            ord.push_back(o);
        }
        for (auto& [o, s] : H.sel) if (!next.count(o)) { H.fd.left(reinterpret_cast<uintptr_t>(o), s2nmc::host_row_drop(C.natfar != 0, s.dead, now - s.deadAt, C.natdead)); host_offa(s, s.r.get(), false); }
        H.sel = std::move(next);
        H.order = std::move(ord);
        for (int v = 0; v < s2nmc::kViews; ++v) { H.ordV[v].clear(); H.perPrev[v].clear(); }
        for (size_t c = 0; c < pl.size(); ++c)
        {
            int v = pl[c].view;
            for (uintptr_t k : pk.per[c])
                if (H.sel.count(reinterpret_cast<UObject*>(k))) { H.ordV[v].push_back(reinterpret_cast<UObject*>(k)); H.perPrev[v].insert(k); }
            H.nuDueV[v] = C.natpark != 0;
        }
        H.nUnion = (int)H.order.size();
        H.nMut = 0;
        for (auto& [o, s] : H.sel) H.nMut += s.mut;
    }

    int g_moHost = 0, g_moPlay = 0, g_moSync = 0, g_moStop = 0, g_moAll = 0, g_mSync = 0;
    auto host_mon_nx(UObject* ai, UObject* m, float& rate, std::string& nx) -> void
    {
        int32_t v[32];
        int n = 0;
        if (uint8_t* in = mont_inst(ai, m)) n = mont_nx_raw(in, rate, v, 32);
        for (int i = 0; i < n; ++i) { if (i) nx += '.'; nx += std::to_string(v[i]); }
    }
    auto host_mon(HSel::HMon& hm, UObject* ai, double now, bool ovl = false) -> int
    {
        UObject* m = mont_cur(ai);
        UObject* om = nullptr;
        if (ovl && C.natmovl && m)
        {
            UObject* act[8];
            int na = mont_active(ai, act, 8);
            for (int i = 0; i < na; ++i) if (act[i] != m) { om = act[i]; break; }
        }
        if (!m) { hm.m = nullptr; hm.om = nullptr; hm.opath.clear(); return 0; }
        float pos = mont_pos(ai, m), opos = om ? mont_pos(ai, om) : 0.f;
        float rate = 1.f, orate = 1.f;
        std::string nx, onx;
        bool sec = false;
        if (C.natmsec)
        {
            host_mon_nx(ai, m, rate, nx);
            sec = m == hm.m && (std::fabs(rate - hm.rate) > 0.01f || nx != hm.nx);
            if (om)
            {
                host_mon_nx(ai, om, orate, onx);
                if (om == hm.om && (std::fabs(orate - hm.orate) > 0.01f || onx != hm.onx)) sec = true;
            }
        }
        if (om != hm.om || (om && opos + 0.05f < hm.opos)) sec = true;
        if (m != hm.m || pos + 0.05f < hm.pos || sec)
        {
            if (++hm.seq > 999999) hm.seq = 1;
            std::string p = w2u(m->GetPathName());
            hm.path = p.rfind("/Game/", 0) == 0 ? pack_path(p) : std::string();
            hm.at = now; ++H.monts; if (hm.path.empty()) ++H.montDyn;
            hm.opath.clear();
            if (om)
            {
                std::string q = w2u(om->GetPathName());
                if (q.rfind("/Game/", 0) == 0) { hm.opath = pack_path(q); ++g_moHost; }
            }
        }
        hm.m = m; hm.pos = pos; hm.om = om; hm.opos = opos;
        if (C.natmsec) { hm.rate = rate; hm.nx = nx; hm.orate = orate; hm.onx = onx; }
        return hm.seq;
    }

    const char* g_bState = "nao instalado";
    uint8_t* g_bE87 = nullptr; uint8_t* g_bTime = nullptr;
    int g_bhShots = 0, g_bhSel = 0, g_bhOff = 0, g_bhRead = 0, g_bhSent = 0;
    int g_bRx = 0, g_bNew = 0, g_bLost = 0, g_bNoEnt = 0, g_bFire = 0, g_bNoCopy = 0, g_bNoComp = 0, g_bStale = 0, g_bExc = 0, g_bOffT = 0;
    volatile LONG g_bProj = 0, g_bImp = 0, g_bBlk = 0, g_bPt = 0;
    double g_bAgeSum = 0, g_bAgeMax = 0, g_bUs = 0, g_bUsMax = 0; int g_bFr = 0;
    auto host_bullets(HSel& s, int64_t wnow, std::string& bb) -> void;
    auto dmg_host_str(int64_t wnow) -> std::string;
    auto dmg_update(UObject* tgt, UObject* pup) -> void;
    int g_hMb[7] = {};
    auto weak_get_seh(const uint8_t* w) -> UObject*;
    auto inv_comp(UObject* a) -> uint8_t*
    {
        int o = a ? off_of(a, STR("InvisibilityComponent")) : -1;
        return o >= 0 ? reinterpret_cast<uint8_t*>(weak_get_seh(reinterpret_cast<const uint8_t*>(a) + o)) : nullptr;
    }
    int g_hmMon = 0, g_hmSent = 0, g_hmDyn = 0, g_hmInv[3] = {}, g_hmLog = 0, g_hmCur = 0, g_hmRe = 0;
    auto host_mab(HSel& s, UObject* o, double now, uint32_t& fl, int& bs, std::string& mm, std::string& mre) -> void
    {
        int inv = -1;
        if (C.natmab & 2)
        {
            bool has = s.ci >= 0 && s.ci < (int)g_cls.size() ? g_cls[s.ci].invis : off_of(o, STR("InvisibilityComponent")) >= 0;
            if (has)
            {
                uint8_t* ic = inv_comp(o);
                float t = ic ? rd_f_seh(ic + 0xac) : -1.f;
                if (ic && s2nmab::inv_valid(t)) inv = s2nmab::inv_state(t);
                ++g_hmInv[inv + 1];
                if (inv >= 0) { uint32_t cb = s2nmab::cur_bits(C.natcloak, rd_f_seh(ic + 0xa8)); fl |= cb; if (cb) ++g_hmCur; }
            }
        }
        fl |= s2nmab::host_bits(C.natmab, true, true, inv);
        if (!(C.natmab & 1)) return;
        HSel::HMon& hm = s.mon[0];
        int sq0 = hm.seq;
        bs = host_mon(hm, anim_of(prop_obj(o, STR("Mesh"))), now);
        if (bs != 0 && bs != sq0)
        {
            ++g_hmMon; if (hm.path.empty()) ++g_hmDyn;
            if (C.natlog && g_hmLog < 40)
            {
                ++g_hmLog;
                logl("NOLA ma1 natmab host: mutante uid=" + std::to_string(s.uid) + " sid=" + s.sid + " montagem seq=" + std::to_string(bs) + " " +
                     (hm.path.empty() ? std::string("(dinamica, nao enviada)") : hm.path));
            }
        }
        if (bs == 0 || hm.path.empty()) return;
        bool fresh = now - hm.at <= C.natmwin;
        if (!fresh && !C.natmre) return;
        std::string& dst = fresh ? mm : mre;
        char mb[48];
        snprintf(mb, sizeof mb, "m%d,b,%d,%d,", s.uid, hm.seq, (int)std::lround(hm.pos * 1000));
        if (!dst.empty()) dst += ";";
        dst += mb + hm.path;
        if (fresh) ++g_hmSent; else ++g_hmRe;
    }
    int g_cpOn = 0, g_cpHp = 0;
    constexpr uint32_t kHostHid = 1u << 23;
    constexpr int kCbProp = 1 << 21, kCbPropOk = 1 << 22;
    int g_prHostLog = 0, g_prLog = 0, g_prFail = 0, g_prCut = 0, g_prCutFail = 0, g_prEv = 0;
    auto props_raw(UObject* a, UObject** out, int cap) -> int
    {
        int n = 0;
        __try
        {
            auto* ab = reinterpret_cast<const uint8_t*>(a);
            const uint8_t* unit = *reinterpret_cast<const uint8_t* const*>(ab + 0x650);
            if (!unit) return 0;
            const uint8_t* mgr = *reinterpret_cast<const uint8_t* const*>(unit + 0x678);
            if (!mgr) return 0;
            const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(mgr + 0x160);
            int num = *reinterpret_cast<const int*>(mgr + 0x170), fr = *reinterpret_cast<const int*>(mgr + 0x19c);
            if (!data || num <= 0 || num - fr <= 0) return 0;
            if (num > 32) num = 32;
            const uint32_t* bits = *reinterpret_cast<const uint32_t* const*>(mgr + 0x188);
            if (!bits) bits = reinterpret_cast<const uint32_t*>(mgr + 0x178);
            for (int i = 0; i < num && n < cap; ++i)
            {
                if (!(bits[i >> 5] & (1u << (i & 31)))) continue;
                if (UObject* x = weak_get_seh(data + (size_t)i * 0x58 + 8); x && x != a) out[n++] = x;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
        return n;
    }
    auto props_of(UObject* a, std::vector<UObject*>& out) -> int
    {
        out.clear();
        if (!a) return 0;
        UObject* raw[32];
        int n = props_raw(a, raw, 32);
        if (n < 0) { ++g_prFail; return 0; }
        for (int i = 0; i < n; ++i)
        {
            UObject* x = raw[i];
            if (std::find(out.begin(), out.end(), x) != out.end()) continue;
            if (c_obj(x, STR("GetOwner")) == a) out.push_back(x);
        }
        return (int)out.size();
    }
    int g_visHostN = 0, g_visN = 0;
    auto host_chunk(HSel& s, double now, UObject* me, const std::vector<HPl>* pl, UObject* pup, UObject* tgt, s2nmc::Chunk& c) -> void
    {
        c.uid = s.uid; c.fixed = s.fixed; c.look = &s.look;
        std::string& mm = c.mm;
        std::string& bb = c.bb;
        {
            UObject* o = s.r.get();
            if (!o) return;
            double x, y, z;
            if (!loc_of(o, x, y, z)) { c.noData = true; return; }
            if (pooled(x, y)) return;
            double yaw = yaw_of(o);
            double vx = 0, vy = 0, vz = 0;
            c_vec(o, STR("GetVelocity"), vx, vy, vz);
            if (s.pt >= 0 && now - s.pt >= 0.03 && now - s.pt <= 1.0 && vx * vx + vy * vy < 25)
            { vx = (x - s.px) / (now - s.pt); vy = (y - s.py) / (now - s.pt); }
            s.px = x; s.py = y; s.pt = now;
            bool al = !s.dead && alive_of(o);
            if (!al && !s.dead) { s.dead = true; s.deadAt = now; }
            uint32_t fl = 0;
            bool hd = prop_bool(o, STR("bHidden"), false);
            if (hd) fl |= kHostHid;
            if (hd != s.hid)
            {
                s.hid = hd;
                ++g_visHostN;
                if (C.natlog && (g_visHostN <= 40 || g_visHostN % 50 == 0))
                    logl("NOLA npcvis host: uid=" + std::to_string(s.uid) + " (_" + s.nm + ") bHidden=" + (hd ? "1" : "0") + " on the host, sent to clients #" + std::to_string(g_visHostN));
            }
            UObject* foc = nullptr;
            if (al && s.mut)
            {
                fl |= 1;
                if (std::sqrt(vx * vx + vy * vy) > C.natrun) fl |= 128;
                if (C.natgait)
                {
                    int mb = c_u8(o, STR("GetMovementBehaviour"), -1);
                    ++g_hMb[std::clamp(mb + 1, 0, 6)];
                    if (mb >= 0 && mb <= 5) fl |= (1u << 19) | ((uint32_t)mb << 16);
                }
            }
            else if (al)
            {
                fl |= 1;
                if (c_bool(o, STR("IsCrouching"), false)) fl |= 2;
                if (c_bool(o, STR("IsAiming"), false)) fl |= 4;
                if (c_bool(o, STR("IsShooting"), false)) fl |= 8;
                if (c_bool(o, STR("IsInThreat"), false)) fl |= 16;
                foc = focus_of(o);
                if (UObject* f = foc)
                {
                    if (f == me) fl |= 32;
                    else if (!pl) { if (f == pup || f == tgt) fl |= 64; }
                    else for (const HPl& p : *pl) if (f == p.pup || f == p.tgt) { fl |= 64u | ((uint32_t)(p.pid & 7) << 11); break; }
                }
                if (std::sqrt(vx * vx + vy * vy) > C.natrun) fl |= 128;
                if (C.natmbh)
                {
                    int mb = c_u8(o, STR("GetMovementBehaviour"), -1);
                    if (vx * vx + vy * vy > 2500) ++g_hMbH[std::clamp(mb + 1, 0, 6)];
                    if (mb >= 0 && mb <= 5) fl |= (1u << 19) | ((uint32_t)mb << 16);
                }
            }
            else { vx = vy = 0; }
            if (C.natlegs && al && vx * vx + vy * vy > 2500) { double lr = 0; if (legs_step(s.lg, o, now, lr, g_lgOffH)) { g_lgHost.add(lr); if (s.lg.sk == 1) g_lgHostM.add(lr); } }
            else s.lg.t = -1;
            float hp = c_float(o, STR("GetHP"), 0.f), mh = c_float(o, STR("GetMaxHP"), 1.f);
            int hpp = s2nmc::hp_wire(hp, mh, g_cpHp == 1);
            if (s.mut) { fl |= 1024; ++H.nMutSent; }
            bool wchg = false;
            if (!s.mut)
            {
                UObject* w = c_obj(o, STR("GetWeaponInHandsMeshComponent"));
                UObject* wm = w ? c_obj(w, STR("GetSkeletalMeshAsset")) : nullptr;
                UObject* wc = w ? prop_obj(w, STR("AnimClass")) : nullptr;
                if (wm != s.wMesh || wc != s.wCls) { if (!s.look.empty()) { wchg = true; ++g_wpReread; } s.wMesh = wm; s.wCls = wc; }
            }
            bool mutLook = s.mut && C.natmutlook && C.natlook && !(s.ci >= 0 && s.ci < (int)g_cls.size() ? g_cls[s.ci].invis : off_of(o, STR("InvisibilityComponent")) >= 0);
            if (s.mut && !mutLook && !s.look.empty()) { s.look.clear(); s.lk = 0; }
            if ((!s.mut || mutLook) && (wchg || now - s.lookAt >= 10 || s.look.empty()))
            {
                std::vector<Part> ps;
                parts_of(o, ps);
                std::string l = look_str(ps);
                if (s.mut) ++g_mutLookH;
                std::string g = s.mut ? std::string() : gen_str(o);
                if (!g.empty()) { l += (l.empty() ? "" : "|") + std::string("MeshGen:0@") + g + "@@1"; ++g_genSent; }
                uint32_t h = fnv(l);
                if (h != s.lk) { s.look = l; s.lk = h; s.lookSent = 0; std::fill(std::begin(s.lookSentV), std::end(s.lookSentV), 0); }
                s.lookAt = now;
            }
            s.fixed = C.natfixed && (s.vend || s.look.find("^_uni/") != std::string::npos);
            if (s.vend) { fl |= 512; ++H.nVend; }
            if (s.fixed) { fl |= 256; ++H.nFixed; } else ++H.nRand;
            int eq = -1, ls = 0, cb = 0, bs = 0, ws = 0, fx = 0, fy = 0, fz = 0;
            if (C.natmab && al && s.mut) host_mab(s, o, now, fl, bs, mm, c.mre);
            if (C.natcb && al && !s.mut)
            {
                eq = c_u8(o, STR("GetMainHandEquipType"), -1);
                UObject* ai = anim_of(prop_obj(o, STR("Mesh")));
                AnimSt as;
                anim_state(ai, as);
                ls = as.ls < 0 ? 0 : as.ls;
                if (as.inCombat > 0) cb |= 1;
                if (as.hasW > 0) cb |= 2;
                if (ai)
                {
                    auto* ab = reinterpret_cast<const uint8_t*>(ai);
                    int sd = off_of(ai, STR("StateData")), la = off_of(ai, STR("LookAtData"));
                    if (la >= 0 && rd_u8_seh(ab + la + 0x13C) > 0) cb |= 1 << 16;
                    if (sd >= 0 && rd_u8_seh(ab + sd + 0x36) > 0) cb |= 1 << 17;
                    cb |= 1 << 18;
                    if (la >= 0)
                    {
                        int lset = rd_u8_seh(ab + la + 0xa8), lty = rd_u8_seh(ab + la + 0x40);
                        if (lset >= 0 && lty >= 0) { cb |= 1 << 20; if (lset > 0 && lty != 3) cb |= 1 << 19; }
                    }
                }
                if (c_bool(o, STR("IsReloadAnimationActive"), false)) cb |= 4;
                if (c_bool(o, STR("IsChangingWeapon"), false)) cb |= 8;
                if (c_u8(o, STR("GetWoundedState_BP"), 0) > 0) cb |= 16;
                if (c_u8(o, STR("GetKnockedDownState"), 0) > 0) cb |= 32;
                if (as.hitPart >= 0)
                {
                    if (s.hitPart >= 0 && (as.hitPart != s.hitPart || std::fabs(as.hitDir - s.hitDir) > 0.5f || std::fabs(as.hitV - s.hitV) > 0.5f)) { ++s.hitN; ++H.hits; }
                    s.hitPart = as.hitPart; s.hitDir = as.hitDir; s.hitV = as.hitV;
                }
                cb |= (s.hitN & 255) << 8;
                if (cb & 1) ++H.combat;
                bs = host_mon(s.mon[0], ai, now, true);
                ws = host_mon(s.mon[1], anim_of(c_obj(o, STR("GetWeaponInHandsMeshComponent"))), now);
                if (foc && foc != me && (pl || (foc != pup && foc != tgt)))
                {
                    double qx, qy, qz;
                    if (loc_of(foc, qx, qy, qz)) { fx = (int)std::lround(qx); fy = (int)std::lround(qy); fz = (int)std::lround(qz); }
                }
                if (uint8_t* S = ai_s_of(o))
                {
                    char vb[420];
                    if (cov_hex(S, s.uid, vb, sizeof vb) > 0) { if (!mm.empty()) mm += ";"; mm += vb; ++g_covSent; }
                }
                wpn_host(o, s.uid, now, mm);
                for (int k = 0; k < 2; ++k)
                {
                    const HSel::HMon& hm = s.mon[k];
                    if ((k ? ws : bs) == 0 || hm.path.empty()) continue;
                    bool fresh = now - hm.at <= C.natmwin;
                    if (!fresh && !C.natmre) continue;
                    std::string& dst = fresh ? mm : c.mre;
                    char mb[48];
                    snprintf(mb, sizeof mb, "m%d,%c,%d,%d,", s.uid, k ? 'w' : 'b', hm.seq, (int)std::lround(hm.pos * 1000));
                    if (!dst.empty()) dst += ";";
                    dst += mb;
                    if (C.natmsec)
                    {
                        char rb[16];
                        snprintf(rb, sizeof rb, "r%d,", (int)std::lround(hm.rate * 1000));
                        dst += rb;
                        if (!hm.nx.empty()) { dst += 'x'; dst += hm.nx; dst += ','; }
                    }
                    dst += hm.path;
                    if (k == 0 && !hm.opath.empty())
                    {
                        snprintf(mb, sizeof mb, ";m%d,o,%d,%d,", s.uid, hm.seq, (int)std::lround(hm.opos * 1000));
                        dst += mb;
                        if (C.natmsec)
                        {
                            char rb[16];
                            snprintf(rb, sizeof rb, "r%d,", (int)std::lround(hm.orate * 1000));
                            dst += rb;
                            if (!hm.onx.empty()) { dst += 'x'; dst += hm.onx; dst += ','; }
                        }
                        dst += hm.opath;
                    }
                }
            }
            if (C.natprops && !s.mut)
            {
                if (now - s.prAt >= 0.25)
                {
                    std::vector<UObject*> pv;
                    int pn = props_of(o, pv);
                    if (pn != s.prN && C.natlog && g_prHostLog < 80)
                    {
                        ++g_prHostLog;
                        logl("NOLA natprops host: uid=" + std::to_string(s.uid) + " (_" + s.nm + ") contextual props " + std::to_string(s.prN) + " -> " + std::to_string(pn) + " (sent to clients)");
                    }
                    s.prN = pn; s.prAt = now;
                }
                cb |= kCbPropOk | (s.prN > 0 ? kCbProp : 0);
            }
            if (al || !s.shots.empty()) host_bullets(s, wall_ms(), bb);
            if (C.natdeadpos && !al)
            {
                double qx = 0, qy = 0, qz = 0;
                bool pk = pelvis_of(o, qx, qy, qz);
                s2nmc::Pt rp = s2nmc::dead_row_pos(true, s2nmc::Pt{x, y}, s2nmc::Pt{qx, qy}, pk);
                if (pk) { x = rp.x; y = rp.y; z = qz; }
            }
            char b[320];
            snprintf(b, sizeof b, "%d,%d,%s,%d,%d,%d,%d,%d,%d,%u,%d,%08x,%d,%d,%d,%d,%d,%d,%d,%d,%s", s.uid, s.pid, s.nm.c_str(), (int)std::lround(x), (int)std::lround(y),
                     (int)std::lround(z - s.hh), (int)std::lround(yaw), (int)std::lround(vx), (int)std::lround(vy), fl, hpp, s.lk, eq, ls, cb, bs, ws, fx, fy, fz,
                     s.sid.empty() ? "-" : s.sid.c_str());
            c.rec = b;
            if (C.natqg && !s.qg.empty()) { c.rec = s2nmc::with_qg(c.rec, s.qg); ++H.nQgRows; }
            c.fixed = s.fixed;
            c.ok = true;
        }
    }

    auto host_lookcfg() -> s2nmc::LookCfg { return {C.natlookn, C.natlookmax, C.natlookp}; }
    auto host_hdr() -> std::string
    {
        std::string h = C.natrhost && H.nr > 0 ? "r" + std::to_string(std::lround(H.nr)) : std::string();
        if (g_cpHp == 1) h += (h.empty() ? "" : ";") + std::string("h4");
        return h;
    }

    auto host_snapshot(double now, UObject* me, UObject* pup, UObject* tgt) -> std::string
    {
        H.nFixed = H.nRand = H.nVend = 0;
        std::vector<s2nmc::Chunk> ch(H.order.size());
        std::vector<const s2nmc::Chunk*> ord;
        std::vector<int*> ls;
        for (size_t i = 0; i < H.order.size(); ++i)
        {
            auto it = H.sel.find(H.order[i]);
            if (it != H.sel.end()) host_chunk(it->second, now, me, nullptr, pup, tgt, ch[i]);
            ord.push_back(&ch[i]);
            ls.push_back(it != H.sel.end() ? &it->second.lookSent : &g_lsNone);
        }
        std::string nd = dmg_host_str(wall_ms());
        std::string nu;
        if (H.nuDue) { H.nuDue = false; nu = "u" + H.nu; ++H.nuSent; }
        s2nmc::ViewSt vs{H.rr, H.lookRR, H.mrr};
        s2nmc::Out st;
        std::string out = s2nmc::assemble(std::to_string(wall_ms()), ord, ls, vs, host_lookcfg(), now, nu, nd, st, host_hdr());
        H.rr = vs.rr; H.lookRR = vs.lookRR; H.mrr = vs.mrr;
        H.sent += st.n; H.looks += st.looks; H.mBytes += st.mBytes; H.mRe += st.mRe;
        return out;
    }

    auto host_snapshot_mc(double now, UObject* me, const std::vector<HPl>& pl) -> void
    {
        H.nFixed = H.nRand = H.nVend = 0;
        std::unordered_map<UObject*, size_t> at;
        std::vector<s2nmc::Chunk> ch(H.order.size());
        for (size_t i = 0; i < H.order.size(); ++i)
        {
            at[H.order[i]] = i;
            auto it = H.sel.find(H.order[i]);
            if (it != H.sel.end()) host_chunk(it->second, now, me, &pl, nullptr, nullptr, ch[i]);
        }
        std::string nd = dmg_host_str(wall_ms());
        std::string tms = std::to_string(wall_ms());
        for (int v = 0; v < s2nmc::kViews; ++v) H.outV[v].clear();
        for (const HPl& p : pl)
        {
            int v = p.view;
            std::vector<const s2nmc::Chunk*> ord;
            std::vector<int*> ls;
            for (UObject* k : H.ordV[v])
            {
                auto a = at.find(k);
                auto it = H.sel.find(k);
                if (a == at.end() || it == H.sel.end()) continue;
                ord.push_back(&ch[a->second]);
                ls.push_back(&it->second.lookSentV[v]);
            }
            std::string nu;
            if (H.nuDueV[v]) { H.nuDueV[v] = false; nu = "u" + H.nuV[v]; ++H.nuSent; }
            s2nmc::Out st;
            H.outV[v] = s2nmc::assemble(tms, ord, ls, H.vs[v], host_lookcfg(), now, nu, v == 0 ? nd : std::string(), st, host_hdr());
            H.sent += st.n; H.looks += st.looks; H.mBytes += st.mBytes; H.mRe += st.mRe;
            H.mcN[v] += st.n; H.mcBytes[v] += H.outV[v].size(); H.mcMax[v] = std::max(H.mcMax[v], H.outV[v].size());
        }
    }

    int g_st = 0;
    std::string g_stMsg;
    auto selftest_body(std::string& msg) -> bool
    {
        UObject* m = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.Default__CppMediator"));
        if (!m) { msg = "Default__CppMediator nao achado"; return false; }
        int32_t i = m->GetInternalIndex();
        FUObjectItem* it = FUObjectArray::IndexToObject(i);
        if (!it || it->GetUObject() != m) { msg = "slot do objeto nao confere"; return false; }
        int32_t ser = it->GetSerialNumber();
        WRef r; r.set(m);
        if (r.get() != m) { msg = "WRef ida-e-volta falhou"; return false; }
        if (mediator() != m) { msg = "mediator() != StaticFindObject"; return false; }
        if (!fn_of(m, STR("GetGUID"))) { msg = "CppMediator:GetGUID sem UFunction"; return false; }
        msg = "ok (mediator idx=" + std::to_string(i) + " serial=" + std::to_string(ser) + ")";
        return true;
    }
    auto selftest_seh(std::string* msg) -> int
    {
        uintptr_t pf0 = s2n_pf::save();
        __try { return selftest_body(*msg) ? 1 : -1; }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return -2; }
    }
    auto selftest() -> bool
    {
        if (g_st != 0) return g_st == 1;
        int r = selftest_seh(&g_stMsg);
        if (r == -2) g_stMsg = "excecao SEH no autoteste";
        g_st = r == 1 ? 1 : -1;
        logl(std::string("NOLA natnpc autoteste: ") + (g_st == 1 ? "PASS " : "FALHOU (natnpc inerte) ") + g_stMsg);
        return g_st == 1;
    }

#include "s2n_ag.inc"
    uint8_t* g_agStub = nullptr;
    AgData* g_agD = nullptr;
    const char* g_agState = "nao instalado";
    bool g_agTried = false;
    ULONGLONG g_agLogAt = 0;
    LONG g_agLast = 0;
    int g_agN = 0;
    auto ag_eq(const uint8_t* p, const uint8_t* b, size_t n) -> bool
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto ag_near_alloc(uint8_t* base, size_t sz) -> uint8_t*
    {
        SYSTEM_INFO si; GetSystemInfo(&si);
        uintptr_t g = si.dwAllocationGranularity, b = reinterpret_cast<uintptr_t>(base);
        uintptr_t lo = b > 0x60000000ull ? b - 0x60000000ull : 0x10000ull;
        for (uintptr_t a = (b - g) & ~(g - 1); a > lo; a -= g)
        {
            MEMORY_BASIC_INFORMATION mi{};
            if (!VirtualQuery(reinterpret_cast<void*>(a), &mi, sizeof mi) || mi.State != MEM_FREE) continue;
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(a), sz, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return static_cast<uint8_t*>(p);
        }
        return nullptr;
    }
    auto ag_install() -> void
    {
        g_agTried = true;
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        uint8_t* sA = base + 0xaeea43; uint8_t* sB = base + 0xaeec03; uint8_t* sC = base + 0xaeece9;
        static const uint8_t kA[] = {0x3B, 0x05, 0xF7, 0xB6, 0x3E, 0x09, 0x0F, 0x84, 0x6F, 0x05, 0x00, 0x00, 0x31, 0xC0};
        static const uint8_t kB[] = {0x44, 0x8B, 0x25, 0x36, 0xB5, 0x3E, 0x09, 0x66, 0x45, 0x0F, 0x57, 0xFF, 0x44, 0x39, 0xE7, 0x74, 0x05, 0x66, 0x45, 0x0F, 0x28, 0xF8,
                                     0xE8, 0x24, 0x09, 0x00, 0x00};
        static const uint8_t kC[] = {0x31, 0xC0, 0x44, 0x39, 0xE7, 0x0F, 0x94, 0xC0, 0x31, 0xD2, 0x3B, 0x0D, 0x47, 0xB4, 0x3E, 0x09, 0x0F, 0x94, 0xC2,
                                     0x0F, 0x57, 0xC9, 0xF3, 0x0F, 0x5A, 0x4C, 0x95, 0x58};
        if (!ag_eq(sA, kA, sizeof kA) || !ag_eq(sB, kB, sizeof kB) || !ag_eq(sC, kC, sizeof kC)) { g_agState = "bytes diferentes (exe novo?)"; C.aggrop = 0; return; }
        uint8_t* st = ag_near_alloc(base, 0x1000);
        if (!st) { g_agState = "sem memoria perto do exe"; C.aggrop = 0; return; }
        g_agStub = st;
        g_agD = reinterpret_cast<AgData*>(st + 0x800);
        g_agD->hits = 0;
        ag_set(g_agD, nullptr, 0);
        AgStubs ss = ag_emit(st, base, g_agD);
        uint8_t* stA = ss.a; uint8_t* stB = ss.b; uint8_t* stC = ss.c;
        FlushInstructionCache(GetCurrentProcess(), st, 0x1000);
        struct Site { uint8_t* at; uint8_t* to; size_t n; } sites[] = {{sA, stA, 12}, {sB, stB, 0x16}, {sC, stC, 0x13}};
        for (auto& x : sites)
        {
            DWORD old = 0;
            if (!VirtualProtect(x.at, x.n, PAGE_EXECUTE_READWRITE, &old)) { g_agState = "VirtualProtect falhou"; C.aggrop = 0; return; }
            uint8_t buf[0x16];
            std::memset(buf, 0x90, sizeof buf);
            buf[0] = 0xE9;
            int32_t r = (int32_t)(x.to - (x.at + 5));
            std::memcpy(buf + 1, &r, 4);
            std::vector<uint8_t> orig(x.at, x.at + x.n);
            std::memcpy(x.at, buf, x.n);
            VirtualProtect(x.at, x.n, old, &old);
            x0::hot::on_shutdown([at = x.at, orig] {
                DWORD o = 0;
                if (!VirtualProtect(at, orig.size(), PAGE_EXECUTE_READWRITE, &o)) return;
                std::memcpy(at, orig.data(), orig.size());
                VirtualProtect(at, orig.size(), o, &o);
                FlushInstructionCache(GetCurrentProcess(), at, orig.size());
            });
        }
        FlushInstructionCache(GetCurrentProcess(), base + 0xaee000, 0x2000);
        g_agState = "on";
        logl("NOLA b200 aggrop: scorer FUN_140b83c56 player tests extended (exe+0xaeea43, +0xaeec03, +0xaeece9)");
    }
    auto ag_tid_of(UObject* tgt) -> uint32_t
    {
        __try
        {
            auto* ag = *reinterpret_cast<uint8_t**>(reinterpret_cast<uint8_t*>(tgt) + 0x650);
            if (reinterpret_cast<uintptr_t>(ag) < 0x10000) return 0xFFFFFFFFu;
            return *reinterpret_cast<uint32_t*>(ag + 0x10);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFFu; }
    }
    auto ag_update(UObject* const* tgts, int n) -> void
    {
        if (!g_agD) return;
        uint32_t pl = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + 0x9eda140);
        uint32_t ids[kAgTids];
        int k = 0;
        for (int i = 0; i < n && k < kAgTids; ++i)
        {
            uint32_t id = (tgts[i] && C.aggrop) ? ag_tid_of(tgts[i]) : 0xFFFFFFFFu;
            if (id == pl || id == 0xFFFFFFFFu) continue;
            bool dup = false;
            for (int j = 0; j < k; ++j) dup = dup || ids[j] == id;
            if (!dup) ids[k++] = id;
        }
        g_agN = k;
        ag_set(g_agD, ids, k);
    }
    auto ag_update(UObject* tgt) -> void { ag_update(&tgt, 1); }
    auto ag_tick() -> void
    {
        if (C.aggrop && !g_agTried) ag_install();
        if (!g_agD) return;
        if (!C.aggrop) g_agD->on = 0;
        ULONGLONG t = GetTickCount64();
        if (C.natlog && t - g_agLogAt >= 10000)
        {
            g_agLogAt = t;
            LONG h = g_agD->hits;
            char b[200];
            snprintf(b, sizeof b, "NOLA b200 aggrop 10 s: aggrop=%d ativo=%d alvo_id=0x%x alvo_como_jogador=%ld alvos=%d", C.aggrop, (int)g_agD->on, g_agD->tid, h - g_agLast, g_agN);
            g_agLast = h;
            logl(b);
        }
    }
    auto host_mode(bool mc) -> void
    {
        if (H.mc == mc) return;
        H.mc = mc;
        H.listAt = -100;
        for (int v = 0; v < s2nmc::kViews; ++v) { H.ordV[v].clear(); H.perPrev[v].clear(); H.outV[v].clear(); H.vs[v] = s2nmc::ViewSt{}; }
    }
    auto host_stats(double now, double t0, size_t bytes) -> void;
    auto host_api(int mode, double meA, double rx, double ry, double rz, double pupA, double tgtA) -> std::string
    {
        (void)rz;
        if (mode != 1)
        {
            ag_update(nullptr); dmg_update(nullptr, nullptr);
            if (!H.sel.empty()) { for (auto& [o, s] : H.sel) host_offa(s, s.r.get(), false); H.sel.clear(); H.order.clear(); }
            for (int v = 0; v < s2nmc::kViews; ++v) { H.ordV[v].clear(); H.outV[v].clear(); }
            return std::string();
        }
        host_mode(false);
        if (!selftest()) return std::string();
        double t0 = now_s(), now = t0;
        UObject* me = H.me.get(meA);
        UObject* pup = H.pup.get(pupA);
        UObject* tgt = H.tgt.get(tgtA);
        ag_update(tgt);
        dmg_update(tgt, pup);
        std::string out;
        if (rx != 0 || ry != 0)
        {
            if (now - H.listAt >= 1.0) { H.listAt = now; host_rebuild(rx, ry, now, me, pup, tgt); anim_watch(pup, "boneco"); }
            out = host_snapshot(now, me, pup, tgt);
        }
        host_stats(now, t0, out.size());
        return out;
    }
    auto host_mc_api(double meA, const std::vector<s2nat::Player>& in) -> void
    {
        host_mode(true);
        for (int v = 0; v < s2nmc::kViews; ++v) H.outV[v].clear();
        if (!selftest()) return;
        double t0 = now_s(), now = t0;
        UObject* me = H.me.get(meA);
        std::vector<HPl> pl;
        std::vector<UObject*> tg;
        UObject* pup0 = nullptr;
        UObject* tgt0 = nullptr;
        bool seen[s2nmc::kViews] = {};
        for (const s2nat::Player& p : in)
        {
            if (p.view < 0 || p.view >= s2nmc::kViews || seen[p.view]) continue;
            seen[p.view] = true;
            HPl h;
            h.view = p.view; h.pid = p.pid; h.x = p.x; h.y = p.y;
            h.pup = H.plPup[p.view].get((double)p.pup);
            h.tgt = H.plTgt[p.view].get((double)p.tgt);
            if (h.tgt) tg.push_back(h.tgt);
            if (p.view == 0) { pup0 = h.pup; tgt0 = h.tgt; }
            if (p.x != 0 || p.y != 0) pl.push_back(h);
        }
        ag_update(tg.data(), (int)tg.size());
        dmg_update(tgt0, pup0);
        size_t bytes = 0;
        H.nPl = (int)pl.size(); H.nPlMax = std::max(H.nPlMax, H.nPl);
        if (!pl.empty())
        {
            if (now - H.listAt >= 1.0) { H.listAt = now; host_rebuild_mc(pl, now, me); anim_watch(pup0, "boneco"); }
            host_snapshot_mc(now, me, pl);
            for (int v = 0; v < s2nmc::kViews; ++v) bytes += H.outV[v].size();
            ++H.mcSnaps;
        }
        host_stats(now, t0, bytes);
    }
    auto host_stats(double now, double t0, size_t bytes) -> void
    {
        double ms = (now_s() - t0) * 1000;
        H.snaps++; H.bytes += bytes; H.ms += ms; H.msmax = std::max(H.msmax, ms);
        if (now - H.at >= 10)
        {
            if (C.natlog)
            {
                int nDead = 0;
                double oldest = 0;
                for (auto& kv : H.sel) if (kv.second.dead) { ++nDead; oldest = std::max(oldest, now - kv.second.deadAt); }
                char b[560];
                snprintf(b, sizeof b, "NOLA natnpc 10 s (host): npcs_classe=%d na_lista=%d enviados_med=%.1f looks=%d bytes_med=%.0f snapshots=%d custo med=%.2f max=%.2f ms sem_funcao=%d falhas_pe=%d | b162 aparencia fixa=%d aleatoria=%d vendedores=%d (linhas_tabela_perto=%d natvend=%d)"
                         " | b168 natcb=%d em_combate=%d montagens=%d (dinamicas=%d) acertos=%d bytes_m_med=%.0f natmre=%d reenvios=%d",
                         H.nall, (int)H.order.size(), H.sent / (double)std::max(1, H.snaps), H.looks, H.bytes / (double)std::max(1, H.snaps), H.snaps,
                         H.ms / std::max(1, H.snaps), H.msmax, g_noFn, g_peFail, H.nFixed, H.nRand, H.nVend, H.vendRows, C.natvend,
                         C.natcb, H.combat, H.monts, H.montDyn, H.hits, H.mBytes / (double)std::max(1, H.snaps), C.natmre, H.mRe);
                char bb6[200];
                snprintf(bb6, sizeof bb6, " | b196 natbullet=%d (%s) tiros_jogo=%d lista=%d entradas=%d falha_leitura=%d fora_thread=%d",
                         C.natbullet, g_bState, g_bhShots, g_bhSel, g_bhSent, g_bhRead, g_bhOff);
                char bm[200];
                snprintf(bm, sizeof bm, " | m1 natmut=%d classes=%d mutantes_lista=%d mutantes_enviados=%d offanim=%d offvb=%d anim_ligada=%d devolvida=%d natpark=%d uids_park=%d listas_park=%d",
                         C.natmut, (int)g_cls.size(), H.nMut, H.nMutSent, C.offanim, C.offvb, H.offOn, H.offBack, C.natpark, H.nNu, H.nuSent);
                char bg[200];
                snprintf(bg, sizeof bg, " | ms1 natgait=%d marcha_mutante falhou=%d nenhuma=%d parado=%d andar=%d correr=%d sprint=%d trote=%d",
                         C.natgait, g_hMb[0], g_hMb[1], g_hMb[2], g_hMb[3], g_hMb[4], g_hMb[5], g_hMb[6]);
                char ba[320];
                snprintf(ba, sizeof ba, " | ma1 natmab=%d montagens_mutante=%d (dinamicas=%d) registros_m=%d reenvios_m=%d invis_sem_comp=%d visivel=%d invisivel=%d | i16d1 natcloak=%d atual_enviado=%d",
                         C.natmab, g_hmMon, g_hmDyn, g_hmSent, g_hmRe, g_hmInv[0], g_hmInv[1], g_hmInv[2], C.natcloak, g_hmCur);
                char bq[160];
                snprintf(bq, sizeof bq, " | zk1 natqg=%d linhas_com_guid=%d uids_com_guid=%d guid_lido=%d falhou=%d", C.natqg, H.nQgRows, H.nQgNu, g_qgCalls, g_qgFail);
                logl(std::string(b) + " | b191c cobertura_enviada=" + std::to_string(g_covSent) + " | b191g gerador_lido=" + std::to_string(g_genSent) + " | b191h arma_reler=" + std::to_string(g_wpReread) + bb6 + bm + bg + ba + bq + " | i16a5 natrhost=" + std::to_string(C.natrhost) + " raio=" + std::to_string(std::lround(H.nr)) + " fonte=" + (H.rSrc == 1 ? "alife" : H.rSrc == 2 ? "natr(falha)" : "natr") +
                     " | i16lk natlooksec=" + std::to_string(C.natlooksec) + " natmutlook=" + std::to_string(C.natmutlook) + " looks_mutante=" + std::to_string(g_mutLookH) +
                     " | i16cl natfar=" + std::to_string(C.natfar) + " corpos_listados=" + std::to_string(nDead) + " corpo_mais_velho=" + std::to_string(std::lround(oldest)) + " s natdeadall=" + std::to_string(C.natdeadall) + " corpos_antigos=" + std::to_string(H.fd.nAll));
                g_mutLookH = 0;
                if (C.natlegs || C.natmbh)
                {
                    char bl[260];
                    snprintf(bl, sizeof bl, " | natmbh=%d humanos_andando marcha falhou=%d nenhuma=%d parado=%d andar=%d correr=%d sprint=%d trote=%d | sem_socket=%d custo med=%.1f max=%.1f us (n=%d)",
                             C.natmbh, g_hMbH[0], g_hMbH[1], g_hMbH[2], g_hMbH[3], g_hMbH[4], g_hMbH[5], g_hMbH[6], g_lgNoSock, g_lgUs / std::max(1, g_lgCalls), g_lgUsMax, g_lgCalls);
                    logl("NOLA natlegs 10 s (host): natlegs=" + std::to_string(C.natlegs) + " " + g_lgHost.text("referencia_host") + " | " + g_lgHostM.text("referencia_host_mutante") + bl + " | fora_da_tela=" + std::to_string(g_lgOffH));
                }
                if (H.mc)
                {
                    std::string m = "NOLA natnpc mc 10 s (host): natmc=" + std::to_string(C.natmc) + " jogadores=" + std::to_string(H.nPl) + " max=" + std::to_string(H.nPlMax) +
                                    " uniao=" + std::to_string(H.nUnion) + " alvos_aggro=" + std::to_string(g_agN) + " snapshots=" + std::to_string(H.mcSnaps);
                    for (int v = 0; v < s2nmc::kViews; ++v)
                    {
                        char vb[160];
                        snprintf(vb, sizeof vb, " | v%d pid=%d lista=%d enviados_med=%.1f bytes_med=%.0f bytes_max=%zu", v, v + 1, (int)H.ordV[v].size(),
                                 H.mcN[v] / (double)std::max(1, H.mcSnaps), H.mcBytes[v] / (double)std::max(1, H.mcSnaps), H.mcMax[v]);
                        m += vb;
                    }
                    logl(m);
                }
            }
            H.at = now; H.snaps = H.sent = H.looks = 0; H.bytes = 0; H.ms = H.msmax = 0;
            H.nMutSent = H.offOn = H.offBack = H.nuSent = 0; H.nQgRows = H.nQgNu = 0;
            std::fill(std::begin(g_hMb), std::end(g_hMb), 0);
            std::fill(std::begin(g_hMbH), std::end(g_hMbH), 0);
            g_lgHost.clear(); g_lgHostM.clear(); g_lgOffH = 0; g_lgNoSock = 0; g_lgUs = g_lgUsMax = 0; g_lgCalls = 0;
            g_hmMon = g_hmSent = g_hmDyn = g_hmCur = g_hmRe = 0; std::fill(std::begin(g_hmInv), std::end(g_hmInv), 0);
            H.combat = H.monts = H.montDyn = H.hits = H.mRe = 0; H.mBytes = 0;
            g_bhShots = g_bhSel = g_bhSent = g_bhRead = g_bhOff = 0;
            g_wpReread = 0; g_covSent = 0; g_genSent = 0;
            H.mcSnaps = 0; H.nPlMax = 0;
            for (int v = 0; v < s2nmc::kViews; ++v) { H.mcN[v] = 0; H.mcBytes[v] = 0; H.mcMax[v] = 0; }
        }
    }
    auto lua_host(const LuaMadeSimple::Lua& lua) -> int
    {
        auto num = [&](double def) { return lua.is_number() ? lua.get_number() : def; };
        int mode = (int)num(0);
        double meA = num(0), rx = num(0), ry = num(0), rz = num(0), pupA = num(0), tgtA = num(0);
        lua.set_string(host_api(mode, meA, rx, ry, rz, pupA, tgtA));
        return 1;
    }

    struct HRec
    {
        int32_t uid = 0, pid = 0;
        std::string nm;
        double x = 0, y = 0, zg = 0, yaw = 0, vx = 0, vy = 0;
        uint32_t fl = 0, lk = 0;
        int hp = 10000;
        int hpS = 100;
        double recv = -100;
        int gen = 0;
        bool ext = false;
        int eq = -1, ls = 0, cb = 0, bs = 0, ws = 0;
        double fx = 0, fy = 0, fz = 0;
        double covRecv = -200;
        uint8_t cov[0xa0] = {};
        int covP = 0, covV = 0;
        std::string sid;
        std::string qg;
        bool mut() const { return (fl & 1024) != 0; }
    };
    enum St { NONE = 0, PEND = 1, LIVE = 2, DEAD = 3, KEPT = 4, PARK = 5 };
    struct Ent
    {
        HRec h;
        std::string look;
        uint32_t lookH = 0;
        WRef body;
        int32_t luid = 0;
        int how = 0;
        int st = NONE;
        bool shown = false;
        double wpnAt = 0;
        double takeAt = 0, lookChk = 0, keptChk = 0, failUntil = 0, killAt = 0, intAt = 0, lookAtT = 0;
        uint32_t lookDone = 0;
        int mism = -1, reapply = 0;
        bool lookRechk = false;
        std::string why;
        double ordAt = -100, ox = 0, oy = 0;
        int ogait = 0;
        bool crouch = false, aim = false;
        double hh = 88;
        UObject* relMe = nullptr; UObject* relPup = nullptr; UObject* relTgt = nullptr;
        uint32_t relGen = 0;
        int killTry = 0;
        bool vend = false, catchUp = false, lostDead = false, hHid = false, qgAd = false;
        int fdLog = 0;
        double qgSeen = -1;
        double latAt = -1, nudgeAt = 0, csp = 0, oax = 0, oay = 0;
        int ordN = 0;
        struct CMon { int seq = 0; std::string path; double pos = 0, recv = -100; int rate = 0; std::vector<int> nx; } mon[3];
        int mDone[2] = {0, 0};
        double mdWait[2] = {-1, -1}, mdAt = 0;
        int mdWaitSeq[2] = {0, 0}, mdEndSeq = 0;
        double eqAt = -100, eqChg = -100, wpnChk = 0, shAt = -100, statAt = -100;
        int lsDone = -1, ows = -1, cbPrev = 0;
        double wfAt = -100, wfSince = -1, kfSince = -1;
        int wfTries = 0;
        float mws0 = -1;
        int sh0 = 0, brain0 = -1; bool uk0 = false;
        double err = 0, snapSince = -1, aiAt = -100;
        double frcUntil = -100, frcAt = -1;
        double prAt = -100, prSince = -1;
        int frcIn = -1, frcCl = -1, frcRx = -1;
        bool covSrc = false, covLearned = false;
        uintptr_t covKey = 0;
        int covMm0 = -1, covCm0 = 0;
        bool lookOn = false;
        double parkAt = -1, holdAt = -100;
        bool stSrc = false;
        bool frcThr = false;
        int gb = 0, mGateSeq = 0;
        double gbAt = -100, rs = 0, abAt = -100, awAt = -100;
        float mwsW = -1;
        WRef aiB, aiW;
        struct KSmp { double t, x, y, zg, yaw, vx, vy; };
        KSmp ks[16]; int kn = 0;
        bool kOn = false;
        double kx = 0, ky = 0, kz = 0, kyaw = 0, kAt = -1, kmx = 0, kmy = 0, kmt = -1, kcsp = 0;
        float kmws0 = -1, rms0 = -1;
        double rmAt = -100;
        bool rmPin = false;
        int pinHit = 0;
        double clAt = -1e9;
        struct BShot { double t = 0; int seq = 0; float f = 0; double sx = 0, sy = 0, sz = 0; int n = 0; double d[36] = {}; };
        std::vector<BShot> bq; int bSeq = 0;
        LegSt lg;
        int lostN = 0, eqWant = -1, eqDead = -1, eqSeen = -2, lgait = 0;
        int mDefer[2] = {0, 0};
        double turnAt = -100, turnTry = -100, yawHoldAt = -1;
        bool turnOk = false;
        bool gaveUp = false, farLost = false;
        double lx = 0, ly = 0, lz = 0;
        int hgtLog = 0;
        int deadRm = 0;
    };
    struct Grave { WRef r; int32_t luid = 0; double at = 0; int step = 0; };
    struct Pend { int32_t key = 0; double x = 0, y = 0, z = 0, at = 0; int ci = -1; std::unordered_set<UObject*> before; };
    struct IdxE { FName nm; int32_t uid = 0, pid = 0; int ci = -1; double seen = 0; std::string qg; double qgAt = -100; int qgN = 0; };
    struct CliSt
    {
        std::unordered_map<int32_t, Ent> E;
        std::vector<Grave> graves;
        bool pending = false;
        Pend pend;
        double idxAt = -100;
        std::unordered_map<UObject*, IdxE> idx;
        std::unordered_map<int32_t, UObject*> byUid;
        std::unordered_map<std::string, UObject*> byQg;
        std::unordered_map<std::string, int32_t> rowQg;
        std::unordered_map<std::string, double> hq;
        int fdRows = 0;
        int qgAdopt = 0, qgAdoptW = 0, qgRowsRx = 0, qgTwin = 0, qgListed = 0, qgPendSkip = 0, qgWait = 0, qgKeep = 0;
        std::unordered_set<UObject*> idxList;
        std::string lastNN, lastNL;
        int nnGen = 0;
        bool nnFull = false;
        double hostR = 0;
        int hpScale = 100;
        AddrRef me, pup, tgt;
        uintptr_t meAddr = 0;
        int myPid = -1;
        std::vector<std::pair<int, uint64_t>> plA;
        AddrRef plR[8];
        std::vector<std::pair<int, UObject*>> pl;
        std::unordered_set<UObject*> plSet;
        uint32_t plGen = 0;
        std::unordered_set<uintptr_t> own;
        struct Twin { WRef r; int32_t uid = 0; double at = 0; double holdAt = -100; std::string qg; };
        std::unordered_map<uintptr_t, Twin> twin;
        double twinAt = -100;
        std::unordered_set<uint64_t> pkLogged;
        std::unordered_map<int32_t, double> hu;
        double nuAt = -100;
        s2nmc::LostSeen lostSeen;
        int nuRx = 0;
        int parkIn = 0, parkBack = 0, parkOut = 0, twinIn = 0, twinBack = 0, twinOut = 0, dupSpawn = 0, pendWorld = 0, swapReal = 0, dupW = 0, swapW = 0, pkDeadW = 0, pkRefW = 0, pkWaitW = 0, parkHold = 0, parkHoldFail = 0, lookStop = 0, lookStopFail = 0, lookSkip = 0, lookNoop = 0;
        double at = 0;
        int orders = 0, tele = 0, takes = 0, adopt = 0, spawns = 0, copySpawn = 0, spawnFail = 0, kills = 0, removed = 0, errs = 0, errN = 0, ordFail = 0, looksApplied = 0;
        double errSum = 0, errMax = 0;
        int hMov[41] = {}, hStill[41] = {};
        double mvSum = 0, stSum = 0;
        int mvN = 0, stN = 0, gaitN[6] = {}, keepOrd = 0;
        std::string lastErr;
        int noLook = 0;
        bool stLogged = false;
        double inertAt = -100;
        double offWin = 1e9, offMin = 1e9, offAt = 0;
        bool sameClock = true, hasStamp = false;
        int ageH[101] = {}, ageN = 0, latH[31] = {}, latN = 0, latCap = 0, ratN = 0, nudges = 0, vendPause = 0, fixedWait = 0, corpseCap = 0;
        double ageSum = 0, ageMax = 0, latSum = 0, latMax = 0, ratSum = 0;
        std::unordered_set<UObject*> vend;
        double vendAt = -100;
        int vendRows = 0;
        int cN = 0, cIn = 0, cW = 0, cEq = 0, cLs = 0, eqSet = 0, eqFail = 0, shOff = 0, lsSet = 0, lsFail = 0, wound = 0, knock = 0, hits = 0;
        int mPlay = 0, mFail = 0, mMiss = 0, mDyn = 0, mStop = 0, faceN = 0, faceFix = 0, spdSet = 0, holds = 0, nudgeS = 0;
        double faceSum = 0;
        int keptFull = 0, keptSoft = 0, keptTalk = 0, keptRel = 0, keptProto = 0, noSid = 0;
        int snapDefer = 0, snapNow = 0, mGate = 0, mGateStop = 0, aiOn = 0, thrN = 0, thrOn = 0, relaxN = 0, cLook = 0, nudgeCap = 0;
        int frcW = 0, frcKept = 0, frcLost = 0, thrKept = 0, thrLost = 0, thrNoS = 0, frcN = 0, atkShoot = 0, atkN = 0, cInC = 0, cliN = 0;
        double frcUs = 0, cliMs = 0, cliMax = 0;
        int mutLook = 0;
        int gbUp = 0, gbDn = 0, faceSkip = 0, lookDefer = 0, mLoadDefer = 0, mGateKeep = 0, idxN = 0, mwsN = 0, mwsRw = 0, rgN[6] = {};
        double rgS[6] = {}, rgV[6] = {}, mwsPrev = 0;
        int lookLeft = 0, mLoadLeft = 0, ldLeft = 0;
        int lookWait = 0, lookLoads = 0;
        UObject* talkTo = nullptr;
        int dAgeH[101] = {}, dAgeN = 0, mAsyncWait = 0, viaBr = 0, viaT = 0;
        double dAgeSum = 0, dAgeMax = 0;
        std::unordered_map<int32_t, s2nmc::HotDead> hotDead;
        double hotAt = -1;
        bool hotTaken = false;
        int hotBack = 0, hotDrop = 0, hotBad = 0, deadUp = 0, deadFlat = 0, deadNoRag = 0, deadNear = 0, deadExtra = 0, garbRej = 0, deadRemade = 0;
        double deadDistMax = 0;
        double deadScanAt = -100;
    } K;

    enum { T_PARSE, T_IDX, T_LOOP, T_LOOK, T_DRV, T_FACE, T_FORCE, T_CMB, T_MONT, T_STAT, T_N };
    struct SecT { double sum = 0, max = 0; };
    SecT g_sec[T_N];
    auto sec_add(int i, Clock::time_point& t) -> void
    {
        auto n = Clock::now();
        double us = std::chrono::duration<double, std::micro>(n - t).count();
        g_sec[i].sum += us;
        if (us > g_sec[i].max) g_sec[i].max = us;
        t = n;
    }
    auto rd_p_seh(const uint8_t* p) -> UObject* { __try { return *reinterpret_cast<UObject* const*>(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; } }
    auto obj_at(UObject* o, const wchar_t* n) -> UObject*
    {
        if (!o) return nullptr;
        int off = off_of(o, n);
        return off >= 0 ? rd_p_seh(reinterpret_cast<const uint8_t*>(o) + off) : nullptr;
    }
    auto body_ai(Ent& e, UObject* a, double now) -> UObject*
    {
        UObject* ai = e.aiB.get();
        if (ai && now - e.abAt < 1.0) return ai;
        ai = anim_of(obj_at(a, STR("Mesh")));
        e.aiB.set(ai); e.abAt = now;
        return ai;
    }
    auto weap_ai(Ent& e, UObject* a, double now) -> UObject*
    {
        if (now - e.awAt < 0.5) return e.aiW.get();
        e.awAt = now;
        UObject* ai = anim_of(c_obj(a, STR("GetWeaponInHandsMeshComponent")));
        e.aiW.set(ai);
        return ai;
    }
    auto mont_stop_all(UObject* ai, const UObject* const* keep, int nk, float blend) -> int
    {
        UObject* act[8];
        int na = mont_active(ai, act, 8), n = 0;
        for (int i = 0; i < na; ++i)
        {
            bool k = false;
            for (int j = 0; j < nk; ++j) if (act[i] == keep[j]) k = true;
            if (!k && mont_stop(ai, act[i], blend)) ++n;
        }
        return n;
    }
    auto mont_set_tick(Ent& e, UObject* ai, int hs, double now) -> int
    {
        UObject* want[2] = {nullptr, nullptr};
        int wj[2] = {0, 0}, nw = 0;
        for (int j = 0; j < 3; j += 2)
        {
            Ent::CMon& cm = e.mon[j];
            if (cm.seq != hs || cm.path.empty()) continue;
            std::wstring wp = u2w(unpack_path(cm.path));
            if (!asset_ready(wp))
            {
                int st = asset_state(wp);
                if (st == 1) { ++K.mAsyncWait; return 0; }
                if (st == 2) { if (K.mLoadLeft <= 0) { ++K.mLoadDefer; return 0; } --K.mLoadLeft; ++g_asSync; }
            }
            UObject* m = find_asset_once(wp);
            if (!m) { ++K.mMiss; continue; }
            if (C.natmroot) keep_loaded(m);
            want[nw] = m; wj[nw] = j; ++nw;
        }
        e.mDone[0] = hs;
        g_moStop += mont_stop_all(ai, want, nw, 0.25f);
        for (int w = 0; w < nw; ++w)
        {
            Ent::CMon& cm = e.mon[wj[w]];
            UObject* m = want[w];
            float rt = C.natmsec && cm.rate > 0 ? cm.rate / 1000.f : 1.f;
            double at = std::max(0.0, cm.pos + std::clamp(now - cm.recv, 0.0, 2.0) * rt);
            if (C.natmsec) at = msec_at(m, cm.pos, at, cm.nx);
            if (mont_inst(ai, m) && mont_setpos(ai, m, (float)at))
            {
                ++g_mSync; if (wj[w]) ++g_moSync;
                if (C.natmsec) mont_msec(ai, m, cm.rate, cm.nx);
                continue;
            }
            float len = mont_play(ai, m, (float)at, false, rt);
            if (len > 0) { ++K.mPlay; if (wj[w]) ++g_moPlay; if (C.natmsec) mont_msec(ai, m, 0, cm.nx); }
            else ++K.mFail;
        }
        if (nw == 2) ++g_moAll;
        return 1;
    }
    std::unordered_set<std::wstring> g_lookNeg;
    auto look_ready(const std::string& look, int& loads) -> bool
    {
        std::vector<Part> host;
        parse_look(look, host);
        bool ready = true;
        for (const Part& h : host)
        {
            if (wpn_key(h.key)) continue;
            std::vector<const std::string*> ps;
            std::string wm, wc;
            wpn_split(h.mesh, wm, wc);
            if (!wm.empty()) ps.push_back(&wm);
            if (!wc.empty()) ps.push_back(&wc);
            for (const auto& mt : h.mats) if (!mt.empty()) ps.push_back(&mt);
            for (const std::string* q : ps)
            {
                std::wstring w = u2w(unpack_path(*q));
                if (g_lookNeg.count(w) || asset_ready(w)) continue;
                int st = asset_state(w);
                if (st == 0) continue;
                if (st == 1) { ready = false; continue; }
                if (loads <= 0) return false;
                --loads; ++K.lookLoads; ++g_asSync;
                if (!find_asset(w)) g_lookNeg.insert(w);
            }
        }
        return ready;
    }

    auto ent_key(int32_t uid, const std::string& nm) -> int32_t
    {
        if (uid) return uid;
        int32_t v = 0; for (char ch : nm) v = v * 10 + (ch - '0');
        return -std::max(1, v);
    }

    auto stamp_age(int64_t ht, double now) -> double
    {
        double raw = (double)(wall_ms() - ht) / 1000.0;
        K.offWin = std::min(K.offWin, raw);
        if (now - K.offAt >= 10) { K.offMin = K.offWin; K.offWin = 1e9; K.offAt = now; }
        double age;
        if (raw > -0.05 && raw < 3.0) { age = std::max(0.0, raw); K.sameClock = true; }
        else { double base = std::min(K.offMin, K.offWin); age = std::clamp(raw - base + 0.03, 0.0, 3.0); K.sameClock = false; }
        K.ageSum += age; ++K.ageN; K.ageMax = std::max(K.ageMax, age); ++K.ageH[std::min(100, (int)(age * 100))];
        return age;
    }
    auto client_bullet(const std::string& e, double recv) -> void;
    auto cli_r() -> double { return C.natrhost && K.hostR > 0 ? K.hostR : C.natr; }
    auto host_knows(int32_t uid) -> bool;
    auto cli_lost(const HRec& h, double now) -> bool { return now - h.recv > C.natlost || s2nmc::cli_gone(C.natrhost != 0, K.nnFull, h.gen, K.nnGen, K.lostSeen.has(K.nnGen, h.uid)); }
    auto parse_nn(const std::string& s, double now) -> void
    {
        size_t i = 0;
        double recv = now;
        K.hasStamp = false;
        ++K.nnGen;
        K.nnFull = false;
        K.hpScale = 100;
        while (i < s.size())
        {
            size_t j = s.find(';', i);
            if (j == std::string::npos) j = s.size();
            std::string e = s.substr(i, j - i);
            i = j + 1;
            if (!e.empty() && e[0] == 't')
            {
                int64_t ht = _strtoi64(e.c_str() + 1, nullptr, 10);
                if (ht > 0) { recv = now - stamp_age(ht, now); K.hasStamp = true; }
                continue;
            }
            if (!e.empty() && e[0] == 'b') { client_bullet(e, recv); continue; }
            if (!e.empty() && e[0] == 'r')
            {
                double r = std::atof(e.c_str() + 1);
                if (r > 0) { K.hostR = r; K.nnFull = true; }
                continue;
            }
            if (int hs = s2nmc::hp_scale_tok(e)) { K.hpScale = hs; continue; }
            if (!e.empty() && e[0] == 'l')
            {
                s2nmc::nu_each(e, 1, [&](int32_t u, const std::string&) { if (u > 0) K.lostSeen.add(K.nnGen, u); });
                continue;
            }
            if (!e.empty() && e[0] == 'u')
            {
                K.nuAt = now; ++K.nuRx;
                s2nmc::nu_each(e, 1, [&](int32_t u, const std::string& g) {
                    if (u > 0) K.hu[u] = now;
                    if (u > 0 && C.natqg && !g.empty()) K.hq[g] = now;
                });
                for (auto it = K.hu.begin(); it != K.hu.end();) it = now - it->second > 120 ? K.hu.erase(it) : std::next(it);
                for (auto it = K.hq.begin(); it != K.hq.end();) it = now - it->second > 120 ? K.hq.erase(it) : std::next(it);
                continue;
            }
            if (!e.empty() && e[0] == 'v')
            {
                int vu = 0, off = 0;
                if (sscanf_s(e.c_str() + 1, "%d,%n", &vu, &off) >= 1 && off > 0 && e.size() >= (size_t)(1 + off + 0x140))
                {
                    auto it = K.E.find(vu);
                    if (it != K.E.end())
                    {
                        HRec& h = it->second.h;
                        const char* hx = e.c_str() + 1 + off;
                        bool ok = true;
                        for (int q = 0; q < 0xa0 && ok; ++q)
                        {
                            unsigned v = 0;
                            ok = sscanf_s(hx + q * 2, "%2x", &v) == 1;
                            h.cov[q] = (uint8_t)v;
                        }
                        int pp = 0, pv = 0;
                        if (ok && sscanf_s(hx + 0x140, ",%d,%d", &pp, &pv) == 2) { h.covP = pp; h.covV = pv; h.covRecv = recv; }
                    }
                }
                continue;
            }
            if (!e.empty() && e[0] == 'w') { wpn_rx(e, now); continue; }
            if (!e.empty() && e[0] == 'm')
            {
                int mu = 0, sq = 0, pm = 0, off = 0;
                char kc = 0;
                if (sscanf_s(e.c_str() + 1, "%d,%c,%d,%d,%n", &mu, &kc, 1, &sq, &pm, &off) >= 4 && off > 0)
                {
                    auto it = K.E.find(mu);
                    if (it != K.E.end() && (kc == 'b' || kc == 'w' || kc == 'o'))
                    {
                        Ent::CMon& cm = it->second.mon[kc == 'w' ? 1 : kc == 'o' ? 2 : 0];
                        if (cm.seq != sq || C.natmre)
                        {
                            const char* s = e.c_str() + 1 + off;
                            int rr = 0;
                            std::vector<int> xv;
                            while (*s == 'r' || *s == 'x')
                            {
                                const char* c = strchr(s, ',');
                                if (!c) break;
                                if (*s == 'r') rr = atoi(s + 1);
                                else for (const char* q = s + 1; q < c;) { xv.push_back(atoi(q)); const char* d = static_cast<const char*>(memchr(q, '.', c - q)); q = d ? d + 1 : c; }
                                s = c + 1;
                            }
                            if (cm.seq == sq) ++g_mRefresh;
                            cm.seq = sq; cm.path = s; cm.pos = pm / 1000.0; cm.recv = recv; cm.rate = rr; cm.nx = std::move(xv);
                        }
                    }
                }
                continue;
            }
            int uid, pid, x, y, zg, yaw, vx, vy, hp;
            unsigned fl, lk;
            int eq = -1, ls = 0, cb = 0, bs = 0, ws = 0, fx = 0, fy = 0, fz = 0;
            char nm[32] = {};
            char sd[64] = {};
            int nr = sscanf_s(e.c_str(), "%d,%d,%31[0-9],%d,%d,%d,%d,%d,%d,%u,%d,%x,%d,%d,%d,%d,%d,%d,%d,%d,%63[A-Za-z0-9_-]", &uid, &pid, nm, (unsigned)sizeof nm, &x, &y, &zg, &yaw, &vx, &vy, &fl, &hp, &lk,
                              &eq, &ls, &cb, &bs, &ws, &fx, &fy, &fz, sd, (unsigned)sizeof sd);
            if (nr != 12 && nr != 20 && nr != 21) continue;
            if (!C.natmut && (fl & 1024)) continue;
            int32_t key = ent_key(uid, nm);
            Ent& E = K.E[key];
            E.h.uid = uid; E.h.pid = pid; E.h.nm = nm; E.h.x = x; E.h.y = y; E.h.zg = zg; E.h.yaw = yaw; E.h.vx = vx; E.h.vy = vy;
            E.h.fl = fl; E.h.hp = hp; E.h.hpS = K.hpScale; E.h.lk = lk; E.h.recv = recv; E.h.gen = K.nnGen;
            if (E.kn == 0 || recv > E.ks[E.kn - 1].t + 0.001)
            {
                if (E.kn >= 16) { std::memmove(E.ks, E.ks + 1, 15 * sizeof(Ent::KSmp)); E.kn = 15; }
                E.ks[E.kn++] = {recv, (double)x, (double)y, (double)zg, (double)yaw, (double)vx, (double)vy};
            }
            E.h.sid = (nr == 21 && std::strcmp(sd, "-") != 0) ? std::string(sd) : std::string();
            E.h.qg = (nr == 21 && C.natqg) ? s2nmc::qg_field(e) : std::string();
            if (!E.h.qg.empty()) { K.rowQg[E.h.qg] = uid; ++K.qgRowsRx; }
            E.h.ext = nr >= 20;
            if (E.h.ext) { E.h.eq = eq; E.h.ls = ls; E.h.cb = cb; E.h.bs = bs; E.h.ws = ws; E.h.fx = fx; E.h.fy = fy; E.h.fz = fz; }
        }
    }
    auto parse_nl(const std::string& s) -> void
    {
        size_t i = 0;
        while (i < s.size())
        {
            size_t j = s.find(';', i);
            if (j == std::string::npos) j = s.size();
            std::string e = s.substr(i, j - i);
            i = j + 1;
            size_t q = e.find('=');
            if (q == std::string::npos) continue;
            int32_t uid = (int32_t)std::strtol(e.substr(0, q).c_str(), nullptr, 10);
            auto it = K.E.find(uid);
            if (it == K.E.end()) continue;
            std::string l = e.substr(q + 1);
            uint32_t h = fnv(l);
            if (h != it->second.lookH) { it->second.look = std::move(l); it->second.lookH = h; }
        }
    }

    auto is_ours(UObject* o) -> bool { return o && K.own.count(reinterpret_cast<uintptr_t>(o)) > 0; }
    auto is_pl(UObject* o) -> bool { return o && K.plSet.count(o) > 0; }

    struct InE { WRef r; int32_t uid = 0; double at = 0; bool asked = false; };
    std::unordered_map<uintptr_t, InE> g_in;
    int g_inOn = 0, g_dsOn = 0;
    uintptr_t g_inMe = 0, g_inPup = 0, g_inTgt = 0;
    long g_inMaIn = 0, g_inMaHid = 0, g_inHoldOk = 0, g_inHoldFail = 0, g_inAdd = 0, g_inDrop = 0, g_inLogN = 0;
    auto in_block(UObject* o) -> int
    {
        if (!g_inOn || !o) return 0;
        uintptr_t a = reinterpret_cast<uintptr_t>(o);
        if (a == g_inMe || a == g_inPup) return 0;
        if (g_in.count(a)) return 1;
        return prop_bool(o, STR("bHidden"), false) ? 2 : 0;
    }
    auto in_note(UObject* o, int why) -> void
    {
        if (!C.natlog || (++g_inLogN > 20 && g_inLogN % 100 != 0)) return;
        std::string nm = "?", cl = "?";
        try { nm = w2u(o->GetName()); if (UClass* c = o->GetClassPrivate()) cl = w2u(c->GetName()); } catch (...) {}
        char b[400];
        snprintf(b, sizeof b, "NOLA clinert: ataque de NPC local cortado na fonte (MakeAttack) %s classe=%s motivo=%s #%ld", nm.c_str(), cl.c_str(), why == 1 ? "inerte" : "escondido", g_inLogN);
        logl(b);
    }

    using MontPlayFn = float (*)(void*, void*, float, uint8_t, float, bool);
    using MakeAtkFn = void (*)(void*, int);
    uint64_t g_mpTramp = 0, g_maTramp = 0;
    PLH::x64Detour* g_mpDet = nullptr;
    PLH::x64Detour* g_maDet = nullptr;
    volatile LONG g_mpNull = 0, g_mpNullTot = 0;
    int g_maCopy = 0, g_maSkip = 0, g_mpCopy = 0, g_mpFree = 0;
    uintptr_t g_mpLastRet = 0;
    std::string g_mpLastWho = "-";
    UObject* g_maCur = nullptr;
    const char* g_mpState = "nao instalado";
    const char* g_maState = "nao instalado";
    auto mem_eq_seh(const uint8_t* p, const uint8_t* b, size_t n) -> bool
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    float mp_detour(void* self, void* m, float rate, uint8_t rt, float at, bool stopAll)
    {
        if (!self)
        {
            InterlockedIncrement(&g_mpNull);
            InterlockedIncrement(&g_mpNullTot);
            g_mpLastRet = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            if (g_gtid != 0 && GetCurrentThreadId() == g_gtid && g_maCur)
            {
                bool ours = is_ours(g_maCur);
                ours ? ++g_mpCopy : ++g_mpFree;
                try { g_mpLastWho = std::string(ours ? "copia " : "livre ") + w2u(g_maCur->GetName()); } catch (...) { g_mpLastWho = "?"; }
            }
            if (C.montnull) return 0.f;
        }
        return reinterpret_cast<MontPlayFn>(g_mpTramp)(self, m, rate, rt, at, stopAll);
    }
    void ma_detour(void* comp, int k)
    {
        UObject* owner = nullptr;
        if (comp && g_gtid != 0 && GetCurrentThreadId() == g_gtid) owner = reinterpret_cast<UObject*>(comp)->GetOuterPrivate();
        if (owner && is_ours(owner))
        {
            ++g_maCopy;
            if (C.natnoatk) { ++g_maSkip; return; }
        }
        else if (int why = in_block(owner))
        {
            why == 1 ? ++g_inMaIn : ++g_inMaHid;
            in_note(owner, why);
            return;
        }
        UObject* prev = g_maCur;
        g_maCur = owner;
        reinterpret_cast<MakeAtkFn>(g_maTramp)(comp, k);
        g_maCur = prev;
    }
    using V370Fn = void (*)(void*, float, void*);
    using E87Fn = void (*)(void*, float, void*);
    using PtFn = void (*)(void*);
    using HitFn = void (*)(void*, void*);
    using ImpFn = int (*)(void*, void*, void*, void*, void*, uint8_t);
    using SpFn = void (*)(void*, void*, void*, uint8_t);
    uint64_t g_bvTramp = 0, g_bpTramp = 0, g_bhTramp = 0, g_biTramp = 0, g_bsTramp = 0;
    PLH::x64Detour* g_bvDet = nullptr; PLH::x64Detour* g_bpDet = nullptr; PLH::x64Detour* g_bhDet = nullptr;
    PLH::x64Detour* g_biDet = nullptr; PLH::x64Detour* g_bsDet = nullptr;
    thread_local bool g_bRep = false;
    thread_local bool g_bCall = false;
    constexpr int kBOwnN = 64;
    volatile uintptr_t g_bOwn[kBOwnN] = {};
    uintptr_t g_bOwnA[kBOwnN] = {};
    auto bown_add(uintptr_t ent, uintptr_t a) -> void
    {
        int fr = -1;
        for (int i = 0; i < kBOwnN; ++i) { if (g_bOwn[i] == ent) { g_bOwnA[i] = a; return; } if (fr < 0 && g_bOwn[i] == 0) fr = i; }
        if (fr < 0) fr = (int)((ent >> 4) % kBOwnN);
        g_bOwn[fr] = 0; MemoryBarrier(); g_bOwnA[fr] = a; g_bOwn[fr] = ent;
    }
    auto bown_has(uintptr_t a) -> bool { if (!a) return false; for (int i = 0; i < kBOwnN; ++i) if (g_bOwn[i] == a) return true; return false; }
    auto bown_prune() -> void
    {
        for (int i = 0; i < kBOwnN; ++i) if (g_bOwn[i] && !K.own.count(g_bOwnA[i])) g_bOwn[i] = 0;
    }
    auto bsd_read_seh(const uint8_t* b, double* st, int* n, double* d, int cap) -> bool
    {
        __try
        {
            const double* t = reinterpret_cast<const double*>(b + 0x20);
            st[0] = t[0]; st[1] = t[1]; st[2] = t[2];
            const double* dd = *reinterpret_cast<const double* const*>(b + 0x60);
            int k = *reinterpret_cast<const int*>(b + 0x68);
            if (k < 0 || k > 64 || (k > 0 && !dd)) return false;
            if (k > cap) k = cap;
            for (int i = 0; i < 3 * k; ++i) d[i] = dd[i];
            *n = k;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void bv_detour(void* self, float f, void* bsd)
    {
        if (C.natbullet && self && bsd && !H.sel.empty())
        {
            if (g_gtid != 0 && GetCurrentThreadId() == g_gtid)
            {
                ++g_bhShots;
                auto it = H.sel.find(reinterpret_cast<UObject*>(self));
                if (it != H.sel.end() && it->second.r.get() == reinterpret_cast<UObject*>(self))
                {
                    HSel& hs = it->second;
                    HSel::HShot sh;
                    double st[3], d[36];
                    int n = 0;
                    if (bsd_read_seh(reinterpret_cast<const uint8_t*>(bsd), st, &n, d, 12) && n > 0)
                    {
                        if (++hs.shSeq > 999999) hs.shSeq = 1;
                        sh.seq = hs.shSeq; sh.ms = wall_ms(); sh.n = n;
                        sh.sx = (int)std::lround(st[0]); sh.sy = (int)std::lround(st[1]); sh.sz = (int)std::lround(st[2]);
                        sh.f = (int)std::lround(std::clamp((double)f, -1e6, 1e6) * 100);
                        for (int i = 0; i < 3 * n; ++i) sh.d[i] = (int)std::lround(std::clamp(d[i], -1.0, 1.0) * 10000);
                        if (hs.shots.size() >= 24) hs.shots.erase(hs.shots.begin());
                        hs.shots.push_back(sh);
                        ++g_bhSel;
                    }
                    else ++g_bhRead;
                }
            }
            else ++g_bhOff;
        }
        reinterpret_cast<V370Fn>(g_bvTramp)(self, f, bsd);
    }
    auto host_bullets(HSel& s, int64_t wnow, std::string& bb) -> void
    {
        int64_t keep = (int64_t)(C.natbwin * 1000);
        while (!s.shots.empty() && wnow - s.shots.front().ms > keep) s.shots.erase(s.shots.begin());
        if (!C.natbullet) { s.shots.clear(); return; }
        for (const HSel::HShot& sh : s.shots)
        {
            char t[64];
            snprintf(t, sizeof t, "b%d,%d,%lld,%d,%d,%d,%d,%d", s.uid, sh.seq, (long long)(wnow - sh.ms), sh.sx, sh.sy, sh.sz, sh.f, sh.n);
            std::string e = t;
            for (int i = 0; i < 3 * sh.n; ++i) { e += ","; e += std::to_string(sh.d[i]); }
            if (!bb.empty()) bb += ";";
            bb += e;
            ++g_bhSent;
        }
    }
    auto client_bullet(const std::string& e, double recv) -> void
    {
        ++g_bRx;
        int v[8 + 36]; int k = 0;
        const char* p = e.c_str() + 1;
        while (*p && k < 8 + 36)
        {
            char* q = nullptr;
            long x = strtol(p, &q, 10);
            if (q == p) return;
            v[k++] = (int)x;
            p = q;
            if (*p == ',') ++p; else break;
        }
        if (k < 8 || v[7] < 1 || v[7] > 12 || k != 8 + 3 * v[7]) return;
        auto it = K.E.find(v[0]);
        if (it == K.E.end()) { ++g_bNoEnt; return; }
        Ent& E = it->second;
        int sq = v[1];
        bool newer = E.bSeq == 0 || sq > E.bSeq || E.bSeq - sq > 500000;
        if (!newer) return;
        if (E.bSeq != 0 && sq > E.bSeq + 1) g_bLost += sq - E.bSeq - 1;
        E.bSeq = sq;
        ++g_bNew;
        Ent::BShot b;
        b.t = recv - v[2] / 1000.0; b.seq = sq; b.f = v[6] / 100.f; b.sx = v[3]; b.sy = v[4]; b.sz = v[5]; b.n = v[7];
        for (int i = 0; i < 3 * b.n; ++i) b.d[i] = v[8 + i] / 10000.0;
        if (E.bq.size() >= 32) { E.bq.erase(E.bq.begin()); ++g_bStale; }
        E.bq.push_back(b);
    }
    alignas(16) uint8_t g_bGate[0x20] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    auto e87_call_seh(uint8_t* comp, float f, uint8_t* bsd) -> int
    {
        uintptr_t pf0 = s2n_pf::save();
        __try
        {
            uint8_t** gp = reinterpret_cast<uint8_t**>(comp + 0x180);
            uint8_t* old = *gp;
            *gp = g_bGate;
            g_bCall = true;
            __try { reinterpret_cast<E87Fn>(g_bE87)(comp, f, bsd); }
            __finally { g_bCall = false; *gp = old; }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return -1; }
    }
    auto bul_ok_seh(uint8_t* a, uint8_t*& comp, uint8_t*& ent, double& ts) -> int
    {
        __try
        {
            comp = *reinterpret_cast<uint8_t**>(a + 0x7c8);
            if (!comp || *reinterpret_cast<uint8_t**>(comp + 0x98) != a) return 1;
            uint8_t* l320 = *reinterpret_cast<uint8_t**>(comp + 0x320);
            if (!l320 || !*reinterpret_cast<uint8_t**>(l320)) return 1;
            uint8_t* a650 = *reinterpret_cast<uint8_t**>(a + 0x650);
            if (!a650 || !*reinterpret_cast<uint8_t**>(a650 + 0x678)) return 1;
            ent = a650;
            uint8_t* tm = g_bTime ? *reinterpret_cast<uint8_t**>(g_bTime) : nullptr;
            if (!tm) return 2;
            ts = *reinterpret_cast<double*>(tm + 0x90);
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
    }
    auto bullet_fire(Ent& e, const Ent::BShot& b, double now) -> void
    {
        UObject* a = e.st == LIVE ? e.body.get() : nullptr;
        if (!a) { ++g_bNoCopy; return; }
        uint8_t* comp = nullptr; uint8_t* ent = nullptr; double ts = 0;
        int r = bul_ok_seh(reinterpret_cast<uint8_t*>(a), comp, ent, ts);
        if (r != 0) { ++g_bNoComp; return; }
        alignas(16) uint8_t bsd[0x80] = {};
        double* q = reinterpret_cast<double*>(bsd);
        q[3] = 1.0;
        q[4] = b.sx; q[5] = b.sy; q[6] = b.sz;
        q[8] = q[9] = q[10] = 1.0;
        struct Arr { double* p; int32_t n, m; };
        double dd[36];
        for (int i = 0; i < 3 * b.n; ++i) dd[i] = b.d[i];
        Arr* ar = reinterpret_cast<Arr*>(bsd + 0x60);
        ar->p = dd; ar->n = b.n; ar->m = b.n;
        *reinterpret_cast<double*>(bsd + 0x70) = ts;
        *reinterpret_cast<uint32_t*>(bsd + 0x78) = 0x9E3779B9u * (uint32_t)b.seq;
        bown_add(reinterpret_cast<uintptr_t>(ent), reinterpret_cast<uintptr_t>(a));
        if (e87_call_seh(comp, b.f, bsd) < 0) { ++g_bExc; return; }
        ++g_bFire;
        double age = (now - b.t) * 1000;
        g_bAgeSum += age; g_bAgeMax = std::max(g_bAgeMax, age);
    }
    auto bullet_frame() -> void
    {
        if (!g_bE87) return;
        double now = now_s();
        auto t0 = Clock::now();
        bool any = false;
        for (auto& [k, e] : K.E)
        {
            if (e.bq.empty()) continue;
            any = true;
            size_t i = 0;
            for (; i < e.bq.size(); ++i)
            {
                const Ent::BShot& b = e.bq[i];
                double at = b.t + C.natbdel;
                if (now < at) break;
                if (!C.natbullet || now - at > 1.0) { ++g_bStale; continue; }
                bullet_fire(e, b, now);
            }
            if (i) e.bq.erase(e.bq.begin(), e.bq.begin() + i);
        }
        if (any)
        {
            double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
            ++g_bFr; g_bUs += us; g_bUsMax = std::max(g_bUsMax, us);
        }
    }
    auto proj_owner_seh(const uint8_t* p) -> uintptr_t { __try { return *reinterpret_cast<const uintptr_t*>(p + 0x310); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
    void bp_detour(void* proj)
    {
        bool prev = g_bRep;
        bool rep = proj && bown_has(proj_owner_seh(reinterpret_cast<const uint8_t*>(proj)));
        if (rep) { g_bRep = true; InterlockedIncrement(&g_bPt); }
        reinterpret_cast<PtFn>(g_bpTramp)(proj);
        g_bRep = prev;
    }
    void bh_detour(void* comp, void* args)
    {
        if (g_bRep && C.natbullet) { InterlockedIncrement(&g_bBlk); return; }
        reinterpret_cast<HitFn>(g_bhTramp)(comp, args);
    }
    int bi_detour(void* a, void* proj, void* hit, void* res, void* p5, uint8_t p6)
    {
        if (g_bRep) InterlockedIncrement(&g_bImp);
        return reinterpret_cast<ImpFn>(g_biTramp)(a, proj, hit, res, p5, p6);
    }
    void bs_detour(void* sub, void* req, void* dirs, uint8_t pl)
    {
        if (g_bCall) InterlockedIncrement(&g_bProj);
        reinterpret_cast<SpFn>(g_bsTramp)(sub, req, dirs, pl);
    }
    auto bul_hook(uint8_t* p, const uint8_t* k, size_t n, void* det, uint64_t& tr, PLH::x64Detour*& d) -> bool
    {
        if (!mem_eq_seh(p, k, n)) return false;
        d = new x0::hot::Detour((uint64_t)p, (uint64_t)det, &tr);
        if (d->hook()) return true;
        delete d; d = nullptr; tr = 0;
        return false;
    }
    auto bul_install(uint8_t* base) -> void
    {
        static const uint8_t kV[] = {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x0F, 0x29,
                                     0xB4, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x4D, 0x89, 0xC6, 0x0F, 0x28, 0xF1, 0x48, 0x89, 0xCB};
        static const uint8_t kE[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0x98, 0x01, 0x00, 0x00};
        static const uint8_t kP[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0x38, 0x04, 0x00, 0x00};
        static const uint8_t kH[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0xD3, 0x48, 0x89, 0xCE};
        static const uint8_t kI[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xB8, 0x02, 0x00, 0x00};
        static const uint8_t kS[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xA8, 0x02, 0x00, 0x00};
        static const uint8_t kCall[] = {0xE8};
        uint8_t* e87 = base + 0xe6c378;
        bool okE = mem_eq_seh(e87, kE, sizeof kE) && mem_eq_seh(base + 0xb30cd0, kCall, 1);
        bool v = okE && bul_hook(base + 0xe6c1c8, kV, sizeof kV, (void*)&bv_detour, g_bvTramp, g_bvDet);
        bool pt = v && bul_hook(base + 0xb31106, kP, sizeof kP, (void*)&bp_detour, g_bpTramp, g_bpDet);
        bool h = pt && bul_hook(base + 0x7613b2, kH, sizeof kH, (void*)&bh_detour, g_bhTramp, g_bhDet);
        bool i = h && bul_hook(base + 0xb375f0, kI, sizeof kI, (void*)&bi_detour, g_biTramp, g_biDet);
        bool sp = i && bul_hook(base + 0xe6d378, kS, sizeof kS, (void*)&bs_detour, g_bsTramp, g_bsDet);
        if (sp) { g_bE87 = e87; g_bTime = base + 0xa2c85a8; g_bState = "on"; }
        else
        {
            if (g_bsDet) { g_bsDet->unHook(); } if (g_biDet) { g_biDet->unHook(); } if (g_bhDet) { g_bhDet->unHook(); }
            if (g_bpDet) { g_bpDet->unHook(); } if (g_bvDet) { g_bvDet->unHook(); }
            g_bState = !okE ? "bytes diferentes (exe novo?)" : !v ? "vf_370 falhou" : !pt ? "acfd68 falhou" : !h ? "6b73a6 falhou" : !i ? "ad6130 falhou" : "e88d2a falhou";
            C.natbullet = 0;
        }
        logl(std::string("NOLA b196 natbullet: ") + g_bState);
    }
    using DmgFn = void (*)(void*, void*, char);
    using DdBuildFn = void* (*)(void*, const void*);
    using DdDtorFn = void (*)(void*);
    uint64_t g_ddTramp = 0;
    volatile LONG g_killBypassHit = 0;
    PLH::x64Detour* g_ddDet = nullptr;
    uint8_t* g_ddBuild = nullptr; uint8_t* g_ddDtor = nullptr;
    const char* g_ddState = "nao instalado";
    volatile uintptr_t g_ddTgt = 0, g_ddPup = 0;
    volatile uint32_t g_ddTgtId = 0xFFFFFFFFu;
    volatile ULONGLONG g_ddUpdAt = 0;
    constexpr uint32_t kNoAtk = 0xFFFFFFFEu;
    struct DRaw { uint32_t atk; float f[5]; uint32_t bi, bn; double v[6]; uint8_t ty, src, b7a, b7b; };
    struct DHit { int seq = 0; int64_t ms = 0; DRaw r{}; std::string bone; bool logged = false; };
    SRWLOCK g_ddLock = SRWLOCK_INIT;
    std::vector<DHit> g_ddQ;
    int g_ddSeq = 0, g_ddLogN = 0;
    volatile LONG g_ddTgtN = 0, g_ddKept = 0, g_ddNoAtk = 0, g_ddHost = 0, g_ddBleed = 0, g_ddRead = 0, g_ddPupN = 0, g_ddSentN = 0;
    double g_ddSumKept = 0;
    std::unordered_map<int, double> g_dcSeen;
    int g_dcMax = 0, g_dcRx = 0, g_dcNew = 0, g_dcLost = 0, g_dcOk = 0, g_dcDead = 0, g_dcProt = 0, g_dcCap = 0, g_dcExc = 0, g_dcNoEnt = 0,
        g_dcOff = 0, g_dcBad = 0, g_dcLogN = 0;
    double g_dcSum = 0, g_dcHp = 0, g_dcAgeSum = 0, g_dcAgeMax = 0, g_dcBudget = -1, g_dcBudAt = 0, g_dcProtUntil = 0, g_dcPruneAt = 0;
    uintptr_t g_dcMe = 0;
    ULONGLONG g_ddLogAt = 0;
    auto dd_read_seh(const uint8_t* d, DRaw* r) -> bool
    {
        __try
        {
            std::memcpy(r->f, d, 20);
            r->atk = *reinterpret_cast<const uint32_t*>(d + 0x20);
            r->bi = *reinterpret_cast<const uint32_t*>(d + 0x40);
            r->bn = *reinterpret_cast<const uint32_t*>(d + 0x44);
            std::memcpy(r->v, d + 0x48, 48);
            r->ty = d[0x78]; r->src = d[0x79]; r->b7a = d[0x7a]; r->b7b = d[0x7b];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto dd_ent_seh(UObject* a, uint32_t* id) -> uintptr_t
    {
        __try
        {
            if (!a) return 0;
            uintptr_t e = *reinterpret_cast<uintptr_t*>(reinterpret_cast<uint8_t*>(a) + 0x650);
            if (e < 0x10000) return 0;
            if (id) *id = *reinterpret_cast<uint32_t*>(e + 0x10);
            return e;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    auto dd_id_seh(uintptr_t e) -> uint32_t { __try { return *reinterpret_cast<uint32_t*>(e + 0x10); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0xFFFFFFFEu; } }
    constexpr int kCpN = 64;
    volatile uintptr_t g_cpE[kCpN] = {};
    volatile uint32_t g_cpId[kCpN] = {};
    volatile LONG g_cpU[kCpN] = {};
    volatile LONG g_cpN = 0, g_cpSeen = 0, g_cpDrop = 0, g_cpMaxN = 0;
    volatile ULONGLONG g_cpAt = 0;
    long g_cpHpSet = 0, g_cpHpOk = 0, g_cpHpFail = 0;
    double g_cpHpErrMax = 0;
    auto cp_slot(uintptr_t e) -> int
    {
        if (GetTickCount64() - g_cpAt > 1000) return -1;
        LONG n = g_cpN;
        for (LONG i = 0; i < n && i < kCpN; ++i)
            if (g_cpE[i] == e) return dd_id_seh(e) == g_cpId[i] ? (int)i : -1;
        return -1;
    }
    auto cp_has(uintptr_t e) -> bool { return cp_slot(e) >= 0; }
    volatile LONG g_cpG[kCpN] = {};
    using GsFn = uint32_t (*)(uint8_t*);
    uint64_t g_gsTramp = 0;
    PLH::x64Detour* g_gsDet = nullptr;
    const char* g_gsState = "nao instalado";
    volatile LONG g_gsCp = 0, g_gsOv = 0, g_gsIdle = 0, g_gsMv = 0, g_gsWhy[6] = {};
    auto gs_rec_seh(const uint8_t* m) -> uintptr_t { __try { return *reinterpret_cast<const uintptr_t*>(m + 8); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
    auto gs_why_seh(const uint8_t* m) -> int
    {
        __try
        {
            if (m[0x218] > 1) return 1;
            if (m[0xa8] == 1) return 2;
            if (m[0x13a]) return 3;
            if (m[0x228] & 1) return 4;
            return 5;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    auto gs_mv_seh(const uint8_t* m) -> int { __try { return *reinterpret_cast<const int32_t*>(m + 0x108) >= 2 ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; } }
    uint32_t gs_detour(uint8_t* m)
    {
        uint32_t r = reinterpret_cast<GsFn>(g_gsTramp)(m);
        if (!C.natgaitcp || g_cpN <= 0) return r;
        uintptr_t rec = gs_rec_seh(m);
        int s = rec ? cp_slot(rec) : -1;
        if (s < 0) return r;
        InterlockedIncrement(&g_gsCp);
        LONG g = g_cpG[s];
        if (g < 1 || g > 4) return r;
        if ((r & 0xff) == 1 && g > 1) { InterlockedIncrement(&g_gsIdle); InterlockedIncrement(&g_gsWhy[gs_why_seh(m)]); }
        if (g > 1) InterlockedAdd(&g_gsMv, gs_mv_seh(m));
        if ((LONG)(r & 0xff) != g) InterlockedIncrement(&g_gsOv);
        return (r & ~0xffu) | (uint32_t)g;
    }
    auto gs_install(uint8_t* base) -> void
    {
        static const uint8_t kGs[] = {0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x28, 0xB3, 0x01, 0x80, 0xB9, 0x18, 0x02, 0x00, 0x00, 0x01, 0x77, 0x0C,
                                      0x48, 0x89, 0xCE, 0x80, 0xB9, 0xA8, 0x00, 0x00, 0x00, 0x01};
        uint8_t* gs = base + 0x239895c;
        if (!mem_eq_seh(gs, kGs, sizeof kGs)) { g_gsState = "bytes diferentes (exe novo?)"; return; }
        g_gsDet = new x0::hot::Detour((uint64_t)gs, (uint64_t)&gs_detour, &g_gsTramp);
        if (g_gsDet->hook()) g_gsState = "on"; else { delete g_gsDet; g_gsDet = nullptr; g_gsTramp = 0; g_gsState = "FALHOU"; }
    }
    using LuFn = void (*)(uint8_t*);
    using TagFn = uint64_t (*)(uintptr_t, uint64_t);
    uint64_t g_luTramp = 0;
    PLH::x64Detour* g_luDet = nullptr;
    const char* g_luState = "nao instalado";
    TagFn g_luClr = nullptr, g_luAdd = nullptr;
    using PrmFn = float (*)(uintptr_t, uint8_t);
    PrmFn g_luPrm = nullptr;
    volatile LONG g_luPrmOk = 0, g_luPrmBad = 0;
    volatile uintptr_t g_cpA[kCpN] = {};
    volatile LONG g_cpSp[kCpN] = {}, g_cpHc[kCpN] = {};
    volatile LONG g_luCall = 0, g_luCp = 0, g_luWr = 0, g_luFail = 0, g_luBad = 0, g_luIdle = 0, g_luMove = 0, g_luKeep = 0;
    uint64_t g_luTag[2][2][5] = {};
    uint64_t g_luTagS[2][2][5] = {};
    volatile double g_luC0 = 0;
    bool g_luTagOk = false;
    volatile LONG g_luStrafe = 0, g_luStockMode = 0;
    auto lu_tags() -> void
    {
        if (g_luTagOk) return;
        static const wchar_t* gn[5] = {L"", L".Idle", L".Walk", L".Run", L".Sprint"};
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 2; ++s)
                for (int g = 1; g <= 4; ++g)
                    for (int sv = 0; sv < 2; ++sv)
                    {
                        std::wstring n = std::wstring(L"Locomotion") + (c ? L".Combat" : L".Relax") + (sv ? L".Strafe" : L".Default") + (s ? L".Crouch" : L".Stand") + gn[g];
                        FName f(n.c_str(), FNAME_Add);
                        uint64_t raw = 0;
                        std::memcpy(&raw, &f, sizeof raw);
                        (sv ? g_luTagS : g_luTag)[c][s][g] = raw;
                    }
        g_luTagOk = true;
    }
    auto lu_known(uint64_t v) -> bool
    {
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 2; ++s)
                for (int g = 1; g <= 4; ++g)
                    if (g_luTag[c][s][g] == v) return true;
        return false;
    }
    auto lu_is_strafe(uint64_t v) -> bool
    {
        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < 2; ++s)
                for (int g = 1; g <= 4; ++g)
                    if (g_luTagS[c][s][g] == v) return true;
        return false;
    }
    volatile LONG g_luOther = 0, g_luBits = 0, g_luTagW = 0, g_luSpW = 0, g_luSfFix = 0;
    auto lu_fl_seh(uint8_t* m, uintptr_t rec, uint8_t* a, int g, float sp, int hc) -> bool
    {
        __try
        {
            if (*reinterpret_cast<uintptr_t*>(a + 0x650) != rec) return false;
            int cr = m[0xa9] == 1 ? 1 : 0;
            uint8_t* x = nullptr;
            uintptr_t r658 = *reinterpret_cast<uintptr_t*>(rec + 0x658);
            if (r658 && *reinterpret_cast<int32_t*>(r658 + 0x5ac) != 0)
            {
                uintptr_t u2 = *reinterpret_cast<uintptr_t*>(a + 0xad0);
                if (!u2) return false;
                x = reinterpret_cast<uint8_t*>(u2) + 0x450;
            }
            else
            {
                uintptr_t u = *reinterpret_cast<uintptr_t*>(a + 0xac8);
                if (!u) return false;
                x = reinterpret_cast<uint8_t*>(u) + 0x620;
            }
            uint64_t cur = *reinterpret_cast<uint64_t*>(x + 0xb8);
            bool strafe = lu_is_strafe(cur);
            if (cur != 0 && !strafe && !lu_known(cur)) { InterlockedIncrement(&g_luOther); return true; }
            bool useS = strafe;
            if (strafe && C.natlocosf && !(hc && g <= 2)) { useS = false; InterlockedIncrement(&g_luSfFix); }
            uint64_t tag = (useS ? g_luTagS : g_luTag)[hc ? 1 : 0][cr][g];
            if (!tag) return false;
            bool mv = g > 1;
            if (C.natlocoprm && g_luPrm)
            {
                uint8_t* p668 = *reinterpret_cast<uint8_t**>(rec + 0x668);
                if (p668 && p668[0x1e8] != 0)
                {
                    g_luPrm(rec, (uint8_t)(mv ? g : 2));
                    InterlockedIncrement(p668[0x1e8] == 0 ? &g_luPrmOk : &g_luPrmBad);
                }
            }
            InterlockedIncrement(&g_luStockMode);
            if (strafe) InterlockedIncrement(&g_luStrafe);
            if (g_luC0 == 0 && cur == g_luTag[0][0][1] && *reinterpret_cast<double*>(x + 0xc0) != 0.0) g_luC0 = *reinterpret_cast<double*>(x + 0xc0);
            uint64_t clr = 0, add = 0;
            switch (g)
            {
            case 1: clr = 0x3f0; break;
            case 2: clr = 0x50; add = 0xa0; break;
            case 3: clr = 0x30; add = 0xc0; break;
            case 4: clr = 0x60; add = 0x90; break;
            }
            if (m[0xa9] == 0) clr |= 0x400; else if (m[0xa9] == 1) add |= 0x400;
            uint64_t bits = *reinterpret_cast<uint64_t*>(rec + 0x250);
            bool any = false;
            if ((bits & clr) != 0 || (bits & add) != add)
            {
                if (clr) g_luClr(rec, clr);
                if (add) g_luAdd(rec, add);
                InterlockedIncrement(&g_luBits);
                any = true;
            }
            *reinterpret_cast<uint64_t*>(m + 0x130) = tag; m[0x138] = (uint8_t)g;
            if (cur != tag)
            {
                *reinterpret_cast<uint64_t*>(x + 0xb8) = tag;
                if (mv || g_luC0 != 0) *reinterpret_cast<double*>(x + 0xc0) = mv ? 0.0 : g_luC0;
                x[0x224] = 1;
                InterlockedIncrement(&g_luTagW);
                mv ? InterlockedIncrement(&g_luMove) : InterlockedIncrement(&g_luIdle);
                any = true;
            }
            if (mv)
            {
                float d = *reinterpret_cast<float*>(x + 0xc8) - sp;
                if (x[0xcc] != 1 || d > 5.f || d < -5.f) { *reinterpret_cast<float*>(x + 0xc8) = sp; x[0xcc] = 1; InterlockedIncrement(&g_luSpW); any = true; }
                *reinterpret_cast<float*>(m + 0x128) = sp; m[0x12c] = 1;
            }
            else
            {
                if (x[0xcc] != 0) { x[0xcc] = 0; any = true; }
                m[0x12c] = 0;
            }
            InterlockedIncrement(any ? &g_luWr : &g_luKeep);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto lu_apply_seh(uint8_t* m, uintptr_t rec, uint8_t* a, int g, float sp, int hc) -> bool
    {
        __try
        {
            if (*reinterpret_cast<uintptr_t*>(a + 0x650) != rec) return false;
            int cr = m[0xa9] == 1 ? 1 : 0;
            uint64_t tag = g_luTag[hc ? 1 : 0][cr][g];
            if (!tag) return false;
            bool mv = g > 1;
            uint64_t* lastTag = reinterpret_cast<uint64_t*>(m + 0x130);
            float* lastSp = reinterpret_cast<float*>(m + 0x128);
            if (C.natlocoprm && g_luPrm)
            {
                uint8_t* p668 = *reinterpret_cast<uint8_t**>(rec + 0x668);
                if (p668 && p668[0x1e8] != 0)
                {
                    g_luPrm(rec, (uint8_t)(mv ? g : 2));
                    InterlockedIncrement(p668[0x1e8] == 0 ? &g_luPrmOk : &g_luPrmBad);
                }
            }
            bool chg = *lastTag != tag || m[0x138] != (uint8_t)g;
            float dsp = *lastSp - sp;
            bool spChg = mv && (dsp > 5.f || dsp < -5.f);
            if (!chg && !spChg) { InterlockedIncrement(&g_luKeep); return true; }
            uintptr_t u =*reinterpret_cast<uintptr_t*>(a + 0xac8);
            if (!u) return false;
            uint8_t* x = reinterpret_cast<uint8_t*>(u) + 0x620;
            uintptr_t r658 = *reinterpret_cast<uintptr_t*>(rec + 0x658);
            if (r658 && *reinterpret_cast<int32_t*>(r658 + 0x5ac) != 0)
            {
                uintptr_t u2 = *reinterpret_cast<uintptr_t*>(a + 0xad0);
                if (!u2) return false;
                x = reinterpret_cast<uint8_t*>(u2) + 0x450;
            }
            uint64_t cur = *reinterpret_cast<uint64_t*>(x + 0xb8);
            if (cur != 0 && !lu_known(cur)) { InterlockedIncrement(&g_luBad); return false; }
            if (g_luC0 == 0 && cur == g_luTag[0][0][1] && *reinterpret_cast<double*>(x + 0xc0) != 0.0) g_luC0 = *reinterpret_cast<double*>(x + 0xc0);
            if (chg)
            {
                switch (g)
                {
                case 1: g_luClr(rec, 0x3f0); break;
                case 2: g_luClr(rec, 0xd0); g_luAdd(rec, 0xa0); break;
                case 3: g_luClr(rec, 0xb0); g_luAdd(rec, 0xc0); break;
                case 4: g_luClr(rec, 0xe0); g_luAdd(rec, 0x90); break;
                }
                if (m[0xa9] == 0) g_luClr(rec, 0x400); else if (m[0xa9] == 1) g_luAdd(rec, 0x400);
                *lastTag = tag; m[0x138] = (uint8_t)g;
                *reinterpret_cast<uint64_t*>(x + 0xb8) = tag;
                if (mv || g_luC0 != 0) *reinterpret_cast<double*>(x + 0xc0) = mv ? 0.0 : g_luC0;
                mv ? InterlockedIncrement(&g_luMove) : InterlockedIncrement(&g_luIdle);
            }
            if (mv) { *lastSp = sp; *reinterpret_cast<float*>(x + 0xc8) = sp; x[0xcc] = 1; m[0x12c] = 1; }
            else { x[0xcc] = 0; m[0x12c] = 0; }
            x[0x224] = 1;
            InterlockedIncrement(&g_luWr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void lu_detour(uint8_t* m)
    {
        InterlockedIncrement(&g_luCall);
        if (C.natloco && g_cpN > 0 && g_luTagOk)
        {
            uintptr_t rec = gs_rec_seh(m);
            int s = rec ? cp_slot(rec) : -1;
            if (s >= 0)
            {
                LONG g = g_cpG[s];
                uintptr_t a = g_cpA[s];
                if (g >= 1 && g <= 4 && a)
                {
                    InterlockedIncrement(&g_luCp);
                    if (C.natlocofl)
                    {
                        reinterpret_cast<LuFn>(g_luTramp)(m);
                        if (!lu_fl_seh(m, rec, reinterpret_cast<uint8_t*>(a), (int)g, (float)g_cpSp[s], (int)g_cpHc[s])) InterlockedIncrement(&g_luFail);
                        return;
                    }
                    if (lu_apply_seh(m, rec, reinterpret_cast<uint8_t*>(a), (int)g, (float)g_cpSp[s], (int)g_cpHc[s])) return;
                    InterlockedIncrement(&g_luFail);
                }
            }
        }
        reinterpret_cast<LuFn>(g_luTramp)(m);
    }
    auto lu_install(uint8_t* base) -> void
    {
        static const uint8_t kLu[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0x08, 0x05, 0x00, 0x00,
                                      0x0F, 0x29, 0xBC, 0x24, 0xF0, 0x04, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0xE0, 0x04, 0x00, 0x00, 0x48, 0x89, 0xCE};
        static const uint8_t kClr[] = {0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0x05};
        static const uint8_t kAdd[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0xD6, 0x48, 0x8B, 0x05};
        uint8_t* lu = base + 0x2397454;
        if (!mem_eq_seh(lu, kLu, sizeof kLu) || !mem_eq_seh(base + 0x53671c, kClr, sizeof kClr) || !mem_eq_seh(base + 0x53677a, kAdd, sizeof kAdd)) { g_luState = "bytes diferentes (exe novo?)"; return; }
        g_luClr = reinterpret_cast<TagFn>(base + 0x53671c);
        g_luAdd = reinterpret_cast<TagFn>(base + 0x53677a);
        static const uint8_t kPrm[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x89, 0xD3, 0x48, 0x89, 0xCE, 0x48, 0x8B, 0xB9, 0x68, 0x06, 0x00, 0x00, 0x80, 0xBF, 0xE8, 0x01, 0x00, 0x00, 0x00};
        if (mem_eq_seh(base + 0x8ea172, kPrm, sizeof kPrm)) g_luPrm = reinterpret_cast<PrmFn>(base + 0x8ea172);
        g_luDet = new x0::hot::Detour((uint64_t)lu, (uint64_t)&lu_detour, &g_luTramp);
        if (g_luDet->hook()) g_luState = "on"; else { delete g_luDet; g_luDet = nullptr; g_luTramp = 0; g_luState = "FALHOU"; }
    }
    using TbFn = uint64_t (*)(uint8_t*, float);
    uint64_t g_tbTramp = 0;
    PLH::x64Detour* g_tbDet = nullptr;
    const char* g_tbState = "nao instalado";
    volatile uintptr_t g_cpXh[kCpN] = {}, g_cpXm[kCpN] = {}, g_cpCmc[kCpN] = {};
    volatile float g_cpVx[kCpN] = {}, g_cpVy[kCpN] = {};
    volatile LONG g_tbCall = 0, g_tbCp = 0, g_tbFill = 0, g_tbStock = 0, g_tbGrow = 0, g_tbFail = 0, g_tbIdle = 0, g_tbStill = 0, g_tbVel = 0, g_tbVelBad = 0, g_tbVelJump = 0;
    auto tb_slot(uintptr_t x) -> int
    {
        if (GetTickCount64() - g_cpAt > 1000) return -1;
        LONG n = g_cpN;
        for (LONG i = 0; i < n && i < kCpN; ++i)
            if (g_cpXh[i] == x || g_cpXm[i] == x) return (int)i;
        return -1;
    }
    auto tb_idle_seh(uint8_t* x) -> int
    {
        __try
        {
            uint8_t* d = *reinterpret_cast<uint8_t**>(x + 8);
            if (!d || d[0x79] == 0) return 0;
            d[0x79] = 0;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    volatile LONG g_tbOwn[2] = {}, g_tbOwnOff[2] = {}, g_tbOwnBack[2] = {};
    auto tb_own_seh(uint8_t* data, int32_t num, double vx, double vy, int mut) -> int32_t
    {
        constexpr int kSz = 0x60;
        int32_t k = num - 1;
        while (k > 0 && *reinterpret_cast<float*>(data + (size_t)k * kSz + 0x38) > 0.001f) --k;
        if (k == num - 1) return num;
        InterlockedIncrement(&g_tbOwn[mut]);
        double sp = std::hypot(vx, vy);
        if (sp > 1)
        {
            uint8_t* c = data + (size_t)k * kSz;
            uint8_t* f = data + (size_t)(num - 1) * kSz;
            double fx = *reinterpret_cast<double*>(f + 0x20) - *reinterpret_cast<double*>(c + 0x20), fy = *reinterpret_cast<double*>(f + 0x28) - *reinterpret_cast<double*>(c + 0x28);
            double fl = std::hypot(fx, fy);
            if (fl > 1)
            {
                double cs = (fx * vx + fy * vy) / (fl * sp);
                if (cs < 0.7071) InterlockedIncrement(&g_tbOwnOff[mut]);
                if (cs < 0) InterlockedIncrement(&g_tbOwnBack[mut]);
            }
        }
        return k + 1;
    }
    auto tb_fill_seh(uint8_t* x, double vx, double vy, bool mv, int mut) -> int
    {
        __try
        {
            constexpr int kSz = 0x60;
            uint8_t* d = *reinterpret_cast<uint8_t**>(x + 8);
            if (!d) return -1;
            uint8_t* data = *reinterpret_cast<uint8_t**>(d);
            int32_t num = *reinterpret_cast<int32_t*>(d + 8), mx = *reinterpret_cast<int32_t*>(d + 0xc);
            if (!data || num < 1 || num > 4096 || mx < num || (data >= d && data < d + 0x200)) return -1;
            int32_t k = tb_own_seh(data, num, mv ? vx : 0.0, mv ? vy : 0.0, mut);
            if (C.nattrajown && k != num) { num = k; *reinterpret_cast<int32_t*>(d + 8) = num; }
            if (!mv) return 3;
            uint8_t* base = data + (size_t)(num - 1) * kSz;
            float t0 = *reinterpret_cast<float*>(base + 0x38);
            if (t0 > 0.001f) return 2;
            int n = std::clamp(C.nattrajn, 1, 60);
            double h = std::clamp(C.nattrajh, 0.1, 3.0);
            int need = num + n;
            if (need > mx)
            {
                int nm = need + 8;
                void* p = (*RC::Unreal::GMalloc)->Realloc(data, (size_t)nm * kSz, 16);
                if (!p) return -1;
                data = static_cast<uint8_t*>(p);
                *reinterpret_cast<uint8_t**>(d) = data;
                *reinterpret_cast<int32_t*>(d + 0xc) = nm;
                base = data + (size_t)(num - 1) * kSz;
                InterlockedIncrement(&g_tbGrow);
            }
            double bx = *reinterpret_cast<double*>(base + 0x20), by = *reinterpret_cast<double*>(base + 0x28);
            for (int i = 1; i <= n; ++i)
            {
                uint8_t* s = base + (size_t)i * kSz;
                std::memcpy(s, base, kSz);
                float t = (float)(h * i / n);
                double dt = (double)t - (double)t0;
                *reinterpret_cast<double*>(s + 0x20) = bx + vx * dt;
                *reinterpret_cast<double*>(s + 0x28) = by + vy * dt;
                *reinterpret_cast<float*>(s + 0x38) = t;
            }
            *reinterpret_cast<int32_t*>(d + 8) = need;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto tb_vel_seh(uint8_t* x, double vx, double vy) -> int
    {
        __try
        {
            constexpr int kSz = 0x60;
            uint8_t* d = *reinterpret_cast<uint8_t**>(x + 8);
            if (!d) return -1;
            uint8_t* data = *reinterpret_cast<uint8_t**>(d);
            int32_t num = *reinterpret_cast<int32_t*>(d + 8);
            if (!data || num < 1 || num > 4096) return -1;
            auto at = [&](int i) { return data + (size_t)i * kSz; };
            auto tt = [&](int i) { return (double)*reinterpret_cast<float*>(at(i) + 0x38); };
            for (int i = 0; i < num; ++i)
            {
                uint8_t* s = at(i);
                if (tt(i) > -0.001)
                {
                    *reinterpret_cast<double*>(s + 0x40) = vx;
                    *reinterpret_cast<double*>(s + 0x48) = vy;
                    continue;
                }
                int j0 = i > 0 ? i - 1 : i, j1 = i + 1 < num ? i + 1 : i;
                double h = tt(j1) - tt(j0);
                if (h < 1e-4) continue;
                double v[3];
                for (int c = 0; c < 3; ++c)
                    v[c] = (*reinterpret_cast<double*>(at(j1) + 0x20 + c * 8) - *reinterpret_cast<double*>(at(j0) + 0x20 + c * 8)) / h;
                if (std::hypot(v[0], v[1]) > C.nattrajvmax) { v[0] = vx; v[1] = vy; v[2] = 0; InterlockedIncrement(&g_tbVelJump); }
                for (int c = 0; c < 3; ++c) *reinterpret_cast<double*>(s + 0x40 + c * 8) = v[c];
            }
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    volatile LONG g_tbCv = 0, g_tbCvBad = 0, g_tbCvMut = 0;
    auto tb_cv_seh(uint8_t* x, uintptr_t cmc, double vx, double vy) -> int
    {
        __try
        {
            uint8_t* d = *reinterpret_cast<uint8_t**>(x + 8);
            if (!d || !cmc || *reinterpret_cast<uintptr_t*>(d + 0x150) != cmc) return 0;
            auto* v = reinterpret_cast<double*>(cmc + 0x12d0);
            v[0] = vx; v[1] = vy; v[2] = 0;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    uint64_t tb_detour(uint8_t* x, float dt)
    {
        InterlockedIncrement(&g_tbCall);
        int s = ((C.nattraj || C.natcpvel) && g_cpN > 0) ? tb_slot(reinterpret_cast<uintptr_t>(x)) : -1;
        if (s < 0) return reinterpret_cast<TbFn>(g_tbTramp)(x, dt);
        double vx = g_cpVx[s], vy = g_cpVy[s];
        if (C.natcpvel)
        {
            if (tb_cv_seh(x, g_cpCmc[s], vx, vy)) { InterlockedIncrement(&g_tbCv); if (g_cpXm[s] == reinterpret_cast<uintptr_t>(x)) InterlockedIncrement(&g_tbCvMut); }
            else InterlockedIncrement(&g_tbCvBad);
        }
        if (!C.nattraj) return reinterpret_cast<TbFn>(g_tbTramp)(x, dt);
        InterlockedIncrement(&g_tbCp);
        bool mv = std::hypot(vx, vy) > C.nattrajmin;
        if (mv && tb_idle_seh(x) == 1) InterlockedIncrement(&g_tbIdle);
        uint64_t r = reinterpret_cast<TbFn>(g_tbTramp)(x, dt);
        int mut = g_cpXm[s] == reinterpret_cast<uintptr_t>(x) ? 1 : 0;
        if (mv)
        {
            int f = tb_fill_seh(x, vx, vy, true, mut);
            InterlockedIncrement(f == 1 ? &g_tbFill : f == 2 ? &g_tbStock : &g_tbFail);
        }
        else
        {
            InterlockedIncrement(&g_tbStill);
            tb_fill_seh(x, vx, vy, false, mut);
        }
        if (C.nattrajv) InterlockedIncrement(tb_vel_seh(x, mv ? vx : 0.0, mv ? vy : 0.0) == 1 ? &g_tbVel : &g_tbVelBad);
        return r;
    }
    auto tb_install(uint8_t* base) -> void
    {
        static const uint8_t kTb[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0xB8, 0x68, 0x22, 0x00, 0x00, 0xE8};
        uint8_t* tb = base + 0x237106c;
        if (!mem_eq_seh(tb, kTb, sizeof kTb)) { g_tbState = "bytes diferentes (exe novo?)"; return; }
        g_tbDet = new x0::hot::Detour((uint64_t)tb, (uint64_t)&tb_detour, &g_tbTramp);
        if (g_tbDet->hook()) g_tbState = "on"; else { delete g_tbDet; g_tbDet = nullptr; g_tbTramp = 0; g_tbState = "FALHOU"; }
    }
    using NwFn = void (*)(uint8_t*, uint8_t*, float);
    uint64_t g_nwTramp = 0;
    PLH::x64Detour* g_nwDet = nullptr;
    const char* g_nwState = "nao instalado";
    volatile uintptr_t g_nwCmc[kCpN] = {};
    volatile LONG g_nwN = 0, g_nwSkip = 0, g_nwPass = 0, g_nwMaxN = 0;
    volatile ULONGLONG g_nwAt = 0;
    auto nw_own(uintptr_t c) -> bool
    {
        if (GetTickCount64() - g_nwAt > 1000) return false;
        LONG n = g_nwN;
        for (LONG i = 0; i < n && i < kCpN; ++i)
            if (g_nwCmc[i] == c) return true;
        return false;
    }
    void nw_detour(uint8_t* cmc, uint8_t* q, float dt)
    {
        if (C.natnavw && g_nwN > 0 && nw_own(reinterpret_cast<uintptr_t>(cmc)))
        {
            cmc[0x12c8] = 1;
            InterlockedIncrement(&g_nwSkip);
        }
        else InterlockedIncrement(&g_nwPass);
        reinterpret_cast<NwFn>(g_nwTramp)(cmc, q, dt);
    }
    auto nw_install(uint8_t* base) -> void
    {
        static const uint8_t kNw[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xD8, 0x01, 0x00, 0x00};
        static const uint8_t kNwRst[] = {0x80, 0xB9, 0xC8, 0x12, 0x00, 0x00, 0x00};
        static const uint8_t kTp[] = {0x80, 0x89, 0x44, 0x05, 0x00, 0x00, 0x20, 0xC6, 0x81, 0xC8, 0x12, 0x00, 0x00, 0x01};
        uint8_t* nw = base + 0x2167e2a;
        if (!mem_eq_seh(nw, kNw, sizeof kNw) || !mem_eq_seh(nw + 0x61, kNwRst, sizeof kNwRst) || !mem_eq_seh(base + 0x116bb94, kTp, sizeof kTp)) { g_nwState = "bytes diferentes (exe novo?)"; return; }
        g_nwDet = new x0::hot::Detour((uint64_t)nw, (uint64_t)&nw_detour, &g_nwTramp);
        if (g_nwDet->hook()) g_nwState = "on"; else { delete g_nwDet; g_nwDet = nullptr; g_nwTramp = 0; g_nwState = "FALHOU"; }
    }
    using WpFn = uint32_t (*)(uint8_t*, uint32_t, float, uint32_t);
    uint64_t g_wpTramp = 0, g_wiTramp = 0;
    PLH::x64Detour* g_wpDet = nullptr;
    PLH::x64Detour* g_wiDet = nullptr;
    const char* g_wpState = "nao instalado";
    auto wp_block(uint8_t* self) -> bool
    {
        if (!C.natwpnlocal || g_cpN <= 0 || t_wpnOurs > 0) return false;
        uint32_t id = 0;
        uintptr_t p = dd_ent_seh(reinterpret_cast<UObject*>(self), &id);
        return p && cp_slot(p) >= 0;
    }
    uint32_t wp_wrap_detour(uint8_t* self, uint32_t t, float h, uint32_t s)
    {
        if (wp_block(self)) { InterlockedIncrement(&g_wpBlk); return 0; }
        return reinterpret_cast<WpFn>(g_wpTramp)(self, t, h, s);
    }
    uint32_t wp_impl_detour(uint8_t* self, uint32_t t, float h, uint32_t s)
    {
        if (wp_block(self)) { InterlockedIncrement(&g_wpBlkImpl); return 0; }
        return reinterpret_cast<WpFn>(g_wiTramp)(self, t, h, s);
    }
    auto wp_install(uint8_t* base) -> void
    {
        static const uint8_t kWp[] = {0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x0F, 0x29, 0x74, 0x24, 0x40, 0x44, 0x89, 0xCF, 0x0F, 0x28, 0xF2, 0x89, 0xD5};
        static const uint8_t kWi[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xC8, 0x00, 0x00, 0x00, 0x0F, 0x29, 0xB4, 0x24, 0xB0};
        uint8_t* wp = base + 0xc60660;
        uint8_t* wi = base + 0xc60752;
        if (!mem_eq_seh(wp, kWp, sizeof kWp) || !mem_eq_seh(wi, kWi, sizeof kWi)) { g_wpState = "bytes diferentes (exe novo?)"; return; }
        g_wpDet = new x0::hot::Detour((uint64_t)wp, (uint64_t)&wp_wrap_detour, &g_wpTramp);
        if (!g_wpDet->hook()) { delete g_wpDet; g_wpDet = nullptr; g_wpTramp = 0; g_wpState = "FALHOU"; return; }
        g_wiDet = new x0::hot::Detour((uint64_t)wi, (uint64_t)&wp_impl_detour, &g_wiTramp);
        if (g_wiDet->hook()) g_wpState = "on"; else { delete g_wiDet; g_wiDet = nullptr; g_wiTramp = 0; g_wpState = "so wrapper"; }
    }    auto hp_mirror(const Ent& e, UObject* a) -> void
    {
        if (!g_cpHp || !(e.h.fl & 1) || e.h.hp <= 0) return;
        float mh = c_float(a, STR("GetMaxHP"), 0.f);
        float cur = c_float(a, STR("GetHP"), -1.f);
        if (mh <= 0 || cur < 0) return;
        double want = s2nmc::hp_want(e.h.hp, e.h.hpS, mh);
        double err = std::fabs(cur - want);
        if (err < 1.0) return;
        g_cpHpErrMax = std::max(g_cpHpErrMax, err);
        ++g_cpHpSet;
        Call c(a, STR("ForceSetHP"));
        c.i(STR("HP"), (int32_t)want);
        if (c.run()) ++g_cpHpOk; else ++g_cpHpFail;
    }
    auto dmg_update(UObject* tgt, UObject* pup) -> void
    {
        if (!g_ddDet) return;
        uint32_t id = 0xFFFFFFFFu;
        uintptr_t t = (C.natdmg && tgt) ? dd_ent_seh(tgt, &id) : 0;
        g_ddTgt = 0; MemoryBarrier(); g_ddTgtId = id; g_ddTgt = t;
        g_ddUpdAt = GetTickCount64();
        g_ddPup = pup ? dd_ent_seh(pup, nullptr) : 0;
    }
    auto dd_capture(const uint8_t* d) -> void
    {
        InterlockedIncrement(&g_ddTgtN);
        DRaw r{};
        if (!d || !dd_read_seh(d, &r)) { InterlockedIncrement(&g_ddRead); return; }
        uint32_t pl = *reinterpret_cast<const uint32_t*>(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + 0x9eda140);
        if (r.atk >= 0x8000000u) { InterlockedIncrement(&g_ddNoAtk); return; }
        if (r.atk == pl) { InterlockedIncrement(&g_ddHost); return; }
        if (r.ty == 10 || r.src == 19) { InterlockedIncrement(&g_ddBleed); return; }
        DHit h;
        h.ms = wall_ms(); h.r = r;
        AcquireSRWLockExclusive(&g_ddLock);
        if (++g_ddSeq > 999999) g_ddSeq = 1;
        h.seq = g_ddSeq;
        if (g_ddQ.size() >= 32) g_ddQ.erase(g_ddQ.begin());
        g_ddQ.push_back(std::move(h));
        g_ddSumKept += r.f[0];
        ReleaseSRWLockExclusive(&g_ddLock);
        InterlockedIncrement(&g_ddKept);
    }
    constexpr int kFfN = 16;
    volatile uintptr_t g_ffE[kFfN] = {};
    volatile uint32_t g_ffId[kFfN] = {};
    volatile LONG g_ffV[kFfN] = {}, g_ffP[kFfN] = {}, g_ffK[kFfN] = {};
    volatile LONG g_ffN = 0, g_ffOn = 0, g_ffLive = 0, g_ffOff = 0;
    volatile ULONGLONG g_ffAt = 0;
    struct FRaw { int view = 0, pid = 0, puppet = 0; DRaw r{}; };
    SRWLOCK g_ffLock = SRWLOCK_INIT;
    std::vector<FRaw> g_ffQ;
    volatile LONG g_ffSeen = 0, g_ffKept = 0, g_ffOther = 0, g_ffBleed = 0, g_ffRead = 0, g_ffFull = 0, g_ffApOk = 0, g_ffApExc = 0;
    auto pl_id() -> uint32_t { return *reinterpret_cast<const uint32_t*>(reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)) + 0x9eda140); }
    auto ff_slot(uintptr_t e) -> int
    {
        if (!g_ffOn || GetTickCount64() - g_ffAt > 2000) return -1;
        LONG n = g_ffN;
        for (LONG i = 0; i < n && i < kFfN; ++i)
            if (g_ffE[i] == e) return dd_id_seh(e) == g_ffId[i] ? (int)i : -1;
        return -1;
    }
    auto ff_capture(int i, const uint8_t* d) -> bool
    {
        InterlockedIncrement(&g_ffSeen);
        DRaw r{};
        if (!d || !dd_read_seh(d, &r)) { InterlockedIncrement(&g_ffRead); return false; }
        if (r.atk >= 0x8000000u || r.atk != pl_id()) { InterlockedIncrement(&g_ffOther); return false; }
        if (r.ty == 10 || r.src == 19) { InterlockedIncrement(&g_ffBleed); return true; }
        if (!g_ffLive) { InterlockedIncrement(&g_ffOff); return true; }
        FRaw h;
        h.view = (int)g_ffV[i]; h.pid = (int)g_ffP[i]; h.puppet = (int)g_ffK[i]; h.r = r;
        AcquireSRWLockExclusive(&g_ffLock);
        if (g_ffQ.size() >= 32) { g_ffQ.erase(g_ffQ.begin()); InterlockedIncrement(&g_ffFull); }
        g_ffQ.push_back(h);
        ReleaseSRWLockExclusive(&g_ffLock);
        InterlockedIncrement(&g_ffKept);
        return true;
    }
    struct MRaw { int32_t uid = 0; DRaw r{}; };
    SRWLOCK g_mdLock = SRWLOCK_INIT;
    std::vector<MRaw> g_mdQ;
    volatile LONG g_mdOn = 0;
    volatile LONG g_mdSeen = 0, g_mdKept = 0, g_mdBullet = 0, g_mdOther = 0, g_mdBleed = 0, g_mdRead = 0, g_mdFull = 0, g_mdApOk = 0, g_mdApExc = 0, g_mdOtherAtk = -1, g_mdOtherSrc = -1;
    volatile LONG g_mdBySrc[48] = {};
    auto md_bullet(uint8_t s) -> bool { return s == 2 || s == 23 || s == 24; }
    auto md_capture(int slot, const uint8_t* d) -> void
    {
        InterlockedIncrement(&g_mdSeen);
        DRaw r{};
        if (!d || !dd_read_seh(d, &r)) { InterlockedIncrement(&g_mdRead); return; }
        if (r.atk >= 0x8000000u || r.atk != pl_id())
        {
            InterlockedIncrement(&g_mdOther);
            g_mdOtherAtk = (LONG)r.atk; g_mdOtherSrc = r.src;
            return;
        }
        if (r.ty == 10 || r.src == 19) { InterlockedIncrement(&g_mdBleed); return; }
        if (md_bullet(r.src)) { InterlockedIncrement(&g_mdBullet); return; }
        MRaw h;
        h.uid = (int32_t)g_cpU[slot]; h.r = r;
        AcquireSRWLockExclusive(&g_mdLock);
        if (g_mdQ.size() >= 32) { g_mdQ.erase(g_mdQ.begin()); InterlockedIncrement(&g_mdFull); }
        g_mdQ.push_back(h);
        ReleaseSRWLockExclusive(&g_mdLock);
        InterlockedIncrement(&g_mdKept);
        InterlockedIncrement(&g_mdBySrc[r.src < 48 ? r.src : 47]);
    }
    struct SRaw { uint32_t atk = 0; float dmg = 0; uint8_t ty = 0, src = 0; int64_t ms = 0; };
    SRWLOCK g_dsLock = SRWLOCK_INIT;
    std::vector<SRaw> g_dsQ;
    volatile LONG g_dsSeen = 0, g_dsBleed = 0, g_dsFull = 0;
    auto ds_capture(const uint8_t* d) -> void
    {
        InterlockedIncrement(&g_dsSeen);
        DRaw r{};
        if (!d || !dd_read_seh(d, &r)) return;
        if (r.ty == 10 || r.src == 19) { InterlockedIncrement(&g_dsBleed); return; }
        SRaw s;
        s.atk = r.atk; s.dmg = r.f[0]; s.ty = r.ty; s.src = r.src; s.ms = wall_ms();
        AcquireSRWLockExclusive(&g_dsLock);
        if (g_dsQ.size() >= 32) { g_dsQ.erase(g_dsQ.begin()); InterlockedIncrement(&g_dsFull); }
        g_dsQ.push_back(s);
        ReleaseSRWLockExclusive(&g_dsLock);
    }
    s2n_ds::Tracker g_dsLocal;
    auto ds_local_note(uint32_t id) -> void
    {
        int64_t now = wall_ms();
        AcquireSRWLockExclusive(&g_dsLock);
        g_dsLocal.note(id, now);
        ReleaseSRWLockExclusive(&g_dsLock);
    }
    auto ds_local_drop(const uint8_t* d) -> bool
    {
        DRaw r{};
        if (!d || !dd_read_seh(d, &r)) return false;
        int64_t now = wall_ms();
        AcquireSRWLockExclusive(&g_dsLock);
        bool hit = g_dsLocal.drop(r.atk, pl_id(), r.ty, r.src, now);
        ReleaseSRWLockExclusive(&g_dsLock);
        return hit;
    }
    volatile LONG g_dsDropped = 0;
    volatile LONG g_lhCause = 0;
    volatile LONG64 g_lhAt = 0;
    auto lh_note(uintptr_t e, float dmg, uint8_t ty, uint8_t src) -> void
    {
        if (e && dmg > 0 && dd_id_seh(e) == pl_id()) { InterlockedExchange64(&g_lhAt, wall_ms()); InterlockedExchange(&g_lhCause, 0x10000 | ty | (src << 8)); }
    }
    auto lh_note_seh(uintptr_t e, const uint8_t* d) -> void
    {
        __try { if (d) lh_note(e, *reinterpret_cast<const float*>(d), d[0x78], d[0x79]); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    volatile LONG g_cpFlinch = 0;
    auto cp_shrink_seh(uint8_t* d, float& keep) -> bool
    {
        __try
        {
            float* f = reinterpret_cast<float*>(d);
            if (!(*f > 0.f)) return false;
            keep = *f;
            *f = (std::min)(keep, 0.01f);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto cp_restore_seh(uint8_t* d, float keep) -> void
    {
        __try { *reinterpret_cast<float*>(d) = keep; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    void dd_detour(void* ent, void* dd, char p3)
    {
        uintptr_t e = reinterpret_cast<uintptr_t>(ent);
        if (t_killBypass > 0 && e && e == t_killEnt)
        {
            InterlockedIncrement(&g_killBypassHit);
            reinterpret_cast<DmgFn>(g_ddTramp)(ent, dd, p3);
            return;
        }
        if (e && g_dsOn && dd_id_seh(e) == pl_id())
        {
            ds_capture(reinterpret_cast<const uint8_t*>(dd));
            if (ds_local_drop(reinterpret_cast<const uint8_t*>(dd))) { InterlockedIncrement(&g_dsDropped); return; }
        }
        if (e && g_ffN > 0)
        {
            int i = ff_slot(e);
            if (i >= 0 && ff_capture(i, reinterpret_cast<const uint8_t*>(dd))) return;
        }
        if (e && e == g_ddTgt && C.natdmg && GetTickCount64() - g_ddUpdAt < 2000 && dd_id_seh(e) == g_ddTgtId) { dd_capture(reinterpret_cast<const uint8_t*>(dd)); return; }
        if (e && e == g_ddPup) InterlockedIncrement(&g_ddPupN);
        if (e && g_cpN > 0)
        {
            int cs = cp_slot(e);
            if (cs >= 0)
            {
                if (g_mdOn) md_capture(cs, reinterpret_cast<const uint8_t*>(dd));
                InterlockedIncrement(&g_cpSeen);
                if (g_cpOn == 2)
                {
                    float keep = 0.f;
                    if (cp_shrink_seh(reinterpret_cast<uint8_t*>(dd), keep))
                    {
                        InterlockedIncrement(&g_cpFlinch);
                        reinterpret_cast<DmgFn>(g_ddTramp)(ent, dd, p3);
                        cp_restore_seh(reinterpret_cast<uint8_t*>(dd), keep);
                        return;
                    }
                }
                if (g_cpOn) { InterlockedIncrement(&g_cpDrop); return; }
            }
        }
        lh_note_seh(e, reinterpret_cast<const uint8_t*>(dd));
        reinterpret_cast<DmgFn>(g_ddTramp)(ent, dd, p3);
    }
    auto dd_bone_ok(std::string& b) -> void
    {
        std::string o;
        for (char c : b) if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_') o += c;
        b = o.empty() ? std::string("None") : o;
    }
    auto dmg_host_str(int64_t wnow) -> std::string
    {
        if (!g_ddDet) return {};
        bool ndOk = s2x_nd_send_ok();
        std::string out;
        std::vector<std::string> logs;
        AcquireSRWLockExclusive(&g_ddLock);
        int64_t keep = (int64_t)(C.natdwin * 1000);
        while (!g_ddQ.empty() && wnow - g_ddQ.front().ms > keep) g_ddQ.erase(g_ddQ.begin());
        if (!C.natdmg || !ndOk) g_ddQ.clear();
        for (DHit& h : g_ddQ)
        {
            const DRaw& r = h.r;
            if (h.bone.empty()) { h.bone = r.bi ? w2u(FName(r.bi, 0u).ToString()) : std::string("None"); dd_bone_ok(h.bone); }
            char t[520];
            snprintf(t, sizeof t, "%d,%lld,%u,%.7g,%.7g,%.7g,%.7g,%.7g,%u,%u,%u,%u,%u,%.7g,%.7g,%.7g,%.7g,%.7g,%.7g,", h.seq, (long long)h.ms, r.atk,
                     r.f[0], r.f[1], r.f[2], r.f[3], r.f[4], (unsigned)r.ty, (unsigned)r.src, (unsigned)r.b7a, (unsigned)r.b7b, r.bn,
                     r.v[0], r.v[1], r.v[2], r.v[3], r.v[4], r.v[5]);
            if (!out.empty()) out += ";";
            out += t; out += h.bone;
            InterlockedIncrement(&g_ddSentN);
            if (!h.logged)
            {
                h.logged = true;
                s2x_nd_sent(r.f[0]);
                if (C.natlog && (++g_ddLogN <= 30 || g_ddLogN % 50 == 0))
                {
                    char b[300];
                    snprintf(b, sizeof b, "NOLA b206 natdmg (host): golpe no alvo do cliente seq=%d dano=%.2f armadura=%.2f perfuracao=%.2f sangra=%.2f/%.2f osso=%s tipo=%u fonte=%u atacante=0x%x (consumido, enviado) #%d",
                             h.seq, r.f[0], r.f[1], r.f[2], r.f[3], r.f[4], h.bone.c_str(), (unsigned)r.ty, (unsigned)r.src, r.atk, g_ddLogN);
                    logs.push_back(b);
                }
            }
        }
        ReleaseSRWLockExclusive(&g_ddLock);
        for (auto& l : logs) logl(l);
        return out.empty() ? out : "#ND:" + out;
    }
    auto dd_apply_seh(uint8_t* actor, const DRaw* a, uint32_t atk = kNoAtk) -> int
    {
        uintptr_t pf0 = s2n_pf::save();
        __try
        {
            uint8_t* ent = *reinterpret_cast<uint8_t**>(actor + 0x650);
            if (!ent) return 0;
            alignas(16) uint8_t bp[0x30] = {};
            std::memcpy(bp, a->f, 20);
            *reinterpret_cast<uint32_t*>(bp + 0x14) = atk;
            bp[0x20] = a->ty; bp[0x21] = a->src;
            alignas(16) uint8_t nd[0xa0] = {};
            reinterpret_cast<DdBuildFn>(g_ddBuild)(nd, bp);
            *reinterpret_cast<uint32_t*>(nd + 0x40) = a->bi;
            *reinterpret_cast<uint32_t*>(nd + 0x44) = a->bn;
            std::memcpy(nd + 0x48, a->v, 48);
            nd[0x7a] = a->b7a; nd[0x7b] = a->b7b;
            lh_note(reinterpret_cast<uintptr_t>(ent), a->f[0], a->ty, a->src);
            reinterpret_cast<DmgFn>(g_ddTramp)(ent, nd, 0);
            reinterpret_cast<DdDtorFn>(g_ddDtor)(nd);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return -1; }
    }
    auto dmg_client(const std::string& s, double now) -> void
    {
        if (now - g_dcPruneAt >= 1)
        {
            g_dcPruneAt = now;
            for (auto it = g_dcSeen.begin(); it != g_dcSeen.end();) { if (now - it->second > 5) it = g_dcSeen.erase(it); else ++it; }
        }
        if (s.empty()) return;
        UObject* me = K.me.real ? K.me.r.get() : nullptr;
        if (g_dcBudget < 0) { g_dcBudget = C.natdcap; g_dcBudAt = now; }
        g_dcBudget = std::min(C.natdcap, g_dcBudget + C.natdcap * std::max(0.0, now - g_dcBudAt)); g_dcBudAt = now;
        size_t i = 0;
        while (i < s.size())
        {
            size_t j = s.find(';', i);
            if (j == std::string::npos) j = s.size();
            std::string e = s.substr(i, j - i);
            i = j + 1;
            ++g_dcRx;
            double v[19]; int k = 0;
            const char* p = e.c_str();
            while (*p && k < 19)
            {
                char* q = nullptr;
                double x = std::strtod(p, &q);
                if (q == p) break;
                v[k++] = x; p = q;
                if (*p == ',') ++p; else break;
            }
            if (k != 19 || !*p) { ++g_dcBad; continue; }
            std::string bone = p;
            int sq = (int)v[0];
            if (g_dcSeen.count(sq)) { g_dcSeen[sq] = now; continue; }
            g_dcSeen[sq] = now;
            if (g_dcMax && sq > g_dcMax + 1 && sq - g_dcMax < 1000) g_dcLost += sq - g_dcMax - 1;
            if (sq > g_dcMax || g_dcMax - sq > 1000) g_dcMax = sq;
            ++g_dcNew;
            if (!g_ddBuild) { ++g_dcOff; continue; }
            if (!s2x_nd_apply_ok()) { ++g_dcOff; continue; }
            if (!me) { ++g_dcNoEnt; continue; }
            uintptr_t ma = reinterpret_cast<uintptr_t>(me);
            if (ma != g_dcMe) { g_dcMe = ma; g_dcProtUntil = now + C.natdspawn; }
            float hp0 = c_float(me, STR("GetHP"), 0.f);
            if (!alive_of(me) || hp0 <= 0) { ++g_dcDead; continue; }
            if (now < g_dcProtUntil) { ++g_dcProt; continue; }
            DRaw a{};
            a.atk = (uint32_t)v[2];
            for (int f = 0; f < 5; ++f) a.f[f] = (float)v[3 + f];
            a.ty = (uint8_t)v[8]; a.src = (uint8_t)v[9]; a.b7a = (uint8_t)v[10]; a.b7b = (uint8_t)v[11]; a.bn = (uint32_t)v[12];
            for (int f = 0; f < 6; ++f) a.v[f] = v[13 + f];
            double want = a.f[0];
            double got = std::min({want, C.natdmax, std::max(0.0, g_dcBudget)});
            if (got < want) { ++g_dcCap; a.f[0] = (float)got; }
            if (want > 0 && got <= 0) continue;
            g_dcBudget -= got;
            FName fn(u2w(bone).c_str(), FNAME_Add);
            a.bi = bone == "None" ? 0u : fn.GetComparisonIndex().ToUnstableInt();
            int r = dd_apply_seh(reinterpret_cast<uint8_t*>(me), &a);
            if (r < 0) { ++g_dcExc; continue; }
            if (r == 0) { ++g_dcNoEnt; continue; }
            float hp1 = c_float(me, STR("GetHP"), hp0);
            ++g_dcOk; g_dcSum += got; g_dcHp += hp0 - hp1;
            s2x_nd_applied(got, hp0, hp1);
            double age = (double)(wall_ms() - (int64_t)v[1]);
            g_dcAgeSum += age; g_dcAgeMax = std::max(g_dcAgeMax, age);
            if (C.natlog && (++g_dcLogN <= 30 || g_dcLogN % 50 == 0))
            {
                char b[300];
                snprintf(b, sizeof b, "NOLA b206 natdmg (cliente): seq=%d dano=%.2f%s armadura=%.2f perfuracao=%.2f osso=%s tipo=%u fonte=%u hp %.2f -> %.2f idade=%.0f ms #%d",
                         sq, got, got < want ? " (teto)" : "", a.f[1], a.f[2], bone.c_str(), (unsigned)a.ty, (unsigned)a.src, hp0, hp1, age, g_dcLogN);
                logl(b);
            }
        }
    }
    auto dmg_tick() -> void
    {
        ULONGLONG t = GetTickCount64();
        if (t - g_ddLogAt < 10000) return;
        g_ddLogAt = t;
        if (!C.natlog) return;
        if (g_ddTgtN || g_ddSentN || g_ddPupN)
        {
            double sum;
            AcquireSRWLockShared(&g_ddLock); sum = g_ddSumKept; ReleaseSRWLockShared(&g_ddLock);
            char b[400];
            snprintf(b, sizeof b, "NOLA b206 natdmg 10 s (host): natdmg=%d (%s) golpes_no_alvo=%ld (consumidos) guardados=%ld dano=%.1f entradas_enviadas=%ld sem_atacante=%ld tiro_host=%ld sangramento=%ld falha_leitura=%ld golpes_no_boneco=%ld",
                     C.natdmg, g_ddState, g_ddTgtN, g_ddKept, sum, g_ddSentN, g_ddNoAtk, g_ddHost, g_ddBleed, g_ddRead, g_ddPupN);
            logl(b);
        }
        if (g_dcRx)
        {
            char b[400];
            snprintf(b, sizeof b, "NOLA b206 natdmg 10 s (cliente): natdmg=%d (%s) entradas=%d novos=%d perdidos=%d aplicados=%d dano=%.1f hp_perdido=%.1f morto=%d protecao=%d teto=%d sem_pawn=%d desligado=%d mal_formados=%d excecao=%d idade med=%.0f max=%.0f ms",
                     C.natdmg, g_ddState, g_dcRx, g_dcNew, g_dcLost, g_dcOk, g_dcSum, g_dcHp, g_dcDead, g_dcProt, g_dcCap, g_dcNoEnt, g_dcOff, g_dcBad, g_dcExc,
                     g_dcOk ? g_dcAgeSum / g_dcOk : 0.0, g_dcAgeMax);
            logl(b);
        }
        if (g_cpSeen || g_cpMaxN || g_cpHpSet)
        {
            char b[360];
            snprintf(b, sizeof b, "NOLA natcpdmg 10 s (cliente): natcpdmg=%d copias_vivas_max=%ld golpes_nativos_nas_copias=%ld descartados=%ld reacao_sem_dano=%ld aplicados=%ld | natcphp=%d hp_espelhado=%ld ok=%ld falha=%ld erro_max_antes=%.1f",
                     g_cpOn, g_cpMaxN, g_cpSeen, g_cpDrop, g_cpFlinch, g_cpSeen - g_cpDrop - g_cpFlinch, g_cpHp, g_cpHpSet, g_cpHpOk, g_cpHpFail, g_cpHpErrMax);
            logl(b);
        }
        if (g_killGun || g_killFall || g_killBypassHit)
        {
            char b[160];
            snprintf(b, sizeof b, "NOLA natdeathfx 10 s (cliente): natdeathfx=%d gun=%ld fallback_apos_tiro=%ld bypass_dd=%ld", C.natdeathfx, g_killGun, g_killFall, (long)g_killBypassHit);
            logl(b);
        }
        g_killGun = g_killFall = 0;
        g_killBypassHit = 0;
        g_cpSeen = g_cpDrop = g_cpMaxN = g_cpFlinch = 0;
        g_cpHpSet = g_cpHpOk = g_cpHpFail = 0; g_cpHpErrMax = 0;
        g_ddTgtN = g_ddKept = g_ddNoAtk = g_ddHost = g_ddBleed = g_ddRead = g_ddPupN = g_ddSentN = 0;
        AcquireSRWLockExclusive(&g_ddLock); g_ddSumKept = 0; ReleaseSRWLockExclusive(&g_ddLock);
        g_dcRx = g_dcNew = g_dcLost = g_dcOk = g_dcDead = g_dcProt = g_dcCap = g_dcExc = g_dcNoEnt = g_dcOff = g_dcBad = 0;
        g_dcSum = g_dcHp = g_dcAgeSum = g_dcAgeMax = 0;
    }
    auto dmg_install(uint8_t* base) -> void
    {
        static const uint8_t kD[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0x88, 0x02, 0x00, 0x00};
        static const uint8_t kB[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0xF3, 0x0F, 0x10, 0x42, 0x10, 0x8B, 0x42, 0x14, 0x0F, 0xB7, 0x7A, 0x20};
        static const uint8_t kX[] = {0x56, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xCE, 0x80, 0xB9, 0x80, 0x00, 0x00, 0x00, 0x00};
        bool ok = mem_eq_seh(base + 0x6c4e86e, kB, sizeof kB) && mem_eq_seh(base + 0x25fcefa, kX, sizeof kX);
        if (ok && bul_hook(base + 0xc4c814, kD, sizeof kD, (void*)&dd_detour, g_ddTramp, g_ddDet))
        { g_ddBuild = base + 0x6c4e86e; g_ddDtor = base + 0x25fcefa; g_ddState = "on"; }
        else { g_ddState = ok ? "650bee falhou" : "bytes diferentes (exe novo?)"; C.natdmg = 0; }
        logl(std::string("NOLA b206 natdmg: damage entry FUN_140650bee ") + g_ddState);
    }
    struct StEnt { volatile uintptr_t ai; volatile LONG st; volatile ULONGLONG t; };
    constexpr int kStN = 128;
    StEnt g_stTab[kStN] = {};
    using OwnStFn = uint64_t (*)(void**);
    uint64_t g_stTramp = 0;
    PLH::x64Detour* g_stDet = nullptr;
    const char* g_stState = "nao instalado";
    volatile LONG g_stForced = 0, g_stSame = 0;
    using LookReqFn = void (*)(void*, uint8_t*);
    LookReqFn g_lkFn = nullptr;
    const char* g_lkState = "nao instalado";
    auto look_stop_raw(uint8_t* a, uint8_t* ai, uint8_t* lad) -> int
    {
        __try
        {
            uint8_t* U = *reinterpret_cast<uint8_t**>(a + 0xac8);
            if (reinterpret_cast<uintptr_t>(U) < 0x10000) return -1;
            uint8_t* lk = nullptr;
            for (int o = 0x500; o <= 0x700; o += 8)
                if (*reinterpret_cast<uint8_t**>(U + o + 8) == lad && *reinterpret_cast<uint8_t**>(U + o) == ai) { lk = U + o; break; }
            if (!lk) return -2;
            if (lad[0xa8] == 0 || lad[0x40] == 3) return 0;
            alignas(16) uint8_t req[0x68] = {};
            req[0] = 3;
            std::memcpy(req + 0x18, lad + 0x158, 24);
            req[0x4c] = 1;
            g_lkFn(lk, req);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -3; }
    }
    auto stance_set(UObject* ai, int st) -> void
    {
        auto k = reinterpret_cast<uintptr_t>(ai);
        ULONGLONG now = GetTickCount64();
        int fr = -1;
        for (int i = 0; i < kStN; ++i)
        {
            if (g_stTab[i].ai == k) { g_stTab[i].st = st; g_stTab[i].t = now; return; }
            if (fr < 0 && (g_stTab[i].ai == 0 || now - g_stTab[i].t > 2000)) fr = i;
        }
        if (fr < 0) return;
        g_stTab[fr].ai = 0; MemoryBarrier();
        g_stTab[fr].st = st; g_stTab[fr].t = now; MemoryBarrier();
        g_stTab[fr].ai = k;
    }
    uint64_t st_detour(void** ctx)
    {
        uint64_t r = reinterpret_cast<OwnStFn>(g_stTramp)(ctx);
        if (!C.natstance || !ctx) return r;
        uintptr_t k = reinterpret_cast<uintptr_t>(ctx[0]);
        if (!k) return r;
        for (int i = 0; i < kStN; ++i)
            if (g_stTab[i].ai == k)
            {
                LONG st = g_stTab[i].st;
                if (GetTickCount64() - g_stTab[i].t > 1000 || st < 1 || st > 12) return r;
                if ((r & 0xff) != (uint64_t)st) InterlockedIncrement(&g_stForced); else InterlockedIncrement(&g_stSame);
                return (r & ~0xffull) | (uint64_t)st;
            }
        return r;
    }
    auto stance_entries() -> int
    {
        ULONGLONG now = GetTickCount64(); int n = 0;
        for (int i = 0; i < kStN; ++i) n += (g_stTab[i].ai != 0 && now - g_stTab[i].t <= 1000);
        return n;
    }
    struct IvEnt { volatile uintptr_t c; volatile LONG st; volatile ULONGLONG t; };
    constexpr int kIvN = 64;
    IvEnt g_ivTab[kIvN] = {};
    volatile ULONGLONG g_ivLast = 0;
    using InvMgrFn = void (*)(void*, float, uint8_t*);
    uint64_t g_imTramp = 0;
    PLH::x64Detour* g_imDet = nullptr;
    const char* g_imState = "nao instalado";
    volatile LONG g_imHit = 0, g_imForced = 0, g_imWr = 0, g_imDead = 0, g_imExc = 0;
    using InvApplyFn = void (*)(void*);
    InvApplyFn g_iaFn = nullptr;
    const char* g_iaState = "nao instalado";
    int g_clSnap = 0, g_clRe = 0, g_clExc = 0, g_clRx = 0, g_clMaxErr = 0;
    auto cloak_apply_seh(uint8_t* ic, float v) -> int
    {
        __try
        {
            *reinterpret_cast<float*>(ic + 0xa8) = v;
            g_iaFn(ic);
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    auto cloak_rd_seh(const uint8_t* ic, float& cur, float& tgt) -> bool
    {
        __try { cur = *reinterpret_cast<const float*>(ic + 0xa8); tgt = *reinterpret_cast<const float*>(ic + 0xac); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto inv_set(uintptr_t c, int st) -> void
    {
        ULONGLONG now = GetTickCount64();
        g_ivLast = now;
        int fr = -1;
        for (int i = 0; i < kIvN; ++i)
        {
            if (g_ivTab[i].c == c) { g_ivTab[i].st = st; g_ivTab[i].t = now; return; }
            if (fr < 0 && (g_ivTab[i].c == 0 || now - g_ivTab[i].t > 2000)) fr = i;
        }
        if (fr < 0) return;
        g_ivTab[fr].c = 0; MemoryBarrier();
        g_ivTab[fr].st = st; g_ivTab[fr].t = now; MemoryBarrier();
        g_ivTab[fr].c = c;
    }
    auto inv_find(uintptr_t c) -> int
    {
        if (!c) return -1;
        ULONGLONG now = GetTickCount64();
        if (now - g_ivLast > 1000) return -1;
        for (int i = 0; i < kIvN; ++i)
            if (g_ivTab[i].c == c) { LONG st = g_ivTab[i].st; return now - g_ivTab[i].t > 1000 || st < 0 || st > 1 ? -1 : (int)st; }
        return -1;
    }
    auto inv_entries() -> int
    {
        ULONGLONG now = GetTickCount64(); int n = 0;
        for (int i = 0; i < kIvN; ++i) n += (g_ivTab[i].c != 0 && now - g_ivTab[i].t <= 1000);
        return n;
    }
    auto im_comp_seh(const uint8_t* mgr) -> uintptr_t
    {
        __try { return *reinterpret_cast<const uintptr_t*>(mgr + 0x28); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    auto im_apply_seh(const uint8_t* mgr, uint8_t* comp, int st) -> int
    {
        __try
        {
            const uint8_t* ad = *reinterpret_cast<uint8_t* const*>(mgr + 8);
            if (ad && ad[0x88] == 1) return 2;
            float t = s2nmab::inv_target(st);
            float* p = reinterpret_cast<float*>(comp + 0xac);
            if (*p == t) return 0;
            *p = t;
            return 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
    }
    void im_detour(uint8_t* mgr, float dt, uint8_t* req)
    {
        uintptr_t c = (C.natmab & 2) && mgr && GetTickCount64() - g_ivLast <= 1000 ? im_comp_seh(mgr) : 0;
        int st = c ? inv_find(c) : -1;
        if (st < 0) { reinterpret_cast<InvMgrFn>(g_imTramp)(mgr, dt, req); return; }
        InterlockedIncrement(&g_imHit);
        uint8_t q[2] = {(uint8_t)st, 1};
        if (req && req[1] && req[0] != q[0]) InterlockedIncrement(&g_imForced);
        reinterpret_cast<InvMgrFn>(g_imTramp)(mgr, dt, q);
        int r = im_apply_seh(mgr, reinterpret_cast<uint8_t*>(c), st);
        if (r == 1) InterlockedIncrement(&g_imWr);
        else if (r == 2) InterlockedIncrement(&g_imDead);
        else if (r < 0) InterlockedIncrement(&g_imExc);
    }
    using InvCntFn = uint32_t (*)(void*, void*);
    uint64_t g_ivTramp = 0;
    PLH::x64Detour* g_ivDet = nullptr;
    const char* g_ivState = "nao instalado";
    volatile LONG g_ivCalls = 0, g_ivGuard = 0, g_ivGuardTot = 0;
    volatile uintptr_t g_ivLastRet = 0;
    auto inv_unsafe(void* a) -> bool
    {
        if (!a) return false;
        __try
        {
            uintptr_t p = *reinterpret_cast<volatile uintptr_t*>(reinterpret_cast<uint8_t*>(a) + 0x650);
            if (!p) return true;
            (void)*reinterpret_cast<volatile uintptr_t*>(p + 0x678);
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }
    uint32_t iv_detour(void* actor, void* fstr)
    {
        InterlockedIncrement(&g_ivCalls);
        if (C.invnull && inv_unsafe(actor))
        {
            InterlockedIncrement(&g_ivGuard);
            InterlockedIncrement(&g_ivGuardTot);
            g_ivLastRet = reinterpret_cast<uintptr_t>(_ReturnAddress()) - reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            return reinterpret_cast<InvCntFn>(g_ivTramp)(nullptr, fstr);
        }
        return reinterpret_cast<InvCntFn>(g_ivTramp)(actor, fstr);
    }
    auto inv_install(uint8_t* base) -> void
    {
        static const uint8_t kIvA[] = {0x56, 0x57, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x89, 0xD6, 0x48};
        static const uint8_t kIvB[] = {0x31, 0xFF, 0x48, 0x85, 0xC9, 0x74, 0x41, 0x48, 0x8B, 0x81, 0x50, 0x06, 0x00, 0x00,
                                       0x48, 0x8B, 0x88, 0x78, 0x06, 0x00, 0x00, 0x48, 0x85, 0xC9, 0x74, 0x2E};
        uint8_t* iv = base + 0x6b07ee0;
        if (mem_eq_seh(iv, kIvA, sizeof kIvA) && mem_eq_seh(iv + 0x18, kIvB, sizeof kIvB))
        {
            g_ivDet = new x0::hot::Detour((uint64_t)iv, (uint64_t)&iv_detour, &g_ivTramp);
            if (g_ivDet->hook()) g_ivState = "on"; else { delete g_ivDet; g_ivDet = nullptr; g_ivTramp = 0; g_ivState = "FALHOU"; }
        }
        else g_ivState = "bytes diferentes (exe novo?)";
    }
    constexpr int kThN = 128;
    volatile uintptr_t g_thTab[kThN] = {};
    using ThrFn = void (*)(uint8_t*);
    using U8Fn = uint8_t (*)(uint8_t*);
    using HolFn = void (*)(uint8_t*);
    uint64_t g_thTramp = 0, g_hoTramp = 0;
    PLH::x64Detour* g_thDet = nullptr;
    PLH::x64Detour* g_hoDet = nullptr;
    U8Fn g_thSecond = nullptr;
    const char* g_thState = "nao instalado";
    const char* g_hoState = "nao instalado";
    volatile LONG g_thKeep = 0, g_thNat = 0;
    int g_hoSkip = 0, g_ssW = 0, g_ssClr = 0;
    auto thr_mark(uint8_t* S, bool on) -> void
    {
        auto k = reinterpret_cast<uintptr_t>(S);
        int fr = -1;
        for (int i = 0; i < kThN; ++i)
        {
            if (g_thTab[i] == k) { if (!on) g_thTab[i] = 0; return; }
            if (fr < 0 && g_thTab[i] == 0) fr = i;
        }
        if (on && fr >= 0) g_thTab[fr] = k;
    }
    auto thr_has(uint8_t* S) -> bool
    {
        auto k = reinterpret_cast<uintptr_t>(S);
        for (int i = 0; i < kThN; ++i) if (g_thTab[i] == k) return true;
        return false;
    }
    void th_detour(uint8_t* S)
    {
        if (C.natthrsrc && S && g_thSecond && thr_has(S))
        {
            bool kept = false;
            __try
            {
                if (S[0x1ad] == 1 && S[0x1ac] == 1)
                {
                    S[0x1af] = 0;
                    S[0x1ae] = g_thSecond(S);
                    S[0x1af] = 1;
                    kept = true;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { kept = false; }
            if (kept) { InterlockedIncrement(&g_thKeep); return; }
            InterlockedIncrement(&g_thNat);
        }
        reinterpret_cast<ThrFn>(g_thTramp)(S);
    }
    auto weak_get_seh(const uint8_t* w) -> UObject*
    {
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        __try
        {
            int idx = *reinterpret_cast<const int*>(w), ser = *reinterpret_cast<const int*>(w + 4);
            if (!ser || idx < 0 || idx >= *reinterpret_cast<int*>(base + 0xa0d49a4)) return nullptr;
            uint8_t* ch = reinterpret_cast<uint8_t**>(*reinterpret_cast<uint8_t**>(base + 0xa0d4990))[idx >> 16];
            if (!ch) return nullptr;
            uint8_t* it = ch + (size_t)(idx & 0xffff) * 0x18;
            if (*reinterpret_cast<int*>(it + 0x10) != ser || (*reinterpret_cast<uint32_t*>(it + 8) & 0x10200000)) return nullptr;
            return *reinterpret_cast<UObject**>(it);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    void ho_detour(uint8_t* ctx)
    {
        if (C.nathol && ctx && g_gtid != 0 && GetCurrentThreadId() == g_gtid)
            if (UObject* ag = weak_get_seh(ctx + 0x50); ag && is_ours(ag)) { ++g_hoSkip; return; }
        reinterpret_cast<HolFn>(g_hoTramp)(ctx);
    }
    struct CvEnt { volatile uintptr_t key; volatile uint8_t scp; uint8_t pad[7]; };
    CvEnt* g_cvT = nullptr;
    volatile LONG* g_cvHits = nullptr;
    const char* g_cvState = "nao instalado";
    auto cvt_key_off(uintptr_t k) -> void
    {
        if (!k || !g_cvT) return;
        for (int i = 0; i < kThN; ++i) if (g_cvT[i].key == k) g_cvT[i].key = 0;
    }
    auto cvt_clear_all() -> void
    {
        if (!g_cvT) return;
        for (int i = 0; i < kThN; ++i) g_cvT[i].key = 0;
    }
    auto cvt_mark(uintptr_t k, uint8_t scp) -> void
    {
        if (!k || !g_cvT) return;
        int fr = -1;
        for (int i = 0; i < kThN; ++i)
        {
            if (g_cvT[i].key == k) { g_cvT[i].scp = scp; return; }
            if (fr < 0 && g_cvT[i].key == 0) fr = i;
        }
        if (fr >= 0) { g_cvT[fr].scp = scp; g_cvT[fr].key = k; }
    }
    auto cov_scp(int act, int valid) -> uint8_t
    {
        if (!valid) return 0;
        uint8_t k = (uint8_t)(act - 1);
        return k < 8 ? (uint8_t)((0x0001000100010100ull >> (k * 8)) & 1) : 1;
    }
    auto cv_install(uint8_t* base) -> void
    {
        uint8_t* site = base + 0x2372df5;
        uint8_t* zero = base + 0x23712b8;
        uint8_t* store = base + 0x23712ba;
        uint8_t* cont = base + 0x2372e02;
        static const uint8_t kSite[] = {0x80, 0xB8, 0x31, 0x09, 0x00, 0x00, 0x00, 0x0F, 0x84, 0xB6, 0xE4, 0xFF, 0xFF, 0x8A, 0x88, 0x30, 0x09, 0x00, 0x00};
        static const uint8_t kStore[] = {0x31, 0xC0, 0x48, 0x8B, 0x4B, 0x08, 0x88, 0x81, 0x11, 0x01, 0x00, 0x00};
        if (!mem_eq_seh(site, kSite, sizeof kSite) || !mem_eq_seh(zero, kStore, sizeof kStore)) { g_cvState = "bytes diferentes (exe novo?)"; return; }
        uint8_t* st = ag_near_alloc(base, 0x1000);
        if (!st) { g_cvState = "sem memoria perto do exe"; return; }
        auto* T = reinterpret_cast<CvEnt*>(st + 0x100);
        for (int i = 0; i < kThN; ++i) { T[i].key = 0; T[i].scp = 0; }
        T[kThN].key = ~uintptr_t(0);
        auto* hits = reinterpret_cast<volatile LONG*>(st + 0xF0);
        *hits = 0;
        AgEmit e{st};
        e.b({0x48, 0x8D, 0x0D}); e.rel(reinterpret_cast<uint8_t*>(T));
        uint8_t* L = e.p;
        e.b({0x48, 0x83, 0x39, 0xFF});
        e.b({0x74}); uint8_t* jMiss = e.j8();
        e.b({0x48, 0x3B, 0x01});
        e.b({0x74}); uint8_t* jHit = e.j8();
        e.b({0x48, 0x83, 0xC1, 0x10});
        e.b({0xEB}); *e.p = (uint8_t)(int8_t)(L - (e.p + 1)); ++e.p;
        e.at(jHit);
        e.b({0xF0, 0xFF, 0x05}); e.rel(reinterpret_cast<uint8_t*>(const_cast<LONG*>(hits)));
        e.b({0x8A, 0x41, 0x08});
        e.b({0xE9}); e.rel(store);
        e.at(jMiss);
        e.b({0x80, 0xB8, 0x31, 0x09, 0x00, 0x00, 0x00});
        e.b({0x0F, 0x84}); e.rel(zero);
        e.b({0xE9}); e.rel(cont);
        FlushInstructionCache(GetCurrentProcess(), st, 0x1000);
        DWORD old = 0;
        if (!VirtualProtect(site, 13, PAGE_EXECUTE_READWRITE, &old)) { g_cvState = "VirtualProtect falhou"; return; }
        uint8_t buf[13];
        std::memset(buf, 0x90, sizeof buf);
        buf[0] = 0xE9;
        int32_t r = (int32_t)(st - (site + 5));
        std::memcpy(buf + 1, &r, 4);
        std::vector<uint8_t> orig(site, site + 13);
        g_cvT = T;
        g_cvHits = hits;
        std::memcpy(site, buf, 13);
        VirtualProtect(site, 13, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 13);
        x0::hot::on_shutdown([site, orig] {
            DWORD o = 0;
            if (!VirtualProtect(site, orig.size(), PAGE_EXECUTE_READWRITE, &o)) return;
            std::memcpy(site, orig.data(), orig.size());
            VirtualProtect(site, orig.size(), o, &o);
            FlushInstructionCache(GetCurrentProcess(), site, orig.size());
        });
        g_cvState = "on";
    }
    constexpr uintptr_t kCoverMachineOff = 0x720, kCoverEntityOff = 0x08, kCoverBackOff = 0x50;
    struct CoverCb { uint8_t* cb = nullptr; uintptr_t vtRva = 0; bool back = false; };
    auto cover_probe(UObject* a, uint8_t* S) -> CoverCb
    {
        __try
        {
            uint8_t* cb = *reinterpret_cast<uint8_t**>(S + 0x58);
            if (reinterpret_cast<uintptr_t>(cb) < 0x10000) return CoverCb{};
            uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
            uint8_t* ent = *reinterpret_cast<uint8_t**>(cb + kCoverMachineOff + kCoverEntityOff);
            return CoverCb{ cb, *reinterpret_cast<uintptr_t*>(cb) - base, reinterpret_cast<uintptr_t>(ent) >= 0x10000 && weak_get_seh(ent + kCoverBackOff) == a };
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return CoverCb{}; }
    }
    auto cover_cb(UObject* a, uint8_t* S) -> uint8_t*
    {
        CoverCb p = kCoverVtRva ? cover_probe(a, S) : CoverCb{};
        if (p.cb && p.vtRva == kCoverVtRva && p.back) return p.cb;
        ++g_covBad;
        return nullptr;
    }
    auto cover_learn(Ent& e, UObject* a, uint8_t* S) -> void
    {
        if (e.covLearned) return;
        CoverCb p = cover_probe(a, S);
        if (!p.cb) return;
        e.covLearned = true;
        char b[160];
        snprintf(b, sizeof b, "NOLA natcover: cb=%p vt_rva=0x%llx backlink_ok=%d", (void*)p.cb, (unsigned long long)p.vtRva, p.back ? 1 : 0);
        logl(b);
    }
    auto stsrc_set(Ent& e, UObject* a, uint8_t* S, int st) -> void
    {
        uint8_t* cb = cover_cb(a, S);
        if (!cb) return;
        __try
        {
            if (cb[0x9c1] != (uint8_t)st || cb[0x9c2] != 1) { cb[0x9c1] = (uint8_t)st; cb[0x9c2] = 1; ++g_ssW; }
            e.stSrc = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    auto cover_set(Ent& e, UObject* a, uint8_t* S) -> void
    {
        uint8_t* cb = cover_cb(a, S);
        if (!cb) return;
        __try
        {
            static const uint8_t z16[16] = {};
            if (std::memcmp(cb + 0x870, e.h.cov, 0xa0) != 0 || std::memcmp(cb + 0x860, z16, 16) != 0 || cb[0x910] != 1)
            {
                std::memset(cb + 0x860, 0, 16);
                std::memcpy(cb + 0x870, e.h.cov, 0xa0);
                cb[0x910] = 1;
                ++g_covW;
            }
            if (e.covKey && e.covKey != reinterpret_cast<uintptr_t>(cb)) cvt_key_off(e.covKey);
            e.covSrc = true;
            e.covKey = reinterpret_cast<uintptr_t>(cb);
            cvt_mark(e.covKey, cov_scp(e.h.covP, e.h.covV));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    auto cov_unmark(Ent& e) -> void
    {
        cvt_key_off(e.covKey);
        e.covKey = 0;
        e.covSrc = false;
    }
    int g_covMode = 0, g_covModeFail = 0, g_covHold = 0;
    auto cover_mode(Ent& e, UObject* a, bool on) -> void
    {
        UObject* cmc = a ? obj_at(a, STR("CharacterMovement")) : nullptr;
        if (!cmc) { if (!on) e.covMm0 = -1; return; }
        if (on)
        {
            auto* b = reinterpret_cast<uint8_t*>(cmc);
            int om = off_of(cmc, STR("MovementMode")), oc = off_of(cmc, STR("CustomMovementMode"));
            int mm = om >= 0 ? rd_u8_seh(b + om) : -1, cm = oc >= 0 ? rd_u8_seh(b + oc) : 0;
            if (mm < 0 || mm == 6) { e.covMm0 = 2; e.covCm0 = 0; }
            else { e.covMm0 = mm; e.covCm0 = cm < 0 ? 0 : cm; }
            Call c(cmc, STR("SetMovementMode"));
            c.i(STR("NewMovementMode"), 6).i(STR("NewCustomMode"), 4);
            c.run() ? ++g_covMode : ++g_covModeFail;
        }
        else
        {
            Call c(cmc, STR("SetMovementMode"));
            c.i(STR("NewMovementMode"), e.covMm0).i(STR("NewCustomMode"), e.covCm0);
            c.run() ? ++g_covMode : ++g_covModeFail;
            e.covMm0 = -1;
        }
    }
    auto cover_clear(Ent& e, UObject* a, uint8_t* S) -> void
    {
        if (!e.covSrc) return;
        cov_unmark(e);
        uint8_t* cb = cover_cb(a, S);
        if (!cb) return;
        __try { cb[0x910] = 0; ++g_covClr; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    auto cover_tick(Ent& e, UObject* a) -> bool
    {
        bool on = C.natcover && e.h.ext && e.h.covRecv == e.h.recv && !e.vend && e.how != 3;
        uint8_t* S = a ? ai_s_of(a) : nullptr;
        if (S) cover_learn(e, a, S);
        if (on && S)
        {
            cover_set(e, a, S);
            if (e.covSrc)
            {
                if (e.covMm0 < 0) cover_mode(e, a, true);
                return true;
            }
        }
        if (e.covSrc && S) cover_clear(e, a, S);
        if (e.covSrc || e.covKey) cov_unmark(e);
        if (e.covMm0 >= 0) cover_mode(e, a, false);
        return false;
    }
    auto stsrc_clear(Ent& e, UObject* a, uint8_t* S) -> void
    {
        if (!e.stSrc) return;
        e.stSrc = false;
        uint8_t* cb = cover_cb(a, S);
        if (!cb) return;
        __try { cb[0x9c2] = 0; ++g_ssClr; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    auto wpn_install() -> void
    {
        static bool tried = false;
        if (tried) return;
        tried = true;
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        static const uint8_t kTh[] = {0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0x80, 0xB9, 0xAD, 0x01, 0x00, 0x00, 0x00, 0x74,
                                      0x07, 0xC6, 0x86, 0xAD, 0x01, 0x00, 0x00, 0x00, 0x80, 0xBE, 0xAF, 0x01, 0x00, 0x00, 0x00, 0x74};
        static const uint8_t kThCall[] = {0x80, 0xBE, 0xAF, 0x01, 0x00, 0x00, 0x00};
        static const uint8_t kSec[] = {0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0x80, 0xB9, 0xAF, 0x01, 0x00, 0x00, 0x00, 0x74};
        uint8_t* th = base + 0x21c78c;
        if (mem_eq_seh(th, kTh, sizeof kTh) && mem_eq_seh(th + 0x40, kThCall, sizeof kThCall) && mem_eq_seh(base + 0x49482e, kSec, sizeof kSec))
        {
            g_thSecond = reinterpret_cast<U8Fn>(base + 0x49482e);
            g_thDet = new x0::hot::Detour((uint64_t)th, (uint64_t)&th_detour, &g_thTramp);
            if (g_thDet->hook()) g_thState = "on"; else { delete g_thDet; g_thDet = nullptr; g_thTramp = 0; g_thState = "FALHOU"; }
        }
        else g_thState = "bytes diferentes (exe novo?)";
        static const uint8_t kHo[] = {0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0x8B, 0x41, 0x54, 0x85, 0xC0, 0x0F, 0x84, 0xE8,
                                      0x00, 0x00, 0x00, 0x8B, 0x56, 0x50, 0x31, 0xC9, 0x85, 0xD2, 0x78, 0x4D, 0x39, 0x15, 0x28, 0x44,
                                      0x47, 0x09, 0x7E, 0x45, 0x44, 0x0F, 0xB7, 0xC2, 0x48, 0x8B, 0x0D, 0x07, 0x44, 0x47, 0x09};
        uint8_t* ho = base + 0xc6055a;
        if (mem_eq_seh(ho, kHo, sizeof kHo))
        {
            g_hoDet = new x0::hot::Detour((uint64_t)ho, (uint64_t)&ho_detour, &g_hoTramp);
            if (g_hoDet->hook()) g_hoState = "on"; else { delete g_hoDet; g_hoDet = nullptr; g_hoTramp = 0; g_hoState = "FALHOU"; }
        }
        else g_hoState = "bytes diferentes (exe novo?)";
        cv_install(base);
        logl(std::string("NOLA b191w: threat-cache writer detour ") + g_thState + ", holster tick detour " + g_hoState + ", cover pose gate exe+0x2372df5 " + g_cvState);
    }
    auto gen_install(uint8_t* base) -> void
    {
        static const uint8_t kPick[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xB8, 0x01, 0x00, 0x00, 0x0F, 0x29, 0xBC, 0x24, 0xA0};
        static const uint8_t kRed[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00, 0x48, 0x89, 0xD6};
        static const uint8_t kSid[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xC2, 0xE9};
        static const uint8_t kRes[] = {0x56, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x89, 0xCE, 0x85, 0xD2, 0x0F, 0x84, 0x88, 0x00, 0x00, 0x00};
        if (!mem_eq_seh(base + 0x854bbe, kPick, sizeof kPick) || !mem_eq_seh(base + 0x6898afe, kRed, sizeof kRed) ||
            !mem_eq_seh(base + 0x25db0e4, kSid, sizeof kSid) || !mem_eq_seh(base + 0x989d88, kRes, sizeof kRes))
        { g_genState = "bytes diferentes (exe novo?)"; return; }
        g_genSid = reinterpret_cast<GenSidFn>(base + 0x25db0e4);
        g_genResize = reinterpret_cast<GenResizeFn>(base + 0x989d88);
        g_genRedress = reinterpret_cast<GenRedressFn>(base + 0x6898afe);
        g_genTab = base + 0xb10f588;
        g_genDet = new x0::hot::Detour((uint64_t)(base + 0x854bbe), (uint64_t)&gen_pick_detour, &g_genTramp);
        if (g_genDet->hook()) g_genState = "on"; else { delete g_genDet; g_genDet = nullptr; g_genTramp = 0; g_genState = "FALHOU"; }
    }
    auto mont_install() -> void
    {
        static bool tried = false;
        if (tried) return;
        tried = true;
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        static const uint8_t kMp[] = {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xA8, 0x00, 0x00, 0x00, 0x44};
        static const uint8_t kMpCall[] = {0xE8, 0xD4, 0x09, 0x5D, 0x00};
        uint8_t* mp = base + 0x4d1476;
        if (mem_eq_seh(mp, kMp, sizeof kMp) && mem_eq_seh(mp + 0xe9, kMpCall, sizeof kMpCall))
        {
            g_mpDet = new x0::hot::Detour((uint64_t)mp, (uint64_t)&mp_detour, &g_mpTramp);
            if (g_mpDet->hook()) g_mpState = "on"; else { delete g_mpDet; g_mpDet = nullptr; g_mpTramp = 0; g_mpState = "FALHOU"; }
        }
        else g_mpState = "bytes diferentes (exe novo?)";
        static const uint8_t kMa[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xD8,
                                      0x00, 0x00, 0x00, 0x89, 0xD5, 0x48, 0x89, 0xCB};
        uint8_t* ma = base + 0xf31c40;
        if (mem_eq_seh(ma, kMa, sizeof kMa))
        {
            g_maDet = new x0::hot::Detour((uint64_t)ma, (uint64_t)&ma_detour, &g_maTramp);
            if (g_maDet->hook()) g_maState = "on"; else { delete g_maDet; g_maDet = nullptr; g_maTramp = 0; g_maState = "FALHOU"; }
        }
        else g_maState = "bytes diferentes (exe novo?)";
        static const uint8_t kSt[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0xCE, 0x48, 0x8B, 0x05, 0x67, 0x0F, 0xCA,
                                      0x09, 0x48, 0x31, 0xE0, 0x48, 0x89, 0x44, 0x24, 0x28, 0x48, 0x8B, 0x09, 0x8B, 0x81, 0x1C, 0x04};
        uint8_t* sf = base + 0x1e8b48;
        if (mem_eq_seh(sf, kSt, sizeof kSt))
        {
            g_stDet = new x0::hot::Detour((uint64_t)sf, (uint64_t)&st_detour, &g_stTramp);
            if (g_stDet->hook()) g_stState = "on"; else { delete g_stDet; g_stDet = nullptr; g_stTramp = 0; g_stState = "FALHOU"; }
        }
        else g_stState = "bytes diferentes (exe novo?)";
        static const uint8_t kLk[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xB8,
                                      0x03, 0x00, 0x00, 0x48, 0x89, 0xD3, 0x49, 0x89, 0xCE};
        uint8_t* lk = base + 0x222e62;
        if (mem_eq_seh(lk, kLk, sizeof kLk)) { g_lkFn = reinterpret_cast<LookReqFn>(lk); g_lkState = "on"; }
        else g_lkState = "bytes diferentes (exe novo?)";
        static const uint8_t kIm[] = {0x56, 0x57, 0x48, 0x83, 0xEC, 0x48, 0x0F, 0x29, 0x7C, 0x24, 0x30, 0x0F, 0x29, 0x74, 0x24, 0x20,
                                      0x48, 0x8B, 0x79, 0x28, 0x48, 0x85, 0xFF, 0x75, 0x11};
        uint8_t* im = base + 0x21df6a;
        if (mem_eq_seh(im, kIm, sizeof kIm))
        {
            g_imDet = new x0::hot::Detour((uint64_t)im, (uint64_t)&im_detour, &g_imTramp);
            if (g_imDet->hook()) g_imState = "on"; else { delete g_imDet; g_imDet = nullptr; g_imTramp = 0; g_imState = "FALHOU"; }
        }
        else g_imState = "bytes diferentes (exe novo?)";
        static const uint8_t kIa[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x56, 0x57, 0x53, 0x48, 0x81, 0xEC, 0x08, 0x01, 0x00, 0x00,
                                      0x0F, 0x29, 0xBC, 0x24, 0xF0, 0x00, 0x00, 0x00};
        static const uint8_t kIaChk[] = {0x48, 0x83, 0xB9, 0x98, 0x00, 0x00, 0x00, 0x00};
        static const uint8_t kIaCur[] = {0xF3, 0x0F, 0x10, 0x8E, 0xA8, 0x00, 0x00, 0x00};
        uint8_t* ia = base + 0xe939e2;
        if (mem_eq_seh(ia, kIa, sizeof kIa) && mem_eq_seh(ia + 0x35, kIaChk, sizeof kIaChk) && mem_eq_seh(ia + 0x54, kIaCur, sizeof kIaCur))
        { g_iaFn = reinterpret_cast<InvApplyFn>(ia); g_iaState = "on"; }
        else g_iaState = "bytes diferentes (exe novo?)";
        bul_install(base);
        dmg_install(base);
        gen_install(base);
        inv_install(base);
        gs_install(base);
        wp_install(base);
        lu_install(base);
        tb_install(base);
        nw_install(base);
        logl(std::string("NOLA natnavw: posicionador NavWalking assincrono 0x2167e2a ") + g_nwState + ", copia com posicao do host usa o reset de teleporte do jogo (CMC+0x12c8) e nao e movida pelo navmesh");
        logl(std::string("NOLA natloco: atualizador de locomocao 0x2397454 + tags 0x53671c/0x53677a ") + g_luState);
        logl(std::string("NOLA nattraj: construtor de trajetoria PoseSearch 0x237106c ") + g_tbState);
        logl(std::string("NOLA natgaitcp: seletor de marcha 0x239895c ") + g_gsState);
        logl(std::string("NOLA natwpnlocal: ChangeMainHandWeapon wrapper 0xc60660 + impl 0xc60752 ") + g_wpState);
        logl(std::string("NOLA b191 montnull: Montage_Play guard ") + g_mpState + ", MakeAttack detour " + g_maState + ", natstance " + g_stState +
             ", b195 invnull GetItemsCountInInventory guard " + g_ivState + ", b191g natgen pick detour " + g_genState + ", a1 look request " + g_lkState + ", ma1 invisibilidade FUN_14043f3c2 " + g_imState + ", i16d1 natcloak aplicar material 0xe939e2 " + g_iaState);
    }

    auto rebuild_index(double now) -> void
    {
        std::vector<UObject*> all;
        cls_all(all);
        ++K.idxN;
        K.idxList.clear();
        K.byUid.clear();
        K.byQg.clear();
        for (UObject* o : all)
        {
            if (!o) continue;
            K.idxList.insert(o);
            auto& e = K.idx[o];
            FName nm = o->GetNamePrivate();
            if (!(e.nm == nm) || e.seen == 0) { e.nm = nm; e.uid = guid_of(o); e.pid = pid_of(o); e.ci = cls_of(o); e.qgN = 0; e.qgAt = -100; e.qg.clear(); }
            if (C.natqg && e.qg.empty() && e.qgN < 8 && now - e.qgAt >= 2.0)
            {
                e.qgAt = now;
                double qx = 0, qy = 0, qz = 0;
                bool hasLoc = loc_of(o, qx, qy, qz);
                if (C.natqgsafe && (!hasLoc || pooled(qx, qy))) ++g_qgSkip;
                else
                {
                    ++e.qgN;
                    bool thr = false;
                    e.qg = qg_of(o, &thr);
                    if (thr)
                    {
                        ++g_qgThrow;
                        if (C.natqgsafe) e.qgN = 8;
                        char qb[200];
                        snprintf(qb, sizeof qb, "%s vivo=%d pool=%d x=%.0f y=%.0f", w2u(o->GetName()).c_str(), alive_of(o) ? 1 : 0, hasLoc && pooled(qx, qy) ? 1 : 0, qx, qy);
                        g_qgLast = qb;
                    }
                }
            }
            e.seen = now;
            if (e.uid) K.byUid[e.uid] = o;
            if (C.natqg && !e.qg.empty()) K.byQg[e.qg] = o;
        }
        for (auto it = K.idx.begin(); it != K.idx.end();) { if (now - it->second.seen > 10) it = K.idx.erase(it); else ++it; }
        for (auto it = K.rowQg.begin(); it != K.rowQg.end();) it = K.E.count(it->second) ? std::next(it) : K.rowQg.erase(it);
    }

    auto rel_zero(Ent& e, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        if (e.relMe == me && e.relPup == pup && e.relTgt == tgt && e.relGen == K.plGen) return;
        std::vector<UObject*> os{me, pup, tgt};
        for (const auto& pr : K.pl) os.push_back(pr.second);
        for (UObject* o : os)
        {
            if (!o) continue;
            int32_t u = guid_of(o);
            if (u) { set_relation(e.luid, u, 0); set_relation(u, e.luid, 0); }
        }
        e.relMe = me; e.relPup = pup; e.relTgt = tgt; e.relGen = K.plGen;
    }

    auto rm_off(Ent& e, UObject* a, double now, bool force) -> void;
    int g_lsFar = 0, g_lsFarSkip = 0, g_lsBack = 0;
    auto far_r(bool dead) -> double { return s2nmc::far_keep_r(dead, cli_r(), C.natfardead); }
    auto me_dist(UObject* me, double x, double y) -> double
    {
        double mx = 0, my = 0, mz = 0;
        return me && loc_of(me, mx, my, mz) ? std::hypot(x - mx, y - my) : -1.0;
    }

    auto take(Ent& e, UObject* a, int how, double now, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        e.body.set(a);
        K.own.insert(reinterpret_cast<uintptr_t>(a));
        e.luid = guid_of(a);
        e.how = how; e.st = LIVE; e.takeAt = now; e.shown = false; e.lookDone = 0; e.mism = -1; e.reapply = 0;
        e.hh = half_height(a);
        e.ordAt = -100; e.ogait = 0; e.crouch = e.aim = false; e.killTry = 0;
        e.relMe = e.relPup = e.relTgt = nullptr;
        e.vend = (e.h.fl & 512) != 0;
        e.catchUp = false; e.lostDead = false; e.latAt = -1; e.ordN = 0;
        if (e.farLost) { e.farLost = false; ++g_lsBack; }
        e.mDone[0] = e.mDone[1] = 0; e.eqAt = e.eqChg = -100; e.wpnChk = now + 1.0; e.shAt = now; e.lsDone = -1; e.ows = -1; e.cbPrev = e.h.cb;
        e.eqWant = e.eqDead = -1; e.eqSeen = -2; e.lg = LegSt{};
        e.mDefer[0] = e.mDefer[1] = 0; e.turnAt = e.turnTry = -100; e.yawHoldAt = -1; e.turnOk = false; e.lgait = 0;
        int sh = 0;
        bool uk = false;
        if (how == 3)
        {
            e.shown = true;
            if (!e.vend) ai_on(a);
        }
        else
        {
            if (how == 4) { e.shown = true; e.brain0 = brain_st(a); }
            else set_hidden(a, true);
            if (!e.vend) { rel_zero(e, me, pup, tgt); sh = shooter_off(a); uk = unkillable(e.luid, true); }
            ai_on(a);
            if (how != 4) teleport(a, e.h.x, e.h.y, e.h.zg + e.hh, e.h.yaw);
        }
        e.sh0 = sh; e.uk0 = uk; e.mws0 = -1;
        e.rmAt = -100; e.pinHit = 0; e.rmPin = false;
        rm_off(e, a, now, true);
        if (how == 4) ++K.keptFull;
        ++K.takes; how == 2 ? ++K.spawns : ++K.adopt;
        bool qa = how != 2 && e.qgAd;
        if (qa) { ++K.qgAdopt; ++K.qgAdoptW; }
        char b[420];
        snprintf(b, sizeof b, "NOLA natnpc: copia de host uid=%d (_%s) %s: %s uid_local=%d meia_altura=%.0f gatilho_off=%d imortal=%s%s%s%s%s%s",
                 e.h.uid, e.h.nm.c_str(), how == 1 ? "ADOTADA (mesma uid)" : (how == 3 ? "ADOTADA VISIVEL (mesma uid, b162 suave)" : (how == 4 ? "ADOTADA VISIVEL CONTROLE TOTAL (mesma uid, b169)" : "CRIADA (spawn)")),
                 w2u(a->GetName()).c_str(), e.luid, e.hh, sh, how == 3 || e.vend ? "nao mexido" : (uk ? "ok" : "falhou"), e.vend ? " VENDEDOR" : "",
                 (e.h.fl & 256) ? " aparencia_fixa" : "", qa ? " | natqg: mesmo guid de quest " : "", qa ? e.h.qg.c_str() : "", qa ? " (o corpo de quest local tem outra uid: ele e a copia)" : "");
        logl(b);
    }

    int g_lsNull = 0, g_lsPool = 0, g_lsKill = 0, g_lsGive = 0, g_lsLog = 0, g_lsMax = 0;
    int g_pdNear = 0, g_pdCls = 0, g_pdHp = 0, g_pdDead = 0, g_pdWorld = 0, g_pdTo = 0;
    int g_eqNoFx = 0, g_eqNoFxLog = 0, g_eqOwnLog = 0, g_eqOwnFlag = -1, g_turnLog = 0;
    std::unordered_set<int32_t> g_eqStuck;
    int g_kF = 0, g_kW = 0, g_kS = 0, g_kFail = 0, g_kTake = 0, g_kRel = 0, g_kMaxN = 0;
    int g_kPin = 0, g_kPinFail = 0, g_kNoAnim = 0, g_kEqNoAnim = 0, g_mEqHold = 0;
    double g_kUs = 0, g_kUsMax = 0, g_kLast = -1;
    bool g_kAny = false;
    auto wr_v3_seh(uint8_t* p, double x, double y, double z) -> bool
    { __try { auto* q = reinterpret_cast<double*>(p); q[0] = x; q[1] = y; q[2] = z; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
    int g_mbRoot = 0, g_mbRootOff = 0;
    auto root_lock(Ent& e, UObject* a, bool on) -> void
    {
        if (!on && e.rms0 < 0) return;
        int o = a ? off_of(a, STR("AnimRootMotionTranslationScale")) : -1;
        if (o < 0) { e.rms0 = -1; return; }
        auto* b = reinterpret_cast<uint8_t*>(a) + o;
        if (on)
        {
            if (e.rms0 < 0) e.rms0 = rd_f_seh(b);
            if (rd_f_seh(b) != 0.f && wr_f_seh(b, 0.f)) ++g_mbRoot;
        }
        else { if (wr_f_seh(b, e.rms0)) ++g_mbRootOff; e.rms0 = -1; }
    }
    int g_rmN = 0, g_rmHit = 0, g_rmLog = 0;
    auto rm_off(Ent& e, UObject* a, double now, bool force) -> void
    {
        if (!C.natrmoff || e.vend || e.how == 3 || !a) return;
        if (!force && now - e.rmAt < 1.0) return;
        e.rmAt = now;
        e.rmPin = true;
        root_lock(e, a, true);
        if (UObject* mw = obj_at(a, STR("MotionWarpingComponent")))
        {
            Call r(mw, STR("RemoveAllWarpTargets"));
            r.run();
            Call d(mw, STR("DisableAllRootMotionModifiers"));
            d.run();
        }
        ++g_rmN;
    }
    auto kin_release(Ent& e) -> void
    {
        if (!e.kOn) return;
        e.kOn = false; e.kAt = -1; e.kmt = -1; ++g_kRel;
        UObject* a = e.body.get();
        if (!e.rmPin) root_lock(e, a, false);
        if (!a) { e.kmws0 = -1; return; }
        if (UObject* cmc = obj_at(a, STR("CharacterMovement")))
        {
            auto* b = reinterpret_cast<uint8_t*>(cmc);
            int o = off_of(cmc, STR("MaxWalkSpeed"));
            if (o >= 0 && e.kmws0 > 0) wr_f_seh(b + o, e.kmws0);
            int ob = off_of(cmc, STR("bHasRequestedVelocity"));
            if (ob >= 0) { int v = rd_u8_seh(b + ob); if (v >= 0) wr_u8_seh(b + ob, v & ~1); }
        }
        e.kmws0 = -1;
    }
    auto kin_target(const Ent& e, double rt, double& x, double& y, double& z, double& yaw, double& vx, double& vy) -> bool
    {
        if (e.kn <= 0) return false;
        const Ent::KSmp* s = e.ks;
        if (rt <= s[0].t) { x = s[0].x; y = s[0].y; z = s[0].zg; yaw = s[0].yaw; vx = s[0].vx; vy = s[0].vy; return true; }
        for (int i = e.kn - 1; i > 0; --i)
            if (s[i - 1].t <= rt && rt <= s[i].t)
            {
                const Ent::KSmp& a = s[i - 1]; const Ent::KSmp& b = s[i];
                double dt = std::max(0.001, b.t - a.t), k = (rt - a.t) / dt;
                x = a.x + (b.x - a.x) * k; y = a.y + (b.y - a.y) * k; z = a.zg + (b.zg - a.zg) * k;
                yaw = a.yaw + std::remainder(b.yaw - a.yaw, 360.0) * k;
                vx = (b.x - a.x) / dt; vy = (b.y - a.y) / dt;
                return true;
            }
        const Ent::KSmp& b = s[e.kn - 1];
        double ex = std::min(rt - b.t, C.natkinext / 1000.0);
        x = b.x + b.vx * ex; y = b.y + b.vy * ex; z = b.zg; yaw = b.yaw; vx = b.vx; vy = b.vy;
        return true;
    }

    auto host_hid(const Ent& e) -> bool { return C.npcvis && (e.h.fl & kHostHid) != 0; }
    auto vis_log(const Ent& e, const char* what) -> void
    {
        ++g_visN;
        if (g_visN > 40 && g_visN % 50 != 0) return;
        char b[300];
        snprintf(b, sizeof b, "NOLA npcvis: copy of host uid=%d (_%s) how=%d %s #%d", e.h.uid, e.h.nm.c_str(), e.how, what, g_visN);
        logl(b);
    }
    auto release(Ent& e, const char* why, double now) -> void
    {
        kin_release(e);
        UObject* a = e.body.get();
        if (e.rmPin) { e.rmPin = false; root_lock(e, a, false); }
        e.rmAt = -100; e.pinHit = 0;
        if (a && e.frcThr) { if (uint8_t* S = ai_s_of(a)) { thr_mark(S, false); stsrc_clear(e, a, S); wr_u8_seh(S + 0x1ad, 0); } }
        e.frcThr = false;
        if (a && e.covSrc) { if (uint8_t* S = ai_s_of(a)) cover_clear(e, a, S); }
        cov_unmark(e);
        if (a && e.covMm0 >= 0) cover_mode(e, a, false);
        e.covMm0 = -1;
        if (a && e.how == 4)
        {
            unkillable(e.luid, false);
            shooter_on(a);
            ai_on(a);
            if (e.crouch) state_tag(e.luid, 1024, false);
            if (e.aim) state_tag(e.luid, 8192, false);
            e.crouch = e.aim = false;
            if (e.mws0 > 0)
                if (UObject* cmc = prop_obj(a, STR("CharacterMovement")))
                {
                    int o = off_of(cmc, STR("MaxWalkSpeed"));
                    if (o >= 0) wr_f_seh(reinterpret_cast<uint8_t*>(cmc) + o, e.mws0);
                }
            if (UObject* bc = brain_of(a)) if (!c_bool(bc, STR("IsRunning"), true)) { Call c(bc, STR("RestartLogic")); c.run(); }
            K.own.erase(reinterpret_cast<uintptr_t>(a));
            ++K.keptRel;
        }
        else if (a && e.how == 3) K.own.erase(reinterpret_cast<uintptr_t>(a));
        else if (a)
        {
            if (!e.vend) unkillable(e.luid, false);
            std::string wq = e.how == 2 && C.natqg ? qg_of(a) : std::string();
            if (!wq.empty())
            {
                ++K.qgKeep;
                logl("NOLA natqg: corpo " + w2u(a->GetName()) + " uid_local=" + std::to_string(e.luid) + " tem guid de quest " + wq + ": e um ator de quest do mundo, nao uma copia nossa -> nao destruido, devolvido ao jogo");
            }
            if (e.how == 2 && wq.empty())
            {
                cc_uid(g_ai2, STR("CustomConsoleManagerAI2"), STR("XDestroyModel"), STR("UID"), e.luid);
                Grave g; g.r.set(a); g.luid = e.luid; g.at = now; g.step = 0;
                K.graves.push_back(std::move(g));
            }
            else
            {
                set_hidden(a, true);
                K.own.erase(reinterpret_cast<uintptr_t>(a));
            }
        }
        else if (e.body.raw) K.own.erase(reinterpret_cast<uintptr_t>(e.body.raw));
        if (a && C.natdeadpos && e.st == DEAD && (e.how == 3 || e.how == 4) && !s2nmc::released_dead_visible(e.how, true)) set_hidden(a, true);
        char b[200];
        snprintf(b, sizeof b, "NOLA natnpc: copia de host uid=%d (_%s) solta: %s (%s)", e.h.uid, e.h.nm.c_str(), why, e.how == 2 ? "XDestroyModel" : (e.how == 3 ? "fica visivel onde esta" : (e.how == 4 ? "fica visivel onde esta, controle devolvido (b169)" : "escondida de volta")));
        if (e.st == LIVE || e.st == DEAD) logl(b);
        cov_unmark(e);
        e.body.reset(); e.st = NONE; e.shown = false; e.luid = 0;
    }

    auto dead_remake(Ent& e, UObject* a, double dch, double now) -> void
    {
        int how = e.how;
        ++e.deadRm; ++K.deadRemade;
        char b[300];
        snprintf(b, sizeof b, "NOLA natdeadpos: copia morta host uid=%d (_%s) how=%d fora do corpo do host: dist_corpo_host=%.0f tol=%.0f -> escondida e refeita no corpo do host (#%d)", e.h.uid, e.h.nm.c_str(), how, dch, 4.0 * e.hh, K.deadRemade);
        logl(b);
        set_hidden(a, true);
        release(e, "natdeadpos: corpo longe do corpo do host", now);
        if (how != 2)
        {
            K.own.insert(reinterpret_cast<uintptr_t>(a));
            Grave g; g.r.set(a); g.luid = 0; g.at = now; g.step = 1;
            K.graves.push_back(std::move(g));
        }
        e.takeAt = 0; e.hgtLog = 0; e.failUntil = 0;
    }

    auto dead_out(Ent& e, UObject* a, double now) -> void
    {
        int how = e.how;
        int32_t lu = e.luid;
        vis_log(e, "dead on the host but still alive here after 2 XKillNpcByUID: hidden and XDestroyModel");
        set_hidden(a, true);
        interaction_off(a);
        release(e, "host NPC dead, local body still alive (npcvis)", now);
        if (how == 2 || !lu) return;
        cc_uid(g_ai2, STR("CustomConsoleManagerAI2"), STR("XDestroyModel"), STR("UID"), lu);
        Grave g; g.r.set(a); g.luid = lu; g.at = now; g.step = 0;
        K.own.insert(reinterpret_cast<uintptr_t>(a));
        K.graves.push_back(std::move(g));
    }

    auto graves_tick(double now) -> void
    {
        for (auto it = K.graves.begin(); it != K.graves.end();)
        {
            Grave& g = *it;
            UObject* a = g.r.get();
            double x = 0, y = 0, z = 0;
            bool gone = !a || (loc_of(a, x, y, z) && pooled(x, y)) ;
            if (gone) { ++K.removed; if (g.r.raw) K.own.erase(reinterpret_cast<uintptr_t>(g.r.raw)); it = K.graves.erase(it); continue; }
            if (g.step == 0 && now - g.at >= 3)
            {
                g.step = 1;
                if (alive_of(a)) cc_uid(g_dc, STR("CustomConsoleManagerDC"), STR("XKillNpcByUID"), STR("NpcUID"), g.luid);
                set_hidden(a, true);
                interaction_off(a);
            }
            if (g.step == 1 && now - g.at >= 10)
            { ++K.removed; K.own.erase(reinterpret_cast<uintptr_t>(g.r.raw)); it = K.graves.erase(it); continue; }
            ++it;
        }
    }

    auto park_hold(UObject* a, int32_t luid, double& at, double now) -> void
    {
        if (!a || !luid || now - at < std::max(0.5, C.natkeepc)) return;
        double x, y, z;
        if (!loc_of(a, x, y, z) || pooled(x, y)) return;
        at = now;
        int r = s2n_moveto_native((uint32_t)luid, x, y, z - half_height(a), 1, (float)C.natsucc, 24, 3, 1);
        r == 0 ? ++K.parkHold : ++K.parkHoldFail;
    }
    long g_dsCat[10] = {};
    const char* const kDsCat[10] = {"sem_atacante", "eu_mesmo", "copia", "inerte", "boneco", "alvo", "local_escondido", "local_visivel", "sem_pawn", "outro"};
    long g_dsLogN = 0;
    double g_inLogAt = 0;
    auto ds_pawns(std::unordered_map<uint32_t, UObject*>& by) -> void
    {
        std::vector<UObject*> all;
        UObjectGlobals::FindAllOf(STR("Pawn"), all);
        for (UObject* o : all)
        {
            uint32_t id = 0xFFFFFFFFu;
            if (dd_ent_seh(o, &id) && id < 0x8000000u) by[id] = o;
        }
    }
    auto ds_cat(uint32_t atk, const std::unordered_map<uint32_t, UObject*>& by, UObject*& o) -> int
    {
        auto it = by.find(atk);
        o = it == by.end() ? nullptr : it->second;
        uintptr_t a = reinterpret_cast<uintptr_t>(o);
        bool ours = o && is_ours(o), inert = o && g_in.count(a) != 0, pup = o && a == g_inPup, tgt = o && a == g_inTgt;
        bool hid = o && !ours && !inert && !pup && !tgt && prop_bool(o, STR("bHidden"), false);
        return s2n_ds::classify(atk, pl_id(), o != nullptr, ours, inert, pup, tgt, hid);
    }
    auto ds_refresh() -> void
    {
        static double at = 0;
        double now = now_s();
        if (!g_dsOn || now - at < 1.0) return;
        at = now;
        std::unordered_map<uint32_t, UObject*> by;
        ds_pawns(by);
        for (auto& kv : by)
        {
            UObject* o = nullptr;
            if (s2n_ds::is_local(ds_cat(kv.first, by, o)) && alive_of(o)) ds_local_note(kv.first);
        }
    }
    auto ds_tick() -> void
    {
        ds_refresh();
        std::vector<SRaw> q;
        AcquireSRWLockExclusive(&g_dsLock);
        q.swap(g_dsQ);
        ReleaseSRWLockExclusive(&g_dsLock);
        if (q.empty()) return;
        std::unordered_map<uint32_t, UObject*> by;
        bool need = false;
        for (const SRaw& s : q) if (s.atk < 0x8000000u && s.atk != pl_id()) need = true;
        if (need) ds_pawns(by);
        for (const SRaw& s : q)
        {
            UObject* o = nullptr;
            int c = ds_cat(s.atk, by, o);
            ++g_dsCat[c];
            if (s2n_ds::is_local(c)) ds_local_note(s.atk);
            if (!C.natlog || (++g_dsLogN > 40 && g_dsLogN % 50 != 0)) continue;
            std::string nm = "-", cl = "-";
            int alive = -1;
            if (o) { try { nm = w2u(o->GetName()); if (UClass* k = o->GetClassPrivate()) cl = w2u(k->GetName()); alive = alive_of(o) ? 1 : 0; } catch (...) {} }
            char b[420];
            snprintf(b, sizeof b, "NOLA clinert dmgsrc: golpe no jogador local dano=%.2f tipo=%u fonte=%u atacante=0x%x categoria=%s ator=%s classe=%s vivo=%d #%ld",
                     s.dmg, (unsigned)s.ty, (unsigned)s.src, s.atk, kDsCat[c], nm.c_str(), cl.c_str(), alive, g_dsLogN);
            logl(b);
        }
    }
    auto in_tick() -> void
    {
        double now = now_s();
        static double at = 0;
        if (now - at >= 0.25)
        {
            at = now;
            for (auto it = g_in.begin(); it != g_in.end();)
            {
                UObject* a = it->second.r.get();
                if (!g_inOn || !a || is_ours(a) || !alive_of(a)) { ++g_inDrop; it = g_in.erase(it); continue; }
                InE& e = it->second;
                if (!e.uid && !e.asked) { e.asked = true; e.uid = std::max(0, guid_of(a)); }
                double x, y, z;
                if (e.uid && now - e.at >= std::max(0.5, C.natkeepc) && loc_of(a, x, y, z) && !pooled(x, y))
                {
                    e.at = now;
                    int r = s2n_moveto_native((uint32_t)e.uid, x, y, z - half_height(a), 1, (float)C.natsucc, 24, 3, 1);
                    r == 0 ? ++g_inHoldOk : ++g_inHoldFail;
                }
                ++it;
            }
        }
        if (now - g_inLogAt < 10) return;
        g_inLogAt = now;
        long hits = 0;
        for (long v : g_dsCat) hits += v;
        if (!g_inOn && !g_dsOn) return;
        if (!C.natlog || (!hits && !g_inMaIn && !g_inMaHid && !g_inAdd && !g_inDrop && !g_dsBleed)) return;
        std::string cs;
        for (int i = 0; i < 10; ++i) if (g_dsCat[i]) cs += std::string(" ") + kDsCat[i] + "=" + std::to_string(g_dsCat[i]);
        char b[600];
        snprintf(b, sizeof b, "NOLA clinert 10 s: clinert=%d cldmgsrc=%d inertes=%zu novos=%ld largados=%ld segurar_ok=%ld falha=%ld ataques_cortados inerte=%ld escondido=%ld | golpes_no_jogador=%ld sangramento=%ld fila_cheia=%ld descartados_locais=%ld%s",
                 g_inOn, g_dsOn, g_in.size(), g_inAdd, g_inDrop, g_inHoldOk, g_inHoldFail, g_inMaIn, g_inMaHid, hits, (long)g_dsBleed, (long)g_dsFull, (long)InterlockedExchange(&g_dsDropped, 0), cs.c_str());
        logl(b);
        g_inAdd = g_inDrop = g_inHoldOk = g_inHoldFail = g_inMaIn = g_inMaHid = 0;
        g_dsBleed = g_dsFull = g_dsSeen = 0;
        for (long& v : g_dsCat) v = 0;
    }
    auto inert_api(uintptr_t a, int32_t uid, int on) -> bool
    {
        if (!a) return false;
        if (!on) { if (g_in.erase(a)) ++g_inDrop; return true; }
        if (!g_inOn || !UObject::IsReal(reinterpret_cast<void*>(a))) return false;
        UObject* o = reinterpret_cast<UObject*>(a);
        if (is_ours(o) || a == g_inMe || a == g_inPup) return false;
        auto it = g_in.find(a);
        if (it != g_in.end() && it->second.r.get() == o) { if (uid) it->second.uid = uid; return true; }
        InE e;
        e.r.set(o);
        if (!e.r.get()) return false;
        e.uid = uid;
        g_in[a] = e;
        ++g_inAdd;
        return true;
    }
    auto host_knows(int32_t uid) -> bool
    {
        if (uid <= 0) return false;
        auto it = K.hu.find(uid);
        return it != K.hu.end() && now_s() - it->second <= std::max(1.0, C.natparkage);
    }
    auto host_knows_qg(const std::string& g) -> bool
    {
        if (!C.natqg || g.empty()) return false;
        auto it = K.hq.find(g);
        return it != K.hq.end() && now_s() - it->second <= std::max(1.0, C.natparkage);
    }
    auto qg_idx(UObject* a) -> std::string
    {
        auto ix = a ? K.idx.find(a) : K.idx.end();
        return ix != K.idx.end() ? ix->second.qg : std::string();
    }
    auto host_link() -> int { return now_s() - K.nuAt <= 5 ? 1 : 0; }
    auto park_ok(Ent& e) -> bool
    {
        if (!C.natpark || e.st != LIVE || e.how == 3 || e.vend) return false;
        UObject* a = e.body.get();
        return a && alive_of(a) && host_knows(e.h.uid);
    }
    auto park_enter(Ent& e, double now) -> void
    {
        kin_release(e);
        UObject* a = e.body.get();
        if (a && e.frcThr) { if (uint8_t* S = ai_s_of(a)) { thr_mark(S, false); stsrc_clear(e, a, S); wr_u8_seh(S + 0x1ad, 0); } }
        e.frcThr = false;
        if (a && e.covSrc) { if (uint8_t* S = ai_s_of(a)) cover_clear(e, a, S); }
        cov_unmark(e);
        if (a && e.covMm0 >= 0) cover_mode(e, a, false);
        e.covMm0 = -1;
        if (e.crouch) state_tag(e.luid, 1024, false);
        if (e.aim) state_tag(e.luid, 8192, false);
        e.crouch = e.aim = false;
        if (a)
        {
            set_hidden(a, true);
            e.holdAt = -100;
            park_hold(a, e.luid, e.holdAt, now);
        }
        e.st = PARK; e.parkAt = now; e.shown = false; e.lookOn = false;
        ++K.parkIn;
        logl("NOLA natnpc: copia de host uid=" + std::to_string(e.h.uid) + " (_" + e.h.nm + ") guardada (natpark): host parou de listar mas ainda manda a uid, corpo escondido e parado, sem dono novo");
    }
    auto park_out(Ent& e, const char* why, double now) -> void
    {
        UObject* a = e.body.get();
        if (a)
        {
            if (e.how == 4) set_hidden(a, false);
        }
        e.st = LIVE;
        ++K.parkOut;
        release(e, why, now);
    }
    auto park_back(Ent& e, UObject* a, double now, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        int ad = K.adopt, tk = K.takes, kf = K.keptFull, sp = K.spawns;
        if (e.how == 4) { teleport(a, e.h.x, e.h.y, e.h.zg + e.hh, e.h.yaw); set_hidden(a, false); }
        double pa = e.parkAt;
        take(e, a, e.how, now, me, pup, tgt);
        K.adopt = ad; K.takes = tk; K.keptFull = kf; K.spawns = sp;
        e.parkAt = -1;
        ++K.parkBack;
        char b[200];
        snprintf(b, sizeof b, "NOLA natnpc: copia guardada de host uid=%d (_%s) volta para a lista: mesmo corpo retomado depois de %.1f s (natpark)", e.h.uid, e.h.nm.c_str(), now - pa);
        logl(b);
    }
    auto twin_free(CliSt::Twin& t, UObject* a, const char* why) -> void
    {
        if (a)
        {
            shooter_on(a);
            unkillable(guid_of(a), false);
        }
        if (t.r.raw) K.own.erase(reinterpret_cast<uintptr_t>(t.r.raw));
        ++K.twinOut;
        logl("NOLA natnpc: gemeo local uid=" + std::to_string(t.uid) + " devolvido a IA local: " + why);
    }
    auto twin_tick(double now) -> void
    {
        for (auto it = K.twin.begin(); it != K.twin.end();)
        {
            UObject* a = it->second.r.get();
            if (!a) { K.own.erase(it->first); ++K.twinOut; it = K.twin.erase(it); continue; }
            int ln = host_link();
            if (!C.natpark || (ln == 1 && !host_knows(it->second.uid) && !host_knows_qg(it->second.qg))) { twin_free(it->second, a, !C.natpark ? "natpark=0" : "o host nao manda mais a uid"); it = K.twin.erase(it); continue; }
            park_hold(a, it->second.uid, it->second.holdAt, now);
            ++it;
        }
    }
    auto twin_park(uintptr_t ad, int32_t uid) -> bool
    {
        if (!ad || uid <= 0) return false;
        if (K.twin.count(ad)) return true;
        int ln = host_link();
        if (!C.natpark || !K.meAddr || K.own.count(ad)) return false;
        UObject* a = nullptr;
        for (UObject* o : K.idxList) if (reinterpret_cast<uintptr_t>(o) == ad) { a = o; break; }
        std::string qg = qg_idx(a);
        bool byQg = ln == 1 && !host_knows(uid) && host_knows_qg(qg);
        if (ln == 1 && !host_knows(uid) && !byQg) return false;
        if (!a || !alive_of(a) || guid_of(a) != uid) return false;
        if (byQg) ++K.qgTwin;
        CliSt::Twin t; t.r.set(a); t.uid = uid; t.at = now_s(); t.qg = qg;
        K.own.insert(ad);
        int sh = shooter_off(a);
        bool uk = unkillable(uid, true);
        set_hidden(a, true);
        park_hold(a, uid, t.holdAt, t.at);
        K.twin[ad] = t;
        ++K.twinIn;
        logl("NOLA natnpc: gemeo local " + w2u(a->GetName()) + " uid=" + std::to_string(uid) + " guardado (natpark, host manda a uid ou ainda sem host): escondido, parado, gatilho_off=" + std::to_string(sh) + " imortal=" + (uk ? "ok" : "falhou") + ", nao morto");
        return true;
    }
    auto twin_take(UObject* a) -> void
    {
        auto it = K.twin.find(reinterpret_cast<uintptr_t>(a));
        if (it == K.twin.end()) return;
        K.own.erase(it->first);
        K.twin.erase(it);
        ++K.twinBack;
    }

    auto release_all(const char* why, double now) -> void
    {
        for (auto& [k, e] : K.E)
        {
            if (e.st == PARK) park_out(e, why, now);
            else if (e.st == LIVE || e.st == DEAD) release(e, why, now);
        }
        for (auto& [k, e] : K.E) cov_unmark(e);
        K.E.clear();
        K.pending = false;
        for (auto& [ad, t] : K.twin) twin_free(t, t.r.get(), why);
        K.twin.clear();
    }

    auto hot_out() -> void
    {
        if (!C.nathotkeep) return;
        double now = now_s();
        std::vector<s2nmc::HotDead> v;
        for (auto it = K.E.begin(); it != K.E.end();)
        {
            Ent& e = it->second;
            UObject* a = e.body.get();
            if (!s2nmc::hot_dead_hand(true, e.st == DEAD, e.how, a != nullptr)) { ++it; continue; }
            s2nmc::HotDead h;
            h.uid = it->first; h.luid = e.luid; h.idx = e.body.idx; h.serial = e.body.serial; h.raw = reinterpret_cast<uint64_t>(e.body.raw);
            v.push_back(h);
            K.own.erase(reinterpret_cast<uintptr_t>(e.body.raw));
            cov_unmark(e);
            it = K.E.erase(it);
        }
        x0::hot::logic::keep("NATDEAD", s2nmc::hot_dead_pack(v));
        int rest = (int)K.E.size() + (int)K.twin.size();
        if (rest) release_all("hot reload (nathotkeep)", now);
        logl("NOLA nathot: hot reload: " + std::to_string(v.size()) + " copias mortas entregues a proxima geracao (ficam no lugar), " + std::to_string(rest) + " outras copias/gemeos soltos");
    }

    auto hot_in(double now) -> void
    {
        if (K.hotTaken) return;
        K.hotTaken = true;
        if (!C.nathotkeep) return;
        auto v = s2nmc::hot_dead_unpack(x0::hot::logic::take("NATDEAD"));
        if (v.empty()) return;
        for (const s2nmc::HotDead& h : v) K.hotDead[h.uid] = h;
        K.hotAt = now;
        logl("NOLA nathot: " + std::to_string(K.hotDead.size()) + " copias mortas da geracao anterior recebidas: a linha morta do host retoma cada uma sem spawn; as que o host nao listar em 30 s sao destruidas");
    }

    auto hot_body(const s2nmc::HotDead& h) -> UObject*
    {
        WRef r;
        r.raw = reinterpret_cast<UObject*>(h.raw); r.idx = h.idx; r.serial = h.serial;
        if (!r.raw || r.idx < 0) return nullptr;
        FUObjectItem* it = FUObjectArray::IndexToObject(r.idx);
        if (!it || it->GetUObject() != r.raw || (r.serial != 0 && it->GetSerialNumber() != r.serial)) return nullptr;
        if (it->HasAnyFlags(static_cast<EInternalObjectFlags>((1 << 28) | (1 << 21)))) return nullptr;
        if (h.luid && guid_of(r.raw) != h.luid) return nullptr;
        return r.raw;
    }

    auto hot_adopt(Ent& e, const s2nmc::HotDead& h, double now) -> bool
    {
        UObject* a = hot_body(h);
        if (!a || alive_of(a)) { ++K.hotBad; return false; }
        e.body.set(a);
        if (!e.body.get()) { ++K.hotBad; return false; }
        K.own.insert(reinterpret_cast<uintptr_t>(a));
        e.luid = h.luid ? h.luid : guid_of(a);
        e.how = 2; e.st = DEAD; e.takeAt = now; e.killAt = now; e.intAt = now; e.killTry = 1; e.shown = true; e.lostDead = false; e.hgtLog = 1;
        e.hh = half_height(a);
        interaction_off(a);
        ++K.hotBack; ++K.takes;
        logl("NOLA nathot: copia morta de host uid=" + std::to_string(e.h.uid) + " (_" + e.h.nm + ") retomada apos o hot reload: " + w2u(a->GetName()) + " uid_local=" + std::to_string(e.luid) + " fica onde esta, sem spawn");
        return true;
    }

    auto hot_drop(double now) -> void
    {
        if (!s2nmc::hot_dead_due(K.hotAt >= 0, K.hotAt, now)) return;
        K.hotAt = -1;
        for (auto& [uid, h] : K.hotDead)
        {
            UObject* a = hot_body(h);
            if (!a) continue;
            set_hidden(a, true);
            cc_uid(g_ai2, STR("CustomConsoleManagerAI2"), STR("XDestroyModel"), STR("UID"), h.luid ? h.luid : guid_of(a));
            ++K.hotDrop;
            logl("NOLA nathot: copia morta da geracao anterior host uid=" + std::to_string(uid) + " uid_local=" + std::to_string(h.luid) + " sem linha do host em 30 s: escondida e XDestroyModel");
        }
        K.hotDead.clear();
    }

    auto dead_pose_log(Ent& e, UObject* a) -> double
    {
        e.hgtLog = 1;
        double dch = -1;
        {
            double px = 0, py = 0, pz = 0;
            if (pelvis_of(a, px, py, pz))
            {
                dch = std::sqrt((px - e.h.x) * (px - e.h.x) + (py - e.h.y) * (py - e.h.y) + (pz - (e.h.zg + e.hh)) * (pz - (e.h.zg + e.hh)));
                K.deadDistMax = std::max(K.deadDistMax, dch);
            }
        }
        UObject* mesh = prop_obj(a, STR("Mesh"));
        bool rag = mesh ? c_bool(mesh, STR("IsAnySimulatingPhysics"), false) : false;
        double bx = 0, by = 0, bz = 0, ox = 0, oy = 0, oz = 0;
        {
            Call c(a, STR("GetActorBounds"));
            c.bl(STR("bOnlyCollidingComponents"), false);
            if (c.run())
            {
                if (uint8_t* p = c.at(STR("Origin"), 24)) { auto* q = reinterpret_cast<double*>(p); ox = q[0]; oy = q[1]; oz = q[2]; }
                if (uint8_t* p = c.at(STR("BoxExtent"), 24)) { auto* q = reinterpret_cast<double*>(p); bx = q[0]; by = q[1]; bz = q[2]; }
            }
        }
        bool up = !rag && bz > 0 && bz >= e.hh * 0.5;
        if (!rag) ++K.deadNoRag;
        up ? ++K.deadUp : ++K.deadFlat;
        char b[420];
        snprintf(b, sizeof b, "NOLA natdead: copia host uid=%d (_%s) uid_local=%d 3 s apos a morte: ragdoll=%d caixa_z=%.0f meia_altura=%.0f caixa_xy=%.0f,%.0f centro_z=%.0f em_pe=%d hp=%.1f vivo=%d dist_corpo_host=%.0f tol=%.0f",
                 e.h.uid, e.h.nm.c_str(), e.luid, rag ? 1 : 0, bz, e.hh, bx, by, oz, up ? 1 : 0, c_float(a, STR("GetHP"), -1.f), alive_of(a) ? 1 : 0, dch, 4.0 * e.hh);
        logl(b);
        return dch;
    }

    enum { PK_NONE = 0, PK_ADOPT, PK_DEAD, PK_REFUSE, PK_WAIT };

    auto local_of(const Ent& e, bool& viaQg) -> UObject*
    {
        return s2nmc::local_pick(e.h.uid, e.h.qg, C.natqg != 0, K.byUid, K.byQg,
            [](UObject* o) { auto ix = K.idx.find(o); return ix != K.idx.end() ? ix->second.uid : 0; },
            [](int32_t u) { return K.E.count(u) > 0; }, viaQg);
    }

    auto local_garbage(UObject* r) -> bool
    {
        FUObjectItem* it = FUObjectArray::IndexToObject(r->GetInternalIndex());
        if (!it || it->GetUObject() != r) return true;
        return s2nmc::pick_garbage(it->HasAnyFlags(static_cast<EInternalObjectFlags>(1 << 28)), it->HasAnyFlags(static_cast<EInternalObjectFlags>(1 << 21)));
    }

    auto pick_real(Ent& e, UObject* me, UObject* pup, UObject* tgt, UObject*& a, float& mh) -> int
    {
        a = nullptr; mh = 0;
        e.qgAd = false;
        if (!e.h.uid && e.h.qg.empty()) return PK_NONE;
        bool vq = false;
        UObject* r = local_of(e, vq);
        if (!r || is_ours(r) || r == me || r == pup || r == tgt || is_pl(r)) return PK_NONE;
        if (C.natdeadpos && local_garbage(r)) { ++K.garbRej; return PK_NONE; }
        e.qgAd = vq;
        a = r;
        if (!alive_of(r)) return PK_DEAD;
        mh = c_float(r, STR("GetMaxHP"), 0.f);
        if (!C.natadopt) return PK_REFUSE;
        bool hv = (e.h.fl & 512) != 0;
        if (!(mh > 0 && (mh < cls_cut(cls_of(r)) || (C.natadopt_kept && hv)))) return PK_REFUSE;
        double x, y, z;
        if (!loc_of(r, x, y, z)) return PK_WAIT;
        return PK_ADOPT;
    }

    auto pick_note(Ent& e, int p, UObject* a, float mh, const char* where) -> void
    {
        if (p == PK_DEAD) ++K.pkDeadW; else if (p == PK_REFUSE) ++K.pkRefW; else if (p == PK_WAIT) ++K.pkWaitW;
        if (p != PK_DEAD && p != PK_REFUSE) return;
        uint64_t k = (static_cast<uint64_t>(static_cast<uint32_t>(e.h.uid)) << 2) | static_cast<uint64_t>(p);
        if (!K.pkLogged.insert(k).second) return;
        char b[360];
        snprintf(b, sizeof b, "NOLA natnpc: uid=%d (_%s) corpo local %s %s (%s): MaxHP=%.0f corte=%.0f vendedor=%d -> sem copia criada",
                 e.h.uid, e.h.nm.c_str(), a ? w2u(a->GetName()).c_str() : "?", p == PK_DEAD ? "MORTO fica nativo com o loot dele" : "RECUSADO pela adocao",
                 where, mh, a ? cls_cut(cls_of(a)) : 0.0, (e.h.fl & 512) ? 1 : 0);
        logl(b);
    }

    auto try_body(Ent& e, int32_t key, double now, int busy, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        if (now < e.failUntil || !s2nmc::dead_row_copy(C.natfd != 0, (e.h.fl & 1) != 0, s2nmc::far_had_body(C.natfar != 0, e.takeAt > 0, e.farLost))) return;
        if (!(e.h.fl & 1) && e.fdLog == 0)
        {
            e.fdLog = 1; ++K.fdRows;
            logl("NOLA natfd: host uid=" + std::to_string(e.h.uid) + " (_" + e.h.nm + ") arrived dead with no copy here: a copy is made and killed at the host corpse");
        }
        bool hv = (e.h.fl & 512) != 0;
        auto visible = [&](UObject* a) -> void
        {
            if (host_hid(e)) { take(e, a, 1, now, me, pup, tgt); vis_log(e, "visible local body adopted hidden: the NPC is hidden on the host"); return; }
            bool vendHere = C.natvend && K.vend.count(a) > 0;
            if (hv && vendHere) { take(e, a, 3, now, me, pup, tgt); return; }
            if (!hv && C.natkept == 1) { take(e, a, 3, now, me, pup, tgt); ++K.keptSoft; return; }
            if (!hv && C.natkept >= 2)
            {
                if (vendHere) { take(e, a, 3, now, me, pup, tgt); ++K.keptSoft; return; }
                UObject* ia = me ? c_obj(me, STR("GetInteractedActor")) : nullptr;
                if (ia == a) { ++K.keptTalk; return; }
                take(e, a, 4, now, me, pup, tgt);
                return;
            }
            if (e.st != KEPT) { e.st = KEPT; e.keptChk = now; }
        };
        e.qgAd = false;
        if (e.h.uid || !e.h.qg.empty())
        {
            bool vq = false;
            UObject* lr = local_of(e, vq);
            if (lr && K.twin.count(reinterpret_cast<uintptr_t>(lr))) twin_take(lr);
            UObject* a = nullptr;
            float mh = 0;
            int p = pick_real(e, me, pup, tgt, a, mh);
            if (p == PK_ADOPT)
            {
                if (!prop_bool(a, STR("bHidden"), true)) { visible(a); return; }
                take(e, a, 1, now, me, pup, tgt);
                return;
            }
            if (p != PK_NONE)
            {
                pick_note(e, p, a, mh, "antes de criar");
                e.failUntil = now + (p == PK_WAIT ? 1 : 5);
                return;
            }
        }
        if (!(e.h.fl & 1) && !K.hotDead.empty())
        {
            auto hd = K.hotDead.find(key);
            if (hd != K.hotDead.end())
            {
                bool ok = hot_adopt(e, hd->second, now);
                K.hotDead.erase(hd);
                if (ok) return;
            }
        }
        if (C.natqg && !e.h.qg.empty())
        {
            if (e.qgSeen < 0) e.qgSeen = now;
            if (now - e.qgSeen < kQgWait) { ++K.qgWait; e.failUntil = now + 0.5; return; }
        }
        if (C.natadopt && C.natadopt_kept && e.h.pid > 0)
        {
            UObject* best = nullptr;
            double bd = C.natkeptr * C.natkeptr;
            for (UObject* o : K.idxList)
            {
                if (!o || is_ours(o) || o == me || o == pup || o == tgt || is_pl(o) || K.twin.count(reinterpret_cast<uintptr_t>(o))) continue;
                auto ix = K.idx.find(o);
                if (ix == K.idx.end() || ix->second.pid != e.h.pid || ix->second.uid == e.h.uid || (ix->second.uid && K.E.count(ix->second.uid))) continue;
                if (C.natqg && !ix->second.qg.empty() && K.rowQg.count(ix->second.qg)) continue;
                double x, y, z;
                if (!loc_of(o, x, y, z) || pooled(x, y)) continue;
                double dz = (z - half_height(o)) - e.h.zg;
                double d = (x - e.h.x) * (x - e.h.x) + (y - e.h.y) * (y - e.h.y) + dz * dz;
                if (d > bd || prop_bool(o, STR("bHidden"), true) || !alive_of(o)) continue;
                best = o; bd = d;
            }
            if (best)
            {
                ++K.keptProto;
                logl("NOLA natnpc: corpo local mantido " + w2u(best->GetName()) + " (outra uid, mesmo prototipo " + std::to_string(e.h.pid) + ", " + std::to_string((int)std::sqrt(bd)) + " cm) adotado para host uid=" + std::to_string(e.h.uid) + " (natadopt_kept)");
                visible(best);
                return;
            }
        }
        if (C.natfixed && (e.h.fl & (256 | 512)))
        {
            ++K.fixedWait; e.failUntil = now + 2;
            return;
        }
        if (!C.natspawn || K.pending || busy || e.gaveUp) return;
        if (!(e.h.fl & 1) && s2nmc::far_out(C.natfar != 0, me_dist(me, e.h.x, e.h.y), far_r(true))) { ++g_lsFarSkip; e.failUntil = now + 2; return; }
        std::wstring sid = e.h.sid.empty() ? (e.h.mut() ? std::wstring() : C.natsid) : u2w(e.h.sid);
        if (sid.empty()) { ++K.noSid; e.failUntil = now + 5; return; }
        std::wstring ssid = (C.natcopy && !e.h.mut() && sid == C.natsid && !C.natcopysid.empty()) ? C.natcopysid : sid;
        K.pending = true;
        K.pend = Pend{};
        K.pend.key = key; K.pend.x = e.h.x; K.pend.y = e.h.y; K.pend.z = e.h.zg; K.pend.at = now; K.pend.ci = cls_by_sid(sid);
        K.pend.before = K.idxList;
        bool ok = spawn_sid(ssid, e.h.x, e.h.y, e.h.zg + 140, e.h.yaw);
        if (ok && ssid != sid) ++K.copySpawn;
        if (ok && e.h.uid)
        {
            auto bu = K.byUid.find(e.h.uid);
            if (bu != K.byUid.end() && bu->second)
            {
                UObject* tw = bu->second;
                ++K.dupSpawn; ++K.dupW;
                char db[260];
                snprintf(db, sizeof db, "NOLA natnpc: DUPLICADA host uid=%d (_%s) criada com corpo local da mesma uid ainda no mundo: %s vivo=%d nosso=%d escondido=%d #%d",
                         e.h.uid, e.h.nm.c_str(), w2u(tw->GetName()).c_str(), alive_of(tw) ? 1 : 0, is_ours(tw) ? 1 : 0, prop_bool(tw, STR("bHidden"), false) ? 1 : 0, K.dupSpawn);
                logl(db);
            }
        }
        e.st = PEND;
        if (!ok) { K.pending = false; e.st = NONE; e.failUntil = now + 5; ++K.spawnFail; ++K.errs; K.lastErr = "XSpawnObjBySID falhou"; }
    }

    auto pend_tick(double now, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        if (!K.pending) return;
        auto it = K.E.find(K.pend.key);
        if (it == K.E.end() || it->second.st != PEND) { K.pending = false; return; }
        Ent& e = it->second;
        UObject* best = nullptr;
        double bd = 800.0 * 800.0;
        bool last = now - K.pend.at > 6;
        int rj[6] = {};
        double nearD = 1e18;
        std::string nearWhy;
        auto rej = [&](int k, UObject* o, double d, const char* why) { ++rj[k]; if (last && d < nearD) { nearD = d; nearWhy = w2u(o->GetName()) + " " + why; } };
        for (UObject* o : K.idxList)
        {
            if (!o || K.pend.before.count(o) || is_ours(o) || o == me || o == pup || o == tgt || is_pl(o)) continue;
            double x, y, z;
            if (!loc_of(o, x, y, z)) continue;
            double d = (x - K.pend.x) * (x - K.pend.x) + (y - K.pend.y) * (y - K.pend.y);
            if (d >= bd) { rej(0, o, d, "longe"); continue; }
            int oc = cls_of(o);
            if (K.pend.ci >= 0 && oc != K.pend.ci) { rej(1, o, d, "classe"); continue; }
            float mh = c_float(o, STR("GetMaxHP"), 0.f);
            if (mh <= 0 || mh >= cls_cut(oc)) { rej(2, o, d, "hp"); continue; }
            if (!alive_of(o)) { rej(3, o, d, "morto"); continue; }
            if (C.natpark)
            {
                int32_t g = guid_of(o);
                if (g && (g == e.h.uid || host_knows(g))) { ++K.pendWorld; rej(4, o, d, "mundo"); continue; }
            }
            if (C.natqg)
            {
                std::string oq = qg_idx(o);
                if (oq.empty())
                {
                    auto ix = K.idx.find(o);
                    if (C.natqgsafe && ix != K.idx.end() && ix->second.qgN >= 8) ++g_qgSkip;
                    else oq = qg_of(o);
                }
                if (!oq.empty() && (K.rowQg.count(oq) || host_knows_qg(oq))) { ++K.pendWorld; ++K.qgPendSkip; rej(4, o, d, "quest"); continue; }
            }
            best = o; bd = d;
        }
        if (best)
        {
            K.pending = false;
            if (C.natlog && C.natlostfix)
            {
                auto ix = K.idx.find(best);
                char b[260];
                snprintf(b, sizeof b, "NOLA natnpc: spawn de host uid=%d pegou %s pid=%d a %.0f cm do ponto apos %.2f s (perdas desta uid=%d)",
                         e.h.uid, w2u(best->GetName()).c_str(), ix != K.idx.end() ? ix->second.pid : -1, std::sqrt(bd), now - K.pend.at, e.lostN);
                logl(b);
            }
            take(e, best, 2, now, me, pup, tgt);
            return;
        }
        if (last)
        {
            K.pending = false; e.st = NONE; e.failUntil = now + 5; ++K.spawnFail; K.lastErr = "spawn sem corpo em 6 s";
            ++g_pdTo; g_pdNear += rj[0]; g_pdCls += rj[1]; g_pdHp += rj[2]; g_pdDead += rj[3]; g_pdWorld += rj[4];
            if (C.natlog && g_pdTo <= 3)
            {
                char b[420];
                snprintf(b, sizeof b, "NOLA natnpc: spawn sem corpo em 6 s host uid=%d (_%s) em %.0f,%.0f,%.0f rejeitados longe=%d classe=%d hp=%d morto=%d mundo/quest=%d mais_perto=%s a %.0f cm",
                         e.h.uid, e.h.nm.c_str(), K.pend.x, K.pend.y, K.pend.z, rj[0], rj[1], rj[2], rj[3], rj[4], nearWhy.empty() ? "-" : nearWhy.c_str(), nearD < 1e17 ? std::sqrt(nearD) : -1.0);
                logl(b);
            }
        }
    }

    auto mut_look(Ent& e, UObject* a, double now) -> void
    {
        if (e.look.empty()) return;
        bool due = e.lookDone != e.lookH || now - e.lookChk >= ((e.reapply >= 3 && e.mism != 0) ? C.natlookbk : 10.0);
        if (!due) return;
        if (K.lookLeft <= 0) { ++K.lookDefer; return; }
        --K.lookLeft;
        int m = -1;
        std::string why;
        if (e.lookDone != e.lookH || e.mism != 0)
        {
            apply_look(a, e.look, m, &why, true);
            if (e.lookDone == e.lookH) ++e.reapply; else e.reapply = 0;
            e.lookDone = e.lookH; ++K.looksApplied; ++K.mutLook; e.lookRechk = false;
        }
        else
        {
            std::vector<Part> host, cl;
            parse_look(e.look, host); parts_of(a, cl, true);
            m = look_diff(host, cl, &why, true, true);
            e.lookRechk = true;
        }
        e.mism = m; e.why = why; e.lookChk = now;
    }

    auto show_if_ready(Ent& e, UObject* a, double now) -> void
    {
        if (host_hid(e))
        {
            if (!e.hHid) { e.hHid = true; vis_log(e, "hidden: the NPC is hidden on the host"); }
            if (e.shown) { set_hidden(a, true); e.shown = false; }
            return;
        }
        if (e.hHid) { e.hHid = false; vis_log(e, "shown again: the host unhid the NPC"); }
        if ((C.natfixed && (e.h.fl & 256)) || e.h.mut())
        {
            if (!e.shown) { set_hidden(a, false); e.shown = true; }
            if (e.h.mut() && C.natmutlook && C.natlook) mut_look(e, a, now);
            return;
        }
        bool lkDue = C.natlook && !e.look.empty() && (e.lookDone != e.lookH || now - e.lookChk >= ((e.reapply >= 3 && e.mism != 0) ? C.natlookbk : (now - e.takeAt < 30 ? 3.0 : 10.0)));
        if (lkDue && K.lookLeft <= 0) { lkDue = false; ++K.lookDefer; }
        if (lkDue)
        {
            --K.lookLeft;
            int m = -1;
            bool soon = false;
            std::string why;
            bool wait = C.natlookld > 0 && (e.lookDone != e.lookH || e.mism != 0) && !look_ready(e.look, K.ldLeft);
            if (wait) { ++K.lookWait; soon = true; }
            else if (e.lookDone != e.lookH || e.mism != 0)
            {
                int genOk0 = g_genOk;
                apply_look(a, e.look, m, &why);
                if (C.natgenmre && g_genOk != genOk0) { e.mDone[0] = e.mDone[1] = 0; ++g_genMre; }
                if (e.lookDone == e.lookH) ++e.reapply; else e.reapply = 0;
                e.lookDone = e.lookH; e.mism = m; e.why = why; ++K.looksApplied; e.lookRechk = false;
            }
            else
            {
                std::vector<Part> host, cl;
                face_retire(a);
                parse_look(e.look, host); parts_of(a, cl, true);
                for (const Part& p : cl) if (p.made && p.self && p.comp) face_setup(p.comp, prop_obj(a, STR("Mesh")));
                std::string gs = gen_on() ? look_gen(e.look) : std::string();
                std::string gmw;
                bool natg = !gs.empty() && gen_of(a) != 0 && C.natgenmk && C.natlooksec;
                int gmiss = gs.empty() ? 0 : gen_keep(host, cl, &gmw);
                e.mism = look_diff(host, cl, &why, !gs.empty(), false, natg) + gmiss; e.why = why + (gmiss > 0 ? gmw : std::string());
                if (!gs.empty() && !gen_same(a, gs)) { ++e.mism; e.why += " gerador"; }
                e.lookRechk = true;
                soon = C.natwpnlook == 0 && e.mism != 0 && e.reapply < 3;
            }
            e.lookChk = soon ? -100 : now;
        }
        if (!e.shown && (e.lookDone != 0 || !C.natlook || now - e.takeAt >= C.natshow))
        {
            if (e.lookDone == 0 && C.natlook) ++K.noLook;
            set_hidden(a, false);
            e.shown = true;
        }
    }

    auto combat_tick(Ent& e, UObject* a, double now, bool hc) -> void
    {
        if (!C.natcb || !e.h.ext || e.vend) return;
        auto tq = Clock::now();
        bool eqNew = C.nateqsync && e.h.eq != e.eqSeen;
        if (e.h.eq >= 0 && e.h.eq <= 3 && (now - e.eqAt >= 2.0 || eqNew))
        {
            if (eqNew && now - e.eqAt < 2.0)
            {
                ++g_eqNow;
                static int s_eqNowLog = 0;
                if (C.natlog && s_eqNowLog < 60) { ++s_eqNowLog; double lx = 0, ly = 0, lz = 0; loc_of(a, lx, ly, lz); logl("NOLA natnpc: troca de arma imediata host uid=" + std::to_string(e.h.uid) + " eq=" + std::to_string(e.h.eq) + " x=" + std::to_string((long long)lx) + " y=" + std::to_string((long long)ly) + " z=" + std::to_string((long long)lz)); }
            }
            e.eqAt = now; e.eqSeen = e.h.eq;
            int ceq = c_u8(a, STR("GetMainHandEquipType"), -1);
            if (ceq >= 0 && ceq == e.h.eq) { e.eqWant = -1; e.eqDead = -1; g_eqStuck.erase(e.h.uid); }
            if (g_eqOwnFlag != C.nateqsync) { g_eqOwnFlag = C.nateqsync; g_eqOwnLog = 0; }
            bool chgNow = C.nateqfix && ceq >= 0 && ceq != e.h.eq && c_bool(a, STR("IsChangingWeapon"), false);
            bool noFx = C.nateqfix && ceq >= 0 && ceq != e.h.eq && !chgNow && e.eqWant == e.h.eq;
            if (noFx && e.eqDead != e.h.eq)
            {
                e.eqDead = e.h.eq; ++g_eqNoFx; g_eqStuck.insert(e.h.uid);
                if (C.natlog && g_eqNoFxLog < 40)
                {
                    ++g_eqNoFxLog;
                    UObject* wm = c_obj(a, STR("GetWeaponInHandsMeshComponent"));
                    UObject* wa = wm ? c_obj(wm, STR("GetSkeletalMeshAsset")) : nullptr;
                    char b[360];
                    snprintf(b, sizeof b, "NOLA natnpc: troca de arma sem efeito host uid=%d (_%s) how=%d host_eq=%d copia_eq=%d trocando=%d escondida=%d malha_mao=%s -> nao repete ate o host mudar de arma",
                             e.h.uid, e.h.nm.c_str(), e.how, e.h.eq, ceq, chgNow ? 1 : 0, prop_bool(a, STR("bHidden"), false) ? 1 : 0, wa ? w2u(wa->GetName()).c_str() : "-");
                    logl(b);
                }
            }
            if (wpn_apply(a, e.luid, e.h.uid, e.h.eq, now) > 0)
            {
                if (C.nateqsync) e.eqWant = e.h.eq;
                e.eqChg = now; e.wpnChk = now + 1.5; e.wpnAt = 0;
                if (C.natlog && g_eqOwnLog < 80) { ++g_eqOwnLog; double lx = 0, ly = 0, lz = 0; loc_of(a, lx, ly, lz); logl("NOLA natnpc: troca de arma da copia host uid=" + std::to_string(e.h.uid) + " eq=" + std::to_string(e.h.eq) + " copia_eq=" + std::to_string(ceq) + " via=wpn_apply nateqsync=" + std::to_string(C.nateqsync) + " x=" + std::to_string((long long)lx) + " y=" + std::to_string((long long)ly) + " z=" + std::to_string((long long)lz)); }
            }
            else if (ceq >= 0 && ceq != e.h.eq && !(C.nateqfix && e.eqDead == e.h.eq) && !c_bool(a, STR("IsChangingWeapon"), false))
            {
                e.eqWant = e.h.eq;
                UObject* wm = obj_at(a, STR("Mesh"));
                bool noAnim = C.natwpnguard && (!wm || !obj_at(wm, STR("AnimScriptInstance")));
                if (noAnim) ++g_kEqNoAnim;
                Call c(a, STR("ChangeMainHandWeapon"));
                c.i(STR("MainHandEquipment"), e.h.eq).fl(STR("HideWeaponTimeLimit"), 0.f).bl(STR("bShouldSkipAnimation"), noAnim);
                ++t_wpnOurs; bool okc = c.run(); --t_wpnOurs; InterlockedIncrement(&g_wpOurs);
                okc ? ++K.eqSet : ++K.eqFail;
                e.eqChg = now; e.wpnChk = now + 1.5; e.wpnAt = 0;
                if (C.natlog && g_eqOwnLog < 80) { ++g_eqOwnLog; double lx = 0, ly = 0, lz = 0; loc_of(a, lx, ly, lz); logl("NOLA natnpc: troca de arma da copia host uid=" + std::to_string(e.h.uid) + " eq=" + std::to_string(e.h.eq) + " copia_eq=" + std::to_string(ceq) + " via=ChangeMainHandWeapon nateqsync=" + std::to_string(C.nateqsync) + " x=" + std::to_string((long long)lx) + " y=" + std::to_string((long long)ly) + " z=" + std::to_string((long long)lz)); }
            }
        }
        if ((e.wpnChk > 0 && now >= e.wpnChk) || now - e.shAt >= 5)
        {
            bool chk = e.wpnChk > 0 && now >= e.wpnChk;
            e.shAt = now; e.wpnChk = 0;
            if (c_obj(a, STR("GetWeaponInHandsMeshComponent"))) { if (C.natwpnhid && !e.shown) { e.wpnAt = now; wpn_sync(a, prop_bool(a, STR("bHidden"), false)); } int n = shooter_off(a); if (n > 0) K.shOff += n; if (chk) { if (C.natwpnlook) e.lookDone = 0; else e.lookChk = -100; } }
        }
        if (e.h.ls >= 1 && e.h.ls <= 12 && e.h.ls != e.lsDone)
        {
            UObject* m = single(g_vp, STR("CustomConsoleManagerVP"));
            bool ok = false;
            if (m) { Call c(m, STR("XNpcSetLookAtOwnerState")); c.i(STR("NpcUID"), e.luid).i(STR("LookAtOwnerState"), e.h.ls); ok = c.run(); }
            ok ? ++K.lsSet : ++K.lsFail;
            e.lsDone = e.h.ls;
        }
        if (C.natwound)
        {
            if ((e.h.cb & 16) && !(e.cbPrev & 16) && !C.natwoundfix) { cc_uid(g_vp, STR("CustomConsoleManagerVP"), STR("XWoundNpcByUID"), STR("NpcUID"), e.luid); ++K.wound; }
            if ((e.h.cb & 32) && !(e.cbPrev & 32)) { cc_uid(g_vp, STR("CustomConsoleManagerVP"), STR("XKnockDownNpc"), STR("NpcUID"), e.luid); ++K.knock; }

            if (C.natwoundfix)
            {
                bool hw = (e.h.cb & 16) != 0, hk = (e.h.cb & 32) != 0;
                bool rw = hw && !(e.cbPrev & 16), fw = !hw && (e.cbPrev & 16), rk = hk && !(e.cbPrev & 32), fk = !hk && (e.cbPrev & 32);
                bool tick = now - e.wfAt >= 0.5;
                int cw = -1, ck = -1;
                float chp = 0.f, mh = 1.f;
                if (rw || fw || rk || fk || tick)
                {
                    e.wfAt = now;
                    cw = c_u8(a, STR("GetWoundedState_BP"), -1);
                    ck = c_u8(a, STR("GetKnockedDownState"), -1);
                    chp = c_float(a, STR("GetHP"), 0.f);
                    mh = c_float(a, STR("GetMaxHP"), 1.f);
                }
                auto wlog = [&](const char* ev, const char* act)
                {
                    char b[300];
                    snprintf(b, sizeof b, "NOLA natwound %s uid=%d host_cb=%d host_hp=%.0f copia_hp=%.0f copia_ferido=%d copia_derrubado=%d acao=%s",
                             ev, e.h.uid, e.h.cb & 48, s2nmc::hp_want(e.h.hp, e.h.hpS, mh), chp, cw, ck, act);
                    logl(b);
                };
                auto fall = [&](const char* ev)
                {
                    if (!alive_of(a)) return;
                    Call f(a, STR("FallWounded"));
                    f.i(STR("NewWoundedType"), 1);
                    bool ok = f.run();
                    ++K.wound;
                    ++e.wfTries;
                    wlog(ev, ok ? "FallWounded(1) ok" : "FallWounded(1) falhou");
                    e.wfSince = now;
                };
                if (rk) wlog("sobe_derrubado", "XKnockDownNpc");
                if (rw || fw) e.wfTries = 0;
                if (cw >= 0)
                {
                    bool localW = !hw && cw > 0, missW = hw && cw == 0, localK = !hk && ck > 0;
                    if (!localW && !missW) e.wfTries = 0;
                    if (e.wfTries >= 10) localW = missW = false;
                    if (fw) wlog("cai_ferido", cw > 0 ? "levantar" : "copia_ja_de_pe");
                    if (fk) wlog("cai_derrubado", ck > 0 ? "esperar_fim_nativo" : "copia_ja_de_pe");
                    if (localW || missW) { if (e.wfSince < 0) e.wfSince = now; } else e.wfSince = -1;
                    if (localK) { if (e.kfSince < 0) e.kfSince = now; } else e.kfSince = -1;
                    if (rw) { if (cw == 0) fall("sobe_ferido"); else wlog("sobe_ferido", "copia_ja_ferida"); }
                    else if (missW && now - e.wfSince >= 1.0) fall("ferido_faltando");
                    if (localW && cw == 2 && now - e.wfSince >= 1.0)
                    {
                        Call f(a, STR("ForceSetHP"));
                        f.i(STR("HP"), (int32_t)std::max(mh, 36.f));
                        bool o1 = f.run();
                        Call w(a, STR("WakeUpWounded"));
                        bool o2 = w.run();
                        ++e.wfTries;
                        wlog("levanta_ferido", o1 && o2 ? "ForceSetHP+WakeUpWounded ok" : "falhou");
                        e.wfSince = now;
                    }
                    if (localK && now - e.kfSince >= 15.0) { wlog("derrubado_preso", "so_log"); e.kfSince = now; }
                }
            }
        }
        K.hits += (((e.h.cb >> 8) & 255) - ((e.cbPrev >> 8) & 255) + 256) % 256;
        e.cbPrev = e.h.cb;
        sec_add(T_CMB, tq);
        UObject* insts[2] = {body_ai(e, a, now), weap_ai(e, a, now)};
        for (int k = 0; k < 2; ++k)
        {
            UObject* ai = insts[k];
            if (!ai) continue;
            int hs = k ? e.h.ws : e.h.bs;
            if (k == 0 && hs != 0 && e.err > C.natmerr && (!C.natmgate || e.mon[0].seq != hs || root_mont(e.mon[0].path)))
            {
                if (c_bool(a, STR("IsChangingWeapon"), false)) { ++g_mEqHold; continue; }
                ++K.mGate;
                if (!C.natmgate || e.mGateSeq != hs)
                {
                    if (C.natmovl) K.mGateStop += mont_stop_all(ai, nullptr, 0, 0.2f);
                    else if (UObject* cur = mont_cur(ai)) { if (mont_stop(ai, cur, 0.2f)) ++K.mGateStop; }
                    e.mGateSeq = hs;
                }
                else ++K.mGateKeep;
                e.mDone[0] = 0;
                continue;
            }
            if (k == 0) e.mGateSeq = 0;
            if (hs != 0)
            {
                if (e.mDefer[k] && e.mDefer[k] != hs) { if (!hc) ++g_mLostCalm; e.mDefer[k] = 0; }
                if (C.natmre && e.mon[k].seq != hs)
                {
                    if (e.mdWait[k] < 0) e.mdWait[k] = now;
                    else if (now - e.mdWait[k] > 1.0 && e.mdWaitSeq[k] != hs)
                    {
                        e.mdWaitSeq[k] = hs; ++g_mxWait;
                        if (g_mxLog < 60) { ++g_mxLog; char b[200]; snprintf(b, sizeof b, "NOLA mdiag espera: uid=%d k=%d hs=%d seq_recebida=%d ha %.1f s", e.h.uid, k, hs, e.mon[k].seq, now - e.mdWait[k]); logl(b); }
                    }
                }
                else e.mdWait[k] = -1;
                if (C.natmre && k == 0 && hs == e.mDone[0] && e.mon[0].seq == hs && !e.mon[0].path.empty() && now - e.mdAt > 0.25)
                {
                    e.mdAt = now;
                    UObject* cur = mont_cur(ai);
                    std::string hp = unpack_path(e.mon[0].path);
                    if (C.natmovl && cur) { if (UObject* b = resident(u2w(hp))) if (b != cur && mont_inst(ai, b)) cur = b; }
                    std::string cp = cur ? w2u(cur->GetPathName()) : std::string();
                    UObject* hm = cur ? nullptr : resident(u2w(hp));
                    float hl = hm ? c_float(hm, STR("GetPlayLength"), -1.f) : -1.f;
                    float hr = e.mon[0].rate > 0 ? e.mon[0].rate / 1000.f : 1.f;
                    bool live = cur || hl < 0 || (hl - e.mon[0].pos) / hr > now - e.mon[0].recv + 0.5;
                    if (!live) ++g_mxShort;
                    if (cp != hp && e.mdEndSeq != hs && live)
                    {
                        e.mdEndSeq = hs; cur ? ++g_mxOther : ++g_mxEnd;
                        if (g_mxLog < 60)
                        {
                            ++g_mxLog; char b[400];
                            snprintf(b, sizeof b, "NOLA mdiag %s: uid=%d hs=%d host=%s pos_host=%.2f taxa=%d idade=%.2f copia=%s", cur ? "outra" : "acabou", e.h.uid, hs,
                                     hp.substr(hp.rfind('/') + 1).c_str(), e.mon[0].pos, e.mon[0].rate, now - e.mon[0].recv, cp.empty() ? "-" : cp.substr(cp.rfind('/') + 1).c_str());
                            logl(b);
                        }
                    }
                }
                if (hs == e.mDone[k] || e.mon[k].seq != hs) continue;
                if (C.nateqsync && e.h.eq >= 0 && e.h.eq <= 3 && e.eqDead != e.h.eq)
                {
                    bool chg = c_bool(a, STR("IsChangingWeapon"), false);
                    int ceq = chg ? -1 : c_u8(a, STR("GetMainHandEquipType"), -1);
                    if (chg || (ceq >= 0 && ceq != e.h.eq && now - e.eqChg < 2.5))
                    {
                        if (e.mDefer[k] != hs) { e.mDefer[k] = hs; ++g_mEqSkip; }
                        continue;
                    }
                }
                e.mDefer[k] = 0;
                if (e.mon[k].path.empty()) { e.mDone[k] = hs; ++K.mDyn; continue; }
                std::wstring wp = u2w(unpack_path(e.mon[k].path));
                if (!asset_ready(wp))
                {
                    int st = asset_state(wp);
                    if (st == 1) { ++K.mAsyncWait; continue; }
                    if (st == 2) { if (K.mLoadLeft <= 0) { ++K.mLoadDefer; continue; } --K.mLoadLeft; ++g_asSync; }
                }
                if (k == 0 && c_bool(a, STR("IsChangingWeapon"), false)) { ++g_mEqHold; continue; }
                if (k == 0 && C.natmovl) { mont_set_tick(e, ai, hs, now); continue; }
                e.mDone[k] = hs;
                UObject* m = find_asset_once(wp);
                if (!m) { ++K.mMiss; continue; }
                if (C.natmroot) keep_loaded(m);
                float rt = C.natmsec && e.mon[k].rate > 0 ? e.mon[k].rate / 1000.f : 1.f;
                double at = std::max(0.0, e.mon[k].pos + std::clamp(now - e.mon[k].recv, 0.0, 2.0) * rt);
                if (C.natmsec) at = msec_at(m, e.mon[k].pos, at, e.mon[k].nx);
                if ((C.natcover || C.natmsec) && mont_cur(ai) == m && mont_setpos(ai, m, (float)at))
                {
                    ++g_mSync;
                    if (C.natmsec) mont_msec(ai, m, e.mon[k].rate, e.mon[k].nx);
                    continue;
                }
                float len = mont_play(ai, m, (float)at, k == 0 || !C.natmkeep, rt);
                if (len > 0) { ++K.mPlay; if (C.natmsec) mont_msec(ai, m, 0, e.mon[k].nx); }
                else ++K.mFail;
            }
            else if (C.natmstop && now - e.eqChg > 2.5)
            {
                if (e.mDefer[k]) { if (!hc) ++g_mLostCalm; e.mDefer[k] = 0; }
                if (UObject* cur = mont_cur(ai))
                {
                    if (c_bool(a, STR("IsChangingWeapon"), false)) ++g_mEqHold;
                    else if (k == 0 && C.natmovl) K.mStop += mont_stop_all(ai, nullptr, 0, 0.25f);
                    else if (mont_stop(ai, cur, 0.25f)) ++K.mStop;
                }
            }
        }
        sec_add(T_MONT, tq);
        if (hc && now - e.statAt >= 0.5)
        {
            e.statAt = now;
            AnimSt cs;
            anim_state(insts[0], cs);
            ++K.cN; K.cIn += cs.inCombat > 0; K.cW += cs.hasW > 0; K.cLs += cs.ls == e.h.ls;
            K.cEq += c_u8(a, STR("GetMainHandEquipType"), -1) == e.h.eq;
            ++K.thrN; K.thrOn += c_bool(a, STR("IsInThreat"), false) ? 1 : 0;
            K.cInC += cs.inCombat > 0;
            if (UObject* atk = prop_obj(a, STR("AttackComponent")))
            {
                ++K.atkN;
                bool own = rd_u8_seh(reinterpret_cast<const uint8_t*>(atk) + 0xA8) == 1;
                K.atkShoot += own;
                if (own && C.natkinpin && e.how != 3 && !e.vend)
                {
                    double px, py, pz;
                    if (loc_of(a, px, py, pz))
                    {
                        int r = s2n_moveto_native_rot((uint32_t)e.luid, px, py, e.h.zg, 2, (float)std::max(60.0, C.natsucc), 24, 3 | 8, 2, e.h.yaw);
                        if (r == 0) { ++g_kPin; e.ordAt = now; e.ox = px; e.oy = py; e.ogait = 2; e.ows = 2; } else ++g_kPinFail;
                    }
                }
            }
            if (insts[0])
            {
                auto* ab = reinterpret_cast<const uint8_t*>(insts[0]);
                int sd = off_of(insts[0], STR("StateData")), la = off_of(insts[0], STR("LookAtData"));
                if (sd >= 0) K.relaxN += rd_u8_seh(ab + sd + 0x36) > 0;
                if (la >= 0) K.cLook += rd_u8_seh(ab + la + 0x13C) > 0;
            }
        }
        sec_add(T_STAT, tq);
    }

    auto anim_force(Ent& e, UObject* a, double now, bool hc) -> void
    {
        if (!C.natforce || !C.natcb || !e.h.ext || e.vend || e.how == 3) return;
        auto t0 = Clock::now();
        if (hc) e.frcUntil = now + 1.0;
        if (!hc && now >= e.frcUntil)
        {
            if (e.frcThr) { if (uint8_t* S = ai_s_of(a)) { thr_mark(S, false); stsrc_clear(e, a, S); wr_u8_seh(S + 0x1ad, 0); } e.frcThr = false; }
            e.frcAt = -1;
            return;
        }
        bool hb = ((e.h.cb >> 18) & 1) != 0;
        int inC = hb ? (e.h.cb & 1) : (hc ? 1 : 0);
        int cl = hb ? ((e.h.cb >> 16) & 1) : (hc ? 1 : 0);
        int rx = hb ? ((e.h.cb >> 17) & 1) : 0;
        if (C.natforce & 1)
            if (UObject* ai = body_ai(e, a, now))
            {
                auto* b = reinterpret_cast<uint8_t*>(ai);
                int sd = off_of(ai, STR("StateData")), la = off_of(ai, STR("LookAtData"));
                if (sd >= 0 && la >= 0)
                {
                    if (e.frcAt >= 0)
                        (rd_u8_seh(b + sd + 0x09) == e.frcIn && rd_u8_seh(b + la + 0x13C) == e.frcCl && rd_u8_seh(b + sd + 0x36) == e.frcRx) ? ++K.frcKept : ++K.frcLost;
                    wr_u8_seh(b + sd + 0x09, inC);
                    wr_u8_seh(b + sd + 0x36, rx);
                    if (inC) { wr_u8_seh(b + sd + 0x37, 0); wr_u8_seh(b + sd + 0x3A, 0); wr_u8_seh(b + sd + 0x3B, 0); }
                    wr_u8_seh(b + la + 0x13C, cl);
                    if (e.h.ls >= 1 && e.h.ls <= 12) wr_u8_seh(b + la + 0x132, e.h.ls);
                    if (e.h.ls >= 1 && e.h.ls <= 12) stance_set(ai, e.h.ls);
                    e.frcIn = inC; e.frcCl = cl; e.frcRx = rx; e.frcAt = now; ++K.frcW;
                }
            }
        if (C.natforce & 2)
        {
            if (uint8_t* S = ai_s_of(a))
            {
                if (hc)
                {
                    if (e.frcThr) (rd_u8_seh(S + 0x1ad) == 1 && rd_u8_seh(S + 0x1ac) == 1) ? ++K.thrKept : ++K.thrLost;
                    wr_u8_seh(S + 0x1ac, 1); wr_u8_seh(S + 0x1ad, 1); e.frcThr = true;
                    thr_mark(S, true);
                    if (C.natstsrc && e.h.ls >= 1 && e.h.ls <= 12) stsrc_set(e, a, S, e.h.ls); else stsrc_clear(e, a, S);
                }
                else if (e.frcThr) { thr_mark(S, false); stsrc_clear(e, a, S); wr_u8_seh(S + 0x1ad, 0); e.frcThr = false; }
            }
            else ++K.thrNoS;
        }
        K.frcUs += std::chrono::duration<double, std::micro>(Clock::now() - t0).count(); ++K.frcN;
    }

    auto look_stop(Ent& e, UObject* a) -> void
    {
        e.lookOn = false;
        UObject* ai = (g_lkFn && a) ? anim_of(prop_obj(a, STR("Mesh"))) : nullptr;
        int la = ai ? off_of(ai, STR("LookAtData")) : -1;
        if (la < 0) { ++K.lookStopFail; return; }
        int r = look_stop_raw(reinterpret_cast<uint8_t*>(a), reinterpret_cast<uint8_t*>(ai), reinterpret_cast<uint8_t*>(ai) + la);
        if (r > 0) ++K.lookStop; else if (r == 0) ++K.lookNoop; else ++K.lookStopFail;
    }
    auto props_tick(Ent& e, UObject* a, double now) -> void
    {
        if (!C.natprops || !e.h.ext || !(e.h.cb & kCbPropOk) || now - e.prAt < 0.25) return;
        e.prAt = now;
        std::vector<UObject*> v;
        int n = props_of(a, v);
        if (n == 0 || (e.h.cb & kCbProp)) { e.prSince = -1; return; }
        if (e.prSince < 0) { e.prSince = now; return; }
        if (now - e.prSince < 0.6) return;
        int r = 0;
        std::string nm;
        UObject* body = prop_obj(a, STR("Mesh"));
        UObject* root = prop_obj(a, STR("RootComponent"));
        for (UObject* x : v)
        {
            if (x == body || x == root) { --n; continue; }
            UObject* par = c_obj(x, STR("GetAttachParent"));
            if (nm.size() < 200) { if (!nm.empty()) nm += ","; nm += w2u(x->GetName()) + "/" + w2u(x->GetClassPrivate()->GetName()) + "@" + (par ? w2u(par->GetName()) : std::string("none")); }
            Call d(x, STR("K2_DestroyComponent"));
            d.ob(STR("Object"), x);
            if (d.run()) ++r;
        }
        g_prCut += r; g_prCutFail += n - r; ++g_prEv;
        e.prSince = -1;
        if (C.natlog && g_prLog < 80)
        {
            ++g_prLog;
            logl("NOLA natprops client: uid=" + std::to_string(e.h.uid) + " copy had " + std::to_string(n) + " contextual prop(s) [" + nm + "] while the host has none -> destroyed " +
                 std::to_string(r) + " (events=" + std::to_string(g_prEv) + " destroyed=" + std::to_string(g_prCut) + " failed=" + std::to_string(g_prCutFail) + " readfail=" + std::to_string(g_prFail) + ")");
        }
    }
    int g_mbN = 0, g_mbPlay = 0, g_mbFail = 0, g_mbMiss = 0, g_mbDyn = 0, g_mbStop = 0, g_mbGate = 0, g_mbWait = 0, g_mbLog = 0;
    int g_ivPub[2] = {}, g_ivNoComp = 0;
    auto mut_tick(Ent& e, UObject* a, double now, bool kin) -> void
    {
        root_lock(e, a, e.rmPin || s2nmab::root_lock(C.natmab, e.h.fl, kin && e.kOn));
        if (!C.natmab || !e.h.ext || e.vend) return;
        int iv = s2nmab::inv_of(e.h.fl, C.natmab);
        if (iv >= 0)
        {
            if (uint8_t* ic = inv_comp(a))
            {
                inv_set(reinterpret_cast<uintptr_t>(ic), iv); ++g_ivPub[iv];
                float hc = s2nmab::cur_of(e.h.fl, C.natcloak, C.natmab), cc = -1, ct = -1;
                if (hc >= 0 && g_iaFn && cloak_rd_seh(ic, cc, ct))
                {
                    ++g_clRx;
                    g_clMaxErr = std::max(g_clMaxErr, (int)std::lround(std::fabs(cc - hc) * 100));
                    float v = s2nmab::cloak_snap(hc, iv, cc, ct, now - e.clAt, 1.0);
                    if (v >= 0)
                    {
                        int r = cloak_apply_seh(ic, v);
                        if (r < 0) ++g_clExc;
                        else { if (std::fabs(cc - v) > 1e-3f) ++g_clSnap; else ++g_clRe; e.clAt = now; }
                    }
                }
            }
            else ++g_ivNoComp;
        }
        if (!s2nmab::mon_on(e.h.fl, C.natmab)) return;
        UObject* ai = body_ai(e, a, now);
        if (!ai) return;
        ++g_mbN;
        s2nmab::MIn in;
        in.hs = e.h.bs; in.done = e.mDone[0]; in.seq = e.mon[0].seq; in.path = !e.mon[0].path.empty(); in.kin = kin && e.kOn;
        in.gate = C.natmgate != 0; in.root = in.hs != 0 && root_mont(e.mon[0].path); in.err = e.err; in.merr = C.natmerr; in.mstop = C.natmstop != 0;
        UObject* cur = in.hs == 0 && in.mstop ? mont_cur(ai) : nullptr;
        in.cur = cur != nullptr;
        s2nmab::Act act = s2nmab::mon_act(in);
        if (act == s2nmab::GATE)
        {
            ++g_mbGate;
            if (!C.natmgate || e.mGateSeq != in.hs) { if (UObject* c = mont_cur(ai)) mont_stop(ai, c, 0.2f); e.mGateSeq = in.hs; }
            e.mDone[0] = 0;
            return;
        }
        e.mGateSeq = 0;
        if (act == s2nmab::DYN) { e.mDone[0] = in.hs; ++g_mbDyn; return; }
        if (act == s2nmab::STOP) { if (mont_stop(ai, cur, 0.25f)) ++g_mbStop; return; }
        if (act != s2nmab::PLAY) return;
        std::wstring wp = u2w(unpack_path(e.mon[0].path));
        if (!asset_ready(wp))
        {
            int st = asset_state(wp);
            if (st == 1) { ++g_mbWait; return; }
            if (st == 2) { if (K.mLoadLeft <= 0) { ++g_mbWait; return; } --K.mLoadLeft; ++g_asSync; }
        }
        e.mDone[0] = in.hs;
        UObject* m = find_asset_once(wp);
        if (!m) { ++g_mbMiss; return; }
        if (C.natmroot) keep_loaded(m);
        float len = mont_play(ai, m, (float)s2nmab::play_at(e.mon[0].pos, now - e.mon[0].recv), true);
        len > 0 ? ++g_mbPlay : ++g_mbFail;
        if (C.natlog && g_mbLog < 40)
        {
            ++g_mbLog;
            logl("NOLA ma1 natmab cliente: mutante uid=" + std::to_string(e.h.uid) + " montagem seq=" + std::to_string(in.hs) + " " + e.mon[0].path +
                 (len > 0 ? " TOCOU" : " FALHOU") + (in.kin ? " kin raiz_travada" : ""));
        }
    }
    struct LocoDbg
    {
        int xg = -1, xhs = -1, dirty = -1;
        float xsp = -1, mws = -1, run668 = -1, f1b0 = -1, f1b4 = -1, f19c = -1;
        double armv = -1;
    };
    auto loco_dbg_seh(uint8_t* a, LocoDbg& d) -> void
    {
        __try
        {
            uint8_t* rec = *reinterpret_cast<uint8_t**>(a + 0x650);
            if (reinterpret_cast<uintptr_t>(rec) < 0x10000) return;
            d.f19c = *reinterpret_cast<float*>(rec + 0x19c);
            uint8_t* p658 = *reinterpret_cast<uint8_t**>(rec + 0x658);
            if (reinterpret_cast<uintptr_t>(p658) >= 0x10000) { d.f1b0 = *reinterpret_cast<float*>(p658 + 0x1b0); d.f1b4 = *reinterpret_cast<float*>(p658 + 0x1b4); }
            uint8_t* p668 = *reinterpret_cast<uint8_t**>(rec + 0x668);
            if (reinterpret_cast<uintptr_t>(p668) >= 0x10000) { d.dirty = p668[0x1e8]; d.run668 = *reinterpret_cast<float*>(p668 + 0x1f0); }
            uint8_t* cmc = *reinterpret_cast<uint8_t**>(a + 0x338);
            if (reinterpret_cast<uintptr_t>(cmc) >= 0x10000)
            {
                d.mws = *reinterpret_cast<float*>(cmc + 0x268);
                d.armv = std::hypot(*reinterpret_cast<double*>(cmc + 0xf80), *reinterpret_cast<double*>(cmc + 0xf88));
            }
            uint8_t* x = nullptr;
            if (reinterpret_cast<uintptr_t>(p658) >= 0x10000 && *reinterpret_cast<int32_t*>(p658 + 0x5ac) != 0)
            {
                uint8_t* u2 = *reinterpret_cast<uint8_t**>(a + 0xad0);
                if (reinterpret_cast<uintptr_t>(u2) >= 0x10000) x = u2 + 0x450;
            }
            else
            {
                uint8_t* u = *reinterpret_cast<uint8_t**>(a + 0xac8);
                if (reinterpret_cast<uintptr_t>(u) >= 0x10000) x = u + 0x620;
            }
            if (!x) return;
            uint64_t t = *reinterpret_cast<uint64_t*>(x + 0xb8);
            d.xg = t ? 9 : 0;
            if (g_luTagOk)
                for (int c = 0; c < 2; ++c)
                    for (int s = 0; s < 2; ++s)
                        for (int g = 1; g <= 4; ++g)
                            if (g_luTag[c][s][g] == t || g_luTagS[c][s][g] == t) d.xg = g + c * 10;
            d.xsp = *reinterpret_cast<float*>(x + 0xc8);
            d.xhs = x[0xcc];
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    struct GaitAgg { int n = 0, ok = 0, raiz = 0, cmc0 = 0, rm2 = 0; };
    GaitAgg g_lgG[5];
    LegH g_lgCli[4];
    int g_lgMmN[4] = {}, g_lgMmFz[4] = {}, g_lgMmNone[4] = {}, g_lgLowMmFz = 0;
    auto obj_chk_seh(uintptr_t p) -> UObject*
    {
        if (p < 0x10000) return nullptr;
        __try
        {
            int32_t i = *reinterpret_cast<const int32_t*>(p + 0xc);
            if (i < 0) return nullptr;
            FUObjectItem* it = FUObjectArray::IndexToObject(i);
            return it && reinterpret_cast<uintptr_t>(it->GetUObject()) == p ? reinterpret_cast<UObject*>(p) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    int g_lgLow = 0, g_lgLowGait = 0, g_lgLowCmc = 0, g_lgLowMont = 0, g_lgLowMode = 0, g_lgMbN = 0, g_lgMbSame = 0, g_lgOkGait = 0;
    std::vector<std::pair<double, std::string>> g_lgWorst;
    auto legs_note(Ent& e, UObject* a, double now, double sp, double cv, double r, bool mut) -> void
    {
        int k = mut ? 3 : (e.how == 2 ? 1 : (e.how == 4 ? 2 : 0));
        g_lgCli[k].add(r);
        bool mmFz = e.lg.mmRate >= 0 && e.lg.mmRate < 0.3;
        if (e.lg.mmRate < 0) ++g_lgMmNone[k];
        else { ++g_lgMmN[k]; if (mmFz) ++g_lgMmFz[k]; }
        if (r < 0.5 && mmFz) ++g_lgLowMmFz;
        int mb = c_u8(a, STR("GetMovementBehaviour"), -1);
        int hmb = (e.h.fl & (1u << 19)) ? (int)((e.h.fl >> 16) & 7) : -1;
        if (hmb >= 0 && mb >= 0) { ++g_lgMbN; if (hmb == mb) ++g_lgMbSame; }
        UObject* mo = mont_cur(body_ai(e, a, now));
        int mm = -1, ctk = -1, rqb = -1, vbo = -1, uro = -1, mtk = -1;
        double rqv = -1, acc = -1;
        if (UObject* cmc = obj_at(a, STR("CharacterMovement")))
        {
            auto* cb = reinterpret_cast<const uint8_t*>(cmc);
            int o = off_of(cmc, STR("MovementMode"));
            if (o >= 0) mm = rd_u8_seh(cb + o);
            ctk = c_bool(cmc, STR("IsComponentTickEnabled"), false) ? 1 : 0;
            int ob = off_of(cmc, STR("bHasRequestedVelocity")), orv = off_of(cmc, STR("RequestedVelocity")), oac = off_of(cmc, STR("Acceleration"));
            if (ob >= 0) rqb = rd_u8_seh(cb + ob);
            if (orv >= 0) rqv = std::hypot(rd_d_seh(cb + orv), rd_d_seh(cb + orv + 8));
            if (oac >= 0) acc = std::hypot(rd_d_seh(cb + oac), rd_d_seh(cb + oac + 8));
        }
        if (UObject* mesh = obj_at(a, STR("Mesh")))
        {
            auto* mb2 = reinterpret_cast<const uint8_t*>(mesh);
            int ov = off_of(mesh, STR("VisibilityBasedAnimTickOption"));
            if (ov >= 0) vbo = rd_u8_seh(mb2 + ov);
            uro = prop_bool(mesh, STR("bEnableUpdateRateOptimizations"), false) ? 1 : 0;
            mtk = c_bool(mesh, STR("IsComponentTickEnabled"), false) ? 1 : 0;
        }
        UObject* ai = body_ai(e, a, now);
        int rm = -1;
        if (ai)
        {
            int orm = off_of(ai, STR("RootMotionMode"));
            if (orm >= 0) rm = rd_u8_seh(reinterpret_cast<const uint8_t*>(ai) + orm);
        }
        int raiz = c_bool(a, STR("IsPlayingRootMotion"), false) ? 1 : 0;
        {
            GaitAgg& ga = g_lgG[std::clamp(hmb >= 0 ? hmb : e.lgait, 0, 4)];
            ++ga.n;
            if (r >= 0.5) ++ga.ok;
            if (raiz) ++ga.raiz;
            if (cv < 0.3 * sp) ++ga.cmc0;
            if (rm == 2) ++ga.rm2;
        }
        if (r >= 0.5) { if (mb >= 2) ++g_lgOkGait; }
        else
        {
            ++g_lgLow;
            if (mb < 2) ++g_lgLowGait;
            if (cv < 0.5 * sp) ++g_lgLowCmc;
            if (mo) ++g_lgLowMont;
            if (mm != 1 && mm != 2) ++g_lgLowMode;
        }
        if (g_lgWorst.size() >= 3 && r >= g_lgWorst.back().first) return;
        char gs[120] = {};
        s2n_goal_snap((uint32_t)e.luid, gs, (int)sizeof gs);
        uint64_t f250 = 0, f258 = 0;
        int rel = st_flags(a, f250, f258);
        double px = 0, py = 0, pz = 0;
        loc_of(a, px, py, pz);
        LocoDbg d;
        loco_dbg_seh(reinterpret_cast<uint8_t*>(a), d);
        UObject* mmo = obj_chk_seh(e.lg.mmS);
        char b[1400];
        snprintf(b, sizeof b, "NOLA natlegs detalhe: host uid=%d (_%s) how=%d mutante=%d razao_pe_corpo=%.2f host_v=%.0f cmc_v=%.0f marcha_copia=%d marcha_host=%d ordem_gait=%d idade_ordem=%.2f s ordens_copia=%d modo_cmc=%d montagem=%s erro=%.0f cm %s estado250=%llx estado258=%llx relevante=%d raiz=%d cmc_tick=%d req_bits=%d req_v=%.0f acel=%.0f malha_tick=%d vbato=%d uro=%d marcha_sel=%d"
                 " modo_raiz_anim=%d v_raiz_anim=%.0f max_walk=%.0f tag_x=%d tem_vel_x=%d vel_x=%.0f run668=%.0f sujo668=%d r658_1b0=%.1f r658_1b4=%.1f rec19c=%.1f x=%.0f y=%.0f z=%.0f mm_n=%d mm_ritmo=%.2f mm_anim=%s",
                 e.h.uid, e.h.nm.c_str(), e.how, mut ? 1 : 0, r, sp, cv, mb, hmb, e.ogait, now - e.ordAt, e.ordN, mm, mo ? w2u(mo->GetName()).c_str() : "-", e.err, gs,
                 (unsigned long long)f250, (unsigned long long)f258, rel, raiz, ctk, rqb, rqv, acc, mtk, vbo, uro, e.lgait,
                 rm, d.armv, d.mws, d.xg, d.xhs, d.xsp, d.run668, d.dirty, d.f1b0, d.f1b4, d.f19c, px, py, pz, e.lg.mmN, e.lg.mmRate, mmo ? w2u(mmo->GetName()).c_str() : "-");
        g_lgWorst.emplace_back(r, b);
        std::sort(g_lgWorst.begin(), g_lgWorst.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
        if (g_lgWorst.size() > 3) g_lgWorst.pop_back();
    }
    int g_gtMut = 0, g_gtHost = 0, g_gtSame = 0, g_gtCls = 0, g_gtOld = 0, g_gtLock = 0, g_gtN[6] = {};
    auto gait_pick(Ent& e, UObject* a, double now, double sp, double err, bool moving, bool soft, bool kin, bool mut) -> int
    {
        s2ngait::Cfg gc;
        gc.natrun = C.natrun; gc.natsprint = C.natsprint; gc.natcatch = C.natcatch; gc.natcatch2 = C.natcatch2; gc.natok = C.natok;
        gc.natgadlo = C.natgadlo; gc.natgadhi = C.natgadhi; gc.natgadt = C.natgadt; gc.natgaitmax = C.natgaitmax; gc.natgad = C.natgad; gc.natgait = C.natgait; gc.natmbh = C.natmbh;
        s2ngait::In in;
        in.sp = sp; in.err = err; in.csp = e.csp; in.now = now; in.latAt = e.latAt; in.og = e.ogait;
        in.hg = (e.h.fl & (1u << 19)) ? (int)((e.h.fl >> 16) & 7) : -1;
        in.moving = moving; in.soft = soft; in.kin = kin; in.mut = mut; in.fl128 = (e.h.fl & 128) != 0;
        int ci = mut ? cls_of(a) : -1;
        if (ci >= 0 && ci < (int)g_cls.size()) in.cs = g_cls[ci].spd;
        s2ngait::St st{e.gb, e.rs, e.gbAt, e.catchUp};
        s2ngait::Out o = s2ngait::pick(in, gc, st);
        e.gb = st.gb; e.rs = st.rs; e.gbAt = st.gbAt; e.catchUp = st.catchUp;
        K.gbUp += o.gbUp; K.gbDn += o.gbDn;
        if (mut && moving)
        {
            ++g_gtMut; ++g_gtN[std::clamp(o.gait, 0, 5)];
            int hg = s2ngait::host_gait(in.hg, std::clamp(C.natgaitmax, 2, 5));
            if (hg >= 0) { ++g_gtHost; if (hg == o.gait) ++g_gtSame; }
            if (o.src == s2ngait::CLASS) ++g_gtCls;
            if (o.legacy != o.base) ++g_gtOld;
            if (o.lock) ++g_gtLock;
        }
        if (!mut && moving)
        {
            ++g_lgHum;
            if (o.src == s2ngait::HOST) { ++g_lgHumHost; if (o.legacy != o.gait) ++g_lgHumDiff; }
        }
        return o.gait;
    }

    auto drive(Ent& e, UObject* a, double now, UObject* me, UObject* pup, UObject* tgt) -> void
    {
        bool soft = e.how == 3;
        bool mut = e.h.mut();
        bool kin = C.natkin && !soft && !e.vend;
        if (!soft && !e.vend) rel_zero(e, me, pup, tgt);
        double x, y, z;
        if (!loc_of(a, x, y, z)) return;
        double ageRaw = std::max(0.0, now - e.h.recv);
        K.dAgeSum += ageRaw; ++K.dAgeN; K.dAgeMax = std::max(K.dAgeMax, ageRaw); ++K.dAgeH[std::min(100, (int)(ageRaw * 100))];
        double age = std::clamp(ageRaw, 0.0, C.natagemax);
        double hx = e.h.x + e.h.vx * age, hy = e.h.y + e.h.vy * age;
        double err = std::hypot(x - hx, y - hy);
        if (C.natagek != 1.0) { hx = e.h.x + e.h.vx * age * C.natagek; hy = e.h.y + e.h.vy * age * C.natagek; }
        double sp = std::hypot(e.h.vx, e.h.vy);
        bool moving = sp > C.natstill;
        double cvx = 0, cvy = 0, cvz = 0;
        c_vec(a, STR("GetVelocity"), cvx, cvy, cvz);
        e.csp = std::hypot(cvx, cvy);
        if (kin && e.kOn && e.kmt >= 0) e.csp = e.kcsp;
        K.errSum += err; ++K.errN; K.errMax = std::max(K.errMax, err);
        int bk = std::min(40, (int)(err / 25));
        if (moving) { ++K.hMov[bk]; K.mvSum += err; ++K.mvN; } else { ++K.hStill[bk]; K.stSum += err; ++K.stN; }
        if (moving && sp > 50)
        {
            K.ratSum += e.csp / sp; ++K.ratN;
            int rg = std::clamp(e.ogait, 2, 5); K.rgS[rg] += e.csp / sp; K.rgV[rg] += sp; ++K.rgN[rg];
        }
        if (e.latAt >= 0)
        {
            if (!moving) e.latAt = -1;
            else if (e.csp >= 0.5 * sp) { double l = now - e.latAt; K.latSum += l; ++K.latN; K.latMax = std::max(K.latMax, l); ++K.latH[std::min(30, (int)(l * 10))]; e.latAt = -1; }
            else if (now - e.latAt > 3) { ++K.latCap; e.latAt = -1; }
        }
        bool talking = false;
        talking = me && K.talkTo == a;
        if (talking) { ++K.vendPause; return; }
        e.err = err;
        if (err > 150) rm_off(e, a, now, false);
        bool inCov = !mut && cover_tick(e, a);
        if (inCov) ++g_covHold;
        if (!kin && err > C.natsnap)
        {
            if (e.snapSince < 0) e.snapSince = now;
            if (!e.shown || err > 2.5 * C.natsnap || now - e.snapSince >= C.natsnapt)
            {
                teleport(a, hx, hy, e.h.zg + e.hh, e.h.yaw);
                ++K.tele; ++K.snapNow; e.ordAt = -100; e.snapSince = -1;
                return;
            }
            ++K.snapDefer;
        }
        else e.snapSince = -1;
        bool hc = C.natcb && e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16));
        int wst = hc ? 2 : 1, mfl = hc ? (3 | 8) : 3;
        if (hc && C.natthr) mfl = 2 | 8;
        if (hc && C.natthr && now - e.aiAt >= 5) { ai_on(a); e.aiAt = now; ++K.aiOn; }
        int gmax = std::clamp(C.natgaitmax, 2, 5);
        int gait = gait_pick(e, a, now, sp, err, moving, soft, kin, mut);
        double tx = hx, ty = hy;
        if (moving) { tx += e.h.vx * C.natlead; ty += e.h.vy * C.natlead; }
        bool atRest = !moving && err < C.natok;
        double dT = now - e.ordAt;
        double ang = 0;
        if (e.ordAt >= 0)
        {
            double a0 = std::atan2(e.oy - y, e.ox - x), a1 = std::atan2(ty - y, tx - x);
            ang = std::fabs(std::remainder(a1 - a0, 2 * 3.14159265358979)) * 57.2957795;
        }
        double moved = std::hypot(tx - e.ox, ty - e.oy);
        bool farT = e.ordAt < 0 || gait != e.ogait || moved > 3 * C.natre || (moved > C.natre && (ang > C.natang || dT >= C.natkeep || !moving));
        if (C.natcb && wst != e.ows && e.ows >= 0) farT = true;
        if (e.snapSince >= 0 && gait < std::min(gmax, 3)) { gait = std::min(gmax, 3); farT = farT || gait != e.ogait; }
        bool keep = !farT && dT >= C.natkeep && err > C.natok;
        bool hold = hc && atRest && dT >= C.natkeepc;
        bool kmv = C.natkinmv && kin && e.kOn && !hold;
        int kwhy = 0;
        if (kmv)
        {
            s2ngait::KoIn ki;
            ki.moving = moving; ki.has = e.ordAt >= 0;
            ki.hx = hx; ki.hy = hy; ki.vx = e.h.vx; ki.vy = e.h.vy; ki.cx = x; ki.cy = y; ki.ox = e.ox; ki.oy = e.oy;
            ki.gait = gait; ki.og = e.ogait; ki.lead = C.natkinlead; ki.re = C.natre; ki.ang = C.natang; ki.succ = C.natsucc;
            s2ngait::KoOut ko = s2ngait::kin_order(ki);
            kwhy = ko.why;
            bool kre = moving && dT >= C.natkeep && std::hypot(cvx, cvy) < 0.3 * sp;
            farT = kwhy != s2ngait::KO_NONE || kre || (C.natcb && wst != e.ows && e.ows >= 0);
            keep = kre && kwhy == s2ngait::KO_NONE;
            tx = ko.tx; ty = ko.ty;
            if (!farT) ++g_koKeep;
        }
        if (!inCov && (!kin || C.natkinord) && (((!atRest || kmv) && (farT || keep)) || hold) && dT >= 1.0 / std::max(0.1, C.natrate))
        {
            int r = (C.natcb && C.natrot) ? s2n_moveto_native_rot((uint32_t)e.luid, tx, ty, e.h.zg, gait, (float)C.natsucc, 24, mfl, wst, e.h.yaw)
                                          : s2n_moveto_native((uint32_t)e.luid, tx, ty, e.h.zg, gait, (float)C.natsucc, 24, 3, 1);
            if (r == 0)
            {
                if (hold) ++K.holds;
                e.ows = wst;
                ++K.orders; ++e.ordN; ++K.gaitN[std::clamp(gait, 0, 5)]; if (keep) ++K.keepOrd; e.ordAt = now; e.ox = tx; e.oy = ty; e.ogait = gait;
                if (kmv) ++g_koWhy[std::clamp(kwhy, 0, 5)];
                if (moving && e.csp < 30 && e.latAt < 0) e.latAt = now;
            }
            else { ++K.ordFail; e.ordAt = now; K.lastErr = "s2n_moveto e=" + std::to_string(r); }
        }
        e.turnOk = C.natturn && kin && e.kOn && !moving && !hc && !mut && !inCov && err < 150 && now - e.ordAt >= 1.0 && now - e.turnTry >= 3.0;
        if (!C.natturn || !kin || !e.kOn || moving || hc || mut) e.turnAt = -100;
        else if (e.turnOk)
        {
            double dyaw = std::fabs(std::remainder(e.h.yaw - yaw_of(a), 360.0));
            if (dyaw > 30)
            {
                e.turnTry = now;
                if (s2n_moveto_native_rot((uint32_t)e.luid, x, y, e.h.zg, 2, (float)std::max(60.0, C.natsucc), 24, 3, 1, e.h.yaw) == 0)
                {
                    e.turnAt = now; e.ordAt = now; e.ox = x; e.oy = y; ++g_turnOrd;
                    if (C.natlog && g_turnLog < 80)
                    {
                        ++g_turnLog;
                        char tb[240];
                        snprintf(tb, sizeof tb, "NOLA natturn: giro uid=%d (_%s) dyaw=%.0f x=%.0f y=%.0f z=%.0f", e.h.uid, e.h.nm.c_str(), dyaw, x, y, z);
                        logl(tb);
                    }
                }
                else { ++g_turnFail; e.turnOk = false; }
            }
        }
        e.lgait = (C.natgaitcp && kin && e.kOn && !inCov) ? (moving || !atRest ? std::clamp(gait, 2, 4) : (now - e.turnAt < 1.8 ? 0 : 1)) : 0;
        e.lx = x; e.ly = y; e.lz = z;
        if (C.natlegs && kin && e.kOn && moving && sp > 50)
        {
            double lr = 0;
            if (legs_step(e.lg, a, now, lr, g_lgOff)) legs_note(e, a, now, sp, std::hypot(cvx, cvy), lr, mut);
        }
        else e.lg.t = -1;
        double nrate = moving ? C.natnudge : C.natnudges;
        bool nOn = C.natcb ? (!soft && nrate > 0 && err < C.natsnap && (moving ? err > 20 : err > C.natok))
                           : (C.natnudge > 0 && moving && err > 20 && err < 150 && !soft);
        if (nOn && !kin && !inCov)
        {
            double dt = std::clamp(now - e.nudgeAt, 0.0, 0.2);
            double step = std::min(nrate * dt, err * 0.5);
            if (C.natcb && moving) { double cap = C.natnudgek * e.csp * dt; if (cap < step) { step = cap; ++K.nudgeCap; } }
            if (step > 1)
            {
                double ux = (hx - x) / err, uy = (hy - y) / err;
                Call c(a, STR("K2_SetActorLocation"));
                c.vec(STR("NewLocation"), x + ux * step, y + uy * step, z).bl(STR("bSweep"), true).bl(STR("bTeleport"), false);
                if (c.run()) { ++K.nudges; if (!moving) ++K.nudgeS; }
            }
        }
        e.nudgeAt = now;
        if (!moving) e.mwsW = -1;
        if (C.natcb && C.natspd && moving && !soft && !kin && !inCov)
            if (UObject* cmc = obj_at(a, STR("CharacterMovement")))
            {
                int o = off_of(cmc, STR("MaxWalkSpeed"));
                double vcap = e.ogait <= 2 ? C.natspdw : (e.ogait == 3 ? C.natspdr : 0.0);
                if (vcap <= 0) vcap = C.natspdmax;
                double v = std::clamp(sp * 1.05 + std::min(err, 300.0), 100.0, std::min(C.natspdmax, std::max(100.0, vcap)));
                if (o >= 0 && e.mws0 < 0) e.mws0 = rd_f_seh(reinterpret_cast<uint8_t*>(cmc) + o);
                if (o >= 0)
                {
                    float prev = rd_f_seh(reinterpret_cast<uint8_t*>(cmc) + o);
                    K.mwsPrev += prev; ++K.mwsN;
                    if (e.mwsW > 0 && std::fabs(prev - e.mwsW) > 1) ++K.mwsRw;
                    if (wr_f_seh(reinterpret_cast<uint8_t*>(cmc) + o, (float)v)) { ++K.spdSet; e.mwsW = (float)v; }
                }
            }
        auto tfc = Clock::now();
        bool faceOk = C.natfacemv || e.csp < 60;
        if (hc && C.natface && !soft && !faceOk) ++K.faceSkip;
        if (hc && C.natface && !soft && faceOk && !kin && !inCov)
        {
            double cy = yaw_of(a), d = std::fabs(std::remainder(e.h.yaw - cy, 360.0));
            K.faceSum += d; ++K.faceN;
            if (d > C.natfaceang)
            {
                double dd = std::remainder(e.h.yaw - cy, 360.0), stp = std::max(5.0, C.natfacer * 0.1);
                double ny = std::fabs(dd) <= stp ? e.h.yaw : cy + (dd > 0 ? stp : -stp);
                Call c(a, STR("K2_SetActorRotation"));
                c.vec(STR("NewRotation"), 0, ny, 0).bl(STR("bTeleportPhysics"), false);
                if (c.run()) ++K.faceFix;
            }
        }
        bool cr = (e.h.fl & 2) != 0, am = (e.h.fl & (4 | 8)) != 0;
        if (mut) { sec_add(T_FACE, tfc); sec_add(T_FORCE, tfc); mut_tick(e, a, now, kin); sec_add(T_MONT, tfc); return; }
        if (cr != e.crouch) { state_tag(e.luid, 1024, cr); e.crouch = cr; }
        if (am != e.aim) { state_tag(e.luid, 8192, am); e.aim = am; }
        UObject* foc = (e.h.fl & 32) ? pup : ((e.h.fl & 64) ? me : nullptr);
        if ((e.h.fl & 64) && (e.h.fl & (7u << 11)))
        {
            int fp = (int)((e.h.fl >> 11) & 7);
            foc = fp == K.myPid ? me : nullptr;
            if (!foc)
                for (const auto& pr : K.pl)
                    if (pr.first == fp) { foc = pr.second; break; }
        }
        bool hostLook = !C.natlookst || !e.h.ext || !(e.h.cb & (1 << 20)) || (e.h.cb & (1 << 19));
        if (!hostLook)
        {
            ++K.lookSkip;
            if (e.lookOn) look_stop(e, a);
        }
        else if (foc && now - e.lookAtT >= (hc ? 0.2 : 0.5))
        {
            double fx, fy, fz;
            if (loc_of(foc, fx, fy, fz)) { look_at(e.luid, fx, fy, fz + 60); e.lookOn = true; }
            e.lookAtT = now;
        }
        else if (!foc && hc && (e.h.fx != 0 || e.h.fy != 0) && now - e.lookAtT >= 0.2)
        {
            look_at(e.luid, e.h.fx, e.h.fy, e.h.fz + 40);
            e.lookOn = true;
            e.lookAtT = now;
        }
        sec_add(T_FACE, tfc);
        anim_force(e, a, now, hc);
        sec_add(T_FORCE, tfc);
        combat_tick(e, a, now, hc);
        props_tick(e, a, now);
    }

    auto pl_resolve() -> void
    {
        K.pl.clear();
        std::unordered_set<UObject*> ns;
        for (size_t i = 0; i < K.plA.size() && i < 8; ++i)
            if (UObject* o = K.plR[i].get((double)K.plA[i].second)) { K.pl.push_back({K.plA[i].first, o}); ns.insert(o); }
        if (ns != K.plSet) { K.plSet = std::move(ns); ++K.plGen; }
    }

    auto client_run(int mode, const std::string& nn, const std::string& nl, double meA, double pupA, double tgtA, int busy, int via) -> bool
    {
        double now = now_s();
        if (mode != 1)
        {
            if (!K.E.empty() || !K.twin.empty()) release_all("natnpc=0", now);
            graves_tick(now);
            wpn_tick(now);
            return true;
        }
        wpn_tick(now);
        auto inert = [&](const std::string& why) -> bool
        {
            if (now - K.inertAt >= 10)
            {
                K.inertAt = now;
                logl("NOLA natnpc 10 s (cliente): INERTE (" + why + ") #NN=" + std::to_string(nn.size()) + " #NL=" + std::to_string(nl.size()));
                if (!K.stLogged) { K.stLogged = true; logl("NOLA natnpc autoteste (cliente): FALHOU " + why); }
            }
            return false;
        };
        if (!selftest()) return inert("autoteste geral falhou: " + g_stMsg);
        auto meU = static_cast<uintptr_t>(meA);
        if (meU != K.meAddr)
        {
            if (K.meAddr && (!K.E.empty() || !K.twin.empty())) release_all("pawn local novo (mundo mudou)", now);
            K.meAddr = meU; K.idx.clear();
        }
        UObject* me = K.me.get(meA);
        UObject* pup = K.pup.get(pupA);
        UObject* tgt = K.tgt.get(tgtA);
        pl_resolve();
        if (!me)
        {
            char b[160];
            snprintf(b, sizeof b, "pawn local invalido: addr=%llu real=%d", (unsigned long long)meU, K.me.real ? 1 : 0);
            return inert(b);
        }
        if (!K.stLogged)
        {
            K.stLogged = true;
            FUObjectItem* it = FUObjectArray::IndexToObject(me->GetInternalIndex());
            char b[300];
            snprintf(b, sizeof b, "NOLA natnpc autoteste (cliente): PASS pawn=%s idx=%d flags_internos=0x%08x geral=%s #NN=%zu #NL=%zu",
                     w2u(me->GetName()).c_str(), me->GetInternalIndex(), it ? (unsigned)((it->HasAnyFlags(static_cast<EInternalObjectFlags>(1 << 29)) ? (1u << 29) : 0u) | (it->HasAnyFlags(static_cast<EInternalObjectFlags>(1 << 30)) ? (1u << 30) : 0u)) : 0u,
                     g_stMsg.c_str(), nn.size(), nl.size());
            logl(b);
        }
        K.lookLeft = C.natlookpt; K.mLoadLeft = C.natmloadpt; K.ldLeft = C.natlookld;
        g_asLeft = C.natasyncpt; if (g_asMe.get() != me) g_asMe.set(me);
        hot_in(now);
        hot_drop(now);
        if (via == 2) ++g_viaP; else if (via == 1) ++K.viaBr; else ++K.viaT;
        K.talkTo = c_obj(me, STR("GetInteractedActor"));
        auto tp = Clock::now();
        if (nn != K.lastNN) { K.lastNN = nn; parse_nn(nn, now); }
        if (nl != K.lastNL) { K.lastNL = nl; parse_nl(nl); }
        sec_add(T_PARSE, tp);
        bool waits = K.pending;
        for (auto& [k, e] : K.E) if (e.st == NONE || e.st == PEND) { waits = true; break; }
        if (now - K.idxAt >= (K.pending ? 0.25 : (waits ? 1.0 : std::max(1.0, C.natidxs))))
        {
            K.idxAt = now; rebuild_index(now); pend_tick(now, me, pup, tgt);
            if (now - K.deadScanAt >= 9.0)
            {
                K.deadScanAt = now; K.deadNear = 0; K.deadExtra = 0;
                double dr = far_r(true);
                for (UObject* o : K.idxList)
                {
                    double x, y, z;
                    if (!o || o == me || o == pup || o == tgt || is_pl(o) || !loc_of(o, x, y, z) || pooled(x, y) || alive_of(o) || prop_bool(o, STR("bHidden"), false)) continue;
                    if (me_dist(me, x, y) > dr) continue;
                    ++K.deadNear;
                    if (!is_ours(o)) ++K.deadExtra;
                }
            }
            if (C.natpark || C.natqg || C.natdupfix)
                for (auto& [k, e] : K.E)
                {
                    if (e.st != LIVE || e.how != 2 || (!e.h.uid && e.h.qg.empty())) continue;
                    UObject* rb = nullptr;
                    float mh = 0;
                    int p = pick_real(e, me, pup, tgt, rb, mh);
                    if (!rb || rb == e.body.get()) continue;
                    bool viaDup = !C.natpark && !e.qgAd;
                    if (viaDup && !C.natdupfix) continue;
                    if (viaDup && p == PK_DEAD)
                    {
                        if (g_dupDeadHid.insert({rb, e.h.uid}).second)
                        {
                            set_hidden(rb, true);
                            logl("NOLA natdup: uid " + std::to_string(e.h.uid) + " corpo nativo MORTO apareceu com o NPC vivo no host: corpo nativo escondido, a copia criada fica (um corpo visivel)");
                        }
                        continue;
                    }
                    if (p != PK_ADOPT && p != PK_DEAD)
                    {
                        if (viaDup && p == PK_REFUSE)
                        {
                            if (UObject* cb = e.body.get()) set_hidden(cb, true);
                            logl("NOLA natdup: uid " + std::to_string(e.h.uid) + " corpo nativo recusado apareceu, copia criada solta (um corpo visivel)");
                            release(e, "corpo nativo recusado da mesma uid apareceu", now);
                            e.failUntil = now + 5;
                            continue;
                        }
                        pick_note(e, p, rb, mh, "copia criada mantida");
                        continue;
                    }
                    ++K.swapReal; ++K.swapW;
                    logl("NOLA natnpc: corpo real da uid " + std::to_string(e.h.uid) + " (" + w2u(rb->GetName()) + ") apareceu no mundo do cliente " + (p == PK_DEAD ? "MORTO: copia criada sai, o corpo nativo fica com o loot dele" : "vivo: copia criada sai, o corpo real e adotado") + (e.qgAd ? " (natqg: mesmo guid de quest " + e.h.qg + ", outra uid)" : (viaDup ? std::string(" (natdupfix: corpo nativo tardio, uma copia visivel por NPC do host)") : std::string(" (natpark)"))));
                    if (viaDup) if (UObject* cb = e.body.get()) set_hidden(cb, true);
                    release(e, p == PK_DEAD ? "corpo real morto da mesma uid apareceu" : "corpo real da mesma uid apareceu", now);
                    e.failUntil = 0;
                }
            if (now - K.vendAt >= 5)
            {
                K.vendAt = now;
                double mx, my, mz;
                std::vector<VPos> lv;
                if (C.natvend && loc_of(me, mx, my, mz))
                {
                    const double CR = cli_r();
                    const double R2 = (CR + C.natvendr) * (CR + C.natvendr);
                    bool any = false;
                    for (const VRow& v : kVend) if ((v.x - mx) * (v.x - mx) + (v.y - my) * (v.y - my) <= R2) { any = true; break; }
                    if (any)
                        for (UObject* o : K.idxList)
                        {
                            double x, y, z;
                            if (!o || o == me || o == pup || o == tgt || is_pl(o) || !loc_of(o, x, y, z) || pooled(x, y)) continue;
                            if ((x - mx) * (x - mx) + (y - my) * (y - my) > R2 || !alive_of(o)) continue;
                            lv.push_back({o, x, y});
                        }
                    K.vendRows = any ? vend_pick(lv, {s2nmc::Pt{mx, my}}, CR, K.vend) : 0;
                    if (!any) K.vend.clear();
                }
                else { K.vend.clear(); K.vendRows = 0; }
            }
            int nd = 0;
            for (auto& [k, e] : K.E) nd += e.lostDead;
            while (nd > std::max(0, C.natcorpsemax))
            {
                auto old = K.E.end();
                for (auto j = K.E.begin(); j != K.E.end(); ++j) if (j->second.lostDead && (old == K.E.end() || j->second.killAt < old->second.killAt)) old = j;
                if (old == K.E.end()) break;
                release(old->second, "corpos guardados > natcorpsemax", now);
                K.E.erase(old); --nd; ++K.corpseCap;
            }
        }
        sec_add(T_IDX, tp);
        graves_tick(now);
        if (now - K.twinAt >= 1) { K.twinAt = now; twin_tick(now); }
        for (auto it = K.E.begin(); it != K.E.end();)
        {
            Ent& e = it->second;
            if (e.st == PARK)
            {
                UObject* a = e.body.get();
                double x, y, z;
                if (!a || !alive_of(a) || (loc_of(a, x, y, z) && pooled(x, y)))
                {
                    if (e.body.raw) K.own.erase(reinterpret_cast<uintptr_t>(e.body.raw));
                    cov_unmark(e);
                    e.body.reset(); ++K.parkOut;
                    logl("NOLA natnpc: copia guardada de host uid=" + std::to_string(e.h.uid) + " (_" + e.h.nm + ") sumiu/morreu/pool: entrada removida");
                    it = K.E.erase(it); continue;
                }
                if (!cli_lost(e.h, now)) { park_back(e, a, now, me, pup, tgt); ++it; continue; }
                if (!C.natpark || !host_knows(e.h.uid))
                {
                    park_out(e, C.natpark ? "guardada e o host nao manda mais a uid" : "natpark=0", now);
                    it = K.E.erase(it); continue;
                }
                park_hold(a, e.luid, e.holdAt, now);
                ++it; continue;
            }
            if (cli_lost(e.h, now) && e.st == DEAD && e.body.get())
            {
                if (!e.lostDead) { e.lostDead = true; logl("NOLA natnpc: copia morta uid=" + std::to_string(e.h.uid) + " (_" + e.h.nm + ") fica como corpo (host parou de mandar; b162)"); }
            }
            else if (cli_lost(e.h, now))
            {
                if (park_ok(e)) { park_enter(e, now); ++it; continue; }
                if (e.st == LIVE || e.st == DEAD) release(e, now - e.h.recv > C.natlost ? "o host nao manda mais" : "fora do snapshot completo do host (natrhost)", now);
                if (e.st == PEND) K.pending = false;
                it = K.E.erase(it);
                continue;
            }
            if (e.st == KEPT) { if (now - e.keptChk > 5) { e.st = NONE; try_body(e, it->first, now, busy, me, pup, tgt); } ++it; continue; }
            if (e.st == LIVE || e.st == DEAD)
            {
                UObject* a = e.body.get();
                double x, y, z;
                if (!a || (loc_of(a, x, y, z) && pooled(x, y)))
                {
                    if (e.body.raw) K.own.erase(reinterpret_cast<uintptr_t>(e.body.raw));
                    if (C.natlostfix)
                    {
                        bool nul = !a;
                        nul ? ++g_lsNull : ++g_lsPool;
                        kin_release(e);
                        bool kill = e.how == 2 && e.luid != 0;
                        double md = me_dist(me, e.lx, e.ly);
                        bool farL = s2nmc::far_out(C.natfar != 0, md, far_r(e.st == DEAD));
                        if (farL) { e.farLost = true; ++g_lsFar; }
                        if (kill && !farL) { ++e.lostN; g_lsMax = std::max(g_lsMax, e.lostN); }
                        if (kill) { cc_uid(g_ai2, STR("CustomConsoleManagerAI2"), STR("XDestroyModel"), STR("UID"), e.luid); ++g_lsKill; }
                        bool give = kill && !farL && e.lostN >= C.natlostmax;
                        if (give) { e.gaveUp = true; ++g_lsGive; }
                        if (C.natlog && (e.lostN <= C.natlostmax || g_lsLog < 40))
                        {
                            ++g_lsLog;
                            char b[560];
                            snprintf(b, sizeof b, "NOLA natnpc: corpo da copia perdido host uid=%d (_%s) how=%d %s uid_local=%d%s perdas=%d idade=%.2f s ultima_pos=%.0f,%.0f,%.0f host=%.0f,%.0f,%.0f dz=%.0f dist_jogador=%.0f%s%s",
                                     e.h.uid, e.h.nm.c_str(), e.how, nul ? "ponteiro_nulo" : "no_pool", e.luid, kill ? " XDestroyModel" : "", e.lostN, now - e.takeAt,
                                     e.lx, e.ly, e.lz, e.h.x, e.h.y, e.h.zg, e.lz - e.hh - e.h.zg, md, farL ? " fora_do_raio (nao conta; refeita quando o jogador voltar)" : "", give ? " -> desiste desta uid (natlostmax)" : "");
                            logl(b);
                        }
                        if (kill) e.luid = 0;
                    }
                    if (e.rmPin) { e.rmPin = false; root_lock(e, a, false); }
                    cov_unmark(e);
                    if (e.covMm0 >= 0) e.covMm0 = -1;
                    e.body.reset(); e.st = NONE; e.shown = false; ++K.errs; K.lastErr = "corpo sumiu/pool";
                    ++it; continue;
                }
                { auto ts = Clock::now(); show_if_ready(e, a, now); sec_add(T_LOOK, ts); }
                if (e.st == LIVE && !(e.h.fl & 1))
                {
                    unkillable(e.luid, false);
                    bool byGun = kill_copy(g_dc, a, e.luid);
                    if (!e.shown && !host_hid(e)) { set_hidden(a, false); e.shown = true; }
                    interaction_off(a);
                    e.st = DEAD; e.killAt = now; e.intAt = now; ++K.kills;
                    char b[160];
                    snprintf(b, sizeof b, "NOLA natnpc: host uid=%d (_%s) morreu no host -> copia morta (%s %d), interacao off", e.h.uid, e.h.nm.c_str(), byGun ? "natdeathfx ReceiveDamageStruct type6 src2" : "XKillNpcByUID", e.luid);
                    logl(b);
                }
                else if (e.st == LIVE) { auto td = Clock::now(); drive(e, a, now, me, pup, tgt); hp_mirror(e, a); sec_add(T_DRV, td); }
                else if (e.st == DEAD)
                {
                    if (now - e.intAt >= 1) { e.intAt = now; if (!g_clown.count(reinterpret_cast<uintptr_t>(a))) interaction_off(a); }
                    if (!e.hgtLog && now - e.killAt >= 3)
                    {
                        double dch = dead_pose_log(e, a);
                        if (C.natdeadpos && e.deadRm == 0 && dch >= 0 && !s2nmc::dead_corpse_ok(true, (float)dch, (float)e.hh))
                        {
                            dead_remake(e, a, dch, now);
                            ++it; continue;
                        }
                    }
                    if (e.killTry == 0 && now - e.killAt >= 2 && alive_of(a))
                    { e.killTry = 1; unkillable(e.luid, false); kill_copy(g_dc, a, e.luid, true); }
                    else if (C.npcvis && e.killTry == 1 && now - e.killAt >= 6 && alive_of(a)) dead_out(e, a, now);
                }
            }
            else if (e.st == NONE) try_body(e, it->first, now, busy, me, pup, tgt);
            ++it;
        }
        sec_add(T_LOOP, tp);
        { double ms = (now_s() - now) * 1000; K.cliMs += ms; K.cliMax = std::max(K.cliMax, ms); ++K.cliN; }
        if (now - K.at >= 10)
        {
            if (C.natlog)
            {
                int live = 0, dead = 0, kept = 0, shown = 0, mismN = 0, mismS = 0;
                for (auto& [k, e] : K.E)
                {
                    if (e.st == LIVE) ++live;
                    if (e.st == DEAD) ++dead;
                    if (e.st == KEPT) ++kept;
                    if (e.shown) ++shown;
                    if (e.mism >= 0) { ++mismN; mismS += e.mism; }
                }
                auto pct = [](const int* h, int n, double q) -> int
                {
                    if (n <= 0) return -1;
                    int need = (int)std::ceil(q * n), acc = 0;
                    for (int i = 0; i < 41; ++i) { acc += h[i]; if (acc >= need) return i * 25 + 25; }
                    return 1025;
                };
                char hb[320];
                snprintf(hb, sizeof hb, " | b161 andando: n=%d med=%.0f p50<%d p90<%d | parado: n=%d med=%.0f p90<%d | ordens gait2/3/4=%d/%d/%d keepalive=%d | look sem_arma_prop=%d",
                         K.mvN, K.mvN ? K.mvSum / K.mvN : -1.0, pct(K.hMov, K.mvN, 0.5), pct(K.hMov, K.mvN, 0.9), K.stN, K.stN ? K.stSum / K.stN : -1.0,
                         pct(K.hStill, K.stN, 0.9), K.gaitN[2], K.gaitN[3], K.gaitN[4], K.keepOrd, g_lookNoSpecial);
                char b[1100];
                snprintf(b, sizeof b,
                         "NOLA natnpc 10 s (cliente): host=%d copias=%d (vivas=%d mortas=%d mostradas=%d) mantidas_cliente=%d novas: adotadas=%d criadas=%d (natcopy=%d %s=%d) spawn_falhou=%d | ordens=%d falhas_ordem=%d teleportes=%d erro_med=%.0f max=%.0f cm | looks aplicados=%d sem_look_timeout=%d diferencas=%d em %d copias set=%d falhou=%d assets_faltando=%d | mortes=%d removidas=%d erros=%d sem_funcao=%d falhas_pe=%d%s%s",
                         (int)K.E.size(), live + dead, live, dead, shown, kept, K.adopt, K.spawns, C.natcopy, w2u(C.natcopysid).c_str(), K.copySpawn, K.spawnFail, K.orders, K.ordFail, K.tele,
                         K.errN ? K.errSum / K.errN : 0.0, K.errMax, K.looksApplied, K.noLook, mismS, mismN, g_lookSet, g_lookFail, g_assetMiss, K.kills, K.removed, K.errs,
                         g_noFn, g_peFail, K.lastErr.empty() ? "" : " ultimo_erro=", K.lastErr.c_str());
                static int s_peLast = 0;
                if (g_peFail != s_peLast) { s_peLast = g_peFail; logl("NOLA natpe 10 s (cliente): falhas_pe=" + std::to_string(g_peFail) + " quem=" + pe_last().substr(0, 900) + " | qg_pulados=" + std::to_string(g_qgSkip) + " qg_excecao=" + std::to_string(g_qgThrow) + " ultimo_qg=" + g_qgLast); }
                {
                    int mutN = 0, mutD = 0, mutS = 0, humD = 0, conv = 0;
                    std::string w1;
                    for (auto& [k, e] : K.E)
                    {
                        if (e.st != LIVE && e.st != DEAD) continue;
                        bool chk = e.lookDone != 0 && now - e.takeAt >= 30;
                        if (e.mism > 0 && !chk) { ++conv; continue; }
                        if (e.h.mut()) { ++mutN; if (e.mism > 0) { ++mutD; mutS += e.mism; } }
                        else if (e.mism > 0) ++humD;
                        if (e.mism > 0 && w1.empty()) w1 = " primeira=uid" + std::to_string(k) + e.why.substr(0, 220);
                    }
                    char lk[760];
                    snprintf(lk, sizeof lk, "NOLA natlook 10 s (cliente): i16lk natlooksec=%d secoes_set=%d secoes_dif=%d secoes_falhou=%d cpd_set=%d cpd_dif=%d cpd_falhou=%d partes_sem_gerador=%d natgenmk=%d partes_do_host=%d feitas=%d falhou=%d refeitas=%d | natmutlook=%d mutantes=%d aplicados=%d com_diferenca=%d diferencas=%d | humanos_com_diferenca=%d convergindo=%d | natmidsc=%d mid_set=%d mid_dif=%d mid_falhou=%d mid_do_host=%d",
                             C.natlooksec, g_secSet, g_secDif, g_secFail, g_cpdSet, g_cpdDif, g_cpdFail, g_genMiss, C.natgenmk, g_genMk, g_genMkMade, g_genMkFail, g_genRemk, C.natmutlook, mutN, K.mutLook, mutD, mutS, humD, conv, C.natmidsc, g_midSet, g_midDif, g_midFail, g_midHost);
                    logl(std::string(lk) + w1);
                }
                if (!C.natgenmk) g_madeEver.clear();
                {
                    int dm = 0, dmB = 0, extra = 0, nearN = 0;
                    double dr = far_r(true);
                    for (auto& [k, e] : K.E)
                        if (e.st == DEAD) { ++dm; if (e.body.get()) ++dmB; }
                    nearN = K.deadNear; extra = K.deadExtra;
                    char db[640];
                    snprintf(db, sizeof db, "NOLA natdead 10 s (cliente): nathotkeep=%d natdeadkeep=%d copias_mortas=%d com_corpo=%d sem_corpo=%d corpos_mortos_no_raio=%d extras=%d raio=%.0f retomadas_reload=%d descartadas_reload=%d invalidas_reload=%d a_receber=%d | pose 3 s: deitadas=%d em_pe=%d sem_ragdoll=%d natdeadpos=%d refeitas_corpo=%d garbage_rejeitados=%d dist_corpo_max=%d",
                             C.nathotkeep, C.natdeadkeep, dm, dmB, dm - dmB, nearN, extra, dr, K.hotBack, K.hotDrop, K.hotBad, (int)K.hotDead.size(), K.deadFlat, K.deadUp, K.deadNoRag, C.natdeadpos, K.deadRemade, K.garbRej, (int)std::lround(K.deadDistMax));
                    logl(db);
                }
                if (C.natlegs || C.natlostfix || C.nateqfix || C.natmbh)
                {
                    char lb[1040];
                    snprintf(lb, sizeof lb, " | baixa(<0.5)=%d sem_marcha=%d vel_cmc_baixa=%d montagem=%d modo=%d | ok_com_marcha=%d | marcha_igual_host=%d/%d | natmbh=%d humanos_andando=%d marcha_do_host=%d difere_da_regra=%d | sem_socket=%d custo med=%.1f max=%.1f us (n=%d)"
                             " | natlostfix=%d perdido_nulo=%d perdido_pool=%d modelo_destruido=%d desistiu=%d perdas_max_uid=%d | natfar=%d raio_morto=%.0f fora_do_raio=%d perdas_longe=%d refeitas_ao_voltar=%d | pend sem_corpo=%d rejeitados longe=%d classe=%d hp=%d morto=%d mundo_quest=%d | nateqfix=%d troca_sem_efeito=%d uids_troca_presa=%d",
                             g_lgLow, g_lgLowGait, g_lgLowCmc, g_lgLowMont, g_lgLowMode, g_lgOkGait, g_lgMbSame, g_lgMbN, C.natmbh, g_lgHum, g_lgHumHost, g_lgHumDiff,
                             g_lgNoSock, g_lgUs / std::max(1, g_lgCalls), g_lgUsMax, g_lgCalls,
                             C.natlostfix, g_lsNull, g_lsPool, g_lsKill, g_lsGive, g_lsMax, C.natfar, C.natfardead, g_lsFarSkip, g_lsFar, g_lsBack, g_pdTo, g_pdNear, g_pdCls, g_pdHp, g_pdDead, g_pdWorld, C.nateqfix, g_eqNoFx, (int)g_eqStuck.size());
                    char kb[560];
                    snprintf(kb, sizeof kb, " | fora_da_tela=%d | natkinmv=%d lead=%.1f s ordens_kin novo=%d marcha=%d curva=%d perto=%d parada=%d mantida=%d | nateqsync=%d troca_imediata=%d montagem_troca_pulada=%d montagem_perdida_calmo=%d | natturn=%d giro_ordens=%d giro_falhas=%d giro_seguro=%d",
                             g_lgOff, C.natkinmv, C.natkinlead, g_koWhy[1], g_koWhy[2], g_koWhy[3], g_koWhy[4], g_koWhy[5], g_koKeep, C.nateqsync, g_eqNow, g_mEqSkip, g_mLostCalm, C.natturn, g_turnOrd, g_turnFail, g_yawHold);
                    char gb[260];
                    snprintf(gb, sizeof gb, " | natgaitcp=%d seletor=%s copia=%ld trocado=%ld idle_local=%ld por_que 218=%ld a8=%ld 13a=%ld 228=%ld caminho=%ld mov108=%ld",
                             C.natgaitcp, g_gsState, (long)g_gsCp, (long)g_gsOv, (long)g_gsIdle, (long)g_gsWhy[1], (long)g_gsWhy[2], (long)g_gsWhy[3], (long)g_gsWhy[4], (long)g_gsWhy[5], (long)g_gsMv);
                    char wb[600];
                    snprintf(wb, sizeof wb, " | natwpnlocal=%d hook=%s troca_local_bloqueada=%ld (wrapper=%ld impl=%ld) troca_nossa_passou=%ld | natloco=%d atualizador=%s chamadas=%ld copias=%ld escritas=%ld mantidas=%ld tag_andar=%ld tag_parado=%ld falhou=%ld bloco_estranho=%ld natlocoprm=%d leitor=%d params_recalc=%ld params_ainda_sujos=%ld natlocofl=%d tag_strafe=%ld pos_padrao=%ld", C.natwpnlocal, g_wpState, (long)(g_wpBlk + g_wpBlkImpl), (long)g_wpBlk, (long)g_wpBlkImpl, (long)g_wpOurs,
                             C.natloco, g_luState, (long)g_luCall, (long)g_luCp, (long)g_luWr, (long)g_luKeep, (long)g_luMove, (long)g_luIdle, (long)g_luFail, (long)g_luBad, C.natlocoprm, g_luPrm ? 1 : 0, (long)g_luPrmOk, (long)g_luPrmBad, C.natlocofl, (long)g_luStrafe, (long)g_luStockMode);
                    char mb2[640];
                    snprintf(mb2, sizeof mb2, " | por_marcha_host andar n=%d ok=%d raiz=%d cmc_baixa=%d modo2=%d correr n=%d ok=%d raiz=%d cmc_baixa=%d modo2=%d sprint n=%d ok=%d raiz=%d cmc_baixa=%d modo2=%d"
                             " | mm_parado adotada=%d/%d criada=%d/%d controle_total=%d/%d mutante=%d/%d sem_mm=%d baixa_com_mm_parado=%d/%d",
                             g_lgG[2].n, g_lgG[2].ok, g_lgG[2].raiz, g_lgG[2].cmc0, g_lgG[2].rm2, g_lgG[3].n, g_lgG[3].ok, g_lgG[3].raiz, g_lgG[3].cmc0, g_lgG[3].rm2,
                             g_lgG[4].n, g_lgG[4].ok, g_lgG[4].raiz, g_lgG[4].cmc0, g_lgG[4].rm2,
                             g_lgMmFz[0], g_lgMmN[0], g_lgMmFz[1], g_lgMmN[1], g_lgMmFz[2], g_lgMmN[2], g_lgMmFz[3], g_lgMmN[3],
                             g_lgMmNone[0] + g_lgMmNone[1] + g_lgMmNone[2] + g_lgMmNone[3], g_lgLowMmFz, g_lgLow);
                    char tb2[900];
                    snprintf(tb2, sizeof tb2, " | natlocofl tag_trocada=%ld bits_trocados=%ld vel_trocada=%ld tag_outro=%ld | nattraj=%d construtor=%s chamadas=%ld copias=%ld futuro_preenchido=%ld futuro_stock=%ld parado=%ld idle_forcado_movendo=%ld cresceu=%ld falhou=%ld nattrajv=%d vel_escrita=%ld vel_falhou=%ld vel_salto=%ld natlocosf=%d strafe_sem_tag=%ld"
                             " | nattrajown=%d futuro_proprio_trocado humano=%ld mutante=%ld fora_45 humano=%ld mutante=%ld para_tras humano=%ld mutante=%ld | natcpvel=%d vel_host_no_cmc=%ld mutante=%ld cmc_diferente=%ld",
                             (long)g_luTagW, (long)g_luBits, (long)g_luSpW, (long)g_luOther, C.nattraj, g_tbState, (long)g_tbCall, (long)g_tbCp, (long)g_tbFill, (long)g_tbStock, (long)g_tbStill, (long)g_tbIdle, (long)g_tbGrow, (long)g_tbFail, C.nattrajv, (long)g_tbVel, (long)g_tbVelBad, (long)g_tbVelJump, C.natlocosf, (long)g_luSfFix,
                             C.nattrajown, (long)g_tbOwn[0], (long)g_tbOwn[1], (long)g_tbOwnOff[0], (long)g_tbOwnOff[1], (long)g_tbOwnBack[0], (long)g_tbOwnBack[1], C.natcpvel, (long)g_tbCv, (long)g_tbCvMut, (long)g_tbCvBad);
                    logl("NOLA natlegs 10 s (cliente): natlegs=" + std::to_string(C.natlegs) + " " + g_lgCli[0].text("adotada") + " | " + g_lgCli[1].text("criada") + " | " + g_lgCli[2].text("controle_total") + " | " + g_lgCli[3].text("mutante") + lb + kb + gb + wb + mb2 + tb2);
                    for (auto& w : g_lgWorst) logl(w.second);
                }
                auto pctN = [](const int* h, int nb, int n, double q, double w) -> double
                {
                    if (n <= 0) return -1;
                    int need = (int)std::ceil(q * n), acc = 0;
                    for (int i = 0; i < nb; ++i) { acc += h[i]; if (acc >= need) return (i + 1) * w; }
                    return nb * w;
                };
                int fx = 0, rn = 0, vd = 0, soft = 0, kd = 0, kf = 0;
                for (auto& [k, e] : K.E)
                {
                    if (e.h.fl & 256) ++fx; else ++rn;
                    if (e.h.fl & 512) ++vd;
                    if ((e.st == LIVE || e.st == DEAD) && e.how == 3) ++soft;
                    if ((e.st == LIVE || e.st == DEAD) && e.how == 4) ++kf;
                    kd += e.lostDead;
                }
                char b2[600];
                snprintf(b2, sizeof b2,
                         " | b162 idade_real med=%.0f p50<%.0f p90<%.0f max=%.0f ms (n=%d %s%s) | ordem->movimento n=%d med=%.2f p90<%.1f max=%.2f s sem_partida=%d | vel copia/host=%.2f (n=%d) | ordens/s por copia=%.2f empurroes=%d"
                         " | aparencia fixa=%d aleatoria=%d so_adotar_espera=%d | vendedores host=%d aqui=%d (linhas=%d) suaves=%d pausa_conversa=%d | corpos guardados=%d corte=%d",
                         K.ageN ? K.ageSum / K.ageN * 1000 : -1.0, pctN(K.ageH, 101, K.ageN, 0.5, 10), pctN(K.ageH, 101, K.ageN, 0.9, 10), K.ageMax * 1000, K.ageN,
                         K.hasStamp ? "carimbo" : "SEM carimbo (host antigo)", K.hasStamp ? (K.sameClock ? " mesmo relogio" : " relogio outro PC") : "",
                         K.latN, K.latN ? K.latSum / K.latN : -1.0, pctN(K.latH, 31, K.latN, 0.9, 0.1), K.latMax, K.latCap, K.ratN ? K.ratSum / K.ratN : -1.0, K.ratN,
                         live > 0 ? K.orders / (10.0 * live) : 0.0, K.nudges, fx, rn, K.fixedWait, vd, (int)K.vend.size(), K.vendRows, soft, K.vendPause, kd, K.corpseCap);
                char b3[700];
                snprintf(b3, sizeof b3,
                         " | b168 natcb=%d combate: amostras=%d copia_inCombat=%d arma_na_mao=%d eq_igual=%d olhar_igual=%d | troca_arma=%d falhou=%d gatilho_off=%d olhar_estado=%d/%d ferido=%d derrubado=%d acertos_host=%d"
                         " | montagens tocadas=%d falhou=%d sem_asset=%d dinamicas=%d paradas=%d | virada err_med=%.0f graus n=%d corrigida=%d | maxwalk=%d segura=%d empurroes_parado=%d",
                         C.natcb, K.cN, K.cIn, K.cW, K.cEq, K.cLs, K.eqSet, K.eqFail, K.shOff, K.lsSet, K.lsFail, K.wound, K.knock, K.hits,
                         K.mPlay, K.mFail, K.mMiss, K.mDyn, K.mStop, K.faceN ? K.faceSum / K.faceN : -1.0, K.faceN, K.faceFix, K.spdSet, K.holds, K.nudgeS);
                char b4[200];
                snprintf(b4, sizeof b4, " | b169 natkept=%d kept_full=%d (novas=%d soltas=%d) suaves_kept_novas=%d conversa_adiada=%d",
                         C.natkept, kf, K.keptFull, K.keptRel, K.keptSoft, K.keptTalk);
                char b5[400];
                snprintf(b5, sizeof b5, " | b170 natthr=%d ameaca=%d/%d olhar_combate=%d relax=%d ai_on=%d | partes_novas=%d (rosto=%d) falhou=%d | montagem_longe=%d paradas=%d | snap adiado=%d feito=%d | empurrao_limitado=%d",
                         C.natthr, K.thrOn, K.thrN, K.cLook, K.relaxN, K.aiOn, g_lookMade, g_lookFaceMade, g_lookMakeFail, K.mGate, K.mGateStop, K.snapDefer, K.snapNow, K.nudgeCap);
                char b6[460];
                snprintf(b6, sizeof b6, " | b171 natforce=%d natthr=%d escritas=%d persistiu=%d perdido=%d copia_inCombat=%d/%d | ameaca_forcada persistiu=%d perdida=%d sem_ia=%d | copia_tiro_proprio=%d/%d | custo forca med=%.1f us (n=%d) tick cliente med=%.2f max=%.2f ms copias_vivas=%d",
                         C.natforce, C.natthr, K.frcW, K.frcKept, K.frcLost, K.cInC, K.cN, K.thrKept, K.thrLost, K.thrNoS, K.atkShoot, K.atkN,
                         K.frcN ? K.frcUs / K.frcN : -1.0, K.frcN, K.cliN ? K.cliMs / K.cliN : -1.0, K.cliMax, live);
                double cn = K.cliN ? (double)K.cliN : 1.0;
                auto S = [&](int i) { return g_sec[i].sum / cn; };
                double inner = g_sec[T_FACE].sum + g_sec[T_FORCE].sum + g_sec[T_CMB].sum + g_sec[T_MONT].sum + g_sec[T_STAT].sum;
                auto rgr = [&](int g) { return K.rgN[g] ? K.rgS[g] / K.rgN[g] : -1.0; };
                auto rgv = [&](int g) { return K.rgN[g] ? K.rgV[g] / K.rgN[g] : -1.0; };
                char b7[1100];
                snprintf(b7, sizeof b7,
                         " | b172 custo us/tick: parse=%.0f indice=%.0f (n=%d) corpos=%.0f look=%.0f mover=%.0f virar+olhar=%.0f forca=%.0f combate=%.0f montagem=%.0f stats=%.0f"
                         " | max us: parse=%.0f indice=%.0f laco=%.0f look=%.0f drive=%.0f virar=%.0f forca=%.0f combate=%.0f montagem=%.0f stats=%.0f"
                         " | look adiado=%d espera_assets=%d cargas_look=%d carga_montagem adiada=%d recarga_asset=%d presos=%d portao_mantido=%d | gait_adapt +%d -%d"
                         " | vel por gait 2/3/4: razao=%.2f/%.2f/%.2f host=%.0f/%.0f/%.0f n=%d/%d/%d | maxwalk jogo_med=%.0f reescrito=%d/%d | virar_pulado_andando=%d",
                         S(T_PARSE), S(T_IDX), K.idxN, (g_sec[T_LOOP].sum - g_sec[T_LOOK].sum - g_sec[T_DRV].sum) / cn, S(T_LOOK), (g_sec[T_DRV].sum - inner) / cn,
                         S(T_FACE), S(T_FORCE), S(T_CMB), S(T_MONT), S(T_STAT),
                         g_sec[T_PARSE].max, g_sec[T_IDX].max, g_sec[T_LOOP].max, g_sec[T_LOOK].max, g_sec[T_DRV].max, g_sec[T_FACE].max, g_sec[T_FORCE].max,
                         g_sec[T_CMB].max, g_sec[T_MONT].max, g_sec[T_STAT].max,
                         K.lookDefer, K.lookWait, K.lookLoads, K.mLoadDefer, g_assetReload, g_assetRooted, K.mGateKeep, K.gbUp, K.gbDn,
                         rgr(2), rgr(3), rgr(4), rgv(2), rgv(3), rgv(4), K.rgN[2], K.rgN[3], K.rgN[4], K.mwsN ? K.mwsPrev / K.mwsN : -1.0, K.mwsRw, K.mwsN, K.faceSkip);
                char b8[700];
                snprintf(b8, sizeof b8,
                         " | b173 natasync=%d pedidos=%d prontos=%d espera med=%.0f max=%.0f ms timeout=%d falhou=%d sync=%d pendentes=%d montagem_espera_async=%d"
                         " | idade_no_comando med=%.0f p50<%.0f p90<%.0f max=%.0f ms (n=%d) natagek=%.2f | chamadas ponte=%d tick100=%d natbr=%d | rosto_faltando=%d",
                         C.natasync, g_asN, g_asDone, g_asWaitN ? g_asWait / g_asWaitN * 1000 : -1.0, g_asWaitMax * 1000, g_asTo, g_asFail, g_asSync, (int)g_asReq.size(),
                         K.mAsyncWait, K.dAgeN ? K.dAgeSum / K.dAgeN * 1000 : -1.0, pctN(K.dAgeH, 101, K.dAgeN, 0.5, 10), pctN(K.dAgeH, 101, K.dAgeN, 0.9, 10),
                         K.dAgeMax * 1000, K.dAgeN, C.natagek, K.viaBr, K.viaT, C.natbr, g_lookFaceMiss);
                char b9[400];
                snprintf(b9, sizeof b9, " | b176 natfind=%d rapido=%d achados=%d lento=%d lento_ms=%.0f max=%.1f | natpoll=%d lidas=%d chamadas_poll=%d lua_puladas=%d falhas_leitura=%d leitura_ms=%.1f",
                         C.natfind, g_fFast, g_fFound, g_fSlow, g_fSlowMs, g_fSlowMax, C.natpoll, g_pollRead, g_viaP, g_pollSkip, g_pollBad, g_pollMs);
                char b10[600];
                snprintf(b10, sizeof b10, " | b182 natkin=%d quadros=%d escritas=%d falhou=%d snaps=%d tomadas=%d soltas=%d copias_max=%d custo med=%.0f max=%.0f us/quadro atraso=%.0f ms | natrmoff=%d aplicado=%d jogo_moveu_de_volta=%d"
                         " | natnavw=%d (%s) navmesh_pulado=%ld navmesh_nativo=%ld copias_max=%ld",
                         C.natkin, g_kF, g_kW, g_kFail, g_kS, g_kTake, g_kRel, g_kMaxN, g_kF ? g_kUs / g_kF : -1.0, g_kUsMax, C.natkind, C.natrmoff, g_rmN, g_rmHit,
                         C.natnavw, g_nwState, (long)g_nwSkip, (long)g_nwPass, (long)g_nwMaxN);
                char b11[280];
                snprintf(b11, sizeof b11, " | b185 natkinord=%d natkinstop=%d natkinpin=%d prender=%d falhou=%d sem_anim=%d | natwpnguard=%d troca_sem_anim=%d montagem_troca_arma_mantida=%d",
                         C.natkinord, C.natkinstop, C.natkinpin, g_kPin, g_kPinFail, g_kNoAnim, C.natwpnguard, g_kEqNoAnim, g_mEqHold);
                char b12[260];
                snprintf(b12, sizeof b12, " | b191 montnull=%d (%s) nulos=%ld total=%ld copia=%d livre=%d ultimo=+%#llx %s | natnoatk=%d (%s) ataques_copia=%d pulados=%d",
                         C.montnull, g_mpState, (long)g_mpNull, (long)g_mpNullTot, g_mpCopy, g_mpFree, (unsigned long long)g_mpLastRet, g_mpLastWho.c_str(),
                         C.natnoatk, g_maState, g_maCopy, g_maSkip);
                char b13[160];
                snprintf(b13, sizeof b13, " | b191 natstance=%d (%s) forcado=%ld igual=%ld entradas=%d", C.natstance, g_stState, (long)g_stForced,
                         (long)g_stSame, stance_entries());
                char b15[1600];
                snprintf(b15, sizeof b15, " | b191w natthrsrc=%d (%s) mantido=%ld nativo=%ld | nathol=%d (%s) pulados=%d | natstsrc=%d escritas=%d limpas=%d"
                         " | b191h arma_par=%d falha=%d sem_classe=%d | b191k rosto_init=%d rosto_retirado=%d rosto_pulado=%d anim_perdida=%d | b191c natcover=%d escritas=%d limpas=%d modo=%d modo_falha=%d em_cobertura=%d cov_badcb=%d cov_pose=%d (%s) mont_sync=%d natmsec=%d taxa=%d secoes=%d sec_dif=%d sec_falha=%d mdiag espera=%d acabou=%d curta=%d outra=%d atualizada=%d fim_mantido=%d volta=%d limite=%d"
                         " | b191g natgen=%d (%s) refeitos=%d ok=%d falhas=%d gancho=%d feitas_removidas=%d ms_max=%.2f natgenmre=%d remontadas=%d natmloop=%d laco=%d natmkeep=%d natmovl=%d sobre_host=%d sobre_tocadas=%d sobre_sync=%d paradas_extra=%d duplas=%d",
                         C.natthrsrc, g_thState, (long)g_thKeep, (long)g_thNat, C.nathol, g_hoState, g_hoSkip, C.natstsrc, g_ssW, g_ssClr,
                         g_wpSet, g_wpFail, g_wpNoCls, g_faceInit, g_faceRetire, g_faceSkip, g_animLost, C.natcover, g_covW, g_covClr, g_covMode, g_covModeFail, g_covHold, g_covBad, g_cvHits ? (int)*g_cvHits : 0, g_cvState, g_mSync, C.natmsec, g_msRate, g_msNext, g_msMis, g_msFail, g_mxWait, g_mxEnd, g_mxShort, g_mxOther, g_mRefresh, g_msKeep, g_msWrap, g_msClamp, C.natgen, g_genState, g_genRe, g_genOk, g_genFail, g_genHook, g_genDrop, g_genMsMax, C.natgenmre, g_genMre, C.natmloop, g_msLoop, C.natmkeep, C.natmovl, g_moHost, g_moPlay, g_moSync, g_moStop, g_moAll);
                char b14[200];
                snprintf(b14, sizeof b14, " | b195 invnull=%d (%s) chamadas=%ld guardado=%ld total=%ld ultimo=+%#llx", C.invnull, g_ivState,
                         (long)g_ivCalls, (long)g_ivGuard, (long)g_ivGuardTot, (unsigned long long)g_ivLastRet);
                char b16[400];
                snprintf(b16, sizeof b16, " | b196 natbullet=%d (%s) recebidos=%d novos=%d perdidos=%d disparados=%d projeteis=%ld quadros_replica=%ld impactos=%ld dano_bloqueado=%ld"
                         " falhou: sem_ent=%d sem_copia=%d sem_comp=%d velhos=%d excecao=%d | idade med=%.0f max=%.0f ms | custo med=%.1f max=%.1f us/quadro",
                         C.natbullet, g_bState, g_bRx, g_bNew, g_bLost, g_bFire, (long)g_bProj, (long)g_bPt, (long)g_bImp, (long)g_bBlk,
                         g_bNoEnt, g_bNoCopy, g_bNoComp, g_bStale, g_bExc, g_bFire ? g_bAgeSum / g_bFire : -1.0, g_bAgeMax,
                         g_bFr ? g_bUs / g_bFr : -1.0, g_bUsMax);
                int parked = 0;
                for (auto& [k, e] : K.E) parked += e.st == PARK;
                char b17[560];
                snprintf(b17, sizeof b17, " | a1 natlookst=%d (%s) look_parado=%d noop=%d falhou=%d pulado=%d | natpark=%d guardadas=%d entraram=%d voltaram=%d sairam=%d gemeos=%d (entraram=%d adotados=%d sairam=%d) segurar=%d falhou=%d duplicadas=%d spawn_pegou_mundo_evitado=%d trocas_corpo_real=%d | nathys=%.2f",
                         C.natlookst, g_lkState, K.lookStop, K.lookNoop, K.lookStopFail, K.lookSkip, C.natpark, parked, K.parkIn, K.parkBack, K.parkOut, (int)K.twin.size(), K.twinIn, K.twinBack, K.twinOut, K.parkHold, K.parkHoldFail, K.dupSpawn, K.pendWorld, K.swapReal, C.nathys);
                int mutN = 0;
                for (auto& [k, e] : K.E) mutN += (e.st == LIVE || e.st == DEAD) && e.h.mut();
                char b18[320];
                snprintf(b18, sizeof b18, " | m1 natmut=%d classes=%d mutantes=%d sem_sid=%d natadopt_kept=%d mantidos_prototipo=%d | park uids_host=%d listas=%d link=%d | k1 laco_10s: ciclos=%d duplicadas=%d corpo_morto_nativo=%d recusados=%d espera=%d",
                         C.natmut, (int)g_cls.size(), mutN, K.noSid, C.natadopt_kept, K.keptProto, (int)K.hu.size(), K.nuRx, host_link(), K.swapW, K.dupW, K.pkDeadW, K.pkRefW, K.pkWaitW);
                char b19[240];
                snprintf(b19, sizeof b19, " | ms1 natgait=%d mutante_quadros=%d marcha_host=%d igual_host=%d classe=%d regra_humana_difere=%d kin_sem_reforco=%d andar=%d correr=%d sprint=%d trote=%d",
                         C.natgait, g_gtMut, g_gtHost, g_gtSame, g_gtCls, g_gtOld, g_gtLock, g_gtN[2], g_gtN[3], g_gtN[4], g_gtN[5]);
                char b20[360];
                snprintf(b20, sizeof b20, " | ma1 natmab=%d mutante_quadros=%d montagem tocou=%d falhou=%d sem_asset=%d dinamica=%d parou_local=%d gate=%d espera_asset=%d raiz_travada=%d raiz_devolvida=%d"
                         " | invis (%s) visivel=%d invisivel=%d sem_comp=%d tabela=%d gerente_forcado=%ld pedido_trocado=%ld alvo_escrito=%ld morto=%ld excecao=%ld",
                         C.natmab, g_mbN, g_mbPlay, g_mbFail, g_mbMiss, g_mbDyn, g_mbStop, g_mbGate, g_mbWait, g_mbRoot, g_mbRootOff,
                         g_imState, g_ivPub[0], g_ivPub[1], g_ivNoComp, inv_entries(), (long)g_imHit, (long)g_imForced, (long)g_imWr, (long)g_imDead, (long)g_imExc);
                char b20c[200];
                snprintf(b20c, sizeof b20c, " | i16d1 natcloak=%d aplicar (%s) atual_recebido=%d erro_max_pct=%d atual_igualado=%d material_reaplicado=%d excecao=%d",
                         C.natcloak, g_iaState, g_clRx, g_clMaxErr, g_clSnap, g_clRe, g_clExc);
                char b21[260];
                snprintf(b21, sizeof b21, " | zk1 natqg=%d linhas_com_guid=%d guids_lista=%d adotados_guid=%d (10s=%d) listados_guid=%d gemeos_guid=%d spawn_evitou_quest=%d espera_quest=%d ator_quest_poupado=%d guid_lido=%d falhou=%d",
                         C.natqg, K.qgRowsRx, (int)K.hq.size(), K.qgAdopt, K.qgAdoptW, K.qgListed, K.qgTwin, K.qgPendSkip, K.qgWait, K.qgKeep, g_qgCalls, g_qgFail);
                char b22[200];
                snprintf(b22, sizeof b22, " | natwpnhid oculto_escalado=%d restaurado=%d npcs=%d sweeps=%d us_media=%.1f us_max=%.1f",
                         g_wpnWinHide, g_wpnWinShow, g_wpnWinN, g_wpnWinSweeps, g_wpnWinSweeps ? g_wpnWinUs / g_wpnWinSweeps : 0.0, g_wpnWinUsMax);
                logl(std::string(b) + hb + b2 + b3 + b4 + b5 + b6 + b7 + b8 + b9 + b10 + b11 + b12 + b13 + b15 + b14 + b16 + b17 + b18 + b19 + b20 + b20c + b21 + b22);
                g_wpnWinHide = g_wpnWinShow = g_wpnWinSweeps = 0; g_wpnWinUs = g_wpnWinUsMax = 0;
            }
            for (auto& q : g_sec) q = SecT{};
            g_kF = g_kW = g_kFail = g_kS = 0; g_kUs = g_kUsMax = 0; g_kMaxN = 0; g_kTake = g_kRel = 0;
            g_kPin = g_kPinFail = g_kNoAnim = g_kEqNoAnim = g_mEqHold = 0;
            g_rmN = g_rmHit = 0;
            InterlockedExchange(&g_nwSkip, 0); InterlockedExchange(&g_nwPass, 0); InterlockedExchange(&g_nwMaxN, 0);
            g_gtMut = g_gtHost = g_gtSame = g_gtCls = g_gtOld = g_gtLock = 0; std::fill(std::begin(g_gtN), std::end(g_gtN), 0);
            g_mbN = g_mbPlay = g_mbFail = g_mbMiss = g_mbDyn = g_mbStop = g_mbGate = g_mbWait = g_mbRoot = g_mbRootOff = 0; g_ivPub[0] = g_ivPub[1] = g_ivNoComp = 0;
            g_clSnap = g_clRe = g_clExc = g_clRx = g_clMaxErr = 0;
            InterlockedExchange(&g_imHit, 0); InterlockedExchange(&g_imForced, 0); InterlockedExchange(&g_imWr, 0); InterlockedExchange(&g_imDead, 0); InterlockedExchange(&g_imExc, 0);
            InterlockedExchange(&g_stForced, 0); InterlockedExchange(&g_stSame, 0);
            InterlockedExchange(&g_thKeep, 0); InterlockedExchange(&g_thNat, 0); g_hoSkip = g_ssW = g_ssClr = 0; g_wpSet = g_wpFail = g_wpNoCls = 0; g_faceInit = g_faceRetire = g_faceSkip = g_animLost = 0; g_covW = g_covClr = g_covBad = 0; if (g_cvHits) InterlockedExchange(g_cvHits, 0); g_covMode = g_covModeFail = g_covHold = g_mSync = 0; g_msRate = g_msNext = g_msMis = g_msFail = 0; g_mxWait = g_mxEnd = g_mxOther = g_mxShort = g_mRefresh = g_msKeep = g_msWrap = g_msClamp = 0; g_genRe = g_genOk = g_genFail = g_genHook = g_genDrop = g_genMre = g_msLoop = 0; g_genMsMax = 0;
            InterlockedExchange(&g_ivCalls, 0); InterlockedExchange(&g_ivGuard, 0);
            g_bRx = g_bNew = g_bLost = g_bNoEnt = g_bFire = g_bNoCopy = g_bNoComp = g_bStale = g_bExc = g_bOffT = 0;
            InterlockedExchange(&g_bProj, 0); InterlockedExchange(&g_bImp, 0); InterlockedExchange(&g_bBlk, 0); InterlockedExchange(&g_bPt, 0);
            g_bAgeSum = g_bAgeMax = g_bUs = g_bUsMax = 0; g_bFr = 0; bown_prune();
            InterlockedExchange(&g_mpNull, 0); g_mpCopy = g_mpFree = g_maCopy = g_maSkip = 0;
            g_asN = g_asDone = g_asTo = g_asFail = g_asSync = g_asWaitN = 0; g_asWait = g_asWaitMax = 0; g_lookFaceMiss = 0;
            g_fFast = g_fFound = g_fSlow = 0; g_fSlowMs = g_fSlowMax = 0; g_viaP = g_pollSkip = g_pollRead = g_pollBad = 0; g_pollMs = 0;
            std::fill(std::begin(K.dAgeH), std::end(K.dAgeH), 0); K.dAgeN = K.mAsyncWait = K.viaBr = K.viaT = 0; K.dAgeSum = K.dAgeMax = 0;
            K.mutLook = 0; g_secSet = g_secDif = g_secFail = g_cpdSet = g_cpdDif = g_cpdFail = g_genMiss = g_genMk = g_genMkMade = g_genMkFail = g_genRemk = g_midSet = g_midDif = g_midFail = g_midHost = 0;
            K.lookWait = K.lookLoads = 0; K.gbUp = K.gbDn = K.faceSkip = K.lookDefer = K.mLoadDefer = K.mGateKeep = K.idxN = K.mwsN = K.mwsRw = 0; K.mwsPrev = 0; g_assetReload = 0;
            std::fill(std::begin(K.rgN), std::end(K.rgN), 0); std::fill(std::begin(K.rgS), std::end(K.rgS), 0.0); std::fill(std::begin(K.rgV), std::end(K.rgV), 0.0);
            K.frcW = K.frcKept = K.frcLost = K.thrKept = K.thrLost = K.thrNoS = K.frcN = K.atkShoot = K.atkN = K.cInC = K.cliN = 0; K.frcUs = K.cliMs = K.cliMax = 0;
            K.keptFull = K.keptSoft = K.keptTalk = K.keptRel = 0; K.keptProto = K.noSid = K.nuRx = 0; K.swapW = K.dupW = K.pkDeadW = K.pkRefW = K.pkWaitW = 0; K.qgAdoptW = 0;
            K.snapDefer = K.snapNow = K.mGate = K.mGateStop = K.aiOn = K.thrN = K.thrOn = K.relaxN = K.cLook = K.nudgeCap = 0; g_lookMade = g_lookFaceMade = g_lookMakeFail = 0;
            K.cN = K.cIn = K.cW = K.cEq = K.cLs = K.eqSet = K.eqFail = K.shOff = K.lsSet = K.lsFail = K.wound = K.knock = K.hits = 0;
            K.mPlay = K.mFail = K.mMiss = K.mDyn = K.mStop = K.faceN = K.faceFix = K.spdSet = K.holds = K.nudgeS = 0; K.faceSum = 0;
            std::fill(std::begin(K.ageH), std::end(K.ageH), 0); std::fill(std::begin(K.latH), std::end(K.latH), 0);
            K.ageN = K.latN = K.latCap = K.ratN = K.nudges = K.vendPause = K.fixedWait = K.corpseCap = 0;
            K.ageSum = K.ageMax = K.latSum = K.latMax = K.ratSum = 0;
            for (auto& h : g_lgCli) h.clear();
            g_lgWorst.clear();
            for (auto& ga : g_lgG) ga = GaitAgg{};
            for (int i = 0; i < 4; ++i) g_lgMmN[i] = g_lgMmFz[i] = g_lgMmNone[i] = 0;
            g_lgLowMmFz = 0;
            g_lgLow =g_lgLowGait = g_lgLowCmc = g_lgLowMont = g_lgLowMode = g_lgMbN = g_lgMbSame = g_lgOkGait = 0;
            g_lgHum = g_lgHumHost = g_lgHumDiff = 0; g_lgNoSock = 0; g_lgUs = g_lgUsMax = 0; g_lgCalls = 0;
            g_lsNull = g_lsPool = g_lsKill = g_lsGive = g_lsFarSkip = 0;
            g_pdNear = g_pdCls = g_pdHp = g_pdDead = g_pdWorld = g_pdTo = 0;
            g_eqNoFx = 0;
            g_luCall = g_luCp = g_luWr = g_luFail = g_luBad = g_luIdle = g_luMove = g_luKeep = g_luPrmOk = g_luPrmBad = g_luStrafe = g_luStockMode = 0;
            g_gsCp = g_gsOv = g_gsIdle = g_gsMv = 0; for (auto& w : g_gsWhy) w = 0;
            std::fill(std::begin(g_koWhy), std::end(g_koWhy), 0); g_koKeep = g_lgOff = g_eqNow = g_mEqSkip = g_mLostCalm = g_turnOrd = g_turnFail = g_yawHold = 0;
            K.at = now; K.orders = K.tele = K.takes = K.adopt = K.spawns = K.copySpawn = K.spawnFail = K.kills = K.removed = K.errs = K.errN = K.ordFail = K.looksApplied = K.noLook = 0;
            K.errSum = K.errMax = 0; K.lastErr.clear();
            std::fill(std::begin(K.hMov), std::end(K.hMov), 0); std::fill(std::begin(K.hStill), std::end(K.hStill), 0);
            std::fill(std::begin(K.gaitN), std::end(K.gaitN), 0);
            K.mvSum = K.stSum = 0; K.mvN = K.stN = K.keepOrd = 0;
        }
        return true;
    }

    struct PollSt
    {
        bool init = false;
        std::wstring path;
        FILETIME ft{};
        double lastRun = -100, argAt = -100, meA = 0, pupA = 0, tgtA = 0;
        int busy = 0, mode = 0;
    } g_poll;
    bool g_feedOn = false, g_feedNew = false;
    std::string g_feedText;
    auto poll_path() -> void
    {
        g_poll.init = true;
        wchar_t e[1024];
        DWORD n = GetEnvironmentVariableW(L"S2_BRIDGE", e, 1024);
        std::wstring dir;
        if (n > 0 && n < 1024) dir = e;
        if (dir.empty()) dir = L"C:\\nola_bridge\\";
        if (dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';
        g_poll.path = dir + L"in.json";
        logl("NOLA b176 natpoll: " + w2u(g_poll.path));
    }
    auto poll_in() -> void
    {
        double now = now_s();
        if (g_poll.mode != 1 || now - g_poll.argAt > 0.5) return;
        WIN32_FILE_ATTRIBUTE_DATA fa{};
        auto t0 = Clock::now();
        std::string t;
        if (g_feedOn)
        {
            if (!g_feedNew) return;
            g_feedNew = false;
            t.swap(g_feedText);
            fa.ftLastWriteTime = g_poll.ft;
        }
        else
        {
            if (!g_poll.init) poll_path();
            if (!GetFileAttributesExW(g_poll.path.c_str(), GetFileExInfoStandard, &fa)) return;
            if (CompareFileTime(&fa.ftLastWriteTime, &g_poll.ft) == 0) return;
            t0 = Clock::now();
            HANDLE h = CreateFileW(g_poll.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) { ++g_pollBad; return; }
            DWORD sz = GetFileSize(h, nullptr), got = 0;
            if (sz != INVALID_FILE_SIZE && sz > 0 && sz < (16u << 20)) { t.resize(sz); if (!ReadFile(h, t.data(), sz, &got, nullptr)) got = 0; t.resize(got); }
            CloseHandle(h);
        }
        size_t endH = std::min(t.find("\"apps\""), t.find("\"npcs\""));
        size_t k = t.find("\"x1\"");
        if (k == std::string::npos || k > endH) { ++g_pollBad; return; }
        size_t q = t.find('"', t.find(':', k + 4));
        size_t q2 = q == std::string::npos ? q : t.find('"', q + 1);
        if (q2 == std::string::npos) { ++g_pollBad; return; }
        g_poll.ft = fa.ftLastWriteTime;
        ++g_pollRead;
        std::string x1 = t.substr(q + 1, q2 - q - 1);
        auto part = [&](const char* tag) -> std::string
        {
            size_t i = x1.find(tag);
            if (i == std::string::npos) return {};
            i += std::strlen(tag);
            size_t j = x1.find('#', i);
            return x1.substr(i, j == std::string::npos ? std::string::npos : j - i);
        };
        g_pollMs += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (client_run(1, part("#NN:"), part("#NL:"), g_poll.meA, g_poll.pupA, g_poll.tgtA, g_poll.busy, 2)) g_poll.lastRun = now;
        try { dmg_client(part("#ND:"), now_s()); } catch (...) { ++g_dcExc; }
    }
    auto kin_frame() -> void
    {
        double now = now_s();
        double dt = g_kLast < 0 ? 0.016 : std::clamp(now - g_kLast, 0.0005, 0.1);
        g_kLast = now;
        bool run = C.natkin && g_poll.mode == 1 && now - g_poll.argAt <= 0.5;
        if (!run) { if (g_kAny) { for (auto& [k, e] : K.E) kin_release(e); g_kAny = false; } return; }
        auto t0 = Clock::now();
        int n = 0;
        for (auto& [k, e] : K.E)
        {
            UObject* a = e.st == LIVE ? e.body.get() : nullptr;
            if (!a || !(e.h.fl & 1) || e.how == 3 || e.vend || now - e.h.recv > 1.5 || e.kn <= 0) { kin_release(e); continue; }
            if (K.talkTo == a) continue;
            UObject* animI = nullptr;
            {
                UObject* mesh = obj_at(a, STR("Mesh"));
                animI = mesh ? obj_at(mesh, STR("AnimScriptInstance")) : nullptr;
                if (!animI) { ++g_kNoAnim; kin_release(e); continue; }
            }
            double tx, ty, tz, tyaw, tvx, tvy;
            if (!kin_target(e, now - C.natkind / 1000.0, tx, ty, tz, tyaw, tvx, tvy)) continue;
            tz += e.hh;
            if (!e.kOn)
            {
                e.kOn = true; e.kAt = -1; e.kmt = -1; ++g_kTake; g_kAny = true;
                if (C.natkinstop)
                    if (UObject* ctl = obj_at(a, STR("Controller"))) { Call c(ctl, STR("StopMovement")); c.run(); }
            }
            if (e.kAt < 0)
            {
                double x, y, z;
                if (!loc_of(a, x, y, z)) continue;
                e.kx = x; e.ky = y; e.kz = z; e.kyaw = yaw_of(a);
            }
            else if (C.natrmoff)
            {
                double px, py, pz;
                if (loc_of(a, px, py, pz) && std::hypot(px - e.kx, py - e.ky) > C.natrmpin)
                {
                    ++g_rmHit; ++e.pinHit;
                    rm_off(e, a, now, true);
                    if (g_rmLog < 40 && (e.pinHit == 1 || e.pinHit % 120 == 0))
                    {
                        ++g_rmLog;
                        char pb[320];
                        snprintf(pb, sizeof pb, "NOLA natrmoff: uid=%d (_%s) how=%d the game moved the copy back after our write: wrote=%.0f,%.0f read=%.0f,%.0f dist=%.0f cm hits=%d (root motion scale 0 + motion warping cleared)",
                                 e.h.uid, e.h.nm.c_str(), e.how, e.kx, e.ky, px, py, std::hypot(px - e.kx, py - e.ky), e.pinHit);
                        logl(pb);
                    }
                }
            }
            e.kx += tvx * dt; e.ky += tvy * dt;
            double ex = tx - e.kx, ey = ty - e.ky;
            if (std::hypot(ex, ey) > C.natkinsnap) { e.kx = tx; e.ky = ty; ++g_kS; }
            else { double kb = 1 - std::exp(-dt * C.natking); e.kx += ex * kb; e.ky += ey * kb; }
            e.kz += (tz - e.kz) * (1 - std::exp(-dt * 12));
            if (std::fabs(tz - e.kz) > 150) e.kz = tz;
            bool yhold = false;
            bool ycalm = C.natturn && std::hypot(tvx, tvy) <= C.natstill && !e.h.mut() && !(C.natcb && e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16)));
            bool ystart = (e.turnOk && std::fabs(std::remainder(e.h.yaw - e.kyaw, 360.0)) > 30) || now - e.turnAt < 1.8;
            if (ycalm && (ystart || (e.yawHoldAt >= 0 && e.turnOk && std::fabs(std::remainder(tyaw - e.kyaw, 360.0)) > 5)))
            {
                if (e.yawHoldAt < 0) { e.yawHoldAt = now; ++g_yawHold; }
                yhold = now - std::max(e.turnAt, e.yawHoldAt) < 1.8;
            }
            else e.yawHoldAt = -1;
            if (yhold) e.kyaw = yaw_of(a);
            else e.kyaw += std::remainder(tyaw - e.kyaw, 360.0) * (1 - std::exp(-dt * 12));
            e.kAt = now;
            {
                Call c(a, STR("K2_SetActorLocationAndRotation"));
                c.vec(STR("NewLocation"), e.kx, e.ky, e.kz).vec(STR("NewRotation"), 0, e.kyaw, 0).bl(STR("bSweep"), false).bl(STR("bTeleport"), false);
                c.run() ? ++g_kW : ++g_kFail;
            }
            if (C.natkinreq)
                if (UObject* cmc = obj_at(a, STR("CharacterMovement")))
                {
                    double sp = std::hypot(tvx, tvy);
                    auto* b = reinterpret_cast<uint8_t*>(cmc);
                    int orv = off_of(cmc, STR("RequestedVelocity")), ob = off_of(cmc, STR("bHasRequestedVelocity")), om = off_of(cmc, STR("MaxWalkSpeed"));
                    if (sp > 20 && orv >= 0 && ob >= 0)
                    {
                        wr_v3_seh(b + orv, tvx, tvy, 0);
                        int v = rd_u8_seh(b + ob);
                        if (v >= 0) wr_u8_seh(b + ob, (v | 1) & ~2);
                        if (om >= 0 && C.natkinmws > 0)
                        {
                            if (e.kmws0 < 0) e.kmws0 = rd_f_seh(b + om);
                            wr_f_seh(b + om, (float)std::max(120.0, sp * C.natkinmws));
                        }
                    }
                }
            if (e.kmt < 0) { e.kmx = e.kx; e.kmy = e.ky; e.kmt = now; }
            else if (now - e.kmt >= 0.2) { e.kcsp = std::hypot(e.kx - e.kmx, e.ky - e.kmy) / (now - e.kmt); e.kmx = e.kx; e.kmy = e.ky; e.kmt = now; }
            ++n;
        }
        g_kMaxN = std::max(g_kMaxN, n);
        if (n > 0)
        {
            double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
            ++g_kF; g_kUs += us; g_kUsMax = std::max(g_kUsMax, us);
        }
    }
    auto cp_refresh() -> void
    {
        if (!g_ddDet && !g_gsDet && !g_luDet && !g_tbDet) return;
        if (g_luDet) lu_tags();
        LONG n = 0;
        uintptr_t pe[kCpN], pa[kCpN], pxh[kCpN], pxm[kCpN], pcm[kCpN];
        uint32_t pi[kCpN];
        LONG pu[kCpN], pg[kCpN], ps[kCpN], ph[kCpN];
        float pvx[kCpN], pvy[kCpN];
        for (auto& [k, e] : K.E)
        {
            if (n >= kCpN) break;
            if (e.st != LIVE) continue;
            UObject* a = e.body.get();
            if (!a) continue;
            uint32_t id = 0;
            uintptr_t p = dd_ent_seh(a, &id);
            if (!p) continue;
            pe[n] = p; pi[n] = id; pu[n] = (LONG)e.h.uid; pg[n] = e.lgait; pa[n] = reinterpret_cast<uintptr_t>(a);
            ps[n] = (LONG)std::lround(std::hypot(e.h.vx, e.h.vy));
            ph[n] = (C.natcb && e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16))) ? 1 : 0;
            pxh[n] = pxm[n] = pcm[n] = 0;
            if (e.kOn)
            {
                uintptr_t u = reinterpret_cast<uintptr_t>(rd_p_seh(reinterpret_cast<uint8_t*>(a) + 0xac8)), u2 = reinterpret_cast<uintptr_t>(rd_p_seh(reinterpret_cast<uint8_t*>(a) + 0xad0));
                pxh[n] = u ? u + 0x620 : 0;
                pxm[n] = u2 ? u2 + 0x450 : 0;
                pcm[n] = reinterpret_cast<uintptr_t>(rd_p_seh(reinterpret_cast<uint8_t*>(a) + 0x338));
            }
            pvx[n] = (float)e.h.vx; pvy[n] = (float)e.h.vy;
            ++n;
        }
        g_cpN = 0;
        MemoryBarrier();
        for (LONG i = 0; i < n; ++i) { g_cpId[i] = pi[i]; g_cpU[i] = pu[i]; g_cpG[i] = pg[i]; g_cpE[i] = pe[i]; g_cpA[i] = pa[i]; g_cpSp[i] = ps[i]; g_cpHc[i] = ph[i]; g_cpXh[i] = pxh[i]; g_cpXm[i] = pxm[i]; g_cpCmc[i] = pcm[i]; g_cpVx[i] = pvx[i]; g_cpVy[i] = pvy[i]; }
        MemoryBarrier();
        g_cpN = n;
        g_cpAt = GetTickCount64();
        if (n > g_cpMaxN) g_cpMaxN = n;
    }
    auto nw_refresh() -> void
    {
        if (!g_nwDet) return;
        LONG n = 0;
        uintptr_t pc[kCpN];
        for (auto& [k, e] : K.E)
        {
            if (n >= kCpN) break;
            if (e.st != LIVE || !e.kOn || e.vend || e.how == 3) continue;
            UObject* a = e.body.get();
            if (!a) continue;
            uintptr_t c = reinterpret_cast<uintptr_t>(rd_p_seh(reinterpret_cast<uint8_t*>(a) + 0x338));
            if (c) pc[n++] = c;
        }
        g_nwN = 0;
        MemoryBarrier();
        for (LONG i = 0; i < n; ++i) g_nwCmc[i] = pc[i];
        MemoryBarrier();
        g_nwN = n;
        g_nwAt = GetTickCount64();
        if (n > g_nwMaxN) g_nwMaxN = n;
    }
    auto tick_cb() -> void
    {
        g_gtid = GetCurrentThreadId();
        try { ag_tick(); } catch (...) {}
        try { cp_refresh(); } catch (...) { g_cpN = 0; }
        try { dmg_tick(); } catch (...) {}
        try { ds_tick(); } catch (...) {}
        try { in_tick(); } catch (...) {}
        try { bullet_frame(); } catch (...) { ++g_bExc; }
        try { kin_frame(); } catch (const std::exception& ex) { ++K.errs; K.lastErr = std::string("natkin: ") + ex.what(); } catch (...) { ++K.errs; K.lastErr = "natkin"; }
        try { nw_refresh(); } catch (...) { g_nwN = 0; }
        if (!C.natpoll) return;
        try { poll_in(); } catch (const std::exception& ex) { ++K.errs; K.lastErr = std::string("natpoll: ") + ex.what(); } catch (...) { ++K.errs; K.lastErr = "natpoll"; }
    }
    auto client_api(int mode, const std::string& nn, const std::string& nl, double meA, double pupA, double tgtA, int busy, int via) -> bool;
    auto lua_client(const LuaMadeSimple::Lua& lua) -> int
    {
        int mode = lua.is_number() ? (int)lua.get_number() : 0;
        std::string nn = lua.is_string() ? std::string(lua.get_string()) : std::string();
        std::string nl = lua.is_string() ? std::string(lua.get_string()) : std::string();
        auto num = [&](double def) { return lua.is_number() ? lua.get_number() : def; };
        double meA = num(0), pupA = num(0), tgtA = num(0);
        int busy = (int)num(0);
        int via = (int)num(0);
        lua.set_bool(client_api(mode, nn, nl, meA, pupA, tgtA, busy, via));
        return 1;
    }
    auto client_api(int mode, const std::string& nn, const std::string& nl, double meA, double pupA, double tgtA, int busy, int via) -> bool
    {
        double now = now_s();
        g_poll.mode = mode; g_poll.meA = meA; g_poll.pupA = pupA; g_poll.tgtA = tgtA; g_poll.busy = busy; g_poll.argAt = now;
        {
            static double wAt = 0;
            if (mode == 1 && now - wAt >= 1.0)
            {
                wAt = now;
                for (auto& [k, e] : K.E) if (e.shown && e.st == LIVE) if (UObject* a = e.body.get()) anim_watch(a, "copia");
            }
        }
        if (mode == 1 && C.natpoll && now - g_poll.lastRun < 0.35) { ++g_pollSkip; return true; }
        return client_run(mode, nn, nl, meA, pupA, tgtA, busy, via);
    }

    auto suffix_of(const std::string& s) -> std::string
    {
        size_t u = s.rfind('_');
        return u == std::string::npos ? s : s.substr(u + 1);
    }
    auto shown_by_nm(const std::string& sx, bool deadOnly) -> Ent*
    {
        if (sx.empty() || sx == "0") return nullptr;
        for (auto& [k, e] : K.E)
            if (e.h.nm == sx && e.shown && (e.st == LIVE || e.st == DEAD) && (!deadOnly || e.st == DEAD)) return &e;
        return nullptr;
    }

#include "s2n_b164.inc"
    auto own_api(double a) -> bool { return a != 0 && K.own.count(static_cast<uintptr_t>(a)) > 0; }
    auto lua_own(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        lua.set_bool(own_api(a));
        return 1;
    }
    auto dead_api(const std::string& s) -> bool
    {
        Ent* e = shown_by_nm(suffix_of(s), true);
        return e != nullptr && !swapped_src(e->body.raw);
    }
    auto lua_dead(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        lua.set_bool(dead_api(s));
        return 1;
    }
    auto actor_api(const std::string& s, UObject*& w, UObject*& m) -> UObject*
    {
        Ent* e = shown_by_nm(suffix_of(s), false);
        UObject* a = e ? e->body.get() : nullptr;
        w = m = nullptr;
        if (!a) return nullptr;
        w = c_obj(a, STR("GetWeaponInHandsMeshComponent"));
        m = prop_obj(a, STR("Mesh"));
        return a;
    }
    auto lua_actor(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        UObject* w = nullptr;
        UObject* m = nullptr;
        UObject* a = actor_api(s, w, m);
        if (!a) { lua.set_nil(); return 1; }
        LuaType::auto_construct_object(lua, a);
        if (w) LuaType::auto_construct_object(lua, w); else lua.set_nil();
        if (m) LuaType::auto_construct_object(lua, m); else lua.set_nil();
        return 3;
    }
    auto listed_uid(int32_t uid) -> bool
    {
        if (!uid) return false;
        double now = now_s();
        auto it = K.E.find(uid);
        if (it != K.E.end() && it->second.st != KEPT && (!cli_lost(it->second.h, now) || it->second.st == LIVE || it->second.st == DEAD)) return true;
        for (auto& [k, s] : H.sel) if (s.uid == uid) return true;
        return false;
    }
    auto md_find_api(int32_t uid) -> UObject*
    {
        if (!uid) return nullptr;
        for (auto& [k, s] : H.sel)
            if (s.uid == uid)
            {
                UObject* o = s.r.get();
                if (o && guid_of(o) == uid) return o;
            }
        std::vector<UObject*> all;
        cls_all(all);
        for (UObject* o : all)
            if (o && guid_of(o) == uid) return o;
        return nullptr;
    }
    auto lua_listed(const LuaMadeSimple::Lua& lua) -> int
    {
        double u = lua.is_number() ? lua.get_number() : 0;
        lua.set_bool(listed_uid((int32_t)u));
        return 1;
    }
    auto listed_addr_api(double a) -> bool
    {
        auto p = static_cast<uintptr_t>(a);
        bool r = false;
        if (p)
        {
            if (K.own.count(p)) r = true;
            else
            {
                auto it = K.idx.find(reinterpret_cast<UObject*>(p));
                if (it != K.idx.end()) r = listed_uid(it->second.uid);
                if (!r && C.natqg && it != K.idx.end() && !it->second.qg.empty())
                {
                    auto q = K.rowQg.find(it->second.qg);
                    if (q != K.rowQg.end() && listed_uid(q->second)) { r = true; ++K.qgListed; }
                }
                if (!r) for (auto& [k, s] : H.sel) if (reinterpret_cast<uintptr_t>(k) == p && s.r.get()) { r = true; break; }
            }
        }
        return r;
    }
    auto lua_listed_addr(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        lua.set_bool(listed_addr_api(a));
        return 1;
    }
    auto corpse_api(const std::string& s, double& x, double& y, double& z) -> bool
    {
        Ent* e = shown_by_nm(suffix_of(s), true);
        UObject* a = e ? e->body.get() : nullptr;
        if (!a || !loc_of(a, x, y, z)) return false;
        if (UObject* m = prop_obj(a, STR("Mesh")))
        {
            Call c(m, STR("GetSocketLocation"));
            c.nm(STR("InSocketName"), STR("jnt_hips"));
            double px, py, pz;
            if (c.run() && c.rv(px, py, pz) && !(px == 0 && py == 0) && std::hypot(px - x, py - y) < 400) { x = px; y = py; }
        }
        return true;
    }
    auto lua_corpse(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        double x, y, z;
        if (!corpse_api(s, x, y, z)) { lua.set_nil(); return 1; }
        lua.set_number(x); lua.set_number(y); lua.set_number(z);
        return 3;
    }
    auto filt_drop() -> std::unordered_set<std::string>
    {
        std::unordered_set<std::string> drop;
        for (auto& [k, e] : K.E) if (e.shown && (e.st == LIVE || e.st == DEAD) && e.h.nm != "0") drop.insert(e.h.nm);
        return drop;
    }
    auto filt_api(const std::string& t, int& n) -> std::string
    {
        std::unordered_set<std::string> drop = filt_drop();
        n = 0;
        size_t pos = t.find("\"npcs\"");
        if (drop.empty() || pos == std::string::npos) return t;
        std::string out = t.substr(0, pos);
        size_t i = pos;
        while (i < t.size())
        {
            size_t o = t.find("{\"id\":", i);
            if (o == std::string::npos) { out += t.substr(i); break; }
            size_t c = t.find('}', o);
            size_t inner = t.find('{', o + 1);
            if (c == std::string::npos || (inner != std::string::npos && inner < c)) { out += t.substr(i); break; }
            size_t q1 = t.find('"', o + 6);
            size_t q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
            bool dropIt = false;
            if (q2 != std::string::npos && q2 < c) dropIt = drop.count(suffix_of(t.substr(q1 + 1, q2 - q1 - 1))) > 0;
            out += t.substr(i, o - i);
            if (dropIt) ++n; else out += t.substr(o, c - o + 1);
            i = c + 1;
        }
        return out;
    }
    auto lua_filt(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string t = lua.is_string() ? std::string(lua.get_string()) : std::string();
        int n = 0;
        std::string out = filt_api(t, n);
        lua.set_string(out);
        lua.set_integer(n);
        return 2;
    }
    auto lua_status(const LuaMadeSimple::Lua& lua) -> int
    {
        int live = 0, dead = 0, kept = 0, pend = 0, shown = 0;
        for (auto& [k, e] : K.E) { live += e.st == LIVE; dead += e.st == DEAD; kept += e.st == KEPT; pend += e.st == PEND; shown += e.shown; }
        char b[420];
        snprintf(b, sizeof b, "cliente: host=%d vivas=%d mortas=%d mantidas=%d pendente=%d mostradas=%d proprios=%d covas=%d pawn_real=%d | host: lista=%d | autoteste=%d sem_funcao=%d falhas_pe=%d ultimo_erro=%s",
                 (int)K.E.size(), live, dead, kept, pend, shown, (int)K.own.size(), (int)K.graves.size(), K.me.real ? 1 : 0, (int)H.order.size(), g_st, g_noFn, g_peFail, K.lastErr.c_str());
        lua.set_string(b);
        return 1;
    }
    auto lua_dump(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s;
        double now = now_s();
        for (auto& [k, e] : K.E)
        {
            UObject* a = e.body.get();
            double x = 0, y = 0, z = 0;
            if (a) loc_of(a, x, y, z);
            std::string cl, hl;
            std::vector<Part> hp, cp;
            parse_look(e.look, hp);
            if (a) parts_of(a, cp, true);
            int mm = -1;
            std::string why;
            if (a && !e.look.empty()) mm = look_diff(hp, cp, &why);
            auto xt = [](const Part& p) { return p.ext && (!p.hid.empty() || !p.cpd.empty()) ? "{sec=" + s2nlook::hid_txt(p.hid) + " cpd=" + s2nlook::cpd_txt(p.cpd) + "}" : std::string(); };
            for (auto& p : hp) hl += (hl.empty() ? "" : ",") + p.key + "=" + p.mesh.substr(p.mesh.rfind('/') == std::string::npos ? 0 : p.mesh.rfind('/') + 1) + xt(p);
            for (auto& p : cp) cl += (cl.empty() ? "" : ",") + p.key + "=" + p.mesh.substr(p.mesh.rfind('/') == std::string::npos ? 0 : p.mesh.rfind('/') + 1) + (p.vis ? "" : "(oculto)") + xt(p);
            char b[560];
            snprintf(b, sizeof b, "uid=%d nm=%s st=%d how=%d luid=%d mostrada=%d fl=%u hp=%d host=%.0f,%.0f,%.0f idade=%.1f corpo=%s pos=%.0f,%.0f,%.0f err=%.0f look=%08x aplicado=%08x reaplicado=%d DIFERENCAS=%d"
                     " | b162 erro_ext=%.0f idade_real=%.3f ord_idade=%.2f ordens=%d gait=%d vel_host=%.0f vel_copia=%.0f atraso=%d vendedor=%d fixa=%d corpo_guardado=%d",
                     e.h.uid, e.h.nm.c_str(), e.st, e.how, e.luid, e.shown ? 1 : 0, e.h.fl, e.h.hp, e.h.x, e.h.y, e.h.zg, now - e.h.recv,
                     a ? w2u(a->GetName()).c_str() : "-", x, y, z, a ? std::hypot(x - e.h.x, y - e.h.y) : -1.0, e.lookH, e.lookDone, e.reapply, mm,
                     a ? std::hypot(x - (e.h.x + e.h.vx * std::clamp(now - e.h.recv, 0.0, C.natagemax)), y - (e.h.y + e.h.vy * std::clamp(now - e.h.recv, 0.0, C.natagemax))) : -1.0,
                     now - e.h.recv, e.ordAt < 0 ? -1.0 : now - e.ordAt, e.ordN, e.ogait, std::hypot(e.h.vx, e.h.vy), e.csp, e.catchUp ? 1 : 0,
                     (e.h.fl & 512) ? 1 : 0, (e.h.fl & 256) ? 1 : 0, e.lostDead ? 1 : 0);
            s += b;
            s += " | host_meshes=[" + hl + "] cliente_meshes=[" + cl + "]" + (why.empty() ? "" : " porque:" + why) + "\n";
        }
        lua.set_string(s);
        return 1;
    }
    auto lua_dump169(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s;
        double now = now_s();
        UObject* me = K.me.real ? K.me.r.get() : nullptr;
        UObject* ia = me ? c_obj(me, STR("GetInteractedActor")) : nullptr;
        UObject* pc = me ? c_obj(me, STR("GetController")) : nullptr;
        int xr = pc ? c_u8(pc, STR("GetLastCrosshairRelation"), -1) : -1;
        if (xr < 0 && me) xr = c_u8(me, STR("GetLastCrosshairRelation"), -1);
        static WRef scls;
        UObject* sc = scls.get();
        if (!sc) { sc = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, STR("/Script/Stalker2.ShootingWeaponComponent")); scls.set(sc); }
        for (auto& [k, e] : K.E)
        {
            UObject* a = (e.st == LIVE || e.st == DEAD) ? e.body.get() : nullptr;
            int bs = a ? brain_st(a) : -9, ait = -1, trig = -1, ceq = -1, hid = -1;
            AnimSt as;
            if (a)
            {
                if (UObject* t = prop_obj(a, STR("AITickComponent"))) ait = c_bool(t, STR("IsComponentTickEnabled"), false) ? 1 : 0;
                if (sc) { Call c(a, STR("GetComponentByClass")); c.ob(STR("ComponentClass"), sc); if (c.run()) if (UObject* comp = c.ro()) trig = c_bool(comp, STR("IsActive"), false) ? 1 : 0; }
                ceq = c_u8(a, STR("GetMainHandEquipType"), -1);
                hid = prop_bool(a, STR("bHidden"), false) ? 1 : 0;
                anim_state(anim_of(prop_obj(a, STR("Mesh"))), as);
            }
            const char* hw = e.how == 1 ? "adotada_oculta" : e.how == 2 ? "criada" : e.how == 3 ? "SUAVE" : e.how == 4 ? "TOTAL_visivel" : "-";
            char b[520];
            snprintf(b, sizeof b, "uid=%d nm=%s st=%d how=%d(%s) vend=%d mostrada=%d bHidden=%d corpo=%s | cerebro=%s(%d) aitick=%d | rel0_com_eu=%d rel0_boneco=%d imortal_set=%d"
                     " gatilho_ativo=%d | host_combate=%d copia_inCombat=%d arma_na_mao=%d olhar=%d/host %d | eq copia=%d host=%d %s | conversa=%d idade=%.1f ordens=%d mws0=%.0f\n",
                     e.h.uid, e.h.nm.c_str(), e.st, e.how, hw, e.vend ? 1 : 0, e.shown ? 1 : 0, hid, a ? w2u(a->GetName()).c_str() : "-",
                     bs == -9 ? "-" : bs < 0 ? "SEM" : (bs & 1) ? ((bs & 2) ? "PAUSADO" : "LIGADO") : "PARADO", bs,
                     ait, (me && e.relMe == me) ? 1 : 0, e.relPup ? 1 : 0, e.uk0 ? 1 : 0, trig,
                     (e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16))) ? 1 : 0, as.inCombat, as.hasW, as.ls, e.h.ls, ceq, e.h.eq, (ceq == e.h.eq) ? "IGUAL" : "DIF",
                     (a && ia == a) ? 1 : 0, now - e.h.recv, e.ordN, e.mws0);
            s += b;
        }
        char t[200];
        snprintf(t, sizeof t, "natkept=%d natvend=%d natcb=%d mira_relacao(ERelationLevel 0 inimigo 2 neutro 3 amigo)=%d interagindo_com=%s\n",
                 C.natkept, C.natvend, C.natcb, xr, ia ? w2u(ia->GetName()).c_str() : "-");
        lua.set_string(std::string(t) + s);
        return 1;
    }
    auto lua_dump170(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s;
        double now = now_s();
        char t[300];
        snprintf(t, sizeof t, "natthr=%d natmkmax=%d natmerr=%.0f natsnap=%.0f natsnapt=%.1f natnudge=%.0f natnudges=%.0f natnudgek=%.2f natspdw=%.0f natspdr=%.0f natfacer=%.0f\n",
                 C.natthr, C.natmkmax, C.natmerr, C.natsnap, C.natsnapt, C.natnudge, C.natnudges, C.natnudgek, C.natspdw, C.natspdr, C.natfacer);
        s += t;
        for (auto& [k, e] : K.E)
        {
            UObject* a = (e.st == LIVE || e.st == DEAD) ? e.body.get() : nullptr;
            if (!a) continue;
            std::vector<Part> hp, cp;
            parse_look(e.look, hp);
            parts_of(a, cp, true);
            std::string why;
            int mm = e.look.empty() ? -1 : look_diff(hp, cp, &why);
            int made = 0, hidden = 0;
            for (auto& p : cp) { made += p.made; hidden += !p.vis; }
            UObject* ai = anim_of(prop_obj(a, STR("Mesh")));
            AnimSt as;
            anim_state(ai, as);
            int relax = -1, clook = -1;
            if (ai)
            {
                auto* ab = reinterpret_cast<const uint8_t*>(ai);
                int sd = off_of(ai, STR("StateData")), la = off_of(ai, STR("LookAtData"));
                if (sd >= 0) relax = rd_u8_seh(ab + sd + 0x36);
                if (la >= 0) clook = rd_u8_seh(ab + la + 0x13C);
            }
            UObject* cur = mont_cur(ai);
            int ait = -1;
            if (UObject* tk = prop_obj(a, STR("AITickComponent"))) ait = c_bool(tk, STR("IsComponentTickEnabled"), false) ? 1 : 0;
            char b[900];
            snprintf(b, sizeof b, "uid=%d how=%d st=%d | look dif=%d partes=%d novas=%d ocultas=%d reaplicado=%d%s%s | host_combate=%d ameaca=%d inCombat=%d relax=%d olhar_combate=%d olhar=%d/host %d aitick=%d"
                     " | err=%.0f snap_pendente=%.1f s montagem=%s host_montagem_corpo=%d\n",
                     e.h.uid, e.how, e.st, mm, (int)cp.size(), made, hidden, e.reapply, why.empty() ? "" : " porque:", why.c_str(),
                     (e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16))) ? 1 : 0, c_bool(a, STR("IsInThreat"), false) ? 1 : 0, as.inCombat, relax, clook, as.ls, e.h.ls, ait,
                     e.err, e.snapSince >= 0 ? now - e.snapSince : -1.0, cur ? w2u(cur->GetName()).c_str() : "-", e.h.bs);
            s += b;
        }
        lua.set_string(s);
        return 1;
    }
    auto lua_dump171(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s;
        double now = now_s();
        char t[200];
        snprintf(t, sizeof t, "natforce=%d natthr=%d natcb=%d\n", C.natforce, C.natthr, C.natcb);
        s += t;
        for (auto& [k, e] : K.E)
        {
            UObject* a = (e.st == LIVE || e.st == DEAD) ? e.body.get() : nullptr;
            if (!a) continue;
            int inC = -1, rx = -1, s2r = -1, ir = -1, cl = -1, ls = -1, thc = -1, thv = -1, atk = -1, eq = -1, hasW = -1;
            if (UObject* ai = anim_of(prop_obj(a, STR("Mesh"))))
            {
                auto* b = reinterpret_cast<const uint8_t*>(ai);
                int sd = off_of(ai, STR("StateData")), la = off_of(ai, STR("LookAtData")), wd = off_of(ai, STR("WeaponData"));
                if (sd >= 0) { inC = rd_u8_seh(b + sd + 0x09); rx = rd_u8_seh(b + sd + 0x36); s2r = rd_u8_seh(b + sd + 0x37); ir = rd_u8_seh(b + sd + 0x3B); }
                if (la >= 0) { cl = rd_u8_seh(b + la + 0x13C); ls = rd_u8_seh(b + la + 0x132); }
                if (wd >= 0) hasW = rd_u8_seh(b + wd);
            }
            if (const uint8_t* S = ai_s_of(a)) { thc = rd_u8_seh(S + 0x1ad); thv = rd_u8_seh(S + 0x1ac); }
            if (UObject* c = prop_obj(a, STR("AttackComponent"))) atk = rd_u8_seh(reinterpret_cast<const uint8_t*>(c) + 0xA8);
            eq = c_u8(a, STR("GetMainHandEquipType"), -1);
            int hc = (e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16))) ? 1 : 0;
            char b[600];
            snprintf(b, sizeof b, "uid=%d how=%d st=%d | host: combate=%d inCombat=%d olhar_combate=%d relax=%d bits171=%d ls=%d eq=%d | copia: inCombat=%d relax=%d s2r=%d initRelax=%d olhar_combate=%d ls=%d arma_na_mao=%d eq=%d"
                     " | ameaca=%d cache=%d/%d forcada=%d | ataque=%d | ultima_escrita=%.2f s\n",
                     e.h.uid, e.how, e.st, hc, e.h.cb & 1, (e.h.cb >> 16) & 1, (e.h.cb >> 17) & 1, (e.h.cb >> 18) & 1, e.h.ls, e.h.eq,
                     inC, rx, s2r, ir, cl, ls, hasW, eq, c_bool(a, STR("IsInThreat"), false) ? 1 : 0, thc, thv, e.frcThr ? 1 : 0, atk,
                     e.frcAt >= 0 ? now - e.frcAt : -1.0);
            s += b;
        }
        lua.set_string(s);
        return 1;
    }
    auto lua_dump172(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s;
        double now = now_s();
        char t[600];
        snprintf(t, sizeof t, "natgad=%d natgadlo=%.2f natgadhi=%.2f natgadt=%.2f natfacemv=%d natmgate=%d natwpnlook=%d natlookpt=%d natmloadpt=%d natlookld=%d natmroot=%d natidxs=%.1f natspdw=%.0f natspdr=%.0f natnudgek=%.2f presos=%d\n",
                 C.natgad, C.natgadlo, C.natgadhi, C.natgadt, C.natfacemv, C.natmgate, C.natwpnlook, C.natlookpt, C.natmloadpt, C.natlookld, C.natmroot, C.natidxs,
                 C.natspdw, C.natspdr, C.natnudgek, g_assetRooted);
        s += t;
        for (auto& [k, e] : K.E)
        {
            UObject* a = e.st == LIVE ? e.body.get() : nullptr;
            if (!a) continue;
            float mws = -1;
            if (UObject* cmc = obj_at(a, STR("CharacterMovement"))) { int o = off_of(cmc, STR("MaxWalkSpeed")); if (o >= 0) mws = rd_f_seh(reinterpret_cast<uint8_t*>(cmc) + o); }
            char b[400];
            snprintf(b, sizeof b, "uid=%d how=%d host_v=%.0f copia_v=%.0f razao_suave=%.2f gait=%d extra=%d err=%.0f maxwalk=%.0f (escrito %.0f) combate=%d ordem_ha=%.2f s\n",
                     e.h.uid, e.how, std::hypot(e.h.vx, e.h.vy), e.csp, e.rs, e.ogait, e.gb, e.err, mws, e.mwsW,
                     (e.h.ext && ((e.h.cb & 1) || (e.h.fl & 16))) ? 1 : 0, e.ordAt >= 0 ? now - e.ordAt : -1.0);
            s += b;
        }
        lua.set_string(s);
        return 1;
    }
    auto lua_look(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        auto p = static_cast<uintptr_t>(a);
        if (!p || !UObject::IsReal(reinterpret_cast<void*>(p))) { lua.set_string(""); return 1; }
        std::vector<Part> ps;
        parts_of(reinterpret_cast<UObject*>(p), ps);
        lua.set_string(look_str(ps));
        return 1;
    }
    auto lua_cfg(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string k = lua.is_string() ? std::string(lua.get_string()) : std::string();
        bool isNum = lua.is_number();
        double v = isNum ? lua.get_number() : 0;
        std::string sv = (!isNum && lua.is_string()) ? std::string(lua.get_string()) : std::string();
        struct D { const char* n; double* p; };
        D ds[] = {{"natr", &C.natr}, {"natdead", &C.natdead}, {"natlookp", &C.natlookp}, {"natlost", &C.natlost}, {"natlead", &C.natlead},
                  {"natre", &C.natre}, {"natrate", &C.natrate}, {"natsnap", &C.natsnap}, {"natshow", &C.natshow}, {"natrun", &C.natrun}, {"natstill", &C.natstill},
                  {"natkeep", &C.natkeep}, {"natok", &C.natok}, {"natcatch", &C.natcatch}, {"natsprint", &C.natsprint}, {"natsucc", &C.natsucc},
                  {"natang", &C.natang}, {"natcatch2", &C.natcatch2}, {"natnudge", &C.natnudge}, {"natagemax", &C.natagemax}, {"natvendr", &C.natvendr},
                  {"natmwin", &C.natmwin}, {"natfaceang", &C.natfaceang}, {"natkeepc", &C.natkeepc}, {"natnudges", &C.natnudges}, {"natspdmax", &C.natspdmax},
                  {"natmerr", &C.natmerr}, {"natsnapt", &C.natsnapt}, {"natnudgek", &C.natnudgek}, {"natspdw", &C.natspdw}, {"natspdr", &C.natspdr},
                  {"natfacer", &C.natfacer}, {"natlookbk", &C.natlookbk},
                  {"natgadlo", &C.natgadlo}, {"natgadhi", &C.natgadhi}, {"natgadt", &C.natgadt}, {"natidxs", &C.natidxs},
                  {"natasyncto", &C.natasyncto}, {"natagek", &C.natagek},
                  {"natkind", &C.natkind}, {"natkinlead", &C.natkinlead}, {"natkinext", &C.natkinext}, {"natking", &C.natking}, {"natkinsnap", &C.natkinsnap}, {"natkinmws", &C.natkinmws}, {"natbwin", &C.natbwin}, {"natbdel", &C.natbdel},
                  {"natdwin", &C.natdwin}, {"natdmax", &C.natdmax}, {"natdcap", &C.natdcap}, {"natdspawn", &C.natdspawn}, {"nathys", &C.nathys}, {"natparkage", &C.natparkage}, {"natparkr", &C.natparkr}, {"natkeptr", &C.natkeptr}, {"natfardead", &C.natfardead}, {"nattrajh", &C.nattrajh}, {"nattrajmin", &C.nattrajmin}, {"nattrajvmax", &C.nattrajvmax}};
        struct I { const char* n; int* p; };
        I is[] = {{"natmax", &C.natmax}, {"natlookn", &C.natlookn}, {"natlookmax", &C.natlookmax}, {"natspawn", &C.natspawn}, {"natadopt", &C.natadopt},
                  {"natlook", &C.natlook}, {"natlog", &C.natlog}, {"natgaitmax", &C.natgaitmax}, {"natgait", &C.natgait}, {"natmab", &C.natmab},
                  {"natcorpsemax", &C.natcorpsemax}, {"natvend", &C.natvend}, {"natkept", &C.natkept}, {"natfixed", &C.natfixed},
                  {"natcb", &C.natcb}, {"natmstop", &C.natmstop}, {"natwound", &C.natwound}, {"natwoundfix", &C.natwoundfix}, {"natdeathfx", &C.natdeathfx}, {"natface", &C.natface}, {"natrot", &C.natrot}, {"natspd", &C.natspd},
                  {"natthr", &C.natthr}, {"natmkmax", &C.natmkmax}, {"natforce", &C.natforce},
                  {"natgad", &C.natgad}, {"natfacemv", &C.natfacemv}, {"natmgate", &C.natmgate}, {"natwpnlook", &C.natwpnlook},
                  {"natlookpt", &C.natlookpt}, {"natmloadpt", &C.natmloadpt}, {"natmroot", &C.natmroot}, {"natlookld", &C.natlookld},
                  {"natasync", &C.natasync}, {"natasyncpt", &C.natasyncpt}, {"natbr", &C.natbr},
                  {"natfind", &C.natfind}, {"natpoll", &C.natpoll},
                  {"natkin", &C.natkin}, {"natkinreq", &C.natkinreq}, {"natrmoff", &C.natrmoff}, {"natnavw", &C.natnavw},{"natwpnhid", &C.natwpnhid},
                  {"natkinord", &C.natkinord}, {"natkinstop", &C.natkinstop}, {"natkinpin", &C.natkinpin}, {"natwpnguard", &C.natwpnguard},
                  {"montnull", &C.montnull}, {"natnoatk", &C.natnoatk}, {"natstance", &C.natstance}, {"natthrsrc", &C.natthrsrc}, {"nathol", &C.nathol}, {"natstsrc", &C.natstsrc}, {"natcover", &C.natcover}, {"natmsec", &C.natmsec}, {"natmre", &C.natmre}, {"natgenmre", &C.natgenmre}, {"natmloop", &C.natmloop}, {"natmsecq", &C.natmsecq}, {"natmkeep", &C.natmkeep}, {"natmovl", &C.natmovl}, {"natlooksec", &C.natlooksec}, {"natmutlook", &C.natmutlook}, {"natgenmk", &C.natgenmk}, {"natgenmkmax", &C.natgenmkmax}, {"natgen", &C.natgen}, {"natlookst", &C.natlookst}, {"natpark", &C.natpark},
                  {"invnull", &C.invnull}, {"natbullet", &C.natbullet}, {"aggrop", &C.aggrop},
                  {"natmut", &C.natmut}, {"natrhost", &C.natrhost}, {"natadopt_kept", &C.natadopt_kept}, {"npcvis", &C.npcvis}, {"natqg", &C.natqg}, {"natfd", &C.natfd}, {"offanim", &C.offanim}, {"offvb", &C.offvb},
                  {"natlegs", &C.natlegs}, {"natmbh", &C.natmbh}, {"natqgsafe", &C.natqgsafe}, {"natlostfix", &C.natlostfix}, {"natlostmax", &C.natlostmax}, {"nateqfix", &C.nateqfix}, {"natfar", &C.natfar}, {"natdeadall", &C.natdeadall}, {"natdeadpos", &C.natdeadpos},
                  {"natkinmv", &C.natkinmv}, {"nateqsync", &C.nateqsync}, {"natturn", &C.natturn}, {"natgaitcp", &C.natgaitcp}, {"natwpnlocal", &C.natwpnlocal}, {"natloco", &C.natloco}, {"natlocoprm", &C.natlocoprm}, {"natlocofl", &C.natlocofl}, {"nattraj", &C.nattraj}, {"nattrajv", &C.nattrajv}, {"natlocosf", &C.natlocosf}, {"nattrajown", &C.nattrajown}, {"natcpvel", &C.natcpvel}, {"nattrajn", &C.nattrajn}, {"natprops", &C.natprops},
                  {"natwpnsid", &g_wsOn}, {"natwpnarm", &g_wsArmOn}, {"natcpdmg", &g_cpOn}, {"natpeframe", &s2n_pf::g_on}, {"natpefix", &s2n_pf::g_fix}, {"natpeseen", &s2n_pf::g_seen}};
        bool ok = false;
        for (auto& d : ds) if (k == d.n) { if (isNum) *d.p = v; v = *d.p; ok = true; }
        for (auto& d : is) if (k == d.n) { if (isNum) *d.p = (int)v; v = *d.p; ok = true; }
        if (k == "natsid") { if (!sv.empty()) C.natsid = u2w(sv); ok = true; }
        if (k == "natcopysid") { C.natcopysid = u2w(sv); ok = true; }
        if (k == "natcls") { if (!sv.empty()) C.natcls = u2w(sv); ok = true; }
        if (k == "natpelast") { lua.set_bool(true); lua.set_string(pe_last()); return 2; }
        lua.set_bool(ok);
        if (k == "natsid") lua.set_string(w2u(C.natsid));
        else if (k == "natcopysid") lua.set_string(w2u(C.natcopysid));
        else if (k == "natcls") lua.set_string(w2u(C.natcls));
        else lua.set_number(v);
        return 2;
    }
    auto lua_ping(const LuaMadeSimple::Lua& lua) -> int { lua.set_string("s2n_nat b206+b191m+b191g+b195+b196+b200"); return 1; }

    template <int (*F)(const LuaMadeSimple::Lua&)>
    auto guarded(const LuaMadeSimple::Lua& lua) -> int
    {
        try { return F(lua); }
        catch (const std::exception& ex) { ++K.errs; K.lastErr = std::string("excecao: ") + ex.what(); lua.set_nil(); return 1; }
        catch (...) { ++K.errs; K.lastErr = "excecao"; lua.set_nil(); return 1; }
    }
}

namespace s2nat
{
    std::string host(int mode, uint64_t me, double rx, double ry, double rz, uint64_t pup, uint64_t tgt)
    {
        return host_api(mode, (double)me, rx, ry, rz, (double)pup, (double)tgt);
    }
    void host_views(uint64_t me, const std::vector<Player>& pl) { host_mc_api((double)me, pl); }
    std::string host_view(int view) { return view >= 0 && view < s2nmc::kViews ? H.outV[view] : std::string(); }
    void bodies(int mypid, const std::vector<Body>& others)
    {
        K.myPid = mypid;
        K.plA.clear();
        for (const Body& b : others) if (b.a) K.plA.push_back({b.pid, b.a});
    }
    void cfg_mc(int on) { C.natmc = on == 1 ? 1 : 0; }
    int mc_on() { return C.natmc; }
    bool client(int mode, const std::string& nn, const std::string& nl, uint64_t me, uint64_t pup, uint64_t tgt, int busy, int via)
    {
        return client_api(mode, nn, nl, (double)me, (double)pup, (double)tgt, busy, via);
    }
    bool own(uint64_t a) { return own_api((double)a); }
#ifndef S2X_RELEASE
    bool census_hk(int32_t uid) { return host_knows(uid); }
    void census_nat(std::vector<CenNat>& out, int& hu, int& hk_live)
    {
        double t = now_s();
        hu = (int)K.hu.size();
        hk_live = host_link();
        for (auto& [uid, e] : K.E)
        {
            CenNat c;
            c.uid = uid;
            c.kind = 1;
            c.st = e.st;
            c.shown = e.shown;
            c.luid = e.luid;
            c.hx = e.h.x;
            c.hy = e.h.y;
            c.hz = e.h.zg;
            c.hp = s2nmc::hp_pct(e.h.hp, e.h.hpS);
            c.age = t - e.h.recv;
            c.sid = e.h.sid;
            if (UObject* o = e.body.get()) c.a = reinterpret_cast<uint64_t>(o);
            out.push_back(c);
        }
        for (auto& [a, tw] : K.twin)
        {
            CenNat c;
            c.a = a;
            c.uid = tw.uid;
            c.kind = 2;
            c.age = t - tw.at;
            out.push_back(c);
        }
        for (auto& [a, ie] : g_in)
        {
            CenNat c;
            c.a = a;
            c.uid = ie.uid;
            c.kind = 3;
            c.age = t - ie.at;
            out.push_back(c);
        }
        for (uintptr_t a : K.own)
        {
            CenNat c;
            c.a = a;
            c.kind = 4;
            out.push_back(c);
        }
        for (auto& [o, s] : H.sel)
        {
            CenNat c;
            c.a = reinterpret_cast<uint64_t>(o);
            c.uid = s.uid;
            c.kind = 5;
            c.sid = s.sid;
            out.push_back(c);
        }
        for (UObject* o : K.vend)
        {
            CenNat c;
            c.a = reinterpret_cast<uint64_t>(o);
            c.kind = 6;
            out.push_back(c);
        }
    }
#endif
    bool dead(const std::string& hid) { return dead_api(hid); }
    uint64_t actor(const std::string& nm, uint64_t& weapon, uint64_t& mesh)
    {
        UObject* w = nullptr;
        UObject* m = nullptr;
        UObject* a = actor_api(nm, w, m);
        weapon = reinterpret_cast<uint64_t>(w);
        mesh = reinterpret_cast<uint64_t>(m);
        return reinterpret_cast<uint64_t>(a);
    }
    bool listed(int32_t uid) { return listed_uid(uid); }
    bool listed_addr(uint64_t a) { return listed_addr_api((double)a); }
    bool corpse(const std::string& hid, double& x, double& y, double& z) { return corpse_api(hid, x, y, z); }
    int natbr() { return C.natbr; }
    bool park_twin(uint64_t a, int32_t uid) { return twin_park(static_cast<uintptr_t>(a), uid); }
    void natcb(int on) { C.natcb = on; }
    void cfg_a1(int lookst, int park, double hys, double parkage) { C.natlookst = lookst; C.natpark = park; C.nathys = hys; C.natparkage = parkage; }
    void ff_bodies(int on, const std::vector<FfBody>& b)
    {
        uintptr_t pe[kFfN] = {};
        uint32_t pi[kFfN] = {};
        LONG pv[kFfN] = {}, pp[kFfN] = {}, pk[kFfN] = {};
        LONG n = 0;
        if (g_ddDet)
            for (const FfBody& x : b)
            {
                if (!x.a || n >= kFfN) continue;
                uint32_t id = 0xFFFFFFFFu;
                uintptr_t e = dd_ent_seh(reinterpret_cast<UObject*>(x.a), &id);
                if (!e) continue;
                pe[n] = e; pi[n] = id; pv[n] = x.view; pp[n] = x.pid; pk[n] = x.puppet ? 1 : 0; ++n;
            }
        g_ffN = 0;
        MemoryBarrier();
        for (LONG i = 0; i < n; ++i) { g_ffId[i] = pi[i]; g_ffV[i] = pv[i]; g_ffP[i] = pp[i]; g_ffK[i] = pk[i]; g_ffE[i] = pe[i]; }
        MemoryBarrier();
        g_ffN = n;
        g_ffAt = GetTickCount64();
        g_ffLive = on == 1 ? 1 : 0;
        g_ffOn = n > 0 ? 1 : 0;
    }
    std::vector<FfHit> ff_take()
    {
        std::vector<FRaw> q;
        AcquireSRWLockExclusive(&g_ffLock);
        q.swap(g_ffQ);
        ReleaseSRWLockExclusive(&g_ffLock);
        std::vector<FfHit> o;
        for (const FRaw& h : q)
        {
            FfHit x;
            x.view = h.view; x.pid = h.pid; x.puppet = h.puppet;
            for (int k = 0; k < 5; ++k) x.f[k] = h.r.f[k];
            x.ty = h.r.ty; x.src = h.r.src; x.b7a = h.r.b7a; x.b7b = h.r.b7b; x.bn = h.r.bn;
            for (int k = 0; k < 6; ++k) x.v[k] = h.r.v[k];
            x.bone = h.r.bi ? w2u(FName(h.r.bi, 0u).ToString()) : std::string("None");
            dd_bone_ok(x.bone);
            o.push_back(std::move(x));
        }
        return o;
    }
    int ff_apply(uint64_t actor, const FfHit& h)
    {
        if (!actor || !g_ddBuild || !g_ddDet) return 0;
        DRaw a{};
        a.atk = kNoAtk;
        for (int k = 0; k < 5; ++k) a.f[k] = h.f[k];
        a.ty = h.ty; a.src = h.src; a.b7a = h.b7a; a.b7b = h.b7b; a.bn = h.bn;
        for (int k = 0; k < 6; ++k) a.v[k] = h.v[k];
        std::string bone = h.bone;
        dd_bone_ok(bone);
        FName fn(u2w(bone).c_str(), FNAME_Add);
        a.bi = bone == "None" ? 0u : fn.GetComparisonIndex().ToUnstableInt();
        int r = dd_apply_seh(reinterpret_cast<uint8_t*>(actor), &a);
        if (r > 0) InterlockedIncrement(&g_ffApOk);
        if (r < 0) InterlockedIncrement(&g_ffApExc);
        return r;
    }
    std::string ff_status(bool& any)
    {
        any = g_ffSeen || g_ffKept || g_ffOther || g_ffBleed || g_ffRead || g_ffFull || g_ffApOk || g_ffApExc || g_ffOff;
        char b[360];
        snprintf(b, sizeof b, "hook=%s corpos=%ld golpes_nos_corpos=%ld meus=%ld outros=%ld sangramento=%ld falha_leitura=%ld fila_cheia=%ld aplicados=%ld excecao=%ld engolidos_ff0=%ld",
                 g_ddState, (long)g_ffN, (long)g_ffSeen, (long)g_ffKept, (long)g_ffOther, (long)g_ffBleed, (long)g_ffRead, (long)g_ffFull, (long)g_ffApOk, (long)g_ffApExc, (long)g_ffOff);
        g_ffSeen = g_ffKept = g_ffOther = g_ffBleed = g_ffRead = g_ffFull = g_ffApOk = g_ffApExc = g_ffOff = 0;
        return b;
    }
    void md_cfg(int on) { g_mdOn = on == 1 && g_ddDet ? 1 : 0; }
    std::vector<MdHit> md_take()
    {
        std::vector<MRaw> q;
        AcquireSRWLockExclusive(&g_mdLock);
        q.swap(g_mdQ);
        ReleaseSRWLockExclusive(&g_mdLock);
        std::vector<MdHit> o;
        for (const MRaw& h : q)
        {
            MdHit x;
            x.uid = h.uid;
            for (int k = 0; k < 5; ++k) x.f[k] = h.r.f[k];
            x.ty = h.r.ty; x.src = h.r.src; x.b7a = h.r.b7a; x.b7b = h.r.b7b; x.bn = h.r.bn;
            for (int k = 0; k < 6; ++k) x.v[k] = h.r.v[k];
            x.bone = h.r.bi ? w2u(FName(h.r.bi, 0u).ToString()) : std::string("None");
            dd_bone_ok(x.bone);
            o.push_back(std::move(x));
        }
        return o;
    }
    uint64_t md_find(int32_t uid) { return reinterpret_cast<uint64_t>(md_find_api(uid)); }
    int md_apply(uint64_t actor, const MdHit& h, int64_t atk)
    {
        if (!actor || !g_ddBuild || !g_ddDet) return 0;
        DRaw a{};
        for (int k = 0; k < 5; ++k) a.f[k] = h.f[k];
        a.ty = h.ty; a.src = h.src; a.b7a = h.b7a; a.b7b = h.b7b; a.bn = h.bn;
        for (int k = 0; k < 6; ++k) a.v[k] = h.v[k];
        std::string bone = h.bone;
        dd_bone_ok(bone);
        FName fn(u2w(bone).c_str(), FNAME_Add);
        a.bi = bone == "None" ? 0u : fn.GetComparisonIndex().ToUnstableInt();
        uint32_t who = atk < 0 || atk >= 0x8000000 ? pl_id() : (uint32_t)atk;
        int r = dd_apply_seh(reinterpret_cast<uint8_t*>(actor), &a, who);
        if (r > 0) InterlockedIncrement(&g_mdApOk);
        if (r < 0) InterlockedIncrement(&g_mdApExc);
        return r;
    }
    std::string md_status(bool& any)
    {
        any = g_mdSeen || g_mdKept || g_mdOther || g_mdBleed || g_mdBullet || g_mdRead || g_mdFull || g_mdApOk || g_mdApExc;
        std::string src;
        for (int i = 0; i < 48; ++i)
            if (g_mdBySrc[i]) { src += " fonte" + std::to_string(i) + "=" + std::to_string((long)g_mdBySrc[i]); g_mdBySrc[i] = 0; }
        char b[420];
        snprintf(b, sizeof b, "golpes_nas_copias=%ld enviados=%ld tiros_ignorados=%ld sangramento=%ld outro_atacante=%ld (ultimo atk=0x%x fonte=%ld) falha_leitura=%ld fila_cheia=%ld aplicados=%ld excecao=%ld%s",
                 (long)g_mdSeen, (long)g_mdKept, (long)g_mdBullet, (long)g_mdBleed, (long)g_mdOther, (unsigned)g_mdOtherAtk, (long)g_mdOtherSrc, (long)g_mdRead, (long)g_mdFull, (long)g_mdApOk, (long)g_mdApExc, src.c_str());
        g_mdSeen = g_mdKept = g_mdOther = g_mdBleed = g_mdBullet = g_mdRead = g_mdFull = g_mdApOk = g_mdApExc = 0;
        return b;
    }
    void cfg_dmg(int on) { C.natdmg = on == 1 && g_ddBuild ? 1 : 0; }
    int nd_on() { return C.natdmg; }
    int last_cause(double& age)
    {
        int c = (int)InterlockedExchange(&g_lhCause, 0);
        age = c ? (double)(wall_ms() - (int64_t)g_lhAt) / 1000.0 : 1e9;
        return c;
    }
    void cfg_m1(int mut, int adoptKept, int offanim, int offvb) { C.natmut = mut; C.natadopt_kept = adoptKept; C.offanim = offanim; C.offvb = offvb; }
    void cfg_vis(int on) { C.npcvis = on; }
    void cfg_qg(int on) { C.natqg = on; }
    void cfg_fd(int on) { C.natfd = on; if (!on) H.fd.reset(); }
    void cfg_deadall(int on) { C.natdeadall = on ? 1 : 0; }
    void cfg_hotkeep(int on) { C.nathotkeep = on ? 1 : 0; }
    void cfg_deadkeep(int on) { C.natdeadkeep = on ? 1 : 0; }
    void cfg_deadpos(int on) { C.natdeadpos = on ? 1 : 0; }
    void gc_keep(std::vector<uint64_t>& out)
    {
        std::vector<std::pair<bool, uint64_t>> v;
        for (auto& [k, e] : K.E)
        {
            UObject* a = e.st == DEAD ? e.body.get() : nullptr;
            v.push_back({a != nullptr, reinterpret_cast<uint64_t>(a)});
        }
        s2nmc::dead_keep(C.natdeadkeep != 0, v, out);
    }
    void cfg_wide(int on) { C.natmax = on ? (std::numeric_limits<int>::max)() : 8; }
    void cfg_rhost(int on) { C.natrhost = on; }
    void cfg_far(int on, double deadR) { C.natfar = on ? 1 : 0; C.natfardead = deadR > 0 ? deadR : 4500; }
    void cfg_wpn(int sid, int arm) { g_wsOn = sid == 1 ? 1 : 0; g_wsArmOn = arm == 1 ? 1 : 0; }
    void cfg_cpd(int on, int hp) { g_cpOn = std::clamp(on, 0, 2); g_cpHp = hp == 1 ? 1 : 0; }
    void cfg_cover(int on) { C.natcover = on ? 1 : 0; if (!on) cvt_clear_all(); }
    void cfg_msec(int on) { C.natmsec = on ? 1 : 0; }
    void cfg_mre(int on) { C.natmre = on ? 1 : 0; }
    void cfg_genmre(int on) { C.natgenmre = on ? 1 : 0; }
    void cfg_mloop(int on) { C.natmloop = on ? 1 : 0; }
    void cfg_msecq(int on) { C.natmsecq = on ? 1 : 0; }
    void cfg_mkeep(int on) { C.natmkeep = on ? 1 : 0; }
    void cfg_movl(int on) { C.natmovl = on ? 1 : 0; }
    void cfg_rmoff(int on) { C.natrmoff = on ? 1 : 0; }
    void cfg_navw(int on) { C.natnavw = on ? 1 : 0; }
    void cfg_wpnhid(int on) { C.natwpnhid = on ? 1 : 0; }
    void cfg_looksec(int on) { C.natlooksec = on ? 1 : 0; }
    void cfg_mutlook(int on) { C.natmutlook = on ? 1 : 0; }
    void cfg_genmk(int on) { C.natgenmk = on ? 1 : 0; }
    void cfg_midsc(int on) { C.natmidsc = on ? 1 : 0; }
    int cfg_lookre()
    {
        int n = 0;
        for (auto& [k, e] : K.E)
        {
            if (e.st != LIVE || e.look.empty()) continue;
            e.lookDone = 0; e.lookChk = -100; ++n;
        }
        return n;
    }
    void cfg_gait(int on) { C.natgait = std::clamp(on, 0, 2); }
    void cfg_mab(int on) { C.natmab = std::clamp(on, 0, 3); }
    void cfg_cloak(int on) { C.natcloak = on ? 1 : 0; }
    void cfg_copy(int on) { C.natcopy = on ? 1 : 0; }
    void cfg_sync(int legs, int mbh, int qgsafe, int lostfix, int eqfix)
    {
        C.natlegs = legs ? 1 : 0; C.natmbh = mbh ? 1 : 0; C.natqgsafe = qgsafe ? 1 : 0; C.natlostfix = lostfix ? 1 : 0; C.nateqfix = eqfix ? 1 : 0;
    }
    void cfg_loco(int kinmv, int eqsync, int turn) { C.natkinmv = kinmv ? 1 : 0; C.nateqsync = eqsync ? 1 : 0; C.natturn = turn ? 1 : 0; }
    void cfg_gaitcp(int on) { C.natgaitcp = on ? 1 : 0; }
    void cfg_wpnlocal(int on) { C.natwpnlocal = on ? 1 : 0; }
    void cfg_loco_host(int on) { C.natloco = on ? 1 : 0; }
    void cfg_loco_prm(int on) { C.natlocoprm = on ? 1 : 0; }
    void cfg_loco_fl(int on) { C.natlocofl = on ? 1 : 0; }
    void cfg_traj(int on) { C.nattraj = on ? 1 : 0; }
    void cfg_traj_v(int on) { C.nattrajv = on ? 1 : 0; }
    void cfg_loco_sf(int on) { C.natlocosf = on ? 1 : 0; }
    void cfg_traj_own(int on) { C.nattrajown = on ? 1 : 0; }
    void cfg_cp_vel(int on) { C.natcpvel = on ? 1 : 0; }
    void cfg_props(int on) { C.natprops = on ? 1 : 0; }
    void cfg_dupfix(int on) { C.natdupfix = on ? 1 : 0; }
    void cfg_kin(int kin, int ord, int stop, int req, int pin) { C.natkin = kin ? 1 : 0; C.natkinord = ord ? 1 : 0; C.natkinstop = stop ? 1 : 0; C.natkinreq = req ? 1 : 0; C.natkinpin = pin ? 1 : 0; }
    void cfg_inert(int on, int dsrc, uint64_t me, uint64_t pup, uint64_t tgt)
    {
        g_inOn = on == 1 ? 1 : 0;
        g_dsOn = dsrc == 1 && g_ddDet ? 1 : 0;
        g_inMe = static_cast<uintptr_t>(me); g_inPup = static_cast<uintptr_t>(pup); g_inTgt = static_cast<uintptr_t>(tgt);
        if (!g_inOn) g_in.clear();
    }
    bool inert(uint64_t a, int32_t uid, int on) { return inert_api(static_cast<uintptr_t>(a), uid, on); }
    void classes(const std::vector<ClsRow>& rows)
    {
        std::vector<NCls> v;
        for (const ClsRow& r : rows)
            if (!r.cls.empty() && (r.kind == "human" || r.kind == "mutant" || r.kind == "mutant_phantom")) v.push_back({u2w(r.cls), r.kind != "human", u2w(r.sid), r.hpmax, {r.walk, r.run, r.sprint}, r.invis > 0});
        if (v.empty()) return;
        g_cls = std::move(v);
        g_clsOf.clear();
    }
    std::string lk_hold(const std::string& slot, const std::string& key, double age, double lease, double hmax) { return lk_hold_api(slot, key, age, lease, hmax); }
    int lk_off(uint64_t a, int on, double lease) { return lk_off_api((double)a, on, lease); }
    bool cl_swap(const std::string& hid, uint64_t loot, uint64_t src, int& mism) { return cl_swap_api(hid, (double)loot, (double)src, mism); }
    int cl_swap_tick() { return cl_swap_tick_api(); }
    uint64_t body(const std::string& hid, int& st)
    {
        Ent* e = shown_by_nm(suffix_of(hid), false);
        st = e ? (int)e->st : 0;
        return e ? reinterpret_cast<uint64_t>(e->body.get()) : 0;
    }
    void cl_own(uint64_t a, int on)
    {
        if (!a) { if (!on) g_clown.clear(); return; }
        if (on) g_clown.insert(static_cast<uintptr_t>(a)); else g_clown.erase(static_cast<uintptr_t>(a));
    }
    bool qi_npcadd(const std::string& sid) { return qi_npcadd_api(sid); }
    bool qr_npc(const std::string& sid, std::string& guid) { return qr_npc_api(sid, guid); }
    std::string b164_status() { return b164_status_api(); }
    void feed_mode(bool on) { if (g_feedOn != on) { g_feedOn = on; g_feedNew = false; g_feedText.clear(); } }
    void feed(const std::string& text) { if (!g_feedOn) return; g_feedText = text; g_feedNew = true; }
}

void s2n_nat_install()
{
    static bool b176 = false;
    if (!b176)
    {
        b176 = true;
        fo_install();
        mont_install();
        wpn_install();
        x0::hot::cb(Unreal::Hook::RegisterEngineTickPreCallback([](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UEngine*, float, bool) { tick_cb(); },
                                                    {false, false, STR("NolaNative"), STR("b176tick")}));
        x0::hot::on_shutdown([] { hot_out(); });
    }
}

void s2n_nat_register(RC::LuaMadeSimple::Lua& l)
{
    s2n_nat_install();
    l.register_function("s2n_nat_host", guarded<lua_host>);
    l.register_function("s2n_nat_client", guarded<lua_client>);
    l.register_function("s2n_nat_own", guarded<lua_own>);
    l.register_function("s2n_nat_dead", guarded<lua_dead>);
    l.register_function("s2n_nat_actor", guarded<lua_actor>);
    l.register_function("s2n_nat_filt", guarded<lua_filt>);
    l.register_function("s2n_nat_status", guarded<lua_status>);
    l.register_function("s2n_nat_dump", guarded<lua_dump>);
    l.register_function("s2n_nat_look", guarded<lua_look>);
    l.register_function("s2n_nat_cfg", guarded<lua_cfg>);
    l.register_function("s2n_nat_ping", guarded<lua_ping>);
    l.register_function("s2n_nat_listed", guarded<lua_listed>);
    l.register_function("s2n_nat_listed_addr", guarded<lua_listed_addr>);
    l.register_function("s2n_nat_corpse", guarded<lua_corpse>);
    l.register_function("s2n_lk_hold", guarded<lua_lk_hold>);
    l.register_function("s2n_lk_off", guarded<lua_lk_off>);
    l.register_function("s2n_cl_swap", guarded<lua_cl_swap>);
    l.register_function("s2n_cl_swap_tick", guarded<lua_cl_swap_tick>);
    l.register_function("s2n_qi_npcadd", guarded<lua_qi_npcadd>);
    l.register_function("s2n_qr_npc", guarded<lua_qr_npc>);
    l.register_function("s2n_b164_status", guarded<lua_b164_status>);
    l.register_function("s2n_nat_dump169", guarded<lua_dump169>);
    l.register_function("s2n_nat_dump170", guarded<lua_dump170>);
    l.register_function("s2n_nat_dump171", guarded<lua_dump171>);
    l.register_function("s2n_nat_dump172", guarded<lua_dump172>);
}
