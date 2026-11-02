#include "s2m_pcompass_logic.h"

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

static int count(const std::vector<pcomp::Op>& ops, bool add, int32_t uid)
{
    int n = 0;
    for (const auto& o : ops)
        if (o.add == add && o.uid == uid) ++n;
    return n;
}

int main()
{
    using namespace pcomp;
    check(kPartyLeader == 29, "EMarkerType::PartyLeader is 29");
    check(uid32(0) == 0 && uid32(-5) == 0 && uid32(4294967296ll) == 0 && uid32(1234) == 1234, "uid32 keeps only positive 32-bit uids");

    Uids cur{};
    Uids want{};
    check(step(cur, want, true).empty(), "no partner: no calls");

    want[0] = 100;
    auto ops = step(cur, want, true);
    check(ops.size() == 1 && count(ops, true, 100) == 1, "partner joins: one add");
    check(cur[0] == 100, "partner tracked");

    ops = step(cur, want, true);
    check(ops.size() == 1 && count(ops, true, 100) == 1 && count(ops, false, 100) == 0, "steady: add re-asserted, never removed");

    want[1] = 200;
    want[2] = 300;
    want[3] = 400;
    ops = step(cur, want, true);
    check(count(ops, true, 100) == 1 && count(ops, true, 200) == 1 && count(ops, true, 300) == 1 && count(ops, true, 400) == 1 && ops.size() == 4,
          "four partners: one marker each");

    want[1] = 0;
    ops = step(cur, want, true);
    check(count(ops, false, 200) == 1 && count(ops, true, 200) == 0 && cur[1] == 0, "partner 2 leaves: its marker removed");

    want[0] = 101;
    ops = step(cur, want, true);
    check(count(ops, false, 100) == 1 && count(ops, true, 101) == 1, "puppet re-created with a new uid: old removed, new added");
    check(ops.front().add == false && ops.front().uid == 100, "remove runs before the add of the same view");

    ops = step(cur, want, false);
    check(count(ops, false, 101) == 1 && count(ops, false, 300) == 1 && count(ops, false, 400) == 1 && ops.size() == 3, "pcompass=0: every marker removed");
    check(cur == Uids{}, "pcompass=0: nothing tracked");
    check(step(cur, want, false).empty(), "pcompass=0 steady: no calls");

    ops = step(cur, want, true);
    check(ops.size() == 3, "pcompass back to 1: markers come back");

    check(start_state(1, true) == 3 && start_state(2, true) == 3, "new marker starts Explored (EMarkerState 3), visible at any distance");
    check(start_state(3, true) == 3, "Explored stays Explored");
    check(start_state(1, false) == 1 && start_state(2, false) == 2, "pcompexpl=0: stock discovery state kept");

    if (fails == 0) std::printf("ok test_pcompass\n");
    return fails ? 1 : 0;
}
