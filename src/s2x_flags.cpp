#include "s2x_int.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace x0::in
{
    static bool is_lua_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
    static bool is_w(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

    bool lua_tonumber(std::string_view s0, Val& out)
    {
        size_t b = 0, e = s0.size();
        while (b < e && is_lua_space(s0[b])) ++b;
        while (e > b && is_lua_space(s0[e - 1])) --e;
        std::string s(s0.substr(b, e - b));
        if (s.empty()) return false;
        for (char c : s) if (c == 'n' || c == 'N') return false;
        size_t i = 0;
        bool neg = false;
        if (s[i] == '-' || s[i] == '+') { neg = s[i] == '-'; ++i; }
        if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X'))
        {
            size_t j = i + 2;
            bool any = false, isint = true;
            uint64_t a = 0;
            for (; j < s.size(); ++j)
            {
                char c = s[j];
                int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                if (d < 0) { isint = false; break; }
                a = a * 16 + (uint64_t)d;
                any = true;
            }
            if (isint && any)
            {
                out = Val::I((int64_t)(neg ? (0 - a) : a));
                return true;
            }
            char* endp = nullptr;
            errno = 0;
            double d = std::strtod(s.c_str(), &endp);
            if (endp && *endp == 0) { out = Val::F(d); return true; }
            return false;
        }
        bool digits = i < s.size();
        for (size_t j = i; j < s.size(); ++j) if (s[j] < '0' || s[j] > '9') { digits = false; break; }
        if (digits)
        {
            uint64_t a = 0;
            bool ovf = false;
            for (size_t j = i; j < s.size(); ++j)
            {
                uint64_t d = (uint64_t)(s[j] - '0');
                if (a > (UINT64_MAX - d) / 10) { ovf = true; break; }
                a = a * 10 + d;
            }
            if (!ovf && (a <= (uint64_t)INT64_MAX || (neg && a == (uint64_t)INT64_MAX + 1)))
            {
                out = Val::I(neg ? (int64_t)(0 - a) : (int64_t)a);
                return true;
            }
        }
        for (char c : s)
            if (!((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-')) return false;
        char* endp = nullptr;
        double d = std::strtod(s.c_str(), &endp);
        if (!endp || *endp != 0 || endp == s.c_str()) return false;
        out = Val::F(d);
        return true;
    }

    std::string lua_num_str(double v)
    {
        if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
        if (std::isnan(v)) return v != v && std::signbit(v) ? "-nan" : "nan";
        char b[64];
        std::snprintf(b, sizeof b, "%.14g", v);
        bool looks_int = true;
        for (char* p = b; *p; ++p) if (!((*p >= '0' && *p <= '9') || *p == '-')) { looks_int = false; break; }
        std::string r(b);
        if (looks_int) r += ".0";
        return r;
    }
}

namespace x0
{
    double Val::num() const
    {
        if (k == i) return (double)iv;
        if (k == f) return fv;
        if (k == s) { Val t; if (in::lua_tonumber(sv, t)) return t.num(); }
        return 0;
    }
    int64_t Val::integer() const
    {
        if (k == i) return iv;
        if (k == f) return (int64_t)fv;
        if (k == s) { Val t; if (in::lua_tonumber(sv, t)) return t.integer(); }
        return 0;
    }
    std::string Val::str() const
    {
        if (k == i) return std::to_string(iv);
        if (k == f) return in::lua_num_str(fv);
        if (k == s) return sv;
        return "nil";
    }
    bool Val::operator==(const Val& o) const
    {
        if (k == nil || o.k == nil) return k == o.k;
        if (k == s || o.k == s) return k == o.k && sv == o.sv;
        if (k == i && o.k == i) return iv == o.iv;
        return num() == o.num();
    }
    Val Val::I(int64_t v) { Val r; r.k = i; r.iv = v; return r; }
    Val Val::F(double v) { Val r; r.k = f; r.fv = v; return r; }
    Val Val::S(std::string v) { Val r; r.k = s; r.sv = std::move(v); return r; }
}

namespace x0::in
{
    const char* const kHcKeys =
        "qsync qrollback qrelay lead ldtrig ldkill ldhold ldclose ldblk ldblkr mqpull mqwin qpick choice n3kill n3trig n3use qitem qimirror n7 dmir dmirpick cjoin "
        "locks taken takenown ground loot ptake contall cbusy cdeposit cown istate qreward itake tsync lootshare lscont sstash artsync idrop qshare "
        "emsync cine doors dobj dobjgen dobjfx ien ientab ienprompt ienanim ienlamp ienhatch ientoggle lockbrk anomsync iamir vendmir vendone npsmove "
        "lifeguard downhp downsecs revivehp reviveheal reviveradius revivesecs revwait downgod downdie reviveitem bothdie lifegod bothdown dieload nopause ftfollow sleepsync sleepmode difficulty friendlyfire ffscale";

    static FlagNotify g_notify;
    void flags_set_notify(FlagNotify n) { g_notify = std::move(n); }

    static std::set<std::string>& dev_lock()
    {
        static std::set<std::string> t;
        static bool init = false;
        if (init) return t;
        init = true;
#ifdef S2X_RELEASE
        std::string text;
        if (!data_res("nola_runtime.tsv", text)) return t;
        for (auto& l : split(text, '\n', false))
        {
            auto c = split(l, '\t', true);
            if (c.size() >= 3 && c[0] == "flag" && c[2] == "c") t.insert(c[1]);
        }
#endif
        return t;
    }
    bool dev_locked(std::string_view k) { return dev_lock().count(std::string(k)) != 0; }

    Flags& flags()
    {
        static Flags f;
        return f;
    }

    FlagEnt& Flags::ent(std::string_view k)
    {
        auto it = m.find(std::string(k));
        if (it != m.end()) return it->second;
        return m.emplace(std::string(k), FlagEnt{}).first->second;
    }
    const Val* Flags::find(std::string_view k) const
    {
        auto it = m.find(std::string(k));
        return it == m.end() ? nullptr : &it->second.v;
    }
    void Flags::set_default(std::string_view k, const Val& v, bool lua_known)
    {
        FlagEnt& e = ent(k);
        e.def = v;
        e.v = v;
        e.has_def = true;
        e.lua_known = e.lua_known || lua_known;
    }

    std::vector<FlagLine> parse_flags_text(std::string_view text)
    {
        std::vector<FlagLine> out;
        size_t pos = 0;
        int ln = 0;
        while (pos <= text.size())
        {
            size_t nl = text.find('\n', pos);
            bool last = nl == std::string_view::npos;
            std::string_view line = text.substr(pos, last ? std::string_view::npos : nl - pos);
            if (last && line.empty()) break;
            ++ln;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.size() >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF) line.remove_prefix(3);
            size_t i = 0;
            while (i < line.size() && is_lua_space(line[i])) ++i;
            size_t kb = i;
            while (i < line.size() && (is_w(line[i]) || line[i] == '_')) ++i;
            size_t ke = i;
            if (ke > kb)
            {
                while (i < line.size() && is_lua_space(line[i])) ++i;
                if (i < line.size() && line[i] == '=')
                {
                    ++i;
                    while (i < line.size() && is_lua_space(line[i])) ++i;
                    size_t vb = i;
                    while (i < line.size() && (is_w(line[i]) || line[i] == '.' || line[i] == '-' || line[i] == '_' || line[i] == ',' || line[i] == ':')) ++i;
                    if (i > vb) out.push_back({std::string(line.substr(kb, ke - kb)), std::string(line.substr(vb, i - vb)), ln});
                }
            }
            if (last) break;
            pos = nl + 1;
        }
        return out;
    }

    static Val coerce(const FlagEnt* e, const std::string& raw)
    {
        if (e && e->v.k == Val::s) return Val::S(raw);
        Val n;
        if (e && e->v.k != Val::nil)
        {
            if (lua_tonumber(raw, n)) return n;
            return e->v;
        }
        if (e && e->has_def && e->def.k == Val::s) return Val::S(raw);
        if (lua_tonumber(raw, n)) return n;
        return Val::S(raw);
    }

    int Flags::apply_text(std::string_view text, std::vector<std::string>* logs, bool all)
    {
        auto lines = parse_flags_text(text);
        std::map<std::string, std::vector<std::pair<int, std::string>>> seen;
        std::vector<std::string> order;
        for (auto& l : lines)
        {
            auto& v = seen[l.k];
            if (v.empty()) order.push_back(l.k);
            v.push_back({l.line, l.v});
        }
        std::string sig;
        std::vector<std::string> duplog;
        for (auto& k : order)
        {
            auto& v = seen[k];
            if (v.size() < 2) continue;
            std::string t = k + " (";
            for (size_t j = 0; j < v.size(); ++j) t += (j ? ", " : "") + std::string("line ") + std::to_string(v[j].first) + "=" + v[j].second;
            t += ")";
            sig += t + ";";
            duplog.push_back(t);
        }
        if (sig != last_dup_sig)
        {
            last_dup_sig = sig;
            if (logs) for (auto& d : duplog) logs->push_back("NOLA x0 flags: duplicate key in flags.txt: " + d + "; the last one wins");
        }
        int changed = 0;
        std::map<std::string, Val> first;
        for (auto& l : lines)
        {
            auto it = m.find(l.k);
            if (it == m.end() || !it->second.has_def || dev_locked(l.k)) continue;
            FlagEnt& en = it->second;
            bool adopted = ad.count(l.k) != 0;
            auto oi = own.find(l.k);
            Val cur = adopted && oi != own.end() ? oi->second : en.v;
            FlagEnt tmp = en;
            tmp.v = cur;
            Val nv = coerce(&tmp, l.v);
            if (nv == cur) continue;
            if (logs && (all || !en.lua_known)) logs->push_back("AVATAR flags: " + l.k + " " + cur.str() + " -> " + nv.str());
            if (adopted) { own[l.k] = nv; continue; }
            if (!first.count(l.k)) first[l.k] = en.v;
            en.v = nv;
        }
        for (auto& [k, before] : first)
        {
            FlagEnt& en = m[k];
            if (en.v == before) continue;
            ++changed;
            if (g_notify) g_notify(k, before, en.v);
        }
        return changed;
    }

    bool Flags::set(std::string_view k, const Val& v, std::vector<std::string>* logs, std::string_view why)
    {
        FlagEnt& e = ent(k);
        if (dev_locked(k) || (e.v == v && e.v.k == v.k)) return false;
        Val before = e.v;
        e.v = v;
        if (logs) logs->push_back("NOLA x0 cfg: " + std::string(k) + " " + before.str() + " -> " + v.str() + " (" + std::string(why) + ")");
        if (g_notify) g_notify(std::string(k), before, v);
        return true;
    }

    static std::set<std::string> hc_set(const std::string& keys)
    {
        std::set<std::string> t;
        size_t i = 0;
        while (i < keys.size())
        {
            while (i < keys.size() && is_lua_space(keys[i])) ++i;
            size_t b = i;
            while (i < keys.size() && !is_lua_space(keys[i])) ++i;
            if (i > b) t.insert(keys.substr(b, i - b));
        }
        return t;
    }

    void Flags::hc_give_back(std::string_view why, std::string_view only, std::vector<std::string>* logs)
    {
        std::vector<std::string> ch;
        for (auto it = ad.begin(); it != ad.end();)
        {
            if (only.empty() || only == it->first)
            {
                const std::string k = it->first;
                Val hv = it->second;
                auto oi = own.find(k);
                Val ov = oi == own.end() ? Val{} : oi->second;
                if (oi != own.end())
                {
                    FlagEnt& e = ent(k);
                    Val before = e.v;
                    e.v = ov;
                    if (g_notify && before != ov) g_notify(k, before, ov);
                }
                ch.push_back(k + " " + hv.str() + " -> " + ov.str());
                if (oi != own.end()) own.erase(oi);
                it = ad.erase(it);
            }
            else ++it;
        }
        std::sort(ch.begin(), ch.end());
        if (!ch.empty() && logs)
        {
            std::string t;
            for (size_t j = 0; j < ch.size(); ++j) t += (j ? ", " : "") + ch[j];
            logs->push_back("NOLA hostcfg: de volta ao valor daqui (" + std::string(why) + "): " + t);
        }
    }

    int Flags::hc_adopt(std::string_view rest, const std::string& hc_keys, std::vector<std::string>* logs, const std::string& peer_id, std::string& sum_key)
    {
        static std::string cached_keys;
        static std::set<std::string> set;
        if (cached_keys != hc_keys) { cached_keys = hc_keys; set = hc_set(hc_keys); }
        std::set<std::string> seenk;
        int n = 0;
        size_t i = 0;
        while (i < rest.size())
        {
            size_t kb = i;
            while (i < rest.size() && (is_w(rest[i]) || rest[i] == '_')) ++i;
            if (i == kb || i >= rest.size() || rest[i] != '=') { i = kb + 1; continue; }
            std::string k(rest.substr(kb, i - kb));
            ++i;
            size_t vb = i;
            while (i < rest.size() && rest[i] != ';') ++i;
            std::string v(rest.substr(vb, i - vb));
            auto it = m.find(k);
            if (!set.count(k) || it == m.end() || it->second.v.k == Val::nil || seenk.count(k)) continue;
            Val cur = it->second.v;
            Val hv;
            if (cur.k == Val::s) hv = Val::S(v);
            else if (!lua_tonumber(v, hv)) continue;
            seenk.insert(k);
            ++n;
            auto oi = own.find(k);
            if (ad.count(k) && oi != own.end() && hv == oi->second)
            {
                it->second.v = hv;
                ad.erase(k);
                own.erase(k);
                if (logs) logs->push_back("NOLA hostcfg: " + k + " = " + hv.str() + " (igual ao flags.txt daqui; antes " + cur.str() + ")");
                if (g_notify && cur != hv) g_notify(k, cur, hv);
            }
            else if (cur != hv)
            {
                if (!ad.count(k)) own[k] = cur;
                it->second.v = hv;
                ad[k] = hv;
                if (logs) logs->push_back("NOLA hostcfg: " + k + " " + cur.str() + " -> " + hv.str() + " (valor do host; flags.txt daqui = " + own[k].str() + ")");
                if (g_notify) g_notify(k, cur, hv);
            }
        }
        std::vector<std::string> gone;
        for (auto& [k, v] : ad) if (!seenk.count(k)) gone.push_back(k);
        for (auto& k : gone) hc_give_back("o host nao manda mais " + k, k, logs);
        if (sum_key != peer_id)
        {
            sum_key = peer_id;
            if (logs)
            {
                char b[256];
                std::snprintf(b, sizeof b, "NOLA hostcfg: %d flags compartilhadas recebidas do host (sessao %s); %d com o valor do host diferente de flags.txt daqui", n,
                              peer_id.empty() ? "nil" : peer_id.c_str(), (int)ad.size());
                logs->push_back(b);
            }
        }
        return n;
    }

    static bool tsv_val(const TsvRow& r, Val& v)
    {
        if (r.key.size() != 1 || r.ktype != "s") return false;
        if (r.t == 'i') { Val t; if (!lua_tonumber(r.v, t)) return false; v = t; }
        else if (r.t == 'f') { Val t; if (!lua_tonumber(r.v, t)) return false; v = Val::F(t.num()); }
        else if (r.t == 's') v = Val::S(r.v);
        else if (r.t == 'b') v = Val::I(r.v == "true" ? 1 : 0);
        else return false;
        return true;
    }

    static int load_tsv_defaults(Flags& f, std::string_view name, bool lua_known, std::vector<std::string>* logs)
    {
        std::vector<TsvRow> rows;
        std::string err;
        if (!data_tsv(name, rows, &err))
        {
            if (logs) logs->push_back("NOLA x0 flags: defaults " + std::string(name) + " not loaded: " + err);
            return -1;
        }
        int n = 0;
        for (auto& r : rows)
        {
            Val v;
            if (!tsv_val(r, v)) continue;
            f.set_default(r.key[0], v, lua_known);
            ++n;
        }
        return n;
    }

    struct RoleDefs
    {
        bool ok = false;
        std::map<std::string, Val> base, host, client;
    };
    static RoleDefs& role_defs()
    {
        static RoleDefs d;
        return d;
    }

    static void role_defs_load(Flags& f)
    {
        RoleDefs& d = role_defs();
        d = RoleDefs{};
        for (int w = 0; w < 2; ++w)
        {
            std::vector<TsvRow> rows;
            if (!data_tsv(w == 0 ? "flags_role_host.tsv" : "flags_role_client.tsv", rows, nullptr)) return;
            for (auto& r : rows)
            {
                Val v;
                if (tsv_val(r, v)) (w == 0 ? d.host : d.client)[r.key[0]] = v;
            }
        }
        for (auto* m : {&d.host, &d.client})
            for (auto& [k, v] : *m)
            {
                auto it = f.m.find(k);
                d.base[k] = it != f.m.end() && it->second.has_def ? it->second.def : Val{};
            }
        d.ok = true;
    }

    int flags_role_switch(Flags& f, const std::string& to, std::vector<std::string>* logs)
    {
        RoleDefs& d = role_defs();
        if (!d.ok) return 0;
        const std::map<std::string, Val>* t = to == "host" ? &d.host : to == "client" ? &d.client : nullptr;
        std::vector<std::pair<std::string, std::pair<Val, Val>>> moved;
        std::vector<std::string> kept;
        for (auto& [k, b] : d.base)
        {
            Val nd = b;
            if (t)
            {
                auto it = t->find(k);
                if (it != t->end()) nd = it->second;
            }
            if (nd.k == Val::nil) continue;
            FlagEnt& e = f.ent(k);
            if (e.has_def && e.def == nd && e.def.k == nd.k) continue;
            Val od = e.def;
            bool adopted = f.ad.count(k) != 0;
            bool follow = !adopted && (!e.has_def || e.v == od);
            e.def = nd;
            e.has_def = true;
            if (adopted)
            {
                auto oi = f.own.find(k);
                if (oi != f.own.end() && oi->second == od) oi->second = nd;
                kept.push_back(k + " (host value)");
                continue;
            }
            if (!follow) { kept.push_back(k + "=" + e.v.str()); continue; }
            Val before = e.v;
            e.v = nd;
            moved.push_back({k, {before, nd}});
        }
        if (logs)
        {
            std::string a, b;
            for (auto& [k, p] : moved) a += (a.empty() ? "" : ", ") + k + " " + p.first.str() + " -> " + p.second.str();
            for (auto& k : kept) b += (b.empty() ? "" : ", ") + k;
            logs->push_back("NOLA x0 flags: role " + to + " defaults: " + std::to_string(moved.size()) + " changed" + (a.empty() ? "" : " (" + a + ")") +
                            (b.empty() ? "" : "; kept, set by flags.txt/cfg/host: " + b));
        }
        for (auto& [k, p] : moved)
            if (g_notify) g_notify(k, p.first, p.second);
        return (int)moved.size();
    }

    bool load_default_tables(Flags& f, std::vector<std::string>* logs)
    {
        int a = load_tsv_defaults(f, "flags_s2core.tsv", true, logs);
        int a2 = load_tsv_defaults(f, "flags_s2core_tail.tsv", true, logs);
        if (a2 > 0) a += a2;
        int b = load_tsv_defaults(f, "flags_main.tsv", true, logs);
        int c = load_tsv_defaults(f, "flags_main_extra.tsv", true, logs);
        int d = load_tsv_defaults(f, "flags_x0.tsv", false, logs);
        role_defs_load(f);
        const std::string& ro = x0::role();
        int e = ro == "host" || ro == "client" ? load_tsv_defaults(f, "flags_role_" + ro + ".tsv", false, logs) : 0;
        std::vector<TsvRow> dups;
        if (data_tsv("flags_dups.tsv", dups, nullptr))
            for (auto& r : dups)
                if (r.key.size() == 1 && logs) logs->push_back("NOLA x0 flags: default " + r.key[0] + " is set twice in the Lua source (" + r.v + "); the last one wins = " + f.ent(r.key[0]).v.str());
        if (logs)
        {
            char t[256];
            std::snprintf(t, sizeof t, "NOLA x0 flags: defaults s2core=%d main=%d main_extra=%d x0=%d role %s=%d total=%d", a, b, c, d, ro.c_str(), e, (int)f.m.size());
            logs->push_back(t);
#ifdef S2X_RELEASE
            logs->push_back("NOLA x0 flags: release build, " + std::to_string(dev_lock().size()) + " dev flags locked to their defaults");
#endif
        }
        return a >= 0 && b >= 0;
    }
}

namespace x0
{
    namespace
    {
        struct FlagCbEnt { Mod m; std::string key; FlagCb cb; };
        std::vector<FlagCbEnt>& flag_cbs() { static std::vector<FlagCbEnt> v; return v; }
        std::vector<std::string>& flag_logs() { static std::vector<std::string> v; return v; }
        bool g_notify_init = false;
        void ensure_notify()
        {
            if (g_notify_init) return;
            g_notify_init = true;
            in::flags_set_notify([](const std::string& k, const Val& a, const Val& b) {
                for (auto& c : flag_cbs())
                {
                    if (!(c.key.empty() || c.key == k)) continue;
                    if (c.m != Mod::x0 && !port(c.m)) continue;
                    try { c.cb(k, a, b); }
                    catch (...) { log("NOLA x0 flags: on_flag callback of " + std::string(mod_name(c.m)) + " threw for " + k); }
                }
            });
        }
        void flush_logs()
        {
            auto& v = flag_logs();
            for (auto& l : v) log(l);
            v.clear();
        }
        double g_lastRead = -100, g_lastStat = -100;
        uint64_t g_sig = 0;
        bool g_loaded = false;
        bool g_applied = false;
        std::string summary_line()
        {
            static const char* const ks[] = {"clientalife", "playergod", "godtoggle", "godtest", "godlayers", "autocontinue", "autobridge", "autowalk", "beacon", "anim", "ctrlrot", "disablemove", "velwrite", "compvel", "readopt", "diag", "revalidate", "maxdist", "rotate", "hz", "avatar", "god", "nodamage", "avatarmode", "avdebug", "avground"};
            std::string t = "AVATAR flags:";
            for (const char* k : ks) { const Val* v = in::flags().find(k); t += std::string(" ") + k + "=" + (v ? v->str() : std::string("nil")); }
            return t;
        }
    }

    Val flag(std::string_view key)
    {
        const Val* v = in::flags().find(key);
        return v ? *v : Val{};
    }
    int64_t fi(std::string_view key, int64_t def)
    {
        const Val* v = in::flags().find(key);
        return (v && v->k != Val::nil) ? v->integer() : def;
    }
    double fd(std::string_view key, double def)
    {
        const Val* v = in::flags().find(key);
        return (v && v->k != Val::nil) ? v->num() : def;
    }
    std::string fs(std::string_view key, std::string_view def)
    {
        const Val* v = in::flags().find(key);
        return (v && v->k != Val::nil) ? v->str() : std::string(def);
    }
    bool fon(std::string_view key)
    {
        const Val* v = in::flags().find(key);
        return v && v->k != Val::nil && v->k != Val::s && v->num() == 1;
    }
    void fset(std::string_view key, const Val& v, std::string_view why)
    {
        ensure_notify();
        in::flags().set(key, v, &flag_logs(), why);
        flush_logs();
    }
    void on_flag(Mod m, std::string_view key, FlagCb cb)
    {
        ensure_notify();
        flag_cbs().push_back({m, std::string(key), std::move(cb)});
    }
    const Val* Flag::res() const
    {
        if (!p_) p_ = &in::flags().ent(key_).v;
        return p_;
    }
    int64_t Flag::i() const { const Val* v = res(); return v->k == Val::nil ? 0 : v->integer(); }
    double Flag::d() const { const Val* v = res(); return v->k == Val::nil ? 0 : v->num(); }
    std::string Flag::s() const { const Val* v = res(); return v->k == Val::nil ? std::string() : v->str(); }
}

namespace x0
{
    namespace
    {
        struct RoleHook
        {
            RoleHook()
            {
                on_role(Mod::x0, [](const std::string&, const std::string& to) {
                    if (!g_loaded) return;
                    ensure_notify();
                    in::flags_role_switch(in::flags(), to, &flag_logs());
                    flush_logs();
                });
            }
        } g_roleHook;
    }
}

namespace x0::in
{
    void flags_poll(double t, bool force)
    {
        ensure_notify();
        auto& F = flags();
        if (!g_loaded)
        {
            g_loaded = true;
            load_default_tables(F, &flag_logs());
            force = true;
        }
        bool read = force || t - g_lastRead >= 5;
        if (!read && t - g_lastStat >= 1)
        {
            g_lastStat = t;
            WIN32_FILE_ATTRIBUTE_DATA a{};
            uint64_t sig = 0;
            if (GetFileAttributesExW(bridge_file(L"flags.txt").c_str(), GetFileExInfoStandard, &a))
                sig = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime) ^ ((uint64_t)a.nFileSizeLow << 7);
            if (sig != g_sig) read = true;
        }
        if (read)
        {
            g_lastRead = t;
            g_lastStat = t;
            WIN32_FILE_ATTRIBUTE_DATA a{};
            if (GetFileAttributesExW(bridge_file(L"flags.txt").c_str(), GetFileExInfoStandard, &a))
                g_sig = ((uint64_t)a.ftLastWriteTime.dwHighDateTime << 32 | a.ftLastWriteTime.dwLowDateTime) ^ ((uint64_t)a.nFileSizeLow << 7);
            std::string text;
            if (read_text(bridge_file(L"flags.txt"), text)) F.apply_text(text, &flag_logs(), g_applied && Flag{"cpp_port"}.i() == 1);
            if (!g_applied) { g_applied = true; if (Flag{"cpp_port"}.i() == 1) flag_logs().push_back(summary_line()); }
        }
        flush_logs();
    }
}
