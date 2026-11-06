// Included after the verbatim production CRx implementation by run_issue_tests.py.
int main()
{
    using namespace s2r;
    using js::V;
    for (int local : {1, 2, 4})
    {
        CRx rx;
        Roster published;
        Stats stats;
        std::map<int, int> seen;
        int dropped = 0;
        rx.say = [](const std::string&) {};
        rx.warn = [](const char*, const std::string&) {};
        rx.publish = [&](const Roster& r) { published = r; };
        rx.roster = [&] { return published; };
        rx.view_bye = [&](int v, const std::string&) { seen.erase(v); ++dropped; };
        rx.put_view = [&](int v, std::string wire, double) {
            V snap; assert(js::parse(wire, snap));
            seen[v] = (int)snap.get("pid")->num();
        };
        auto roster = [&](int me) {
            V r = V::Object(); r.set("you", V::Int(me)); r.set("max", V::Int(4));
            V peers = V::Arr();
            for (int p = 1; p <= 4; ++p) {
                V e = V::Object(); e.set("pid", V::Int(p));
                e.set("sid", V::Str("session" + std::to_string(p))); e.set("st", V::Str("up"));
                peers.a.push_back(e);
            }
            r.set("peers", peers);
            rx.rx(proto::pack(proto::T_ROSTER, r), proto::T_ROSTER, 1, stats);
        };
        auto peer = [&](int p) {
            V r = V::Object(); r.set("pid", V::Int(p)); r.set("f", V::Int(10));
            r.set("sid", V::Str("session" + std::to_string(p)));
            rx.rx(proto::pack(proto::T_PEER, r), proto::T_PEER, 2, stats);
        };
        roster(local);
        assert(rx.you == local); // Regression: old code only assigned a local variable.
        for (int p = 1; p <= 4; ++p) peer(p);
        assert(seen.size() == 3);
        for (int p = 1; p <= 4; ++p)
            if (p != local) assert(seen.at(p < local ? p : p - 1) == p);
        roster(local);
        assert(dropped == 0 && seen.size() == 3);
        int reassigned = local == 1 ? 2 : 1;
        roster(reassigned);
        assert(rx.you == reassigned && seen.empty() && dropped == 3);
        for (int p = 1; p <= 4; ++p) peer(p);
        assert(seen.size() == 3);
        roster(0);
        assert(rx.you == reassigned && stats.badpeer == 1);
    }
    std::cout << "PASS: roster assignment, teammate forwarding, self exclusion, slot reassignment, malformed slot\n";
}
