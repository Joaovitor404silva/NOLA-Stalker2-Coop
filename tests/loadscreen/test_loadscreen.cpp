#include "s2m_loadscreen_logic.h"

#include <cstdint>
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

int main()
{
    using lsx::Why;
    check(lsx::decide(1, true, true, false, false, true) == Why::skip, "client in session, press-any-button branch: skip");
    check(lsx::decide(1, true, false, true, false, true) == Why::skip, "client, peer silent during the load and not left: skip");
    check(lsx::decide(1, true, false, true, true, true) == Why::solo, "client, peer silent and left: stock");
    check(lsx::decide(1, true, false, false, false, true) == Why::solo, "client solo (no peer): stock");
    check(lsx::decide(1, false, true, false, false, true) == Why::host, "host in session: stock");
    check(lsx::decide(1, false, false, false, false, true) == Why::host, "host solo: stock");
    check(lsx::decide(0, true, true, false, false, true) == Why::off, "lsskip=0: stock");
    check(lsx::decide(2, true, true, false, false, true) == Why::off, "lsskip other than 1: stock");
    check(lsx::decide(1, true, true, false, false, false) == Why::nowait, "screen already auto-completes: nothing to do");
    check(lsx::waits(1, 0) && lsx::waits(0, 1) && !lsx::waits(0, 0), "wait branch = gameflow +0x31 || +0x32");
    check(lsx::peer_in(true, false, true) && !lsx::peer_in(false, false, false), "peer_in");

    using lsx::Hw;
    using lsx::Hold;
    {
        lsx::HostW h;
        check(!lsx::host_step(h, 0, 0, 0, -1, -1) && h.st == Hw::loading, "host: no pawn = loading");
        check(lsx::host_step(h, 1, 0x100, 1, 0, 3) && h.st == Hw::held, "host: new pawn, ScenarioManager tick off = held (press-any-key screen)");
        check(!lsx::host_step(h, 5, 0x100, 1, 0, 3) && h.st == Hw::held && h.epoch == 0, "host: still on its loading screen = held");
        check(!lsx::host_step(h, 6, 0x100, 1, 1, 2) && h.st == Hw::held, "host: tick on but quest starts pending = held");
        check(lsx::host_step(h, 6.5, 0x100, 1, 1, 0) && h.st == Hw::up && h.epoch == 1, "host: tick on and quest queue drained = up, epoch 1");
        check(lsx::hw_str(h) == "1,3", "host world string epoch,state");
        check(lsx::host_step(h, 20, 0x100, 2, 1, 0) && h.st == Hw::loading, "host: load begin count rose with the old pawn still there = loading");
        check(!lsx::host_step(h, 22, 0, 2, -1, -1) && h.st == Hw::loading, "host: pawn gone during the load = loading");
        check(lsx::host_step(h, 30, 0x100, 2, 0, 5) && h.st == Hw::held, "host: same pawn address reused after the load = held again");
        check(lsx::host_step(h, 31, 0x100, 2, 1, 0) && h.st == Hw::up && h.epoch == 2, "host: up again, epoch 2");
        check(lsx::host_step(h, 40, 0x100, 3, 1, 0) && h.st == Hw::loading, "host: load begin");
        check(!lsx::host_step(h, 60, 0x100, 3, 1, 0) && h.st == Hw::loading, "host: begin with pawn kept, under the stuck limit = loading");
        check(lsx::host_step(h, 71, 0x100, 3, 1, 0) && h.st == Hw::up && h.epoch == 3, "host: load begin that never dropped the pawn (cancelled) falls back after 30 s");
    }
    {
        lsx::HostW h;
        lsx::host_step(h, 0, 0x200, 0, -1, -1);
        check(h.st == Hw::held, "host without ScenarioManager: held first");
        check(!lsx::host_step(h, 9, 0x200, 0, -1, -1) && h.st == Hw::held, "host without ScenarioManager: held under 10 s");
        check(lsx::host_step(h, 10, 0x200, 0, -1, -1) && h.st == Hw::up, "host without ScenarioManager: up after the pawn is 10 s stable");
    }
    {
        lsx::HostW h;
        lsx::host_step(h, 0, 0x300, 0, 1, 4);
        check(h.st == Hw::held, "host: quest queue never drains: held");
        check(!lsx::host_step(h, 29, 0x300, 0, 1, 4) && h.st == Hw::held, "host: still held at 29 s of tick");
        check(lsx::host_step(h, 30, 0x300, 0, 1, 4) && h.st == Hw::up, "host: up after 30 s of tick even with a stuck quest queue");
    }
    {
        lsx::HostW h;
        lsx::host_step(h, 200, 0x400, 0, 0, 530);
        check(h.st == Hw::held, "host: after a load, tick 0 and 530 quest starts pending: held");
        check(!lsx::host_step(h, 289, 0x400, 0, 0, 530) && h.st == Hw::held, "host: tick 0 at pawnAt+89: still held");
        check(!lsx::host_step(h, 290, 0x400, 0, 0, 530) && h.st == Hw::held, "host: tick 0 at pawnAt+90: still held (press-any-button screen, no cap)");
        check(!lsx::host_step(h, 500, 0x400, 0, 0, 530) && h.st == Hw::held && h.epoch == 0, "host: tick 0 at pawnAt+300: still held, the key has not been pressed");
        check(lsx::host_step(h, 501, 0x400, 0, 1, 0) && h.st == Hw::up && h.epoch == 1, "host: key pressed, tick 1 and queue drained = up");
        lsx::HostW k;
        lsx::host_step(k, 0, 0x500, 0, 1, 1, 10, 30, 30, 20);
        check(!lsx::host_step(k, 19, 0x500, 0, 1, 1, 10, 30, 30, 20) && k.st == Hw::held && lsx::host_step(k, 20, 0x500, 0, 1, 1, 10, 30, 30, 20) && k.st == Hw::up, "host: tick on, queue stuck: lshostcap=20 leaves held at +20");
        lsx::HostW z;
        lsx::host_step(z, 0, 0x600, 0, 0, 1, 10, 30, 30, 20);
        check(!lsx::host_step(z, 20, 0x600, 0, 0, 1, 10, 30, 30, 20) && !lsx::host_step(z, 200, 0x600, 0, 0, 1, 10, 30, 30, 20) && z.st == Hw::held, "host: tick 0: lshostcap never applies");
        check(lsx::decide(1, false, true, false, false, true, true) == Why::skip, "host in session with lshostskip: skip the press-any-button screen");
        check(lsx::decide(1, false, true, false, false, false, true) == Why::nowait, "host with lshostskip, screen auto-completes: nothing to do");
        check(lsx::decide(0, false, true, false, false, true, true) == Why::off, "lsskip=0 keeps the host screen stock even with lshostskip");
        check(lsx::decide(1, false, false, false, false, true, false) == Why::host, "host without lshostskip (solo or flag 0): stock");
    }
    {
        uint32_t e = 0;
        check(lsx::hw_parse("7,3", &e) == Hw::up && e == 7, "parse up");
        check(lsx::hw_parse("12,1", &e) == Hw::loading && e == 12, "parse loading");
        check(lsx::hw_parse("0,2") == Hw::held, "parse held");
        check(lsx::hw_parse("") == Hw::none, "old host without the roster field: none");
        check(lsx::hw_parse("3,4") == Hw::none && lsx::hw_parse(",3") == Hw::none && lsx::hw_parse("a,3") == Hw::none && lsx::hw_parse("3,33") == Hw::none && lsx::hw_parse("3") == Hw::none, "parse rejects junk");
    }
    check(lsx::arm(1, true, true, true), "arm: client in a relay session with a pawn");
    check(!lsx::arm(0, true, true, true), "arm: lsgate=0");
    check(!lsx::arm(1, false, true, true), "arm: host never holds");
    check(!lsx::arm(1, true, false, true), "arm: solo client (no relay) keeps the stock screen");
    check(!lsx::arm(1, true, true, false), "arm: main-menu load (no pawn) is not held");
    auto in0 = [] {
        lsx::In i;
        i.t = 100;
        i.t0 = 100;
        i.hostAt = 100;
        i.rxAt = 100;
        return i;
    };
    {
        lsx::In i = in0();
        i.flag = 0;
        check(lsx::gate(i) == Hold::off, "gate: flag off");
        i = in0();
        i.client = false;
        check(lsx::gate(i) == Hold::off, "gate: role switched to host");
        i = in0();
        i.relay = false;
        check(lsx::gate(i) == Hold::off, "gate: relay ended");
    }
    {
        lsx::In i = in0();
        i.host = Hw::up;
        i.up = false;
        check(lsx::gate(i) == Hold::peer, "first join: load done before PEER UP: wait for the host signal");
        i.t = 219;
        i.rxAt = 219;
        check(lsx::gate(i) == Hold::peer, "first join: still waiting under lsholdwait");
        i.t = 221;
        i.rxAt = 221;
        check(lsx::gate(i) == Hold::gone, "first join: no PEER UP for lsholdwait: gone");
    }
    {
        lsx::Up u;
        lsx::In i = in0();
        i.host = Hw::up;
        i.up = true;
        i.partB = true;
        lsx::up_step(u, 100, true, 50);
        i.upAt = u.at;
        i.frames = u.frames;
        check(lsx::gate(i) == Hold::sync, "death reload, host up: wait for fresh frames (a stale up is not enough)");
        lsx::up_step(u, 100.1, true, 50);
        lsx::up_step(u, 100.2, true, 51);
        i.t = 100.2;
        i.frames = u.frames;
        check(lsx::gate(i) == Hold::sync, "one fresh frame: still sync");
        lsx::up_step(u, 100.3, true, 52);
        i.t = 100.3;
        i.frames = u.frames;
        check(u.frames == 2 && lsx::gate(i) == Hold::go, "two fresh frames with quests (B): go");
        i.natnpc = true;
        check(lsx::gate(i) == Hold::sync, "natnpc on and no NPC list yet: sync");
        i.partNN = true;
        check(lsx::gate(i) == Hold::go, "NPC list arrived: go");
        i.partNN = false;
        i.t = 110.1;
        i.rxAt = 110.1;
        check(lsx::gate(i) == Hold::go, "NPC list never comes (host has no NPCs near): go after lssync");
        lsx::up_step(u, 111, false, 52);
        check(u.at < 0 && u.frames == 0, "host leaves up: frame count resets");
    }
    {
        lsx::In i = in0();
        i.host = Hw::loading;
        i.up = false;
        check(lsx::gate(i) == Hold::host, "host load followed: host still loading");
        i.t = 150;
        i.hostAt = 140;
        i.rxAt = 150;
        check(lsx::gate(i) == Hold::host, "slow host load: still waiting");
        i.t = 261;
        i.rxAt = 261;
        check(lsx::gate(i) == Hold::gone, "host stuck loading for lsholdwait after its last change: gone");
        i = in0();
        i.host = Hw::held;
        i.t = 1000;
        i.rxAt = 1000;
        check(lsx::gate(i) == Hold::enter, "host waits on its own press-any-key screen: the client waits too, no timeout while the relay answers");
        i.rxAt = 979;
        check(lsx::gate(i) == Hold::gone, "relay silent for lssilent: gone");
    }
    {
        lsx::In i = in0();
        i.host = Hw::none;
        i.up = true;
        i.partB = true;
        i.upAt = 100;
        i.frames = 3;
        check(lsx::gate(i) == Hold::go, "old host without the world field: peer up + fresh frames is enough");
        i.host = Hw::up;
        i.joinBusy = true;
        i.t = 105;
        i.rxAt = 105;
        check(lsx::gate(i) == Hold::join, "join teleport running: wait for it");
        i.t = 120.5;
        i.rxAt = 120.5;
        check(lsx::gate(i) == Hold::go, "join teleport over lstpcap: go anyway");
        i.joinBusy = false;
        i.t = 106;
        check(lsx::gate(i) == Hold::go, "teleport done: go");
    }
    std::printf("%s %d fail(s)\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
