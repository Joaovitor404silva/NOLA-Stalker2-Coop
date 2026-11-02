#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2x.h"

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool rd_seh(const void* p, void* out, size_t n)
    {
        __try { std::memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool wr_seh(void* p, const void* in, size_t n)
    {
        __try { std::memcpy(p, in, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    const uintptr_t kCleanup = 0x361d26e;
    const uintptr_t kResetAll = 0x6b306ea;
    const uintptr_t kStopAll = 0x6b30888;
    const uintptr_t kWorld = 0xb0f6530;
    const uintptr_t kStarted = 0xb0f6a81;
    const uintptr_t kProj = 0xa2c8770;

    using CleanupFn = void (*)(void*, void*, bool, bool);

    x0::Flag F_ldmgr("ldmgr");

    uint64_t g_tramp = 0;
    PLH::x64Detour* g_det = nullptr;
    bool g_tried = false;
    int g_n = 0;
    int g_seen = 0;
    std::string g_last = "none";

    bool match(const uint8_t* p)
    {
        uint8_t b[42];
        if (!rd_seh(p, b, sizeof b)) return false;
        const uint8_t a0[] = {0x48, 0x83, 0xec, 0x28, 0x48, 0x39, 0x15};
        const uint8_t a1[] = {0x75, 0x09, 0x80, 0x3d};
        const uint8_t a2[] = {0x00, 0x75, 0x05, 0x48, 0x83, 0xc4, 0x28, 0xc3, 0xe8};
        const uint8_t a3[] = {0x90, 0x48, 0x83, 0xc4, 0x28, 0xe9};
        if (std::memcmp(b, a0, sizeof a0) || std::memcmp(b + 11, a1, sizeof a1) || std::memcmp(b + 19, a2, sizeof a2) || std::memcmp(b + 32, a3, sizeof a3)) return false;
        auto rel = [&](int at, int end) { int32_t r; std::memcpy(&r, b + at, 4); return p + end + r; };
        uint8_t* e = exe();
        return rel(7, 11) == e + kWorld && rel(15, 20) == e + kStarted && rel(28, 32) == e + kStopAll && rel(38, 42) == e + kResetAll;
    }

    void cleanup_detour(void* gi, void* world, bool a, bool b)
    {
        uint8_t* e = exe();
        void* cur = nullptr;
        uint8_t started = 1;
        uint8_t* proj = nullptr;
        uint8_t st = 0;
        int32_t cnt = 0;
        rd_seh(e + kWorld, &cur, 8);
        rd_seh(e + kStarted, &started, 1);
        if (rd_seh(e + kProj, &proj, 8) && proj)
        {
            rd_seh(proj + 0x70, &st, 1);
            rd_seh(proj + 0x74, &cnt, 4);
        }
        if (world && world == cur && started == 0)
        {
            ++g_seen;
            if (F_ldmgr.on() && st == 2)
            {
                ++g_n;
                uint8_t one = 1;
                bool set = wr_seh(e + kStarted, &one, 1);
                g_last = std::string(set ? "stop+reset" : "started byte write failed") + " (projectile manager state 2 counter " + std::to_string(cnt) + ")";
                x0::logf("NOLA ldmgr: world cleanup with loaded but never started managers (load issued before press-any-key): running the stock StopAll + ResetAll so manager lists are emptied and the next LoadMap wait stays balanced (#%d, projectile state %d counter %d, started byte set %d)", g_n, (int)st, cnt, set ? 1 : 0);
            }
            else
            {
                g_last = "skipped (flag " + std::to_string(F_ldmgr.i()) + " projectile state " + std::to_string((int)st) + ")";
                x0::logf("NOLA ldmgr: world cleanup with never started managers left to stock (ldmgr=%d, projectile state %d counter %d)", F_ldmgr.i(), (int)st, cnt);
            }
        }
        reinterpret_cast<CleanupFn>(g_tramp)(gi, world, a, b);
    }

    void install()
    {
        if (g_det || g_tried) return;
        g_tried = true;
        uint8_t* b = exe();
        if (!match(b + kCleanup))
        {
            x0::log("NOLA ldmgr: GameInstance world cleanup handler exe+0x361d26e with unexpected bytes: manager reset fix NOT installed");
            return;
        }
        g_det = new x0::hot::Detour((uint64_t)(b + kCleanup), (uint64_t)&cleanup_detour, &g_tramp);
        if (!g_det->hook())
        {
            delete g_det;
            g_det = nullptr;
            g_tramp = 0;
            x0::log("NOLA ldmgr: detour of the world cleanup handler exe+0x361d26e failed: manager reset fix NOT installed");
            return;
        }
        x0::logf("NOLA ldmgr: world cleanup handler exe+0x361d26e hooked (ldmgr=%d)", F_ldmgr.i());
    }

    void init()
    {
        x0::every(x0::Mod::save, "ldmgrInstall", x0::Rate::t1000, [] { install(); });
        x0::status(x0::Mod::save, [] {
            return std::string("ldmgr: ") + (g_det ? "hooked" : "off") + " flag=" + std::to_string(F_ldmgr.i()) + " unstarted=" + std::to_string(g_seen) + " resets=" + std::to_string(g_n) + " last=" + g_last;
        });
    }

    ::x0::ModReg g_reg(::x0::Mod::save, "ldmgr", &init);
}
