#include "s2m_bodies_logic.h"

#include <cstdio>
#include <string>

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
    lg::Hold h;
    lg::hold_arm(h, true, 100);
    check(h.held && h.at && *h.at == 100, "armed");
    check(lg::hold_step(h, true, false, 531, 102, 300).empty() && h.held, "not stable: no read");
    std::string m = lg::hold_step(h, true, true, 531, 105, 300);
    check(h.held && m.find("531 nos") != std::string::npos, "held logs once");
    check(lg::hold_step(h, true, true, 531, 150, 300).empty() && h.held, "held quiet");
    m = lg::hold_step(h, true, true, 0, 221.2, 300);
    check(!h.held && m.find("vazia (0) 121.2 s") != std::string::npos, "released at 0 after 121 s");
    check(lg::hold_step(h, true, true, 531, 230, 300).empty() && !h.held, "no re-latch without a new pawn");
    lg::hold_arm(h, true, 1000);
    lg::hold_step(h, true, true, 7, 1005, 300);
    m = lg::hold_step(h, true, true, 7, 1301, 300);
    check(!h.held && m.find("qholdmax") != std::string::npos, "cap releases");
    lg::hold_arm(h, true, 2000);
    m = lg::hold_step(h, true, true, -1, 2005, 300);
    check(!h.held, "no ScenarioManager releases");
    lg::hold_arm(h, true, 3000);
    check(lg::hold_step(h, false, true, 531, 3005, 300).empty() && !h.held, "qhold 0 never holds");
    lg::hold_arm(h, false, 4000);
    check(!h.held, "arm off");
    std::printf("hold: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
