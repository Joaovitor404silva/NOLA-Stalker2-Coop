#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#define RC_UNREAL_DISABLE_PROPERTY_DEPRECATION_WARNINGS
#endif
#include <Windows.h>
#include "s2x_int.h"
#include "s2x_tut_logic.h"
#include "s2x_uiq.h"
#include <cstdlib>
#include <string>
#include <unordered_map>

#include <Unreal/FText.hpp>
#include <Unreal/NameTypes.hpp>
#include <Unreal/UClass.hpp>
#include <Unreal/UObject.hpp>
#include <Unreal/UObjectGlobals.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace x0::in
{
    namespace
    {
        struct Msg
        {
            Ref w, tb, world;
            std::string txt;
        };
        std::unordered_map<int, Msg>& widgets()
        {
            static std::unordered_map<int, Msg> m;
            return m;
        }
        int g_uiErr = 0;
        int g_nextH = 0;

        void ui_log(const std::string& s)
        {
            if (++g_uiErr <= 20) log("NOLA x0 screen_text: " + s);
        }

        Ref local_pc()
        {
            for (const Ref& r : all_of(L"PlayerController"))
                if (call_b(r, L"IsLocalPlayerController", false)) return r;
            return Ref{};
        }

        bool set_obj(UObject* self, const wchar_t* name, UObject* v)
        {
            if (!self) return false;
            auto** p = self->GetValuePtrByPropertyNameInChain<UObject*>(name);
            if (!p) return false;
            *p = v;
            return true;
        }

        bool set_text(const Ref& tb, const std::string& txt)
        {
            if (!tb) return false;
            int sz = FText::StaticSize();
            if (sz <= 0 || sz > 0x30) return false;
            FText ft(u2w(txt).c_str());
            alignas(8) uint8_t tmp[0x30]{};
            ft.CopyBorrowedTo(tmp);
            Call c(tb, L"SetText");
            c.raw(L"InText", tmp, sz);
            return c.run();
        }

        UObject* make(UClass* cls, UObject* outer)
        {
            if (!cls || !outer) return nullptr;
            FStaticConstructObjectParameters P{cls, outer};
            return UObjectGlobals::StaticConstructObject(P);
        }

        bool create(Msg& m, const std::string& txt, std::string& why)
        {
            Ref pc = local_pc();
            if (!pc) { why = "sem PlayerController"; return false; }
            auto* cU = static_cast<UClass*>(find_path(L"/Script/UMG.UserWidget").get());
            auto* cT = static_cast<UClass*>(find_path(L"/Script/UMG.WidgetTree").get());
            auto* cB = static_cast<UClass*>(find_path(L"/Script/UMG.TextBlock").get());
            if (!cU || !cT || !cB) { why = "classes UMG ausentes"; return false; }
            UObject* w = make(cU, pc.get());
            if (!w) { why = "UserWidget nao criado"; return false; }
            UObject* tr = make(cT, w);
            if (!tr) { why = "WidgetTree nao criado"; return false; }
            UObject* tb = make(cB, tr);
            if (!tb) { why = "TextBlock nao criado"; return false; }
            if (!set_obj(w, L"WidgetTree", tr) || !set_obj(tr, L"RootWidget", tb)) { why = "WidgetTree/RootWidget sem propriedade"; return false; }
            Ref rw = Ref::of(w), rtb = Ref::of(tb);
            if (!set_text(rtb, txt)) { why = "SetText falhou"; return false; }
            Call(rtb, L"SetJustification").i(L"InJustification", 1).run();
            double so[2] = {2, 2};
            Call(rtb, L"SetShadowOffset").raw(L"InShadowOffset", so, (int)sizeof so).run();
            float sc[4] = {0, 0, 0, 1};
            Call(rtb, L"SetShadowColorAndOpacity").raw(L"InShadowColorAndOpacity", sc, (int)sizeof sc).run();
            double rt[2] = {0, 160};
            Call(rtb, L"SetRenderTranslation").raw(L"Translation", rt, (int)sizeof rt).run();
            if (!Call(rw, L"SetVisibility").i(L"InVisibility", 3).run()) { why = "SetVisibility falhou"; return false; }
            if (!Call(rw, L"AddToViewport").i(L"ZOrder", 1000).run()) { why = "AddToViewport falhou"; return false; }
            m.w = rw;
            m.tb = rtb;
            m.world = world();
            m.txt = txt;
            return true;
        }

        bool same_world(const Msg& m)
        {
            Ref cur = world();
            return m.world && cur && m.world.same(cur);
        }

        int w_create(const std::string& txt)
        {
            Msg m;
            std::string why;
            if (!create(m, txt, why)) { ui_log(why); return 0; }
            int h = ++g_nextH;
            widgets()[h] = m;
            return h;
        }

        bool w_set(int h, const std::string& txt)
        {
            auto it = widgets().find(h);
            if (it == widgets().end()) return false;
            if (!set_text(it->second.tb, txt)) { ui_log("SetText falhou"); return false; }
            it->second.txt = txt;
            return true;
        }

        void w_remove(int h)
        {
            auto it = widgets().find(h);
            if (it == widgets().end()) return;
            Msg m = it->second;
            widgets().erase(it);
            if (m.w && same_world(m)) call0(m.w, L"RemoveFromParent");
        }

        bool w_alive(int h)
        {
            auto it = widgets().find(h);
            return it != widgets().end() && it->second.w && it->second.tb && same_world(it->second);
        }

        bool queue_on();
        constexpr int kTutH = 1 << 30;
        tutl::Waiter g_wait;
        std::string g_tutWhy;

        bool tut_on() { return fi("uitut", 1) == 1; }

        int b_create(const std::string& txt)
        {
            if (!tut_on() || !queue_on()) return w_create(txt);
            return tutl::create(
                g_wait, g_tutWhy, txt, now(), (double)fi("uitutwait", 2), kTutH, [](const std::string& t, std::string& why) { return tut::show(t, "NOLA", why); }, [](const std::string& t) { return w_create(t); },
                [](const std::string& s) { log("NOLA x0 uitut: " + s); });
        }

        bool b_set(int h, const std::string& txt) { return h == kTutH ? tut::set(txt) : w_set(h, txt); }

        void b_remove(int h)
        {
            if (h == kTutH) tut::release();
            else w_remove(h);
        }

        bool b_alive(int h) { return h == kTutH ? (tut_on() && tut::mine()) : w_alive(h); }

        uiq::Board& board()
        {
            static uiq::Board b = [] {
                uiq::Board x(uiq::Backend{b_create, b_set, b_remove, b_alive});
                x.log = [](const std::string& s) { log("NOLA x0 ui: " + s); };
                return x;
            }();
            return b;
        }

        bool queue_on() { return fi("uiq", 1) == 1; }
        bool g_sessBlock = false;

        void sync_life() { board().max_life = fd("uimaxlife", 120); }

        bool session_id(std::string_view id) { return id.substr(0, 12) == "s2m_session_"; }
        int queue_max() { return (int)fi("uiqmax", 4); }

        void show(const std::string& id, const std::string& txt, double secs)
        {
            sync_life();
            board().show(id, txt, secs, now(), queue_on(), queue_max());
            double life = secs > 0 ? secs : board().max_life;
            if (life > 0) after(Mod::x0, life, [] { board().tick(now(), queue_on(), queue_max()); });
        }

        void clear(const std::string& id) { board().clear(id, now(), queue_on(), queue_max()); }

        void clear_prefix(std::string_view p) { board().clear_prefix(p, now(), queue_on(), queue_max()); }

        std::string ui_status()
        {
            int shown = 0, waiting = 0;
            std::string t = board().compose(now(), queue_max(), &shown, &waiting);
            for (auto& c : t)
                if (c == '\n') c = '|';
            return tut::status() + " uitut=" + std::to_string(tut_on() ? 1 : 0) + " | ui uiq=" + std::to_string(queue_on() ? 1 : 0) + " active=" + std::to_string(board().lines()) + " shown=" + std::to_string(shown) + " waiting=" +
                   std::to_string(waiting) + " widgets=" + std::to_string(widgets().size()) + " board=[" + t + "]";
        }

        std::string ui_cmd(const std::vector<std::string>& a)
        {
            if (a.size() >= 4 && a[1] == "show")
            {
                std::string t;
                for (size_t k = 4; k < a.size(); ++k) t += (t.empty() ? "" : " ") + a[k];
                show(a[2], t, std::atof(a[3].c_str()));
            }
            else if (a.size() >= 3 && a[1] == "clear") clear(a[2]);
            return ui_status();
        }

        void ui_tick()
        {
            tut::tick();
            sync_life();
            board().tick(now(), queue_on(), queue_max());
            if (board().lines() == 0) g_wait.reset();
        }

        void ui_init()
        {
            cmd(Mod::x0, "ui", &ui_cmd);
            every(Mod::x0, "uitut", Rate::t100, [] { ui_tick(); });
            on_flag(Mod::x0, "uitut", [](const std::string&, const Val&, const Val&) { board().tick(now(), queue_on(), queue_max()); });
            status(Mod::x0, [] { return ui_status(); });
            for (const char* k : {"uiq", "uiqmax"})
                on_flag(Mod::x0, k, [](const std::string&, const Val&, const Val&) { board().tick(now(), queue_on(), queue_max()); });
        }
        ModReg g_uiReg(Mod::x0, "ui", &ui_init);
    }
}

namespace x0
{
    void screen_text(std::string_view id, std::string_view text)
    {
        std::string i(id), t(text);
        if (!on_game_thread()) { post([i, t] { screen_text(i, t); }); return; }
        try { in::show(i, t, 0); }
        catch (...) { in::ui_log(i + ": exception"); }
    }

    bool text_keep(const Ref& tb, TextKeep& out)
    {
        int sz = FText::StaticSize();
        if (!tb || sz <= 0 || sz > (int)sizeof out.b) return false;
        Call c(tb, L"GetText");
        if (!c.run() || !c.rraw(out.b, sz)) return false;
        out.n = sz;
        return true;
    }

    bool text_put(const Ref& tb, const TextKeep& k)
    {
        if (!tb || k.n <= 0 || k.n != FText::StaticSize()) return false;
        Call c(tb, L"SetText");
        c.raw(L"InText", k.b, k.n);
        return c.run();
    }

    bool text_set(const Ref& tb, std::string_view text) { return in::set_text(tb, std::string(text)); }

    void screen_text_clear(std::string_view id)
    {
        std::string i(id);
        if (!on_game_thread()) { post([i] { screen_text_clear(i); }); return; }
        try { in::clear(i); }
        catch (...) { in::ui_log(i + ": exception"); }
    }

    void ui_msg(std::string_view id, std::string_view text, double secs)
    {
        if (fi("uimsg", 1) != 1) return;
        if (in::g_sessBlock && in::session_id(id)) return;
        std::string i(id), t(text);
        if (!on_game_thread()) { post([i, t, secs] { ui_msg(i, t, secs); }); return; }
        try { in::show(i, t, secs); }
        catch (...) { in::ui_log(i + ": exception"); }
    }

    void ui_session_end()
    {
        in::g_sessBlock = true;
        if (!on_game_thread()) { post([] { ui_session_end(); }); return; }
        try
        {
            in::clear_prefix("s2m_session_");
            in::clear_prefix("s2m_save_");
        }
        catch (...) { in::ui_log("session end: exception"); }
    }

    void ui_session_begin() { in::g_sessBlock = false; }

    bool ui_session_blocked() { return in::g_sessBlock; }
}
