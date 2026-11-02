#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include <Unreal/FMemory.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectArray.hpp>
#include <Unreal/UObjectGlobals.hpp>

#include "s2m_menu_logic.h"
#include "s2m_steam_logic.h"
#include "s2m_text.h"
#include "s2m_title_logic.h"
#include "s2r_net.h"
#include "s2r_x0.h"
#include "s2x.h"

using namespace RC;
using namespace RC::Unreal;

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool wr_seh(void* p, const void* in, size_t n)
    {
        __try { std::memcpy(p, in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    struct TArr
    {
        uint8_t* d;
        int32_t n;
        int32_t m;
    };

    struct FStr
    {
        wchar_t* d;
        int32_t n;
        int32_t m;
    };

    const uintptr_t kBuild = 0x33319a4;
    const uintptr_t kWeakGet = 0x20afdf6;
    const uintptr_t kSetName = 0x3331cf4;
    const uint8_t kBuildSig[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48,
                                 0x83, 0xec, 0x48, 0x0f, 0x29, 0x74, 0x24, 0x30, 0x48, 0x89, 0xd6, 0x48, 0x89};
    const uint8_t kWeakSig[] = {0x8b, 0x51, 0x04, 0x85, 0xd2, 0x74, 0x4a, 0x8b, 0x09, 0x31, 0xc0, 0x85, 0xc9, 0x78, 0x41};
    const uint8_t kNameSig[] = {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x81, 0x00, 0x03, 0x00, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x30, 0x48, 0x89, 0xce};
    const size_t kBtnSize = 0x60;
    const size_t kBtnSid = 0x28;
    const size_t kBtnAct = 0x38;
    const size_t kBtnShort = 0x40;
    const size_t kBtnDef = 0x50;
    const size_t kModelList = 0x30;
    const size_t kWAction = 0x338;
    const int kVtSlots = 91;
    const int kExec = 89;

    const uintptr_t kFlow = 0x346b32e;
    const uintptr_t kFlowState = 0xb0f6590;
    const uint8_t kFlowSig[] = {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x89, 0xce, 0x88, 0x51, 0x68, 0xe8, 0x82, 0x00, 0x00, 0x00, 0x0f, 0xb6, 0x05};
    const uint8_t kFlowMainMenu = 6;

    const uintptr_t kRefresh = 0x33308e2;
    const uint8_t kRefreshSig[] = {0x56, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0x81, 0xe0, 0x02, 0x00, 0x00, 0x48, 0x85, 0xc0, 0x74, 0x4a};
    const int kSelect = 160;

    const size_t kImageBrush = 0x170;
    const size_t kBrushImageSize = 0x34;

    using BuildFn = void (*)(int64_t, int64_t);
    using FlowFn = void (*)(int64_t, uint8_t);
    using WeakGetFn = int64_t (*)(int64_t);
    using SetNameFn = void (*)(int64_t, const FStr*);
    using ExecFn = void (*)(int64_t);

    x0::Flag F_menu("joinmenu");

    x0::Flag F_exit("csexit");

    x0::Flag F_refresh("csmenu");

    uint64_t g_tramp = 0;
    PLH::x64Detour* g_det = nullptr;
    bool g_tried = false;
    uint64_t g_flowTramp = 0;
    PLH::x64Detour* g_flowDet = nullptr;
    bool g_flowTried = false;
    int g_exits = 0;
    bool g_sigName = false;
    bool g_sigRefresh = false;
    int g_builds = 0;
    int g_refreshes = 0;
    x0::SaveStore g_builtStore = x0::SaveStore::normal;
    bool g_builtSwitching = false;
    int g_added = 0;
    bool g_listLogged = false;

    struct Act
    {
        int64_t obj = 0;
        void* vt[kVtSlots + 1] = {};
        ExecFn base = nullptr;
        std::wstring text;
        x0::Ref ref;
        int remade = 0;
    };
    Act g_host, g_join;
    std::atomic<int> g_clicks{0};

    x0::Ref g_sv;
    int64_t g_holder = 0;
    x0::Ref g_cont;

    int64_t g_popup = 0;
    x0::Ref g_popupRef;

    std::atomic<nola::Mode> g_mode{nola::Mode::none};
    bool g_world = false;
    bool g_busy = false;
    bool g_relay = false;
    uint64_t g_full0 = 0;
    int g_fullN = 0;
    int g_fullMax = 0;
    double g_joinAt = -1;
    bool g_peerSeen = false;

    int64_t weak_get(int64_t p)
    {
        __try { return reinterpret_cast<WeakGetFn>(exe() + kWeakGet)(p); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    bool set_name_seh(int64_t w, const FStr* s)
    {
        __try
        {
            reinterpret_cast<SetNameFn>(exe() + kSetName)(w, s);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool exec_seh(ExecFn f, int64_t o)
    {
        __try
        {
            f(o);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool refresh_seh(int64_t sv, ExecFn sel)
    {
        __try
        {
            reinterpret_cast<ExecFn>(exe() + kRefresh)(sv);
            sel(sv);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    using nola_text::tx;

    bool fstr_set(FStr& s, const std::wstring& w)
    {
        int32_t need = (int32_t)w.size() + 1;
        if (!s.d || s.m < need)
        {
            void* p = (*GMalloc)->Malloc(size_t(need) * sizeof(wchar_t), 8);
            if (!p) return false;
            if (s.d) (*GMalloc)->Free(s.d);
            s.d = static_cast<wchar_t*>(p);
            s.m = need;
        }
        std::memcpy(s.d, w.c_str(), size_t(need) * sizeof(wchar_t));
        s.n = need;
        return true;
    }

    bool fstr_new(FStr& s, const std::wstring& w)
    {
        s = FStr{};
        return fstr_set(s, w);
    }

    bool alive(int64_t obj, const x0::Ref& r)
    {
        return obj && r.valid() && r.address() == (uint64_t)obj;
    }

    bool root(UObject* o)
    {
        if (!o) return false;
        FUObjectItem* it = FUObjectArray::IndexToObject(o->GetInternalIndex());
        if (!it || it->GetUObject() != o) return false;
        it->SetRootSet();
        return true;
    }

    UObject* transient_pkg()
    {
        static UObject* t = nullptr;
        if (t) return t;
        UObjectGlobals::ForEachUObject([](UObject* o, int32, int32) {
            if (o && !o->GetOuterPrivate() && o->GetNamePrivate().ToString() == STR("/Engine/Transient"))
            {
                t = o;
                return LoopAction::Break;
            }
            return LoopAction::Continue;
        });
        if (t) x0::logf("NOLA menu: transient package /Engine/Transient = 0x%llx (index %d)", (unsigned long long)(uintptr_t)t, (int)t->GetInternalIndex());
        return t;
    }

    UObject* make(const wchar_t* cls)
    {
        auto* c = static_cast<UClass*>(x0::find_path(cls).get());
        UObject* outer = transient_pkg();
        if (!c || !outer) return nullptr;
        FStaticConstructObjectParameters P{c, outer};
        UObject* o = UObjectGlobals::StaticConstructObject(P);
        if (!root(o)) return nullptr;
        return o;
    }

    void on_click(int which);
    bool pawn();
    void settle(const char* why, std::function<void()> then, int tries = 40);

    const char* store_name(x0::SaveStore st) { return st == x0::SaveStore::coop ? "coop" : "normal"; }

    void exec_host(int64_t self)
    {
        if (g_host.base) g_host.base(self);
        x0::post([] { on_click(1); });
    }

    void exec_join(int64_t self)
    {
        if (g_join.base) g_join.base(self);
        x0::post([] { on_click(2); });
    }

    bool make_act(Act& a, void (*thunk)(int64_t))
    {
        const char* nm = thunk == &exec_host ? "host" : "join";
        if (a.obj)
        {
            void** cur = nullptr;
            bool live = alive(a.obj, a.ref) && rd_seh(reinterpret_cast<void*>(a.obj), &cur, 8) && cur == &a.vt[1];
            if (live) return true;
            ++a.remade;
            x0::logf("NOLA menu: menu action %s 0x%llx was destroyed while the main menu was away (remade #%d)", nm, (unsigned long long)a.obj, a.remade);
            a.obj = 0;
            a.ref.reset();
        }
        UObject* o = make(L"/Script/Stalker2.DoNothing");
        if (!o) return false;
        int64_t self = reinterpret_cast<int64_t>(o);
        void** vt = nullptr;
        if (!rd_seh(reinterpret_cast<void*>(self), &vt, 8) || !vt) return false;
        if (!rd_seh(vt - 1, a.vt, sizeof a.vt)) return false;
        a.base = reinterpret_cast<ExecFn>(a.vt[1 + kExec]);
        a.vt[1 + kExec] = reinterpret_cast<void*>(thunk);
        void** mine = &a.vt[1];
        if (!wr_seh(reinterpret_cast<void*>(self), &mine, 8)) return false;
        a.obj = self;
        a.ref = x0::Ref::of(o);
        x0::logf("NOLA menu: menu action %s = DoNothing 0x%llx, own vtable copy (91 slots, slot 89 -> NOLA)", nm, (unsigned long long)self);
        return true;
    }

    std::wstring wid(const std::string& s)
    {
        std::wstring w;
        size_t i = 0;
        while (i < s.size())
        {
            unsigned char c = (unsigned char)s[i];
            uint32_t cp = c;
            int k = 0;
            if (c >= 0xf0) { cp = c & 7; k = 3; }
            else if (c >= 0xe0) { cp = c & 15; k = 2; }
            else if (c >= 0xc0) { cp = c & 31; k = 1; }
            ++i;
            for (int j = 0; j < k && i < s.size(); ++j, ++i) cp = (cp << 6) | ((unsigned char)s[i] & 63);
            if (cp >= 0x10000)
            {
                cp -= 0x10000;
                w.push_back((wchar_t)(0xd800 + (cp >> 10)));
                w.push_back((wchar_t)(0xdc00 + (cp & 0x3ff)));
            }
            else w.push_back((wchar_t)cp);
        }
        return w;
    }

    std::string sid_of(const uint8_t* e)
    {
        FStr s{};
        if (!rd_seh(e + kBtnSid, &s, sizeof s) || !s.d || s.n <= 1 || s.n > 256) return {};
        wchar_t buf[257] = {};
        if (!rd_seh(s.d, buf, size_t(s.n) * sizeof(wchar_t))) return {};
        buf[s.n - 1] = 0;
        std::string o;
        for (int i = 0; i < s.n - 1; ++i) o += (buf[i] > 0 && buf[i] < 0x80) ? (char)buf[i] : '?';
        return o;
    }

    std::string act_class(const uint8_t* e)
    {
        uint64_t a = 0;
        if (!rd_seh(e + kBtnAct, &a, 8) || !a) return {};
        return x0::class_name(x0::Ref::addr(a));
    }

    std::string label_for(int which)
    {
        if (which == 1) return g_mode == nola::Mode::host ? tx(nola_text::T::menu_host_on) : tx(nola_text::T::menu_host);
        return g_mode == nola::Mode::join ? tx(nola_text::T::menu_join_on) : tx(nola_text::T::menu_join);
    }

    bool g_noPawnSeen = false;

    void ui_end()
    {
        g_noPawnSeen = false;
        x0::ui_session_end();
    }

    void end_session(const char* why)
    {
        ui_end();
        bool had = g_mode != nola::Mode::none;
        g_mode = nola::Mode::none;
        g_world = false;
        g_relay = false;
        g_joinAt = -1;
        g_peerSeen = false;
        x0::save_store_want(x0::SaveStore::normal);
        if (!pawn()) settle(why, nullptr);
        if (!had && s2r::x0_session_mode() == 0) return;
        x0::logf("NOLA menu: co-op off (%s): back to a normal solo game", why);
        x0::post([] {
            std::string w;
            s2r::x0_session(0, "", 0, w);
        });
    }

    struct Prep
    {
        uint8_t* lp = nullptr;
        TArr a{};
        std::vector<uint8_t> buf;
        int which = 0;
        int count = 1;
        nola::Plan pl{};
        std::string label;
        std::string tcls;
        bool main = false;
    };

    bool build_prep(int64_t pair, int64_t holder, Prep& P)
    {
        if (!pair || !F_menu.on()) return false;
        int64_t sv = weak_get(pair + 8);
        if (!sv || !x0::is_a(x0::Ref::addr((uint64_t)sv), L"MainMenuSubView")) return false;
        P.main = true;
        int64_t model = weak_get(pair);
        if (!model) return false;
        ++g_builds;
        if (g_world && g_mode != nola::Mode::none) end_session("main menu");
        if (g_mode == nola::Mode::none) x0::save_store_want(x0::SaveStore::normal);
        g_builtStore = x0::save_store();
        g_builtSwitching = x0::save_store_switching();
        if (g_builtSwitching)
        {
            x0::logf("NOLA menu: main menu build #%d while the save store switch is not finished (store %s): Continue and Load show the old index until the switch ends", g_builds,
                     store_name(g_builtStore));
            settle("main menu built during a store switch", nullptr);
        }
        g_sv = x0::Ref::addr((uint64_t)sv);
        g_holder = holder;
        uint8_t* lp = reinterpret_cast<uint8_t*>(model + kModelList);
        TArr a{};
        if (!rd_seh(lp, &a, sizeof a) || !a.d || a.n <= 0 || a.n > 64) return false;
        std::vector<nola::Entry> es;
        for (int i = 0; i < a.n; ++i)
        {
            const uint8_t* e = a.d + size_t(i) * kBtnSize;
            es.push_back({act_class(e), sid_of(e)});
        }
        if (!g_listLogged)
        {
            g_listLogged = true;
            std::string l;
            for (size_t i = 0; i < es.size(); ++i) l += (i ? "; " : "") + std::to_string(i) + " " + es[i].cls + " " + es[i].sid;
            x0::logf("NOLA menu: main menu %s has %d entries: %s", x0::obj_name(x0::Ref::addr((uint64_t)model)).c_str(), a.n, l.c_str());
        }
        g_cont.reset();
        for (int i = 0; i < a.n; ++i)
            if (es[(size_t)i].cls == "LoadLastSaveAction")
            {
                uint64_t c = 0;
                rd_seh(a.d + size_t(i) * kBtnSize + kBtnAct, &c, 8);
                g_cont = x0::Ref::addr(c);
            }
        std::vector<int> ws = nola::shown(x0::fi("menurole", 1) == 1, x0::is_host(), x0::is_client());
        nola::Plan pl = nola::plan(es, ws.empty() ? 0 : ws[0]);
        if (pl.at < 0) return false;
        size_t k = ws.size();
        std::vector<uint8_t> buf(size_t(a.n + k) * kBtnSize);
        if (!rd_seh(a.d, buf.data(), size_t(pl.at) * kBtnSize) ||
            !rd_seh(a.d + size_t(pl.at) * kBtnSize, buf.data() + size_t(pl.at + k) * kBtnSize, size_t(a.n - pl.at) * kBtnSize))
            return false;
        std::string label;
        for (size_t j = 0; j < k; ++j)
        {
            int which = ws[j];
            Act& act = which == 1 ? g_host : g_join;
            std::string l = label_for(which);
            FStr sid{};
            if (!make_act(act, which == 1 ? &exec_host : &exec_join) || !fstr_new(sid, wid(l))) return false;
            uint8_t* ne = buf.data() + size_t(pl.at + j) * kBtnSize;
            if (!rd_seh(a.d + size_t(pl.tmpl) * kBtnSize, ne, kBtnSize)) return false;
            std::memcpy(ne + kBtnSid, &sid, sizeof(FStr));
            std::memcpy(ne + kBtnAct, &act.obj, 8);
            std::memset(ne + kBtnShort, 0, 16);
            std::memset(ne + kBtnDef, 0, 16);
            label += (j ? " + " : "") + l;
        }
        P.lp = lp;
        P.a = a;
        P.buf = std::move(buf);
        P.which = k == 2 ? 3 : ws[0];
        P.count = (int)k;
        P.pl = pl;
        P.label = std::move(label);
        P.tcls = es[(size_t)pl.tmpl].cls;
        return true;
    }

    x0::Flag F_title("nolatitle");
    x0::Flag F_titleImg("nolatitleimg");
    x0::Ref g_title;
    x0::Ref g_titleSlot;
    x0::Ref g_titleOv;
    x0::Ref g_badge;
    x0::Ref g_badgeSv;
    std::string g_titleKey;
    nola_title::Box g_titleBox;
    int g_titleSrc = 0;
    nola_title::Box g_badgeBox;
    bool g_badgeBoxOk = false;
    int g_titleErrs = 0;
    int g_titleMade = 0;
    int g_titleMoves = 0;
    int g_titleWait = 0;
    std::string g_badgeWhy;
    std::atomic<bool> g_titlePosted{false};

    const int kGeom = 0x38;
    const int kGeomWait = 3;
    const size_t kBrushDrawAs = 0x1c;
    const size_t kBadgeContainer = 0x2d0;

    struct TitleTex
    {
        x0::Ref tex;
        std::string stamp;
        bool tried = false;
        nola_title::Why why = nola_title::Why::missing;
        int w = 0, h = 0;
    };
    TitleTex g_tt;

    template <class T> bool rd_at(uint64_t a, T& v) { return a && rd_seh(reinterpret_cast<const void*>(a), &v, sizeof v); }

    x0::Ref parent_of(const x0::Ref& w)
    {
        uint64_t slot = 0, par = 0;
        if (!w.valid() || !rd_at(w.address() + 0x30, slot) || !slot) return {};
        x0::Ref s = x0::Ref::addr(slot);
        if (!x0::is_a(s, L"PanelSlot") || !rd_at(slot + 0x28, par) || !par) return {};
        x0::Ref p = x0::Ref::addr(par);
        return x0::is_a(p, L"PanelWidget") ? p : x0::Ref{};
    }

    x0::Ref menu_overlay(const x0::Ref& sv)
    {
        uint64_t bp = 0;
        if (!rd_at(sv.address() + 0x2d8, bp) || !bp) return {};
        x0::Ref w = x0::Ref::addr(bp), top;
        for (int i = 0; i < 16 && w.valid(); ++i)
        {
            w = parent_of(w);
            if (w.valid() && x0::class_name(w) == "Overlay") top = w;
        }
        return top;
    }

    x0::Ref find_class(const x0::Ref& w, const wchar_t* cls, int depth)
    {
        if (!w.valid() || depth < 0) return {};
        if (x0::is_a(w, cls)) return w;
        x0::Call n(w, L"GetChildrenCount");
        if (!n.ok() || !n.run()) return {};
        int64_t k = n.ri();
        for (int64_t j = 0; j < k && j < 64; ++j)
        {
            x0::Call c(w, L"GetChildAt");
            c.i(L"Index", j);
            if (!c.ok() || !c.run()) break;
            x0::Ref f = find_class(c.ro(), cls, depth - 1);
            if (f.valid()) return f;
        }
        return {};
    }

    x0::Ref stock_badge(const x0::Ref& ov)
    {
        return find_class(ov, L"MenuMainDLCIndicator", 4);
    }

    bool geom(const x0::Ref& w, uint8_t* g)
    {
        x0::Call c(w, L"GetCachedGeometry");
        return c.ok() && c.run() && c.rraw(g, kGeom);
    }

    bool slate_v(const x0::Ref& lib, const wchar_t* fn, const wchar_t* arg, const uint8_t* g, const double* in, double* out)
    {
        x0::Call c(lib, fn);
        c.raw(L"Geometry", g, kGeom);
        if (arg) c.raw(arg, in, 16);
        return c.ok() && c.run() && c.rraw(out, 16);
    }

    bool box_of(const x0::Ref& w, const x0::Ref& ov, nola_title::Box& b)
    {
        x0::Ref lib = x0::find_path(L"/Script/UMG.Default__SlateBlueprintLibrary");
        alignas(16) uint8_t gl[kGeom] = {}, go[kGeom] = {};
        double sz[2] = {}, z[2] = {}, a0[2] = {}, a1[2] = {}, p0[2] = {}, p1[2] = {};
        if (!lib.valid() || !geom(w, gl) || !geom(ov, go)) return false;
        if (!slate_v(lib, L"GetLocalSize", nullptr, gl, nullptr, sz) || sz[0] <= 0 || sz[1] <= 0) return false;
        if (!slate_v(lib, L"LocalToAbsolute", L"LocalCoordinate", gl, z, a0) || !slate_v(lib, L"LocalToAbsolute", L"LocalCoordinate", gl, sz, a1)) return false;
        if (!slate_v(lib, L"AbsoluteToLocal", L"AbsoluteCoordinate", go, a0, p0) || !slate_v(lib, L"AbsoluteToLocal", L"AbsoluteCoordinate", go, a1, p1)) return false;
        b = {p0[0], p0[1], p1[0] - p0[0], p1[1] - p0[1]};
        return nola_title::sane(b);
    }

    bool badge_box(const x0::Ref& ind, const x0::Ref& ov, nola_title::Box& b)
    {
        uint64_t ct = 0;
        if (rd_at(ind.address() + kBadgeContainer, ct) && ct)
        {
            x0::Ref hb = x0::Ref::addr(ct);
            x0::Call n(hb, L"GetChildrenCount");
            int64_t k = n.ok() && n.run() ? n.ri() : 0;
            bool any = false;
            for (int64_t j = 0; j < k && j < 16; ++j)
            {
                x0::Call c(hb, L"GetChildAt");
                c.i(L"Index", j);
                if (!c.ok() || !c.run()) break;
                x0::Ref ch = c.ro();
                x0::Call v(ch, L"GetVisibility");
                int64_t vis = v.ok() && v.run() ? v.ri() : 1;
                nola_title::Box cb;
                if (!ch.valid() || vis == 1 || vis == 2 || !box_of(ch, ov, cb)) continue;
                b = any ? nola_title::unite(b, cb) : cb;
                any = true;
            }
            if (any) return true;
        }
        return box_of(ind, ov, b);
    }

    void title_off(const char* why)
    {
        if (!g_title.valid()) { g_title.reset(); g_titleSlot.reset(); g_titleOv.reset(); return; }
        x0::Call(g_title, L"RemoveFromParent").run();
        g_title.reset();
        g_titleSlot.reset();
        g_titleOv.reset();
        x0::logf("NOLA menu: main menu title image removed (%s)", why);
    }

    bool title_fail(const char* why)
    {
        if (++g_titleErrs <= 5) x0::logf("NOLA menu: main menu title image not shown: %s", why);
        return false;
    }

    std::string u8(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? n : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    const std::wstring& title_png()
    {
        static std::wstring p;
        static bool done = false;
        if (done) return p;
        done = true;
        HMODULE m = nullptr;
        wchar_t b[2048] = {};
        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&title_png), &m) && m)
        {
            DWORD n = GetModuleFileNameW(m, b, 2048);
            if (n > 0 && n < 2048) p = nola_title::asset_path(std::wstring_view(b, n));
        }
        x0::logf("NOLA menu: title image path %s", p.empty() ? "(DLL path unknown)" : u8(p).c_str());
        return p;
    }

    std::string file_stamp(const std::wstring& p)
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (p.empty() || !GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a) || (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
        uint64_t sz = (uint64_t(a.nFileSizeHigh) << 32) | a.nFileSizeLow;
        uint64_t mt = (uint64_t(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
        return std::to_string(sz) + ":" + std::to_string(mt);
    }

    void title_tex_drop()
    {
        if (UObject* o = g_tt.tex.get())
            if (FUObjectItem* it = FUObjectArray::IndexToObject(o->GetInternalIndex()))
                if (it->GetUObject() == o) it->UnsetRootSet();
        g_tt.tex.reset();
    }

    x0::Ref title_tex(const x0::Ref& ctx)
    {
        using nola_title::Why;
        const std::wstring& p = title_png();
        std::string st = file_stamp(p);
        if (g_tt.tried && st == g_tt.stamp) return g_tt.why == Why::ok ? g_tt.tex : x0::Ref{};
        title_tex_drop();
        g_tt.tried = true;
        g_tt.stamp = st;
        g_tt.w = g_tt.h = 0;
        std::string bytes;
        bool rd = !st.empty() && x0::read_bin(p, bytes);
        int w = 0, h = 0;
        Why png = rd ? nola_title::png_check(bytes, w, h) : Why::ok;
        g_tt.why = nola_title::decide(1, !st.empty(), rd, png);
        if (g_tt.why == Why::ok)
        {
            x0::Ref lib = x0::find_path(L"/Script/Engine.Default__KismetRenderingLibrary");
            x0::Ref t;
            if (lib.valid())
            {
                x0::Call c(lib, L"ImportFileAsTexture2D");
                c.o(L"WorldContextObject", ctx).s(L"Filename", p);
                if (c.ok() && c.run()) t = c.ro();
            }
            if (t.valid() && x0::is_a(t, L"Texture2D") && root(t.get()))
            {
                g_tt.tex = t;
                g_tt.w = w;
                g_tt.h = h;
                x0::logf("NOLA menu: title image %s loaded: %dx%d PNG, %llu bytes -> transient %s %s (rooted)", u8(p).c_str(), w, h, (unsigned long long)bytes.size(), x0::class_name(t).c_str(),
                         x0::obj_name(t).c_str());
                return t;
            }
            g_tt.why = Why::no_texture;
        }
        x0::logf("NOLA menu: title image %s not used (%s): stock main menu only", p.empty() ? "(no path)" : u8(p).c_str(), nola_title::why_text(g_tt.why));
        return {};
    }

    bool title_place(const x0::Ref& w, const x0::Ref& slot, const nola_title::Box& r)
    {
        float bs[2] = {(float)r.w, (float)r.h};
        if (!wr_seh(reinterpret_cast<uint8_t*>(w.address()) + kImageBrush + kBrushImageSize, bs, sizeof bs)) return false;
        float m[4] = {(float)r.x, (float)r.y, 0.f, 0.f};
        x0::Call p(slot, L"SetPadding");
        p.raw(L"InPadding", m, (int)sizeof m);
        if (!p.ok() || !p.run()) return false;
        double ds[2] = {r.w, r.h};
        x0::Call d(w, L"SetDesiredSizeOverride");
        d.raw(L"DesiredSize", ds, (int)sizeof ds);
        if (!d.ok() || !d.run()) x0::logf("NOLA menu: Image::SetDesiredSizeOverride failed, the brush size %.0fx%.0f stays", r.w, r.h);
        return true;
    }

    const char* title_image(const x0::Ref& ov, const x0::Ref& tex, const nola_title::Box& r, x0::Ref& out, x0::Ref& slotOut, int& drawAs)
    {
        auto* c = static_cast<UClass*>(x0::find_path(L"/Script/UMG.Image").get());
        UObject* outer = ov.get() ? ov.get()->GetOuterPrivate() : nullptr;
        if (!outer) outer = transient_pkg();
        if (!c || !outer) return "UMG Image class or outer not found";
        FStaticConstructObjectParameters P{c, outer};
        x0::Ref w = x0::Ref::of(UObjectGlobals::StaticConstructObject(P));
        if (!w.valid() || !x0::is_a(w, L"Image")) return "UMG Image not constructed";
        x0::Call b(w, L"SetBrushFromTexture");
        b.o(L"Texture", tex).b(L"bMatchSize", false);
        if (!b.ok() || !b.run()) return "Image::SetBrushFromTexture failed";
        uint8_t da = 0xff;
        rd_seh(reinterpret_cast<uint8_t*>(w.address()) + kImageBrush + kBrushDrawAs, &da, 1);
        drawAs = da;
        x0::Call a(ov, L"AddChildToOverlay");
        a.o(L"Content", w);
        if (!a.ok() || !a.run()) return "Overlay::AddChildToOverlay failed";
        x0::Ref slot = a.ro();
        if (!slot.valid()) return "Overlay::AddChildToOverlay gave no slot";
        x0::Call(slot, L"SetHorizontalAlignment").i(L"InHorizontalAlignment", 1).run();
        x0::Call(slot, L"SetVerticalAlignment").i(L"InVerticalAlignment", 1).run();
        x0::Call(w, L"SetVisibility").i(L"InVisibility", 3).run();
        x0::Call(w, L"SetRenderOpacity").f(L"InOpacity", (float)nola_title::kOpacity).run();
        if (!title_place(w, slot, r))
        {
            x0::Call(w, L"RemoveFromParent").run();
            return "title size or padding not applied";
        }
        out = w;
        slotOut = slot;
        return nullptr;
    }

    bool ensure_title()
    {
        x0::Ref sv = g_sv;
        if (!F_title.on() || !F_titleImg.on() || !F_menu.on() || !sv.valid() || !x0::is_a(sv, L"MainMenuSubView"))
        {
            if (g_title.valid() || g_titleOv.valid()) title_off(!F_title.on() ? "nolatitle=0" : !F_titleImg.on() ? "nolatitleimg=0" : !F_menu.on() ? "joinmenu=0" : "main menu gone");
            return false;
        }
        if (g_titleErrs >= 5 && !g_title.valid()) return false;
        x0::Ref ov = menu_overlay(sv);
        if (!ov.valid()) return title_fail("no Overlay above MenuSubViewBase.ButtonPanel");
        x0::Ref tex = title_tex(sv);
        if (!tex.valid())
        {
            if (g_title.valid()) title_off("title image not usable");
            return false;
        }
        if (!g_badgeSv.same(sv))
        {
            g_badge.reset();
            g_badgeBoxOk = false;
        }
        nola_title::Box lb;
        bool geo = g_badge.valid() && badge_box(g_badge, ov, lb);
        if (!geo)
        {
            uint64_t was = g_badge.address();
            g_badge = stock_badge(ov);
            g_badgeSv = sv;
            std::string why = g_badge.valid() ? "" : "no MenuMainDLCIndicator in the main menu overlay";
            if (why != g_badgeWhy && !why.empty()) x0::logf("NOLA menu: stock badge not found (%s)", why.c_str());
            if (g_badge.valid() && g_badge.address() != was)
                x0::logf("NOLA menu: stock badge = %s %s 0x%llx in %s %s", x0::class_name(g_badge).c_str(), x0::obj_name(g_badge).c_str(), (unsigned long long)g_badge.address(), x0::class_name(parent_of(g_badge)).c_str(),
                         x0::obj_name(parent_of(g_badge)).c_str());
            g_badgeWhy = why;
            geo = g_badge.valid() && badge_box(g_badge, ov, lb);
        }
        int src = 2;
        if (geo)
        {
            g_badgeBox = lb;
            g_badgeBoxOk = true;
        }
        else if (g_badgeBoxOk)
        {
            lb = g_badgeBox;
            src = 1;
        }
        else
        {
            if (!g_title.valid() && ++g_titleWait < kGeomWait) return false;
            lb = nola_title::kStockBadge;
            src = 0;
        }
        static const char* const kSrc[] = {"the mockup layout numbers, its geometry is not ready yet", "its last measured geometry", "its geometry"};
        nola_title::Box r = nola_title::corner(lb, x0::fi("nolatitlegap", nola_title::kDefGap), g_tt.w, g_tt.h);
        if (r.w <= 0) return title_fail("no title size");
        if (g_title.valid() && g_titleOv.same(ov) && g_tt.stamp == g_titleKey && parent_of(g_title).same(ov))
        {
            if (nola_title::moved(r, g_titleBox) || (src == 2 && g_titleSrc != 2))
            {
                bool ok = title_place(g_title, g_titleSlot, r);
                if (++g_titleMoves <= 10)
                    x0::logf("NOLA menu: main menu title image moved to %.1f,%.1f %.1fx%.1f units (stock badge %.1f,%.1f %.1fx%.1f from %s) ok=%s", r.x, r.y, r.w, r.h, lb.x, lb.y, lb.w, lb.h,
                             kSrc[src], ok ? "true" : "false");
                g_titleBox = r;
                g_titleSrc = src;
            }
            return true;
        }
        title_off("menu rebuilt or title image changed");
        x0::Ref w, slot;
        int da = -1;
        const char* bad = title_image(ov, tex, r, w, slot, da);
        if (bad) return title_fail(bad);
        g_title = w;
        g_titleSlot = slot;
        g_titleOv = ov;
        g_titleKey = g_tt.stamp;
        g_titleBox = r;
        g_titleSrc = src;
        g_titleWait = 0;
        ++g_titleMade;
        x0::logf("NOLA menu: main menu title image shown (#%d): %dx%d PNG as %.1fx%.1f units at %.1f,%.1f in %s %s, right of the stock badge %.1f,%.1f %.1fx%.1f (%s), gap %d, opacity %.2f, brush DrawAs %d",
                 g_titleMade, g_tt.w, g_tt.h, r.w, r.h, r.x, r.y, x0::class_name(ov).c_str(), x0::obj_name(ov).c_str(), lb.x, lb.y, lb.w, lb.h, kSrc[src],
                 nola_title::clamp_gap(x0::fi("nolatitlegap", nola_title::kDefGap)), nola_title::kOpacity, da);
        return true;
    }

    void post_title()
    {
        if (g_titlePosted.exchange(true)) return;
        x0::post([] {
            g_titlePosted = false;
            ensure_title();
        });
    }

    int g_prepErrs = 0;

    void build_detour(int64_t pair, int64_t holder)
    {
        BuildFn orig = reinterpret_cast<BuildFn>(g_tramp);
        Prep P;
        bool ins = false;
        try
        {
            ins = build_prep(pair, holder, P);
        }
        catch (const std::exception& e)
        {
            ins = false;
            if (++g_prepErrs <= 5) x0::logf("NOLA menu: menu entry not added (exception: %s): stock main menu", e.what());
        }
        catch (...)
        {
            ins = false;
            if (++g_prepErrs <= 5) x0::log("NOLA menu: menu entry not added (exception): stock main menu");
        }
        if (P.main) post_title();
        if (!ins) { orig(pair, holder); return; }
        TArr t{P.buf.data(), P.a.n + P.count, P.a.n + P.count};
        if (!wr_seh(P.lp, &t, sizeof t)) { orig(pair, holder); return; }
        orig(pair, holder);
        wr_seh(P.lp, &P.a, sizeof P.a);
        ++g_added;
        if (g_added <= 3 || g_added % 50 == 0)
            x0::logf("NOLA menu: main menu build #%d: %d entr%s \"%s\" from %d (template %d %s, role now %s)", g_added, P.count, P.count == 1 ? "y" : "ies", P.label.c_str(), P.pl.at, P.pl.tmpl,
                     P.tcls.c_str(), x0::role().c_str());
    }

    void relabel(int which)
    {
        if (!g_sigName || !g_sv.valid() || !g_holder) return;
        Act& act = which == 1 ? g_host : g_join;
        if (!act.obj) return;
        TArr h{};
        if (!rd_seh(reinterpret_cast<void*>(g_holder), &h, sizeof h) || !h.d || h.n <= 0 || h.n > 64) return;
        for (int i = 0; i < h.n; ++i)
        {
            int64_t w = 0, ac = 0;
            if (!rd_seh(h.d + size_t(i) * 8, &w, 8) || !w) continue;
            if (!rd_seh(reinterpret_cast<void*>(w + kWAction), &ac, 8) || ac != act.obj) continue;
            FStr nm{};
            if (!fstr_new(nm, wid(label_for(which)))) return;
            bool ok = set_name_seh(w, &nm);
            x0::logf("NOLA menu: entry relabeled \"%s\" ok=%s", label_for(which).c_str(), ok ? "true" : "false");
            return;
        }
    }

    bool popup(const std::string& title, const std::string& desc)
    {
        x0::logf("NOLA NOLA popup: %s | %s", title.c_str(), desc.c_str());
        if (g_popup && !alive(g_popup, g_popupRef))
        {
            x0::logf("NOLA menu: popup action 0x%llx was destroyed while the main menu was away: new one", (unsigned long long)g_popup);
            g_popup = 0;
            g_popupRef.reset();
        }
        if (!g_popup)
        {
            UObject* o = make(L"/Script/Stalker2.ShowPopupAction");
            if (!o) { x0::log("NOLA menu: ShowPopupAction not created: popup only in the log"); return false; }
            g_popup = reinterpret_cast<int64_t>(o);
            g_popupRef = x0::Ref::of(o);
        }
        FStr ps[3] = {};
        if (!fstr_new(ps[0], wid(title)) || !fstr_new(ps[1], wid(desc)) || !fstr_new(ps[2], wid(tx(nola_text::T::popup_ok)))) return false;
        uint8_t* p = reinterpret_cast<uint8_t*>(g_popup);
        FStr none{};
        if (!wr_seh(p + 0x30, &ps[0], sizeof(FStr)) || !wr_seh(p + 0x40, &ps[1], sizeof(FStr)) || !wr_seh(p + 0x50, &ps[2], sizeof(FStr)) || !wr_seh(p + 0x60, &none, sizeof(FStr)))
            return false;
        void** vt = nullptr;
        if (!rd_seh(p, &vt, 8) || !vt) return false;
        ExecFn f = nullptr;
        if (!rd_seh(vt + kExec, &f, 8) || !f) return false;
        bool ok = exec_seh(f, g_popup);
        if (!ok) x0::log("NOLA menu: ShowPopupAction::vf_89 raised an exception");
        return ok;
    }

    void say(const std::string& s, double secs = 6) { x0::ui_msg("nola", s, secs); }

    void refresh_menu(const char* why)
    {
        x0::Ref sv = g_sv;
        if (!F_refresh.on() || !g_sigRefresh || !sv.valid() || !x0::is_a(sv, L"MainMenuSubView") || pawn()) return;
        x0::SaveStore now = x0::save_store();
        if (g_builtStore == now && !g_builtSwitching) return;
        int64_t o = (int64_t)sv.address();
        void** vt = nullptr;
        ExecFn sel = nullptr;
        if (!rd_seh(reinterpret_cast<void*>(o), &vt, 8) || !vt || !rd_seh(vt + kSelect, &sel, 8) || !sel) return;
        x0::SaveStore was = g_builtStore;
        bool wasSwitching = g_builtSwitching;
        int b = g_builds;
        bool ok = refresh_seh(o, sel);
        ++g_refreshes;
        int hs = x0::save_store_has_save();
        x0::logf("NOLA menu: main menu rebuilt #%d for the %s save store (%s; built for %s%s): stock refresh exe+0x33308e2 ok=%s, build #%d -> #%d, Continue %s", g_refreshes, store_name(now), why,
                 store_name(was), wasSwitching ? " during a switch" : "", ok ? "true" : "false", b, g_builds, hs == 1 ? "shown" : hs == 0 ? "hidden (no save in this store)" : "unknown");
    }

    void settle(const char* why, std::function<void()> then, int tries)
    {
        x0::after(x0::Mod::session, 0.25, [why, then, tries] {
            if (!x0::save_store_ready() && tries > 0)
            {
                settle(why, then, tries - 1);
                return;
            }
            if (!x0::save_store_ready()) x0::logf("NOLA menu: save store switch not finished after 10 s (%s): main menu kept as built", why);
            else refresh_menu(why);
            if (then) then();
        });
    }

    std::wstring last_file() { return x0::bridge_file(L"nola_lasthost.txt"); }

    std::string last_host()
    {
        std::string t;
        if (!x0::read_text(last_file(), t)) return {};
        std::string ip;
        int port = 0;
        if (!s2r::disc::parse_addr(t, ip, port)) return {};
        return ip + ":" + std::to_string(port);
    }

    struct Scan
    {
        std::vector<s2r::disc::Host> hosts;
        std::vector<std::string> pref;
        std::string err;
        int port = 0;
    };

    Scan scan_now(const std::string& forced)
    {
        Scan r;
        s2r::Config c = x0::fi("menurole", 1) == 1 ? s2r::x0_config_role(false) : s2r::x0_config();
        r.port = c.peer_port;
        std::vector<s2r::disc::Target> extra;
        std::string ip;
        int port = 0;
        if (!forced.empty() && s2r::disc::parse_addr(forced, ip, port))
        {
            extra.push_back({ip, port});
            r.pref.push_back(ip + ":" + std::to_string(port));
        }
        std::string lh = last_host();
        if (!lh.empty() && s2r::disc::parse_addr(lh, ip, port))
        {
            extra.push_back({ip, port});
            r.pref.push_back(lh);
        }
        for (auto& np : nola::peers(x0::fs("netpeer", "127.0.0.1")))
        {
            extra.push_back({np, c.peer_port});
            r.pref.push_back(np + ":" + std::to_string(c.peer_port));
        }
        auto t = s2r::disc::targets({c.peer_port}, extra, x0::fi("nolabcast", 1) == 1);
        int64_t w = x0::fi("nolascan", 1500);
        r.hosts = s2r::disc::run(t, (int)(w < 200 ? 200 : w > 10000 ? 10000 : w), &r.err);
        return r;
    }

    void join_continue(std::string name, int n, int max, int tries)
    {
        if (g_mode != nola::Mode::join) return;
        if (!x0::save_store_ready() && tries > 0)
        {
            x0::after(x0::Mod::session, 0.25, [name, n, max, tries] { join_continue(name, n, max, tries - 1); });
            return;
        }
        if (!x0::save_store_ready()) x0::log("NOLA menu: co-op save store not ready after 10 s: Continue uses the store that is active now");
        else refresh_menu("join");
        int hs = x0::save_store_has_save();
        if (hs == 0) x0::log("NOLA menu: the co-op save store has no save to continue: no stock Continue, the player picks Load or New Game");
        if (g_cont.valid() && hs != 0)
        {
            void** vt = nullptr;
            ExecFn f = nullptr;
            int64_t o = (int64_t)g_cont.address();
            if (rd_seh(reinterpret_cast<void*>(o), &vt, 8) && vt && rd_seh(vt + kExec, &f, 8) && f)
            {
                say(nola::fmt(tx(nola_text::T::say_joining), name, n, max), 10);
                bool ok = exec_seh(f, o);
                x0::logf("NOLA menu: stock Continue (LoadLastSaveAction::vf_89) ok=%s: the host's world follows by sxjoin", ok ? "true" : "false");
                if (ok) return;
            }
        }
        popup(tx(nola_text::T::popup_title_join), nola::fmt(tx(nola_text::T::popup_joined_nosave), name, n, max));
    }

    void join_result(const Scan& s)
    {
        g_busy = false;
        std::string list;
        for (auto& h : s.hosts) list += " " + h.name + "@" + h.addr() + " " + std::to_string(h.n) + "/" + std::to_string(h.max) + (h.hub ? " hub" : "");
        x0::logf("NOLA menu: LAN search on UDP %d: %d host(s)%s%s", s.port, (int)s.hosts.size(), list.c_str(), s.err.empty() ? "" : (" error: " + s.err).c_str());
        nola::Pick pk = nola::choose(s.hosts, s.pref);
        if (pk.why == nola::Why::none) { popup(tx(nola_text::T::popup_title_join), tx(nola_text::T::popup_no_host)); return; }
        if (pk.why == nola::Why::full)
        {
            const auto& h = s.hosts[0];
            popup(tx(nola_text::T::popup_title_join), nola::fmt(tx(nola_text::T::popup_full), h.name, h.n, h.max));
            return;
        }
        const auto& h = s.hosts[(size_t)pk.i];
        std::string why;
        if (!s2r::x0_session(2, h.ip, h.port, why)) { popup(tx(nola_text::T::popup_title_join), tx(nola_text::T::popup_fail) + " (" + why + ")"); return; }
        g_mode = nola::Mode::join;
        g_relay = true;
        g_joinAt = x0::now();
        g_peerSeen = false;
        s2r::Stats st;
        g_full0 = s2r::x0_stats(st) ? st.full_rx : 0;
        g_fullN = h.n;
        g_fullMax = h.max;
        x0::write_text(last_file(), h.addr() + "\n");
        relabel(2);
        x0::logf("NOLA menu: joining %s at %s (%d/%d players)", h.name.c_str(), h.addr().c_str(), h.n, h.max);
        x0::save_store_want(x0::SaveStore::coop);
        join_continue(h.name, h.n, h.max, 40);
    }

    void start_join(const std::string& forced)
    {
        if (g_busy) return;
        g_busy = true;
        say(tx(nola_text::T::say_searching), 4);
        std::thread([forced] {
            Scan s = scan_now(forced);
            x0::post([s] { join_result(s); });
        }).detach();
    }

    s2r::SteamNote g_sjoin;
    bool g_sjoinOn = false;
    bool g_sfind = false;
    double g_sfindAt = 0;

    void steam_connect(const s2r::SteamNote& n)
    {
        g_sjoinOn = false;
        std::string why;
        if (!s2r::x0_session(2, "127.0.0.1", n.port, why)) { popup(tx(nola_text::T::popup_title_join), tx(nola_text::T::popup_fail) + " (" + why + ")"); return; }
        g_mode = nola::Mode::join;
        g_relay = true;
        g_joinAt = x0::now();
        g_peerSeen = false;
        s2r::Stats st;
        g_full0 = s2r::x0_stats(st) ? st.full_rx : 0;
        g_fullN = n.n;
        g_fullMax = n.max;
        relabel(2);
        x0::logf("NOLA menu: joining %s via Steam lobby %llu (%d/%d players)", n.name.c_str(), (unsigned long long)n.lobby, n.n, n.max);
        x0::save_store_want(x0::SaveStore::coop);
        join_continue(n.name, n.n, n.max, 40);
    }

    void steam_note(const s2r::SteamNote& n)
    {
        using K = s2r::SteamNote::Kind;
        std::string m = nola_steam::text(n, nola_text::cur(), x0::fi("steamdev", 0) == 1);
        x0::logf("NOLA NOLA steam: note %d %s lobby %llu%s%s", (int)n.kind, n.name.c_str(), (unsigned long long)n.lobby, m.empty() ? "" : ": ", m.c_str());
        if (n.kind == K::none || n.kind == K::unavailable)
        {
            bool f = g_sfind;
            g_sfind = false;
            if (f)
            {
                g_busy = false;
                start_join("");
            }
            return;
        }
        if (n.kind == K::join_fail)
        {
            if (g_sfind) g_busy = false;
            g_sfind = false;
            popup(tx(nola_text::T::popup_title_join), m);
            return;
        }
        if (n.kind == K::joined)
        {
            if (g_sfind) g_busy = false;
            g_sfind = false;
            if (g_mode == nola::Mode::join && s2r::x0_session_peer() == "127.0.0.1:" + std::to_string(n.port)) return;
            if (pawn() || !g_sv.valid())
            {
                g_sjoin = n;
                g_sjoinOn = true;
                if (pawn()) say(nola_steam::fill(tx(nola_text::T::steam_quit_to_join), n), 10);
                x0::logf("NOLA menu: Steam join to %s waits for the main menu", n.name.c_str());
                return;
            }
            steam_connect(n);
            return;
        }
        if (!m.empty()) say(m, 8);
    }

    bool steam_join_click()
    {
        if (!s2r::steam_find()) return false;
        g_busy = true;
        g_sfind = true;
        g_sfindAt = x0::now();
        say(tx(nola_text::T::steam_searching), 4);
        return true;
    }

    void steam_tick()
    {
        if (g_sfind && x0::now() - g_sfindAt > 20)
        {
            x0::log("NOLA menu: no Steam answer in 20 s: LAN search");
            g_sfind = false;
            g_busy = false;
            start_join("");
        }
        if (g_sjoinOn && !pawn() && g_sv.valid()) steam_connect(g_sjoin);
    }

    void on_click(int which)
    {
        int n = ++g_clicks;
        x0::logf("NOLA menu: menu entry %s clicked (#%d, mode %s)", which == 1 ? "host" : "join", n, nola::mode_name(g_mode));
        if (nola::click(g_mode, which).end_first)
        {
            end_session(which == 1 ? "Host co-op picked while joining" : "Join co-op picked while hosting");
            relabel(which == 1 ? 2 : 1);
        }
        if (which == 1)
        {
            if (g_mode == nola::Mode::host)
            {
                end_session("host entry clicked again");
                relabel(1);
                settle("host entry clicked again", [] { popup(tx(nola_text::T::popup_title_host), tx(nola_text::T::popup_host_off)); });
                return;
            }
            g_mode = nola::Mode::host;
            g_world = false;
            g_relay = false;
            if (x0::fi("menurole", 1) == 1)
                x0::post([] {
                    if (g_mode == nola::Mode::host) x0::set_session_role("host", "Host co-op picked in the main menu");
                });
            x0::save_store_want(x0::SaveStore::coop);
            relabel(1);
            settle("host entry", [] {
                if (g_mode == nola::Mode::host)
                    popup(tx(nola_text::T::popup_title_host), tx(x0::save_store_has_save() == 0 ? nola_text::T::popup_host_armed_new : nola_text::T::popup_host_armed));
            });
            return;
        }
        if (g_mode == nola::Mode::join)
        {
            end_session("join entry clicked again");
            relabel(2);
            settle("join entry clicked again", [] { popup(tx(nola_text::T::popup_title_join), tx(nola_text::T::popup_join_off)); });
            return;
        }
        if (!steam_join_click()) start_join("");
    }

    bool pawn() { return x0::first_of(L"BP_Stalker2Character_C").valid(); }

    void tick()
    {
        if (!g_det && !g_tried) return;
        if (g_title.valid() || g_titleOv.valid() || (g_sv.valid() && g_titleErrs < 5)) ensure_title();
        steam_tick();
        if (x0::ui_session_blocked())
        {
            if (!pawn()) g_noPawnSeen = true;
            else if (g_noPawnSeen)
            {
                g_noPawnSeen = false;
                x0::ui_session_begin();
            }
        }
        if (g_mode == nola::Mode::none) return;
        bool pw = pawn();
        if (pw && !g_world)
        {
            g_world = true;
            x0::logf("NOLA menu: world loaded in co-op %s mode", nola::mode_name(g_mode));
        }
        if (g_mode == nola::Mode::host && g_world && !g_relay)
        {
            std::string why;
            g_relay = s2r::x0_session(1, "", 0, why);
            say(g_relay ? tx(nola_text::T::say_host_open) : nola::fmt(tx(nola_text::T::say_fail), "", 0, 0, why), 8);
            if (!g_relay) end_session("host relay not started");
            return;
        }
        if (g_mode == nola::Mode::join && g_relay)
        {
            s2r::Stats st;
            if (s2r::x0_stats(st) && st.full_rx > g_full0)
            {
                say(nola::fmt(tx(nola_text::T::say_full), "host", g_fullN, g_fullMax), 10);
                x0::log("NOLA menu: the host answered FULL: co-op join ended");
                end_session("host full");
                return;
            }
            if (!g_peerSeen && x0::coop())
            {
                g_peerSeen = true;
                x0::logf("NOLA menu: host is up %.1f s after the join click", x0::now() - g_joinAt);
            }
            if (!g_peerSeen && g_world && x0::now() - g_joinAt > 60)
            {
                g_peerSeen = true;
                say(tx(nola_text::T::say_no_answer), 10);
                x0::log("NOLA menu: no data from the host 60 s after the join: still trying");
            }
        }
    }

    void world_exit(uint8_t from)
    {
        ++g_exits;
        x0::logf("NOLA menu: world exit #%d to the main menu (game flow request %d from state %d, mode %s, store %s): the save store switch starts now", g_exits, (int)kFlowMainMenu, (int)from,
                 nola::mode_name(g_mode), x0::save_store() == x0::SaveStore::coop ? "coop" : "normal");
        x0::save_store_world_exit();
        ui_end();
        if (g_mode != nola::Mode::none) end_session("world exit");
        else x0::save_store_want(x0::SaveStore::normal);
    }

    void flow_detour(int64_t gf, uint8_t req)
    {
        FlowFn orig = reinterpret_cast<FlowFn>(g_flowTramp);
        bool leave = false;
        uint8_t from = 0xff;
        try
        {
            leave = req == kFlowMainMenu && F_exit.on() && x0::on_game_thread() && x0::first_of(L"BP_Stalker2Character_C").valid();
            rd_seh(exe() + kFlowState, &from, 1);
        }
        catch (...)
        {
            leave = false;
        }
        orig(gf, req);
        if (!leave) return;
        try
        {
            world_exit(from);
        }
        catch (const std::exception& e)
        {
            x0::logf("NOLA menu: world exit handling failed (exception: %s)", e.what());
        }
        catch (...)
        {
            x0::log("NOLA menu: world exit handling failed (exception)");
        }
    }

    void install_flow()
    {
        if (g_flowDet || g_flowTried) return;
        g_flowTried = true;
        uint8_t* b = exe();
        if (!eq_seh(b + kFlow, kFlowSig, sizeof kFlowSig))
        {
            x0::log("NOLA menu: game flow request FUN_1435e46c6 bytes differ (new exe?): the save store switches back only at the main menu build");
            return;
        }
        g_flowDet = new x0::hot::Detour((uint64_t)(b + kFlow), (uint64_t)&flow_detour, &g_flowTramp);
        if (!g_flowDet->hook())
        {
            delete g_flowDet;
            g_flowDet = nullptr;
            g_flowTramp = 0;
            x0::log("NOLA menu: game flow request hook failed: the save store switches back only at the main menu build");
            return;
        }
        x0::logf("NOLA menu: world exit hook on (exe+0x346b32e, request %d, csexit=%lld)", (int)kFlowMainMenu, (long long)F_exit.i());
    }

    void install()
    {
        install_flow();
        if (g_det || g_tried) return;
        g_tried = true;
        uint8_t* b = exe();
        if (!eq_seh(b + kBuild, kBuildSig, sizeof kBuildSig) || !eq_seh(b + kWeakGet, kWeakSig, sizeof kWeakSig))
        {
            x0::log("NOLA menu: menu build FUN_1432fc50a / FUN_142560e08 bytes differ (new exe?): no NOLA entries in the main menu");
            return;
        }
        g_sigName = eq_seh(b + kSetName, kNameSig, sizeof kNameSig);
        g_sigRefresh = eq_seh(b + kRefresh, kRefreshSig, sizeof kRefreshSig);
        g_det = new x0::hot::Detour((uint64_t)(b + kBuild), (uint64_t)&build_detour, &g_tramp);
        if (!g_det->hook())
        {
            delete g_det;
            g_det = nullptr;
            g_tramp = 0;
            x0::log("NOLA menu: menu build hook failed: no NOLA entries in the main menu");
            return;
        }
        x0::logf("NOLA menu: main menu entry hook on (exe+0x33319a4, MainMenuSubView only, joinmenu=%lld, relabel=%s, store refresh=%s csmenu=%lld)", (long long)F_menu.i(), g_sigName ? "on" : "off",
                 g_sigRefresh ? "on" : "off", (long long)F_refresh.i());
    }

    std::string status_line()
    {
        return std::string("nola mode=") + nola::mode_name(g_mode) + " role=" + x0::role() + " boot=" + x0::boot_role() + " menurole=" + std::to_string(x0::fi("menurole", 1)) + " world=" + (g_world ? "1" : "0") + " relay=" + (g_relay ? "1" : "0") + " builds=" + std::to_string(g_builds) +
               " added=" + std::to_string(g_added) + " refreshes=" + std::to_string(g_refreshes) + " clicks=" + std::to_string(g_clicks.load()) + " title=" + (g_title.valid() ? "1" : "0") + " titlepos=" + (g_title.valid() ? (g_titleSrc == 2 ? "badge" : g_titleSrc == 1 ? "last" : "stock") : "-") + " peer=" + s2r::x0_session_peer();
    }

    std::string cmd(const std::vector<std::string>& a)
    {
        std::string v = a.size() > 1 ? a[1] : "status";
        if (v == "host") { on_click(1); return status_line(); }
        if (v == "join")
        {
            if (g_mode == nola::Mode::join) on_click(2);
            else
            {
                if (g_mode == nola::Mode::host) end_session("nola join command while hosting");
                start_join(a.size() > 2 ? a[2] : "");
            }
            return status_line();
        }
        if (v == "off") { end_session("nola off command"); return status_line(); }
        if (v == "scan")
        {
            std::thread([f = a.size() > 2 ? a[2] : std::string()] {
                Scan s = scan_now(f);
                std::string l;
                for (auto& h : s.hosts) l += " " + h.name + "@" + h.addr() + " " + std::to_string(h.n) + "/" + std::to_string(h.max) + " " + std::to_string((int)h.ms) + "ms";
                x0::logf("NOLA NOLA scan UDP %d: %d host(s)%s%s", s.port, (int)s.hosts.size(), l.c_str(), s.err.empty() ? "" : (" error: " + s.err).c_str());
            }).detach();
            return "scan started";
        }
        return status_line();
    }

    void menu_init()
    {
        x0::every(x0::Mod::session, "nolamenu1000", x0::Rate::t1000, [] {
            install();
            tick();
        });
        x0::cmd(x0::Mod::session, "nola", &cmd);
        s2r::steam_menu(&steam_note);
        x0::status(x0::Mod::session, [] { return status_line(); });
    }

    ::x0::ModReg g_menu_reg(::x0::Mod::session, "nolamenu", &menu_init);
}

namespace x0
{
    CoopMode coop_mode()
    {
        switch (g_mode.load())
        {
        case nola::Mode::host: return CoopMode::host;
        case nola::Mode::join: return CoopMode::join;
        default: return CoopMode::none;
        }
    }
}
