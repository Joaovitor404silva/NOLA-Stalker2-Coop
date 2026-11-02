#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace s2m_throw::lg
{
    struct Ent
    {
        int seq = 0;
        char k = 0;
        char st = 0;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0;
        std::string path;
    };

    inline std::string fmt_line(const char* f, ...)
    {
        char b[1024];
        va_list a;
        va_start(a, f);
        int n = std::vsnprintf(b, sizeof b, f, a);
        va_end(a);
        if (n < 0) return std::string();
        return std::string(b, (size_t)(n < (int)sizeof b ? n : (int)sizeof b - 1));
    }

    inline bool path_ok(const std::string& p)
    {
        if (p.size() > 200) return false;
        for (char c : p)
        {
            bool ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '/' || c == '.' || c == '-';
            if (!ok) return false;
        }
        return true;
    }

    inline std::string ent_str(const Ent& e)
    {
        char b[200];
        std::snprintf(b, sizeof b, "%d,%c,%c,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,", e.seq, e.k, e.st, e.x, e.y, e.z, e.vx, e.vy, e.vz);
        return std::string(b) + (path_ok(e.path) ? e.path : std::string());
    }

    inline std::string tag_str(const std::vector<Ent>& v)
    {
        std::string s;
        for (size_t i = 0; i < v.size(); ++i) s += (i ? ";" : "") + ent_str(v[i]);
        return s;
    }

    inline bool num_ok(const std::string& s, double& out)
    {
        if (s.empty() || s.size() > 24) return false;
        char* e = nullptr;
        out = std::strtod(s.c_str(), &e);
        return e && *e == 0 && std::isfinite(out);
    }

    inline std::vector<std::string> cut(const std::string& s, char sep)
    {
        std::vector<std::string> o;
        size_t a = 0;
        while (true)
        {
            size_t b = s.find(sep, a);
            o.push_back(s.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        return o;
    }

    inline std::vector<Ent> parse(const std::string& s)
    {
        std::vector<Ent> out;
        if (s.empty() || s.size() > 4000) return out;
        for (const std::string& part : cut(s, ';'))
        {
            auto f = cut(part, ',');
            if (f.size() != 10 || f[1].size() != 1 || f[2].size() != 1) continue;
            Ent e;
            double sq = 0;
            if (!num_ok(f[0], sq) || sq < 1 || sq > 9999) continue;
            e.seq = (int)sq;
            e.k = f[1][0];
            e.st = f[2][0];
            if (e.k != 'b' && e.k != 'g') continue;
            if (e.st != 'f' && e.st != 'r' && e.st != 'x') continue;
            double* d[6] = {&e.x, &e.y, &e.z, &e.vx, &e.vy, &e.vz};
            bool ok = true;
            for (int i = 0; i < 6 && ok; ++i) ok = num_ok(f[3 + i], *d[i]);
            if (!ok || !path_ok(f[9])) continue;
            e.path = f[9];
            out.push_back(e);
            if (out.size() >= 8) break;
        }
        return out;
    }

    inline void fly(const Ent& e, double dt, double maxDt, double& x, double& y, double& z)
    {
        if (dt < 0) dt = 0;
        if (dt > maxDt) dt = maxDt;
        if (e.st != 'f') dt = 0;
        x = e.x + e.vx * dt;
        y = e.y + e.vy * dt;
        z = e.z + e.vz * dt - 0.5 * 980.0 * dt * dt;
    }

    inline bool tx_ready(const Ent& e, bool on)
    {
        return !on || e.st != 'f' || std::fabs(e.vx) + std::fabs(e.vy) + std::fabs(e.vz) >= 1;
    }

    inline bool mine(double px, double py, double pz, double ax, double ay, double az, double near)
    {
        double dx = ax - px, dy = ay - py, dz = az - pz;
        return dx * dx + dy * dy + dz * dz <= near * near;
    }

    inline bool own_ok(int ownerMatch, double now, double lastThAt, double win)
    {
        if (ownerMatch == 1) return true;
        if (ownerMatch == 0) return false;
        return now - lastThAt < win;
    }

    struct RxCopy
    {
        Ent e;
        double rxAt = 0, endAt = -1;
        bool fx = false;
    };
    struct RxView
    {
        std::map<int, RxCopy> cm;
        std::set<int> done;
        int maxSeq = 0;
        double emptySince = -1;
    };
    struct RxOut
    {
        std::vector<int> spawn, boom, land, despawn;
    };

    inline RxOut rx_step(RxView& V, const std::vector<Ent>& in, double now, double keep, double resetEmpty)
    {
        RxOut o;
        if (in.empty())
        {
            if (V.emptySince < 0) V.emptySince = now;
            if (now - V.emptySince >= resetEmpty && V.cm.empty() && (!V.done.empty() || V.maxSeq))
            {
                V.done.clear();
                V.maxSeq = 0;
            }
        }
        else
            V.emptySince = -1;
        for (const Ent& e : in)
        {
            auto it = V.cm.find(e.seq);
            if (it == V.cm.end())
            {
                if (V.done.count(e.seq)) continue;
                if (e.seq < V.maxSeq)
                {
                    V.done.clear();
                    V.maxSeq = 0;
                }
                if (e.seq > V.maxSeq) V.maxSeq = e.seq;
                if (e.st != 'f')
                {
                    V.done.insert(e.seq);
                    continue;
                }
                RxCopy c;
                c.e = e;
                c.rxAt = now;
                V.cm.emplace(e.seq, c);
                o.spawn.push_back(e.seq);
                continue;
            }
            RxCopy& c = it->second;
            if (c.endAt >= 0) continue;
            bool moved = e.x != c.e.x || e.y != c.e.y || e.z != c.e.z || e.st != c.e.st;
            if (!moved) continue;
            std::string keepPath = c.e.path;
            c.e = e;
            if (e.st == 'f') c.e.path = keepPath;
            c.rxAt = now;
            if (e.st == 'x')
            {
                c.endAt = now;
                if (!c.fx)
                {
                    c.fx = true;
                    o.boom.push_back(e.seq);
                }
            }
            else if (e.st == 'r')
            {
                c.endAt = now;
                o.land.push_back(e.seq);
            }
        }
        for (auto it = V.cm.begin(); it != V.cm.end();)
        {
            const RxCopy& c = it->second;
            bool old = (c.endAt >= 0 && now - c.endAt > keep) || now - c.rxAt > 12;
            if (old)
            {
                o.despawn.push_back(it->first);
                V.done.insert(it->first);
                it = V.cm.erase(it);
            }
            else
                ++it;
        }
        if (V.done.size() > 200) V.done.clear();
        return o;
    }

    inline std::string asset_path(const std::string& fullName)
    {
        size_t sp = fullName.find(' ');
        std::string p = sp == std::string::npos ? fullName : fullName.substr(sp + 1);
        return p.empty() || p[0] != '/' ? std::string() : p;
    }
}
