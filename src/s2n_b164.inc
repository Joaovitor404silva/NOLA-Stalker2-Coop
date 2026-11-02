#include "s2n_b164_tab.inc"

    auto obj_at(double a) -> UObject*
    {
        auto p = static_cast<uintptr_t>(a);
        if (!p || !UObject::IsReal(reinterpret_cast<void*>(p))) return nullptr;
        return reinterpret_cast<UObject*>(p);
    }
    auto interaction_set(UObject* a, bool on) -> int
    {
        std::vector<UObject*> l;
        comps_of(a, STR("/Script/Stalker2.InteractionComponent"), l);
        if (l.empty()) if (UObject* n = prop_obj(a, STR("NPCComponent"))) l.push_back(n);
        int n = 0;
        for (UObject* c : l)
            if (c && s2inter::flip(stock_owned(c), true, prop_bool(c, STR("bIsInteractionActive"), !on), on)) { if (c_set_bool(c, STR("SetInteractionActive"), STR("bActive"), on)) ++n; }
        return n;
    }

    struct LkSlot { std::string key; double since = 0; bool capLog = false, ageLog = false; };
    std::unordered_map<std::string, LkSlot> g_lk;
    auto lk_hold_api(const std::string& slot, const std::string& key, double age, double lease, double hmax) -> std::string
    {
        double now = now_s();
        LkSlot& s = g_lk[slot];
        if (key != s.key) { s.key = key; s.since = now; s.capLog = false; s.ageLog = false; }
        std::string r = key;
        if (!key.empty() && !slot.empty() && slot[0] == 'r' && age > lease)
        {
            r.clear();
            if (!s.ageLog) { s.ageLog = true; logl("NOLA b164 lease: " + slot + " " + key + " solto (o outro jogador sem snapshot ha " + std::to_string((int)age) + " s > " + std::to_string((int)lease) + " s)"); }
        }
        else if (!key.empty() && hmax > 0 && now - s.since > hmax)
        {
            r.clear();
            if (!s.capLog) { s.capLog = true; logl("NOLA b164 lease: " + slot + " " + key + " solto (aberto ha mais de " + std::to_string((int)hmax) + " s)"); }
        }
        return r;
    }
    auto lua_lk_hold(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string slot = lua.is_string() ? std::string(lua.get_string()) : std::string();
        std::string key = lua.is_string() ? std::string(lua.get_string()) : std::string();
        double age = lua.is_number() ? lua.get_number() : 0;
        double lease = lua.is_number() ? lua.get_number() : 3;
        double hmax = lua.is_number() ? lua.get_number() : 120;
        lua.set_string(lk_hold_api(slot, key, age, lease, hmax));
        return 1;
    }
    struct OffE { WRef r; double at = 0; };
    std::unordered_map<uintptr_t, OffE> g_off;
    auto lk_off_api(double a, int on, double lease) -> int
    {
        double now = now_s();
        int n = 0;
        if (a != 0 && on)
        {
            auto p = static_cast<uintptr_t>(a);
            auto it = g_off.find(p);
            if (it == g_off.end() || !it->second.r.get())
            {
                UObject* o = obj_at(a);
                if (o) { OffE e; e.r.set(o); e.at = now; g_off[p] = e; }
            }
            else it->second.at = now;
            return 0;
        }
        for (auto it = g_off.begin(); it != g_off.end();)
        {
            if (now - it->second.at <= lease) { ++it; continue; }
            if (UObject* o = it->second.r.get())
            {
                int k = interaction_set(o, true);
                ++n;
                logl("NOLA b164 lease: corpo " + w2u(o->GetName()) + " sem dono ha " + std::to_string((int)lease) + " s: interacao de volta (" + std::to_string(k) + " componentes)");
            }
            it = g_off.erase(it);
        }
        return n;
    }
    auto lua_lk_off(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        int on = lua.is_number() ? (int)lua.get_number() : 0;
        double lease = lua.is_number() ? lua.get_number() : 3;
        lua.set_number(lk_off_api(a, on, lease));
        return 1;
    }

    struct SwE { WRef loot, src; std::string hid; bool nat = false; double at = 0; };
    std::vector<SwE> g_sw;
    int g_swN = 0, g_unswN = 0;
    auto swapped_src(UObject* a) -> bool
    {
        if (!a) return false;
        for (auto& s : g_sw) if (s.src.raw == a) return true;
        return false;
    }
    auto cl_swap_api(const std::string& hid, double la, double sa, int& mism) -> bool
    {
        UObject* loot = obj_at(la);
        UObject* src = sa != 0 ? obj_at(sa) : nullptr;
        bool nat = false;
        if (!src)
        {
            Ent* e = shown_by_nm(suffix_of(hid), true);
            src = e ? e->body.get() : nullptr;
            nat = src != nullptr;
        }
        if (!loot || !src || loot == src) return false;
        for (auto it = g_sw.begin(); it != g_sw.end();)
            if (it->hid == hid || it->loot.raw == loot) it = g_sw.erase(it); else ++it;
        std::vector<Part> ps;
        parts_of(src, ps);
        std::string look = look_str(ps);
        { std::string g = gen_str(src); if (!g.empty()) look += (look.empty() ? "" : "|") + std::string("MeshGen:0@") + g + "@@1"; }
        if (UObject* m = prop_obj(loot, STR("Mesh")))
        { Call s(m, STR("SetVisibility")); s.bl(STR("bNewVisibility"), true).bl(STR("bPropagateToChildren"), true); s.run(); }
        mism = -1;
        std::string why;
        if (!look.empty()) apply_look(loot, look, mism, &why);
        set_hidden(src, true);
        interaction_off(src);
        SwE s; s.loot.set(loot); s.src.set(src); s.hid = hid; s.nat = nat; s.at = now_s();
        g_sw.push_back(s);
        ++g_swN;
        char b[400];
        snprintf(b, sizeof b, "NOLA b164 um corpo: %s: o corpo de saque veste o corpo nativo %s (%s, partes=%d diferencas=%d%s); o nativo fica escondido",
                 hid.c_str(), w2u(src->GetName()).c_str(), nat ? "copia natnpc" : "gemeo do cliente", (int)ps.size(), mism, why.empty() ? "" : (" porque:" + why.substr(0, 160)).c_str());
        logl(b);
        return true;
    }
    auto lua_cl_swap(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string hid = lua.is_string() ? std::string(lua.get_string()) : std::string();
        double la = lua.is_number() ? lua.get_number() : 0;
        double sa = lua.is_number() ? lua.get_number() : 0;
        int mism = -1;
        if (!cl_swap_api(hid, la, sa, mism)) { lua.set_nil(); return 1; }
        lua.set_number(mism);
        return 1;
    }
    auto cl_swap_tick_api() -> int
    {
        for (auto it = g_sw.begin(); it != g_sw.end();)
        {
            UObject* l = it->loot.get();
            UObject* s = it->src.get();
            if (!s) { it = g_sw.erase(it); continue; }
            double x = 0, y = 0, z = 0;
            bool gone = !l || prop_bool(l, STR("bHidden"), false) || (loc_of(l, x, y, z) && pooled(x, y));
            if (gone)
            {
                set_hidden(s, false);
                interaction_off(s);
                ++g_unswN;
                logl("NOLA b164 um corpo: " + it->hid + ": corpo de saque saiu -> corpo nativo " + w2u(s->GetName()) + " de volta (sem interacao)");
                it = g_sw.erase(it);
                continue;
            }
            if (!prop_bool(s, STR("bHidden"), true)) { set_hidden(s, true); interaction_off(s); }
            ++it;
        }
        return (int)g_sw.size();
    }
    auto lua_cl_swap_tick(const LuaMadeSimple::Lua& lua) -> int
    {
        lua.set_number((double)cl_swap_tick_api());
        return 1;
    }

    auto qi_npcadd_api(const std::string& s) -> bool
    {
        static std::unordered_set<std::string> S;
        if (S.empty()) for (const char* const* p = kQiNpcAdd; *p; ++p) S.insert(*p);
        return !s.empty() && S.count(s) > 0;
    }
    auto lua_qi_npcadd(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        lua.set_bool(qi_npcadd_api(s));
        return 1;
    }
    auto qr_npc_api(const std::string& s, std::string& guid) -> bool
    {
        static std::unordered_map<std::string, std::string> M;
        if (M.empty()) for (const QrRow* r = kQrNpc; r->sid; ++r) M[r->sid] = r->guid;
        auto it = M.find(s);
        if (it == M.end()) return false;
        guid = it->second;
        return true;
    }
    auto lua_qr_npc(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        std::string g;
        if (!qr_npc_api(s, g)) lua.set_nil(); else lua.set_string(g);
        return 1;
    }
    auto b164_status_api() -> std::string
    {
        char b[200];
        snprintf(b, sizeof b, "b164: leases=%d claims_off=%d um_corpo pares=%d trocas=%d devolvidos=%d tabelas qi_npc=%d qr_npc=%d",
                 (int)g_lk.size(), (int)g_off.size(), (int)g_sw.size(), g_swN, g_unswN,
                 (int)(sizeof(kQiNpcAdd) / sizeof(kQiNpcAdd[0])) - 1, (int)(sizeof(kQrNpc) / sizeof(kQrNpc[0])) - 1);
        return b;
    }
    auto lua_b164_status(const LuaMadeSimple::Lua& lua) -> int
    {
        lua.set_string(b164_status_api());
        return 1;
    }
