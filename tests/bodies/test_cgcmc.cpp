#include "s2m_cgcmc_logic.h"

#include <cstdio>
#include <vector>

static int fails = 0;
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}

int main()
{
    using cgcmc::P;
    using cgcmc::View;
    float rad = cgcmc::radius(9500.f, 1.06f);
    check(rad > 10069.f && rad < 10071.f, "stock radius 9500 cm x 1.06");
    check(cgcmc::radius(0.f, 0.f) == cgcmc::radius(9500.f, 1.06f), "unread radius globals fall back to the stock values");
    check(cgcmc::radius(4000.f, 1.06f) < 4300.f, "an area radius override (40 m) is used as is");

    P body{1000.f, 2000.f, 300.f};
    std::vector<P> none;
    std::vector<P> nearc{{1500.f, 2000.f, 300.f}};
    std::vector<P> farc{{1000.f + 20000.f, 2000.f, 300.f}};
    std::vector<P> two{{1000.f + 20000.f, 2000.f, 300.f}, {1200.f, 2100.f, 280.f}};
    auto yes = [](const P&) { return true; };
    auto no = [](const P&) { return false; };

    check(cgcmc::keeper(body, none, rad, yes) < 0, "no remote player: the stock decision stays");
    check(cgcmc::keeper(body, nearc, rad, yes) == 0, "a remote player 5 m away keeps the corpse online");
    check(cgcmc::keeper(body, nearc, rad, no) < 0, "the stock ring test with the remote position says release: released");
    check(cgcmc::keeper(body, farc, rad, yes) < 0, "a remote player 200 m away does not keep it");
    check(cgcmc::keeper(body, two, rad, yes) == 1, "the second remote player keeps it");
    std::vector<P> up{{1000.f, 2000.f, 300.f + 20000.f}};
    check(cgcmc::keeper(body, up, rad, yes) < 0, "a remote player 200 m above does not keep it");
    std::vector<P> edge{{1000.f + rad - 1.f, 2000.f, 300.f}};
    check(cgcmc::keeper(body, edge, rad, yes) == 0, "just inside the radius");
    std::vector<P> out{{1000.f + rad + 1.f, 2000.f, 300.f}};
    check(cgcmc::keeper(body, out, rad, yes) < 0, "just outside the radius");

    int rings = 0;
    cgcmc::keeper(body, farc, rad, [&](const P&) { ++rings; return true; });
    check(rings == 0, "the stock ring test is not called for a far remote player");

    View v;
    v.ok = true;
    v.has_pos = true;
    v.up = true;
    v.age = 1.0;
    check(cgcmc::fresh(v, 5.0), "fresh snapshot of a joined peer");
    View s = v;
    s.age = 6.0;
    check(!cgcmc::fresh(s, 5.0), "stale snapshot (peer silent or loading)");
    View l = v;
    l.left = true;
    check(!cgcmc::fresh(l, 5.0), "a peer that left is not a centre");
    View d = v;
    d.up = false;
    check(!cgcmc::fresh(d, 5.0), "a peer not up is not a centre");
    View n = v;
    n.has_pos = false;
    check(!cgcmc::fresh(n, 5.0), "no position yet");

    check(cgcmc::wants(true, true, true, 1), "flag on, host, corpse, one centre");
    check(!cgcmc::wants(false, true, true, 1), "flag off (corpsemc=0) keeps the old behaviour");
    check(!cgcmc::wants(true, false, true, 1), "a client never changes its own A-Life decision");
    check(!cgcmc::wants(true, true, false, 1), "a live NPC is not touched");
    check(!cgcmc::wants(true, true, true, 0), "solo host: no change");

    check(cgcmc::wants_unit(true, true, 1) && !cgcmc::wants_unit(true, true, 0) && !cgcmc::wants_unit(true, false, 1) && !cgcmc::wants_unit(false, true, 1), "alifemc: any unit near a fresh client stays online (host only, needs a centre)");
    std::vector<View> vs = {{true, true, true, false, 1.0, {1000, 0, 0}}, {true, true, true, false, 9.0, {5000, 0, 0}}, {true, true, true, true, 1.0, {9000, 0, 0}}};
    auto ecs = cgcmc::extra_centres(vs, 5.0);
    check(ecs.size() == 1 && ecs[0].x == 1000, "alifemc: a stale (>5 s) or left view is no centre");
    std::vector<View> many(12, View{true, true, true, false, 1.0, {1, 2, 3}});
    check(cgcmc::extra_centres(many, 5.0).size() == 8, "alifemc: at most 8 centres");
    check(cgcmc::online_passes(true, 2) == 3 && cgcmc::online_passes(false, 2) == 1 && cgcmc::online_passes(true, 0) == 1, "alifemc: one online pass per extra centre");
    P u{15000, 0, 0};
    std::vector<P> hc = {{0, 0, 0}, {20000, 0, 0}};
    check(cgcmc::keeper(u, hc, cgcmc::radius(9500, 1.06f), [](const P&) { return true; }) == 1, "alifemc: a live NPC 50 m from a client and 150 m from the host is kept (B7)");
    P cl[2] = {{20000, 0, 0}, {-30000, 0, 0}};
    check(!cgcmc::far_all(true, u, cl, 2, rad) && cgcmc::far_all(true, u, cl, 0, rad) && !cgcmc::far_all(false, u, cl, 0, rad), "alifemc: far only when far from the host and from every client");
    P cf[1] = {{15000, 0, 20000}};
    check(cgcmc::far_all(true, u, cf, 1, rad), "alifemc: a client 200 m above does not keep it");
    int dd[] = {1, 2, 3, 2, 4, 1, 4, 5};
    int nn = cgcmc::dedup_tail(dd, 3, 8);
    check(nn == 5 && dd[3] == 4 && dd[4] == 5, "alifemc: units already in the host pass or twice in the client pass are dropped");
    int e[] = {7, 8};
    check(cgcmc::dedup_tail(e, 2, 2) == 2, "alifemc: no client pass, nothing dropped");
    check(cgcmc::cap_hit(53, 52) && !cgcmc::cap_hit(52, 52) && !cgcmc::cap_hit(60, 0), "alifemc: the agent cap acts only above the live cap");

    std::printf(fails ? "cgcmc: %d FAIL\n" : "cgcmc: all ok\n", fails);
    return fails ? 1 : 0;
}
