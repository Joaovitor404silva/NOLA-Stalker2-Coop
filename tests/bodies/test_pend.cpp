#include "s2m_bodies_logic.h"

#include <cstdio>

namespace lg = m01::lg;

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
    check(lg::pend_hit(true, false, 5000, 4000, 6000, 120, -90, 800), "new target body near the spawn point is ours before adoption");
    check(!lg::pend_hit(true, true, 5000, 4000, 6000, 120, -90, 800), "a target-range pawn that existed before the spawn request is not ours");
    check(!lg::pend_hit(true, false, 100, 4000, 6000, 10, 10, 800), "a stock NPC hp is not ours");
    check(!lg::pend_hit(true, false, 5000, 4000, 6000, 800, 0, 800), "800 cm from the spawn point is outside the adoption radius");
    check(lg::pend_hit(true, false, 5000, 4000, 6000, 799, 0, 800), "799 cm is inside the adoption radius");
    check(lg::pend_hit(false, true, 100000, 90000, 1e9, 3000, 0, 5000), "puppet adoption may take a body that existed before");
    check(!lg::pend_hit(false, false, 5000, 90000, 1e9, 0, 0, 5000), "a target hp is not a puppet");
    check(!lg::pend_hit(false, false, 100000, 90000, 1e9, 5000, 0, 5000), "puppet outside pupadoptr");
    std::printf(fails ? "pend: %d FAIL\n" : "pend: all ok\n", fails);
    return fails ? 1 : 0;
}
