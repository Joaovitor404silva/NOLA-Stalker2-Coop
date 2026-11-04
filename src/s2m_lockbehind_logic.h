#pragma once
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lockbehind
{
    constexpr int kHost = -1;

    struct Row
    {
        std::string quest, trig, node, guid;
    };

    struct Table
    {
        std::unordered_map<std::string, Row> byNode;
        std::set<std::string> trigs;
        size_t multi = 0;
    };

    struct Vol
    {
        double x = 0, y = 0, z = 0, yaw = 0, sx = 100, sy = 100, sz = 100;
        bool box = true;
    };

    struct Pend
    {
        std::string node, trig, quest;
        double at = 0;
    };

    struct Gate
    {
        std::map<std::string, std::set<int>> entered;
        std::vector<Pend> pend;
    };

    inline std::string lower(std::string_view s)
    {
        std::string o(s);
        for (auto& c : o)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        return o;
    }

    inline std::vector<std::string> cols(std::string_view ln)
    {
        std::vector<std::string> out;
        size_t p = 0;
        while (true)
        {
            size_t q = ln.find('\t', p);
            out.emplace_back(ln.substr(p, q == std::string_view::npos ? std::string_view::npos : q - p));
            if (q == std::string_view::npos) break;
            p = q + 1;
        }
        if (!out.empty() && !out.back().empty() && out.back().back() == '\r') out.back().pop_back();
        return out;
    }

    template <class F>
    inline void each_line(std::string_view text, F fn)
    {
        size_t p = 0;
        while (p < text.size())
        {
            size_t q = text.find('\n', p);
            std::string_view ln = text.substr(p, q == std::string_view::npos ? std::string_view::npos : q - p);
            if (!ln.empty()) fn(ln);
            if (q == std::string_view::npos) break;
            p = q + 1;
        }
    }

    inline int parse(std::string_view text, Table& t)
    {
        std::set<std::string> multi;
        each_line(text, [&](std::string_view ln) {
            auto c = cols(ln);
            if (c.size() < 7 || c[5] != "close" || c[6] != "1" || c[3].size() != 32) return;
            Row r{c[0], c[1], c[2], c[3]};
            std::string k = lower(r.node);
            if (multi.count(k)) return;
            auto it = t.byNode.find(k);
            if (it != t.byNode.end() && it->second.trig != r.trig)
            {
                multi.insert(k);
                t.byNode.erase(it);
                return;
            }
            t.byNode[k] = std::move(r);
        });
        t.multi = multi.size();
        t.trigs.clear();
        for (auto& kv : t.byNode) t.trigs.insert(kv.second.trig);
        return static_cast<int>(t.byNode.size());
    }

    inline bool num(const std::string& s, double& v)
    {
        if (s.empty()) return false;
        char* e = nullptr;
        v = std::strtod(s.c_str(), &e);
        return e && *e == 0;
    }

    inline int parse_vols(std::string_view trigTsv, const std::set<std::string>& trigs, std::unordered_map<std::string, Vol>& out)
    {
        int n = 0;
        each_line(trigTsv, [&](std::string_view ln) {
            auto c = cols(ln);
            if (c.size() < 5 || !trigs.count(c[0])) return;
            Vol v;
            double d;
            if (!num(c[2], v.x) || !num(c[3], v.y) || !num(c[4], v.z)) return;
            if (c.size() >= 6 && num(c[5], d)) v.yaw = d;
            if (c.size() >= 7 && num(c[6], d)) v.sx = d;
            if (c.size() >= 8 && num(c[7], d)) v.sy = d;
            if (c.size() >= 9 && num(c[8], d)) v.sz = d;
            v.box = c.size() < 10 || c[9] == "Box";
            out[c[0]] = v;
            ++n;
        });
        return n;
    }

    inline bool inside(const Vol& r, double px, double py, double pz, double m)
    {
        double dx = px - r.x, dy = py - r.y, dz = pz - r.z;
        if (!r.box) return dx * dx + dy * dy < (r.sx + m) * (r.sx + m) && std::fabs(dz) < r.sz + m + 100;
        double a = -(r.yaw * 3.14159265358979323846 / 180.0);
        double c = std::cos(a), s = std::sin(a);
        double lx = dx * c - dy * s, ly = dx * s + dy * c;
        return std::fabs(lx) <= r.sx + m && std::fabs(ly) <= r.sy + m && std::fabs(dz) <= r.sz + m + 100;
    }

    inline void mark(Gate& g, const std::string& trig, int view) { g.entered[trig].insert(view); }

    struct Pos
    {
        int view;
        double x, y, z;
    };

    using Vols = std::unordered_map<std::string, Vol>;

    inline std::set<int> in_now(const Vols& vols, const std::string& trig, const std::vector<Pos>& pos, double m)
    {
        std::set<int> out;
        auto it = vols.find(trig);
        if (it == vols.end()) return out;
        for (auto& p : pos)
            if (inside(it->second, p.x, p.y, p.z, m)) out.insert(p.view);
        return out;
    }

    inline bool pending(const Gate& g, const std::string& trig)
    {
        for (auto& p : g.pend)
            if (p.trig == trig) return true;
        return false;
    }

    inline void sample(Gate& g, const Vols& vols, const std::vector<Pos>& pos, double m)
    {
        std::set<std::string> trigs;
        for (auto& p : g.pend) trigs.insert(p.trig);
        for (auto& t : trigs)
            for (int v : in_now(vols, t, pos, m)) mark(g, t, v);
    }

    inline bool live_peer(bool loopback, bool up, bool silent, bool left) { return !loopback && (up || (silent && !left)); }

    inline void prune(Gate& g, const std::vector<int>& live)
    {
        for (auto& e : g.entered)
            for (auto it = e.second.begin(); it != e.second.end();)
            {
                bool up = false;
                for (int v : live) up = up || v == *it;
                it = up ? std::next(it) : e.second.erase(it);
            }
    }

    inline int count_in(const Gate& g, const std::string& trig, const std::vector<int>& live)
    {
        auto it = g.entered.find(trig);
        if (it == g.entered.end()) return 0;
        int n = 0;
        for (int v : live) n += it->second.count(v) ? 1 : 0;
        return n;
    }

    inline bool all_in(const Gate& g, const std::string& trig, const std::vector<int>& live) { return count_in(g, trig, live) == static_cast<int>(live.size()); }

    inline const Row* row_of(const Table& t, std::string_view nodeLower, bool& copy)
    {
        std::string key(nodeLower);
        copy = key.size() > 4 && key.compare(key.size() - 4, 4, "_s2q") == 0;
        if (copy) key.resize(key.size() - 4);
        auto it = t.byNode.find(key);
        return it == t.byNode.end() ? nullptr : &it->second;
    }

    inline const Row* hold(const Table& t, Gate& g, std::string_view nodeLower, const std::vector<int>& live, double now, bool& fresh, const Vols& vols, const std::vector<Pos>& pos, double m)
    {
        fresh = false;
        bool copy = false;
        const Row* rp = row_of(t, nodeLower, copy);
        if (!rp) return nullptr;
        const Row& r = *rp;
        if (live.size() <= 1) return nullptr;
        const bool was = pending(g, r.trig);
        if (!was) g.entered[r.trig] = in_now(vols, r.trig, pos, m);
        else
            for (int v : in_now(vols, r.trig, pos, m)) mark(g, r.trig, v);
        if (all_in(g, r.trig, live))
        {
            if (!was) g.entered.erase(r.trig);
            return nullptr;
        }
        const std::string name = copy ? r.node + "_S2Q" : r.node;
        for (auto& p : g.pend)
            if (p.node == name) return &r;
        g.pend.push_back({name, r.trig, r.quest, now});
        fresh = true;
        return &r;
    }

    inline std::vector<Pend> release(Gate& g, const std::vector<int>& live)
    {
        std::vector<Pend> out, keep;
        for (auto& p : g.pend)
        {
            if (all_in(g, p.trig, live)) out.push_back(p);
            else keep.push_back(p);
        }
        g.pend = keep;
        for (auto& p : out) g.entered.erase(p.trig);
        return out;
    }

    inline std::vector<Pend> drain(Gate& g)
    {
        std::vector<Pend> out;
        out.swap(g.pend);
        g.entered.clear();
        return out;
    }
}
