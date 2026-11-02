#include "../../src/s2x_istate.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace s2x_is;

namespace
{
int g_fail = 0, g_ok = 0;
void check(bool c, const char* what)
{
    if (c) ++g_ok;
    else
    {
        ++g_fail;
        std::printf("FAIL %s\n", what);
    }
}
IState mk(const std::string& sid, float dur)
{
    IState s;
    s.sid = sid;
    s.hasDur = true;
    s.dur = durOf(dur);
    return s;
}
IState full()
{
    IState s = mk("GunAK74_ST", 0.35f);
    s.upg = {"Upg_B", "Upg_A"};
    s.att = {"Scope_PSO", "GunAK74_MagExt"};
    s.hasAmmo = true;
    s.rounds = 17;
    s.ammo = "Ammo_545x39";
    s.ext = {{'c', "3f800000"}, {'q', "2"}};
    return s;
}
Inst in(uint32_t uid, long long n, const IState& s)
{
    Inst i;
    i.uid = uid;
    i.n = n;
    i.st = s;
    return i;
}
}

int main()
{
    IState f = full();
    std::string e = enc(f);
    check(e == "d3eb33333~uUpg_A.Upg_B~aScope_PSO.GunAK74_MagExt~r17.Ammo_545x39~c3f800000~q2", "enc of every aspect");
    IState g;
    g.sid = f.sid;
    check(dec(e, g) && same(f, g), "dec round trip of every aspect");
    check(g.hasDur && durF(g.dur) == 0.35f, "durability bits exact");
    check(g.upg.size() == 2 && g.att.size() == 2 && g.att[0] == "Scope_PSO", "upgrades and attach order");
    check(g.hasAmmo && g.rounds == 17 && g.ammo == "Ammo_545x39", "loaded ammo and type");
    check(g.ext.size() == 2 && g.ext[0].first == 'c' && g.ext[1].second == "2", "other aspects kept as ext");

    IState p;
    p.sid = "Medkit";
    check(p.plain() && enc(p).empty(), "plain item encodes empty");
    IState p2;
    check(decSid("Medkit=", p2) && p2.sid == "Medkit" && p2.plain(), "plain sid round trip");

    IState z = mk("GunPM_HG", 1.0f);
    z.hasAmmo = true;
    z.rounds = 0;
    IState z2;
    z2.sid = z.sid;
    check(dec(enc(z), z2) && same(z, z2) && z2.ammo.empty(), "empty magazine round trip");

    std::vector<IState> L = {f, mk("GunAK74_ST", 0.9f), p};
    std::string le = encList(L);
    std::vector<IState> L2;
    check(decList(le, L2) && L2.size() == 3 && same(L2[0], f) && same(L2[1], L[1]) && L2[2].plain(), "list round trip");
    check(textOk(le) && le.find_first_of("|#:; \t") == std::string::npos, "list text is safe for keys");

    std::vector<IState> big;
    for (int i = 0; i < 40; ++i) big.push_back(f);
    std::string be = encList(big);
    check(be.size() <= kMaxText, "list is bounded");
    std::vector<IState> bd;
    check(decList(be, bd) && !bd.empty() && bd.size() < 40, "bounded list still decodes");

    IState bad;
    check(!dec("d123", bad), "short durability rejected");
    check(!dec("r17", bad), "ammo without type separator rejected");
    check(!dec("x|y", bad), "unsafe char rejected");
    check(!dec("d3eb33333~d3eb33333", bad), "double durability rejected");
    std::vector<IState> L3;
    check(!decList("A=d3eb33333,", L3), "empty list entry rejected");
    check(!decSid("Bad Sid=", bad), "bad sid rejected");

    IState f2 = f;
    f2.upg = {"Upg_A", "Upg_B"};
    check(same(f, f2), "upgrade order does not matter");
    IState f3 = f;
    f3.dur = durOf(0.36f);
    check(!same(f, f3), "different durability differs");
    IState f4 = f;
    f4.rounds = 16;
    check(!same(f, f4), "different ammo differs");

    std::vector<Inst> have = {in(1, 1, mk("GunAK74_ST", 0.9f)), in(2, 1, f), in(3, 1, mk("GunAK74_ST", 0.6f))};
    std::vector<bool> used(3, false);
    check(pick(f, have, used) == 1, "pick finds the exact instance");
    check(pick(mk("GunAK74_ST", 0.88f), have, used) == 0, "pick finds the closest durability");
    used[1] = true;
    check(pick(f, have, used) != 1, "pick skips used");
    check(pick(mk("Other", 1.0f), have, used) == -1, "pick never crosses SID");

    std::vector<long long> left = {1, 1, 1};
    check(pickRm(mk("GunAK74_ST", 0.6f), have, left) == 2 && left[2] == 0, "take removes the exact instance");
    check(pickRm(mk("GunAK74_ST", 0.6f), have, left) != 2, "take does not reuse a removed instance");

    IState plainAk;
    plainAk.sid = "GunAK74_ST";
    std::vector<Inst> h2 = {in(10, 1, mk("GunAK74_ST", 0.6f)), in(11, 2, plainAk)};
    Plan pl = plan({f, mk("GunAK74_ST", 0.6f), mk("GunAK74_ST", 0.2f), mk("GunAK74_ST", 0.1f), mk("GunAK74_ST", 0.05f)}, h2);
    check(pl.keep.size() == 1 && pl.keep[0] == 0, "plan keeps the exact instance");
    check(pl.apply.size() == 2 && pl.apply[0].first == 1 && pl.apply[1].first == 1, "plan applies state to default units first");
    check(pl.create.size() == 2, "plan creates what has no item");
    check(pl.left[0] == 0 && pl.left[1] == 0, "plan uses every unit once");

    std::vector<long long> l2 = {1, 3};
    std::vector<Inst> h3 = {in(20, 1, mk("GunAK74_ST", 0.6f)), in(21, 3, plainAk)};
    check(pickSpare("GunAK74_ST", h3, l2) == 1 && l2[1] == 2, "spare prefers the default stack");

    std::vector<IState> a = {f, mk("GunAK74_ST", 0.9f), mk("GunAK74_ST", 0.9f)};
    std::vector<IState> b = {mk("GunAK74_ST", 0.9f)};
    check(minus(a, b).size() == 2 && common(a, b).size() == 1, "multiset minus and common");
    check(ofSid(a, "GunAK74_ST").size() == 3 && ofSid(a, "X").empty(), "of sid");
    check(expand({in(1, 3, plainAk), in(2, 1, f)}, true).size() == 1 && expand({in(1, 3, plainAk)}, false).size() == 3, "expand");

    check(show(f) == "GunAK74_ST dur=0.3500 upg=Upg_A.Upg_B att=Scope_PSO.GunAK74_MagExt ammo=17xAmmo_545x39 c=3f800000 q=2", "show");

    std::printf("istate: %d ok, %d failed\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
