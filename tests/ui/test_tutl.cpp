#include "s2x_tut_logic.h"
#include "s2x_uiq.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace x0::in;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL %s\n", what.c_str()); }
}

static tutl::WState ws(bool vis, int active, int fin, int fout, float op)
{
    tutl::WState s;
    s.exists = true;
    s.visible = vis;
    s.active = (uint8_t)active;
    s.fade_in = (uint8_t)fin;
    s.fade_out = (uint8_t)fout;
    s.opacity = op;
    return s;
}

static bool old_busy(const tutl::WState& w) { return w.exists && (w.active || w.fade_in || w.fade_out || w.visible); }

static void busy_rules()
{
    tutl::WState none;
    for (uint32_t n = 18; n <= 21; ++n) check(!tutl::busy(n, none), "absent widget " + std::to_string(n) + " is free");

    tutl::WState hiddenActive = ws(false, 1, 0, 0, 0.f);
    check(old_busy(hiddenActive), "old rule: hidden stock-active widget 18 counted busy (P3T-2 client D)");
    check(!tutl::busy(18, hiddenActive), "hidden stock-active widget 18 is free");
    check(!tutl::busy(20, hiddenActive), "hidden stock-active widget 20 is free");
    check(!tutl::busy(21, hiddenActive), "hidden stock-active widget 21 is free");
    check(tutl::busy(19, hiddenActive), "stock-active widget 19 stays busy (stock owns our widget)");

    check(tutl::busy(18, ws(true, 1, 0, 0, 1.f)), "stock hint drawn on 18 is busy");
    check(tutl::busy(20, ws(true, 0, 0, 1, 0.5f)), "stock hint fading out on 20 is busy");
    check(tutl::busy(21, ws(true, 1, 1, 0, 0.f)), "stock hint starting its fade-in on 21 is busy");
    check(!tutl::busy(18, ws(true, 0, 0, 0, 0.f)), "visible slate at opacity 0 with no fade is free");
    check(!tutl::busy(18, ws(false, 0, 1, 1, 1.f)), "fade flags on a hidden widget are free");
    check(tutl::busy(19, ws(true, 0, 0, 1, 0.4f)), "our previous box fading out on 19 is busy");
    check(!tutl::busy(19, ws(true, 0, 0, 0, 0.f)), "idle widget 19 is free");
    check(!tutl::busy(19, ws(false, 0, 0, 0, 1.f)), "hidden idle widget 19 is free");

    std::string d = tutl::describe(18, hiddenActive);
    check(d == "widget 18 (active=1 visible=0 drawn=0)", "describe: " + d);
}

static void waiter_rules()
{
    tutl::Waiter w;
    check(w.step(0, 5, 2, 2) == tutl::Next::Box, "free box shows at once");
    check(w.step(2, 5, 2, 2) == tutl::Next::Text, "native path off -> text at once");
    check(w.step(1, 10.0, 2, 2) == tutl::Next::Wait, "busy at t=0 waits");
    check(w.step(1, 11.9, 2, 2) == tutl::Next::Wait, "busy at 1.9 s waits");
    check(w.step(1, 12.0, 2, 2) == tutl::Next::Text, "busy at 2.0 s falls back to text");
    check(!w.waiting, "fallback ends the wait");
    check(w.step(3, 20.0, 2, 2) == tutl::Next::Wait, "widget loading waits");
    check(w.step(3, 22.1, 2, 2) == tutl::Next::Text, "widget loading over 2 s falls back");
    check(w.step(1, 30.0, 2, 2) == tutl::Next::Wait, "new busy wait");
    check(w.step(0, 30.5, 2, 2) == tutl::Next::Box, "box frees after a fade -> box");
    check(!w.waiting, "box ends the wait");
    check(w.step(1, 0.0, 2, 2) == tutl::Next::Wait, "wait can start at time 0");
    check(w.step(1, 2.0, 2, 2) == tutl::Next::Text, "wait from time 0 ends at 2 s");
    w.step(1, 40.0, 2, 2);
    w.reset();
    check(w.step(1, 41.9, 2, 2) == tutl::Next::Wait, "reset restarts the clock");
}

struct Sim
{
    tutl::Waiter wt;
    std::string last;
    std::vector<std::string> logs;
    double t = 0, busyMax = 2;
    int boxBusyUntil = 0;
    bool forever = false, box = false;
    std::map<int, std::string> texts;
    int next = 0, boxCreates = 0, textCreates = 0;
    static constexpr int kBox = 1 << 30;

    int show(const std::string&, std::string& why)
    {
        if (forever || t < boxBusyUntil)
        {
            why = tutl::describe(18, ws(true, 1, 0, 0, 1.f));
            return 1;
        }
        return 0;
    }

    uiq::Backend be()
    {
        return uiq::Backend{
            [this](const std::string& s) {
                int h = tutl::create(
                    wt, last, s, t, busyMax, kBox, [this](const std::string& x, std::string& why) { return show(x, why); },
                    [this](const std::string& x) { ++textCreates; texts[++next] = x; return next; }, [this](const std::string& m) { logs.push_back(m); });
                if (h == kBox) { ++boxCreates; box = true; }
                return h;
            },
            [this](int h, const std::string& s) { if (h == kBox) return box; auto it = texts.find(h); if (it == texts.end()) return false; it->second = s; return true; },
            [this](int h) { if (h == kBox) box = false; else texts.erase(h); },
            [this](int h) { return h == kBox ? box : texts.count(h) > 0; },
        };
    }

    bool visible() const { return box || !texts.empty(); }
};

static double run_until_visible(Sim& s, uiq::Board& b, double life, double step = 0.1)
{
    b.show("s2m_session_peer_left_2", "Stalker 2 left the Zone!", life, s.t, true, 4);
    double start = s.t;
    while (s.t - start < life + 1)
    {
        if (s.visible()) return s.t - start;
        s.t += step;
        b.tick(s.t, true, 4);
        if (b.lines() == 0) s.wt.reset();
    }
    return -1;
}

static void board_flow()
{
    {
        Sim s;
        s.forever = true;
        s.busyMax = 20;
        uiq::Board b(s.be());
        double at = run_until_visible(s, b, 10);
        check(at < 0, "old 20 s wait: a 10 s line behind a busy box never shows (P3T-2 cause)");
    }
    {
        Sim s;
        s.forever = true;
        uiq::Board b(s.be());
        double at = run_until_visible(s, b, 10);
        check(at > 1.9 && at < 2.25, "box busy for good: 10 s line shows as text after ~2 s (at " + std::to_string(at) + ")");
        check(s.textCreates == 1 && s.boxCreates == 0, "one text widget, no box");
        check(!s.logs.empty() && s.logs.back().rfind("text fallback after the wait: widget 18", 0) == 0, "fallback logged: " + (s.logs.empty() ? std::string() : s.logs.back()));
        s.t += 9;
        b.tick(s.t, true, 4);
        if (b.lines() == 0) s.wt.reset();
        check(!s.visible() && b.lines() == 0, "line expires and the text widget goes");
        double at2 = run_until_visible(s, b, 6);
        check(at2 > 1.9 && at2 < 2.25, "next 6 s line also shows after ~2 s (at " + std::to_string(at2) + ")");
    }
    {
        Sim s;
        uiq::Board b(s.be());
        double at = run_until_visible(s, b, 10);
        check(at == 0 && s.boxCreates == 1 && s.textCreates == 0, "free box: line in the box at once");
        check(s.logs.empty(), "no log for an immediate show");
    }
    {
        Sim s;
        s.boxBusyUntil = 1;
        uiq::Board b(s.be());
        double at = run_until_visible(s, b, 10);
        check(at > 0.9 && at < 1.15 && s.boxCreates == 1 && s.textCreates == 0, "stock hint fading out for 1 s: line goes into the box after it (at " + std::to_string(at) + ")");
        check(!s.logs.empty() && s.logs.back() == "shown in the box", "shown after a wait is logged");
    }
    {
        Sim s;
        s.forever = true;
        uiq::Board b(s.be());
        run_until_visible(s, b, 10);
        b.show("s2m_session_peer_up_3", "Player 3 is back!", 6, s.t, true, 4);
        check(s.texts.size() == 1 && s.texts.begin()->second.find("Player 3 is back!") != std::string::npos, "a second line joins the shown text widget");
    }
}

int main()
{
    busy_rules();
    waiter_rules();
    board_flow();
    std::printf("tutl: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
