#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2x.h"
#include "s2m_ixtap.h"

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    using TapFn = void(__fastcall*)(int64_t);
    uint64_t g_tramp = 0;
    PLH::x64Detour* g_det = nullptr;
    bool g_tried = false;
    s2m_ixtap::Probe g_probe = nullptr;
    s2m_ixtap::Gate g_gate = nullptr;
    std::mutex g_mu;
    std::string g_act;
    bool g_pend = false;
    volatile LONG64 g_calls = 0, g_taps = 0;

    bool tap_seh(int64_t ipu)
    {
        __try
        {
            if (!ipu) return false;
            uintptr_t gp = *reinterpret_cast<volatile uintptr_t*>(exe() + 0xa2c85a8);
            if (!gp) return false;
            double t = *reinterpret_cast<volatile double*>(gp + 0xa0);
            float p = *reinterpret_cast<volatile float*>(ipu + 0x40);
            return (float)t - p <= 0.3f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void note()
    {
        if (!g_probe) return;
        std::string a = g_probe();
        if (a.empty()) return;
        InterlockedIncrement64(&g_taps);
        std::lock_guard<std::mutex> g(g_mu);
        g_act = a;
        g_pend = true;
    }

    void __fastcall tap_detour(int64_t ipu)
    {
        InterlockedIncrement64(&g_calls);
        if (tap_seh(ipu))
        {
            if (g_gate && g_gate()) return;
            note();
        }
        reinterpret_cast<TapFn>(g_tramp)(ipu);
    }
}

namespace s2m_ixtap
{
    bool install(Probe p)
    {
        g_probe = p;
        if (g_det) return true;
        if (g_tried) return false;
        g_tried = true;
        static const uint8_t k[] = {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x40};
        uint8_t* f = exe() + 0xf8bce6;
        if (!eq_seh(f, k, sizeof k))
        {
            x0::log("NOLA pupanim: toque de interacao (exe+0xf8bce6) com bytes diferentes do esperado: portas, itens e interruptores nao mandam acao (paix sem gancho)");
            return false;
        }
        g_det = new x0::hot::Detour((uint64_t)f, (uint64_t)&tap_detour, &g_tramp);
        if (!g_det->hook())
        {
            delete g_det;
            g_det = nullptr;
            g_tramp = 0;
            x0::log("NOLA pupanim: gancho do toque de interacao falhou: portas, itens e interruptores nao mandam acao");
            return false;
        }
        x0::log("NOLA pupanim: gancho do toque de interacao instalado (exe+0xf8bce6 SingleClickIPU)");
        return true;
    }
    void gate(Gate g) { g_gate = g; }
    bool take(std::string& actor)
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_pend) return false;
        g_pend = false;
        actor = g_act;
        return true;
    }
    std::string stat()
    {
        char b[96];
        snprintf(b, sizeof b, "toques=%lld com_alvo=%lld", (long long)g_calls, (long long)g_taps);
        return b;
    }
}
