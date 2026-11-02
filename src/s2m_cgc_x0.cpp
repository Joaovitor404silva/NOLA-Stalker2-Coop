#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2x.h"
#include "s2m_cgc.h"

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    using KeepFn = uint64_t(__fastcall*)(int64_t, float*, int);
    uint64_t g_tramp = 0;
    PLH::x64Detour* g_det = nullptr;
    bool g_tried = false;
    SRWLOCK g_lk = SRWLOCK_INIT;
    volatile LONG g_mode = 0;
    std::vector<uintptr_t> g_keep;
    volatile LONG64 g_calls = 0, g_kept = 0;

    uintptr_t actor_seh(int64_t body)
    {
        __try
        {
            uint8_t* b = exe();
            int32_t idx = *reinterpret_cast<volatile int32_t*>(body + 0x50);
            int32_t ser = *reinterpret_cast<volatile int32_t*>(body + 0x54);
            int32_t num = *reinterpret_cast<volatile int32_t*>(b + 0xa0d49a4);
            if (!ser || idx < 0 || idx >= num) return 0;
            uintptr_t chunks = *reinterpret_cast<volatile uintptr_t*>(b + 0xa0d4990);
            if (!chunks) return 0;
            uintptr_t ch = *reinterpret_cast<volatile uintptr_t*>(chunks + (uintptr_t)((uint32_t)idx >> 16) * 8);
            if (!ch) return 0;
            uintptr_t it = ch + (uintptr_t)((uint32_t)idx & 0xffff) * 0x18;
            if (*reinterpret_cast<volatile int32_t*>(it + 0x10) != ser) return 0;
            return *reinterpret_cast<volatile uintptr_t*>(it);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    uint64_t __fastcall keep_detour(int64_t body, float* ref, int count)
    {
        uint64_t r = reinterpret_cast<KeepFn>(g_tramp)(body, ref, count);
        InterlockedIncrement64(&g_calls);
        if ((r & 0xff) || g_mode != 2) return r;
        uintptr_t a = actor_seh(body);
        if (!a) return r;
        AcquireSRWLockShared(&g_lk);
        bool hit = std::binary_search(g_keep.begin(), g_keep.end(), a);
        ReleaseSRWLockShared(&g_lk);
        if (!hit) return r;
        InterlockedIncrement64(&g_kept);
        return (r & ~(uint64_t)0xff) | 1;
    }

    bool install()
    {
        if (g_det) return true;
        if (g_tried) return false;
        g_tried = true;
        static const uint8_t k[] = {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00, 0x0f, 0x29, 0xbc, 0x24, 0xc0, 0x00, 0x00, 0x00, 0x0f, 0x29, 0xb4, 0x24, 0xb0, 0x00, 0x00, 0x00, 0x44, 0x89, 0xc6};
        uint8_t* p = exe() + 0xd2352;
        if (!eq_seh(p, k, sizeof k))
        {
            x0::log("NOLA cloot: teste de corpo online (exe+0xd2352) com bytes diferentes do esperado: o cliente segue a limpeza de corpos dele (clhostgc sem gancho)");
            return false;
        }
        g_det = new x0::hot::Detour((uint64_t)p, (uint64_t)&keep_detour, &g_tramp);
        if (!g_det->hook())
        {
            delete g_det;
            g_det = nullptr;
            g_tramp = 0;
            x0::log("NOLA cloot: gancho do teste de corpo online falhou: o cliente segue a limpeza de corpos dele");
            return false;
        }
        x0::log("NOLA cloot: gancho do teste de corpo online instalado (exe+0xd2352): no cliente os corpos do saque comum so saem quando o host tira");
        return true;
    }
}

namespace s2m_cgc
{
    void feed(int mode, const std::vector<uint64_t>& keep)
    {
        if (mode == 2 && !install()) mode = 0;
        std::vector<uintptr_t> k(keep.begin(), keep.end());
        std::sort(k.begin(), k.end());
        AcquireSRWLockExclusive(&g_lk);
        g_keep.swap(k);
        ReleaseSRWLockExclusive(&g_lk);
        InterlockedExchange(&g_mode, mode);
    }
    std::string stat()
    {
        AcquireSRWLockShared(&g_lk);
        int n = (int)g_keep.size();
        ReleaseSRWLockShared(&g_lk);
        char b[160];
        snprintf(b, sizeof b, "gc=%ld keep=%d testes=%lld mantidos=%lld", (long)g_mode, n, (long long)g_calls, (long long)g_kept);
        return b;
    }
}
