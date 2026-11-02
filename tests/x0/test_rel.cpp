#include "tables_check.h"
#include <cstdio>

using namespace x0;
using namespace x0::in;

static int g_fail = 0, g_pass = 0;
static std::vector<std::string> g_log;

static void check(bool ok, const std::string& what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL %s\n", what.c_str()); }
}
static bool has_log(const std::string& sub)
{
    for (auto& l : g_log) if (l.find(sub) != std::string::npos) return true;
    return false;
}

int main()
{
#ifndef S2X_RELEASE
    std::printf("FAIL test_rel is not an S2X_RELEASE build\n");
    return 1;
#else
    std::wstring dir = tc_empty_dir(L"rel");
    write_text(dir + L"role.txt", "client\n");
    CreateDirectoryW((dir + L"s2x_data").c_str(), nullptr);
    write_text(dir + L"s2x_data\\s2u_kit.tsv", "Loose\t1\n");
    write_text(dir + L"flags.txt", "playergod=1\ndevcmd=1\nnetrecord=1\ntaball=1\n");
    set_role_for_test("host", dir);
    log_sink().out = [](const std::string& l) { g_log.push_back(l); };
    check(release_build(), "release_build() true");
    flags_poll(0, true);
    check(has_log("NOLA x0 flags: release build, "), "release log line");
    for (const char* k : {"playergod", "devcmd", "netrecord", "luacmd"})
    {
        check(dev_locked(k), std::string("dev flag locked: ") + k);
        check(fs(k, "0") == "0", std::string("dev flag stays at its default: ") + k + "=" + fs(k, "0"));
    }
    fset("playergod", Val::I(1), "test");
    check(fi("playergod", 0) == 0, "fset cannot turn a dev flag on");
    check(!dev_locked("taball"), "taball is not locked");
    std::string t;
    check(data("s2u_kit.tsv", t) && t.find("Loose") == std::string::npos, "release data() ignores the loose s2x_data override");
    check(data_file(L"s2u_kit.tsv").empty(), "release data_file() empty");
    remove_file(dir + L"s2x_data\\s2u_kit.tsv");
    RemoveDirectoryW((dir + L"s2x_data").c_str());
    remove_file(dir + L"role.txt");
    remove_file(dir + L"flags.txt");
    tc_tables(check, L"relt");
    RemoveDirectoryW(dir.c_str());
    std::printf("x0 release selftest: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
#endif
}
