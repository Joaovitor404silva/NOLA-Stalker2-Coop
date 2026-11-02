#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

static uint8_t* g_fake = nullptr;
static uint8_t g_fake_dd[16];

static HMODULE fake_module(const wchar_t* name)
{
    if (name) return ::GetModuleHandleW(name);
    return reinterpret_cast<HMODULE>(g_fake);
}

#define GetModuleHandleW fake_module
#include "../../src/s2n_nat.cpp"
#undef GetModuleHandleW

static int g_ndSent = 0;
static int g_ndApplied = 0;
static int g_moveCalls = 0;

extern "C" bool s2x_nd_send_ok() { return true; }
extern "C" bool s2x_nd_apply_ok() { return true; }
extern "C" void s2x_nd_sent(double) { ++g_ndSent; }
extern "C" void s2x_nd_applied(double, double, double) { ++g_ndApplied; }
int s2n_moveto_native(uint32_t, double, double, double, int, float, int, int, int) { ++g_moveCalls; return 0; }
int s2n_moveto_native_rot(uint32_t, double, double, double, int, float, int, int, int, double) { ++g_moveCalls; return 0; }
int s2n_goal_snap(uint32_t, char* out, int n) { if (out && n > 0) out[0] = 0; return 0; }

static int g_fail = 0;
static int g_pass = 0;

static void expect(const char* name, bool ok, const char* extra = "")
{
    if (ok) ++g_pass;
    else ++g_fail;
    std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, extra);
}

static const uint8_t kA[] = {0x3B, 0x05, 0xF7, 0xB6, 0x3E, 0x09, 0x0F, 0x84, 0x6F, 0x05, 0x00, 0x00, 0x31, 0xC0};
static const uint8_t kB[] = {0x44, 0x8B, 0x25, 0x36, 0xB5, 0x3E, 0x09, 0x66, 0x45, 0x0F, 0x57, 0xFF, 0x44, 0x39, 0xE7, 0x74, 0x05, 0x66, 0x45, 0x0F, 0x28, 0xF8,
                             0xE8, 0x24, 0x09, 0x00, 0x00};
static const uint8_t kC[] = {0x31, 0xC0, 0x44, 0x39, 0xE7, 0x0F, 0x94, 0xC0, 0x31, 0xD2, 0x3B, 0x0D, 0x47, 0xB4, 0x3E, 0x09, 0x0F, 0x94, 0xC2,
                             0x0F, 0x57, 0xC9, 0xF3, 0x0F, 0x5A, 0x4C, 0x95, 0x58};

static bool make_fake_exe(bool good)
{
    g_fake = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0xA000000, MEM_RESERVE, PAGE_READWRITE));
    if (!g_fake) return false;
    if (!VirtualAlloc(g_fake + 0xaee000, 0x3000, MEM_COMMIT, PAGE_EXECUTE_READWRITE)) return false;
    if (!VirtualAlloc(g_fake + 0x9eda000, 0x1000, MEM_COMMIT, PAGE_READWRITE)) return false;
    if (good)
    {
        std::memcpy(g_fake + 0xaeea43, kA, sizeof kA);
        std::memcpy(g_fake + 0xaeec03, kB, sizeof kB);
        std::memcpy(g_fake + 0xaeece9, kC, sizeof kC);
    }
    return true;
}

static void drop_fake_exe()
{
    if (g_fake) VirtualFree(g_fake, 0, MEM_RELEASE);
    g_fake = nullptr;
}

static void test_cfg_api()
{
    s2nat::cfg_mc(1);
    expect("cfg_mc 1 turns multi client on", s2nat::mc_on() == 1);
    s2nat::cfg_mc(7);
    expect("cfg_mc accepts only 1 as on", s2nat::mc_on() == 0);
    s2nat::natcb(0);
    expect("natcb writes the callback switch", C.natcb == 0);
    s2nat::natcb(1);
    expect("natcb back on", C.natcb == 1);
    uint8_t* had = g_ddBuild;
    g_ddBuild = nullptr;
    s2nat::cfg_dmg(1);
    expect("cfg_dmg stays off while the damage build is not detected", s2nat::nd_on() == 0);
    g_ddBuild = g_fake_dd;
    s2nat::cfg_dmg(1);
    expect("cfg_dmg on reads back through nd_on when the build is detected", s2nat::nd_on() == 1);
    s2nat::cfg_dmg(0);
    expect("cfg_dmg off reads back through nd_on", s2nat::nd_on() == 0);
    g_ddBuild = had;
}

static void test_feed()
{
    s2nat::feed_mode(false);
    s2nat::feed("dropped");
    expect("feed is ignored while feed mode is off", !g_feedNew && g_feedText.empty());
    s2nat::feed_mode(true);
    s2nat::feed("frame");
    expect("feed stores text in feed mode", g_feedNew && g_feedText == "frame");
    s2nat::feed_mode(true);
    expect("feed_mode on twice keeps the pending text", g_feedNew && g_feedText == "frame");
    s2nat::feed_mode(false);
    expect("feed_mode off clears the pending text", !g_feedNew && g_feedText.empty());
}

static void test_clown()
{
    s2nat::cl_own(0x1000, 1);
    expect("cl_own adds an address", g_clown.count(0x1000) == 1);
    s2nat::cl_own(0x1000, 0);
    expect("cl_own removes an address", g_clown.count(0x1000) == 0);
    s2nat::cl_own(0x2000, 1);
    s2nat::cl_own(0, 0);
    expect("cl_own address 0 off clears the set", g_clown.empty());
}

static void test_ag_bad_bytes()
{
    if (!make_fake_exe(false)) { expect("fake exe for bad bytes", false); return; }
    g_agTried = false;
    g_agD = nullptr;
    g_agState = "nao instalado";
    C.aggrop = 1;
    ag_install();
    expect("ag_install refuses an exe with other bytes", std::strcmp(g_agState, "bytes diferentes (exe novo?)") == 0, g_agState);
    expect("ag_install switches aggrop off on other bytes", C.aggrop == 0);
    expect("ag_install leaves the exe bytes untouched on other bytes", g_fake[0xaeea43] == 0);
    expect("ag_install marks that it tried", g_agTried);
    drop_fake_exe();
}

static void test_ag_good_bytes()
{
    if (!make_fake_exe(true)) { expect("fake exe for good bytes", false); return; }
    g_agTried = false;
    g_agD = nullptr;
    g_agState = "nao instalado";
    C.aggrop = 1;
    ag_install();
    expect("ag_install accepts the 3 known byte runs", std::strcmp(g_agState, "on") == 0, g_agState);
    expect("site A is a near jump", g_fake[0xaeea43] == 0xE9);
    expect("site B is a near jump", g_fake[0xaeec03] == 0xE9);
    expect("site C is a near jump", g_fake[0xaeece9] == 0xE9);
    expect("site A padding is nop", g_fake[0xaeea43 + 5] == 0x90 && g_fake[0xaeea43 + 11] == 0x90);
    expect("site A keeps the bytes after the patch", g_fake[0xaeea43 + 12] == kA[12] && g_fake[0xaeea43 + 13] == kA[13]);
    int32_t ra = 0;
    std::memcpy(&ra, g_fake + 0xaeea43 + 1, 4);
    uint8_t* toA = g_fake + 0xaeea43 + 5 + ra;
    expect("site A jumps to the emitted stub a", g_agStub && toA == g_agStub);
    expect("stub a starts with cmp eax,[player id]", g_agStub && g_agStub[0] == 0x3B && g_agStub[1] == 0x05);
    expect("scorer data block sits at stub+0x800", g_agD == reinterpret_cast<AgData*>(g_agStub + 0x800));
    expect("scorer data starts off with no hits", g_agD && g_agD->hits == 0 && g_agD->on == 0);
    uint32_t ids[2] = {11, 22};
    ag_set(g_agD, ids, 2);
    expect("ag_set arms the scorer with the first id", g_agD->on == 1 && g_agD->tid == 11);
    expect("ag_set repeats ids when fewer than the slots", g_agD->tidx[0] == 22 && g_agD->tidx[1] == 11 && g_agD->tidx[2] == 11);
    ag_set(g_agD, nullptr, 0);
    expect("ag_set with no ids disarms the scorer", g_agD->on == 0 && g_agD->tid == 0xFFFFFFFFu);
    if (g_agStub) VirtualFree(g_agStub, 0, MEM_RELEASE);
    drop_fake_exe();
}

int main()
{
    test_cfg_api();
    test_feed();
    test_clown();
    test_ag_bad_bytes();
    test_ag_good_bytes();
    std::printf("natlayer %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
