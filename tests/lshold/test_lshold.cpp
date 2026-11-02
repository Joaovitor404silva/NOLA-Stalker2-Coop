#include "s2m_lshold_logic.h"
#include <string>
#include "s2m_solo_logic.h"
#include "s2m_wpkg.h"

#include <cstdio>

static int fails = 0;
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}

static lsh::Gate ok()
{
    lsh::Gate g;
    g.flag = 1; g.sxjoin = 1; g.wpkg = 1;
    g.client = true; g.relay = true; g.joined = false; g.loadDelegate = true; g.loadBegan = true;
    g.code = 0; g.rawNum = 5000000; g.rawHead = 0xBB;
    return g;
}

int main()
{
    using lsh::Why;
    check(lsh::decide(ok()) == Why::hold, "joining client, load delegate, good raw: hold");
    { auto g = ok(); g.flag = 0; check(lsh::decide(g) == Why::off, "lshold=0: stock"); }
    { auto g = ok(); g.sxjoin = 0; check(lsh::decide(g) == Why::off, "sxjoin=0: stock"); }
    { auto g = ok(); g.wpkg = 0; check(lsh::decide(g) == Why::off, "wpkg=0: stock"); }
    { auto g = ok(); g.client = false; check(lsh::decide(g) == Why::host, "host: stock"); }
    { auto g = ok(); g.relay = false; check(lsh::decide(g) == Why::norelay, "no relay (solo): stock"); }
    { auto g = ok(); g.joined = true; check(lsh::decide(g) == Why::joined, "already in the host session: later loads stock"); }
    { auto g = ok(); g.loadDelegate = false; check(lsh::decide(g) == Why::notload, "other delegate: stock"); }
    { auto g = ok(); g.loadBegan = false; check(lsh::decide(g) == Why::nobegin, "menu save read with no world load begun: stock"); }
    { auto g = ok(); g.loadBegan = false; g.loadDelegate = false; check(lsh::decide(g) == Why::notload, "other delegate wins over nobegin"); }
    { auto g = ok(); g.loadBegan = false; g.relay = false; check(lsh::decide(g) == Why::nobegin, "main-menu save read without a relay: nobegin, not logged as norelay"); }
    { auto g = ok(); g.loadDelegate = false; g.relay = false; g.joined = true; check(lsh::decide(g) == Why::notload, "other delegate with no relay: notload"); }
    { auto g = ok(); g.code = 6; check(lsh::decide(g) == Why::badbuf, "read error code: stock"); }
    { auto g = ok(); g.rawNum = 100; check(lsh::decide(g) == Why::badbuf, "tiny raw: stock"); }
    { auto g = ok(); g.rawHead = 0xBA; check(lsh::decide(g) == Why::badbuf, "raw without save version: stock"); }
    { auto g = ok(); g.rawHead = 0xBE; check(lsh::decide(g) == Why::hold && lsh::strip_wanted(g, 1), "game 26123434 save version be: held like bb"); }
    { s2m_savefile::g_acceptU21 = false; auto g = ok(); g.rawHead = 0xBE; check(lsh::decide(g) == Why::badbuf, "savebe=0: version be is stock again"); s2m_savefile::g_acceptU21 = true; }

    check(lsh::strip_wanted(ok(), 1), "PL1 joining client world load: co-op bodies stripped");
    { auto g = ok(); g.client = false; check(lsh::strip_wanted(g, 1), "PL1 host world load: stripped"); }
    { auto g = ok(); g.client = false; g.relay = false; check(lsh::strip_wanted(g, 1), "PL1 solo world load: stripped"); }
    { auto g = ok(); g.joined = true; check(lsh::strip_wanted(g, 1), "PL1 client reload inside the session: stripped"); }
    { auto g = ok(); g.flag = 0; g.sxjoin = 0; g.wpkg = 0; check(lsh::strip_wanted(g, 1), "PL1 strip does not depend on the hold flags"); }
    check(!lsh::strip_wanted(ok(), 0), "PL1 ldstrip=0: save loads unchanged");
    { auto g = ok(); g.loadBegan = false; check(!lsh::strip_wanted(g, 1), "PL1 main-menu save read: not stripped"); }
    { auto g = ok(); g.loadDelegate = false; check(!lsh::strip_wanted(g, 1), "PL1 other delegate: not stripped"); }
    { auto g = ok(); g.code = 6; check(!lsh::strip_wanted(g, 1), "PL1 read error: not stripped"); }
    { auto g = ok(); g.rawNum = 100; check(!lsh::strip_wanted(g, 1), "PL1 tiny raw: not stripped"); }
    { auto g = ok(); g.rawHead = 0xBA; check(!lsh::strip_wanted(g, 1), "PL1 not a save: not stripped"); }
    check(lsh::step(10, 0, 120, 9, 20) == lsh::Step::wait, "in time, host sending: wait");
    check(lsh::step(121, 0, 120, 120, 20) == lsh::Step::timeout, "past lsholdwait: timeout");
    check(lsh::step(30, 0, 120, 5, 20) == lsh::Step::silent, "host silent 25 s: release");

    lsh::Ap a = lsh::parse_ap("#S2CA 1 abc123\nW\tabc123\n3\tq_a\n2\tq_b\n");
    check(a.stamped && a.cid && *a.cid == "abc123" && a.w && *a.w == "abc123" && a.n == 5, "stamped record with campaign");
    check(!lsh::is_other(a, "abc123", 1), "same campaign: not other");
    check(lsh::is_other(a, "zzz", 1), "other campaign: other");
    lsh::Ap b = lsh::parse_ap("#S2CA 1\n4\tq_a\n");
    check(b.stamped && !b.cid && b.n == 4 && lsh::is_other(b, "abc123", 0), "record without campaign and events: other");
    lsh::Ap c = lsh::parse_ap("#S2CA 1\r\n");
    check(!c.stamped && c.n == 0, "CR head is not the stamp");
    lsh::Ap d = lsh::parse_ap("");
    check(!d.cid && d.n == 0 && !lsh::is_other(d, "abc123", 0) && lsh::is_other(d, "abc123", 1), "no record: other only with cswc");
    lsh::Ap e = lsh::parse_ap("#S2CA 1 abc123\n7\tq_a\n");
    check(lsh::is_other(e, "abc123", 1) && !lsh::is_other(e, "abc123", 0), "no W line: other only with cswc (as n7boot)");
    std::string wid = s2m_wpkg::world_id(lsh::epoch_ms());
    std::printf("     world request id now: %s\n", wid.c_str());
    check(wid.size() == 14 && s2m_wpkg::is_world_id(wid), "epoch-ms request id is a world request id (14 chars, '9' + 13 digits)");
    check(!s2m_wpkg::is_world_id(s2m_wpkg::world_id(13436890393406LL)), "the 15-char id of the live run (uptime-based wall_ms) is not a world id");
    check(s2m_wpkg::is_world_id(s2m_wpkg::world_id(1792398000000LL)), "2026 epoch ms gives a world id");

    using namespace solo;
    check(eff_mask(0x60f, polStock, false) == 0x60f, "solo game: stock mask");
    check(eff_mask(0x60f, polHost, false) == 0x60d, "co-op host: Auto masked");
    check(eff_mask(0x60f, polHost, true) == 0x60f, "co-op host, join autosave pass: Auto through");
    check(eff_mask(0x60d, polStock, true) == 0x60f, "join pass also beats a stock mask without Auto (HostNoAutoTypeSave pak)");
    check(eff_mask(0x60d, polStock, false) == 0x60d, "no pass: stock mask kept");
    check(eff_mask(0x60f, polClient, false) == 0x402, "client: Auto only");
    PassCount pc;
    count_pass(pc, false, kAutoType, true);
    count_pass(pc, true, 2, true);
    check(pc.asked == 0 && pc.allowed == 0, "only Auto asks inside the pass window count");
    count_pass(pc, true, kAutoType, false);
    count_pass(pc, true, kAutoType, true);
    check(pc.asked == 2 && pc.allowed == 1, "pass window counts CanSaveNow(Auto) asked/allowed");
    std::printf("%s %d fail(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
