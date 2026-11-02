#include <cstdio>
#include "../../src/s2n_dsdrop.h"

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using namespace s2n_ds;

int main()
{
    const uint32_t pl = 7, npc = 100, bloodsucker = 101, anomaly = 0x9000000u;
    CHECK(classify(anomaly, pl, false, false, false, false, false, false) == NoAtk, "anomaly");
    CHECK(classify(pl, pl, true, false, false, false, false, false) == Self, "self");
    CHECK(classify(npc, pl, false, false, false, false, false, false) == NoPawn, "nopawn");
    CHECK(classify(npc, pl, true, true, false, false, false, false) == Copy, "copy");
    CHECK(classify(npc, pl, true, false, true, false, false, false) == Inert, "inert");
    CHECK(classify(npc, pl, true, false, false, true, false, false) == Puppet, "puppet");
    CHECK(classify(npc, pl, true, false, false, false, true, false) == Target, "target");
    CHECK(classify(npc, pl, true, false, false, false, false, true) == LocalHidden, "hidden");
    CHECK(classify(npc, pl, true, false, false, false, false, false) == LocalVisible, "visible");
    CHECK(is_local(LocalHidden) && is_local(LocalVisible) && !is_local(Copy) && !is_local(Inert) && !is_local(Self), "is_local");

    Tracker t;
    int64_t now = 1000;
    for (uint32_t id : {npc, bloodsucker}) t.note(id, now);
    CHECK(t.drop(npc, pl, 1, 3, now + 5), "first hit dropped");
    for (int64_t m = now; m <= now + 6000; m += 1000) t.note(npc, m);
    CHECK(t.drop(npc, pl, 1, 3, now + 6000), "hit 6 s later dropped while refreshed");
    CHECK(!t.drop(bloodsucker, pl, 1, 3, now + 6000), "unrefreshed id expires after 5 s");
    CHECK(!t.drop(anomaly, pl, 1, 3, now + 6000), "anomaly kept");
    t.note(pl, now + 6000);
    CHECK(!t.drop(pl, pl, 1, 3, now + 6000), "own grenade kept");
    CHECK(!t.drop(npc, pl, 10, 3, now + 6000), "bleed ty 10 kept");
    CHECK(!t.drop(npc, pl, 1, 19, now + 6000), "bleed src 19 kept");
    CHECK(!t.drop(555, pl, 1, 3, now + 6000), "host copy (never noted) kept");

    Tracker full;
    for (uint32_t i = 0; i < kMax + 20; ++i) full.note(1000 + i, 0);
    CHECK(full.v.size() == kMax, "cap");

    std::printf("nat_dsdrop ok=%d fail=%d\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
