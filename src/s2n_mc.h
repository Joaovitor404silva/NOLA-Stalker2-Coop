#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace s2nmc
{
    constexpr int kViews = 4;

    struct Pt
    {
        double x = 0, y = 0;
    };

    inline double d2(const Pt& c, double x, double y) { return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y); }

    inline double near2(const std::vector<Pt>& cs, double x, double y, int* at = nullptr)
    {
        double b = 1e300;
        int bi = -1;
        for (int i = 0; i < (int)cs.size(); ++i)
        {
            double d = d2(cs[(size_t)i], x, y);
            if (d < b) { b = d; bi = i; }
        }
        if (at) *at = bi;
        return b;
    }

    struct Item
    {
        uintptr_t key = 0;
        double x = 0, y = 0;
    };

    struct Pick
    {
        std::vector<std::vector<uintptr_t>> per;
        std::vector<uintptr_t> all;
        std::vector<double> d2;
        std::vector<int> own;
    };

    inline Pick pick(const std::vector<Pt>& cs, const std::vector<Item>& items, double R, double hys, int max,
                     const std::vector<std::unordered_set<uintptr_t>>& prev, const std::function<bool(uintptr_t)>& drop,
                     const std::unordered_set<uintptr_t>* free = nullptr, const Pt* also = nullptr)
    {
        Pick out;
        out.per.resize(cs.size());
        const double hy = (std::max)(1.0, hys), R2 = R * R, Rh = R * hy, Rh2 = Rh * Rh;
        std::unordered_set<uintptr_t> inAll;
        for (size_t c = 0; c < cs.size(); ++c)
        {
            struct Cand { uintptr_t k; double rk; };
            std::vector<Cand> v;
            const std::unordered_set<uintptr_t>* pv = c < prev.size() ? &prev[c] : nullptr;
            for (const Item& it : items)
            {
                double d = d2(cs[c], it.x, it.y);
                if (also) d = (std::min)(d, d2(*also, it.x, it.y));
                bool was = pv && pv->count(it.key) > 0;
                if (d > (was ? Rh2 : R2)) continue;
                v.push_back({it.key, was ? d / (hy * hy) : d});
            }
            std::sort(v.begin(), v.end(), [](const Cand& a, const Cand& b) { return a.rk < b.rk; });
            int capped = 0;
            for (const Cand& k : v)
            {
                bool fr = free && free->count(k.k) > 0;
                if (!fr && capped >= max) continue;
                if (drop && drop(k.k)) continue;
                if (!fr) ++capped;
                out.per[c].push_back(k.k);
                if (inAll.insert(k.k).second) out.all.push_back(k.k);
            }
        }
        for (uintptr_t k : out.all)
        {
            double b = 1e300;
            int at = -1;
            for (const Item& it : items)
                if (it.key == k) { b = near2(cs, it.x, it.y, &at); break; }
            out.d2.push_back(b);
            out.own.push_back(at);
        }
        return out;
    }

    template <class Row, class Live>
    int vend_rows(const Row* rows, size_t n, const std::vector<Live>& live, const std::vector<Pt>& cs, double R, double vendR, std::unordered_set<decltype(Live::o)>& out)
    {
        out.clear();
        int cnt = 0;
        const double vr2 = vendR * vendR, R2 = (R + vendR) * (R + vendR);
        for (size_t i = 0; i < n; ++i)
        {
            const Row& v = rows[i];
            if (near2(cs, v.x, v.y) > R2) continue;
            ++cnt;
            decltype(Live::o) best{};
            double bd = vr2;
            for (const Live& p : live)
            {
                double d = (p.x - v.x) * (p.x - v.x) + (p.y - v.y) * (p.y - v.y);
                if (d < bd) { bd = d; best = p.o; }
            }
            if (best) out.insert(best);
        }
        return cnt;
    }

    inline bool qg_ok(const std::string& h)
    {
        if (h.size() != 32) return false;
        bool any = false;
        for (char c : h)
        {
            bool hx = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
            if (!hx) return false;
            if (c != '0') any = true;
        }
        return any;
    }

    inline std::string qg_hex(const int32_t g[4])
    {
        if (!g[0] && !g[1] && !g[2] && !g[3]) return std::string();
        char b[40];
        std::snprintf(b, sizeof b, "%08X%08X%08X%08X", (uint32_t)g[0], (uint32_t)g[1], (uint32_t)g[2], (uint32_t)g[3]);
        return b;
    }

    inline bool qg_parse(const std::string& h, int32_t g[4])
    {
        if (!qg_ok(h)) return false;
        for (int i = 0; i < 4; ++i) g[i] = (int32_t)(uint32_t)std::strtoul(h.substr((size_t)i * 8, 8).c_str(), nullptr, 16);
        return true;
    }

    inline std::string qg_up(std::string g)
    {
        for (char& c : g) if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
        return g;
    }

    inline std::string with_qg(const std::string& rec, const std::string& qg) { return qg_ok(qg) ? rec + "," + qg : rec; }

    inline std::string qg_field(const std::string& rec, int idx = 21)
    {
        size_t p = 0;
        for (int i = 0; i < idx; ++i)
        {
            p = rec.find(',', p);
            if (p == std::string::npos) return std::string();
            ++p;
        }
        size_t e = rec.find(',', p);
        std::string f = rec.substr(p, e == std::string::npos ? std::string::npos : e - p);
        return qg_ok(f) ? qg_up(f) : std::string();
    }

    inline void nu_add(std::string& nu, int32_t uid, const std::string& qg)
    {
        if (!nu.empty()) nu += ",";
        nu += std::to_string(uid);
        if (qg_ok(qg)) { nu += "/"; nu += qg; }
    }

    template <class F>
    inline void nu_each(const std::string& e, size_t from, F f)
    {
        for (size_t q = from; q < e.size();)
        {
            size_t c = e.find(',', q);
            if (c == std::string::npos) c = e.size();
            std::string it = e.substr(q, c - q);
            size_t k = it.find('/');
            int32_t u = (int32_t)std::strtol(it.c_str(), nullptr, 10);
            std::string g = k == std::string::npos ? std::string() : it.substr(k + 1);
            f(u, qg_ok(g) ? qg_up(g) : std::string());
            q = c + 1;
        }
    }

    template <class MU, class MQ, class UidOf, class OtherRow>
    inline typename MU::mapped_type local_pick(int32_t uid, const std::string& qg, bool on, const MU& byUid, const MQ& byQg, UidOf uidOf, OtherRow otherRow, bool& viaQg)
    {
        viaQg = false;
        if (uid)
        {
            auto it = byUid.find(uid);
            if (it != byUid.end() && it->second) return it->second;
        }
        if (!on || !qg_ok(qg)) return typename MU::mapped_type{};
        auto q = byQg.find(qg);
        if (q == byQg.end() || !q->second) return typename MU::mapped_type{};
        int32_t lu = uidOf(q->second);
        if (lu && lu != uid && otherRow(lu)) return typename MU::mapped_type{};
        viaQg = true;
        return q->second;
    }

    struct Chunk
    {
        bool ok = false;
        int32_t uid = 0;
        bool fixed = false;
        bool noData = false;
        const std::string* look = nullptr;
        std::string rec, mm, bb, mre;
    };

    struct ViewSt
    {
        size_t rr = 0;
        double lookRR = -1e300;
        size_t mrr = 0;
    };

    struct LookCfg
    {
        int n = 0;
        int max = 0;
        double period = 0;
    };

    struct Out
    {
        int n = 0;
        int looks = 0;
        size_t mBytes = 0;
        int mRe = 0;
    };

    inline double sec_at(const float* st, int n, double len, double pos, double at, const std::vector<int>& nx, int& how, double tol = 1e-4)
    {
        how = 0;
        if (n <= 0 || len <= 0) return at;
        auto end_of = [&](int i) { double e = len; for (int j = 0; j < n; ++j) if (st[j] > st[i] + 1e-4f && st[j] < e) e = st[j]; return e; };
        int cur = -1;
        for (int i = 0; i < n; ++i) if (st[i] <= pos + tol && (cur < 0 || st[i] > st[cur])) cur = i;
        if (cur < 0 || (int)nx.size() != n) { if (at >= len) how = 2; return std::min(at, len - 0.01); }
        for (int s = 0; s < 16; ++s)
        {
            double e = end_of(cur);
            if (at < e) return at;
            int nxt = nx[cur];
            if (nxt < 0 || nxt >= n) { how = 2; return std::max((double)st[cur], e - 0.01); }
            double over = at - e;
            if (nxt == cur)
            {
                double sl = e - st[cur];
                how = 1;
                return sl > 0.01 ? st[cur] + std::fmod(over, sl) : st[cur];
            }
            at = st[nxt] + over;
            cur = nxt;
        }
        how = 2;
        return std::min(at, len - 0.01);
    }

    inline double r_tok_of(const std::string& e) { return !e.empty() && e[0] == 'r' ? std::atof(e.c_str() + 1) : 0.0; }

    inline bool hdr_has_r(const std::string& hdr)
    {
        for (size_t i = 0; i <= hdr.size();)
        {
            size_t j = hdr.find(';', i);
            if (j == std::string::npos) j = hdr.size();
            if (r_tok_of(hdr.substr(i, j - i)) > 0) return true;
            i = j + 1;
        }
        return false;
    }

    struct LostSeen
    {
        int gen = -1;
        std::unordered_set<int32_t> u;
        void add(int g, int32_t id)
        {
            if (gen != g) { u.clear(); gen = g; }
            u.insert(id);
        }
        bool has(int g, int32_t id) const { return gen == g && u.count(id) > 0; }
    };

    inline std::string lost_tok(const std::string& lost) { return lost.empty() ? std::string() : "l" + lost; }

    inline std::string join_tok(const std::string& a, const std::string& b)
    {
        if (a.empty()) return b;
        return b.empty() ? a : a + ";" + b;
    }

    inline std::string assemble(const std::string& tms, const std::vector<const Chunk*>& ord, const std::vector<int*>& lookSent, ViewSt& vs, const LookCfg& lc, double now,
                                const std::string& nu, const std::string& nd, Out& st, const std::string& hdr = std::string())
    {
        const std::string head = "#NN:t" + tms + ";" + (hdr.empty() ? std::string() : hdr + ";");
        std::string nn, nl, mm, bb;
        int n = 0;
        auto lookAdd = [&](const Chunk& c) {
            if (!nl.empty()) nl += ";";
            nl += std::to_string(c.uid) + "=" + *c.look;
            ++st.looks;
        };
        std::string lost;
        for (size_t i = 0; i < ord.size(); ++i)
        {
            const Chunk& c = *ord[i];
            if (!c.ok)
            {
                if (c.noData) nu_add(lost, c.uid, std::string());
                continue;
            }
            if (!c.mm.empty()) { if (!mm.empty()) mm += ";"; mm += c.mm; }
            if (!c.bb.empty()) { if (!bb.empty()) bb += ";"; bb += c.bb; }
            if (!nn.empty()) nn += ";";
            nn += c.rec;
            ++n;
            int& ls = *lookSent[i];
            if (!c.fixed && ls < lc.n && c.look && !c.look->empty() && (nl.empty() || (int)(nl.size() + c.look->size()) < lc.max))
            {
                lookAdd(c);
                ++ls;
            }
        }
        if (!ord.empty() && now - vs.lookRR >= lc.period)
        {
            vs.lookRR = now;
            vs.rr = (vs.rr + 1) % ord.size();
            const Chunk& c = *ord[vs.rr];
            if (!c.fixed && c.look && !c.look->empty() && *lookSent[vs.rr] >= lc.n && (nl.empty() || (int)(nl.size() + c.look->size()) < lc.max)) lookAdd(c);
        }
        for (size_t j = 0; j < ord.size(); ++j)
        {
            vs.mrr = (vs.mrr + 1) % ord.size();
            const Chunk& c = *ord[vs.mrr];
            if (!c.ok || c.mre.empty()) continue;
            if (!mm.empty()) mm += ";";
            mm += c.mre;
            ++st.mRe;
            break;
        }
        const std::string nuAll = join_tok(nu, lost_tok(lost));
        st.n += n;
        if (n == 0) return nuAll.empty() && !hdr_has_r(hdr) ? nd : head + nuAll + nd;
        if (!nuAll.empty()) nn += ";" + nuAll;
        if (!mm.empty()) { nn += ";" + mm; st.mBytes += mm.size(); }
        if (!bb.empty()) nn += ";" + bb;
        std::string out = head + nn;
        if (!nl.empty()) out += "#NL:" + nl;
        out += nd;
        return out;
    }

    struct FreshDead
    {
        std::unordered_map<uintptr_t, double> alive;
        std::unordered_set<uintptr_t> prev, cur, held;
        double prevAt = -1, aliveT = 10, gap = 2.5, jump2 = 3000.0 * 3000.0;
        int cap = 2, newN = 0, nAlive = 0, nNew = 0, fresh = 0, burst = 4, nAll = 0;
        bool scanOk = false, all = false;
        void begin(double now, double moved2)
        {
            scanOk = prevAt >= 0 && now - prevAt <= gap && moved2 <= jump2;
            newN = 0;
            fresh = 0;
            cur.clear();
        }
        void seen(uintptr_t k, bool al, double now)
        {
            if (cur.insert(k).second && !prev.count(k)) ++fresh;
            if (al) alive[k] = now;
        }
        bool dead_ok(uintptr_t k, double now, bool quest, bool inR = true)
        {
            if (!inR || held.count(k) || newN >= cap) return false;
            auto it = alive.find(k);
            if (it != alive.end() && now - it->second <= aliveT) { ++nAlive; ++newN; held.insert(k); return true; }
            if (quest) return false;
            if (all) { ++newN; ++nAll; held.insert(k); return true; }
            if (!scanOk || prev.count(k) || fresh > burst) return false;
            ++newN; ++nNew;
            held.insert(k);
            return true;
        }
        void left(uintptr_t k, bool aged)
        {
            if (!all) return;
            if (aged) held.insert(k); else held.erase(k);
        }
        void end(double now)
        {
            for (auto it = held.begin(); it != held.end();)
                if (!cur.count(*it)) it = held.erase(it); else ++it;
            prev.swap(cur);
            cur.clear();
            prevAt = now;
            for (auto it = alive.begin(); it != alive.end();)
                if (now - it->second > aliveT) it = alive.erase(it); else ++it;
        }
        void reset() { alive.clear(); prev.clear(); cur.clear(); held.clear(); fresh = 0; prevAt = -1; scanOk = false; newN = 0; }
    };

    inline bool dead_row_copy(bool on, bool rowAlive, bool hadBody) { return rowAlive || (on && !hadBody); }

    struct HotDead { int32_t uid = 0, luid = 0, idx = -1, serial = 0; uint64_t raw = 0; };

    inline bool hot_dead_hand(bool on, bool dead, int how, bool bodyValid) { return on && dead && how == 2 && bodyValid; }

    inline std::string hot_dead_pack(const std::vector<HotDead>& v)
    {
        std::string s = "v1";
        for (const HotDead& h : v)
            s += "\n" + std::to_string(h.uid) + "\t" + std::to_string(h.luid) + "\t" + std::to_string(h.idx) + "\t" + std::to_string(h.serial) + "\t" + std::to_string(h.raw);
        return s;
    }

    inline std::vector<HotDead> hot_dead_unpack(const std::string& s)
    {
        std::vector<HotDead> out;
        if (s.compare(0, 2, "v1") != 0 || (s.size() > 2 && s[2] != '\n')) return out;
        size_t a = s.find('\n');
        while (a != std::string::npos)
        {
            size_t e = s.find('\n', a + 1);
            std::string l = s.substr(a + 1, e == std::string::npos ? std::string::npos : e - a - 1);
            a = e;
            long long f[5];
            size_t p = 0;
            bool ok = true;
            for (int i = 0; i < 5 && ok; ++i)
            {
                size_t t = l.find('\t', p);
                std::string w = l.substr(p, t == std::string::npos ? std::string::npos : t - p);
                ok = !w.empty() && w.find_first_not_of("-0123456789") == std::string::npos && (i == 4 ? t == std::string::npos : t != std::string::npos);
                if (ok) f[i] = std::stoll(w);
                p = t == std::string::npos ? l.size() : t + 1;
            }
            if (!ok) continue;
            HotDead h;
            h.uid = (int32_t)f[0]; h.luid = (int32_t)f[1]; h.idx = (int32_t)f[2]; h.serial = (int32_t)f[3]; h.raw = (uint64_t)f[4];
            out.push_back(h);
        }
        return out;
    }

    inline bool hot_dead_due(bool taken, double takenAt, double now) { return taken && now - takenAt > 30.0; }

    inline int dead_keep(bool on, const std::vector<std::pair<bool, uint64_t>>& v, std::vector<uint64_t>& out)
    {
        if (!on) return 0;
        int n = 0;
        for (auto& [dead, a] : v)
            if (dead && a) { out.push_back(a); ++n; }
        return n;
    }

    inline double r_tok(const std::string& e) { return !e.empty() && e[0] == 'r' ? std::atof(e.c_str() + 1) : 0.0; }

    inline int hp_scale_tok(const std::string& e)
    {
        if (e.size() != 2 || e[0] != 'h' || e[1] < '1' || e[1] > '6') return 0;
        int s = 1;
        for (int i = 0; i < e[1] - '0'; ++i) s *= 10;
        return s;
    }

    inline int hp_scale_of(const std::string& hdr)
    {
        int s = 100;
        for (size_t i = 0; i <= hdr.size();)
        {
            size_t j = hdr.find(';', i);
            if (j == std::string::npos) j = hdr.size();
            if (int t = hp_scale_tok(hdr.substr(i, j - i))) s = t;
            i = j + 1;
        }
        return s;
    }

    inline int hp_wire(float hp, float mh, bool scaled)
    {
        if (mh <= 0) return 0;
        const float f = std::clamp(hp / mh, 0.f, 1.f);
        return scaled ? (int)std::ceil(f * 10000.0) : (int)std::lround(f * 100);
    }

    inline double hp_want(int hp, int scale, double mh)
    {
        const double s = scale > 0 ? scale : 100;
        const double v = (std::min)(s, (double)hp) / s * mh - mh / (2 * s);
        return (std::max)(1.0, std::ceil(v));
    }

    inline int hp_pct(int hp, int scale) { return scale > 0 ? (int)((int64_t)hp * 100 / scale) : hp; }

    inline double radius_pick(bool ok, double r, double k, double fallback, double& lastGood)
    {
        if (ok) { lastGood = r * k; return lastGood; }
        return lastGood > 0 ? lastGood : fallback;
    }

    inline bool cli_gone(bool rhost, bool full, int gen, int nnGen, bool listed) { return rhost && full && gen < nnGen && !listed; }

    inline double far_keep_r(bool dead, double liveR, double deadR) { return dead ? deadR : liveR * 0.9; }

    inline bool far_out(bool on, double dist, double r) { return on && dist >= 0 && dist > r; }

    inline bool pick_garbage(bool garbage, bool unreachable) { return garbage || unreachable; }

    inline bool dead_corpse_ok(bool on, float distCm, float halfHeightCm) { return !on || distCm <= 4.f * halfHeightCm; }

    inline bool released_dead_visible(int how, bool dead) { return !dead; }

    inline Pt dead_row_pos(bool dead, const Pt& actor, const Pt& pelvis, bool pelvisOk) { return (dead && pelvisOk) ? pelvis : actor; }

    inline bool far_had_body(bool on, bool hadBody, bool farLost) { return hadBody && !(on && farLost); }

    inline bool host_dead_row(bool on, bool dead, double sinceDead, double natdead) { return !dead || on || sinceDead <= natdead; }

    inline bool host_row_drop(bool on, bool dead, double sinceDead, double natdead) { return !host_dead_row(on, dead, sinceDead, natdead); }
}
