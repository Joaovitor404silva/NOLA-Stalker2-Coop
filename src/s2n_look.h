#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace s2nlook
{
    inline std::string hid_txt(const std::vector<uint32_t>& h)
    {
        std::string s;
        char b[12];
        for (size_t i = 0; i < h.size(); ++i) { if (i) s += ","; snprintf(b, sizeof b, "%x", (unsigned)h[i]); s += b; }
        return s;
    }
    inline std::string cpd_txt(const std::vector<float>& v)
    {
        std::string s;
        char b[32];
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (i) s += ",";
            snprintf(b, sizeof b, "%.6g", (double)v[i]);
            for (const char* p = b; *p; ++p) if (*p != '+') s += *p;
        }
        return s;
    }
    inline void hid_parse(const std::string& s, std::vector<uint32_t>& out)
    {
        out.clear();
        if (s.empty()) return;
        size_t i = 0;
        while (i <= s.size() && out.size() < 16)
        {
            size_t j = s.find(',', i);
            if (j == std::string::npos) j = s.size();
            out.push_back((uint32_t)std::strtoul(s.substr(i, j - i).c_str(), nullptr, 16));
            i = j + 1;
            if (j >= s.size()) break;
        }
    }
    inline void cpd_parse(const std::string& s, std::vector<float>& out)
    {
        out.clear();
        if (s.empty()) return;
        size_t i = 0;
        while (i <= s.size() && out.size() < 32)
        {
            size_t j = s.find(',', i);
            if (j == std::string::npos) j = s.size();
            out.push_back(std::strtof(s.substr(i, j - i).c_str(), nullptr));
            i = j + 1;
            if (j >= s.size()) break;
        }
    }
    inline void hid_trim(std::vector<uint32_t>& h) { while (!h.empty() && h.back() == 0) h.pop_back(); }
    inline bool cpd_same(float a, float b)
    {
        if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
        float m = std::fabs(a) > 1.f ? std::fabs(a) : 1.f;
        return std::fabs(a - b) <= 1e-4f * m;
    }
    inline int hid_dif(const std::vector<uint32_t>& h, const std::vector<uint32_t>& c, int nlod)
    {
        size_t n = h.size() > c.size() ? h.size() : c.size();
        if (nlod >= 0 && (size_t)nlod < n) n = (size_t)nlod;
        for (size_t l = 0; l < n; ++l)
        {
            uint32_t a = l < h.size() ? h[l] : 0u, b = l < c.size() ? c[l] : 0u;
            if (a != b) return (int)l;
        }
        return -1;
    }
    inline int cpd_dif(const std::vector<float>& h, const std::vector<float>& c)
    {
        for (size_t i = 0; i < h.size(); ++i)
            if (i >= c.size() || !cpd_same(h[i], c[i])) return (int)i;
        return -1;
    }
    struct Sc { int slot = 0; std::string name; float v = 0.f; };
    inline bool sc_name_ok(const std::string& n)
    {
        if (n.empty() || n.size() > 48) return false;
        for (char c : n) if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_')) return false;
        return true;
    }
    inline bool sc_name_gore(const std::string& n)
    {
        std::string l = n;
        for (char& c : l) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
        return l.find("damage") != std::string::npos || l.find("blood") != std::string::npos || l.find("gore") != std::string::npos || l.find("wound") != std::string::npos;
    }
    inline std::string sc_txt(const std::vector<Sc>& v)
    {
        std::string s;
        char b[32];
        for (const Sc& e : v)
        {
            if (!sc_name_ok(e.name)) continue;
            if (!s.empty()) s += ",";
            snprintf(b, sizeof b, "%d:", e.slot);
            s += b;
            s += e.name;
            snprintf(b, sizeof b, ":%.6g", (double)e.v);
            for (const char* p = b; *p; ++p) if (*p != '+') s += *p;
        }
        return s;
    }
    inline void sc_parse(const std::string& s, std::vector<Sc>& out)
    {
        out.clear();
        size_t i = 0;
        while (i < s.size() && out.size() < 64)
        {
            size_t j = s.find(',', i);
            if (j == std::string::npos) j = s.size();
            std::string e = s.substr(i, j - i);
            i = j + 1;
            size_t a = e.find(':'), b = a == std::string::npos ? a : e.find(':', a + 1);
            if (b == std::string::npos) continue;
            Sc c;
            c.slot = (int)std::strtol(e.substr(0, a).c_str(), nullptr, 10);
            c.name = e.substr(a + 1, b - a - 1);
            c.v = std::strtof(e.substr(b + 1).c_str(), nullptr);
            if (c.slot >= 0 && c.slot < 8 && sc_name_ok(c.name)) out.push_back(std::move(c));
        }
    }
    inline int sc_dif(const std::vector<Sc>& h, const std::vector<Sc>& c)
    {
        for (size_t i = 0; i < h.size(); ++i)
        {
            bool ok = false;
            for (const Sc& e : c) if (e.slot == h[i].slot && e.name == h[i].name) { ok = cpd_same(h[i].v, e.v); break; }
            if (!ok) return (int)i;
        }
        return -1;
    }
    struct Tail { bool vis = true, ext = false; std::vector<uint32_t> hid; std::vector<float> cpd; std::vector<Sc> sc; };
    inline Tail tail_parse(const std::string& t)
    {
        Tail r;
        size_t d = t.find('@');
        r.vis = t.substr(0, d) != "0";
        if (d == std::string::npos) return r;
        r.ext = true;
        size_t f = t.find('@', d + 1);
        hid_parse(t.substr(d + 1, f == std::string::npos ? std::string::npos : f - d - 1), r.hid);
        if (f != std::string::npos)
        {
            size_t g = t.find('@', f + 1);
            cpd_parse(t.substr(f + 1, g == std::string::npos ? std::string::npos : g - f - 1), r.cpd);
            if (g != std::string::npos) sc_parse(t.substr(g + 1), r.sc);
        }
        return r;
    }
    inline std::string tail_txt(bool vis, bool ext, const std::vector<uint32_t>& hid, const std::vector<float>& cpd, const std::vector<Sc>& sc = {})
    {
        std::string s = vis ? "1" : "0";
        if (ext)
        {
            s += "@" + hid_txt(hid) + "@" + cpd_txt(cpd);
            std::string t = sc_txt(sc);
            if (!t.empty()) s += "@" + t;
        }
        return s;
    }
    template <class P, class Special, class Compat, class Pri>
    inline std::vector<int> pair_parts(const std::vector<P>& host, const std::vector<P>& cl, bool assign, bool natg, Special special, Compat compat, Pri pri)
    {
        std::vector<int> m(host.size(), -1);
        std::vector<char> used(cl.size(), 0);
        for (size_t i = 0; i < host.size(); ++i)
            for (size_t j = 0; j < cl.size(); ++j)
                if (!used[j] && cl[j].key == host[i].key && (special(host[i].key) || (compat(i, j) && (!natg || cl[j].made || cl[j].mesh == host[i].mesh)))) { m[i] = (int)j; used[j] = 1; break; }
        for (size_t i = 0; i < host.size(); ++i)
        {
            if (m[i] >= 0 || special(host[i].key) || host[i].mesh.empty()) continue;
            for (size_t j = 0; j < cl.size(); ++j)
                if (!used[j] && !special(cl[j].key) && cl[j].mesh == host[i].mesh && compat(i, j)) { m[i] = (int)j; used[j] = 1; break; }
        }
        if (assign && !natg)
        {
            std::vector<size_t> ord;
            for (size_t i = 0; i < host.size(); ++i) ord.push_back(i);
            std::stable_sort(ord.begin(), ord.end(), [&](size_t x, size_t y) { return pri(host[x]) < pri(host[y]); });
            for (size_t i : ord)
            {
                if (m[i] >= 0 || special(host[i].key) || host[i].mesh.empty() || !host[i].vis) continue;
                for (size_t j = 0; j < cl.size(); ++j)
                    if (!used[j] && !special(cl[j].key) && compat(i, j)) { m[i] = (int)j; used[j] = 1; break; }
            }
        }
        return m;
    }
    template <class Drop, class Redress>
    inline int gen_apply_plan(const std::string& g, const std::string& cur, Drop drop, Redress redress)
    {
        size_t sl = g.find('/');
        if (g.empty() || sl == std::string::npos) return -1;
        std::string sid = g.substr(0, sl), want = g.substr(sl + 1);
        if (cur == want) return 0;
        drop();
        return redress(sid, want) ? 1 : -1;
    }
    constexpr size_t kNativeMax = 16, kPartsMax = 32;
    inline bool part_room(bool made, size_t natives) { return made || natives < kNativeMax; }
    inline bool make_room(size_t total) { return total < kPartsMax; }
    template <class P>
    inline bool made_stale(const std::string& key, const std::string& mesh, const std::vector<P>& host, const std::vector<P>& cl)
    {
        bool want = false;
        int hn = 0, cn = 0;
        for (const P& h : host)
        {
            if (h.key == key) want = h.vis && !h.mesh.empty();
            if (h.vis && !h.mesh.empty() && h.mesh == mesh) ++hn;
        }
        for (const P& c : cl) if (!c.made && !c.mesh.empty() && c.mesh == mesh) ++cn;
        return !want || (!mesh.empty() && cn >= hn);
    }
}
