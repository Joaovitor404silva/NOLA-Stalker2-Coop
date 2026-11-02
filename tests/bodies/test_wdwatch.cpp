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
    lg::WdWatch w;
    check(lg::wd_watch(w, 0, 100, 5, 35).empty(), "not armed: quiet");
    lg::wd_arm(w, 100);
    check(lg::wd_watch(w, 1, 10, 100.5, 35).empty(), "falling at 0.5 s: quiet");
    std::string m = lg::wd_watch(w, 2, 10, 102.1, 35);
    check(m.find("caido ha 2 s: estado 2 hp 10") != std::string::npos, "2 s sample");
    check(lg::wd_watch(w, 2, 10, 103, 35).empty(), "between marks quiet");
    m = lg::wd_watch(w, 2, 10, 110.2, 35);
    check(m.find("caido ha 10 s: estado 2 hp 10") != std::string::npos, "10 s sample");
    m = lg::wd_watch(w, 2, 10, 125.4, 35);
    check(m.find("caido ha 25 s") != std::string::npos, "25 s sample");
    check(lg::wd_watch(w, 2, 10, 140, 35).empty(), "no more samples");
    lg::wd_arm(w, 200);
    m = lg::wd_watch(w, 3, 2542, 203.5, 35);
    check(m.find("levantou o boneco caido 3.5 s depois") != std::string::npos && m.find("RegenHP") != std::string::npos, "stock wake by HP regen named");
    check(lg::wd_watch(w, 0, 2542, 204, 35).empty(), "wake logged once");
    lg::wd_arm(w, 300);
    m = lg::wd_watch(w, 0, 10, 301, 35);
    check(m.find("levantou o boneco caido") != std::string::npos && m.find("RegenHP") == std::string::npos, "wake below 35 not blamed on regen");
    lg::wd_arm(w, 400);
    check(lg::wd_watch(w, -1, -1, 401, 35).empty(), "unknown state before 2 s quiet");
    std::printf("wdwatch: %s\n", fails ? "FAIL" : "ok");
    return fails ? 1 : 0;
}
