#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>

#include "s2m_bodies.h"
#include "s2m_pcompass_logic.h"
#include "s2x.h"

namespace
{
    uint8_t* exe() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }

    const uint32_t kMarkAdd = 0xd05340;
    const uint32_t kMarkDel = 0xd01d10;
    const uint32_t kMapMgr = 0xa2c8650;

    using AddFn = void* (*)(uint64_t, int32_t, uint32_t, uint32_t*);
    using DelFn = void (*)(uint64_t, uint32_t);

    x0::Flag F_pcompass("pcompass");
    x0::Flag F_pcompexpl("pcompexpl");

    struct St
    {
        pcomp::Uids cur{};
        uint64_t mgr = 0;
        bool failed = false;
        int lastN = -2;
        int32_t lastU = 0;
    };
    St g;

    uint64_t mgr()
    {
        uint64_t v = 0;
        __try { std::memcpy(&v, exe() + kMapMgr, 8); } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
        return v;
    }

    bool add_seh(uint64_t m, int32_t uid, bool fresh, int& st0, int& st1)
    {
        st0 = st1 = -1;
        __try
        {
            uint32_t name = 0;
            uint8_t* rec = static_cast<uint8_t*>(reinterpret_cast<AddFn>(exe() + kMarkAdd)(m, uid, pcomp::kPartyLeader, &name));
            if (fresh && rec && rec[0x81] == pcomp::kPartyLeader && *reinterpret_cast<int32_t*>(rec + 0x38) == uid)
            {
                st0 = rec[0x80];
                rec[0x80] = pcomp::start_state(rec[0x80], F_pcompexpl.on());
                st1 = rec[0x80];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool del_seh(uint64_t m, int32_t uid)
    {
        __try { reinterpret_cast<DelFn>(exe() + kMarkDel)(m, static_cast<uint32_t>(uid)); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int party_count(uint64_t m, int32_t& firstUid)
    {
        int n = 0;
        firstUid = 0;
        __try
        {
            int32_t cnt = *reinterpret_cast<int32_t*>(m + 0x188);
            uint64_t arr = *reinterpret_cast<uint64_t*>(m + 0x180);
            if (cnt <= 0 || cnt > 100000 || !arr) return 0;
            for (int32_t i = 0; i < cnt; ++i)
            {
                uint64_t rec = *reinterpret_cast<uint64_t*>(arr + uint64_t(i) * 0x40 + 8);
                if (!rec || *reinterpret_cast<uint8_t*>(rec + 0x81) != pcomp::kPartyLeader) continue;
                if (!n) firstUid = *reinterpret_cast<int32_t*>(rec + 0x38);
                ++n;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
        return n;
    }

    void tally(uint64_t m)
    {
        int32_t u = 0;
        int n = party_count(m, u);
        if (n == g.lastN && u == g.lastU) return;
        g.lastN = n;
        g.lastU = u;
        x0::logf("NOLA pcompass: marcadores PartyLeader no mapa=%d primeiro uid=%d", n, u);
    }

    void tick()
    {
        if (g.failed) return;
        uint64_t m = mgr();
        if (!m)
        {
            g.cur = {};
            g.mgr = 0;
            return;
        }
        if (m != g.mgr)
        {
            g.cur = {};
            g.mgr = m;
        }
        pcomp::Uids want{};
        for (int v = 0; v < pcomp::kViews && v < x0::kMaxClients; ++v)
        {
            if (!m01::view_live(v) || !m01::puppet(v).valid()) continue;
            auto u = m01::puppet_uid(v);
            if (u) want[v] = pcomp::uid32(*u);
        }
        pcomp::Uids before = g.cur;
        for (const pcomp::Op& op : pcomp::step(g.cur, want, F_pcompass.on()))
        {
            bool fresh = op.add && before[op.view] != op.uid;
            int st0 = -1, st1 = -1;
            bool ok = op.add ? add_seh(m, op.uid, fresh, st0, st1) : del_seh(m, op.uid);
            if (!ok)
            {
                g.failed = true;
                x0::logf("NOLA pcompass: chamada nativa %s falhou (uid=%d): marcador do parceiro desligado ate recarregar", op.add ? "add" : "del", op.uid);
                return;
            }
            if (!op.add || fresh)
                x0::logf("NOLA pcompass: %s marcador PartyLeader no compasso view=%d uid=%d estado=%d->%d", op.add ? "criado" : "removido", op.view, op.uid, st0, st1);
        }
        tally(m);
    }

    void init() { x0::every(x0::Mod::bodies, "pcompass", x0::Rate::t1000, [] { tick(); }); }
    ::x0::ModReg g_reg(::x0::Mod::bodies, "pcompass", &init);
}
