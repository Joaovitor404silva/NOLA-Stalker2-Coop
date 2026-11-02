#pragma once

#include <cmath>
#include <vector>

namespace cgcmc
{
    struct P
    {
        float x = 0, y = 0, z = 0;
    };

    struct View
    {
        bool ok = false;
        bool has_pos = false;
        bool up = false;
        bool left = false;
        double age = 1e9;
        P p;
    };

    inline bool fresh(const View& v, double max_age) { return v.ok && v.has_pos && v.up && !v.left && v.age >= 0 && v.age <= max_age; }

    inline float radius(float r, float k)
    {
        if (!(r >= 1000.f && r <= 100000.f)) r = 9500.f;
        if (!(k >= 1.f && k <= 2.f)) k = 1.06f;
        return r * k;
    }

    inline bool inside(const P& u, const P& c, float rad)
    {
        float dx = u.x - c.x, dy = u.y - c.y, dz = u.z - c.z;
        if (!(std::fabs(dz) <= rad)) return false;
        return dx * dx + dy * dy <= rad * rad;
    }

    template <class Ring>
    int keeper(const P& u, const std::vector<P>& cs, float rad, Ring&& ring)
    {
        for (size_t i = 0; i < cs.size(); i++)
            if (inside(u, cs[i], rad) && ring(cs[i])) return (int)i;
        return -1;
    }

    inline bool wants(bool on, bool host, bool corpse, size_t centres) { return on && host && corpse && centres > 0; }

    inline bool wants_unit(bool on, bool host, size_t centres) { return on && host && centres > 0; }

    constexpr size_t kMaxCentres = 8;

    inline std::vector<P> extra_centres(const std::vector<View>& views, double max_age)
    {
        std::vector<P> cs;
        for (const View& v : views)
            if (cs.size() < kMaxCentres && fresh(v, max_age)) cs.push_back(v.p);
        return cs;
    }

    inline int online_passes(bool on, size_t centres) { return on ? 1 + (int)centres : 1; }

    inline bool far_all(bool far_host, const P& u, const P* cs, size_t n, float rad)
    {
        if (!far_host) return false;
        for (size_t i = 0; i < n; i++)
            if (inside(u, cs[i], rad)) return false;
        return true;
    }

    template <class T>
    int dedup_tail(T* d, int n0, int n)
    {
        int w = n0;
        for (int i = n0; i < n; i++)
        {
            bool dup = false;
            for (int j = 0; j < w && !dup; j++) dup = d[j] == d[i];
            if (!dup) d[w++] = d[i];
        }
        return w;
    }

    inline bool cap_hit(int online, int cap) { return cap > 0 && online > cap; }

    inline bool live_ok(float r, float k) { return r >= 1000.f && r <= 100000.f && k >= 1.f && k <= 2.f; }

    bool online_radius(float& r, float& k);
}
