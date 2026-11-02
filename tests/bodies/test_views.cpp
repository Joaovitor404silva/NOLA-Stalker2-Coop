#include "s2m_bodies_logic.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace lg = m01::lg;

static int fails = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { ++fails; std::printf("FAIL %s\n", what); }
    else std::printf("ok   %s\n", what);
}

static bool is(const char* a, const char* b) { return a && b ? std::strcmp(a, b) == 0 : a == b; }

int main()
{
    check(is(lg::view_why(false, 1, 1, false, false, false), "sem hub"), "single mode: views 1..3 closed with no hub");
    check(is(lg::view_why(false, 0, 1, true, false, false), "sem hub"), "single mode reason is only used for v>0");
    check(is(lg::view_why(true, 2, 2, false, false, false), "vista fechada"), "4-slot host with 2 clients: view 2 closed");
    check(is(lg::view_why(true, 1, 3, false, false, false), "sem snapshot"), "client slot not yet heard");
    check(lg::view_why(true, 1, 3, true, false, false) == nullptr, "live client keeps its bodies");
    check(is(lg::view_why(true, 1, 3, true, true, false), "saiu"), "silent client past peerleft: bodies removed");
    check(is(lg::view_why(true, 1, 3, true, true, true), "fechou o jogo"), "client sent BYE");
    check(lg::view_why(true, 0, 3, true, false, false) == nullptr, "view 0 alive in multi");

    bool sp[4] = {false, false, false, false};
    check(lg::spawn_turn(0, sp, 4), "nobody spawning: view 0 may spawn");
    sp[1] = true;
    check(!lg::spawn_turn(0, sp, 4) && !lg::spawn_turn(2, sp, 4), "view 1 spawning: views 0 and 2 wait");
    check(lg::spawn_turn(1, sp, 4), "the spawning view continues its own spawn");
    sp[1] = false;
    sp[3] = true;
    check(!lg::spawn_turn(1, sp, 4) && lg::spawn_turn(3, sp, 4), "one spawn at a time across 4 views");

    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
