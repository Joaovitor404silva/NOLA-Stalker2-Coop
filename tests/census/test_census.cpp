#include "s2m_census_logic.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_fail = 0, g_ok = 0;
static void check(bool c, const char* what)
{
    if (c)
        ++g_ok;
    else
    {
        ++g_fail;
        std::printf("FAIL %s\n", what);
    }
}

static std::string slurp(const char* p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static cen::Row npc(const std::string& key, bool alive, bool hidden, bool paired, double dist)
{
    cen::Row r("npc", key);
    r.s("cls", "BP_AI_Human_C").n("uid", 0).b("alive", alive).b("hidden", hidden).b("paired", paired).b("twin", false).b("inert", false).b("own", false).b("vend", false);
    r.b("hk", false).b("player", false).b("nola", false).s("nolak", "-").n("natnpc", 1).n("dist", dist).n("x", 100).n("y", 200).n("z", 0).s("ai", "none").b("rendered", true).b("coll", true).b("hutab", true);
    r.b("shared", true);
    return r;
}

static void write(const char* path, const std::vector<cen::Row>& rows, const std::string& role, const std::vector<cen::Hit>& hits)
{
    std::vector<std::pair<std::string, std::string>> head = {{"role", cen::jstr(role)}, {"pid", role == "host" ? "0" : "1"}, {"views", "1"}, {"t", "1234.5"}, {"wall_ms", "1700000000000"},
                                                             {"radius", "5000"}, {"me", "[0,0,0]"}, {"peers", "[{\"view\":0,\"x\":10,\"y\":10}]"}};
    std::ofstream f(path, std::ios::binary);
    f << cen::census_json(rows, head, hits, rows, 1700000000000LL, 1234.5, role);
}

int main(int argc, char** argv)
{
    std::string dataDir = argc > 1 ? argv[1] : "..\\..\\data";
    std::string outDir = argc > 2 ? argv[2] : "out";

    {
        std::vector<cen::Cond> c;
        std::string e;
        check(cen::parse_where("a=1,b!=x,dist<5000,hp>=10", c, e) && c.size() == 4, "parse_where 4 conds");
        check(c[0].op == cen::EQ && c[1].op == cen::NE && c[2].op == cen::LT && c[3].op == cen::GE, "parse_where ops");
        check(!cen::parse_where("dist<abc", c, e), "non-numeric compare rejected");
        check(!cen::parse_where("=1", c, e), "empty field rejected");
        check(cen::parse_where("-", c, e) && c.empty(), "dash = no filter");
        cen::Row r("npc", "u1");
        r.b("alive", true).n("dist", 4000).s("cls", "X");
        std::vector<cen::Cond> w;
        cen::parse_where("alive=1,dist<5000,cls=X,missing!=1", w, e);
        check(cen::match(r, w), "match bool/num/str/missing!=");
        cen::parse_where("alive=0", w, e);
        check(!cen::match(r, w), "bool mismatch");
        cen::parse_where("missing=1", w, e);
        check(!cen::match(r, w), "missing field fails =");
        cen::parse_where("key=u1,fam=npc", w, e);
        check(cen::match(r, w), "key and fam pseudo fields");
    }

    std::vector<cen::Inv> inv;
    {
        std::string text = slurp((dataDir + "\\invariants.tsv").c_str());
        std::string e;
        check(!text.empty(), "invariants.tsv readable");
        check(cen::parse_inv(text, inv, e), ("parse invariants.tsv " + e).c_str());
        check(inv.size() >= 8, "invariants has the 8+ rules");
        std::string inc = slurp("..\\..\\src\\s2m_census_inv.inc");
        std::string t2 = text;
        std::string norm;
        for (char ch : t2)
            if (ch != '\r') norm += ch;
        check(inc == "R\"NOLAINV(" + norm + ")NOLAINV\"\n", "s2m_census_inv.inc matches data/invariants.tsv");
        std::vector<cen::Inv> bad;
        check(!cen::parse_inv("id\trole\tfam\twhere\tper\tmax\tgrace\tdetail\nx\tnobody\tnpc\t-\t-\t0\t0\t-\n", bad, e), "bad role rejected");
        check(!cen::parse_inv("x\tall\tnpc\t-\t-\tz\t0\t-\n", bad, e), "bad max rejected");
    }

    {
        std::vector<cen::Row> rows;
        rows.push_back(npc("u10", true, false, false, 900));
        rows.push_back(npc("u11", true, false, true, 900));
        rows.push_back(npc("u12", true, true, false, 900));
        rows.push_back(npc("u13", true, false, false, 9000));
        cen::InvState st;
        std::vector<cen::Hit> cur;
        auto f0 = st.eval(inv, rows, "client", 1, 100, &cur);
        bool unp = false;
        for (auto& h : cur)
            if (h.id == "unpaired_visible_npc" && h.gkey == "u10") unp = true;
        check(unp, "GH1 row u10 is a current violation");
        check(f0.empty(), "grace 15 s: nothing fired at first sight");
        auto f1 = st.eval(inv, rows, "client", 1, 110, &cur);
        check(f1.empty(), "still inside grace at +10 s");
        auto f2 = st.eval(inv, rows, "client", 1, 116, &cur);
        check(f2.size() == 1 && f2[0].id == "unpaired_visible_npc" && f2[0].gkey == "u10", "fires once after grace");
        check(f2.size() == 1 && f2[0].detail.find("cls=BP_AI_Human_C") != std::string::npos, "detail fields");
        auto f3 = st.eval(inv, rows, "client", 1, 130, &cur);
        check(f3.empty(), "fires only once while it stays broken");
        auto f4 = st.eval(inv, rows, "host", 1, 200, &cur);
        check(f4.empty(), "client-only rule skipped on host and state re-armed");
        st.eval(inv, rows, "client", 1, 300, &cur);
        auto f5 = st.eval(inv, rows, "client", 1, 316, &cur);
        check(f5.size() == 1, "re-armed after the row was gone");
        check(st.total() == 2, "total count");
    }

    {
        std::vector<cen::Row> rows;
        cen::Row a("npc", "nola:puppet:0");
        a.s("nolak", "puppet").b("alive", true).b("hidden", false).n("view", 0);
        cen::Row b("npc", "nola:puppet:orphan@1,2");
        b.s("nolak", "puppet").b("alive", true).b("hidden", false).n("view", -1).b("nola", true).b("tracked", false);
        rows.push_back(a);
        rows.push_back(b);
        cen::InvState st;
        std::vector<cen::Hit> cur;
        st.eval(inv, rows, "host", 1, 0, &cur);
        auto f = st.eval(inv, rows, "host", 1, 11, &cur);
        bool pup = false, orph = false;
        for (auto& h : f)
        {
            if (h.id == "one_puppet_per_peer" && h.detail.find("count=2 max=1") != std::string::npos) pup = true;
            if (h.id == "orphan_nola_body") orph = true;
        }
        check(pup, "two puppets for one live view fires one_puppet_per_peer");
        check(orph, "untracked nola actor fires orphan_nola_body");
        cen::InvState st2;
        st2.eval(inv, rows, "host", 2, 0, &cur);
        auto g = st2.eval(inv, rows, "host", 2, 11, &cur);
        bool pup2 = false;
        for (auto& h : g)
            if (h.id == "one_puppet_per_peer") pup2 = true;
        check(!pup2, "two puppets for two live views is fine");
    }

    {
        std::vector<cen::Row> rows;
        cen::Row e("ev", "1");
        e.s("ev", "lootclose").s("why", "taken_deny").s("k", "BP_Box_C@520,30,1").n("age", 0.5);
        cen::Row e2("ev", "2");
        e2.s("ev", "lootclose").s("why", "lock_deny").s("k", "x").n("age", 0.2);
        rows.push_back(e);
        rows.push_back(e2);
        cen::InvState st;
        auto f = st.eval(inv, rows, "host", 1, 5);
        check(f.size() == 1 && f[0].id == "loot_autoclose" && f[0].gkey == "1", "ST3 auto-close event fires at once, lock_deny does not");
    }

    {
        std::vector<cen::Row> a, b;
        a.push_back(std::move(cen::Row("quest", "Q1").n("n", 1)));
        a.push_back(std::move(cen::Row("quest", "Q2").n("n", 1)));
        b.push_back(std::move(cen::Row("quest", "Q2").n("n", 3)));
        b.push_back(std::move(cen::Row("quest", "Q1").n("n", 1)));
        std::vector<cen::Cond> none;
        int na = 0, nb = 0;
        check(cen::digest(a, "quest", none, {}, na) == cen::digest(b, "quest", none, {}, nb) && na == 2 && nb == 2, "digest ignores order and counts");
        b.push_back(std::move(cen::Row("quest", "Q3")));
        check(cen::digest(a, "quest", none, {}, na) != cen::digest(b, "quest", none, {}, nb), "digest sees a new node");
        std::string wh = cen::wh_line(a, 1700000000123LL, 55.7, "host");
        check(wh.rfind("NOLA WH 1700000000123 role=host t=55 npc=", 0) == 0 && wh.find(" quest=") != std::string::npos && wh.find("/2 taken=") != std::string::npos, "WH line format");
    }

    {
        cen::Row r("x", "k\"\\\n\x01");
        r.s("s", "a\tb").n("n", 1.5).b("b", true).n("i", 3);
        check(cen::row_json(r) == "{\"key\":\"k\\\"\\\\\\n\\u0001\",\"s\":\"a\\tb\",\"n\":1.500,\"b\":true,\"i\":3}", "row_json escaping");
        std::vector<cen::Row> rows = {cen::Row("npc", "u1"), cen::Row("npc", "u1"), cen::Row("npc", "u2")};
        cen::dedupe_keys(rows);
        check(rows[1].key == "u1#2" && rows[1].get("dupkey") && rows[2].key == "u2", "dedupe keys");
    }

    {
        std::vector<cen::Row> host, cli;
        auto add = [](std::vector<cen::Row>& v, cen::Row r) { v.push_back(std::move(r)); };
        add(host, npc("u100", true, false, false, 800));
        add(cli, npc("u100", true, false, true, 800));
        add(host, npc("u101", false, false, false, 900));
        add(cli, npc("u101", false, false, true, 900));
        cen::Row ghost = npc("u555", true, false, false, 1200);
        ghost.s("ai", "none");
        add(cli, ghost);
        add(host, cen::Row("lock", "100,200,30").b("locked", true));
        add(cli, cen::Row("lock", "100,200,30").b("locked", false));
        add(host, cen::Row("quest", "Q_Main_A").n("n", 1));
        add(host, cen::Row("quest", "Q_Main_B").n("n", 1));
        add(cli, cen::Row("quest", "Q_Main_A").n("n", 1));
        add(host, cen::Row("taken", "ctake@79E147E1411AB60C018BB79CCC1BB2B1;h1;A556Dx40").s("kind", "ctake").s("cont", "79E147E1411AB60C018BB79CCC1BB2B1").s("items", "A556Dx40"));
        add(cli, cen::Row("taken", "BP_Box_C@520,30,1").s("kind", "BP_Box_C"));
        add(host, cen::Row("window", "BP_Box_C@520,30,1").s("cls", "BP_Box_C").s("items", "EcoMedkitx2"));
        add(cli, cen::Row("window", "BP_Box_C@520,30,1").s("cls", "BP_Box_C").s("items", "A556Dx40,EcoMedkitx2"));
        add(host, cen::Row("body", "puppet:0").s("kind", "puppet").n("view", 0).b("valid", true).b("hidden", false));
        add(cli, cen::Row("body", "puppet:0").s("kind", "puppet").n("view", 0).b("valid", true).b("hidden", false));
        cen::Row ev("ev", "1");
        ev.s("ev", "lootclose").s("why", "taken_deny").s("k", "BP_Box_C@520,30,1").n("age", 2);
        add(cli, ev);
        cen::InvState st;
        std::vector<cen::Hit> hits;
        st.eval(inv, cli, "client", 1, 0, &hits);
        write((outDir + "\\fixture_host.json").c_str(), host, "host", {});
        write((outDir + "\\fixture_client.json").c_str(), cli, "client", hits);
        check(slurp((outDir + "\\fixture_client.json").c_str()).find("\"loot_autoclose\"") != std::string::npos, "client fixture carries in-mod INV hits");
    }

    std::printf("census logic: %d ok, %d fail\n", g_ok, g_fail);
    std::printf("%s\n", g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
