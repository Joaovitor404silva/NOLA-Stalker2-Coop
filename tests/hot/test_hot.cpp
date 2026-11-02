#include "s2x_hot_logic.h"

#include <cstdio>

using namespace x0::hot::logic;

static int fails = 0;
static void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++fails;
}

int main()
{
    check(trim("  abc \r\n") == "abc", "trim strips spaces and CRLF");
    check(trim(" \r\n").empty(), "trim of blanks is empty");
    check(enabled(""), "no flags file text: hot reload on (default on)");
    check(enabled("role=host\njoinmenu=0\n"), "flag absent: on");
    check(!enabled("role=host\r\nhotreload=0\r\n"), "hotreload=0 (CRLF): off");
    check(!enabled("hotreload=0"), "hotreload=0 last line without newline: off");
    check(enabled("hotreload=1\n"), "hotreload=1: on");
    check(enabled("hotreload=0\nhotreload=1\n"), "last value wins: on");
    check(enabled("xhotreload=0\n"), "other key ending in hotreload is ignored");
    check(copy_name(1234, 3) == L"hot_1234_3.dll", "copy name hot_<pid>_<gen>.dll");
    check(is_copy_name(copy_name(1234, 3)), "copy name round trip");
    check(is_copy_name(L"hot_1_1.dll"), "shortest copy name");
    check(!is_copy_name(L"main.dll"), "main.dll is never a copy");
    check(!is_copy_name(L"hot_x_1.dll"), "non-digit pid rejected");
    check(!is_copy_name(L"hot_12.dll"), "missing gen rejected");
    check(!is_copy_name(L"hot__1.dll"), "empty pid rejected");
    check(!is_copy_name(L"hot_1_.dll"), "empty gen rejected");
    check(!is_copy_name(L"hot_1_2.dll.bak"), "other extension rejected");
    Kept k = unpack(pack(42, {"0:7,Scanner,1", "1:3,Key,2", "bad\nkey", ""}));
    check(k.ok && k.seq == 42 && k.keys.size() == 2 && k.keys[0] == "0:7,Scanner,1" && k.keys[1] == "1:3,Key,2", "pack/unpack keeps seq and relay keys, drops empty and multi-line keys");
    check(unpack(pack(5, {})).ok && unpack(pack(5, {})).keys.empty(), "pack with no keys still carries seq");
    check(!unpack("").ok && !unpack("v1 x").ok && !unpack("v2 3").ok, "empty or foreign text is not a stash");
    ::SetEnvironmentVariableA("NOLA_HOT_GEN", nullptr);
    keep("TEST", pack(9, {"a", "b"}));
    check(take("TEST").empty(), "no reload since the write (same generation): take gives nothing, so a state reset or a test process never re-reads its own stash");
    ::SetEnvironmentVariableA("NOLA_HOT_GEN", "1");
    Kept t = unpack(take("TEST"));
    check(t.ok && t.seq == 9 && t.keys.size() == 2, "keep then take round trip through the process environment");
    check(take("TEST").empty(), "take consumes the stash (second take is empty)");
    std::vector<std::string> big;
    for (int i = 0; i < 4000; ++i) big.push_back("entry_" + std::to_string(i) + "_padding");
    big.insert(big.end(), 1000, std::string(40, 'z'));
    keep("TEST", pack(77, big));
    ::SetEnvironmentVariableA("NOLA_HOT_GEN", "2");
    Kept b = unpack(take("TEST"));
    check(b.ok && b.seq == 77 && !b.keys.empty() && b.keys.size() < big.size() && b.keys.back().rfind("entry_", 0) == 0, "oversized stash is cut at a line end and keeps seq");
    int slotA = 0, slotB = 0;
    check(slot_matches(&slotA, &slotA, 5), "alive: same object in its slot");
    check(!slot_matches(&slotB, &slotA, 5), "alive: slot reused by another object");
    check(!slot_matches(nullptr, &slotA, 5), "alive: emptied slot");
    check(!slot_matches(&slotA, &slotA, -1), "alive: no index");
    check(!slot_matches(nullptr, nullptr, 0), "alive: null function");
    QiX qx;
    qx.based = true;
    qx.sent = {{"sid_a", 12.5, 3}, {"sid_b", 0.1 + 0.2, 0}};
    qx.out = {{"7,sid_a,3", false, 0.0}, {"8,sid_b,1", true, 99.25}, {"bad\tentry", false, 0.0}};
    bool qok = false;
    QiX qy = qix_unpack(qix_pack(qx), qok);
    check(qok && qy.based && qy.sent.size() == 2 && std::get<0>(qy.sent[0]) == "sid_a" && std::get<1>(qy.sent[0]) == 12.5 && std::get<2>(qy.sent[0]) == 3 && std::get<1>(qy.sent[1]) == 0.1 + 0.2, "quest pack/unpack keeps based and sent (sid, time, count)");
    check(qy.out.size() == 2 && std::get<0>(qy.out[0]) == "7,sid_a,3" && !std::get<1>(qy.out[0]) && std::get<0>(qy.out[1]) == "8,sid_b,1" && std::get<1>(qy.out[1]) && std::get<2>(qy.out[1]) == 99.25, "quest pack/unpack keeps pending out with and without untilT, drops tab entries");
    QiX qe = qix_unpack(qix_pack(QiX{}), qok);
    check(qok && !qe.based && qe.sent.empty() && qe.out.empty(), "empty quest state round trip");
    qix_unpack("garbage", qok);
    check(!qok, "foreign text is not a quest stash");
    std::string huge(40000, 'x');
    check(!keep("TEST2", "a\n" + huge + "\n" + huge) , "oversized stash reports the cut");
    check(keep("TEST3", "a\nb"), "small stash reports whole");
    std::printf("%s hot logic: %d failures\n", fails ? "FAIL" : "PASS", fails);
    return fails ? 1 : 0;
}
