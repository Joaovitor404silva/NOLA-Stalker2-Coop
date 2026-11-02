#include "s2x_uiq.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <tuple>

using namespace x0::in::uiq;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL %s\n", what.c_str()); }
}

struct Slot
{
    double ax0 = 0, ay0 = 0, ax1 = 1, ay1 = 1, ox = 0, oy = 0, tx = 0, ty = 160;
    int z = 1000;
    bool operator<(const Slot& o) const
    {
        return std::tie(ax0, ay0, ax1, ay1, ox, oy, tx, ty, z) < std::tie(o.ax0, o.ay0, o.ax1, o.ay1, o.ox, o.oy, o.tx, o.ty, o.z);
    }
};

struct Fake
{
    struct W
    {
        std::string text;
        Slot slot;
        int world = 0;
    };
    std::map<int, W> live;
    int next = 0, world = 1, creates = 0, sets = 0, removes = 0;
    Backend be()
    {
        return Backend{
            [this](const std::string& t) { ++creates; live[++next] = W{t, Slot{}, world}; return next; },
            [this](int h, const std::string& t) { auto it = live.find(h); if (it == live.end()) return false; ++sets; it->second.text = t; return true; },
            [this](int h) { ++removes; live.erase(h); },
            [this](int h) { auto it = live.find(h); return it != live.end() && it->second.world == world; },
        };
    }
    int visible() const
    {
        int n = 0;
        for (auto& [h, w] : live) n += w.world == world && !w.text.empty();
        return n;
    }
    int overlaps() const
    {
        std::map<Slot, int> at;
        for (auto& [h, w] : live)
            if (w.world == world && !w.text.empty()) ++at[w.slot];
        int worst = 0;
        for (auto& [s, n] : at) worst = std::max(worst, n);
        return worst;
    }
    std::string only_text() const
    {
        for (auto& [h, w] : live)
            if (w.world == world) return w.text;
        return "";
    }
};

static void t_repro_old()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_save_join", "Following the signal into the Zone (3 s)", 0, 0.0, false, 4);
    b.show("s2m_session_ver", "Your radios are on different frequencies: install the same NOLA version on every PC!", 20, 0.5, false, 4);
    check(f.visible() == 2, "old: two ids give two live widgets");
    check(f.overlaps() == 2, "old: both widgets share the AddToViewport default slot and translation (overlap reproduced)");
    b.show("s2m_session_peer", "The other player's signal is lost", 0, 1.0, false, 4);
    check(f.overlaps() == 3, "old: a third module stacks a third widget on the same spot");
}

static void t_stack_new()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_save_join", "Following the signal into the Zone (3 s)", 0, 0.0, true, 4);
    b.show("s2m_session_ver", "Co-op warning: builds differ", 20, 0.5, true, 4);
    b.show("s2m_session_peer", "The other player's signal is lost", 0, 1.0, true, 4);
    check(f.visible() == 1 && f.overlaps() == 1, "new: one board widget only");
    check(f.creates == 1, "new: the board widget is created once");
    check(f.only_text() == "Co-op warning: builds differ\nFollowing the signal into the Zone (3 s)\nThe other player's signal is lost", "new: lines ordered by priority");
}

static void t_in_place()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer", "The other player's signal is lost", 0, 0.0, true, 4);
    for (int n = 30; n >= 27; --n) b.show("s2m_save_join", "Following the signal into the Zone (" + std::to_string(n) + " s)", 0, 30.0 - n, true, 4);
    check(f.creates == 1 && f.visible() == 1, "in place: countdown never creates a second widget");
    check(f.only_text() == "Following the signal into the Zone (27 s)\nThe other player's signal is lost", "in place: countdown keeps its own line");
    check(b.lines() == 2, "in place: two active lines");
    int s0 = f.sets;
    b.show("s2m_save_join", "Following the signal into the Zone (27 s)", 0, 4.0, true, 4);
    check(f.sets == s0, "in place: same text sends no SetText");
}

static void t_dedup()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer_v1", "Player 2's signal is lost", 0, 0.0, true, 4);
    b.show("s2m_session_peer_v2", "Player 2's signal is lost", 0, 0.1, true, 4);
    check(f.only_text() == "Player 2's signal is lost", "dedup: identical texts show once");
    b.clear("s2m_session_peer_v1", 0.2, true, 4);
    check(f.only_text() == "Player 2's signal is lost", "dedup: the twin keeps the line after one owner clears");
    b.clear("s2m_session_peer_v2", 0.3, true, 4);
    check(f.visible() == 0 && f.live.empty(), "dedup: last clear removes the board");
}

static void t_order_stable()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer_v1", "P1", 0, 0.0, true, 4);
    b.show("s2m_session_peer_v2", "P2", 0, 0.1, true, 4);
    b.show("s2m_session_peer_v1", "P1b", 0, 0.2, true, 4);
    check(f.only_text() == "P1b\nP2", "order: an update keeps the first-shown slot");
    b.show("zz_other", "X", 0, 0.3, true, 4);
    b.show("s2m_save_join", "T", 0, 0.4, true, 4);
    check(f.only_text() == "T\nP1b\nP2\nX", "order: priority then arrival");
}

static void t_timed()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer", "left", 10, 0.0, true, 4);
    b.show("s2m_save_join", "joining", 0, 1.0, true, 4);
    b.tick(9.9, true, 4);
    check(f.only_text() == "joining\nleft", "timed: still shown before its time");
    b.tick(10.0, true, 4);
    check(f.only_text() == "joining", "timed: removed at its time, the other line stays");
    b.show("s2m_session_role", "err", 20, 11.0, true, 4);
    b.show("s2m_session_role", "err2", 0, 15.0, true, 4);
    b.tick(31.0, true, 4);
    check(f.only_text() == "err2\njoining", "timed: a later persistent show is not cleared by the old timer");
    b.show("s2m_session_role", "err3", 5, 32.0, true, 4);
    b.show("s2m_session_role", "err4", 5, 35.0, true, 4);
    b.tick(37.0, true, 4);
    check(f.only_text() == "err4\njoining", "timed: a re-show restarts the time");
    b.tick(40.0, true, 4);
    check(f.only_text() == "joining", "timed: removed after the restarted time");
}

static void t_cap()
{
    Fake f;
    Board b(f.be());
    for (int i = 0; i < 6; ++i) b.show("m" + std::to_string(i), "L" + std::to_string(i), 0, i * 0.1, true, 4);
    check(f.only_text() == "L0\nL1\nL2\nL3", "cap: four lines shown, the rest wait");
    b.clear("m1", 1.0, true, 4);
    check(f.only_text() == "L0\nL2\nL3\nL4", "cap: a waiting line moves up when one is cleared");
    check(f.creates == 1, "cap: still one widget");
}

static void t_world()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer", "silent", 0, 0.0, true, 4);
    f.world = 2;
    b.show("s2m_save_join", "joining", 0, 1.0, true, 4);
    check(f.creates == 2 && f.visible() == 1, "world: a new board in the new world");
    check(f.live.size() == 1, "world: the old world's handle is released");
    check(f.only_text() == "joining\nsilent", "world: active lines carried");
}

static void t_mode_switch()
{
    Fake f;
    Board b(f.be());
    b.show("a", "A", 0, 0.0, true, 4);
    b.show("b", "B", 0, 0.1, true, 4);
    check(f.visible() == 1, "switch: board mode");
    b.tick(0.2, false, 4);
    check(f.visible() == 2 && f.overlaps() == 2, "switch: uiq=0 gives the old per-id widgets");
    b.tick(0.3, true, 4);
    check(f.visible() == 1 && f.only_text() == "A\nB", "switch: uiq=1 back to one board");
    b.clear("a", 0.4, false, 4);
    b.clear("b", 0.5, false, 4);
    check(f.live.empty(), "switch: old mode clear removes each widget");
}

static void t_empty_is_clear()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer", "The other player's signal is lost", 0, 0.0, true, 4);
    b.show("s2m_session_peer", "", 0, 1.0, true, 4);
    check(f.live.empty() && b.lines() == 0, "empty text clears the line");
}

static void t_expiry_no_world()
{
    Fake f;
    Board b(f.be());
    b.show("m1", "timed", 5, 100.0, true, 4);
    f.world = 0;
    b.tick(102.0, true, 4);
    check(b.lines() == 1, "no world: a timed line is still there before its time");
    b.tick(105.5, true, 4);
    check(b.lines() == 0 && f.live.empty(), "no world: the timed line expires on a plain clock tick");
    Fake g;
    Board c(g.be());
    c.max_life = 90;
    c.show("p", "signal lost", 0, 0.0, true, 4);
    g.world = 0;
    c.tick(89.0, true, 4);
    check(c.lines() == 1, "max life: a no-expiry line lives until the cap");
    c.tick(90.5, true, 4);
    check(c.lines() == 0 && g.live.empty(), "max life: a no-expiry line is gone at the cap with no world");
    Fake h;
    Board d(h.be());
    d.show("p", "signal lost", 0, 0.0, true, 4);
    d.tick(500.0, true, 4);
    check(d.lines() == 1, "max life 0: old behaviour, a no-expiry line stays");
}

static void t_clear_prefix()
{
    Fake f;
    Board b(f.be());
    b.show("s2m_session_peer", "gone soon", 0, 0.0, true, 4);
    b.show("s2m_session_peer_v1", "gone soon too", 0, 0.0, true, 4);
    b.show("s2m_save_join", "join", 0, 0.0, true, 4);
    b.show("nola", "keep", 6, 0.0, true, 4);
    b.clear_prefix("s2m_session_", 1.0, true, 4);
    b.clear_prefix("s2m_save_", 1.0, true, 4);
    check(b.lines() == 1 && f.only_text() == "keep", "clear prefix: session and save lines go, others stay");
    Fake g;
    Board c(g.be());
    c.show("s2m_session_peer", "x", 0, 0.0, false, 4);
    c.clear_prefix("s2m_session_", 1.0, false, 4);
    check(c.lines() == 0 && g.live.empty(), "clear prefix: old per-id mode removes the widget");
}

static void t_prio()
{
    check(prio_of("s2m_save_join") == 60 && prio_of("s2m_damage_down") == 40 && prio_of("s2m_travel_tpf") == 40, "prio: save and removed ids");
    check(prio_of("s2m_session_peer_v3") == 50 && prio_of("s2m_session_role") == 70 && prio_of("x") == 40, "prio: session and default");
}

int main()
{
    t_repro_old();
    t_stack_new();
    t_in_place();
    t_dedup();
    t_order_stable();
    t_timed();
    t_cap();
    t_world();
    t_mode_switch();
    t_empty_is_clear();
    t_expiry_no_world();
    t_clear_prefix();
    t_prio();
    std::printf("uiq stub test: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
