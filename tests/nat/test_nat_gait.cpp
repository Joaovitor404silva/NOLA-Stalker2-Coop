#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include "../../src/s2n_gait.h"

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

namespace legacy
{
    struct E { int gb = 0; double rs = 0, gbAt = -100; bool catchUp = false; double csp = 0, latAt = -1; int ogait = 0; unsigned fl = 0; };
    struct C { double natsprint, natrun, natgadlo, natok, natgadhi, natcatch, natcatch2, natgadt; int natgaitmax, natgad; };
    int gait(E& e, const C& C, double now, double sp, double err, bool moving, bool soft, int& gbUp, int& gbDn)
    {
        int gmax = std::clamp(C.natgaitmax, 2, 5);
        int og = e.ogait;
        bool sprint = sp > C.natsprint || (og >= 4 && sp > 0.85 * C.natsprint && !e.catchUp);
        bool run = (e.fl & 128) || sp > C.natrun || (og >= 3 && sp > 0.8 * C.natrun);
        int gait = sprint ? 4 : (run ? 3 : 2);
        if (C.natgad && moving && !soft && sp > 50)
        {
            double rr = e.csp / sp;
            e.rs = e.rs <= 0 ? rr : 0.8 * e.rs + 0.2 * rr;
            if (e.latAt < 0 && now - e.gbAt >= C.natgadt)
            {
                if (e.rs < C.natgadlo && err > C.natok && gait + e.gb < gmax) { ++e.gb; e.gbAt = now; ++gbUp; }
                else if (e.gb > 0 && e.rs > C.natgadhi && err < C.natok) { --e.gb; e.gbAt = now; ++gbDn; }
            }
            gait += e.gb;
        }
        else if (!moving) { e.gb = 0; e.rs = 0; }
        if (!moving) e.catchUp = false;
        else if (err > C.natcatch) e.catchUp = true;
        else if (err < C.natcatch2) e.catchUp = false;
        if (e.catchUp) ++gait;
        if (!moving && err > 2 * C.natcatch) gait = std::max(gait, 3);
        gait = std::min(gait, gmax);
        return gait;
    }
}

struct Row { std::string kind; s2ngait::Spd s; };

static std::map<std::string, Row> load(const char* path)
{
    std::map<std::string, Row> m;
    std::ifstream f(path);
    std::string ln;
    while (std::getline(f, ln))
    {
        std::stringstream ss(ln);
        std::string k1, k2, t, v;
        if (!std::getline(ss, k1, '\t') || !std::getline(ss, k2, '\t') || !std::getline(ss, t, '\t') || !std::getline(ss, v, '\t')) continue;
        if (k1.size() < 2 || k1[0] != 's' || k2.size() < 2) continue;
        Row& r = m[k1.substr(1)];
        std::string k = k2.substr(1);
        if (k == "kind") r.kind = v;
        else if (k == "walk") r.s.walk = std::atof(v.c_str());
        else if (k == "run") r.s.run = std::atof(v.c_str());
        else if (k == "sprint") r.s.sprint = std::atof(v.c_str());
    }
    return m;
}

static void t_legacy(int natgait, bool forceHuman)
{
    std::mt19937 rng(1234 + natgait * 7 + (forceHuman ? 1 : 0));
    std::uniform_real_distribution<double> U(0, 1);
    int same = 0, n = 0;
    for (int run = 0; run < 2000; ++run)
    {
        legacy::C lc{450, 230, 0.6, 40, 0.9, 200, 80, 1, 2 + (int)(U(rng) * 4), U(rng) < 0.5 ? 1 : 0};
        s2ngait::Cfg gc;
        gc.natsprint = lc.natsprint; gc.natrun = lc.natrun; gc.natgadlo = lc.natgadlo; gc.natok = lc.natok; gc.natgadhi = lc.natgadhi;
        gc.natcatch = lc.natcatch; gc.natcatch2 = lc.natcatch2; gc.natgadt = lc.natgadt; gc.natgaitmax = lc.natgaitmax; gc.natgad = lc.natgad; gc.natgait = natgait;
        legacy::E le;
        s2ngait::St st;
        double now = 0;
        for (int f = 0; f < 60; ++f)
        {
            now += 0.016 + U(rng) * 0.3;
            double sp = U(rng) < 0.2 ? U(rng) * 20 : U(rng) * 1100;
            double err = U(rng) < 0.3 ? U(rng) * 50 : U(rng) * 700;
            bool moving = sp > 10, soft = U(rng) < 0.1, kin = U(rng) < 0.5;
            le.csp = U(rng) * 900; le.latAt = U(rng) < 0.7 ? -1 : now - 0.1; le.fl = U(rng) < 0.3 ? 128 : 0;
            int gu = 0, gd = 0;
            int a = legacy::gait(le, lc, now, sp, err, moving, soft, gu, gd);
            s2ngait::In in;
            in.sp = sp; in.err = err; in.csp = le.csp; in.now = now; in.latAt = le.latAt; in.og = le.ogait; in.hg = (int)(U(rng) * 7) - 1;
            in.moving = moving; in.soft = soft; in.kin = kin; in.mut = forceHuman ? false : U(rng) < 0.5; in.fl128 = (le.fl & 128) != 0;
            in.cs = {300, 750, 900};
            s2ngait::Out o = s2ngait::pick(in, gc, st);
            bool eq = o.gait == a && st.gb == le.gb && st.catchUp == le.catchUp && st.rs == le.rs && st.gbAt == le.gbAt && o.gbUp == gu && o.gbDn == gd && o.src == s2ngait::LEGACY;
            same += eq; ++n;
            le.ogait = a;
            if (!eq) break;
        }
    }
    CHECK(same == n, "natgait=%d human=%d: legacy equivalence %d/%d frames", natgait, forceHuman ? 1 : 0, same, n);
}

static void t_host()
{
    CHECK(s2ngait::host_gait(0, 4) == -1, "None -> unknown");
    CHECK(s2ngait::host_gait(1, 4) == -1, "Idle -> unknown");
    CHECK(s2ngait::host_gait(2, 4) == 2, "Walk -> 2");
    CHECK(s2ngait::host_gait(3, 4) == 3, "Run -> 3");
    CHECK(s2ngait::host_gait(4, 4) == 4, "Sprint -> 4");
    CHECK(s2ngait::host_gait(5, 4) == 3, "Jog with gmax 4 -> 3");
    CHECK(s2ngait::host_gait(5, 5) == 5, "Jog with gmax 5 -> 5");
    CHECK(s2ngait::host_gait(-1, 4) == -1, "missing -> unknown");
    s2ngait::Cfg gc;
    gc.natgait = 2;
    for (int hg = 2; hg <= 4; ++hg)
    {
        s2ngait::St st;
        s2ngait::In in;
        in.sp = 845; in.err = 30; in.moving = true; in.kin = true; in.mut = true; in.hg = hg; in.cs = {300, 750, 900};
        s2ngait::Out o = s2ngait::pick(in, gc, st);
        CHECK(o.gait == hg && o.src == s2ngait::HOST, "natgait=2 host gait %d -> %d src %d", hg, o.gait, o.src);
    }
    s2ngait::St st;
    s2ngait::In in;
    in.sp = 300; in.moving = true; in.kin = true; in.mut = true; in.hg = 0; in.cs = {300, 750, 900};
    s2ngait::Out o = s2ngait::pick(in, gc, st);
    CHECK(o.gait == 2 && o.src == s2ngait::CLASS, "natgait=2 unknown host gait falls back to the class rule: %d src %d", o.gait, o.src);
    gc.natgait = 1;
    in.hg = 4;
    o = s2ngait::pick(in, gc, st);
    CHECK(o.gait == 2 && o.src == s2ngait::CLASS, "natgait=1 ignores the host gait: %d src %d", o.gait, o.src);
}

static void t_classes(const std::map<std::string, Row>& m)
{
    int rows = 0, old = 0;
    for (const auto& [name, r] : m)
    {
        if (r.kind != "mutant" || !s2ngait::known(r.s)) continue;
        ++rows;
        const s2ngait::Spd& s = r.s;
        s2ngait::Cfg gc;
        auto at = [&](double sp, int og) {
            s2ngait::St st;
            s2ngait::In in;
            in.sp = sp; in.err = 10; in.moving = true; in.kin = true; in.mut = true; in.og = og; in.cs = s;
            return s2ngait::pick(in, gc, st);
        };
        s2ngait::Out w = at(s.walk, 2);
        CHECK(w.gait == 2, "%s walk %.0f -> %d", name.c_str(), s.walk, w.gait);
        if (s.run > s.walk)
        {
            s2ngait::Out rn = at(s.run, 2);
            CHECK(rn.gait == 3, "%s run %.0f -> %d", name.c_str(), s.run, rn.gait);
            old += rn.legacy != 3;
        }
        if (s.sprint > s.run)
        {
            s2ngait::Out sp = at(s.sprint, 3);
            CHECK(sp.gait == 4, "%s sprint %.0f -> %d", name.c_str(), s.sprint, sp.gait);
            old += sp.legacy != 4;
        }
        else
        {
            s2ngait::Out sp = at(std::max(s.sprint, s.run), 3);
            CHECK(sp.gait == 3, "%s top speed %.0f without a faster sprint stays run -> %d", name.c_str(), std::max(s.sprint, s.run), sp.gait);
        }
        old += w.legacy != 2;
    }
    CHECK(rows >= 30, "mutant classes with speeds: %d", rows);
    CHECK(old > 0, "the human rule disagrees with the class rule on %d mutant states", old);
    std::printf("class rows=%d human_rule_wrong_states=%d\n", rows, old);
}

static void t_hysteresis()
{
    s2ngait::Spd s{300, 750, 900};
    CHECK(s2ngait::cls_gait(500, 2, s) == 2, "below the run midpoint stays walk");
    CHECK(s2ngait::cls_gait(530, 2, s) == 3, "above the run midpoint is run");
    CHECK(s2ngait::cls_gait(500, 3, s) == 3, "run holds down to 0.9 of the midpoint");
    CHECK(s2ngait::cls_gait(470, 3, s) == 2, "run drops below 0.9 of the midpoint");
    CHECK(s2ngait::cls_gait(800, 4, s) == 4, "sprint holds down to 0.9 of the midpoint");
    CHECK(s2ngait::cls_gait(740, 4, s) == 3, "sprint drops below 0.9 of the midpoint");
}

static void t_kin_lock()
{
    s2ngait::Cfg gc;
    gc.natgad = 1;
    s2ngait::St st;
    st.gb = 1;
    double now = 0;
    int boosted = 0, n = 0;
    for (int f = 0; f < 200; ++f)
    {
        now += 0.05;
        s2ngait::In in;
        in.sp = f % 50 == 0 ? 0 : 300; in.err = 900; in.csp = 10; in.now = now; in.moving = in.sp > 10; in.kin = true; in.mut = true; in.cs = {300, 750, 900};
        s2ngait::Out o = s2ngait::pick(in, gc, st);
        boosted += o.gait != o.base; ++n;
        CHECK(o.lock, "kin + mutant + natgait=1 locks the boosts");
    }
    CHECK(boosted == 0 && st.gb == 0, "no catch-up, gad or still-far boost under kin: boosted=%d/%d gb=%d", boosted, n, st.gb);
    s2ngait::St st2;
    s2ngait::In in;
    in.sp = 300; in.err = 900; in.csp = 300; in.now = 1; in.moving = true; in.kin = false; in.mut = true; in.cs = {300, 750, 900};
    s2ngait::Out o = s2ngait::pick(in, gc, st2);
    CHECK(!o.lock && o.gait == 3, "without kin the catch-up boost still applies: gait %d", o.gait);
}

static void t_leg_rate()
{
    struct P { const char* name; s2ngait::Spd s; double clip[5]; };
    P ps[] = {{"Bloodsucker", {300, 750, 900}, {0, 0, 222, 564, 917}},
              {"Flesh", {320, 400, 400}, {0, 0, 150, 391, 0}},
              {"Blinddog", {150, 600, 600}, {0, 0, 117, 476, 617}},
              {"PseudoDog", {300, 480, 550}, {0, 0, 116, 480, 0}}};
    s2ngait::Cfg gc;
    for (const P& p : ps)
    {
        double st[3] = {p.s.walk, p.s.run, p.s.sprint};
        int want[3] = {2, 3, p.s.sprint > p.s.run ? 4 : 3};
        for (int k = 0; k < 3; ++k)
        {
            s2ngait::St s;
            s2ngait::In in;
            in.sp = st[k]; in.err = 0.1 * st[k]; in.moving = true; in.kin = true; in.mut = true; in.og = want[k]; in.cs = p.s;
            s2ngait::Out o = s2ngait::pick(in, gc, s);
            double host = p.clip[want[k]], neu = p.clip[o.gait], old = p.clip[o.legacy];
            CHECK(o.gait == want[k], "%s state %d at %.0f cm/s: client gait %d", p.name, want[k], st[k], o.gait);
            if (host > 0) std::printf("%-12s gait %d at %4.0f cm/s: host clip %4.0f, client clip new %4.0f (x%.2f) old %4.0f (x%.2f)\n", p.name, want[k], st[k], host, neu, neu / host, old, old > 0 ? old / host : 0.0);
        }
    }
}

static void t_human_host()
{
    s2ngait::Cfg gc;
    gc.natgad = 1;
    for (int mbh = 0; mbh <= 1; ++mbh)
    {
        gc.natmbh = mbh;
        for (int hg = 2; hg <= 4; ++hg)
        {
            s2ngait::St st;
            s2ngait::In in;
            in.sp = 120; in.err = 400; in.csp = 20; in.now = 5; in.moving = true; in.kin = true; in.mut = false; in.hg = hg;
            s2ngait::Out o = s2ngait::pick(in, gc, st);
            if (mbh) CHECK(o.gait == hg && o.src == s2ngait::HOST && o.lock, "natmbh=1 human host gait %d -> %d src %d lock %d", hg, o.gait, o.src, o.lock);
            else CHECK(o.src == s2ngait::LEGACY && !o.lock, "natmbh=0 human keeps the speed rule: src %d lock %d", o.src, o.lock);
        }
    }
    gc.natmbh = 1;
    s2ngait::St st;
    s2ngait::In in;
    in.sp = 300; in.err = 30; in.moving = true; in.kin = true; in.mut = false; in.hg = 1;
    s2ngait::Out o = s2ngait::pick(in, gc, st);
    CHECK(o.src == s2ngait::LEGACY && o.gait == 3 && !o.lock, "natmbh=1 host Idle while moving falls back to the speed rule: gait %d src %d", o.gait, o.src);
    s2ngait::St st2;
    s2ngait::In in2;
    in2.sp = 0; in2.err = 0; in2.moving = false; in2.kin = true; in2.mut = false; in2.hg = 3;
    o = s2ngait::pick(in2, gc, st2);
    CHECK(o.src == s2ngait::LEGACY && !o.lock, "natmbh=1 standing human is not locked: src %d", o.src);
}

static void t_kin_order()
{
    s2ngait::KoIn i;
    i.moving = true; i.hx = 0; i.hy = 0; i.vx = 150; i.vy = 0; i.cx = -50; i.cy = 0; i.lead = 3;
    auto o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_NEW && std::fabs(o.tx - 450) < 1e-6 && std::fabs(o.ty) < 1e-6, "kin new: why %d tx %.1f", o.why, o.tx);
    i.has = true; i.ox = 450; i.oy = 0; i.og = 2; i.gait = 2;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_NONE, "kin keep: why %d", o.why);
    i.gait = 3;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_GAIT, "kin gait change: why %d", o.why);
    i.gait = 2; i.cx = 400;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_NEAR, "kin near goal: why %d", o.why);
    i.cx = -50; i.vx = 0; i.vy = 150;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_TURN && std::fabs(o.ty - 450) < 1e-6, "kin turn: why %d ty %.1f", o.why, o.ty);
    i.vx = 150; i.vy = 20;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_NONE, "kin small bend keeps the order: why %d", o.why);
    i.moving = false; i.vx = 0; i.vy = 0;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_STOP && o.tx == 0 && o.ty == 0, "kin stop: why %d", o.why);
    i.ox = 10; i.oy = 0;
    o = s2ngait::kin_order(i);
    CHECK(o.why == s2ngait::KO_NONE, "kin stop already ordered: why %d", o.why);
    int orders = 0;
    s2ngait::KoIn w;
    w.moving = true; w.vx = 150; w.lead = 3;
    for (int f = 0; f < 600; ++f)
    {
        double t = f / 60.0;
        w.hx = 150 * t; w.cx = w.hx - 40;
        auto r = s2ngait::kin_order(w);
        if (r.why != s2ngait::KO_NONE) { ++orders; w.has = true; w.ox = r.tx; w.oy = r.ty; w.og = w.gait; }
    }
    CHECK(orders >= 3 && orders <= 5, "kin straight walk 10 s gives few long orders: %d", orders);
}

int main()
{
    t_legacy(0, false);
    t_legacy(1, true);
    t_legacy(2, true);
    t_host();
    t_classes(load("../../data/nat_classes.tsv"));
    t_hysteresis();
    t_kin_lock();
    t_leg_rate();
    t_human_host();
    t_kin_order();
    std::printf("test_nat_gait: ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
