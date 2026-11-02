#include "nbot_play.h"
#include <cstdio>
#include <fstream>
#include <sstream>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static std::string slurp(const char* p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}

static int find(const std::vector<npl::Stage>& v, const std::string& st)
{
    for (size_t i = 0; i < v.size(); i++) if (v[i].stage == st) return (int)i;
    return -1;
}

int main(int argc, char** argv)
{
    std::string text = slurp(argc > 1 ? argv[1] : "quest_goals.tsv");
    auto st = npl::load_goals(text);
    CHECK(st.size() > 1000);
    int b = find(st, "E01_MQ01_Bunker");
    CHECK(b >= 0);
    if (b < 0) return 1;
    CHECK(st[b].kind == "trigger");
    CHECK(st[b].goalSid == "E01_MQ01_OnTriggerEnterEvent_InsideBunker");
    CHECK(st[b].gz > 100 && st[b].gz < 330);
    int ps = find(st, "E01_MQ01_PassSever");
    CHECK(ps >= 0 && st[ps].goalSid == "E01_MQ01_Trigger_SeverPassed");

    npl::World w;
    auto& q = w["E01_MQ01"];
    for (auto s : {"E01_MQ01_EnterTheZone_Start", "E01_MQ01_EnterTheZone_Finish", "E01_MQ01_EnterSever_Start", "E01_MQ01_EnterSever_Finish", "E01_MQ01_PassSever_Start"}) q.done.insert(s);
    for (auto s : {"E01_MQ01_Trigger_SeverPassed", "E01_MQ01_OnTriggerEnterEvent_InsideBunker", "E01_MQ01_OnTriggerEnterEvent_NearBunker"}) q.active.insert(s);
    w["SQ01"].active.insert("SQ01_Trigger_X");
    auto p = npl::pick_stage(st, w, "E01_MQ01", 348000, 642000);
    CHECK(p.idx == ps);

    npl::World w2 = w;
    auto& q2 = w2["E01_MQ01"];
    q2.active.erase("E01_MQ01_Trigger_SeverPassed");
    q2.done.insert("E01_MQ01_Trigger_SeverPassed");
    q2.done.insert("E01_MQ01_PassSever_Finish");
    q2.done.insert("E01_MQ01_Bunker_Start");
    p = npl::pick_stage(st, w2, "E01_MQ01", 348000, 642000);
    CHECK(p.idx == b);
    CHECK(!npl::stage_finished(st[b], q2));

    auto ch = npl::diff(w, w2);
    int ended = 0, opened = 0;
    for (auto& c : ch) { if (c.node == "E01_MQ01_Trigger_SeverPassed" && c.state == 3) ended++; if (c.state == 1) opened++; }
    CHECK(ended == 1);
    CHECK(opened == 0);

    npl::World w3 = w2;
    auto& q3 = w3["E01_MQ01"];
    q3.active.erase("E01_MQ01_OnTriggerEnterEvent_InsideBunker");
    q3.done.insert("E01_MQ01_OnTriggerEnterEvent_InsideBunker");
    CHECK(npl::stage_finished(st[b], q3));
    q3.done.insert("E01_MQ01_Bunker_Finish");
    p = npl::pick_stage(st, w3, "E01_MQ01", 0, 0);
    CHECK(p.idx != b);

    npl::World w4;
    w4["E01_MQ01"].active = q.active;
    p = npl::pick_stage(st, w4, "E01_MQ01", 366000, 628000);
    CHECK(p.idx == b);
    CHECK(p.why.find("fallback") == 0);

    CHECK(npl::node_of("E01_MQ01", "E01_MQ01_Bunker_Start"));
    CHECK(!npl::node_of("E01_MQ01", "None"));
    CHECK(!npl::node_of("E01_MQ01", "E01_MQ01"));
    CHECK(!npl::node_of("E01_MQ01", "E01_MQ01_x y"));

    {
        npl::P3 cur{348373, 642729, 779}, aim{st[b].mx, st[b].my, st[b].mz};
        CHECK(npl::dist2d(cur, aim) > 22000);
        npl::P3 o{cur.x + (aim.x - cur.x) * 0.35, cur.y + (aim.y - cur.y) * 0.35, 0};
        auto segHits = [&](npl::P3 a, npl::P3 q) {
            double vx = q.x - a.x, vy = q.y - a.y, L2 = vx * vx + vy * vy;
            double t = L2 > 0 ? ((o.x - a.x) * vx + (o.y - a.y) * vy) / L2 : 0;
            t = t < 0 ? 0 : (t > 1 ? 1 : t);
            return npl::dist2d(npl::P3{a.x + vx * t, a.y + vy * t, 0}, o) < 1500;
        };
        auto navOk = [&](npl::P3 a, npl::P3 q) { return npl::dist2d(a, q) <= 4000 && !segHits(a, q); };
        CHECK(!navOk(cur, aim));
        npl::StepProg sp;
        int steps = 0; bool ok = true, inObs = false;
        while (npl::dist2d(cur, aim) > 2500)
        {
            if (!sp.update(npl::dist2d(cur, aim))) { ok = false; break; }
            auto cs = npl::step_candidates(cur, aim, 3000);
            bool moved = false;
            for (auto& c : cs) if (navOk(cur, c)) { cur = c; moved = true; break; }
            if (!moved) { ok = false; break; }
            if (npl::dist2d(cur, o) < 1500) inObs = true;
            steps++;
        }
        CHECK(ok);
        CHECK(!inObs);
        CHECK(steps > 0 && steps < 20);
        printf("step sim: %d steps, end d=%.0f\n", steps, npl::dist2d(cur, aim));

        auto cs = npl::step_candidates(npl::P3{0, 0, 0}, npl::P3{10000, 0, 0}, 3000);
        CHECK(cs.size() == 14);
        CHECK(std::fabs(cs[0].x - 3000) < 1 && std::fabs(cs[0].y) < 1);
        CHECK(std::fabs(cs[7].x - 1500) < 1 && std::fabs(cs[7].y) < 1);
        auto near = npl::step_candidates(npl::P3{0, 0, 0}, npl::P3{1000, 0, 500}, 3000);
        CHECK(std::fabs(near[0].x - 1000) < 1 && std::fabs(near[0].z - 500) < 1);

        npl::P3 ls{348552, 642640, 807}, le{348552, 642640, 720}, la{366837, 628891, 1027};
        CHECK(!npl::step_gain(ls, le, la, 500));
        CHECK(npl::step_gain(ls, npl::P3{350950, 640837, 836}, la, 500));
        CHECK(!npl::same_layer(npl::P3{350950, 640837, 836}, npl::P3{350950, 640837, 100}, 300));
        CHECK(npl::same_layer(npl::P3{350950, 640837, 836}, npl::P3{350950, 640837, 700}, 300));

        npl::StepProg stuck;
        int n = 0; while (stuck.update(5000)) n++;
        CHECK(n == 8);
    }

    printf("%s fails=%d stages=%zu\n", fails ? "FAIL" : "PASS", fails, st.size());
    return fails ? 1 : 0;
}
