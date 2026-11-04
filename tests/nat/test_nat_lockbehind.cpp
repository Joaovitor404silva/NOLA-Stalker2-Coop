#include <cstdio>
#include <string>
#include <vector>
#include "../../src/s2m_lockbehind_logic.h"

static int g_fail = 0, g_ok = 0;
#define CHECK(c, ...) do { if (c) ++g_ok; else { ++g_fail; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const char* kTab =
    "quest\ttrigger\tnode\ttrigguid\treceiver\tkind\tgeo\n"
    "E03_MQ02\tE03_MQ02_Trigger_ToLockDoor\tE03_MQ02_SendSignal_CloseLabDoor\t6AECDAB041291C3878348A83FCCFCD3C\tCF46E2ED4ACF6B69A7A253BAD606DE65\tclose\t1\r\n"
    "E09_MQ01\tE09_MQ01_Trigger_Dal\tE09_MQ01_SendSignal_BP_DoorView.CloseDoorReceiver\t11111111111111111111111111111111\t\tclose\t0\n"
    "E08_MQ03\tE08_MQ03_OnTriggerEnterEvent_PCUnderAgroprom_4\tE08_MQ03_SendSignal_BP_CardlockX5ReceiverOn\t22222222222222222222222222222222\t\tambiguous\t1\n"
    "E08_MQ05\tE08_MQ05_OnTriggerEnterEvent_RatsAttack\tE08_MQ05_SendSignal_BP_Military_Radio_E08_iskra.ReceiverOff\t33333333333333333333333333333333\t\toff\t1\n";

static const char* kTrig =
    "E03_MQ02_Trigger_ToLockDoor\tE03_MQ02\t424008.0\t476926.0\t206.0\t0.0\t50.0\t50.0\t100.0\tBox\t6AECDAB041291C3878348A83FCCFCD3C\n"
    "E03_MQ02_OnTriggerEnterEvent_Other\tE03_MQ02\t1.0\t2.0\t3.0\t0.0\t50.0\t50.0\t100.0\tBox\t60510A124D0171CDC726B3804A09384F\n";

static void t_table()
{
    lockbehind::Table t;
    CHECK(lockbehind::parse(kTab, t) == 1, "only close rows with trigger geometry are active");
    CHECK(t.byNode.count("e03_mq02_sendsignal_closelabdoor") == 1, "node key lowercased, CRLF stripped");
    CHECK(t.byNode.count("e09_mq01_sendsignal_bp_doorview.closedoorreceiver") == 0, "geo=0 row skipped");
    CHECK(t.trigs.size() == 1 && *t.trigs.begin() == "E03_MQ02_Trigger_ToLockDoor", "trigger sid kept as written");
    std::unordered_map<std::string, lockbehind::Vol> vols;
    CHECK(lockbehind::parse_vols(kTrig, t.trigs, vols) == 1, "only the listed trigger volume is loaded");
    const lockbehind::Vol& v = vols["E03_MQ02_Trigger_ToLockDoor"];
    CHECK(v.x == 424008.0 && v.sx == 50.0 && v.sz == 100.0 && v.box, "box geometry from s2u_triggers.tsv");
    CHECK(lockbehind::inside(v, 424008, 476926, 206, 150), "centre is inside");
    CHECK(lockbehind::inside(v, 424190, 476926, 206, 150), "within size + margin is inside");
    CHECK(!lockbehind::inside(v, 424562, 477462, 205, 150), "the yard spot outside the door is outside");
    lockbehind::Vol r;
    r.x = 0; r.y = 0; r.z = 0; r.yaw = 90; r.sx = 1000; r.sy = 10;
    CHECK(lockbehind::inside(r, 0, 500, 0, 0) && !lockbehind::inside(r, 500, 0, 0, 0), "yaw rotates the box");
}

static const std::string kTrigSid = "E03_MQ02_Trigger_ToLockDoor";
static const std::string kNode = "e03_mq02_sendsignal_closelabdoor";

struct Fx
{
    lockbehind::Table t;
    lockbehind::Vols vols;
    Fx()
    {
        lockbehind::parse(kTab, t);
        lockbehind::parse_vols(kTrig, t.trigs, vols);
    }
};

static lockbehind::Pos kIn(int v) { return {v, 424008, 476926, 206}; }
static lockbehind::Pos kOut(int v) { return {v, 424562, 477462, 205}; }

static void t_gate()
{
    Fx f;
    lockbehind::Gate g;
    bool fresh = false;
    std::vector<int> solo{lockbehind::kHost};
    std::vector<lockbehind::Pos> none;
    CHECK(lockbehind::hold(f.t, g, kNode, solo, 1, fresh, f.vols, {kIn(lockbehind::kHost)}, 150) == nullptr, "solo host: stock, never held");
    CHECK(lockbehind::hold(f.t, g, "e03_mq02_sendsignal_2_18_e03_mq02_bp_bunkerdoor_factory_01.receiveron", solo, 1, fresh, f.vols, none, 150) == nullptr, "a node outside the table is never held");

    std::vector<int> two{lockbehind::kHost, 0};
    std::vector<lockbehind::Pos> hostIn{kIn(lockbehind::kHost), kOut(0)};
    const lockbehind::Row* r = lockbehind::hold(f.t, g, kNode, two, 2, fresh, f.vols, hostIn, 150);
    CHECK(r && fresh && r->trig == kTrigSid && g.pend.size() == 1, "host inside, guest outside: node held once");
    CHECK(lockbehind::hold(f.t, g, kNode, two, 3, fresh, f.vols, hostIn, 150) == r && !fresh && g.pend.size() == 1, "second launch of the same node: still held, no duplicate");
    CHECK(lockbehind::count_in(g, kTrigSid, two) == 1, "1 of 2 inside");
    lockbehind::sample(g, f.vols, hostIn, 150);
    CHECK(lockbehind::release(g, two).empty() && g.pend.size() == 1, "nothing released while the guest is outside");
    lockbehind::sample(g, f.vols, {kIn(0)}, 150);
    auto rel = lockbehind::release(g, two);
    CHECK(rel.size() == 1 && rel[0].node == "E03_MQ02_SendSignal_CloseLabDoor" && rel[0].quest == "E03_MQ02" && g.pend.empty(), "guest enters: node released with its original sid");
    CHECK(g.entered.count(kTrigSid) == 0, "latch cleared after the release");

    lockbehind::Gate g2;
    CHECK(lockbehind::hold(f.t, g2, kNode, two, 4, fresh, f.vols, {kOut(lockbehind::kHost), kIn(0)}, 150) != nullptr, "guest inside, host outside: held");
    CHECK(lockbehind::hold(f.t, g2, kNode, two, 5, fresh, f.vols, {kIn(lockbehind::kHost)}, 150) == nullptr, "everyone entered while held: relaunch passes through");
    CHECK(lockbehind::release(g2, two).size() == 1, "the earlier held copy is released too");

    lockbehind::Gate g3;
    std::vector<int> five{lockbehind::kHost, 0, 1, 2, 3};
    std::vector<lockbehind::Pos> p5{kIn(lockbehind::kHost), kIn(0), kIn(1), kOut(2), kOut(3)};
    CHECK(lockbehind::hold(f.t, g3, kNode, five, 6, fresh, f.vols, p5, 150) != nullptr && lockbehind::count_in(g3, kTrigSid, five) == 3, "host + 4 clients: 3 of 5 inside, held");
    std::vector<int> three{lockbehind::kHost, 0, 1};
    lockbehind::prune(g3, three);
    CHECK(lockbehind::release(g3, three).size() == 1, "two clients disconnect: only connected players count, released");

    lockbehind::Gate g4;
    lockbehind::mark(g4, kTrigSid, 2);
    lockbehind::prune(g4, two);
    CHECK(lockbehind::count_in(g4, kTrigSid, two) == 0 && g4.entered[kTrigSid].count(2) == 0, "a view that left is pruned from the latch");
}

static void t_decide()
{
    Fx f;
    std::vector<int> two{lockbehind::kHost, 0};
    bool fresh = false;

    lockbehind::Gate g;
    std::vector<lockbehind::Pos> pos{kIn(lockbehind::kHost), kOut(0)};
    const lockbehind::Row* r = lockbehind::hold(f.t, g, kNode, two, 1, fresh, f.vols, pos, 150);
    CHECK(r && fresh && lockbehind::count_in(g, kTrigSid, two) == 1, "host teleported in before the tick: sampled at decision time, 1 of 2 inside");
    CHECK(lockbehind::hold(f.t, g, kNode + "_s2q", two, 1, fresh, f.vols, pos, 150) == r && fresh && g.pend.size() == 2, "the host mirror node _S2Q is held with the stock node");
    CHECK(g.pend[1].node == "E03_MQ02_SendSignal_CloseLabDoor_S2Q" && g.pend[1].trig == kTrigSid, "mirror pend keeps its own sid");
    CHECK(lockbehind::hold(f.t, g, kNode + "_s2q", two, 2, fresh, f.vols, pos, 150) == r && !fresh && g.pend.size() == 2, "mirror relaunch: no duplicate");
    lockbehind::sample(g, f.vols, {kIn(0)}, 150);
    auto rel = lockbehind::release(g, two);
    CHECK(rel.size() == 2 && rel[0].node == "E03_MQ02_SendSignal_CloseLabDoor" && rel[1].node == "E03_MQ02_SendSignal_CloseLabDoor_S2Q", "guest enters: stock node and mirror released once each");

    lockbehind::Gate s;
    std::vector<int> solo{lockbehind::kHost};
    CHECK(lockbehind::hold(f.t, s, kNode + "_s2q", solo, 1, fresh, f.vols, pos, 150) == nullptr, "solo host: mirror never held");
    CHECK(lockbehind::hold(f.t, s, "e09_mq01_sendsignal_bp_doorview.closedoorreceiver_s2q", two, 1, fresh, f.vols, pos, 150) == nullptr, "mirror of a node outside the table: untouched");
}

static void t_multi()
{
    const std::string tab = std::string(kTab) +
        "E06_MQ01_C04\tE06_MQ01_C04_Trigger_A\tE06_MQ01_C04_SendSignal_Door_Exp1ForceClose\t44444444444444444444444444444444\t\tclose\t1\n"
        "E06_MQ01_C04\tE06_MQ01_C04_Trigger_B\tE06_MQ01_C04_SendSignal_Door_Exp1ForceClose\t55555555555555555555555555555555\t\tclose\t1\n"
        "E06_MQ01_C04\tE06_MQ01_C04_Trigger_C\tE06_MQ01_C04_SendSignal_Door_Exp1ForceClose\t66666666666666666666666666666666\t\tclose\t1\n"
        "E06_MQ01_C06\tE06_MQ01_C06_Trigger_A\tE06_MQ01_C06_SendSignal_CloseFirst_1\t77777777777777777777777777777777\t\tclose\t1\n"
        "E06_MQ01_C06\tE06_MQ01_C06_Trigger_A\tE06_MQ01_C06_SendSignal_CloseFirst_1\t77777777777777777777777777777777\t\tclose\t1\n";
    lockbehind::Table t;
    CHECK(lockbehind::parse(tab, t) == 2, "a node launched by several triggers is dropped, a repeated identical row is kept once");
    CHECK(t.byNode.count("e06_mq01_c04_sendsignal_door_exp1forceclose") == 0 && t.multi == 1, "multi-trigger node stays stock (counted)");
    CHECK(t.byNode.count("e06_mq01_c06_sendsignal_closefirst_1") == 1, "same node, same trigger twice: still gated");
    CHECK(t.trigs.count("E06_MQ01_C04_Trigger_A") == 0 && t.trigs.count("E06_MQ01_C04_Trigger_C") == 0, "triggers of a dropped node are not loaded");
    lockbehind::Gate g;
    bool fresh = false;
    std::vector<int> two{lockbehind::kHost, 0};
    CHECK(lockbehind::hold(t, g, "e06_mq01_c04_sendsignal_door_exp1forceclose", two, 1, fresh, {}, {}, 150) == nullptr, "multi-trigger node is never held");
}

static void t_fresh_reset()
{
    Fx f;
    std::vector<int> two{lockbehind::kHost, 0};
    bool fresh = false;
    lockbehind::Gate g;
    lockbehind::sample(g, f.vols, {kIn(0)}, 150);
    CHECK(g.entered.empty(), "no hold pending: positions are not latched");
    lockbehind::mark(g, kTrigSid, 0);
    CHECK(lockbehind::hold(f.t, g, kNode, two, 1, fresh, f.vols, {kIn(lockbehind::kHost), kOut(0)}, 150) != nullptr && fresh, "guest passed through earlier and stepped back out: held");
    CHECK(lockbehind::count_in(g, kTrigSid, two) == 1, "fresh hold resets the latch to who is inside now");
    lockbehind::sample(g, f.vols, {kIn(0)}, 150);
    lockbehind::sample(g, f.vols, {kOut(0)}, 150);
    CHECK(lockbehind::release(g, two).size() == 1, "while held, a guest who entered counts even after stepping out");
}

static void t_live_and_drain()
{
    CHECK(lockbehind::live_peer(false, true, false, false), "up peer is live");
    CHECK(lockbehind::live_peer(false, false, true, false), "silent peer that has not left is live (hitch)");
    CHECK(!lockbehind::live_peer(false, false, true, true), "silent peer that left is not live");
    CHECK(!lockbehind::live_peer(true, true, false, false), "loopback is not live");
    CHECK(!lockbehind::live_peer(false, false, false, false), "none is not live");

    Fx f;
    std::vector<int> two{lockbehind::kHost, 0};
    bool fresh = false;
    lockbehind::Gate g;
    std::vector<lockbehind::Pos> pos{kIn(lockbehind::kHost), kOut(0)};
    lockbehind::hold(f.t, g, kNode, two, 1, fresh, f.vols, pos, 150);
    lockbehind::hold(f.t, g, kNode + "_s2q", two, 1, fresh, f.vols, pos, 150);
    auto all = lockbehind::drain(g);
    CHECK(all.size() == 2 && g.pend.empty() && g.entered.empty(), "drain (save, world exit, reload) returns every held node and clears the gate");
    CHECK(lockbehind::drain(g).empty(), "second drain: nothing left, nothing runs twice");
}

int main()
{
    t_table();
    t_gate();
    t_decide();
    t_multi();
    t_fresh_reset();
    t_live_and_drain();
    std::printf("test_nat_lockbehind: %d ok, %d fail\n", g_ok, g_fail);
    return g_fail ? 1 : 0;
}
