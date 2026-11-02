    using WsInternFn = uint32_t (*)(void*, const wchar_t*, char);
    using WsEquipFn = uint8_t (*)(void*, uint32_t, uint32_t*);
    using WsRecFn = uint8_t* (*)(void*, uint32_t);
    int g_wsOn = 0;
    int g_wsInit = 0;
    const char* g_wsState = "nao iniciado";
    uint8_t* g_wsBase = nullptr;
    WsInternFn g_wsIntern = nullptr;
    WsEquipFn g_wsEquip = nullptr;
    WsRecFn g_wsRec = nullptr;
    int g_wsHRead = 0, g_wsHChg = 0, g_wsHSent = 0, g_wsHBad = 0;
    int g_wsRx = 0, g_wsTry = 0, g_wsMade = 0, g_wsMadeFail = 0, g_wsOk = 0, g_wsFail = 0, g_wsSkip = 0, g_wsOff = 0, g_wsGive = 0;
    double g_wsLogAt = -100;
    constexpr double kWsGap = 5.0, kWsCheck = 4.0, kWsGiveFor = 60.0;
    constexpr int kWsMaxFail = 3;

    struct WsHost { std::string sid; int left = 0; double at = -100, seen = 0; };
    struct WsCli
    {
        std::string want, tried;
        void* body = nullptr;
        double seen = 0, at = -100, gaveAt = -100;
        int fails = 0;
        bool pend = false, made = false, gave = false;
    };
    std::unordered_map<int32_t, WsHost> g_wsHost;
    std::unordered_map<int32_t, WsCli> g_wsCli;
    int g_wsArmOn = 1;
    int g_wsArmN = 0, g_wsArmOk = 0, g_wsArmNoGate = 0, g_wsArmStale = 0, g_wsArmNoFlag = 0, g_wsArmHid = 0, g_wsArmNoAi = 0, g_wsArmLines = 0;
    std::unordered_map<int32_t, double> g_wsArmAt;
    struct WsArmRaw { int has = -1, osd = -1, eqt = -1; void* wmesh = nullptr; void* comp = nullptr; void* cache = nullptr; };

    auto ws_mem(const uint8_t* p, const uint8_t* b, size_t n) -> bool
    {
        __try { return std::memcmp(p, b, n) == 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto ws_ready() -> bool
    {
        if (g_wsInit) return g_wsInit > 0;
        static const uint8_t kIn[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0xC2, 0xE9};
        static const uint8_t kEq[] = {0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x56, 0x57, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x48, 0x4C, 0x89, 0xC6, 0x89, 0xD3, 0x49, 0x89};
        static const uint8_t kRc[] = {0x56, 0x48, 0x83, 0xEC, 0x20, 0x89, 0xD6, 0x8B, 0x05};
        auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
        if (!base || !ws_mem(base + 0x25db0e4, kIn, sizeof kIn) || !ws_mem(base + 0x3740bb1, kEq, sizeof kEq) || !ws_mem(base + 0x25f9944, kRc, sizeof kRc))
        {
            g_wsInit = -1;
            g_wsState = "bytes diferentes (exe novo?)";
            logl(std::string("NOLA wpnsid: ") + g_wsState + " -> arma por SID desligada");
            return false;
        }
        g_wsBase = base;
        g_wsIntern = reinterpret_cast<WsInternFn>(base + 0x25db0e4);
        g_wsEquip = reinterpret_cast<WsEquipFn>(base + 0x3740bb1);
        g_wsRec = reinterpret_cast<WsRecFn>(base + 0x25f9944);
        g_wsInit = 1;
        g_wsState = "on";
        logl("NOLA wpnsid: on (intern 0x25db0e4, equip 0x3740bb1, item 0x25f9944)");
        return true;
    }
    auto ws_ch(wchar_t c) -> bool
    {
        return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' || c == L'.';
    }
    auto ws_sid_ok(const std::string& s) -> bool
    {
        if (s.empty() || s.size() > 95) return false;
        for (char c : s) if (!ws_ch((wchar_t)(unsigned char)c)) return false;
        return true;
    }
    auto ws_hand_raw(const uint8_t* a, char* out, int cap) -> int
    {
        out[0] = 0;
        __try
        {
            const uint8_t* core = *reinterpret_cast<const uint8_t* const*>(a + 0x650);
            if (reinterpret_cast<uintptr_t>(core) < 0x10000) return -1;
            const uint8_t* eq = *reinterpret_cast<const uint8_t* const*>(core + 0x678);
            if (reinterpret_cast<uintptr_t>(eq) < 0x10000) return -1;
            uint32_t uid = *reinterpret_cast<const uint32_t*>(eq + 0x110);
            if (uid >= 0xfffffff0u) return 0;
            const uint8_t* rec = g_wsRec(nullptr, uid);
            if (reinterpret_cast<uintptr_t>(rec) < 0x10000) return -1;
            int32_t idx = *reinterpret_cast<const int32_t*>(rec + 0x38);
            if (idx == 0) return -1;
            const uint8_t* tab = g_wsBase + 0xb10f588 + (idx < 0 ? 0x20090 : 0);
            int32_t u = (idx >> 31) ^ idx;
            int32_t cnt = *reinterpret_cast<const int32_t*>(tab + 0x20008);
            if (u <= 0 || u >= cnt) return -1;
            const uint8_t* ch = reinterpret_cast<const uint8_t* const*>(tab)[u >> 10];
            if (reinterpret_cast<uintptr_t>(ch) < 0x10000) return -1;
            const uint8_t* en = ch + (size_t)(u & 0x3ff) * 0x20;
            const wchar_t* s = *reinterpret_cast<const wchar_t* const*>(en);
            int32_t len = *reinterpret_cast<const int32_t*>(en + 8);
            if (!s || len <= 0 || len > 256) return -1;
            int k = 0;
            for (int i = 0; i < len && s[i]; ++i)
            {
                if (k >= cap - 1 || !ws_ch(s[i])) { out[0] = 0; return -2; }
                out[k++] = (char)s[i];
            }
            out[k] = 0;
            return k > 0 ? k : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return -1; }
    }
    auto ws_core_raw(const uint8_t* a) -> uint8_t*
    {
        __try
        {
            uint8_t* core = *reinterpret_cast<uint8_t* const*>(a + 0x650);
            if (reinterpret_cast<uintptr_t>(core) < 0x10000) return nullptr;
            const uint8_t* eq = *reinterpret_cast<const uint8_t* const*>(core + 0x678);
            if (reinterpret_cast<uintptr_t>(eq) < 0x10000) return nullptr;
            return core;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }
    auto ws_equip_raw0(uint8_t* core, uint32_t type, const wchar_t* s) -> int
    {
        uintptr_t pf0 = s2n_pf::save();
        __try
        {
            uint32_t id = g_wsIntern(g_wsBase + 0xb10f588, s, 1);
            if ((int32_t)id <= 0) return -2;
            uint8_t r = g_wsEquip(core, type, &id);
            return r ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { s2n_pf::fix(pf0); return -1; }
    }
    auto ws_equip_raw(uint8_t* core, uint32_t type, const wchar_t* s) -> int
    {
        ++t_wpnOurs;
        int r = ws_equip_raw0(core, type, s);
        --t_wpnOurs;
        InterlockedIncrement(&g_wpOurs);
        return r;
    }
    auto ws_hand(UObject* a, std::string& out) -> int
    {
        out.clear();
        if (!a || !ws_ready()) return -1;
        char b[96];
        int n = ws_hand_raw(reinterpret_cast<const uint8_t*>(a), b, (int)sizeof b);
        if (n > 0) out = b;
        return n;
    }
    auto ws_create(int32_t luid, const std::string& sid) -> bool
    {
        UObject* m = single(g_rk, STR("CustomConsoleManagerRK"));
        if (!m || !luid) return false;
        Call c(m, STR("XCreateItemInInventoryByID"));
        c.str(STR("PrototypeID"), u2w(sid)).i(STR("ObjUID"), luid).i(STR("Count"), 1).fl(STR("Durability"), 1.f);
        return c.run();
    }
    auto ws_log(double now) -> void
    {
        if (now - g_wsLogAt < 10) return;
        g_wsLogAt = now;
        for (auto it = g_wsHost.begin(); it != g_wsHost.end();) it = now - it->second.seen > 60 ? g_wsHost.erase(it) : std::next(it);
        for (auto it = g_wsCli.begin(); it != g_wsCli.end();) it = now - it->second.seen > 120 ? g_wsCli.erase(it) : std::next(it);
        bool any = g_wsHRead || g_wsHSent || g_wsHBad || g_wsRx || g_wsTry || g_wsSkip || g_wsOff || g_wsFail || g_wsOk;
        if (C.natlog && any)
        {
            char b[520];
            snprintf(b, sizeof b, "NOLA wpnsid 10 s: natwpnsid=%d estado=%s | host: lidos=%d trocas=%d enviados=%d falha_leitura=%d npcs=%d | cliente: recebidos=%d tentativas=%d criados=%d criar_falhou=%d equipado_ok=%d falhou=%d pulado=%d fora_thread=%d desistiu=%d copias=%d",
                     g_wsOn, g_wsState, g_wsHRead, g_wsHChg, g_wsHSent, g_wsHBad, (int)g_wsHost.size(), g_wsRx, g_wsTry, g_wsMade, g_wsMadeFail, g_wsOk, g_wsFail, g_wsSkip, g_wsOff, g_wsGive, (int)g_wsCli.size());
            logl(b);
        }
        for (auto it = g_wsArmAt.begin(); it != g_wsArmAt.end();) it = now - it->second > 120 ? g_wsArmAt.erase(it) : std::next(it);
        if (C.natlog && g_wsArmN)
        {
            char b[400];
            snprintf(b, sizeof b, "NOLA wpnarm 10 s: natwpnarm=%d checados=%d ok=%d sem_arma_no_jogo=%d malha_velha_no_anim=%d anim_sem_arma=%d arma_oculta=%d sem_anim=%d",
                     g_wsArmOn, g_wsArmN, g_wsArmOk, g_wsArmNoGate, g_wsArmStale, g_wsArmNoFlag, g_wsArmHid, g_wsArmNoAi);
            logl(b);
        }
        g_wsHRead = g_wsHChg = g_wsHSent = g_wsHBad = 0;
        g_wsRx = g_wsTry = g_wsMade = g_wsMadeFail = g_wsOk = g_wsFail = g_wsSkip = g_wsOff = g_wsGive = 0;
        g_wsArmN = g_wsArmOk = g_wsArmNoGate = g_wsArmStale = g_wsArmNoFlag = g_wsArmHid = g_wsArmNoAi = g_wsArmLines = 0;
    }
    auto ws_arm_raw(const uint8_t* a, const uint8_t* ai, int wd, int os, WsArmRaw* r) -> bool
    {
        __try
        {
            const uint8_t* core = *reinterpret_cast<const uint8_t* const*>(a + 0x650);
            if (reinterpret_cast<uintptr_t>(core) >= 0x10000)
            {
                const uint8_t* eq = *reinterpret_cast<const uint8_t* const*>(core + 0x678);
                if (reinterpret_cast<uintptr_t>(eq) >= 0x10000) r->eqt = eq[0xf2];
            }
            const uint8_t* hi = *reinterpret_cast<const uint8_t* const*>(a + 0x7c8);
            if (reinterpret_cast<uintptr_t>(hi) >= 0x10000)
            {
                r->comp = *reinterpret_cast<void* const*>(hi + 0xd0);
                r->cache = *reinterpret_cast<void* const*>(hi + 0xd8);
            }
            if (ai && wd >= 0)
            {
                r->has = ai[wd];
                r->wmesh = *reinterpret_cast<void* const*>(ai + wd + 0x10);
            }
            if (ai && os >= 0) r->osd = ai[os];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    auto ws_arm(UObject* a, int32_t huid, int hostEq, double now) -> void
    {
        if (!g_wsArmOn || !a || huid == 0 || hostEq < 1 || hostEq > 3) return;
        if (g_gtid == 0 || GetCurrentThreadId() != g_gtid) return;
        auto ci = g_wsCli.find(huid);
        if (ci != g_wsCli.end() && (ci->second.pend || now - ci->second.at < 2.0)) return;
        double& at = g_wsArmAt[huid];
        if (now - at < 3.0) return;
        at = now;
        ws_log(now);
        ++g_wsArmN;
        UObject* mesh = prop_obj(a, STR("Mesh"));
        UObject* ai = mesh ? anim_of(mesh) : nullptr;
        if (!ai) { ++g_wsArmNoAi; return; }
        WsArmRaw r;
        int wd = off_of(ai, STR("WeaponData"));
        int os = off_of(ai, STR("CurrentOwnerState"));
        if (!ws_arm_raw(reinterpret_cast<const uint8_t*>(a), reinterpret_cast<const uint8_t*>(ai), wd, os, &r)) { ++g_wsArmNoAi; return; }
        UObject* gate = c_obj(a, STR("GetWeaponInHandsMeshComponent"));
        bool hid = false, vis = true;
        if (gate) { hid = prop_bool(gate, STR("bHiddenInGame"), false); vis = c_bool(gate, STR("IsVisible"), true); }
        const char* why = nullptr;
        if (!gate) { ++g_wsArmNoGate; why = "sem_arma_no_jogo"; }
        else if (r.wmesh != gate) { ++g_wsArmStale; why = "malha_velha_no_anim"; }
        else if (r.has != 1) { ++g_wsArmNoFlag; why = "anim_sem_arma"; }
        else if (hid || !vis) { ++g_wsArmHid; why = "arma_oculta"; }
        else ++g_wsArmOk;
        if (!why || !C.natlog || g_wsArmLines >= 6) return;
        ++g_wsArmLines;
        std::string cur;
        ws_hand(a, cur);
        UObject* m = mont_cur(ai);
        char b[520];
        snprintf(b, sizeof b, "NOLA wpnarm: copia uid=%d %s | host_eq=%d eq_local=%d mao=%s quer=%s | comp=%p cache=%p gate=%p anim_malha=%p anim_tem_arma=%d oculta=%d visivel=%d dono_estado=%d montagem=%s",
                 huid, why, hostEq, r.eqt, cur.empty() ? "-" : cur.c_str(), ci != g_wsCli.end() && !ci->second.want.empty() ? ci->second.want.c_str() : "-",
                 r.comp, r.cache, (void*)gate, r.wmesh, r.has, (int)hid, (int)vis, r.osd, m ? w2u(m->GetName()).c_str() : "-");
        logl(b);
    }
    auto wpn_host(UObject* o, int32_t uid, double now, std::string& mm) -> void
    {
        if (!g_wsOn || !o || uid == 0) return;
        ws_log(now);
        std::string s;
        int n = ws_hand(o, s);
        if (n < 0) { ++g_wsHBad; return; }
        ++g_wsHRead;
        WsHost& h = g_wsHost[uid];
        h.seen = now;
        if (s != h.sid) { h.sid = s; h.left = 4; ++g_wsHChg; }
        if (h.left <= 0 && now - h.at >= 5) h.left = 1;
        if (h.left <= 0) return;
        --h.left;
        h.at = now;
        if (!mm.empty()) mm += ";";
        mm += "w" + std::to_string(uid) + "," + (s.empty() ? std::string("-") : s);
        ++g_wsHSent;
    }
    auto wpn_rx(const std::string& e, double now) -> void
    {
        size_t c = e.find(',');
        if (c == std::string::npos || c < 2) return;
        int32_t uid = (int32_t)std::strtol(e.c_str() + 1, nullptr, 10);
        if (uid == 0) return;
        std::string s = e.substr(c + 1);
        if (s == "-") s.clear();
        else if (!ws_sid_ok(s)) return;
        WsCli& w = g_wsCli[uid];
        w.want = s;
        w.seen = now;
        ++g_wsRx;
    }
    auto wpn_apply(UObject* a, int32_t luid, int32_t huid, int hostEq, double now) -> int
    {
        ws_arm(a, huid, hostEq, now);
        if (!g_wsOn || !a || huid == 0) return 0;
        ws_log(now);
        auto it = g_wsCli.find(huid);
        if (it == g_wsCli.end() || it->second.want.empty()) return 0;
        WsCli& w = it->second;
        w.seen = now;
        if (w.body != a) { w.body = a; w.tried.clear(); w.fails = 0; w.pend = false; w.made = false; w.gave = false; w.at = -100; }
        if (w.tried != w.want) { w.tried = w.want; w.fails = 0; w.pend = false; w.made = false; w.gave = false; w.at = -100; }
        if (hostEq < 1 || hostEq > 3) { ++g_wsSkip; return 0; }
        if (!ws_ready()) return 0;
        if (g_gtid == 0 || GetCurrentThreadId() != g_gtid) { ++g_wsOff; return 0; }
        std::string cur;
        int n = ws_hand(a, cur);
        if (n < 0) { ++g_wsSkip; return 0; }
        if (cur == w.want)
        {
            if (w.pend) { ++g_wsOk; w.pend = false; }
            w.fails = 0;
            return 0;
        }
        if (w.pend && now - w.at >= kWsCheck) { w.pend = false; ++g_wsFail; ++w.fails; }
        if (w.gave)
        {
            if (now - w.gaveAt < kWsGiveFor) return 0;
            w.gave = false; w.fails = 0;
        }
        if (w.fails >= kWsMaxFail)
        {
            w.gave = true; w.gaveAt = now; ++g_wsGive;
            logl("NOLA wpnsid: copia uid=" + std::to_string(huid) + " desiste por " + std::to_string((int)kWsGiveFor) + " s da arma " + w.want + " (mao=" + (cur.empty() ? std::string("-") : cur) + ", falhas=" + std::to_string(w.fails) + ")");
            return 0;
        }
        if (w.pend || now - w.at < kWsGap) return 0;
        UObject* mesh = prop_obj(a, STR("Mesh"));
        if (!mesh || !prop_obj(mesh, STR("AnimScriptInstance"))) { ++g_wsSkip; return 0; }
        if (!alive_of(a) || c_bool(a, STR("IsChangingWeapon"), false)) { ++g_wsSkip; return 0; }
        uint8_t* core = ws_core_raw(reinterpret_cast<const uint8_t*>(a));
        if (!core) { ++g_wsSkip; return 0; }
        w.at = now;
        ++g_wsTry;
        std::wstring ws = u2w(w.want);
        int r = ws_equip_raw(core, (uint32_t)hostEq, ws.c_str());
        if (r == 0 && !w.made)
        {
            w.made = true;
            if (!ws_create(luid, w.want)) { ++g_wsMadeFail; ++g_wsFail; ++w.fails; return 0; }
            ++g_wsMade;
            r = ws_equip_raw(core, (uint32_t)hostEq, ws.c_str());
        }
        if (r <= 0) { ++g_wsFail; ++w.fails; return 0; }
        w.pend = true;
        return 1;
    }
