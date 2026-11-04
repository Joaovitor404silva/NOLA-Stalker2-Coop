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

static int count(const std::vector<pcomp::Op>& ops, bool add, int view)
{
    int n = 0;
    for (const auto& o : ops)
        if (o.add == add && o.view == view) ++n;
    return n;
}

static bool fresh_of(const std::vector<pcomp::Op>& ops, int view)
{
    for (const auto& o : ops)
        if (o.add && o.view == view) return o.fresh;
    return false;
}

int main()
{
    using namespace pcomp;
    check(kPartyLeader == 29, "EMarkerType::PartyLeader is 29");

    for (int v = 0; v < kViews; ++v)
    {
        uint32_t u = static_cast<uint32_t>(uid_for(v));
        check(u >= kUidNoObject && u < kUidReserved, "synthetic uid: no object-table link, below the reserved range");
        check(synthetic(uid_for(v)) && view_of(uid_for(v)) == v, "synthetic uid recognised and mapped back to its view");
    }
    check(uid_for(1) == uid_for(0) + 1 && uid_for(3) != uid_for(2), "one uid per view");
    check(!synthetic(0) && !synthetic(3216) && !synthetic(uid_for(kViews)) && view_of(77) == -1, "stock uids are never ours");

    check(cell(0, 0) == 0 && cell(-5000, -99999) == 0, "cell: origin and negatives land in cell 0");
    check(cell(10150 * 3 + 1, 0) == 3, "cell: x axis steps every 101.5 m");
    check(cell(0, 10150 * 2 + 1) == 2 * kGridSide, "cell: y axis steps rows of 0x50");
    check(cell(1e9, 1e9) == (kGridSide - 1) + (kGridSide - 1) * kGridSide, "cell: far positions clamp to the last row and column");
    check(cell(10150.0 * 0x4e + 1, 0) == 0x4e && cell(10150.0 * 0x4f + 1, 0) == 0x4f, "cell: clamp starts above 0x4e like the game");
    check(cell_valid(0) && cell_valid(kGridCells - 1) && !cell_valid(kGridCells) && !cell_valid(kNoCell), "cell_valid bounds");

    check(fade_byte_known(0x08) && fade_byte_known(0x28) && !fade_byte_known(0x00) && !fade_byte_known(0x18), "fade mask byte: stock or patched only");
    check(fade_byte(true) == 0x28 && fade_byte(false) == 0x08, "fade mask: bit 29 (PartyLeader) added when on");
    check((0x28001c00u >> kPartyLeader & 1u) == 1u && (0x08001c00u >> kPartyLeader & 1u) == 0u, "patched mask includes type 29, stock does not");
    check((0x28001c00u & 0x08001c00u) == 0x08001c00u, "patched mask keeps the stock quest types");

    Flags none{};
    Flags present{};
    Flags want{};
    check(plan(present, want, true).empty(), "no partner, no record: no calls");

    want[0] = true;
    auto ops = plan(present, want, true);
    check(ops.size() == 1 && count(ops, true, 0) == 1 && fresh_of(ops, 0), "partner joins: one fresh add");

    present[0] = true;
    ops = plan(present, want, true);
    check(ops.size() == 1 && count(ops, true, 0) == 1 && !fresh_of(ops, 0), "steady: the record is re-placed, not re-created");

    want[1] = want[2] = want[3] = true;
    ops = plan(present, want, true);
    check(ops.size() == 4 && count(ops, true, 1) == 1 && fresh_of(ops, 1) && !fresh_of(ops, 0), "four partners: one add each, only the new ones fresh");

    present = Flags{true, true, true, true};
    want[1] = false;
    ops = plan(present, want, true);
    check(count(ops, false, 1) == 1 && count(ops, true, 1) == 0 && ops.size() == 4, "partner 2 leaves: its record removed, the others kept");

    present = Flags{true, false, true, true};
    ops = plan(present, want, false);
    check(ops.size() == 3 && count(ops, false, 0) == 1 && count(ops, false, 2) == 1 && count(ops, false, 3) == 1, "pcompass=0: every present record removed, none added");
    check(plan(none, want, false).empty(), "pcompass=0 steady: no calls");

    present = Flags{false, true, false, false};
    want = Flags{false, false, false, false};
    ops = plan(present, want, true);
    check(ops.size() == 1 && count(ops, false, 1) == 1, "ghost record after a world load (partner gone): removed");

    present = Flags{true, false, false, false};
    want = Flags{true, false, false, false};
    ops = plan(present, want, true);
    check(ops.size() == 1 && !fresh_of(ops, 0), "record restored by the game after a load for a live partner: adopted, not duplicated");

    check(start_state(1, true) == 3 && start_state(2, true) == 3, "new marker starts Explored (EMarkerState 3), visible at any distance");
    check(start_state(3, true) == 3, "Explored stays Explored");
    check(start_state(1, false) == 1 && start_state(2, false) == 2, "pcompexpl=0: stock discovery state kept");

    if (fails == 0) std::printf("ok test_pcompass\n");
    return fails ? 1 : 0;
}
