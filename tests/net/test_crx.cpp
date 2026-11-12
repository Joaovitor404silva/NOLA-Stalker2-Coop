#include "../../src/s2r_net.cpp"

using namespace s2r;

static int fails = 0, checks = 0;
static void check(bool ok, const char* what)
{
    ++checks;
    if (!ok) { ++fails; std::printf("FAIL %s\n", what); }
}

struct RxTest
{
    CRx rx;
    Roster published;
    Stats stats;
    int received = 0, dropped = 0, lastView = -1;
    js::V last;

    RxTest()
    {
        rx.say = [](const std::string&) {};
        rx.warn = [](const char*, const std::string&) {};
        rx.view_bye = [&](int, const std::string&) { ++dropped; };
        rx.publish = [&](const Roster& r) { published = r; };
        rx.roster = [&] { return published; };
        rx.put_view = [&](int v, std::string text, double) {
            ++received;
            lastView = v;
            check(js::parse(text, last), "forwarded snapshot is valid JSON");
        };
    }

    void roster(int self, int count, int omit = -1)
    {
        js::V r = js::V::Object(), peers = js::V::Arr();
        r.set("you", js::V::Int(self));
        r.set("max", js::V::Int(count));
        for (int pid = 1; pid <= count; ++pid) {
            if (pid == omit) continue;
            js::V p = js::V::Object();
            p.set("pid", js::V::Int(pid));
            p.set("sid", js::V::Str("session-" + std::to_string(pid)));
            p.set("st", js::V::Str("up"));
            peers.a.push_back(p);
        }
        r.set("peers", peers);
        rx.rx(proto::pack(proto::T_ROSTER, r), proto::T_ROSTER, 1.0, stats);
    }

    void peer(int pid, int frame = 1)
    {
        js::V p = js::V::Object();
        p.set("pid", js::V::Int(pid));
        p.set("sid", js::V::Str("session-" + std::to_string(pid)));
        p.set("f", js::V::Int(frame));
        p.set("x", js::V::Int(123));
        p.set("x1", js::V::Str("#L:a"));
        rx.rx(proto::pack(proto::T_PEER, p), proto::T_PEER, 2.0, stats);
    }
};

int main()
{
    for (int self = 1; self <= 2; ++self) {
        RxTest t;
        int other = 3 - self;
        t.peer(other);
        check(t.received == 0, "peer packets wait for admission");
        t.roster(self, 2);
        check(t.rx.you == self, "roster remembers the assigned player ID");
        check(t.published.you == self, "published roster has the same player ID");
        t.peer(other, 2);
        check(t.received == 1 && t.lastView == 1, "each guest receives the other guest in view 1");
        check(t.last.get("pid") && t.last.get("pid")->num() == other, "snapshot keeps the other guest's ID");
        check(t.last.get("x") && t.last.get("x")->num() == 123, "snapshot keeps the other guest's position");
        check(t.last.get("x1") && t.last.get("x1")->s == "#L:a", "snapshot keeps the other guest's extras");
        t.peer(self);
        check(t.received == 1, "guest does not receive its own snapshot");
        t.peer(other, 1);
        check(t.received == 1 && t.stats.stale == 1, "older peer snapshots are rejected");
        t.roster(self, 2);
        check(t.dropped == 0, "unchanged roster keeps the peer view");
        t.roster(other, 2);
        check(t.rx.you == other, "slot reassignment updates the remembered player ID");
        check(t.dropped == 1 && t.rx.cv[1].pid == -1, "slot reassignment clears the previous peer view");
        t.peer(other);
        check(t.received == 1, "new local player ID is excluded after reassignment");
        t.peer(self);
        check(t.received == 2 && t.lastView == 1, "peer snapshots resume after reassignment");
    }
    for (int self = 1; self <= proto::MAXV; ++self) {
        RxTest t;
        t.roster(self, proto::MAXV);
        for (int pid = 1; pid <= proto::MAXV; ++pid) {
            if (pid == self) continue;
            t.peer(pid);
            int view = pid < self ? pid : pid - 1;
            check(t.lastView == view && t.rx.cv[view].pid == pid, "peer maps to a distinct view for every local slot");
        }
        check(t.received == proto::MAXV - 1, "all other guests are forwarded at maximum capacity");
        int gone = self == 1 ? 2 : 1;
        int view = gone < self ? gone : gone - 1;
        t.roster(self, proto::MAXV, gone);
        check(t.dropped == 1 && t.rx.cv[view].pid == -1, "departed peer is cleared from the view");
    }
    std::printf("%s (%d checks, %d failures)\n", fails ? "FAILED" : "ALL PASS", checks, fails);
    return fails ? 1 : 0;
}
