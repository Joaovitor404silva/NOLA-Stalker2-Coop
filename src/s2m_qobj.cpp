#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "s2m_ixtap.h"
#include "s2m_qobj.h"
#include "s2m_qobj_logic.h"
#include "s2m_text.h"
#include "s2x.h"

extern "C" bool s2m_solo_quest_muted(const char* quest);
extern "C" bool s2m_solo_quest_hostonly(const char* quest);

namespace
{
    using x0::Ref;

    struct St
    {
        qobj::Table tab;
        int loaded = 0;
        qobj::Gate gate;
        qobj::RowCache row;
        double sumAt = 0;
        int lastBlocked = -1;
        Ref gcls;
        int logged = 0;
    };
    St g;

    bool table()
    {
        if (g.loaded != 0) return g.loaded > 0;
        g.loaded = -1;
        std::string text;
        if (!x0::data("s2u_qobj.tsv", text))
        {
            x0::log("NOLA qhostonly: falta s2u_qobj.tsv: nenhum objetivo bloqueado");
            return false;
        }
        int n = qobj::parse(text, g.tab);
        g.loaded = 1;
        x0::logf("NOLA qhostonly: tabela carregada (%d objetivos de missao com guid)", n);
        return true;
    }

    std::vector<std::string> guids_of(const Ref& a)
    {
        std::vector<std::string> out;
        if (!g.gcls) g.gcls = x0::find_path(L"/Script/Stalker2.GuidActorComponent");
        if (!g.gcls) return out;
        x0::Call c(a, L"K2_GetComponentsByClass");
        c.o(L"ComponentClass", g.gcls);
        if (!c.run()) return out;
        for (const Ref& comp : c.rarr())
        {
            int64_t off = x0::prop_off(comp, "Guid");
            if (off < 0) continue;
            const int32_t* v = reinterpret_cast<const int32_t*>(comp.address() + off);
            if (!(v[0] | v[1] | v[2] | v[3])) continue;
            out.push_back(qobj::guid_hex(v[0], v[1], v[2], v[3]));
        }
        return out;
    }

    const qobj::Row* row_of(const Ref& a)
    {
        std::string name = x0::obj_name(a);
        const qobj::Row* r = nullptr;
        if (qobj::cache_get(g.row, a.address(), name, r)) return r;
        std::vector<std::string> gs = guids_of(a);
        r = qobj::lookup_any(g.tab, gs);
        if (r && g.logged < 20)
        {
            ++g.logged;
            x0::logf("NOLA qhostonly: %s e objetivo da missao %s (no %s; %d guids de componente)", name.c_str(), r->quest.c_str(), r->node.c_str(), (int)gs.size());
        }
        qobj::cache_put(g.row, a.address(), name, r);
        return r;
    }

    bool tap_gate()
    {
        if (x0::fi("qhostonly", 1) != 1 || !x0::coop() || !x0::is_client() || !table()) return false;
        Ref p = x0::first_of(L"BP_Stalker2Character_C");
        if (!p) return false;
        Ref a = x0::call_o(p, L"GetInteractedActor");
        if (!a) return false;
        const qobj::Row* r = row_of(a);
        if (!qobj::block(true, true, r, r && s2m_solo_quest_muted(r->quest.c_str()))) return false;
        double now = x0::now();
        bool warn = qobj::warn_due(g.gate, now, x0::fd("qhostonlygap", 3));
        x0::logf("NOLA qhostonly: toque do cliente em %s bloqueado (missao %s)", x0::obj_name(a).c_str(), r->quest.c_str());
        if (warn) x0::ui_msg("qhostonly", nola_text::tx(nola_text::T::qhostonly_block), x0::fd("qhostonlygap", 3));
        return true;
    }

    void tick()
    {
        s2m_ixtap::gate(&tap_gate);
        if (!x0::coop() || !x0::is_client()) return;
        double now = x0::now();
        if (now < g.sumAt) return;
        g.sumAt = now + 10;
        if (g.gate.blocked == g.lastBlocked) return;
        g.lastBlocked = g.gate.blocked;
        x0::logf("NOLA qhostonly 10 s: qhostonly=%d bloqueados=%d avisos=%d", (int)x0::fi("qhostonly", 1), g.gate.blocked, g.gate.warns);
    }

    void qobj_init()
    {
        x0::every(x0::Mod::quest, "qhostonly", x0::Rate::t1000, [] { tick(); });
    }
    ::x0::ModReg g_qobj_reg(::x0::Mod::quest, "qhostonly", &qobj_init);
}

bool s2m_qobj_is_objective(const x0::Ref& actor)
{
    if (!actor.valid() || !table()) return false;
    const qobj::Row* r = row_of(actor);
    if (!r) return false;
    return x0::is_host() ? s2m_solo_quest_hostonly(r->quest.c_str()) : s2m_solo_quest_muted(r->quest.c_str());
}
