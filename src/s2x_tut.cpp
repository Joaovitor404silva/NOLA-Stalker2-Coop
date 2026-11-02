#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#include <Windows.h>
#include "s2x_int.h"
#include "s2x_tut_logic.h"
#include <cstring>
#include <string>

#include <Unreal/FText.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace x0::in::tut
{
    namespace
    {
        constexpr uint32_t kWidget = 19;
        constexpr uint32_t kFamilyLo = 18, kFamilyHi = 21;
        constexpr size_t kOffFadeIn = 0x460, kOffFadeOut = 0x461, kOffFade = 0x464, kOffActive = 0x468;
        constexpr size_t kVtShow = 0x330, kVtPhoto = 0x338;

        using FnGet = void* (*)(void*, uint32_t);
        using FnVis = bool (*)(void*, uint32_t);
        using FnReq = void (*)(void*, uint32_t, void*);
        using FnHide = void (*)(void*, uint32_t, uint8_t);
        using FnShow = void* (*)(void*, uint32_t);
        using FnPhoto = bool (*)(void*, uint32_t);

        struct Native
        {
            bool tried = false, ok = false;
            std::string why;
            FnGet get = nullptr;
            FnVis vis = nullptr;
            FnReq req = nullptr;
            FnHide hide = nullptr;
        } N;

        struct State
        {
            Ref w, world;
            bool ours = false, hiding = false;
            bool reqd = false;
            double reqAt = 0;
            int shown = 0;
        } S;

        struct Sig
        {
            const char* name;
            const char* pat;
            uint32_t rva;
        };
        constexpr Sig kSigs[] = {
            {"GetWidget", "56 57 48 83 EC 28 89 D6 48 89 CF 48 8B 01 FF 90 38 03 00 00 84 C0 75 76 8B 87 78 03 00 00 3B 87 A4 03 00 00", 0x42e250},
            {"IsWidgetVisible", "56 57 53 48 83 EC 20 8B 81 78 03 00 00 3B 81 A4 03 00 00 74 58 48 8D 81 A8 03 00 00 4C 8B 81 B0 03 00 00", 0x42b2ba},
            {"RequestView", "56 57 53 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 48 31 E0 48 89 44 24 48 8B 81 28 04 00 00 3B 81 54 04 00 00", 0x9b351a},
            {"HideWidget", "41 57 41 56 41 55 41 54 56 57 55 53 48 83 EC 78 44 88 44 24 27 89 D7 48 89 CB 48 8B 05 ?? ?? ?? ?? 48 31 E0", 0xc07da0},
        };

        uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

        bool text_range(uint8_t*& lo, size_t& n)
        {
            uint8_t* base = exe();
            auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            auto* s = IMAGE_FIRST_SECTION(nt);
            for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
                if (std::memcmp(s->Name, ".text", 5) == 0)
                {
                    lo = base + s->VirtualAddress;
                    n = s->Misc.VirtualSize;
                    return true;
                }
            return false;
        }

        struct Pat
        {
            uint8_t b[64];
            bool m[64];
            size_t n = 0;
        };

        Pat parse(const char* s)
        {
            Pat p;
            while (*s && p.n < 64)
            {
                while (*s == ' ') ++s;
                if (!*s) break;
                if (s[0] == '?')
                {
                    p.m[p.n] = false;
                    p.b[p.n++] = 0;
                    s += 2;
                }
                else
                {
                    p.m[p.n] = true;
                    p.b[p.n++] = (uint8_t)std::strtoul(std::string(s, 2).c_str(), nullptr, 16);
                    s += 2;
                }
            }
            return p;
        }

        bool match_at(const uint8_t* p, const Pat& pt)
        {
            for (size_t i = 0; i < pt.n; ++i)
                if (pt.m[i] && p[i] != pt.b[i]) return false;
            return true;
        }

        uint8_t* resolve_one(const Sig& g, std::string& via)
        {
            Pat pt = parse(g.pat);
            uint8_t* lo = nullptr;
            size_t n = 0;
            if (!text_range(lo, n)) return nullptr;
            uint8_t* at = exe() + g.rva;
            if (at >= lo && at + pt.n <= lo + n && match_at(at, pt)) { via += std::string(" ") + g.name + "=rva"; return at; }
            uint8_t* hit = nullptr;
            int cnt = 0;
            for (size_t i = 0; i + pt.n <= n && cnt < 2; ++i)
                if (lo[i] == pt.b[0] && match_at(lo + i, pt)) { hit = lo + i; ++cnt; }
            if (cnt != 1) return nullptr;
            via += std::string(" ") + g.name + "=aob";
            return hit;
        }

        bool resolve()
        {
            if (N.tried) return N.ok;
            N.tried = true;
            std::string via;
            uint8_t* f[4] = {};
            for (int i = 0; i < 4; ++i)
            {
                f[i] = resolve_one(kSigs[i], via);
                if (!f[i]) { N.why = std::string(kSigs[i].name) + " not found"; logf("NOLA x0 uitut: native path off: %s", N.why.c_str()); return false; }
            }
            N.get = reinterpret_cast<FnGet>(f[0]);
            N.vis = reinterpret_cast<FnVis>(f[1]);
            N.req = reinterpret_cast<FnReq>(f[2]);
            N.hide = reinterpret_cast<FnHide>(f[3]);
            N.ok = true;
            logf("NOLA x0 uitut: native path ready (%s )", via.c_str());
            return true;
        }

        bool rd(const void* p, void* out, size_t n)
        {
            __try { std::memcpy(out, p, n); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool wr(void* p, const void* in, size_t n)
        {
            __try { std::memcpy(p, in, n); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void* c_get(void* mgr, uint32_t n)
        {
            __try { return N.get(mgr, n); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
        }

        bool c_vis(void* mgr, uint32_t n)
        {
            __try { return N.vis(mgr, n); }
            __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
        }

        bool c_req(void* mgr, uint32_t n)
        {
            alignas(8) uint8_t zero[16] = {};
            __try { N.req(mgr, n, zero); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool c_hide(void* mgr, uint32_t n)
        {
            __try { N.hide(mgr, n, 0); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool vt_slot(void* mgr, size_t off, void*& fn)
        {
            void** vt = nullptr;
            if (!rd(mgr, &vt, 8) || !vt) return false;
            if (!rd(reinterpret_cast<uint8_t*>(vt) + off, &fn, 8) || !fn) return false;
            uint8_t* lo = nullptr;
            size_t n = 0;
            if (!text_range(lo, n)) return false;
            auto* p = reinterpret_cast<uint8_t*>(fn);
            return p >= lo && p < lo + n;
        }

        bool c_photo(void* mgr, bool& photo)
        {
            void* fn = nullptr;
            if (!vt_slot(mgr, kVtPhoto, fn)) return false;
            __try { photo = reinterpret_cast<FnPhoto>(fn)(mgr, kWidget); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool c_show(void* mgr, void*& w)
        {
            void* fn = nullptr;
            if (!vt_slot(mgr, kVtShow, fn)) return false;
            __try { w = reinterpret_cast<FnShow>(fn)(mgr, kWidget); return true; }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void* mgr_ptr()
        {
            Ref m = first_of(L"UIManagerEx");
            return m ? static_cast<void*>(m.get()) : nullptr;
        }

        uint8_t rd8(void* w, size_t off)
        {
            uint8_t v = 0xFF;
            rd(reinterpret_cast<uint8_t*>(w) + off, &v, 1);
            return v;
        }

        void wr8(void* w, size_t off, uint8_t v) { wr(reinterpret_cast<uint8_t*>(w) + off, &v, 1); }

        void wrf(void* w, size_t off, float v) { wr(reinterpret_cast<uint8_t*>(w) + off, &v, 4); }

        UObject* prop(UObject* w, const wchar_t* name);

        float opacity(UObject* o)
        {
            if (!o) return 1.f;
            auto* p = o->GetValuePtrByPropertyNameInChain<float>(L"RenderOpacity");
            float v = 1.f;
            if (!p || !rd(p, &v, 4)) return 1.f;
            return v;
        }

        tutl::WState read_state(void* mgr, uint32_t n, void* w)
        {
            tutl::WState s;
            if (!w) return s;
            s.exists = true;
            s.active = rd8(w, kOffActive);
            s.fade_in = rd8(w, kOffFadeIn);
            s.fade_out = rd8(w, kOffFadeOut);
            s.visible = c_vis(mgr, n);
            s.opacity = opacity(prop(static_cast<UObject*>(w), L"TutorialOverlay"));
            return s;
        }

        bool family_busy(void* mgr, std::string& why)
        {
            for (uint32_t n = kFamilyLo; n <= kFamilyHi; ++n)
            {
                void* w = c_get(mgr, n);
                if (!w) continue;
                tutl::WState s = read_state(mgr, n, w);
                if (tutl::busy(n, s)) { why = tutl::describe(n, s); return true; }
            }
            return false;
        }

        std::string clean(const std::string& s)
        {
            std::string o = s;
            for (char& c : o)
                if (c == '<') c = '(';
                else if (c == '>') c = ')';
            return o;
        }

        bool set_rich(UObject* tb, const std::string& txt)
        {
            if (!tb) return false;
            int sz = FText::StaticSize();
            if (sz <= 0 || sz > 0x30) return false;
            FText ft(u2w(txt).c_str());
            alignas(8) uint8_t tmp[0x30]{};
            ft.CopyBorrowedTo(tmp);
            Ref r = Ref::of(tb);
            Call c(r, L"SetText");
            c.raw(L"InText", tmp, sz);
            return c.run();
        }

        UObject* prop(UObject* w, const wchar_t* name)
        {
            if (!w) return nullptr;
            auto** p = w->GetValuePtrByPropertyNameInChain<UObject*>(name);
            return p ? *p : nullptr;
        }

        void vis(UObject* o, int v)
        {
            if (!o) return;
            Call(Ref::of(o), L"SetVisibility").i(L"InVisibility", v).run();
        }

        void* current(void* mgr)
        {
            if (!S.ours || !S.w) return nullptr;
            void* w = c_get(mgr, kWidget);
            return w && w == S.w.get() ? w : nullptr;
        }

        void drop() { S = State{}; }
    }

    bool available() { return resolve(); }

    bool mine()
    {
        if (!S.ours) return false;
        void* mgr = mgr_ptr();
        if (!mgr) { drop(); return false; }
        void* w = current(mgr);
        if (!w || rd8(w, kOffActive) != 0) { drop(); return false; }
        Ref cur = world();
        if (!S.world || !cur || !S.world.same(cur)) return false;
        return true;
    }

    int show(const std::string& body, const std::string& title, std::string& why)
    {
        if (!resolve()) { why = N.why; return 2; }
        void* mgr = mgr_ptr();
        if (!mgr) { why = "no UIManagerEx"; return 2; }
        if (S.ours)
        {
            why = S.hiding ? "fading out" : "already shown";
            return 1;
        }
        bool photo = false;
        if (!c_photo(mgr, photo)) { why = "manager vtable slots not in code"; return 2; }
        if (photo) { why = "photo mode"; return 1; }
        if (family_busy(mgr, why)) return 1;
        void* w = c_get(mgr, kWidget);
        if (!w)
        {
            double t = now();
            if (!S.reqd)
            {
                S.reqd = true;
                S.reqAt = t;
                if (!c_req(mgr, kWidget)) { why = "RequestView raised"; S.reqd = false; return 2; }
                why = "widget requested";
                return 3;
            }
            if (t - S.reqAt > 5) { why = "widget never created"; S.reqd = false; return 2; }
            why = "widget loading";
            return 3;
        }
        S.reqd = false;
        Ref wr_ = Ref::of(static_cast<UObject*>(w));
        if (!wr_ || !is_a(wr_, L"TutorialReference")) { why = "widget 19 is not a TutorialReference"; return 2; }
        UObject* wo = static_cast<UObject*>(w);
        UObject* text = prop(wo, L"Text");
        UObject* ttl = prop(wo, L"TitleTutorText");
        if (!text) { why = "Text property missing"; return 2; }
        bool hasTitle = !title.empty() && ttl;
        if (hasTitle) set_rich(ttl, "<Title>" + clean(title) + "</>");
        vis(prop(wo, L"TitleBox"), hasTitle ? 3 : 1);
        vis(prop(wo, L"TitleLineBox"), hasTitle ? 3 : 1);
        vis(prop(wo, L"LineWithIconBox"), hasTitle ? 1 : 3);
        if (!set_rich(text, clean(body))) { why = "SetText failed"; return 2; }
        Call(Ref::of(text), L"ForceLayoutPrepass").run();
        void* shown = nullptr;
        if (!c_show(mgr, shown)) { why = "show slot failed"; return 2; }
        if (!shown) { why = "show refused"; return 1; }
        wrf(w, kOffFade, 0.f);
        wr8(w, kOffFadeOut, 0);
        wr8(w, kOffFadeIn, 1);
        S.w = wr_;
        S.world = world();
        S.ours = true;
        S.hiding = false;
        ++S.shown;
        return 0;
    }

    bool set(const std::string& body)
    {
        if (!mine() || S.hiding) return false;
        UObject* wo = S.w.get();
        UObject* text = prop(wo, L"Text");
        bool ok = set_rich(text, clean(body));
        if (ok && text) Call(Ref::of(text), L"ForceLayoutPrepass").run();
        return ok;
    }

    void release()
    {
        if (!S.ours) return;
        void* mgr = mgr_ptr();
        void* w = mgr ? current(mgr) : nullptr;
        if (!w || rd8(w, kOffActive) != 0) { drop(); return; }
        Ref cur = world();
        if (!S.world || !cur || !S.world.same(cur))
        {
            wr8(w, kOffFadeIn, 0);
            wr8(w, kOffFadeOut, 0);
            wrf(w, kOffFade, 0.f);
            c_hide(mgr, kWidget);
            drop();
            return;
        }
        S.hiding = true;
    }

    void tick()
    {
        if (!S.ours || !S.hiding) return;
        void* mgr = mgr_ptr();
        void* w = mgr ? current(mgr) : nullptr;
        if (!w || rd8(w, kOffActive) != 0) { drop(); return; }
        if (rd8(w, kOffFadeIn) == 0)
        {
            wr8(w, kOffFadeOut, 1);
            drop();
        }
    }

    std::string status()
    {
        std::string s = "tut ";
        s += N.tried ? (N.ok ? "native=ok" : "native=off(" + N.why + ")") : "native=untried";
        s += " ours=" + std::to_string(S.ours ? 1 : 0) + " hiding=" + std::to_string(S.hiding ? 1 : 0) + " shown=" + std::to_string(S.shown);
        return s;
    }
}
