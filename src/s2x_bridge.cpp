#include "s2x_int.h"
#include "s2x_fr.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>

namespace x0::in
{
    static bool up_az(char c) { return c >= 'A' && c <= 'Z'; }
    static bool tag_ch(char c) { return up_az(c) || (c >= '0' && c <= '9'); }
    static bool lspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

    std::vector<TagSpan> x1_tags(std::string_view x1)
    {
        std::vector<TagSpan> v;
        size_t i = 0;
        while (i < x1.size())
        {
            if (x1[i] != '#') { ++i; continue; }
            size_t j = i + 1;
            if (j < x1.size() && up_az(x1[j]))
            {
                while (j < x1.size() && tag_ch(x1[j])) ++j;
                if (j < x1.size() && x1[j] == ':')
                {
                    size_t pb = j + 1, pe = x1.find('#', pb);
                    if (pe == std::string_view::npos) pe = x1.size();
                    v.push_back({x1.substr(i + 1, j - i - 1), x1.substr(pb, pe - pb), i, pe});
                    i = pe;
                    continue;
                }
            }
            ++i;
        }
        return v;
    }
    bool x1_first(std::string_view x1, std::string_view tag, std::string& out)
    {
        std::string pat = "#" + std::string(tag) + ":";
        size_t p = x1.find(pat);
        if (p == std::string_view::npos) return false;
        size_t b = p + pat.size(), e = x1.find('#', b);
        out.assign(x1.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b));
        return true;
    }
    std::string_view x1_lead(std::string_view x1)
    {
        size_t p = x1.find('#');
        return x1.substr(0, p);
    }
    std::string x1_strip(std::string_view x1, const std::set<std::string>& tags, bool strip_lead)
    {
        std::string o;
        size_t lead = x1.find('#');
        if (lead == std::string_view::npos) lead = x1.size();
        size_t cur = 0;
        if (strip_lead) cur = lead;
        for (auto& t : x1_tags(x1))
        {
            if (!tags.count(std::string(t.tag))) continue;
            if (t.begin > cur) o.append(x1.substr(cur, t.begin - cur));
            cur = std::max(cur, t.end);
        }
        if (cur < x1.size()) o.append(x1.substr(cur));
        return o;
    }
    std::vector<std::string_view> split_nonempty(std::string_view s, char sep)
    {
        std::vector<std::string_view> v;
        size_t b = 0;
        while (b <= s.size())
        {
            size_t e = s.find(sep, b);
            if (e == std::string_view::npos) e = s.size();
            if (e > b) v.push_back(s.substr(b, e - b));
            b = e + 1;
        }
        return v;
    }

    static size_t key_at(std::string_view h, std::string_view key, size_t from, size_t& after)
    {
        std::string pat = "\"" + std::string(key) + "\"";
        size_t p = from;
        while ((p = h.find(pat, p)) != std::string_view::npos)
        {
            size_t i = p + pat.size();
            while (i < h.size() && lspace(h[i])) ++i;
            if (i < h.size() && h[i] == ':')
            {
                ++i;
                while (i < h.size() && lspace(h[i])) ++i;
                after = i;
                return p;
            }
            p += 1;
        }
        return std::string_view::npos;
    }

    bool json_str(std::string_view head, std::string_view key, std::string& out, size_t* vb, size_t* ve)
    {
        size_t from = 0, after = 0;
        while (true)
        {
            size_t p = key_at(head, key, from, after);
            if (p == std::string_view::npos) return false;
            if (after < head.size() && head[after] == '"')
            {
                size_t b = after + 1, e = head.find('"', b);
                if (e == std::string_view::npos) return false;
                out.assign(head.substr(b, e - b));
                if (vb) *vb = b;
                if (ve) *ve = e;
                return true;
            }
            from = p + 1;
        }
    }
    bool json_num(std::string_view head, std::string_view key, double& out)
    {
        size_t from = 0, after = 0;
        while (true)
        {
            size_t p = key_at(head, key, from, after);
            if (p == std::string_view::npos) return false;
            size_t e = after;
            while (e < head.size() && (head[e] == '-' || (head[e] >= '0' && head[e] <= '9') || head[e] == '.' || head[e] == 'e' || head[e] == 'E' || head[e] == '+')) ++e;
            if (e > after)
            {
                Val v;
                if (!lua_tonumber(head.substr(after, e - after), v)) return false;
                out = v.num();
                return true;
            }
            from = p + 1;
        }
    }
    std::string_view snap_head(std::string_view t)
    {
        size_t a = t.find("\"apps\"");
        if (a == std::string_view::npos) a = t.find("\"npcs\"");
        return t.substr(0, a);
    }

    bool parse_snap(std::string_view text, Snap& s)
    {
        Snap n;
        n.text = std::string(text);
        n.head = std::string(snap_head(text));
        double v = 0;
        n.v = json_num(n.head, "v", v) ? (int)v : -1;
        n.ok = json_num(n.head, "v", v) && v == 2;
        json_str(n.head, "sid", n.sid);
        json_str(n.head, "x1", n.x1raw);
        json_str(n.head, "shots", n.shots);
        json_str(n.head, "need", n.need);
        json_str(n.head, "avmesh", n.avmesh);
        json_str(n.head, "avparts", n.avparts);
        n.x1 = n.x1raw;
        double d;
        if (json_num(n.head, "t", d)) n.t = d;
        bool hx = json_num(n.head, "x", n.x), hy = json_num(n.head, "y", n.y), hz = json_num(n.head, "z", n.z);
        n.has_pos = hx && hy && hz;
        if (json_num(n.head, "vx", d)) n.vx = d;
        if (json_num(n.head, "vy", d)) n.vy = d;
        if (json_num(n.head, "yaw", d)) n.yaw = d;
        n.has_pt = json_num(n.head, "pt", n.pt);
        if (json_num(n.head, "gt", d)) n.gt = d;
        if (json_num(n.head, "wt", d)) n.wt = d;
        if (json_num(n.head, "wx", d)) n.wx = (int)d;
        if (json_num(n.head, "pid", d)) n.pid = (int)d;
        s = std::move(n);
        return s.ok;
    }

    std::vector<App> parse_apps(std::string_view t)
    {
        std::vector<App> v;
        size_t pa = t.find("\"apps\"");
        size_t pn = t.find("\"npcs\"");
        if (pn == std::string_view::npos) pn = t.size();
        if (pa == std::string_view::npos || pa >= pn) return v;
        std::string_view r = t.substr(pa, pn - pa);
        static const char* keys[] = {"a", "mesh", "parts", "mats", "statics"};
        size_t i = 0;
        while ((i = r.find("{\"a\":", i)) != std::string_view::npos)
        {
            size_t c = i + 1;
            std::string val[5];
            bool ok = true;
            for (int k = 0; k < 5 && ok; ++k)
            {
                std::string pat = "\"" + std::string(keys[k]) + "\":";
                if (r.compare(c, pat.size(), pat) != 0) { ok = false; break; }
                c += pat.size();
                while (c < r.size() && lspace(r[c])) ++c;
                if (c >= r.size() || r[c] != '"') { ok = false; break; }
                size_t e = r.find('"', c + 1);
                if (e == std::string_view::npos) { ok = false; break; }
                val[k] = std::string(r.substr(c + 1, e - c - 1));
                c = e + 1;
                if (k < 4)
                {
                    if (c >= r.size() || r[c] != ',') { ok = false; break; }
                    ++c;
                    while (c < r.size() && lspace(r[c])) ++c;
                }
                else if (c >= r.size() || r[c] != '}') ok = false;
            }
            if (ok)
            {
                v.push_back({val[0], val[1], val[2], val[3], val[4]});
                i = c + 1;
            }
            else ++i;
        }
        return v;
    }

    uint32_t djb2(std::string_view s)
    {
        uint32_t h = 5381;
        for (unsigned char c : s) h = h * 33u + c;
        return h;
    }
    std::string hash_hex(std::string_view s)
    {
        char b[16];
        std::snprintf(b, sizeof b, "%08x", djb2(s));
        return b;
    }

    bool nr_tag(std::string_view t, bool nrdoor)
    {
        static const char* k[] = {"J", "QN", "QP", "QI", "T", "G", "W", "U", "Y", "DR", "IDR", "IDU"};
        for (auto* x : k) if (t == x) return true;
        static const char* mc[] = {"UR", "YR", "GR"};
        for (auto* x : mc) if (t == x) return true;
        return nrdoor && t == "O";
    }

    static bool hexrun(std::string_view s, size_t& i)
    {
        size_t b = i;
        while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) ++i;
        return i > b;
    }
    static bool digrun(std::string_view s, size_t& i)
    {
        size_t b = i;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') ++i;
        return i > b;
    }
    static bool match_hexdot(std::string_view x1, std::string_view pfx, std::string& hx, std::string& num)
    {
        size_t p = 0;
        while ((p = x1.find(pfx, p)) != std::string_view::npos)
        {
            size_t i = p + pfx.size(), hb = i;
            if (hexrun(x1, i) && i < x1.size() && x1[i] == '.')
            {
                size_t he = i;
                ++i;
                size_t nb = i;
                if (digrun(x1, i))
                {
                    hx.assign(x1.substr(hb, he - hb));
                    num.assign(x1.substr(nb, i - nb));
                    return true;
                }
            }
            ++p;
        }
        return false;
    }
    static std::string cut(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n); }

    bool NetRel::sess(const std::string* pid)
    {
        bool same = (pid == nullptr && !has_pid) || (pid && has_pid && *pid == this->pid);
        if (!same)
        {
            if (has_pid && logs)
            {
                char b[200];
                std::snprintf(b, sizeof b, "NOLA netrel: nova sessao do outro lado -> %d pendentes e %d vistos esquecidos", (int)p.size(), (int)seen.size());
                logs->push_back(b);
            }
            has_pid = pid != nullptr;
            this->pid = pid ? *pid : std::string();
            p.clear();
            q.clear();
            seen.clear();
            hold.clear();
            ack = 0;
            acked = false;
            ack_out.clear();
            has_raw = false;
            raw.clear();
            merged.clear();
        }
        return pid != nullptr;
    }

    std::string NetRel::x1in(const std::string& x1, double now, const std::string* pid, bool peer_ok, const std::string& my_nonce, double hold_s, double seen_s, bool nrdoor)
    {
        if (!peer_ok) return x1;
        if (!inited) { inited = true; s.at = now; }
        if (!sess(pid)) return x1;
        if (has_raw && raw == x1 && now < raw_t) return merged;
        if (!has_raw || raw != x1)
        {
            for (auto& t : x1_tags(x1))
                if (nr_tag(t.tag, nrdoor))
                    for (auto e : split_nonempty(t.pay, '|')) seen[std::string(t.tag) + "=" + std::string(e)] = now;
            std::string rs;
            if (x1_first(x1, "RS", rs))
            {
                for (auto kv : split_nonempty(rs, '`'))
                {
                    std::string k(kv);
                    if (seen.count(k)) ++s.dup;
                    else
                    {
                        seen[k] = now;
                        hold.push_back({k, now});
                        ++s.rin;
                        if (s.rin <= 20 && logs) logs->push_back("NOLA netrel: recebido de novo (perdido na falha da rede): " + cut(k, 160));
                    }
                }
            }
            std::string nc, nn;
            if (match_hexdot(x1, "#AS:", nc, nn)) ack_out = "#AK:" + nc + "." + nn;
            std::string mc, mn;
            if (match_hexdot(x1, "#AK:", mc, mn) && mc == my_nonce)
            {
                acked = true;
                Val v;
                int64_t a = lua_tonumber(mn, v) ? v.integer() : 0;
                ack = std::max(ack, a);
            }
            if (now - prune_at >= 5)
            {
                prune_at = now;
                for (auto it = seen.begin(); it != seen.end();)
                    if (now - it->second > seen_s) it = seen.erase(it);
                    else ++it;
            }
        }
        has_raw = true;
        raw = x1;
        raw_t = now + 0.5;
        std::vector<std::string> order;
        std::map<std::string, std::vector<std::string>> add;
        std::vector<Hold> keep;
        for (auto& h : hold)
        {
            if (now - h.at < hold_s)
            {
                keep.push_back(h);
                size_t eq = h.k.find('=');
                bool ok = eq != std::string::npos && eq > 0 && up_az(h.k[0]) && eq + 1 < h.k.size();
                for (size_t j = 1; ok && j < eq; ++j) if (!tag_ch(h.k[j])) ok = false;
                if (ok)
                {
                    std::string tag = h.k.substr(0, eq);
                    if (!add.count(tag)) order.push_back(tag);
                    add[tag].push_back(h.k.substr(eq + 1));
                }
            }
        }
        hold = keep;
        std::string out = x1;
        for (auto& tag : order)
        {
            std::string list;
            for (size_t j = 0; j < add[tag].size(); ++j) list += (j ? "|" : "") + add[tag][j];
            std::string pat = "#" + tag + ":";
            size_t b = out.find(pat);
            if (b != std::string::npos) out = out.substr(0, b + pat.size()) + list + "|" + out.substr(b + pat.size());
            else out += pat + list;
        }
        merged = out;
        return out;
    }

    std::string NetRel::out(const std::string& sx, double now, const std::string& remote_x1, const std::string* pid, bool peer_ok, const std::string& my_nonce,
                            double mx, int per, double hold_s, double seen_s, bool nrdoor)
    {
        if (!inited) { inited = true; s.at = now; }
        x1in(remote_x1, now, pid, peer_ok, my_nonce, hold_s, seen_s, nrdoor);
        bool known = sess(pid);
        ++n;
        std::set<std::string> cur;
        for (auto& [k, qq] : p)
            if (qq.run && !qq.done && ack >= qq.n0) { qq.done = true; qq.run = false; }
        if (known)
        {
            for (auto& t : x1_tags(sx))
            {
                if (!nr_tag(t.tag, nrdoor)) continue;
                for (auto e : split_nonempty(t.pay, '|'))
                {
                    if (e.find('`') != std::string_view::npos) continue;
                    std::string k = std::string(t.tag) + "=" + std::string(e);
                    cur.insert(k);
                    if (!p.count(k)) { ++ins; Q nq; nq.k = k; nq.at = now; nq.o = ins; p[k] = nq; }
                }
            }
        }
        std::vector<std::string> join;
        for (auto it = p.begin(); it != p.end();)
        {
            Q& qq = it->second;
            if (qq.done)
            {
                if (!cur.count(it->first)) { it = p.erase(it); continue; }
            }
            else if (now - qq.at > mx)
            {
                ++s.gone;
                if (logs) logs->push_back("NOLA netrel: desisti (sem ack em " + lua_num_str_or_int(mx) + " s): " + cut(it->first, 160));
                it = p.erase(it);
                continue;
            }
            else if (!cur.count(it->first) && !qq.inq && acked) join.push_back(it->first);
            ++it;
        }
        std::sort(join.begin(), join.end(), [&](const std::string& a, const std::string& b) { return p[a].o < p[b].o; });
        for (auto& k : join) { p[k].inq = true; q.push_back({k, p[k].o}); }
        std::vector<std::pair<std::string, int64_t>> keepq;
        std::vector<std::string> t;
        size_t len = 0;
        bool full = false;
        for (auto& [k, qo] : q)
        {
            auto it = p.find(k);
            if (it == p.end() || it->second.o != qo || it->second.done) continue;
            keepq.push_back({k, qo});
            if ((int)t.size() >= per || (!t.empty() && len + k.size() > 3000)) full = true;
            if (!full)
            {
                t.push_back(k);
                len += k.size() + 1;
                it->second.inc = true;
                if (!it->second.rs)
                {
                    it->second.rs = true;
                    ++s.rs;
                    if (s.rs <= 20 && logs) logs->push_back("NOLA netrel: sem ack, reenviado: " + cut(k, 160));
                }
            }
        }
        q = keepq;
        std::string outs = sx;
        if (!keepq.empty())
        {
            std::set<std::string> blocked;
            for (auto& [k, qo] : keepq)
            {
                size_t eq = k.find('=');
                blocked.insert(eq == std::string::npos ? std::string("?") : k.substr(0, eq));
            }
            std::vector<std::string> late;
            std::string o;
            size_t c = 0;
            for (auto& ts : x1_tags(sx))
            {
                std::string tag(ts.tag);
                if (!(blocked.count(tag) && nr_tag(tag, nrdoor))) continue;
                o.append(sx.substr(c, ts.begin - c));
                std::vector<std::string> kept;
                for (auto e : split_nonempty(ts.pay, '|'))
                {
                    std::string k = tag + "=" + std::string(e);
                    auto it = p.find(k);
                    if (it != p.end() && !it->second.done)
                    {
                        cur.erase(k);
                        if (!it->second.inq) { it->second.inq = true; late.push_back(k); }
                    }
                    else kept.emplace_back(e);
                }
                o += "#" + tag + ":";
                for (size_t j = 0; j < kept.size(); ++j) o += (j ? "|" : "") + kept[j];
                c = ts.end;
            }
            o.append(sx.substr(c));
            outs = o;
            std::sort(late.begin(), late.end(), [&](const std::string& a, const std::string& b) { return p[a].o < p[b].o; });
            for (auto& k : late) q.push_back({k, p[k].o});
        }
        for (auto& [k, qq] : p)
        {
            if (qq.done) continue;
            if (cur.count(k) || qq.inc) { if (!qq.run) { qq.run = true; qq.n0 = n; } }
            else qq.run = false;
            qq.inc = false;
        }
        if (!t.empty())
        {
            outs += "#RS:";
            for (size_t j = 0; j < t.size(); ++j) outs += (j ? "`" : "") + t[j];
        }
        if (now - s.at >= 10)
        {
            if (s.rs + s.rin + s.dup + s.gone > 0 && logs)
            {
                int np = 0;
                for (auto& [k, qq] : p) if (!qq.done) ++np;
                char b[300];
                std::snprintf(b, sizeof b, "NOLA netrel 10 s: pendentes=%d reenviados=%d recebidos_de_novo=%d duplicados_ignorados=%d desistidos=%d ack=%lld/%lld", np, s.rs, s.rin, s.dup, s.gone,
                              (long long)ack, (long long)n);
                logs->push_back(b);
            }
            s = S{};
            s.at = now;
        }
        return outs + "#AS:" + my_nonce + "." + std::to_string(n) + ack_out;
    }

    void PeerMirror::on_bye(double now, const std::string& sid)
    {
        bye_has = true;
        bye_sid = sid;
        if (p.state == Peer::up) { p.state = Peer::silent; p.silent_at = now; }
        if (p.state == Peer::silent && !p.left) { p.left = true; p.bye = true; p.left_at = now; ++p.lefts; }
        p.local = false;
    }

    void PeerMirror::on_local_end(double now)
    {
        if (p.state == Peer::up) { p.state = Peer::silent; p.silent_at = now; }
        if (p.state == Peer::silent && !p.left) { p.left = true; p.bye = false; p.left_at = now; ++p.lefts; }
        p.local = p.left;
        p.id.clear();
    }

    void PeerMirror::update(const Snap& sn, double now, double silent_s, double left_s, const std::string& my_nonce)
    {
        bool fresh = sn.recv >= 0 && (now - sn.recv) < silent_s && !(bye_has && sn.sid == bye_sid);
        if (!fresh)
        {
            if (p.state == Peer::up) { p.state = Peer::silent; p.silent_at = now; }
            if (p.state == Peer::silent && !p.left && left_s > 0 && now - p.silent_at >= left_s) { p.left = true; p.bye = false; p.local = false; p.left_at = now; ++p.lefts; }
            return;
        }
        p.left = false;
        p.bye = false;
        p.local = false;
        p.pid = sn.pid;
        if (!p.live)
        {
            if (!has_first) { has_first = true; first_is_t = sn.t >= 0; first_t = sn.t; p.live = false; }
            else if ((sn.t >= 0) != first_is_t || (first_is_t && sn.t != first_t)) p.live = true;
        }
        std::string h;
        std::string role, mod, core, diff, nonce;
        bool hasH = false;
        if (x1_first(sn.x1raw, "H", h))
        {
            auto f = split(h, ',', true);
            if (f.size() == 5) { role = f[0]; mod = f[1]; core = f[2]; diff = f[3]; nonce = f[4]; hasH = true; }
        }
        std::string id = sn.sid + "/" + (hasH ? nonce : std::string("nil"));
        if (p.state != Peer::up) { p.state = Peer::up; ++p.ups; }
        if (id != p.id)
        {
            p.id = id;
            p.sid = sn.sid;
            p.nonce = nonce;
            p.role = role;
            p.mod = mod;
            p.core = core;
            p.diff = diff;
            ++p.sessions;
            p.loopback = hasH && nonce == my_nonce;
        }
    }

    std::string lua_num_str_or_int(double v)
    {
        if (std::floor(v) == v && std::fabs(v) < 1e15) return std::to_string((long long)v);
        return lua_num_str(v);
    }

    static std::string new_nonce()
    {
        std::random_device rd;
        unsigned r = 1 + rd() % 0xFFFFFu;
        char b[32];
        std::snprintf(b, sizeof b, "%x%x", (unsigned)(std::time(nullptr) % 0xFFFFFF), r);
        return b;
    }

    Bridge& bridge()
    {
        static Bridge b;
        static const bool seeded = [] { b.nonce = new_nonce(); return true; }();
        (void)seeded;
        return b;
    }

    Cfg bridge_cfg()
    {
        Cfg c;
        c.netrel = fi("netrel", 0) == 1;
        c.nrdoor = fi("nrdoor", 0) == 1;
        c.netrelmax = fd("netrelmax", 120);
        c.netrelper = (int)fi("netrelper", 40);
        c.netrelhold = fd("netrelhold", 6);
        c.netrelseen = fd("netrelseen", 300);
        c.peersilent = fd("peersilent", 5);
        c.netlockkeep = fd("netlockkeep", 0);
        c.peerleft = fd("peerleft", 60);
        c.peerbye = fi("peerbye", 1) == 1;
        c.appsper = (int)fi("appsper", 2);
        return c;
    }

    bool owner_ported(std::string_view tag)
    {
        auto& B = bridge();
        for (auto& o : B.own) if (o.tag == tag) return port(o.m);
        for (auto& o : B.ownv) if (o.tag == tag) return port(o.m);
        return false;
    }

    int Bridge::views_n() const { return ros.on ? std::clamp(ros.max, 1, kMaxClients) : 1; }

    bool Bridge::hub_host() const { return ros.on && is_host(); }

    int Bridge::view_pid(int v) const
    {
        if (v < 0 || v >= kMaxClients) return -1;
        if (is_host()) return v + 1;
        if (v == 0) return 0;
        return vw[(size_t)v].snap.ok ? vw[(size_t)v].snap.pid : -1;
    }

    void Bridge::roster_step(double t)
    {
        if (!mbox.roster) return;
        Roster r;
        if (!mbox.roster(r)) r = Roster{};
        std::lock_guard<std::recursive_mutex> g(mu);
        bool changed = r.on != ros.on || r.max != ros.max || r.you != ros.you || r.full != ros.full || r.seq != ros.seq;
        int was = views_n();
        ros = std::move(r);
        int nv = views_n();
        for (int v = nv; v < was; ++v)
        {
            vw[(size_t)v] = View{};
            logs.push_back("NOLA x0 bridge: view " + std::to_string(v) + " closed (the relay now has " + std::to_string(nv) + " view(s))");
        }
        if (changed && ros.on)
        {
            std::string pl;
            for (auto& e : ros.peers) pl += (pl.empty() ? "" : " ") + std::to_string(e.pid) + ":" + e.st;
            char b[160];
            std::snprintf(b, sizeof b, "NOLA ROSTER: %s, me pid %d, max %d clients, full=%d, seq %llu: ", is_host() ? "hub host" : "client", is_host() ? 0 : ros.you, ros.max, (int)ros.full,
                          (unsigned long long)ros.seq);
            logs.push_back(b + (pl.empty() ? std::string("no clients") : pl));
        }
        (void)t;
    }

    std::string Bridge::personal(int v) const
    {
        std::string o;
        if (v < 0 || v >= kMaxClients) return o;
        for (auto& p : ownv)
            if (p.has[(size_t)v] && port(p.m)) o += p.cache[(size_t)v];
        return o;
    }

    void Bridge::run_producers()
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        bool hubv = hub_host();
        int nvh = hubv ? views_n() : 1;
        for (auto& o : own)
        {
            if (!port(o.m)) { o.has = false; o.vhas.fill(false); continue; }
            if (hubv && o.pv)
            {
                for (int v = 0; v < kMaxClients; ++v)
                {
                    if (v >= nvh) { o.vhas[(size_t)v] = false; continue; }
                    std::string r;
                    if (sched().guard(("own_tag_hub " + o.tag).c_str(), o.m, o.errs, o.off, [&] { r = o.pv(v); }))
                    {
                        if (!r.empty() && !o.tag.empty() && o.tag[0] != '@' && r[0] != '#') r = "#" + o.tag + ":" + r;
                        o.vcache[(size_t)v] = r;
                        o.vhas[(size_t)v] = true;
                    }
                    else if (o.off) o.vhas.fill(false);
                }
                o.has = o.vhas[0];
                o.cache = o.vcache[0];
                continue;
            }
            o.vhas.fill(false);
            std::string r;
            if (sched().guard(("own_tag " + o.tag).c_str(), o.m, o.errs, o.off, [&] { r = o.p(); }))
            {
                if (!r.empty() && !o.tag.empty() && o.tag[0] != '@' && r[0] != '#') r = "#" + o.tag + ":" + r;
                o.cache = r;
                o.has = true;
            }
            else if (o.off) o.has = false;
        }
        int nv = hub_host() ? views_n() : 1;
        for (auto& o : ownv)
        {
            if (!port(o.m)) { o.has.fill(false); continue; }
            for (int v = 0; v < kMaxClients; ++v)
            {
                if (v >= nv) { o.has[(size_t)v] = false; continue; }
                std::string r;
                if (sched().guard(("own_tag_view " + o.tag).c_str(), o.m, o.errs, o.off, [&] { r = o.p(v); }))
                {
                    if (!r.empty() && !o.tag.empty() && o.tag[0] != '@' && r[0] != '#') r = "#" + o.tag + ":" + r;
                    o.cache[(size_t)v] = r;
                    o.has[(size_t)v] = true;
                }
                else if (o.off) o.has.fill(false);
            }
        }
    }

    std::string Bridge::needs_out(double t)
    {
        std::vector<std::string> v;
        for (auto it = need.begin(); it != need.end();)
        {
            if (t - it->second > 1 || rapps.count(it->first)) it = need.erase(it);
            else { v.push_back(it->first); ++it; }
        }
        std::string o;
        for (size_t i = 0; i < v.size() && i < 16; ++i) o += (i ? "," : "") + v[i];
        return o;
    }

    std::string Bridge::apps_out(double t, int appsper)
    {
        int n = 0;
        return apps_out(t, appsper, n);
    }

    std::string Bridge::apps_out(double t, int appsper, int& count)
    {
        std::vector<std::string> out;
        for (auto it = lapps.begin(); it != lapps.end();)
        {
            LApp& a = it->second;
            if (t - a.used > 120) { it = lapps.erase(it); continue; }
            if (t - a.used < 2)
            {
                bool burst = t - a.sent < 0.25;
                if (!burst && count < appsper && (a.sent < 0 || (rneed.count(a.h) && t - a.sent >= 1))) { a.sent = t; burst = true; }
                if (burst) { out.push_back(a.js); ++count; }
            }
            ++it;
        }
        std::string o;
        for (size_t i = 0; i < out.size(); ++i) o += (i ? "," : "") + out[i];
        return o;
    }

    const Bridge::Own* Bridge::pseudo(std::string_view tag) const
    {
        for (auto& o : own)
            if (o.tag == tag && o.has && port(o.m)) return &o;
        return nullptr;
    }

    std::string Bridge::make_x1(double t)
    {
        Cfg c = bridge_cfg();
        std::string nx;
        if (auto* l = pseudo("@W")) nx = l->cache;
        for (auto& o : own)
        {
            if (o.tag.empty() || o.tag[0] == '@' || !o.has || !port(o.m)) continue;
            nx += o.cache;
        }
        std::string hh;
        if (x1_first(nx, "H", hh))
        {
            auto fl = split(hh, ',', true);
            if (fl.size() == 5) nonce = fl[4];
        }
        int nv = hub_host() ? views_n() : 1;
        bool hubv = false;
        if (hub_host())
            for (auto& o : own)
                if (o.pv && port(o.m)) { hubv = true; break; }
        vx1.clear();
        for (int v = 0; v < nv; ++v)
        {
            View& w = vw[(size_t)v];
            std::string base = nx;
            if (hubv)
            {
                base.clear();
                if (auto* l = pseudo("@W")) base = l->cache;
                for (auto& o : own)
                {
                    if (o.tag.empty() || o.tag[0] == '@' || !port(o.m)) continue;
                    if (o.pv) { if (o.vhas[(size_t)v]) base += o.vcache[(size_t)v]; continue; }
                    if (o.has) base += o.cache;
                }
            }
            std::string xv = base + personal(v);
            if (c.netrel)
            {
                w.nr.logs = &logs;
                bool ok = !(w.peer.p.state == Peer::silent || !w.peer.p.live);
                const std::string* pid = w.peer.p.id.empty() ? nullptr : &w.peer.p.id;
                xv = w.nr.out(xv, t, w.snap.x1raw, pid, ok, nonce, c.netrelmax, c.netrelper, c.netrelhold, c.netrelseen, c.nrdoor);
            }
            if (v == 0) my_x1 = xv;
            else vx1.push_back(std::move(xv));
        }
        return my_x1;
    }

    std::string Bridge::make_body(const LocalIn& L, double t)
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        Cfg c = bridge_cfg();
        if (sid.empty())
        {
            uint64_t r = (uint64_t)wall_ms() * 6364136223846793005ULL + 1442695040888963407ULL;
            sid = std::to_string((long long)(wall_ms() / 1000)) + "-" + std::to_string((unsigned long long)((r >> 33) % 100000ULL + 1));
        }
        ++f;
        std::string x1 = make_x1(t);
        std::string tm = "\"gt\":-1.0,\"wt\":-1.0,\"wx\":-1";
        if (auto* o = pseudo("@time")) if (!o->cache.empty()) tm = o->cache;
        std::string shots;
        if (auto* o = pseudo("@shots")) shots = o->cache;
        std::string nd = needs_out(t);
        if (auto* o = pseudo("@need")) if (!o->cache.empty()) nd = nd.empty() ? o->cache : nd + "," + o->cache;
        int napps = 0;
        std::string ap = apps_out(t, c.appsper, napps);
        if (auto* o = pseudo("@apps")) if (!o->cache.empty()) ap = ap.empty() ? o->cache : ap + "," + o->cache;
        std::string npcs = "[]";
        if (auto* o = pseudo("@npcs")) if (!o->cache.empty()) npcs = o->cache;
        std::string body;
        char b[512];
        std::snprintf(b, sizeof b, "{\"v\":2,\"sid\":\"%s\",\"f\":%lld,\"t\":%.3f,\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,\"vx\":%.1f,\"vy\":%.1f,\"yaw\":%.1f,\"pt\":%.1f,", sid.c_str(), (long long)f, t, L.x, L.y,
                      L.z, L.vx, L.vy, L.yaw, L.pt);
        body = b;
        body += tm;
        body += ",\"avmesh\":\"\",\"avparts\":\"\",\"x1\":\"" + x1 + "\",\"shots\":\"" + shots + "\",\"need\":\"" + nd + "\",\"apps\":[" + ap + "],\"npcs\":" + npcs;
        if (!vx1.empty())
        {
            body += ",\"vx1\":[";
            for (size_t i = 0; i < vx1.size(); ++i) body += (i ? ",\"" : "\"") + vx1[i] + "\"";
            body += "]";
        }
        body += "}";
        S2FR(fr::tx_snap(sid, f, body.size(), x1));
        S2FR(for (size_t i = 0; i < vx1.size(); ++i) fr::tx_view((int)i + 1, view_pid((int)i + 1), f, vx1[i]));
        return body;
    }

    bool Bridge::io_step(const LocalIn& L, double t)
    {
        if (!L.ok) { ++nopawn; return false; }
        roster_step(t);
        if (mbox.bye)
        {
            std::string bsid;
            if (mbox.bye(bye_seen, bsid) && bridge_cfg().peerbye)
            {
                std::lock_guard<std::recursive_mutex> g(mu);
                bool was = peer.p.left;
                peer.on_bye(t, bsid);
                if (!was && peer.p.left) logs.push_back("NOLA PEER BYE: o outro jogo fechou (sid=" + bsid + "): sessao do outro lado encerrada");
            }
        }
        std::string body = make_body(L, t);
        std::string text;
        if (mbox.on && mbox.on())
        {
            mbox.put(std::move(body));
            ++outs;
            std::string nx;
            if (mbox.get(nx)) mb_last = std::move(nx);
            if (mb_last.empty()) { ++noin; return false; }
            text = mb_last;
        }
        else
        {
            if (!write_atomic(bridge_file(L"out.json"), body)) ++outfail;
            else ++outs;
            if (!read_all(bridge_file(L"in.json"), text)) { ++noin; return false; }
        }
        size_t z = text.find('\x1a');
        if (z != std::string::npos) text.resize(z);
        bool changed = false;
        take_in(text, t, changed);
        int nv = views_n();
        bool pbye = nv > 1 && bridge_cfg().peerbye;
        for (int v = 1; v < nv; ++v)
        {
            View& w = vw[(size_t)v];
            if (mbox.vbye && pbye)
            {
                std::string bsid;
                if (mbox.vbye(v, w.bye_seen, bsid))
                {
                    std::lock_guard<std::recursive_mutex> g(mu);
                    bool was = w.peer.p.left;
                    w.peer.on_bye(t, bsid);
                    if (!was && w.peer.p.left) logs.push_back("NOLA PEER BYE view " + std::to_string(v) + " pid " + std::to_string(view_pid(v)) + " (sid=" + bsid + "): that player closed the game");
                }
            }
            std::string tv;
            if (mbox.on && mbox.on())
            {
                std::string nx;
                if (mbox.vget && mbox.vget(v, nx)) w.mb_last = std::move(nx);
                if (w.mb_last.empty()) continue;
                tv = w.mb_last;
            }
            else if (!read_all(bridge_file(L"in_v" + std::to_wstring(v) + L".json"), tv)) continue;
            size_t zv = tv.find('\x1a');
            if (zv != std::string::npos) tv.resize(zv);
            bool chv = false;
            take_in_view(v, tv, t, chv);
        }
        return changed;
    }

    std::string Bridge::take_in(const std::string& text, double t, bool& changed) { return take_in_view(0, text, t, changed); }

    std::string Bridge::take_in_view(int v, const std::string& text, double t, bool& changed)
    {
        std::lock_guard<std::recursive_mutex> g(mu);
        Cfg c = bridge_cfg();
        changed = false;
        if (v < 0 || v >= kMaxClients) return std::string();
        View& w = vw[(size_t)v];
        Snap s;
        parse_snap(text, s);
        if (!s.ok)
        {
            std::string rv = s.v >= 0 ? std::to_string(s.v) : std::string("nil");
            proto_err = "remoto v=" + rv + " local v=2";
            if (t - proto_log_at >= 10)
            {
                proto_log_at = t;
                logs.push_back("PROTO ERRO: formato de snapshot incompativel (" + proto_err + "); snapshot remoto IGNORADO. Ponha o mesmo main.lua e o mesmo relay nos dois PCs.");
            }
            w.last_in_text = text;
            w.has_merged = false;
            S2FR(fr::rx_drop(v, view_pid(v), proto_err));
            return std::string();
        }
        proto_err.clear();
        ++reads;
        for (auto& a : parse_apps(text)) rapps[a.a] = {a.parts, t};
        for (auto it = rapps.begin(); it != rapps.end();)
            if (t - it->second.at > 120) it = rapps.erase(it);
            else ++it;
        bool isnew = !w.snap.ok || (s.t >= 0 ? s.t != w.snap.t : (s.x != w.snap.x || s.y != w.snap.y || s.z != w.snap.z));
        if (!s.has_pos) isnew = false;
        if (isnew)
        {
            s.seq = w.snap.seq + 1;
            s.recv = t;
            changed = true;
            ++ins;
            S2FR(fr::rx_snap(v, view_pid(v), text, s.sid, s.x1raw));
        }
        else
        {
            s.seq = w.snap.seq;
            s.recv = w.snap.recv;
        }
        w.rneed.clear();
        for (auto h : split_nonempty(s.need, ',')) w.rneed.insert(std::string(h));
        int nv = views_n();
        if (nv == 1) rneed = w.rneed;
        else
        {
            rneed.clear();
            for (int i = 0; i < nv; ++i) rneed.insert(vw[(size_t)i].rneed.begin(), vw[(size_t)i].rneed.end());
        }
        w.peer.update(s, t, c.peersilent, c.peerleft, nonce);
        if (w.peer.p.sessions != w.sess_seen)
        {
            w.sess_seen = w.peer.p.sessions;
            w.new_sess = true;
        }
        std::string mx = s.x1raw;
        if (c.netrel && (v == 0 || hub_host()))
        {
            w.nr.logs = &logs;
            bool ok = !(w.peer.p.state == Peer::silent || !w.peer.p.live);
            const std::string* pid = w.peer.p.id.empty() ? nullptr : &w.peer.p.id;
            mx = w.nr.x1in(s.x1raw, t, pid, ok, nonce, c.netrelhold, c.netrelseen, c.nrdoor);
        }
        s.x1 = mx;
        w.snap = std::move(s);
        if (changed) w.pending = true;
        w.last_in_text = text;
        if (mx == w.snap.x1raw) { w.has_merged = false; w.merged_text.clear(); return std::string(); }
        std::string cur;
        size_t vb = 0, ve = 0;
        if (!json_str(text, "x1", cur, &vb, &ve)) { w.has_merged = false; return std::string(); }
        w.merged_text = text.substr(0, vb) + mx + text.substr(ve);
        w.has_merged = true;
        return w.merged_text;
    }

    void Bridge::dispatch()
    {
        int nv = views_n();
        for (int v = 0; v < nv; ++v) dispatch_view(v);
    }

    void Bridge::dispatch_view(int v)
    {
        Snap s;
        Peer pp;
        bool ns = false;
        {
            std::lock_guard<std::recursive_mutex> g(mu);
            View& w = vw[(size_t)v];
            ns = w.new_sess;
            w.new_sess = false;
            pp = w.peer.p;
            if (w.pending)
            {
                w.pending = false;
                s = w.snap;
            }
        }
        if (ns)
            for (auto& sc : sesscbs)
            {
                if (!port(sc.m)) continue;
                sched().guard("on_view_session", sc.m, sc.errs, sc.off, [&] { sc.c(v, pp); });
            }
        if (!s.ok) return;
        if (v == 0) dispatch_legacy(s);
        auto tags = x1_tags(s.x1);
        for (auto& cn : consv)
        {
            if (!port(cn.m)) continue;
            std::string val;
            bool has = false;
            if (pseudo_lead(cn.tag)) { val = std::string(x1_lead(s.x1)); has = !val.empty(); }
            else
                for (auto& tg : tags) if (tg.tag == cn.tag) { val = std::string(tg.pay); has = true; break; }
            if (!has) continue;
            sched().guard(("on_tag_view " + cn.tag).c_str(), cn.m, cn.errs, cn.off, [&] { cn.c(val, s, v); });
        }
        for (auto& sc : snapcbsv)
        {
            if (!port(sc.m)) continue;
            sched().guard("on_snap_view", sc.m, sc.errs, sc.off, [&] { sc.c(s, v); });
        }
    }

    void Bridge::dispatch_legacy(const Snap& s)
    {
        Cfg c = bridge_cfg();
        if (is_client() && fi("hostcfg", 0) == 1 && !port(Mod::session) && !peer.p.loopback)
        {
            std::string hc;
            if (x1_first(s.x1, "HC", hc))
            {
                size_t sc = hc.find(';');
                std::string rest = sc == std::string::npos ? std::string() : hc.substr(sc + 1);
                static std::string sum;
                flags().hc_adopt(rest, kHcKeys, nullptr, peer.p.id, sum);
            }
        }
        else if (fi("hostcfg", 0) != 1 && !port(Mod::session) && !flags().ad.empty()) flags().hc_give_back("hostcfg=0", "", nullptr);
        auto tags = x1_tags(s.x1);
        for (auto& cn : cons)
        {
            if (!port(cn.m)) continue;
            std::string v;
            bool has = false;
            if (pseudo_lead(cn.tag)) { v = std::string(x1_lead(s.x1)); has = !v.empty(); }
            else
                for (auto& t : tags) if (t.tag == cn.tag) { v = std::string(t.pay); has = true; break; }
            if (!has) continue;
            sched().guard(("on_tag " + cn.tag).c_str(), cn.m, cn.errs, cn.off, [&] { cn.c(v, s); });
        }
        for (auto& sc : snapcbs)
        {
            if (!port(sc.m)) continue;
            sched().guard("on_snap", sc.m, sc.errs, sc.off, [&] { sc.c(s); });
        }
        (void)c;
    }
}

namespace x0
{
    const Snap& remote() { return in::bridge().snap; }
    bool rpart(std::string_view tag, std::string& out) { return rpart(tag, out, 0); }
    bool rpart(std::string_view tag, std::string& out, int view)
    {
        auto& B = in::bridge();
        if (view < 0 || view >= B.views_n()) return false;
        const auto& W = B.vw[(size_t)view];
        const Peer& P = W.peer.p;
        if (P.state == Peer::silent)
        {
            double k = fd("netlockkeep", 0);
            bool keep = k > 0 && (tag == "LK" || tag == "K") && now() - P.silent_at < k;
            if (!keep) return false;
        }
        if (!P.live) return false;
        if (B.pseudo_lead(tag)) { out = std::string(in::x1_lead(W.snap.x1)); return true; }
        return in::x1_first(W.snap.x1, tag, out);
    }
    const Peer& peer() { return in::bridge().peer.p; }
    bool coop()
    {
        auto& B = in::bridge();
        int nv = B.views_n();
        for (int v = 0; v < nv; ++v)
        {
            const Peer& p = B.vw[(size_t)v].peer.p;
            if (!p.loopback && (p.state == Peer::up || (p.state == Peer::silent && !p.left))) return true;
        }
        return false;
    }
    int views() { return in::bridge().views_n(); }
    int max_clients()
    {
        auto& B = in::bridge();
        return B.ros.on ? std::clamp(B.ros.max, 1, kMaxClients) : 1;
    }
    bool hub() { return in::bridge().hub_host(); }
    const Roster& roster() { return in::bridge().ros; }
    void coop_session_reset()
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        double t = now();
        for (auto& w : B.vw)
        {
            w.peer.on_local_end(t);
            w.mb_last.clear();
        }
        B.ros = Roster{};
        B.sid.clear();
    }
    const Snap& remote(int view)
    {
        static const Snap none{};
        auto& B = in::bridge();
        return view >= 0 && view < B.views_n() ? B.vw[(size_t)view].snap : none;
    }
    const Peer& peer(int view)
    {
        static const Peer none{};
        auto& B = in::bridge();
        return view >= 0 && view < B.views_n() ? B.vw[(size_t)view].peer.p : none;
    }
    int view_pid(int view)
    {
        auto& B = in::bridge();
        return view >= 0 && view < B.views_n() ? B.view_pid(view) : -1;
    }
    int view_of_pid(int pid)
    {
        auto& B = in::bridge();
        for (int v = 0; v < B.views_n(); ++v)
            if (B.view_pid(v) == pid) return v;
        return -1;
    }
    int my_pid()
    {
        if (is_host()) return 0;
        auto& B = in::bridge();
        return B.ros.on && B.ros.you > 0 ? B.ros.you : 1;
    }
    void each_view(const std::function<void(int view, const Snap& s, const Peer& p)>& fn)
    {
        auto& B = in::bridge();
        int nv = B.views_n();
        for (int v = 0; v < nv; ++v) fn(v, B.vw[(size_t)v].snap, B.vw[(size_t)v].peer.p);
    }
    bool own_tag_view(Mod m, std::string_view tag, ProducerV p)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        auto taken = [&](Mod by)
        {
            log("NOLA x0 bridge: tag " + std::string(tag) + " already owned by " + mod_name(by) + "; " + mod_name(m) + " refused");
            return false;
        };
        for (auto& o : B.own) if (o.tag == tag) return taken(o.m);
        for (auto& o : B.ownv) if (o.tag == tag) return taken(o.m);
        in::Bridge::OwnV o{m, std::string(tag), std::move(p)};
        B.ownv.push_back(std::move(o));
        return true;
    }
    bool own_tag_hub(Mod m, std::string_view tag, ProducerV p)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        for (auto& o : B.own)
            if (o.tag == tag)
            {
                if (o.m != m || o.pv)
                {
                    log("NOLA x0 bridge: hub producer for tag " + std::string(tag) + " refused (" + mod_name(m) + ")");
                    return false;
                }
                o.pv = std::move(p);
                return true;
            }
        log("NOLA x0 bridge: hub producer for tag " + std::string(tag) + " refused: no own_tag (" + mod_name(m) + ")");
        return false;
    }
    void on_tag_view(Mod m, std::string_view tag, ConsumerV c)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        B.consv.push_back({m, std::string(tag), std::move(c)});
    }
    void on_snap_view(Mod m, std::function<void(const Snap& s, int view)> c)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        B.snapcbsv.push_back({m, std::move(c)});
    }
    void on_view_session(Mod m, std::function<void(int view, const Peer& p)> c)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        B.sesscbs.push_back({m, std::move(c)});
    }
    const std::string& my_nonce() { return in::bridge().nonce; }
    bool in_world(double x, double y, double z)
    {
        if (fi("kzsafe", 0) != 1) return true;
        if (x != x || y != y || z != z) return false;
        double xy = fd("kzxy", 5000000);
        if (std::fabs(x) > xy || std::fabs(y) > xy) return false;
        return z >= fd("killz", -20000) && z <= fd("kztop", 1000000);
    }
    bool own_tag(Mod m, std::string_view tag, Producer p)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        for (auto& o : B.own)
            if (o.tag == tag)
            {
                log("NOLA x0 bridge: tag " + std::string(tag) + " already owned by " + mod_name(o.m) + "; " + mod_name(m) + " refused");
                return false;
            }
        for (auto& o : B.ownv)
            if (o.tag == tag)
            {
                log("NOLA x0 bridge: tag " + std::string(tag) + " already owned by " + mod_name(o.m) + "; " + mod_name(m) + " refused");
                return false;
            }
        B.own.push_back({m, std::string(tag), std::move(p), std::string(), false});
        return true;
    }
    void on_tag(Mod m, std::string_view tag, Consumer c)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        B.cons.push_back({m, std::string(tag), std::move(c)});
    }
    void on_snap(Mod m, std::function<void(const Snap& s)> c)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        B.snapcbs.push_back({m, std::move(c)});
    }
    std::string look_put(const std::string& text)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        double t = now();
        if (B.put_text != text || B.put_h.empty())
        {
            B.put_text = text;
            B.put_h = in::hash_hex(std::string("@x\x01") + text);
        }
        auto& a = B.lapps[B.put_h];
        if (a.h.empty())
        {
            a.h = B.put_h;
            a.text = text;
            a.js = "{\"a\":\"" + a.h + "\",\"mesh\":\"@x\",\"parts\":\"" + text + "\",\"mats\":\"\",\"statics\":\"\"}";
        }
        a.used = t;
        return B.put_h;
    }
    bool look_get(const std::string& hash, std::string& text)
    {
        auto& B = in::bridge();
        std::lock_guard<std::recursive_mutex> g(B.mu);
        auto it = B.rapps.find(hash);
        if (it != B.rapps.end()) { it->second.at = now(); text = it->second.parts; return true; }
        B.need[hash] = now();
        return false;
    }
}

namespace x0::in
{
    namespace
    {
        std::string jstr(std::string_view s)
        {
            std::string o = "\"";
            for (char ch : s) o += ((unsigned char)ch < 32 || ch == 127 || ch == '"' || ch == 92) ? '_' : ch;
            return o + "\"";
        }
        bool head_num(const std::string& h, const char* k, double& v)
        {
            std::string key = std::string("\"") + k + "\":";
            size_t p = h.find(key);
            if (p == std::string::npos) return false;
            const char* s = h.c_str() + p + key.size();
            char* e = nullptr;
            v = std::strtod(s, &e);
            return e != s;
        }
        std::string fv(std::string_view k) { return flag(k).str(); }
    }

    std::string state_json(bool in_world, const double* hp, const double* mx, const bool* alive, const std::string* level, const bool* paused)
    {
        auto& B = bridge();
        std::string tm, perr;
        {
            std::lock_guard<std::recursive_mutex> g(B.mu);
            if (auto* o = B.pseudo("@time")) tm = o->cache;
            perr = B.proto_err;
        }
        double gt = -1, wt = -1, wx = -1;
        head_num(tm, "gt", gt);
        head_num(tm, "wt", wt);
        head_num(tm, "wx", wx);
        char b[64];
        auto f1 = [&](const double* v) -> std::string { if (!v) return "null"; std::snprintf(b, sizeof b, "%.1f", *v); return b; };
        auto f0 = [&](double v) -> std::string { if (v < 0) return "null"; std::snprintf(b, sizeof b, "%.0f", v); return b; };
        std::string o = "{\"t\":" + std::to_string((long long)(wall_ms() / 1000));
        o += ",\"role\":" + jstr(role());
        o += std::string(",\"in_world\":") + (in_world ? "true" : "false");
        o += std::string(",\"dead\":") + (alive ? (*alive ? "false" : "true") : "null");
        o += ",\"health\":" + f1(hp) + ",\"max_health\":" + f1(mx);
        o += ",\"level\":" + (level ? jstr(*level) : std::string("null"));
        o += std::string(",\"paused\":") + (paused ? (*paused ? "true" : "false") : "null");
        o += ",\"mod_ver\":" + jstr("v37-b53-cpp");
        o += ",\"autowalk\":" + fv("autowalk") + ",\"god\":" + fv("playergod") + ",\"god_methods\":" + jstr("-") + ",\"god_layers\":" + jstr(fv("godlayers"));
        o += ",\"pawns\":null,\"npcs_scan\":null,\"mirrors\":null,\"mirror_errs\":null";
        o += ",\"game_tod\":" + f0(gt) + ",\"weather_tod\":" + f0(wt) + ",\"weather\":" + (wx < 0 ? std::string("null") : std::to_string((long long)wx));
        o += ",\"godtest\":null,\"proto_err\":" + (perr.empty() ? std::string("null") : jstr(perr)) + "}";
        return o;
    }
}
