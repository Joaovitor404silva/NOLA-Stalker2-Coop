import sys
import os

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rep import edit

SRC = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "src")
NAT = os.path.join(SRC, "s2n_nat.cpp")
B164 = os.path.join(SRC, "s2n_b164.inc")

HOST = [(
'''    auto lua_host(const LuaMadeSimple::Lua& lua) -> int
    {
        auto num = [&](double def) { return lua.is_number() ? lua.get_number() : def; };
        int mode = (int)num(0);
        double meA = num(0), rx = num(0), ry = num(0), rz = num(0), pupA = num(0), tgtA = num(0);
        (void)rz;
        if (mode != 1) { ag_update(nullptr); dmg_update(nullptr, nullptr); if (!H.sel.empty()) { H.sel.clear(); H.order.clear(); } lua.set_string(""); return 1; }
        if (!selftest()) { lua.set_string(""); return 1; }
''',
'''    auto host_api(int mode, double meA, double rx, double ry, double rz, double pupA, double tgtA) -> std::string
    {
        (void)rz;
        if (mode != 1) { ag_update(nullptr); dmg_update(nullptr, nullptr); if (!H.sel.empty()) { H.sel.clear(); H.order.clear(); } return std::string(); }
        if (!selftest()) return std::string();
'''), (
'''            g_wpReread = 0; g_covSent = 0; g_genSent = 0;
        }
        lua.set_string(out);
        return 1;
    }

    struct HRec
''',
'''            g_wpReread = 0; g_covSent = 0; g_genSent = 0;
        }
        return out;
    }
    auto lua_host(const LuaMadeSimple::Lua& lua) -> int
    {
        auto num = [&](double def) { return lua.is_number() ? lua.get_number() : def; };
        int mode = (int)num(0);
        double meA = num(0), rx = num(0), ry = num(0), rz = num(0), pupA = num(0), tgtA = num(0);
        lua.set_string(host_api(mode, meA, rx, ry, rz, pupA, tgtA));
        return 1;
    }

    struct HRec
''')]

CLIENT = [(
'''        double meA = num(0), pupA = num(0), tgtA = num(0);
        int busy = (int)num(0);
        int via = (int)num(0);
        double now = now_s();
''',
'''        double meA = num(0), pupA = num(0), tgtA = num(0);
        int busy = (int)num(0);
        int via = (int)num(0);
        lua.set_bool(client_api(mode, nn, nl, meA, pupA, tgtA, busy, via));
        return 1;
    }
    auto client_api(int mode, const std::string& nn, const std::string& nl, double meA, double pupA, double tgtA, int busy, int via) -> bool
    {
        double now = now_s();
'''), (
'''        if (mode == 1 && C.natpoll && now - g_poll.lastRun < 0.35) { ++g_pollSkip; lua.set_bool(true); return 1; }
        lua.set_bool(client_run(mode, nn, nl, meA, pupA, tgtA, busy, via));
        return 1;
    }
''',
'''        if (mode == 1 && C.natpoll && now - g_poll.lastRun < 0.35) { ++g_pollSkip; return true; }
        return client_run(mode, nn, nl, meA, pupA, tgtA, busy, via);
    }
'''), (
'''    auto lua_client(const LuaMadeSimple::Lua& lua) -> int
    {
''',
'''    auto client_api(int mode, const std::string& nn, const std::string& nl, double meA, double pupA, double tgtA, int busy, int via) -> bool;
    auto lua_client(const LuaMadeSimple::Lua& lua) -> int
    {
''')]

POLL = [(
'''    } g_poll;
''',
'''    } g_poll;
    bool g_feedOn = false, g_feedNew = false;
    std::string g_feedText;
'''), (
'''        if (!g_poll.init) poll_path();
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(g_poll.path.c_str(), GetFileExInfoStandard, &fa)) return;
        if (CompareFileTime(&fa.ftLastWriteTime, &g_poll.ft) == 0) return;
        auto t0 = Clock::now();
        HANDLE h = CreateFileW(g_poll.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) { ++g_pollBad; return; }
        std::string t;
        DWORD sz = GetFileSize(h, nullptr), got = 0;
        if (sz != INVALID_FILE_SIZE && sz > 0 && sz < (16u << 20)) { t.resize(sz); if (!ReadFile(h, t.data(), sz, &got, nullptr)) got = 0; t.resize(got); }
        CloseHandle(h);
''',
'''        WIN32_FILE_ATTRIBUTE_DATA fa{};
        auto t0 = Clock::now();
        std::string t;
        if (g_feedOn)
        {
            if (!g_feedNew) return;
            g_feedNew = false;
            t.swap(g_feedText);
            fa.ftLastWriteTime = g_poll.ft;
        }
        else
        {
            if (!g_poll.init) poll_path();
            if (!GetFileAttributesExW(g_poll.path.c_str(), GetFileExInfoStandard, &fa)) return;
            if (CompareFileTime(&fa.ftLastWriteTime, &g_poll.ft) == 0) return;
            t0 = Clock::now();
            HANDLE h = CreateFileW(g_poll.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h == INVALID_HANDLE_VALUE) { ++g_pollBad; return; }
            DWORD sz = GetFileSize(h, nullptr), got = 0;
            if (sz != INVALID_FILE_SIZE && sz > 0 && sz < (16u << 20)) { t.resize(sz); if (!ReadFile(h, t.data(), sz, &got, nullptr)) got = 0; t.resize(got); }
            CloseHandle(h);
        }
''')]

ND = [(
'''    auto dmg_host_str(int64_t wnow) -> std::string
    {
        if (!g_ddDet) return {};
''',
'''    auto dmg_host_str(int64_t wnow) -> std::string
    {
        if (!g_ddDet) return {};
        bool ndOk = s2x_nd_send_ok();
'''), (
'''        if (!C.natdmg) g_ddQ.clear();
        for (DHit& h : g_ddQ)
''',
'''        if (!C.natdmg || !ndOk) g_ddQ.clear();
        for (DHit& h : g_ddQ)
'''), (
'''            if (!h.logged)
            {
                h.logged = true;
                if (C.natlog && (++g_ddLogN <= 30 || g_ddLogN % 50 == 0))
''',
'''            if (!h.logged)
            {
                h.logged = true;
                s2x_nd_sent(r.f[0]);
                if (C.natlog && (++g_ddLogN <= 30 || g_ddLogN % 50 == 0))
'''), (
'''            if (!C.natdmg || !g_ddBuild) { ++g_dcOff; continue; }
''',
'''            if (!C.natdmg || !g_ddBuild) { ++g_dcOff; continue; }
            if (!s2x_nd_apply_ok()) { ++g_dcOff; continue; }
'''), (
'''            ++g_dcOk; g_dcSum += got; g_dcHp += hp0 - hp1;
''',
'''            ++g_dcOk; g_dcSum += got; g_dcHp += hp0 - hp1;
            s2x_nd_applied(got, hp0, hp1);
'''), (
'''namespace
{
''',
'''extern "C" bool s2x_nd_send_ok();
extern "C" bool s2x_nd_apply_ok();
extern "C" void s2x_nd_sent(double amount);
extern "C" void s2x_nd_applied(double amount, double hp0, double hp1);

namespace
{
''')]

SMALL_OLD_START = "    auto lua_own(const LuaMadeSimple::Lua& lua) -> int\n"
SMALL_OLD_END = "    auto lua_status(const LuaMadeSimple::Lua& lua) -> int\n"
SMALL_NEW = '''    auto own_api(double a) -> bool { return a != 0 && K.own.count(static_cast<uintptr_t>(a)) > 0; }
    auto lua_own(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        lua.set_bool(own_api(a));
        return 1;
    }
    auto dead_api(const std::string& s) -> bool
    {
        Ent* e = shown_by_nm(suffix_of(s), true);
        return e != nullptr && !swapped_src(e->body.raw);
    }
    auto lua_dead(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        lua.set_bool(dead_api(s));
        return 1;
    }
    auto actor_api(const std::string& s, UObject*& w, UObject*& m) -> UObject*
    {
        Ent* e = shown_by_nm(suffix_of(s), false);
        UObject* a = e ? e->body.get() : nullptr;
        w = m = nullptr;
        if (!a) return nullptr;
        w = c_obj(a, STR("GetWeaponInHandsMeshComponent"));
        m = prop_obj(a, STR("Mesh"));
        return a;
    }
    auto lua_actor(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        UObject* w = nullptr;
        UObject* m = nullptr;
        UObject* a = actor_api(s, w, m);
        if (!a) { lua.set_nil(); return 1; }
        LuaType::auto_construct_object(lua, a);
        if (w) LuaType::auto_construct_object(lua, w); else lua.set_nil();
        if (m) LuaType::auto_construct_object(lua, m); else lua.set_nil();
        return 3;
    }
    auto fire_api(const std::string& s) -> bool
    {
        Ent* e = C.natcb ? shown_by_nm(suffix_of(s), false) : nullptr;
        UObject* a = (e && e->st == LIVE) ? e->body.get() : nullptr;
        bool any = false;
        if (a)
        {
            UObject* insts[2] = {anim_of(prop_obj(a, STR("Mesh"))), anim_of(c_obj(a, STR("GetWeaponInHandsMeshComponent")))};
            for (int k = 0; k < 2; ++k)
            {
                UObject* m = e->fire[k].get();
                if (!m || !insts[k]) continue;
                bool pl = mont_playing(insts[k], m);
                if (pl && mont_pos(insts[k], m) < C.natfiremin) continue;
                if (mont_play(insts[k], m, 0.f, true) > 0) { ++K.fire; any = true; }
            }
        }
        return any;
    }
    auto lua_fire(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        lua.set_bool(fire_api(s));
        return 1;
    }
    auto listed_uid(int32_t uid) -> bool
    {
        if (!uid) return false;
        double now = now_s();
        auto it = K.E.find(uid);
        if (it != K.E.end() && it->second.st != KEPT && (now - it->second.h.recv <= C.natlost || it->second.st == LIVE || it->second.st == DEAD)) return true;
        for (auto& [k, s] : H.sel) if (s.uid == uid) return true;
        return false;
    }
    auto lua_listed(const LuaMadeSimple::Lua& lua) -> int
    {
        double u = lua.is_number() ? lua.get_number() : 0;
        lua.set_bool(listed_uid((int32_t)u));
        return 1;
    }
    auto listed_addr_api(double a) -> bool
    {
        auto p = static_cast<uintptr_t>(a);
        bool r = false;
        if (p)
        {
            if (K.own.count(p)) r = true;
            else
            {
                auto it = K.idx.find(reinterpret_cast<UObject*>(p));
                if (it != K.idx.end()) r = listed_uid(it->second.uid);
                if (!r) for (auto& [k, s] : H.sel) if (reinterpret_cast<uintptr_t>(k) == p && s.r.get()) { r = true; break; }
            }
        }
        return r;
    }
    auto lua_listed_addr(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        lua.set_bool(listed_addr_api(a));
        return 1;
    }
    auto corpse_api(const std::string& s, double& x, double& y, double& z) -> bool
    {
        Ent* e = shown_by_nm(suffix_of(s), true);
        UObject* a = e ? e->body.get() : nullptr;
        if (!a || !loc_of(a, x, y, z)) return false;
        if (UObject* m = prop_obj(a, STR("Mesh")))
        {
            Call c(m, STR("GetSocketLocation"));
            c.nm(STR("InSocketName"), STR("jnt_hips"));
            double px, py, pz;
            if (c.run() && c.rv(px, py, pz) && !(px == 0 && py == 0) && std::hypot(px - x, py - y) < 400) { x = px; y = py; }
        }
        return true;
    }
    auto lua_corpse(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        double x, y, z;
        if (!corpse_api(s, x, y, z)) { lua.set_nil(); return 1; }
        lua.set_number(x); lua.set_number(y); lua.set_number(z);
        return 3;
    }
    auto filt_drop() -> std::unordered_set<std::string>
    {
        std::unordered_set<std::string> drop;
        for (auto& [k, e] : K.E) if (e.shown && (e.st == LIVE || e.st == DEAD) && e.h.nm != "0") drop.insert(e.h.nm);
        return drop;
    }
    auto filt_api(const std::string& t, int& n) -> std::string
    {
        std::unordered_set<std::string> drop = filt_drop();
        n = 0;
        size_t pos = t.find("\\"npcs\\"");
        if (drop.empty() || pos == std::string::npos) return t;
        std::string out = t.substr(0, pos);
        size_t i = pos;
        while (i < t.size())
        {
            size_t o = t.find("{\\"id\\":", i);
            if (o == std::string::npos) { out += t.substr(i); break; }
            size_t c = t.find('}', o);
            size_t inner = t.find('{', o + 1);
            if (c == std::string::npos || (inner != std::string::npos && inner < c)) { out += t.substr(i); break; }
            size_t q1 = t.find('"', o + 6);
            size_t q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
            bool dropIt = false;
            if (q2 != std::string::npos && q2 < c) dropIt = drop.count(suffix_of(t.substr(q1 + 1, q2 - q1 - 1))) > 0;
            out += t.substr(i, o - i);
            if (dropIt) ++n; else out += t.substr(o, c - o + 1);
            i = c + 1;
        }
        return out;
    }
    auto lua_filt(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string t = lua.is_string() ? std::string(lua.get_string()) : std::string();
        int n = 0;
        std::string out = filt_api(t, n);
        lua.set_string(out);
        lua.set_integer(n);
        return 2;
    }
'''

INSTALL = [(
'''void s2n_nat_register(RC::LuaMadeSimple::Lua& l)
{
    static bool b176 = false;
    if (!b176)
    {
        b176 = true;
        fo_install();
        mont_install();
        wpn_install();
        Unreal::Hook::RegisterEngineTickPreCallback([](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UEngine*, float, bool) { tick_cb(); },
                                                    {false, false, STR("S2CoopNative"), STR("b176tick")});
    }
''',
'''void s2n_nat_install()
{
    static bool b176 = false;
    if (!b176)
    {
        b176 = true;
        fo_install();
        mont_install();
        wpn_install();
        Unreal::Hook::RegisterEngineTickPreCallback([](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UEngine*, float, bool) { tick_cb(); },
                                                    {false, false, STR("S2CoopNative"), STR("b176tick")});
    }
}

void s2n_nat_register(RC::LuaMadeSimple::Lua& l)
{
    s2n_nat_install();
''')]

TAIL = '''
namespace s2nat
{
    std::string host(int mode, uint64_t me, double rx, double ry, double rz, uint64_t pup, uint64_t tgt)
    {
        return host_api(mode, (double)me, rx, ry, rz, (double)pup, (double)tgt);
    }
    bool client(int mode, const std::string& nn, const std::string& nl, uint64_t me, uint64_t pup, uint64_t tgt, int busy, int via)
    {
        return client_api(mode, nn, nl, (double)me, (double)pup, (double)tgt, busy, via);
    }
    bool own(uint64_t a) { return own_api((double)a); }
    bool dead(const std::string& hid) { return dead_api(hid); }
    uint64_t actor(const std::string& nm, uint64_t& weapon, uint64_t& mesh)
    {
        UObject* w = nullptr;
        UObject* m = nullptr;
        UObject* a = actor_api(nm, w, m);
        weapon = reinterpret_cast<uint64_t>(w);
        mesh = reinterpret_cast<uint64_t>(m);
        return reinterpret_cast<uint64_t>(a);
    }
    bool fire(const std::string& nm) { return fire_api(nm); }
    bool listed(int32_t uid) { return listed_uid(uid); }
    bool listed_addr(uint64_t a) { return listed_addr_api((double)a); }
    bool corpse(const std::string& hid, double& x, double& y, double& z) { return corpse_api(hid, x, y, z); }
    std::string filt(const std::string& text, int& dropped) { return filt_api(text, dropped); }
    bool drop_nm(const std::string& suffix) { return !suffix.empty() && suffix != "0" && filt_drop().count(suffix) > 0; }
    int natbr() { return C.natbr; }
    void natcb(int on) { C.natcb = on; }
    std::string lk_hold(const std::string& slot, const std::string& key, double age, double lease, double hmax) { return lk_hold_api(slot, key, age, lease, hmax); }
    int lk_off(uint64_t a, int on, double lease) { return lk_off_api((double)a, on, lease); }
    bool cl_swap(const std::string& hid, uint64_t loot, uint64_t src, int& mism) { return cl_swap_api(hid, (double)loot, (double)src, mism); }
    int cl_swap_tick() { return cl_swap_tick_api(); }
    bool qi_npcadd(const std::string& sid) { return qi_npcadd_api(sid); }
    bool qr_npc(const std::string& sid, std::string& guid) { return qr_npc_api(sid, guid); }
    std::string b164_status() { return b164_status_api(); }
    void feed_mode(bool on) { if (g_feedOn != on) { g_feedOn = on; g_feedNew = false; g_feedText.clear(); } }
    void feed(const std::string& text) { if (!g_feedOn) return; g_feedText = text; g_feedNew = true; }
}
'''

B164_EDITS = [(
'''    auto lua_lk_hold(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string slot = lua.is_string() ? std::string(lua.get_string()) : std::string();
        std::string key = lua.is_string() ? std::string(lua.get_string()) : std::string();
        double age = lua.is_number() ? lua.get_number() : 0;
        double lease = lua.is_number() ? lua.get_number() : 3;
        double hmax = lua.is_number() ? lua.get_number() : 120;
        double now = now_s();
''',
'''    auto lk_hold_api(const std::string& slot, const std::string& key, double age, double lease, double hmax) -> std::string
    {
        double now = now_s();
'''), (
'''            if (!s.capLog) { s.capLog = true; logl("S2CORE b164 lease: " + slot + " " + key + " solto (aberto ha mais de " + std::to_string((int)hmax) + " s)"); }
        }
        lua.set_string(r);
        return 1;
    }
''',
'''            if (!s.capLog) { s.capLog = true; logl("S2CORE b164 lease: " + slot + " " + key + " solto (aberto ha mais de " + std::to_string((int)hmax) + " s)"); }
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
'''), (
'''    auto lua_lk_off(const LuaMadeSimple::Lua& lua) -> int
    {
        double a = lua.is_number() ? lua.get_number() : 0;
        int on = lua.is_number() ? (int)lua.get_number() : 0;
        double lease = lua.is_number() ? lua.get_number() : 3;
        double now = now_s();
''',
'''    auto lk_off_api(double a, int on, double lease) -> int
    {
        double now = now_s();
'''), (
'''            else it->second.at = now;
            lua.set_number(0);
            return 1;
        }
''',
'''            else it->second.at = now;
            return 0;
        }
'''), (
'''            it = g_off.erase(it);
        }
        lua.set_number(n);
        return 1;
    }
''',
'''            it = g_off.erase(it);
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
'''), (
'''    auto lua_cl_swap(const LuaMadeSimple::Lua& lua) -> int
    {
        std::string hid = lua.is_string() ? std::string(lua.get_string()) : std::string();
        double la = lua.is_number() ? lua.get_number() : 0;
        double sa = lua.is_number() ? lua.get_number() : 0;
        UObject* loot = obj_at(la);
''',
'''    auto cl_swap_api(const std::string& hid, double la, double sa, int& mism) -> bool
    {
        UObject* loot = obj_at(la);
'''), (
'''        if (!loot || !src || loot == src) { lua.set_nil(); return 1; }
''',
'''        if (!loot || !src || loot == src) return false;
'''), (
'''        int mism = -1;
        std::string why;
''',
'''        mism = -1;
        std::string why;
'''), (
'''        logl(b);
        lua.set_number(mism);
        return 1;
    }
    auto lua_cl_swap_tick(const LuaMadeSimple::Lua& lua) -> int
    {
''',
'''        logl(b);
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
'''), (
'''            if (!prop_bool(s, STR("bHidden"), true)) { set_hidden(s, true); interaction_off(s); }
            ++it;
        }
        lua.set_number((double)g_sw.size());
        return 1;
    }

    auto lua_qi_npcadd(const LuaMadeSimple::Lua& lua) -> int
    {
        static std::unordered_set<std::string> S;
        if (S.empty()) for (const char* const* p = kQiNpcAdd; *p; ++p) S.insert(*p);
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        lua.set_bool(!s.empty() && S.count(s) > 0);
        return 1;
    }
    auto lua_qr_npc(const LuaMadeSimple::Lua& lua) -> int
    {
        static std::unordered_map<std::string, std::string> M;
        if (M.empty()) for (const QrRow* r = kQrNpc; r->sid; ++r) M[r->sid] = r->guid;
        std::string s = lua.is_string() ? std::string(lua.get_string()) : std::string();
        auto it = M.find(s);
        if (it == M.end()) lua.set_nil(); else lua.set_string(it->second);
        return 1;
    }
    auto lua_b164_status(const LuaMadeSimple::Lua& lua) -> int
    {
        char b[200];
''',
'''            if (!prop_bool(s, STR("bHidden"), true)) { set_hidden(s, true); interaction_off(s); }
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
'''), (
'''                 (int)(sizeof(kQiNpcAdd) / sizeof(kQiNpcAdd[0])) - 1, (int)(sizeof(kQrNpc) / sizeof(kQrNpc[0])) - 1);
        lua.set_string(b);
        return 1;
    }
''',
'''                 (int)(sizeof(kQiNpcAdd) / sizeof(kQiNpcAdd[0])) - 1, (int)(sizeof(kQrNpc) / sizeof(kQrNpc[0])) - 1);
        return b;
    }
    auto lua_b164_status(const LuaMadeSimple::Lua& lua) -> int
    {
        lua.set_string(b164_status_api());
        return 1;
    }
''')]


def main():
    s = open(NAT, encoding="utf-8", newline="").read()
    if "namespace s2nat" in s:
        raise SystemExit("already applied")
    crlf = "\r\n" in s
    nl = "\r\n" if crlf else "\n"
    a = s.index(SMALL_OLD_START.replace("\n", nl))
    b = s.index(SMALL_OLD_END.replace("\n", nl))
    s = s[:a] + SMALL_NEW.replace("\n", nl) + s[b:]
    open(NAT, "w", encoding="utf-8", newline="").write(s)
    edit(NAT, HOST + CLIENT + INSTALL + POLL + ND)
    edit(B164, B164_EDITS)
    s = open(NAT, encoding="utf-8", newline="").read()
    marker = (nl + "}" + nl + nl + "void s2n_nat_install()")
    assert s.count(marker) == 1
    s = s.replace(marker, nl + "}" + nl + TAIL.replace("\n", nl) + nl + "void s2n_nat_install()")
    open(NAT, "w", encoding="utf-8", newline="").write(s)
    print("ok")


if __name__ == "__main__":
    main()
