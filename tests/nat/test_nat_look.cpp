#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include "../../src/s2n_look.h"

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static void t_old_tail()
{
    s2nlook::Tail a = s2nlook::tail_parse("1");
    CHECK(a.vis && !a.ext && a.hid.empty() && a.cpd.empty(), "old visible tail");
    s2nlook::Tail b = s2nlook::tail_parse("0");
    CHECK(!b.vis && !b.ext, "old hidden tail");
    CHECK(s2nlook::tail_txt(true, false, {}, {}) == "1", "no ext keeps the old 4-field form");
    CHECK(s2nlook::tail_txt(false, false, {1}, {2.f}) == "0", "no ext ignores fields");
}

static void t_round_trip()
{
    std::vector<uint32_t> h = {0x2, 0x0, 0xa};
    std::vector<float> c = {0.f, -1.5f, 3.4705f, 1e-7f, 12345678.f};
    std::string t = s2nlook::tail_txt(true, true, h, c);
    CHECK(t.find('+') == std::string::npos && t.find(';') == std::string::npos && t.find('|') == std::string::npos && t.find('=') == std::string::npos && t.find('#') == std::string::npos, "no reserved chars: %s", t.c_str());
    s2nlook::Tail r = s2nlook::tail_parse(t);
    CHECK(r.vis && r.ext, "ext flags");
    CHECK(r.hid == h, "hid round trip %s", t.c_str());
    CHECK(r.cpd.size() == c.size(), "cpd count");
    CHECK(s2nlook::cpd_dif(c, r.cpd) < 0, "cpd values survive: %s", t.c_str());
    s2nlook::Tail e = s2nlook::tail_parse(s2nlook::tail_txt(false, true, {}, {}));
    CHECK(!e.vis && e.ext && e.hid.empty() && e.cpd.empty(), "empty ext fields");
    CHECK(s2nlook::tail_txt(true, true, {}, {}) == "1@@", "empty ext form");
}

static void t_full_entry()
{
    std::string ent = "CharacterMesh0:0@^ch/SK_ful_ban_07@^m/a+^m/b@1@0,2@0,0,4";
    size_t a = ent.find('@'), b = ent.find('@', a + 1), c = ent.find('@', b + 1);
    std::string ms = ent.substr(b + 1, c - b - 1);
    CHECK(ms == "^m/a+^m/b", "mats field stays split by the third @");
    s2nlook::Tail r = s2nlook::tail_parse(ent.substr(c + 1));
    CHECK(r.vis && r.hid.size() == 2 && r.hid[1] == 2 && r.cpd.size() == 3 && r.cpd[2] == 4.f, "tail fields");
}

static void t_dif()
{
    CHECK(s2nlook::hid_dif({}, {}, 4) < 0, "both clear");
    CHECK(s2nlook::hid_dif({0x2}, {}, 4) == 0, "host hides lod0 section 1, client shows");
    CHECK(s2nlook::hid_dif({}, {0x2}, 4) == 0, "client keeps a stale hidden section");
    CHECK(s2nlook::hid_dif({0, 0, 0x1}, {}, 2) < 0, "host lod beyond client lod count is not counted");
    CHECK(s2nlook::hid_dif({0, 0, 0x1}, {}, 3) == 2, "lod 2 differs");
    CHECK(s2nlook::hid_dif({0x1}, {0x1, 0}, -1) < 0, "trailing zero lods equal");
    CHECK(s2nlook::cpd_dif({0, 0, 3}, {0, 0, 5}) == 2, "tone differs at 2");
    CHECK(s2nlook::cpd_dif({0, 0, 3}, {0, 0, 3.00001f}) < 0, "rounding equal");
    CHECK(s2nlook::cpd_dif({0, 0, 3}, {0}) == 1, "client array shorter");
    CHECK(s2nlook::cpd_dif({}, {1, 2}) < 0, "host has none: nothing to mirror");
    std::vector<uint32_t> t = {1, 0, 0};
    s2nlook::hid_trim(t);
    CHECK(t.size() == 1, "trim");
}

static void t_scalars()
{
    std::vector<s2nlook::Sc> h = {{0, "Damage_Intensity", 1.f}, {2, "BloodAmount", 0.25f}};
    std::string t = s2nlook::tail_txt(true, true, {0x2}, {0.f, 3.f}, h);
    CHECK(t.find('+') == std::string::npos && t.find(';') == std::string::npos && t.find('|') == std::string::npos && t.find('=') == std::string::npos && t.find('#') == std::string::npos, "no reserved chars: %s", t.c_str());
    s2nlook::Tail r = s2nlook::tail_parse(t);
    CHECK(r.ext && r.hid.size() == 1 && r.cpd.size() == 2 && r.sc.size() == 2, "all four fields parse: %s", t.c_str());
    CHECK(s2nlook::sc_dif(h, r.sc) < 0 && r.sc[0].slot == 0 && r.sc[0].name == "Damage_Intensity" && r.sc[1].slot == 2, "scalars survive");
    CHECK(s2nlook::tail_txt(true, true, {}, {}, {}) == "1@@", "no scalars keeps the three-field form");
    s2nlook::Tail o = s2nlook::tail_parse("1@2@0,3");
    CHECK(o.sc.empty() && o.cpd.size() == 2, "old three-field tail parses with no scalars");
    s2nlook::Tail e = s2nlook::tail_parse(s2nlook::tail_txt(false, true, {}, {}, h));
    CHECK(!e.vis && e.hid.empty() && e.cpd.empty() && e.sc.size() == 2, "scalars with empty hid and cpd");
    std::vector<s2nlook::Sc> zero = {{0, "Damage_Intensity", 0.f}};
    CHECK(s2nlook::sc_dif(h, zero) == 0, "the corpse case: host 1, client 0 differs at the first");
    CHECK(s2nlook::sc_dif(h, {}) == 0, "client without the entry differs");
    CHECK(s2nlook::sc_dif({}, zero) < 0, "host has none: nothing to mirror");
    CHECK(s2nlook::sc_dif({{0, "Damage_Intensity", 1.f}}, {{1, "Damage_Intensity", 1.f}}) == 0, "slot is part of the identity");
    CHECK(s2nlook::sc_dif({{0, "Damage_Intensity", 1.f}}, {{0, "Damage_Intensity", 1.00001f}}) < 0, "rounding equal");
    CHECK(s2nlook::sc_name_gore("Damage_Intensity") && s2nlook::sc_name_gore("bloodAmt") && s2nlook::sc_name_gore("GoreMask") && s2nlook::sc_name_gore("WoundSize"), "gore names pass");
    CHECK(!s2nlook::sc_name_gore("Roughness") && !s2nlook::sc_name_gore("Tint"), "other names are not sent");
    CHECK(!s2nlook::sc_name_ok("a=b") && !s2nlook::sc_name_ok("a@b") && !s2nlook::sc_name_ok("") && s2nlook::sc_name_ok("Damage_Intensity"), "wire-safe names only");
    std::vector<s2nlook::Sc> bad = {{0, "a|b", 1.f}};
    CHECK(s2nlook::sc_txt(bad).empty(), "an unsafe name is dropped, not sent");
}

struct P { std::string key, mesh; bool vis = true, made = false; };
static bool sp(const std::string& k) { return k.rfind("CharacterMesh0", 0) == 0; }
static int pri(const P&) { return 0; }
static std::vector<int> pair(const std::vector<P>& h, const std::vector<P>& c, bool assign, bool natg)
{
    return s2nlook::pair_parts(h, c, assign, natg, sp, [](size_t, size_t) { return true; }, pri);
}

static void t_pair()
{
    std::vector<P> h = {{"CharacterMesh0:0", "body"}, {"SK:0", "vest"}, {"SK:1", "pouch"}, {"SK:2", "hood"}};
    std::vector<P> c = {{"CharacterMesh0:0", "body"}, {"SK:0", "pouch"}, {"SK:1", "belt"}};
    std::vector<int> m = pair(h, c, true, true);
    CHECK(m[0] == 0, "body pairs by key");
    CHECK(m[1] < 0, "natg: vest with no client mesh stays unpaired, not given the belt");
    CHECK(m[2] == 1, "natg: pouch pairs by mesh identity even with another key");
    CHECK(m[3] < 0, "natg: hood unpaired");
    std::vector<int> o = pair(h, c, true, false);
    CHECK(o[1] == 1 && o[2] == 2 && o[3] < 0, "flag-0 path: key first, then assign the free component");
    std::vector<P> c2 = {{"CharacterMesh0:0", "body"}, {"SK:0", "vest", true, true}, {"SK:1", "pouch"}};
    std::vector<int> n = pair(h, c2, true, true);
    CHECK(n[1] == 1, "made part pairs by its host key");
    CHECK(n[2] == 2, "native pouch pairs by mesh");
    std::vector<P> c3 = {{"CharacterMesh0:0", "body"}, {"SK:0", "", true, true}};
    std::vector<int> q = pair(h, c3, true, true);
    CHECK(q[1] == 1, "made part with no mesh yet still pairs by key");
}

static void t_stale()
{
    std::vector<P> h = {{"CharacterMesh0:0", "body"}, {"SK:0", "vest"}, {"SK:1", "pouch"}, {"SK:2", "pouch"}, {"SK:3", "cap", false}};
    std::vector<P> c = {{"CharacterMesh0:0", "body"}, {"SK:0", "vest", true, true}, {"SK:1", "pouch"}, {"SK:2", "pouch", true, true}, {"SK:3", "cap", true, true}, {"SK:9", "old", true, true}};
    CHECK(!s2nlook::made_stale("SK:0", "vest", h, c), "made vest still wanted: keep");
    CHECK(!s2nlook::made_stale("SK:2", "pouch", h, c), "second pouch: one native, two wanted: keep");
    CHECK(s2nlook::made_stale("SK:9", "old", h, c), "key gone from the host list: drop");
    CHECK(s2nlook::made_stale("SK:3", "cap", h, c), "host hides the part: drop");
    std::vector<P> c2 = {{"CharacterMesh0:0", "body"}, {"SK:0", "vest"}, {"SK:0", "vest", true, true}};
    CHECK(s2nlook::made_stale("SK:0", "vest", h, c2), "generator now built the vest itself: drop the copy");
    std::vector<P> c3 = {{"CharacterMesh0:0", "body"}, {"SK:0", "", true, true}};
    CHECK(!s2nlook::made_stale("SK:0", "", h, c3), "mesh not set yet: keep, retry the set");
}

static void t_gen_plan()
{
    std::vector<std::string> ev;
    auto drop = [&]() { ev.push_back("drop"); };
    auto redress = [&](const std::string& sid, const std::string& want) { ev.push_back("redress " + sid + " " + want); return true; };
    CHECK(s2nlook::gen_apply_plan("bandit_07/a/b/-/c", "a/b/-/c", drop, redress) == 0 && ev.empty(), "same picks: no drop, no redress (the old early drop destroyed made parts here)");
    CHECK(s2nlook::gen_apply_plan("bandit_07/a/b/-/c", "a/b/x/c", drop, redress) == 1 && ev.size() == 2 && ev[0] == "drop" && ev[1] == "redress bandit_07 a/b/-/c", "picks differ: drop once, then redress");
    ev.clear();
    auto bad = [&](const std::string&, const std::string&) { ev.push_back("redress"); return false; };
    CHECK(s2nlook::gen_apply_plan("bandit_07/a", "b", drop, bad) == -1 && ev.size() == 2, "failed redress reports -1 after the drop");
    ev.clear();
    CHECK(s2nlook::gen_apply_plan("nogenerator", "x", drop, redress) == -1 && ev.empty(), "no sid separator: nothing touched");
    CHECK(s2nlook::gen_apply_plan("", "", drop, redress) == -1 && ev.empty(), "empty: nothing touched");
}

static void t_part_room()
{
    size_t natives = 0;
    std::vector<P> seen;
    for (size_t i = 0; i < 20; ++i)
    {
        if (!s2nlook::part_room(false, natives)) continue;
        ++natives;
        seen.push_back({"SK:" + std::to_string(i), "m" + std::to_string(i)});
    }
    for (size_t i = 0; i < 3; ++i)
    {
        if (!s2nlook::part_room(true, natives)) continue;
        seen.push_back({"H:" + std::to_string(i), "hm" + std::to_string(i), true, true});
    }
    CHECK(natives == s2nlook::kNativeMax, "native parts stop at %zu, got %zu", s2nlook::kNativeMax, natives);
    int made = 0;
    for (const P& p : seen) if (p.made) ++made;
    CHECK(made == 3, "every made part after 16 natives is still listed: %d", made);
    CHECK(!s2nlook::made_stale("H:1", "hm1", {{"H:1", "hm1"}}, seen), "a made part past the native cap is seen by made_stale and kept");
    std::vector<int> m = pair({{"H:2", "hm2"}}, seen, true, true);
    CHECK(m[0] == (int)seen.size() - 1, "a made part past the native cap pairs by key");
    size_t total = seen.size();
    int mk = 0;
    for (int i = 0; i < 100 && s2nlook::make_room(total); ++i) { ++total; ++mk; }
    CHECK(total == s2nlook::kPartsMax && mk == (int)(s2nlook::kPartsMax - 19), "making stops at %zu components total", s2nlook::kPartsMax);
    CHECK(!s2nlook::make_room(s2nlook::kPartsMax) && !s2nlook::make_room(s2nlook::kPartsMax + 5), "no room at or past the cap");
}

int main()
{
    t_old_tail();
    t_round_trip();
    t_full_entry();
    t_gen_plan();
    t_part_room();
    t_dif();
    t_scalars();
    t_pair();
    t_stale();
    std::printf("test_nat_look: %d ok, %d fail\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
