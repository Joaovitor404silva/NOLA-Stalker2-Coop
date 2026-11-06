#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "s2m_lock.h"
#include "s2m_pop.h"

namespace
{
    long long g_ms = 0;
    bool g_host = true;
    std::map<std::string, x0::Val> g_flags;
    std::string g_peer;
    bool g_peer_set = false;
    std::map<std::wstring, std::string> g_fs;
    std::wstring g_data;
    std::string g_last_out;
    int g_maxc = 1;
    int g_nviews = 1;
    int g_my = 1;
    bool g_mcnet = false;
    std::map<int, std::string> g_vlk;
    std::string g_lo;
    bool g_lo_set = false;

    void emit(const std::string& s) { std::cout << s << "\n"; }
}

namespace x0
{
    Val Val::I(int64_t v)
    {
        Val r;
        r.k = i;
        r.iv = v;
        return r;
    }
    Val Val::F(double v)
    {
        Val r;
        r.k = f;
        r.fv = v;
        return r;
    }
    Val Val::S(std::string v)
    {
        Val r;
        r.k = s;
        r.sv = std::move(v);
        return r;
    }
    double Val::num() const
    {
        if (k == i) return (double)iv;
        if (k == f) return fv;
        if (k == s) return std::atof(sv.c_str());
        return 0;
    }
    int64_t Val::integer() const { return k == i ? iv : (int64_t)num(); }
    std::string Val::str() const
    {
        char b[64];
        if (k == i) return std::to_string(iv);
        if (k == f)
        {
            std::snprintf(b, sizeof b, "%.14g", fv);
            std::string t = b;
            if (t.find_first_of(".eEn") == std::string::npos) t += ".0";
            return t;
        }
        if (k == s) return sv;
        return "nil";
    }
    bool Val::operator==(const Val& o) const { return str() == o.str() && k == o.k; }

    Val flag(std::string_view key)
    {
        auto it = g_flags.find(std::string(key));
        return it == g_flags.end() ? Val{} : it->second;
    }
    int64_t fi(std::string_view key, int64_t def)
    {
        Val v = flag(key);
        return v.k == Val::nil ? def : v.integer();
    }
    double fd(std::string_view key, double def)
    {
        Val v = flag(key);
        return v.k == Val::nil ? def : v.num();
    }
    double now() { return g_ms / 1000.0; }
    int64_t wall_ms() { return 1700000000000LL + g_ms; }
    bool is_host() { return g_host; }
    bool is_client() { return !g_host; }
    const std::string& role()
    {
        static std::string h = "host", c = "client";
        return g_host ? h : c;
    }
    bool port(Mod) { return true; }
    void log(std::string_view t)
    {
        char b[32];
        std::snprintf(b, sizeof b, "L %lld ", g_ms);
        emit(std::string(b) + std::string(t));
    }
    bool rpart(std::string_view tag, std::string& out)
    {
        if (tag != "LK" || !g_peer_set) return false;
        out = g_peer;
        return true;
    }
    bool rpart(std::string_view tag, std::string& out, int view)
    {
        if (view < 0 || view >= g_nviews) return false;
        if (view == 0)
        {
            if (tag == "LO")
            {
                if (!g_lo_set) return false;
                out = g_lo;
                return true;
            }
            return rpart(tag, out);
        }
        auto it = g_vlk.find(view);
        if (tag != "LK" || it == g_vlk.end()) return false;
        out = it->second;
        return true;
    }
    int views() { return g_nviews; }
    int max_clients() { return g_maxc; }
    bool hub() { return g_mcnet && g_host; }
    const Roster& roster()
    {
        static Roster r;
        r.on = g_mcnet;
        r.max = g_maxc;
        r.you = g_host ? 0 : g_my;
        return r;
    }
    int view_pid(int view)
    {
        if (view < 0 || view >= g_nviews) return -1;
        return g_host ? view + 1 : (view == 0 ? 0 : -1);
    }
    int my_pid() { return g_host ? 0 : g_my; }
    std::vector<std::string> split(std::string_view s, char sep, bool keep_empty)
    {
        std::vector<std::string> r;
        std::string cur;
        for (char c : s)
        {
            if (c == sep)
            {
                if (keep_empty || !cur.empty()) r.push_back(cur);
                cur.clear();
            }
            else cur += c;
        }
        if (keep_empty || !cur.empty()) r.push_back(cur);
        return r;
    }
    std::wstring data_file(std::wstring_view name) { return g_data + std::wstring(name); }
    std::wstring bridge_file(std::wstring_view name) { return L"B:\\" + std::wstring(name); }
    bool read_text(const std::wstring& path, std::string& out)
    {
        auto it = g_fs.find(path);
        if (it == g_fs.end()) return false;
        out = it->second;
        return true;
    }
    bool write_text(const std::wstring& path, std::string_view text, bool)
    {
        g_fs[path] = std::string(text);
        return true;
    }
    bool tsv_d1(const std::wstring& path, std::vector<TsvRow>& rows, std::string* err);
    bool data_tsv(std::string_view name, std::vector<TsvRow>& rows, std::string* err)
    {
        std::wstring w(name.begin(), name.end());
        return tsv_d1(g_data + w, rows, err);
    }
    bool tsv_d1(const std::wstring& path, std::vector<TsvRow>& rows, std::string* err)
    {
        std::ifstream f(std::string(path.begin(), path.end()), std::ios::binary);
        if (!f)
        {
            if (err) *err = "open failed";
            return false;
        }
        std::string line;
        bool first = true;
        while (std::getline(f, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (first)
            {
                first = false;
                continue;
            }
            if (line.empty()) continue;
            std::vector<std::string> c = split(line, '\t', true);
            if (c.size() < 3) continue;
            size_t nk = c.size() - 2;
            TsvRow r;
            for (size_t i = 0; i < nk; ++i)
            {
                if (c[i].empty()) break;
                r.ktype += c[i][0];
                r.key.push_back(c[i].substr(1));
            }
            r.t = c[nk].empty() ? 's' : c[nk][0];
            std::string v = c[nk + 1], u;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (v[i] == '\\' && i + 1 < v.size())
                {
                    char n = v[++i];
                    u += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
                }
                else u += v[i];
            }
            r.v = u;
            rows.push_back(std::move(r));
        }
        return true;
    }
}

namespace
{
    using s2m_lock::detail::CompSnap;
    using s2m_lock::detail::NpcSnap;
    using s2m_lock::detail::UseComp;
    using s2m_lock::detail::UseSnap;
    using x0::Ref;
    using x0::Vec3;

    Ref mk(int idx, uint64_t addr)
    {
        Ref r;
        r.idx = idx;
        r.serial = 1;
        r.raw = reinterpret_cast<void*>(static_cast<uintptr_t>(addr));
        return r;
    }

    struct FNpc
    {
        std::string id;
        uint64_t addr = 0;
        double x = 0, y = 0, z = 0;
        bool alive = true, active = true, compok = true, own = false, local = false, vendor = false;
        long long guid = 0;
        int idx = 0;
    };

    struct FUse
    {
        std::string id, hex;
        double x = 0, y = 0, z = 0;
        std::vector<std::pair<std::string, bool>> comps;
        int idx = 0;
    };

    struct FakeWorld : s2m_lock::detail::World
    {
        uint64_t paddr = 1000;
        Vec3 ppos;
        bool pawn_ok = true;
        bool view_on = false;
        bool has_q = false;
        Vec3 q;
        int hint = 0;
        int vis = 0;
        std::map<std::string, FNpc> npcs;
        std::map<std::string, FUse> uses;
        std::map<std::string, std::string> ph;
        int beds_total = 0, beds_off = 0;
        int next_idx = 10;
        int fail_restore = 0;
        std::string vendor_identity;
        bool scene = false, trade_ok = true;

        Ref view_ref() { return mk(1, 0x1000); }

        bool local_pawn(uint64_t& a, Vec3& p) override
        {
            if (!pawn_ok) return false;
            a = paddr;
            p = ppos;
            return true;
        }
        Ref dialog_view() override { return view_on ? view_ref() : Ref{}; }
        Ref vend_view() override { return view_on ? view_ref() : Ref{}; }
        bool interacted_actor(Vec3* pos) override
        {
            if (!has_q) return false;
            if (pos) *pos = q;
            return true;
        }
        bool scene_take() override { return scene; }
        bool interacted_vendor(s2m_pop::VendHit& out) override
        {
            if (!has_q) return false;
            if (!vendor_identity.empty()) { out = {vendor_identity, "identified_actor", 2000}; return true; }
            return s2m_pop::vendorNear(q.x, q.y, q.z, 300, out);
        }
        int answers_vis(const Ref&) override { return vis; }
        bool set_answers_vis(const Ref&, int v) override
        {
            emit("A vis " + std::to_string(v));
            vis = v;
            return true;
        }
        bool close_dialog(const Ref&) override
        {
            emit("A close");
            return true;
        }
        bool open_trade(const Ref&, bool up) override
        {
            emit(std::string("A open ") + (up ? "Upgrade" : "Trade"));
            return trade_ok;
        }
        int trade_hint_vis(const Ref&) override { return hint; }
        std::vector<NpcSnap> humans() override
        {
            std::vector<NpcSnap> out;
            for (auto& kv : npcs)
            {
                FNpc& n = kv.second;
                NpcSnap s;
                s.ref = mk(n.idx, n.addr);
                s.comp = mk(n.idx + 5000, n.addr + 1);
                s.addr = n.addr;
                s.alive = n.alive;
                s.is_local = n.local;
                s.own = n.own;
                s.comp_ok = n.compok;
                s.active = n.active;
                s.vendor = n.vendor;
                s.pos = Vec3{n.x, n.y, n.z};
                out.push_back(s);
            }
            return out;
        }
        FNpc* by_comp(const Ref& c)
        {
            for (auto& kv : npcs)
                if (kv.second.addr + 1 == c.address()) return &kv.second;
            return nullptr;
        }
        FNpc* by_ref(const Ref& r)
        {
            for (auto& kv : npcs)
                if (kv.second.addr == r.address()) return &kv.second;
            return nullptr;
        }
        bool set_active(const Ref& comp, bool on) override
        {
            if (FNpc* n = by_comp(comp))
            {
                if (on && fail_restore > 0)
                {
                    --fail_restore;
                    emit("A npc " + n->id + " restore failed");
                    return false;
                }
                emit("A npc " + n->id + " active " + (on ? "1" : "0"));
                n->active = on;
                return true;
            }
            for (auto& kv : uses)
                for (auto& c : kv.second.comps)
                    if (comp.address() == 0x900000 + (uint64_t)kv.second.idx * 100 + (&c - &kv.second.comps[0]))
                    {
                        emit("A use " + kv.second.id + " " + c.first + " active " + (on ? "1" : "0"));
                        c.second = on;
                        return true;
                    }
            return false;
        }
        std::string npc_guid(const Ref& r) override
        {
            FNpc* n = by_ref(r);
            if (!n || n->guid == 0) return {};
            return std::to_string(n->guid);
        }
        bool npc_pos(const Ref& r, Vec3& p) override
        {
            FNpc* n = by_ref(r);
            if (!n) return false;
            p = Vec3{n->x, n->y, n->z};
            return true;
        }
        std::string npc_name(const Ref& r) override
        {
            FNpc* n = by_ref(r);
            return n ? "Npc_" + n->id : std::string();
        }
        Ref placeholder_actor(const std::string& hex) override
        {
            auto it = ph.find(hex);
            if (it == ph.end()) return {};
            auto n = npcs.find(it->second);
            if (n == npcs.end()) return {};
            return mk(n->second.idx, n->second.addr);
        }
        std::vector<UseSnap> usables(const Vec3& c, double r2, const std::function<bool(const std::string&)>& want) override
        {
            std::vector<UseSnap> out;
            for (auto& kv : uses)
            {
                FUse& u = kv.second;
                double dx = u.x - c.x, dy = u.y - c.y;
                if (dx * dx + dy * dy > r2) continue;
                if (!want(u.hex)) continue;
                UseSnap s;
                s.obj = mk(u.idx, 0x800000 + u.idx);
                s.hex = u.hex;
                char b[96];
                std::snprintf(b, sizeof b, "%.0f,%.0f,%.0f", u.x / 10, u.y / 10, u.z / 10);
                s.key = b;
                std::vector<std::pair<std::string, bool>> cs = u.comps;
                for (size_t i = 0; i < u.comps.size(); ++i)
                {
                    CompSnap k;
                    k.name = u.comps[i].first;
                    k.ref = mk(7000 + (int)i, 0x900000 + (uint64_t)u.idx * 100 + i);
                    k.active = u.comps[i].second;
                    s.comps.push_back(k);
                }
                std::sort(s.comps.begin(), s.comps.end(), [](const CompSnap& a, const CompSnap& b2) { return a.name < b2.name; });
                out.push_back(s);
            }
            return out;
        }
        bool use_hex(const Ref& o, std::string& hex) override
        {
            for (auto& kv : uses)
                if (0x800000 + (uint64_t)kv.second.idx == o.address())
                {
                    hex = kv.second.hex;
                    return true;
                }
            return false;
        }
        std::vector<UseComp> inactive_use_comps() override
        {
            std::vector<UseComp> out;
            for (auto& kv : uses)
                for (size_t i = 0; i < kv.second.comps.size(); ++i)
                    if (!kv.second.comps[i].second) out.push_back(UseComp{mk(7000 + (int)i, 0x900000 + (uint64_t)kv.second.idx * 100 + i), kv.second.hex});
            return out;
        }
        bool beds(int& t, int& o) override
        {
            t = beds_total;
            o = beds_off;
            return true;
        }
    };

    std::vector<std::string> words(const std::string& l)
    {
        std::vector<std::string> w;
        std::istringstream is(l);
        std::string t;
        while (is >> t) w.push_back(t);
        return w;
    }

    x0::Val parse_val(const std::string& s)
    {
        if (s == "nil") return x0::Val{};
        char* e = nullptr;
        long long iv = std::strtoll(s.c_str(), &e, 10);
        if (e && *e == '\0') return x0::Val::I(iv);
        double d = std::strtod(s.c_str(), &e);
        if (e && *e == '\0') return x0::Val::F(d);
        return x0::Val::S(s);
    }

    bool g_beds_blocked = false;
}

namespace
{
    struct PopEnv : s2m_pop::Env
    {
        double now() override { return x0::now(); }
        double flag(const char* name, double def) override { return x0::fd(name, def); }
        bool isHost() override { return x0::is_host(); }
        void log(const std::string& line) override { x0::log(line); }
        bool readFile(const char* name, std::string& out) override
        {
            std::ifstream f(std::string(g_data.begin(), g_data.end()) + name, std::ios::binary);
            if (!f) return false;
            std::stringstream ss;
            ss << f.rdbuf();
            out = ss.str();
            return true;
        }
        bool localPawn(s2m_pop::Id&, s2m_pop::V3&) override { return false; }
        void pawns(s2m_pop::PawnClass, std::vector<s2m_pop::Id>&) override {}
        bool valid(s2m_pop::Id) override { return false; }
        bool alive(s2m_pop::Id) override { return false; }
        bool loc(s2m_pop::Id, s2m_pop::V3&) override { return false; }
        bool hiddenFlag(s2m_pop::Id) override { return false; }
        bool maxHp(s2m_pop::Id, double&) override { return false; }
        std::string className(s2m_pop::Id) override { return {}; }
        std::string shortName(s2m_pop::Id) override { return {}; }
        std::string meshName(s2m_pop::Id, const char*) override { return {}; }
        bool ownBodyBase(s2m_pop::Id) override { return false; }
        bool natOwn(s2m_pop::Id) override { return false; }
        bool natListed(s2m_pop::Id) override { return false; }
        bool natListedUid(long long) override { return false; }
        bool storyNpc(s2m_pop::Id, std::string&) override { return false; }
        bool setHidden(s2m_pop::Id, bool) override { return false; }
        bool setCollision(s2m_pop::Id, bool) override { return false; }
        bool setInteractionOff(s2m_pop::Id) override { return false; }
        bool destroyActor(s2m_pop::Id) override { return false; }
        bool aiFeatures(bool, long long) override { return false; }
        bool aLife(bool) override { return false; }
        bool inThreat(s2m_pop::Id) override { return false; }
        bool focusIsLocal(s2m_pop::Id) override { return false; }
        bool guid(s2m_pop::Id, long long&) override { return false; }
        bool protoId(s2m_pop::Id, long long&) override { return false; }
        bool killByUid(long long) override { return false; }
        bool destroyModel(long long) override { return false; }
        bool despawnDeadAtRadius(double) override { return false; }
        s2m_pop::Body target() override { return {}; }
        s2m_pop::Body puppet() override { return {}; }
        bool worldKey(std::string&) override { return false; }
        bool worldOpen() override { return false; }
        bool clPending() override { return false; }
        bool inventoryOpen() override { return false; }
        void containers(std::vector<s2m_pop::Cont>&) override {}
        bool trace(double, double, double, double, double, double, bool&, double&) override { return false; }
        bool simPhysicsOff(s2m_pop::Id) override { return false; }
        bool setLoc(s2m_pop::Id, double, double, double) override { return false; }
    };
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::cerr << "usage: test_lock <data dir> <scenario>\n";
        return 2;
    }
    std::string dd = argv[1];
    if (!dd.empty() && dd.back() != '\\' && dd.back() != '/') dd += "/";
    g_data = std::wstring(dd.begin(), dd.end());
    PopEnv popEnv;
    s2m_pop::init(&popEnv);
    FakeWorld world;
    s2m_lock::detail::Engine eng(world);
    s2m_lock::detail::bind(&eng);
    s2m_lock::set_beds_blocked_probe([] { return g_beds_blocked; });
    std::ifstream sc(argv[2]);
    std::string line;
    s2m_lock::MqEpisode ep;
    auto step100 = [&](int n) {
        eng.vend_trade_tick();
        eng.ld_tick();
        if (n % 10 == 0)
        {
            eng.ch_use_tick();
            eng.ld_blk_tick();
            eng.lg_tick();
        }
        std::string o = eng.ld_out();
        if (o != g_last_out)
        {
            g_last_out = o;
            emit("O " + std::to_string(g_ms) + " " + (o.empty() ? "-" : o));
        }
    };
    int tickn = 0;
    while (std::getline(sc, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        auto w = words(line);
        const std::string& c = w[0];
        emit("> " + line);
        if (c == "role") g_host = w[1] == "host";
        else if (c == "vendoridentity") world.vendor_identity = w[1] == "none" ? "" : w[1];
        else if (c == "scene") world.scene = w[1] == "on";
        else if (c == "tradefail") world.trade_ok = w[1] != "on";
        else if (c == "failrestore") world.fail_restore = std::atoi(w[1].c_str());
        else if (c == "flag") g_flags[w[1]] = parse_val(w[2]);
        else if (c == "time") g_ms = std::atoll(w[1].c_str());
        else if (c == "run")
        {
            long long end = g_ms + std::atoll(w[1].c_str());
            while (g_ms < end)
            {
                g_ms += 100;
                ++tickn;
                step100(tickn);
            }
        }
        else if (c == "peer")
        {
            if (w[1] == "-") g_peer_set = false;
            else
            {
                g_peer_set = true;
                g_peer = w[1];
            }
        }
        else if (c == "pawn")
        {
            world.paddr = std::strtoull(w[1].c_str(), nullptr, 10);
            world.ppos = Vec3{std::atof(w[2].c_str()), std::atof(w[3].c_str()), std::atof(w[4].c_str())};
            world.pawn_ok = true;
        }
        else if (c == "nopawn") world.pawn_ok = false;
        else if (c == "view") world.view_on = w[1] == "on";
        else if (c == "interact")
        {
            if (w[1] == "none") world.has_q = false;
            else
            {
                world.has_q = true;
                world.q = Vec3{std::atof(w[1].c_str()), std::atof(w[2].c_str()), std::atof(w[3].c_str())};
            }
        }
        else if (c == "hint") world.hint = std::atoi(w[1].c_str());
        else if (c == "vis") world.vis = std::atoi(w[1].c_str());
        else if (c == "npc")
        {
            FNpc n;
            n.id = w[1];
            n.addr = std::strtoull(w[2].c_str(), nullptr, 10);
            n.x = std::atof(w[3].c_str());
            n.y = std::atof(w[4].c_str());
            n.z = std::atof(w[5].c_str());
            n.alive = w[6] == "1";
            n.active = w[7] == "1";
            n.compok = w[8] == "1";
            n.own = w[9] == "1";
            n.local = w[10] == "1";
            n.idx = world.next_idx++;
            world.npcs[n.id] = n;
        }
        else if (c == "npcset")
        {
            FNpc& n = world.npcs[w[1]];
            bool v = w[3] == "1";
            if (w[2] == "alive") n.alive = v;
            else if (w[2] == "active") n.active = v;
            else if (w[2] == "vendor") n.vendor = v;
        }
        else if (c == "guid") world.npcs[w[1]].guid = std::atoll(w[2].c_str());
        else if (c == "ph") world.ph[w[1]] = w[2];
        else if (c == "usable")
        {
            FUse u;
            u.id = w[1];
            u.hex = w[2];
            u.x = std::atof(w[3].c_str());
            u.y = std::atof(w[4].c_str());
            u.z = std::atof(w[5].c_str());
            for (auto& p : x0::split(w[6], ',', false))
            {
                auto kv = x0::split(p, ':', true);
                u.comps.push_back({kv[0], kv[1] == "1"});
            }
            u.idx = world.next_idx++;
            world.uses[u.id] = u;
        }
        else if (c == "beds")
        {
            world.beds_total = std::atoi(w[1].c_str());
            world.beds_off = std::atoi(w[2].c_str());
        }
        else if (c == "bedprobe") g_beds_blocked = w[1] == "1";
        else if (c == "ledger")
        {
            std::string cur;
            x0::read_text(x0::bridge_file(L"s2_blk_ledger.txt"), cur);
            cur += w[1] + "\t" + w[2] + "\t" + w[3] + "\n";
            x0::write_text(x0::bridge_file(L"s2_blk_ledger.txt"), cur, true);
        }
        else if (c == "shot")
        {
            auto n = world.npcs[w[1]];
            emit(std::string("R shot ") + (eng.shot_deny(mk(n.idx, n.addr)) ? "1" : "0"));
        }
        else if (c == "trig") emit(std::string("R trig ") + (eng.trig_deny(w[1], w[2]) ? "1" : "0"));
        else if (c == "trigenter") eng.trig_entered(w[1], std::atof(w[2].c_str()), std::atof(w[3].c_str()), std::atof(w[4].c_str()));
        else if (c == "usedeny")
        {
            FUse& u = world.uses[w[1]];
            emit(std::string("R use ") + (eng.use_deny(mk(u.idx, 0x800000 + u.idx), w[2]) ? "1" : "0"));
        }
        else if (c == "ismain") emit(std::string("R ismain ") + (s2m_lock::is_main(w.size() > 1 ? w[1] : std::string()) ? "1" : "0"));
        else if (c == "mqseen") s2m_lock::mq_seen(w[1]);
        else if (c == "mqflag") emit("R mqflag " + std::to_string(s2m_lock::mq_flag(w[1] == "1", w[2] == "1", w[3] == "1", ep)));
        else if (c == "mqok") emit(std::string("R mqok ") + (s2m_lock::mq_ok(w[1] == "1") ? "1" : "0"));
        else if (c == "mqsid") emit(std::string("R mqsid ") + (s2m_lock::mq_sid(w[1]) ? "1" : "0"));
        else if (c == "mc")
        {
            g_maxc = std::atoi(w[1].c_str());
            g_nviews = std::atoi(w[2].c_str());
            g_my = std::atoi(w[3].c_str());
            g_mcnet = true;
        }
        else if (c == "vpeer")
        {
            int v = std::atoi(w[1].c_str());
            if (w[2] == "-") g_vlk.erase(v);
            else g_vlk[v] = w[2];
        }
        else if (c == "lo")
        {
            g_lo_set = w[1] != "-";
            g_lo = g_lo_set ? w[1] : std::string();
        }
        else if (c == "out") emit("O " + eng.ld_out() + eng.lo_out());
        else if (c == "ownerpid") emit("R ownerpid " + std::to_string(eng.owner_pid()));
        else if (c == "state")
        {
            bool sm = eng.mine();
            bool sp = eng.peer();
            std::string so = eng.owner();
            emit(std::string("S mine=") + (sm ? "1" : "0") + " peer=" + (sp ? "1" : "0") + " owner=" + so);
        }
        else if (c == "leaddlg")
        {
            bool a = eng.mine_dlg();
            bool b = eng.dlg_open();
            emit(std::string("R leaddlg ") + (a ? "1" : "0") + (b ? "1" : "0"));
        }
        else if (c == "ledgerdump")
        {
            std::string t;
            x0::read_text(x0::bridge_file(L"s2_blk_ledger.txt"), t);
            emit("F " + std::to_string(t.size()));
            std::istringstream is(t);
            std::string l2;
            while (std::getline(is, l2)) emit("F| " + l2);
        }
        else emit("? unknown command " + c);
    }
    return 0;
}
