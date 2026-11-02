#include "s2m_worn.h"
#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main()
{
    using namespace s2m_worn;
    Worn w = parse("2=wpn_m16,4=armor_x,9=bad sid,0=x,17=y,=z,3=");
    check(w.size() == 2 && w[2] == "wpn_m16" && w[4] == "armor_x", "parse keeps slots 1..16 with a valid SID");
    check(str(w) == "2=wpn_m16,4=armor_x", "str round trip");
    check(parse("").empty() && str(Worn{}).empty(), "empty");
    {
        Worn host = parse("1=wpn_knife,2=wpn_m4,4=armor_host");
        Worn mine = parse("1=wpn_knife,2=wpn_ak74,5=helmet_mine");
        Plan p = plan(host, mine);
        check(p.off.size() == 2 && p.off[0] == 2 && p.off[1] == 4, "host M4 and host armour leave the slots");
        check(p.kept.size() == 1 && p.kept[0] == "wpn_knife", "same SID in the same slot stays");
        check(p.on.size() == 2 && p.on[0] == std::make_pair(2, std::string("wpn_ak74")) && p.on[1] == std::make_pair(5, std::string("helmet_mine")), "client weapon and helmet go back on");
    }
    {
        Plan p = plan(parse("2=wpn_m4"), Worn{});
        check(p.off.size() == 1 && p.on.empty() && p.kept.empty(), "client wore nothing: host rifle leaves, nothing to wear");
    }
    {
        Plan p = plan(parse("2=wpn_m4"), parse("3=wpn_m4"));
        check(p.off.size() == 1 && p.off[0] == 2 && p.on.size() == 1 && p.on[0].first == 3, "same SID in another slot: moved");
    }
    {
        Plan p = plan(parse("2=wpn_ak74"), parse("2=wpn_ak74"));
        check(p.off.empty() && p.on.empty() && p.kept.size() == 1, "identical: nothing changes");
    }
    std::printf("%s\n", fails ? "FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
