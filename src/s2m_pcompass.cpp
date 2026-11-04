#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>

#include "s2x_hot.h"

#include "s2m_bodies.h"
#include "s2m_pcompass_logic.h"
#include "s2x.h"

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    const uint32_t kMarkAdd = 0xd05340;
    const uint32_t kMarkDel = 0xd01d10;
    const uint32_t kMarkBucket = 0x9b51cc;
    const uint32_t kMarkMove = 0xd2296;
    const uint32_t kMapMgr = 0xa2c8650;

    struct FadeSite
    {
        uint32_t rva;
        uint8_t pre[3];
        uint8_t post[2];
    };
    const FadeSite kFade[2] = {{0x24ab1da, {0xF9, 0x81, 0xE1}, {0x0F, 0x84}}, {0x24ab36c, {0x41, 0xF7, 0xC7}, {0x0F, 0x85}}};
    const int kFadeOff = 3;

    using AddFn = void* (*)(uint64_t, int32_t, uint32_t, uint32_t*);
    using DelFn = void (*)(uint64_t, uint32_t);
    using BucketFn = void (*)(uint64_t, const double*);
    using MoveFn = void (*)(uint64_t, const float*);

    x0::Flag F_pcompass("pcompass");
    x0::Flag F_pcompexpl("pcompexpl");

    struct St
    {
        uint64_t mgr = 0;
        bool failed = false;
        bool fadeOn = false;
        bool fadeBad = false;
        int lastN = -2;
        int32_t lastU = 0;
        int moveLog = 0;
        double holdUntil = 0;
        int saves = 0;
    };
    St g;
    const double kSaveHold = 0.5;

    uint64_t mgr()
    {
        uint64_t v = 0;
        __try { std::memcpy(&v, exe() + kMapMgr, 8); } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
        return v;
    }

    bool fade_site_ok(const FadeSite& s, uint8_t& cur)
    {
        __try
        {
            const uint8_t* p = exe() + s.rva;
            if (std::memcmp(p - 3, s.pre, 3) != 0 || std::memcmp(p + 4, s.post, 2) != 0) return false;
            if (p[0] != 0x00 || p[1] != 0x1C || p[2] != 0x00) return false;
            cur = p[kFadeOff];
            return pcomp::fade_byte_known(cur);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    uint8_t* fade_at(const FadeSite& s) { return exe() + s.rva + kFadeOff; }

    bool fade_poke(uint8_t* p, uint8_t v)
    {
        DWORD old = 0;
        if (!VirtualProtect(p, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
        *p = v;
        VirtualProtect(p, 1, old, &old);
        FlushInstructionCache(GetCurrentProcess(), p, 1);
        return *p == v;
    }

    void fade_restore_on_reload()
    {
        static bool once = false;
        if (once) return;
        once = true;
        x0::hot::on_shutdown([] {
            for (const FadeSite& s : kFade)
                if (*fade_at(s) == pcomp::kFadeMaskPatched) fade_poke(fade_at(s), pcomp::kFadeMaskStock);
        });
    }

    bool fade_write(const FadeSite& s, uint8_t v)
    {
        fade_restore_on_reload();
        return fade_poke(fade_at(s), v);
    }

    void fade_apply(bool on)
    {
        if (g.fadeBad || g.fadeOn == on) return;
        uint8_t v = pcomp::fade_byte(on);
        for (const FadeSite& s : kFade)
        {
            uint8_t cur = 0;
            if (!fade_site_ok(s, cur))
            {
                g.fadeBad = true;
                x0::logf("NOLA pcompass: exe+0x%x (mascara de tipos sem fade do compasso) com bytes inesperados: o marcador do parceiro some alem de 30 m nesta versao do jogo", s.rva);
                return;
            }
            if (cur != v && !fade_write(s, v))
            {
                g.fadeBad = true;
                x0::logf("NOLA pcompass: escrita em exe+0x%x falhou: o marcador do parceiro some alem de 30 m", s.rva);
                return;
            }
        }
        g.fadeOn = on;
        x0::logf("NOLA pcompass: fade de 30 m do compasso %s para o tipo PartyLeader (exe+0x%x, exe+0x%x)", on ? "desligado" : "restaurado", kFade[0].rva, kFade[1].rva);
    }

    int find_recs(uint64_t m, uint64_t recs[pcomp::kViews], int32_t& firstUid)
    {
        int n = 0;
        firstUid = 0;
        for (int v = 0; v < pcomp::kViews; ++v) recs[v] = 0;
        __try
        {
            int32_t cnt = *reinterpret_cast<int32_t*>(m + 0x188);
            uint64_t arr = *reinterpret_cast<uint64_t*>(m + 0x180);
            if (cnt <= 0 || cnt > 100000 || !arr) return 0;
            for (int32_t i = 0; i < cnt; ++i)
            {
                uint64_t rec = *reinterpret_cast<uint64_t*>(arr + uint64_t(i) * 0x40 + 8);
                if (!rec || *reinterpret_cast<uint8_t*>(rec + 0x81) != pcomp::kPartyLeader) continue;
                int32_t uid = *reinterpret_cast<int32_t*>(rec + 0x38);
                if (!n) firstUid = uid;
                ++n;
                int v = pcomp::view_of(uid);
                if (v >= 0 && !recs[v]) recs[v] = rec;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
        return n;
    }

    bool add_seh(uint64_t m, int32_t uid)
    {
        __try
        {
            uint32_t name = 0;
            reinterpret_cast<AddFn>(exe() + kMarkAdd)(m, uid, pcomp::kPartyLeader, &name);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool state_seh(uint64_t rec, int& st0, int& st1)
    {
        st0 = st1 = -1;
        __try
        {
            uint8_t* p = reinterpret_cast<uint8_t*>(rec);
            st0 = p[0x80];
            p[0x80] = pcomp::start_state(p[0x80], F_pcompexpl.on());
            st1 = p[0x80];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool del_seh(uint64_t m, int32_t uid)
    {
        __try { reinterpret_cast<DelFn>(exe() + kMarkDel)(m, static_cast<uint32_t>(uid)); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool place_seh(uint64_t rec, const double* pos, bool& rebucket)
    {
        rebucket = false;
        __try
        {
            uint32_t have = *reinterpret_cast<uint32_t*>(rec + 0x2c);
            if (!pcomp::cell_valid(have))
            {
                reinterpret_cast<BucketFn>(exe() + kMarkBucket)(rec + 0x10, pos);
                rebucket = true;
                return true;
            }
            float f[3] = {static_cast<float>(pos[0]), static_cast<float>(pos[1]), static_cast<float>(pos[2])};
            reinterpret_cast<MoveFn>(exe() + kMarkMove)(rec + 0x10, f);
            rebucket = *reinterpret_cast<uint32_t*>(rec + 0x2c) != have;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool world_ready() { return m01::body_gate() && x0::first_of(L"BP_Stalker2Character_C").valid(); }

    void tally(int n, int32_t u)
    {
        if (n == g.lastN && u == g.lastU) return;
        g.lastN = n;
        g.lastU = u;
        x0::logf("NOLA pcompass: marcadores PartyLeader no mapa=%d primeiro uid=0x%x", n, static_cast<uint32_t>(u));
    }

    void fail(const char* what, int view)
    {
        g.failed = true;
        x0::logf("NOLA pcompass: chamada nativa %s falhou (view=%d uid=0x%x): marcador do parceiro desligado ate recarregar", what, view, static_cast<uint32_t>(pcomp::uid_for(view)));
    }

    void tick()
    {
        if (g.failed) return;
        bool on = F_pcompass.on();
        fade_apply(on);
        if (!world_ready()) return;
        uint64_t m = mgr();
        if (!m)
        {
            g.mgr = 0;
            return;
        }
        if (m != g.mgr)
        {
            g.mgr = m;
            g.lastN = -2;
        }
        uint64_t recs[pcomp::kViews];
        int32_t firstUid = 0;
        int n = find_recs(m, recs, firstUid);
        if (n < 0) return fail("leitura dos marcadores", -1);
        pcomp::Flags present{}, want{};
        double pos[pcomp::kViews][3]{};
        bool held = x0::now() < g.holdUntil;
        for (int v = 0; v < pcomp::kViews; ++v)
        {
            present[v] = recs[v] != 0;
            if (held || v >= x0::kMaxClients || !m01::view_live(v)) continue;
            const x0::Snap& s = x0::remote(v);
            if (!s.has_pos) continue;
            want[v] = true;
            pos[v][0] = s.x;
            pos[v][1] = s.y;
            pos[v][2] = s.z;
        }
        std::vector<pcomp::Op> ops = pcomp::plan(present, want, on);
        for (const pcomp::Op& op : ops)
        {
            int32_t uid = pcomp::uid_for(op.view);
            if (!op.add)
            {
                if (!del_seh(m, uid)) return fail("del", op.view);
                x0::logf("NOLA pcompass: removido marcador PartyLeader do compasso view=%d uid=0x%x (parceiro ausente ou pcompass=0)", op.view, static_cast<uint32_t>(uid));
                continue;
            }
            uint64_t rec = recs[op.view];
            int st0 = -1, st1 = -1;
            if (op.fresh)
            {
                if (!add_seh(m, uid)) return fail("add", op.view);
                uint64_t tmp[pcomp::kViews];
                int32_t fu = 0;
                if (find_recs(m, tmp, fu) < 0 || !tmp[op.view]) return fail("add (registro nao apareceu)", op.view);
                rec = tmp[op.view];
                if (!state_seh(rec, st0, st1)) return fail("estado", op.view);
            }
            bool rebucket = false;
            if (!place_seh(rec, pos[op.view], rebucket)) return fail("pos", op.view);
            if (op.fresh)
                x0::logf("NOLA pcompass: criado marcador PartyLeader no compasso view=%d uid=0x%x estado=%d->%d pos=%.0f %.0f %.0f celula=%u", op.view, static_cast<uint32_t>(uid), st0, st1, pos[op.view][0], pos[op.view][1], pos[op.view][2], pcomp::cell(pos[op.view][0], pos[op.view][1]));
            else if (rebucket && g.moveLog < 20)
            {
                ++g.moveLog;
                x0::logf("NOLA pcompass: marcador view=%d mudou de celula -> %u pos=%.0f %.0f", op.view, pcomp::cell(pos[op.view][0], pos[op.view][1]), pos[op.view][0], pos[op.view][1]);
            }
        }
        if (!ops.empty()) n = find_recs(m, recs, firstUid);
        tally(n, firstUid);
    }

    void init() { x0::every(x0::Mod::bodies, "pcompass", x0::Rate::t100, [] { tick(); }); }
    ::x0::ModReg g_reg(::x0::Mod::bodies, "pcompass", &init);
}

extern "C" void s2m_pcompass_before_save()
{
    g.holdUntil = x0::now() + kSaveHold;
    if (g.failed) return;
    uint64_t m = mgr();
    if (!m) return;
    uint64_t recs[pcomp::kViews];
    int32_t firstUid = 0;
    if (find_recs(m, recs, firstUid) < 0) return;
    int n = 0;
    for (int v = 0; v < pcomp::kViews; ++v)
        if (recs[v] && del_seh(m, pcomp::uid_for(v))) ++n;
    if (n && g.saves++ < 10) x0::logf("NOLA pcompass: %d marcador(es) PartyLeader retirados antes do save (voltam em %.1f s): o registro nao vai para o arquivo", n, kSaveHold);
}
