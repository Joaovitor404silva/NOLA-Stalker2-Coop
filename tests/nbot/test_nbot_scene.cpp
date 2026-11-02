#include "s2m_census_logic.h"
#include "nbot_scene.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0, g_ok = 0;
static void check(bool c, const std::string& what)
{
    if (c) ++g_ok;
    else { ++g_fail; std::printf("FAIL %s\n", what.c_str()); }
}

static cen::Row npc(const std::string& key, const std::string& cls, bool alive, bool player, bool nola, double dist)
{
    cen::Row r("npc", key);
    r.s("cls", cls).n("uid", 7).b("alive", alive).b("hidden", false).b("player", player).b("nola", nola).n("dist", dist).n("x", 100).n("y", -200.5).n("z", 30).b("rendered", true).n("hp", alive ? 80 : 0);
    return r;
}

static std::string slurp(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main(int argc, char** argv)
{
    std::string scenes = argc > 1 ? argv[1] : "..\\..\\tools\\bench\\scenes";

    std::vector<cen::Row> rows = {
        npc("player:local", "BP_Player_C", true, true, false, 0),
        npc("u11", "BP_AI_Human_Bandit_C", true, false, false, 900),
        npc("u12", "BP_AI_Human_Bandit_C", false, false, false, 400),
        npc("u13", "BP_AI_Bloodsucker_C", true, false, false, 6000),
        npc("nola:puppet:1", "BP_Puppet_C", true, false, true, 300),
        npc("u14", "BP_AI_Human_Neutral_C", true, false, false, 1200),
    };
    cen::Row body("body", "puppet:1");
    body.n("x", 10).n("y", 11).n("z", 55).b("hidden", false);
    rows.push_back(body);
    std::vector<std::pair<std::string, std::string>> head = {{"role", cen::jstr("host")}, {"pid", "0"}, {"views", "2"}, {"t", "12.5"}, {"wall_ms", "1700000000000"},
                                                             {"radius", "5000"}, {"me", "[1.5,-2.5,30]"}, {"peers", "[{\"view\":1,\"x\":10,\"y\":11}]"}};
    std::vector<cen::Hit> hits;
    std::string text = cen::census_json(rows, head, hits, rows, 1700000000000LL, 12.5, "host");

    nsc::Census c = nsc::parse_census(text);
    check(c.ok, "parse real census_json output");
    check(c.role == "host", "role");
    check(c.me[0] == 1.5 && c.me[1] == -2.5 && c.me[2] == 30, "me array");
    check(c.peers.size() == 1 && c.peers[0].view == 1 && c.peers[0].x == 10, "peers");
    int npcs = 0, bodies = 0;
    for (auto& r : c.rows) { npcs += r.fam == "npc"; bodies += r.fam == "body"; }
    check(npcs == 6, "6 npc rows, got " + std::to_string(npcs));
    check(bodies == 1, "1 body row");
    for (auto& r : c.rows) check(r.fam == "npc" || r.fam == "body", "no digest/wh/inv rows leak: " + r.fam + " " + r.key);
    const nsc::CRow* u12 = c.find("npc", "u12");
    check(u12 && u12->str("alive") == "false" && u12->num("hp") == 0 && u12->num("y") == -200.5, "row u12 fields");
    check(u12 && u12->str("cls") == "BP_AI_Human_Bandit_C", "row cls");
    check(c.find("*", "puppet:1") != nullptr, "any-family find");
    check(!nsc::parse_census("{\"x\":1}\n").ok, "non census rejected");

    {
        std::vector<std::string> t = nsc::split_ws("wait census npc u12 alive=false hp<=0 timeout=9");
        nsc::Query q = nsc::parse_query(t, 2);
        check(q.err.empty() && q.conds.size() == 2, "query parse");
        check(nsc::run_query(c, q).size() == 1, "query dead u12");
        q = nsc::parse_query(nsc::split_ws("expect census npc * cls~Bandit count>=2"), 2);
        check(q.minCount == 2 && nsc::run_query(c, q).size() == 2, "wildcard key + substring + count");
        q = nsc::parse_query(nsc::split_ws("expect census npc * dist>1000 alive=true"), 2);
        check(nsc::run_query(c, q).size() == 2, "numeric > and bool");
        q = nsc::parse_query(nsc::split_ws("expect census npc * missing=1"), 2);
        check(nsc::run_query(c, q).empty(), "missing field never matches");
        q = nsc::parse_query(nsc::split_ws("expect census npc * key!=u11 cls~Human"), 2);
        check(nsc::run_query(c, q).size() == 2, "key pseudo field");
        q = nsc::parse_query(nsc::split_ws("expect census npc * dist"), 2);
        check(!q.err.empty(), "bad condition rejected");
    }

    {
        auto h = nsc::hostiles(c, 1000, {});
        check(h.size() == 1 && h[0]->key == "u11", "hostiles within 10 m: only live bandit u11");
        h = nsc::hostiles(c, 1500, {"Neutral"});
        check(h.size() == 1, "allow list skips neutral");
        h = nsc::hostiles(c, 10000, {});
        check(h.size() == 3, "player, nola puppet and dead skipped");
        nsc::Census c2 = c;
        for (auto& r : c2.rows)
            if (r.key == "u11") { nsc::JVal v; v.t = 's'; v.s = "none"; v.raw = "none"; r.f["ai"] = v; }
        check(nsc::hostiles(c2, 1000, {}).empty(), "npc without an AI controller is not a threat");
    }

    {
        std::vector<std::string> errs;
        auto st = nsc::parse_scene("\xEF\xBB\xBFscene demo\n\nnote hi\r\nrequire nohostile 3000 allow=Neutral\nspawn sid=GeneralNPC_Bandit at=player+4 freeze=1 as=npc\nkill $npc\nwait census npc $npc alive=false timeout=12\ntime 12:30\nframe $npc dist=3 fov=60\nbox $npc at=1700000000000\ntp peer+150\nwait 2\nwait log NOLA.*CAIDO timeout=30\n", errs);
        check(errs.empty(), "scene parses clean");
        check(st.size() == 11, "11 steps, got " + std::to_string(st.size()));
        if (st.size() == 11)
        {
            check(st[0].op == "note" && st[0].line == 3, "line numbers count blank and scene lines");
            check(st[4].timeout == 12, "timeout= option");
            check(st[3].timeout == 20, "default timeout 20");
            check(nsc::opt(st[2].t, "as") == "npc" && nsc::opt(st[2].t, "freeze") == "1", "spawn options");
        }
        errs.clear();
        nsc::parse_scene("time 25:00\nspawn at=player+3\nfly away\nrequire nohostile x\nwait census npc\ntp nowhere\n", errs);
        check(errs.size() == 6, "6 bad lines rejected, got " + std::to_string(errs.size()));
        errs.clear();
        auto sd = nsc::parse_scene("@C give Medkit 2\n@D   wait log NOLA\\sLIFE timeout=6\nkey F 150\n# x\n", errs);
        check(errs.empty() && sd.size() == 3, "side prefixed scene");
        if (sd.size() == 3)
        {
            check(sd[0].side == "C" && sd[0].op == "give" && sd[0].text == "give Medkit 2", "side C step text without prefix");
            check(sd[1].side == "D" && sd[1].timeout == 6 && sd[1].t[2] == "NOLA\\sLIFE", "side D wait log");
            check(sd[2].side.empty() && sd[2].op == "key", "unprefixed harness key step");
        }
        errs.clear();
        auto e2 = nsc::parse_scene("", errs);
        check(e2.empty() && errs.empty(), "empty text");
    }

    {
        int h, m;
        check(nsc::parse_hhmm("07:05", h, m) && h == 7 && m == 5, "hhmm");
        check(!nsc::parse_hhmm("7", h, m), "hhmm bad");
        double v;
        check(nsc::parse_rel("player+2.5", "player", v) && v == 2.5, "rel");
        check(!nsc::parse_rel("peer+1", "player", v), "rel base mismatch");
        nsc::Rot r = nsc::look_rot(0, 0, 0, 100, 100, 0);
        check(std::fabs(r.yaw - 45) < 1e-9 && std::fabs(r.pitch) < 1e-9, "look yaw 45");
        r = nsc::look_rot(0, 0, 0, 0, -100, 100);
        check(std::fabs(r.yaw + 90) < 1e-9 && std::fabs(r.pitch - 45) < 1e-9, "look yaw -90 pitch 45");
        double ox, oy;
        nsc::stand_point(0, 0, 1000, 0, 300, ox, oy);
        check(ox == 300 && oy == 0, "stand point on the player side");
        nsc::stand_point(5, 5, 5, 5, 300, ox, oy);
        check(ox == 305 && oy == 5, "stand point degenerate");
        double cs[8][3];
        nsc::bounds_corners(10, 20, 30, 1, 2, 3, cs);
        check(cs[0][0] == 9 && cs[0][1] == 18 && cs[0][2] == 27 && cs[7][0] == 11 && cs[7][1] == 22 && cs[7][2] == 33, "bounds corners");
        std::vector<std::pair<bool, std::pair<double, double>>> pts = {{true, {100, 100}}, {true, {200, 300}}, {false, {9999, 9999}}};
        nsc::Box b = nsc::screen_box(pts, 1366, 768);
        check(b.any && b.x0 == 100 && b.y1 == 300 && b.inFront == 2 && b.vis == 1, "screen box inside");
        pts = {{true, {-100, 0}}, {true, {100, 100}}};
        b = nsc::screen_box(pts, 1366, 768);
        check(std::fabs(b.vis - 0.5) < 1e-9, "screen box half clipped");
        pts = {{false, {0, 0}}};
        check(!nsc::screen_box(pts, 1366, 768).any, "nothing in front");
        bool ok;
        check(nsc::relation_value("neutral", ok) == 0 && ok, "neutral");
        check(nsc::relation_value("-500", ok) == -500 && ok, "int relation");
        nsc::relation_value("maybe", ok);
        check(!ok, "bad relation");
        check(nsc::jesc("a\"b\\c\nd") == "a\\\"b\\\\c\\nd", "json escape");
        std::vector<nsc::PickCand> pk = {{true, false, 0.0}, {false, true, 5.0}};
        check(nsc::pick_visible(pk) == 1, "pick visible avatar over hidden body at same spot");
        pk = {{false, true, 5.0}, {true, false, 0.0}};
        check(nsc::pick_visible(pk) == 0, "pick visible avatar regardless of order");
        pk = {{false, false, 0.0}, {false, true, 9.0}};
        check(nsc::pick_visible(pk) == 1, "rendered beats not rendered");
        check(nsc::pick_visible({}) == -1, "no candidates");
    }

    {
        const char* names[] = {"corpse_loot.scene", "downed_revive.scene", "mutant_frame.scene"};
        for (auto n : names)
        {
            std::string t = slurp(scenes + "\\" + n);
            check(!t.empty(), std::string("scene file ") + n);
            std::vector<std::string> errs;
            auto st = nsc::parse_scene(t, errs);
            std::string e;
            for (auto& x : errs) e += x + "; ";
            check(errs.empty() && st.size() >= 4, std::string("scene ") + n + " parses: " + e);
        }
    }

    std::printf("nbot_scene: %d ok, %d fail\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
