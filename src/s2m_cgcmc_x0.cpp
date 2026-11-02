#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

#include "s2x.h"
#include "s2m_cgcmc_logic.h"

namespace
{
    constexpr int kMax = 8;
    constexpr double kAge = 5.0;

    x0::Flag F_mc("corpsemc");
    x0::Flag F_al("alifemc");

    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    bool eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
    {
        __try { return std::memcmp(p, b, n) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    using OffFn = uint64_t(__fastcall*)(int64_t, int64_t);
    using RingFn = uint64_t(__fastcall*)(int64_t, float*, int);
    using MgrFn = int64_t(__fastcall*)();
    using GridFn = void(__fastcall*)(int64_t, float*, float, int64_t);
    using FarFn = uint64_t(__fastcall*)(int64_t, float);

    uint64_t g_tramp = 0;
    PLH::x64Detour* g_det = nullptr;
    bool g_tried = false;
    SRWLOCK g_lk = SRWLOCK_INIT;
    cgcmc::P g_cs[kMax];
    int g_n = 0;
    volatile LONG g_on = 0;
    volatile LONG64 g_tests = 0, g_kept = 0;
    long long g_logTests = 0, g_logKept = 0;
    double g_logAt = 0;
    int g_logN = -1;

    uint64_t g_gtramp = 0, g_ftramp = 0;
    PLH::x64Detour* g_gdet = nullptr;
    PLH::x64Detour* g_fdet = nullptr;
    bool g_atried = false;
    volatile LONG g_aon = 0;
    volatile LONG64 g_held = 0, g_passes = 0, g_brought = 0;
    volatile LONG64 g_ring = 0;
    long long g_logHeld = -1;
    bool g_capLogged = false;
    alignas(16) uint8_t g_scratch[0x400];

    bool corpse_seh(int64_t u, cgcmc::P& p)
    {
        __try
        {
            if (*reinterpret_cast<volatile uint8_t*>(u + 0x88) != 1) return false;
            p.x = *reinterpret_cast<volatile float*>(u + 0x28);
            p.y = *reinterpret_cast<volatile float*>(u + 0x2c);
            p.z = *reinterpret_cast<volatile float*>(u + 0x30);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    float rad_seh()
    {
        __try { return cgcmc::radius(*reinterpret_cast<volatile float*>(exe() + 0xb10e258), *reinterpret_cast<volatile float*>(exe() + 0x9ede734)); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return cgcmc::radius(0.f, 0.f); }
    }

    bool count_seh(int& out)
    {
        __try
        {
            int64_t m = reinterpret_cast<MgrFn>(exe() + 0xd1fc4)();
            if (!m) return false;
            out = *reinterpret_cast<volatile int*>(m + 0x334);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool ring_seh(int64_t u, float* ref, int cnt)
    {
        __try { return (reinterpret_cast<RingFn>(exe() + 0xd2352)(u, ref, cnt) & 0xff) != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool near_remote(int64_t unit)
    {
        cgcmc::P u;
        if (!corpse_seh(unit, u)) return false;
        std::vector<cgcmc::P> cs;
        AcquireSRWLockShared(&g_lk);
        cs.assign(g_cs, g_cs + g_n);
        ReleaseSRWLockShared(&g_lk);
        if (!cgcmc::wants(true, true, true, cs.size())) return false;
        int cnt = 0;
        if (!count_seh(cnt)) return false;
        InterlockedIncrement64(&g_tests);
        float rad = rad_seh();
        int k = cgcmc::keeper(u, cs, rad, [&](const cgcmc::P& c) {
            float ref[3] = {c.x, c.y, c.z};
            return ring_seh(unit, ref, cnt);
        });
        if (k < 0) return false;
        InterlockedIncrement64(&g_kept);
        return true;
    }

    uint64_t __fastcall off_detour(int64_t mgr, int64_t unit)
    {
        if (mgr) InterlockedExchange64(&g_ring, mgr);
        if (g_on && unit && near_remote(unit)) return 0;
        return reinterpret_cast<OffFn>(g_tramp)(mgr, unit);
    }

    bool pos_seh(int64_t u, cgcmc::P& p)
    {
        __try
        {
            p.x = *reinterpret_cast<volatile float*>(u + 0x28);
            p.y = *reinterpret_cast<volatile float*>(u + 0x2c);
            p.z = *reinterpret_cast<volatile float*>(u + 0x30);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    float r_seh()
    {
        __try { return *reinterpret_cast<volatile float*>(exe() + 0xb10e258); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0.f; }
    }

    bool int_seh(const uint8_t* p, int& out)
    {
        __try
        {
            out = *reinterpret_cast<const volatile int*>(p);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int centres(cgcmc::P* cs)
    {
        AcquireSRWLockShared(&g_lk);
        int n = g_n;
        for (int i = 0; i < n; i++) cs[i] = g_cs[i];
        ReleaseSRWLockShared(&g_lk);
        return n;
    }

    uint64_t __fastcall far_detour(int64_t unit, float k)
    {
        uint64_t r = reinterpret_cast<FarFn>(g_ftramp)(unit, k);
        if (!g_aon || !unit || !(r & 0xff)) return r;
        cgcmc::P u;
        if (!pos_seh(unit, u)) return r;
        cgcmc::P cs[kMax];
        int n = centres(cs);
        if (cgcmc::far_all(true, u, cs, (size_t)n, cgcmc::radius(r_seh(), k))) return r;
        InterlockedIncrement64(&g_held);
        return r & ~0xffull;
    }

    bool grid_seh(int64_t out, const cgcmc::P& p, float r, int64_t ctx, int& added)
    {
        __try
        {
            alignas(16) uint8_t cx[0x28];
            std::memcpy(cx, reinterpret_cast<const void*>(ctx), sizeof cx);
            float c[3] = {p.x, p.y, p.z};
            *reinterpret_cast<float*>(cx) = p.z;
            *reinterpret_cast<float**>(cx + 8) = c;
            *reinterpret_cast<uint8_t**>(cx + 0x10) = g_scratch;
            int n0 = *reinterpret_cast<int*>(out + 8);
            reinterpret_cast<GridFn>(g_gtramp)(out, c, r, reinterpret_cast<int64_t>(cx));
            int64_t* d = *reinterpret_cast<int64_t**>(out);
            int n = *reinterpret_cast<int*>(out + 8);
            int w = d ? cgcmc::dedup_tail(d, n0, n) : n;
            *reinterpret_cast<int*>(out + 8) = w;
            added = w - n0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void __fastcall grid_detour(int64_t out, float* c, float r, int64_t ctx)
    {
        reinterpret_cast<GridFn>(g_gtramp)(out, c, r, ctx);
        if (!g_aon || !out || !ctx) return;
        cgcmc::P cs[kMax];
        int n = centres(cs);
        int passes = cgcmc::online_passes(true, (size_t)n);
        for (int i = 1; i < passes; i++)
        {
            int added = 0;
            if (!grid_seh(out, cs[i - 1], r, ctx, added)) break;
            InterlockedIncrement64(&g_passes);
            InterlockedAdd64(&g_brought, added);
        }
    }

    bool hook_one(uint8_t* p, uint64_t cb, uint64_t* tramp, PLH::x64Detour*& det)
    {
        det = new x0::hot::Detour((uint64_t)p, cb, tramp);
        if (det->hook()) return true;
        delete det;
        det = nullptr;
        *tramp = 0;
        return false;
    }

    bool install_alife()
    {
        if (g_fdet) return true;
        if (g_atried) return false;
        g_atried = true;
        static const uint8_t f[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xec, 0x30, 0x0f, 0x29, 0x74, 0x24, 0x20, 0x0f, 0x28, 0xf1, 0x48, 0x89, 0xce, 0x8b, 0x05, 0xc0, 0x4f, 0xb3, 0x08};
        static const uint8_t vg[] = {0xf3, 0x0f, 0x10, 0x0d, 0x8f, 0xed, 0x5d, 0x09, 0xe8, 0x2e, 0xfe, 0x91, 0x01};
        static const uint8_t cap[] = {0x8b, 0x47, 0x54, 0x3b, 0x05, 0x4e, 0xad, 0xcb, 0x07};
        static const uint8_t g[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xec, 0x68, 0x0f, 0x29, 0x7c, 0x24, 0x50, 0x0f, 0x29, 0x74, 0x24, 0x40, 0x4c, 0x89, 0xce, 0x48, 0x89, 0xd7, 0x48, 0x89, 0xcb};
        static const uint8_t gc[] = {0x48, 0x89, 0x7b, 0x08, 0x48, 0x89, 0x73, 0x10, 0x4c, 0x89, 0x73, 0x18, 0xc6, 0x43, 0x20, 0x00};
        static const uint8_t gs[] = {0x0f, 0x28, 0xd6, 0xe8, 0x6c, 0x15, 0x00, 0x00};
        uint8_t* pf = exe() + 0x221f7d8;
        uint8_t* pg = exe() + 0x704a76;
        if (!eq_seh(pf, f, sizeof f) || !eq_seh(exe() + 0x8ff99d, vg, sizeof vg) || !eq_seh(exe() + 0x221f3e5, cap, sizeof cap))
        {
            x0::log("NOLA alifemc: bytes do teste de distancia do A-Life (exe+0x221f7d8) diferentes do esperado: NPCs perto de um cliente nao ficam online");
            return false;
        }
        if (!hook_one(pf, (uint64_t)&far_detour, &g_ftramp, g_fdet))
        {
            x0::log("NOLA alifemc: gancho do teste de distancia do A-Life falhou: NPCs perto de um cliente nao ficam online");
            return false;
        }
        if (!eq_seh(pg, g, sizeof g) || !eq_seh(exe() + 0x7034df, gc, sizeof gc) || !eq_seh(exe() + 0x703502, gs, sizeof gs) || !hook_one(pg, (uint64_t)&grid_detour, &g_gtramp, g_gdet))
            x0::log("NOLA alifemc: consulta do conjunto online nao encontrada: so o teste offline cobre os clientes");
        x0::log(g_gdet ? "NOLA alifemc: ganchos instalados (consulta online exe+0x704a76, teste de distancia exe+0x221f7d8): NPCs perto de qualquer jogador ficam online no host"
                       : "NOLA alifemc: gancho do teste de distancia instalado (exe+0x221f7d8) sem a consulta online");
        return true;
    }

    bool install()
    {
        if (g_det) return true;
        if (g_tried) return false;
        g_tried = true;
        static const uint8_t k[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xc8, 0x00, 0x00, 0x00, 0x48, 0x89, 0xd6, 0x48, 0x89, 0xcf};
        static const uint8_t r[] = {0x41, 0x57, 0x41, 0x56, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xec, 0xd8, 0x00, 0x00, 0x00};
        static const uint8_t m[] = {0x48, 0x83, 0xec, 0x28, 0x8b, 0x05};
        uint8_t* p = exe() + 0x221f272;
        if (!eq_seh(p, k, sizeof k) || !eq_seh(exe() + 0xd2352, r, sizeof r) || !eq_seh(exe() + 0xd1fc4, m, sizeof m))
        {
            x0::log("NOLA corpsemc: bytes da decisao offline do A-Life (exe+0x221f272) diferentes do esperado: corpos so ficam online perto do host");
            return false;
        }
        g_det = new x0::hot::Detour((uint64_t)p, (uint64_t)&off_detour, &g_tramp);
        if (!g_det->hook())
        {
            delete g_det;
            g_det = nullptr;
            g_tramp = 0;
            x0::log("NOLA corpsemc: gancho da decisao offline do A-Life falhou: corpos so ficam online perto do host");
            return false;
        }
        x0::log("NOLA corpsemc: gancho da decisao offline do A-Life instalado (exe+0x221f272): um corpo perto de qualquer jogador fica online no host");
        return true;
    }

    void feed()
    {
        bool host = x0::is_host();
        bool on = host && F_mc.on();
        bool al = host && F_al.on();
        std::vector<cgcmc::P> cs;
        if (on || al)
        {
            double now = x0::now();
            std::vector<cgcmc::View> vs;
            x0::each_view([&](int, const x0::Snap& s, const x0::Peer& p) {
                cgcmc::View w;
                w.ok = s.ok;
                w.has_pos = s.has_pos;
                w.up = p.state == x0::Peer::up;
                w.left = p.left;
                w.age = s.recv >= 0 ? now - s.recv : 1e9;
                w.p = {(float)s.x, (float)s.y, (float)s.z};
                vs.push_back(w);
            });
            cs = cgcmc::extra_centres(vs, kAge);
        }
        int n = (int)cs.size() < kMax ? (int)cs.size() : kMax;
        if ((on || al) && n > 0 && !install()) on = false;
        if (al && cgcmc::wants_unit(al, host, (size_t)n) && !install_alife()) al = false;
        if (!on && !al) n = 0;
        AcquireSRWLockExclusive(&g_lk);
        for (int i = 0; i < n; i++) g_cs[i] = cs[i];
        g_n = n;
        ReleaseSRWLockExclusive(&g_lk);
        InterlockedExchange(&g_on, on && n > 0 ? 1 : 0);
        InterlockedExchange(&g_aon, cgcmc::wants_unit(al, host, (size_t)n) ? 1 : 0);
    }

    void cap_log()
    {
        int64_t ring = (int64_t)g_ring;
        int online = 0, cap = 0;
        if (!g_aon || !ring || !int_seh(reinterpret_cast<const uint8_t*>(ring + 0x54), online) || !int_seh(exe() + 0x9eda13c, cap)) return;
        bool hit = cgcmc::cap_hit(online, cap);
        if (hit && !g_capLogged)
        {
            char b[160];
            std::snprintf(b, sizeof b, "NOLA alifemc: limite de agentes online %d atingido (online=%d centros=%d)", cap, online, g_n);
            x0::log(b);
        }
        g_capLogged = hit;
    }

    void report()
    {
        if (!g_det && !g_fdet) return;
        cap_log();
        double now = x0::now();
        long long t = (long long)g_tests, k = (long long)g_kept, h = (long long)g_held;
        int n = g_n;
        if (k == g_logKept && h == g_logHeld && n == g_logN && !g_aon) return;
        if (now - g_logAt < 30 && n == g_logN) return;
        int online = -1, cap = -1;
        int64_t ring = (int64_t)g_ring;
        if (ring) int_seh(reinterpret_cast<const uint8_t*>(ring + 0x54), online);
        int_seh(exe() + 0x9eda13c, cap);
        char b[420];
        std::snprintf(b, sizeof b, "NOLA corpsemc: jogadores remotos=%d testes=%lld (+%lld) corpos mantidos online por estarem perto de um remoto=%lld (+%lld) alifemc=%d unidades_mantidas=%lld passes_online=%lld unidades_trazidas=%lld centros=%d agentes_online=%d limite=%d", n, t, t - g_logTests, k, k - g_logKept, (int)g_aon, h, (long long)g_passes, (long long)g_brought, n, online, cap);
        x0::log(b);
        g_logAt = now;
        g_logTests = t;
        g_logKept = k;
        g_logHeld = h;
        g_logN = n;
    }

    void init()
    {
        x0::every(x0::Mod::bodies, "corpsemc", x0::Rate::t100, [] { feed(); });
        x0::every(x0::Mod::bodies, "corpsemcLog", x0::Rate::t1000, [] { report(); });
    }
    ::x0::ModReg g_reg(::x0::Mod::bodies, "corpsemc", &init);

    bool raw_seh(float& r, float& k)
    {
        __try
        {
            r = *reinterpret_cast<volatile float*>(exe() + 0xb10e258);
            k = *reinterpret_cast<volatile float*>(exe() + 0x9ede734);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
}

bool cgcmc::online_radius(float& r, float& k) { return raw_seh(r, k) && cgcmc::live_ok(r, k); }
