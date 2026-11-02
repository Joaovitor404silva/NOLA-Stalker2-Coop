#include <cstdio>
#include <string>
#include "../../src/s2m_qobj_logic.h"

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const char* kTab =
    "guid\tquest\tnode\n"
    "4F305B3D424CADC0568EB39626DD9553\tE01_MQ01_ScanerDialogDelay\tE01_MQ01_ScanerDialogDelay_OnInteractEvent_1\n"
    "03b8ecfb4753d87a53f67181954b4b6b\tE01_MQ01_ScanerDialogDelay\tE01_MQ01_ScanerDialogDelay_OnInteractEvent\r\n"
    "bad\tQ\tN\n";

static void t_table()
{
    qobj::Table t;
    CHECK(qobj::parse(kTab, t) == 2, "two rows parsed, the bad guid dropped");
    const qobj::Row* r = qobj::lookup(t, "4F305B3D424CADC0568EB39626DD9553");
    CHECK(r && r->quest == "E01_MQ01_ScanerDialogDelay" && r->node == "E01_MQ01_ScanerDialogDelay_OnInteractEvent_1", "scanner guid -> mission and node");
    r = qobj::lookup(t, "03B8ECFB4753D87A53F67181954B4B6B");
    CHECK(r && r->node == "E01_MQ01_ScanerDialogDelay_OnInteractEvent", "lowercase row, CRLF stripped");
    CHECK(qobj::lookup(t, "00000000000000000000000000000000") == nullptr, "a door with no quest guid is not an objective");
    CHECK(qobj::guid_norm("4f305b3d-424c-adc0-568e-b39626dd9553") == "4F305B3D424CADC0568EB39626DD9553", "dashed form normalized");
}

static void t_guid()
{
    qobj::Table t;
    qobj::parse(kTab, t);
    std::string comp = qobj::guid_hex(1328569149, 1112321472, 1452192662, 652055891);
    CHECK(comp == "4F305B3D424CADC0568EB39626DD9553", "live SingleClickComponent Guid A..D -> the quest InteractableQuestGuid (%s)", comp.c_str());
    std::string actor = qobj::guid_hex(1581033383, 1186594104, -1282653031, -586573212);
    CHECK(actor == "5E3CA7A746B9FD38B38C4499DD099A64", "live InteractableObject::Guid is another guid (%s)", actor.c_str());
    std::vector<std::string> gs{actor, qobj::guid_hex(-704890450, 1087879755, 532747659, -222662913), comp};
    const qobj::Row* r = qobj::lookup_any(t, gs);
    CHECK(r && r->node == "E01_MQ01_ScanerDialogDelay_OnInteractEvent_1", "one of the actor's GuidActorComponent guids matches the table");
    gs.pop_back();
    CHECK(qobj::lookup_any(t, gs) == nullptr, "actor guid and scanner component guid alone match nothing");
    CHECK(qobj::lookup_any(t, {}) == nullptr, "no components: no objective");
}

static void t_block()
{
    qobj::Row r{"E01_MQ01_ScanerDialogDelay", "n"};
    CHECK(qobj::block(true, true, &r, true), "client touch on a host-only objective is blocked");
    CHECK(!qobj::block(true, false, &r, true), "the host is never blocked");
    CHECK(!qobj::block(false, true, &r, true), "qhostonly=0 keeps the old behaviour");
    CHECK(!qobj::block(true, true, nullptr, true), "no quest guid: door, loot, ladder stay usable");
    CHECK(!qobj::block(true, true, &r, false), "an objective of a mission the client runs itself is not blocked");
}

static void t_warn()
{
    qobj::Gate g;
    CHECK(qobj::warn_due(g, 10.0, 3.0), "first block warns");
    CHECK(!qobj::warn_due(g, 11.0, 3.0), "second block inside 3 s is silent");
    CHECK(!qobj::warn_due(g, 12.9, 3.0), "still silent at 2.9 s");
    CHECK(qobj::warn_due(g, 13.0, 3.0), "warns again at 3 s");
    CHECK(g.blocked == 4 && g.warns == 2, "bloqueados=4 avisos=2 (%d %d)", g.blocked, g.warns);
}

static void t_state()
{
    qobj::ObjState s;
    s.hid = 0; s.en = 1; s.br = 0; s.st = 2;
    CHECK(qobj::wire("37362,60358,12", s) == "37362,60358,12;0,1,0,2", "wire entry key;hid,en,br,st (%s)", qobj::wire("37362,60358,12", s).c_str());
    std::string k;
    qobj::ObjState d;
    CHECK(qobj::wire_parse("37362,60358,12;0,1,0,2", k, d) && k == "37362,60358,12" && d.hid == 0 && d.en == 1 && d.br == 0 && d.st == 2, "wire entry parsed back");
    CHECK(qobj::wire_parse("1,2,3;1,0,1,-", k, d) && d.hid == 1 && d.br == 1 && d.st == -1, "no scanner component -> estado -");
    CHECK(!qobj::wire_parse("1,2,3", k, d), "entry without state refused");
    CHECK(!qobj::wire_parse("1,2,3;x,1,0,2", k, d), "bad digit refused");
    CHECK(qobj::line("BP_TopazScanner_Prologue_C", "37362,60358,12", s) == "NOLA objstate BP_TopazScanner_Prologue_C 37362,60358,12 vis=1 ativo=1 quebrado=0 estado=2", "log line (%s)", qobj::line("BP_TopazScanner_Prologue_C", "37362,60358,12", s).c_str());
    CHECK(qobj::skip_sync(true, true), "an objective actor leaves the world sync");
    CHECK(!qobj::skip_sync(false, true), "qobjsync=0 keeps the old behaviour");
    CHECK(!qobj::skip_sync(true, false), "a door is still synced");
}

static void t_plan()
{
    qobj::ObjState have, want;
    have.hid = 1; have.en = 0; have.br = 1; have.st = 1;
    want.hid = 0; want.en = 1; want.br = 0; want.st = 3;
    std::vector<qobj::Step> calls;
    int steps = qobj::plan(have, want, calls);
    CHECK(calls.size() == 3, "three bool calls (%d)", (int)calls.size());
    CHECK(calls.size() == 3 && calls[0].fn == "SetActorHiddenInGame" && !calls[0].arg, "client shows the prop the host shows");
    CHECK(calls.size() == 3 && calls[1].fn == "SetEnabled" && calls[1].arg, "client enables what the host enabled");
    CHECK(calls.size() == 3 && calls[2].fn == "SetBroken" && !calls[2].arg, "client unbreaks what the host did not break");
    CHECK(steps == 2, "Inactive(1) -> Active(3) is two ChangeState steps (%d)", steps);
    calls.clear();
    CHECK(qobj::plan(want, want, calls) == 0 && calls.empty(), "equal state: nothing to do");
    have = want;
    have.st = 4;
    calls.clear();
    CHECK(qobj::plan(have, want, calls) == 0, "a state ahead of the host is never stepped back");
    have.st = -1;
    CHECK(qobj::plan(have, want, calls) == 0, "no scanner component: no ChangeState");
    have.st = 0;
    CHECK(qobj::plan(have, want, calls) == 3, "None(0) -> Active(3) is three steps");
}

static void t_cache()
{
    qobj::Table t;
    qobj::parse(kTab, t);
    const qobj::Row* sc = qobj::lookup(t, "4F305B3D424CADC0568EB39626DD9553");
    qobj::RowCache c;
    const qobj::Row* r = nullptr;
    CHECK(!qobj::cache_get(c, 0x1000, "BP_TopazScanner_Prologue_C_UAID_1", r), "empty cache misses");
    qobj::cache_put(c, 0x1000, "BP_TopazScanner_Prologue_C_UAID_1", sc);
    CHECK(qobj::cache_get(c, 0x1000, "BP_TopazScanner_Prologue_C_UAID_1", r) && r == sc, "same address and name hits");
    CHECK(!qobj::cache_get(c, 0x1000, "BP_Lever_Horizontal_C_UAID_7", r), "another actor at the reused address misses");
    qobj::cache_put(c, 0x2000, "BP_Lever_Horizontal_C_UAID_7", nullptr);
    CHECK(!qobj::cache_get(c, 0x2000, "BP_TopazScanner_Prologue_C_UAID_1", r), "an objective at a cached non-objective address misses");
    for (uint64_t a = 0; a < 5000; ++a) qobj::cache_put(c, 0x10000 + a, "X", nullptr);
    CHECK(c.size() <= 4097, "cache stays bounded (%d)", (int)c.size());
}

int main()
{
    t_cache();
    t_table();
    t_guid();
    t_block();
    t_warn();
    t_state();
    t_plan();
    std::printf("test_nat_qobj: %d ok, %d fail\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
