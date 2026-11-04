#include <windows.h>
#include <cstdint>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "s2m_lockbehind_logic.h"
#include "s2x.h"
#include "s2x_hot.h"

namespace
{
    struct St
    {
        lockbehind::Table tab;
        std::unordered_map<std::string, lockbehind::Vol> vols;
        int loaded = 0;
        lockbehind::Gate gate;
        std::vector<int> live;
        std::set<std::string> allow;
        double at = 0;
        bool wasOn = false;
        uint64_t world = 0;
        bool downReg = false;
    };
    St g;
    SRWLOCK g_lk = SRWLOCK_INIT;
    volatile LONG g_on = 0;

    bool table()
    {
        if (g.loaded != 0) return g.loaded > 0;
        g.loaded = -1;
        std::string text, trig;
        if (!x0::data("s2u_lockbehind.tsv", text) || !x0::data("s2u_triggers.tsv", trig))
        {
            x0::log("NOLA lockbehind: falta s2u_lockbehind.tsv ou s2u_triggers.tsv: portas fecham como no jogo original");
            return false;
        }
        int n = lockbehind::parse(text, g.tab);
        int v = lockbehind::parse_vols(trig, g.tab.trigs, g.vols);
        g.loaded = n > 0 && v > 0 ? 1 : -1;
        x0::logf("NOLA lockbehind: tabela carregada (%d nos que fecham portas atras do jogador, %d gatilhos com volume; %d nos com mais de um gatilho ficam no original)", n, v, static_cast<int>(g.tab.multi));
        return g.loaded > 0;
    }

    std::string w2a(const wchar_t* w)
    {
        std::string o;
        for (; w && *w; ++w) o.push_back(static_cast<char>(*w < 128 ? *w : '?'));
        return o;
    }

    std::wstring a2w(const std::string& s) { return std::wstring(s.begin(), s.end()); }

    void start_node(const lockbehind::Pend& p, int in, int of)
    {
        x0::Ref aa = x0::first_of(L"CustomConsoleManagerAA");
        bool ok = false;
        if (aa)
        {
            x0::Call c(aa, L"XStartQuestNodeBySID");
            c.s(L"SID", a2w(p.node));
            ok = c.run();
        }
        if (in < 0) x0::logf("NOLA lockbehind: liberado sem esperar (%s): no %s iniciado ok=%d", p.trig.c_str(), p.node.c_str(), ok ? 1 : 0);
        else x0::logf("NOLA lockbehind: todos dentro de %s (%d de %d): no %s iniciado ok=%d", p.trig.c_str(), in, of, p.node.c_str(), ok ? 1 : 0);
    }

    std::vector<lockbehind::Pos> positions(const std::vector<int>& live)
    {
        std::vector<lockbehind::Pos> out;
        x0::Vec3 hp;
        x0::Ref pw = x0::first_of(L"BP_Stalker2Character_C");
        if (pw && x0::call_v(pw, L"K2_GetActorLocation", hp)) out.push_back({lockbehind::kHost, hp.x, hp.y, hp.z});
        for (size_t i = 1; i < live.size(); ++i)
        {
            const x0::Snap& s = x0::remote(live[i]);
            if (s.has_pos) out.push_back({live[i], s.x, s.y, s.z});
        }
        return out;
    }

    void flush(const char* why)
    {
        AcquireSRWLockExclusive(&g_lk);
        std::vector<lockbehind::Pend> all = lockbehind::drain(g.gate);
        for (auto& p : all) g.allow.insert(lockbehind::lower(p.node));
        const int of = static_cast<int>(g.live.size());
        ReleaseSRWLockExclusive(&g_lk);
        if (all.empty()) return;
        x0::logf("NOLA lockbehind: %s: %d no(s) segurado(s) iniciados agora para a missao nao travar", why, static_cast<int>(all.size()));
        for (auto& p : all) start_node(p, -1, of);
    }

    void tick()
    {
        double now = x0::now();
        if (now - g.at < 0.5) return;
        g.at = now;
        if (!g.downReg)
        {
            g.downReg = true;
            x0::hot::on_shutdown([] { flush("modulo descarregado (hot reload)"); });
        }
        const uint64_t w = x0::world().address();
        if (w != g.world)
        {
            AcquireSRWLockExclusive(&g_lk);
            const size_t dropped = g.gate.pend.size();
            lockbehind::drain(g.gate);
            g.allow.clear();
            ReleaseSRWLockExclusive(&g_lk);
            if (dropped) x0::logf("NOLA lockbehind: mundo trocado: %d no(s) segurado(s) do mundo anterior descartados", static_cast<int>(dropped));
            g.world = w;
        }
        const bool on = x0::coop() && x0::is_host() && x0::fi("lockbehind", 1) == 1 && table();
        InterlockedExchange(&g_on, on ? 1 : 0);
        if (on != g.wasOn)
        {
            g.wasOn = on;
            x0::logf("NOLA lockbehind: %s", on ? "ligado: portas que fecham atras do jogador esperam todos os jogadores conectados" : "desligado: comportamento original");
        }
        std::vector<int> live{lockbehind::kHost};
        if (on)
            for (int v = 0; v < x0::views(); ++v)
            {
                const x0::Peer& p = x0::peer(v);
                if (lockbehind::live_peer(p.loopback, p.state == x0::Peer::up, p.state == x0::Peer::silent, p.left)) live.push_back(v);
            }
        std::vector<lockbehind::Pend> rel;
        const std::vector<lockbehind::Pos> pos = on ? positions(live) : std::vector<lockbehind::Pos>{};
        AcquireSRWLockExclusive(&g_lk);
        if (!on)
        {
            rel = lockbehind::drain(g.gate);
            g.allow.clear();
        }
        else
        {
            g.live = live;
            lockbehind::prune(g.gate, live);
            lockbehind::sample(g.gate, g.vols, pos, x0::fd("trigmargin", 150));
            rel = lockbehind::release(g.gate, live);
            for (auto& p : rel) g.allow.insert(lockbehind::lower(p.node));
        }
        ReleaseSRWLockExclusive(&g_lk);
        for (auto& p : rel) start_node(p, static_cast<int>(live.size()), static_cast<int>(live.size()));
    }
}

extern "C" void s2m_lockbehind_tick() { tick(); }

extern "C" bool s2m_lockbehind_on() { return g_on != 0; }

extern "C" void s2m_lockbehind_before_save() { flush("antes do save"); }

extern "C" void s2m_lockbehind_world_exit() { flush("saida do mundo"); }

extern "C" bool s2m_lockbehind_hold(const wchar_t* nodeLower)
{
    if (!g_on || !nodeLower) return false;
    std::string n = w2a(nodeLower);
    bool fresh = false, copy = false;
    int in = 0, of = 0;
    AcquireSRWLockShared(&g_lk);
    const bool listed = lockbehind::row_of(g.tab, n, copy) != nullptr;
    const std::vector<int> live = g.live;
    ReleaseSRWLockShared(&g_lk);
    if (!listed) return false;
    const std::vector<lockbehind::Pos> pos = positions(live);
    AcquireSRWLockExclusive(&g_lk);
    const bool allowed = g.allow.erase(n) > 0;
    const lockbehind::Row* r = allowed ? nullptr : lockbehind::hold(g.tab, g.gate, n, g.live, x0::now(), fresh, g.vols, pos, x0::fd("trigmargin", 150));
    if (r)
    {
        in = lockbehind::count_in(g.gate, r->trig, g.live);
        of = static_cast<int>(g.live.size());
    }
    ReleaseSRWLockExclusive(&g_lk);
    if (r && fresh && copy) x0::logf("NOLA lockbehind: copia %s_S2Q segurada junto: os clientes nao recebem o fechamento ate todos entrarem", r->node.c_str());
    if (r && fresh && !copy) x0::logf("NOLA lockbehind: no %s segurado: %d de %d jogadores dentro do gatilho %s, a porta fica aberta ate todos entrarem", r->node.c_str(), in, of, r->trig.c_str());
    return r != nullptr;
}
