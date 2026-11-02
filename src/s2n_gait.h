#pragma once
#include <algorithm>
#include <cmath>

namespace s2ngait
{
    struct Cfg
    {
        double natrun = 230, natsprint = 450, natcatch = 200, natcatch2 = 80, natok = 40, natgadlo = 0.6, natgadhi = 0.9, natgadt = 1;
        int natgaitmax = 4, natgad = 0, natgait = 1, natmbh = 0;
    };

    struct Spd
    {
        double walk = 0, run = 0, sprint = 0;
    };

    struct St
    {
        int gb = 0;
        double rs = 0, gbAt = -100;
        bool catchUp = false;
    };

    struct In
    {
        double sp = 0, err = 0, csp = 0, now = 0, latAt = -1;
        int og = 0, hg = -1;
        bool moving = false, soft = false, kin = false, mut = false, fl128 = false;
        Spd cs;
    };

    enum Src { LEGACY = 0, HOST = 1, CLASS = 2 };

    struct Out
    {
        int gait = 2, base = 2, legacy = 2, src = LEGACY, gbUp = 0, gbDn = 0;
        bool lock = false;
    };

    inline int host_gait(int mb, int gmax)
    {
        if (mb >= 2 && mb <= 4) return mb;
        if (mb == 5) return gmax >= 5 ? 5 : 3;
        return -1;
    }

    inline bool known(const Spd& s) { return s.walk > 0 && s.run > 0; }

    inline double run_at(const Spd& s) { return 0.5 * (s.walk + s.run); }

    inline double sprint_at(const Spd& s) { return s.sprint > s.run ? 0.5 * (s.run + s.sprint) : -1; }

    inline int cls_gait(double sp, int og, const Spd& s)
    {
        double r = run_at(s), q = sprint_at(s);
        bool sprint = q > 0 && (sp > q || (og >= 4 && sp > 0.9 * q));
        bool run = sp > r || (og >= 3 && sp > 0.9 * r);
        return sprint ? 4 : (run ? 3 : 2);
    }

    inline int legacy_base(const In& i, const Cfg& c, bool catchUp)
    {
        bool sprint = i.sp > c.natsprint || (i.og >= 4 && i.sp > 0.85 * c.natsprint && !catchUp);
        bool run = i.fl128 || i.sp > c.natrun || (i.og >= 3 && i.sp > 0.8 * c.natrun);
        return sprint ? 4 : (run ? 3 : 2);
    }

    inline Out pick(const In& i, const Cfg& c, St& s)
    {
        Out o;
        int gmax = std::clamp(c.natgaitmax, 2, 5);
        o.legacy = legacy_base(i, c, s.catchUp);
        int gait = o.legacy;
        bool own = c.natgait >= 1 && i.mut;
        int hg = own && c.natgait >= 2 ? host_gait(i.hg, gmax) : -1;
        bool hum = c.natmbh && !i.mut && i.moving;
        if (hum) hg = host_gait(i.hg, gmax);
        if (hg >= 0) { gait = hg; o.src = HOST; }
        else if (own && known(i.cs)) { gait = cls_gait(i.sp, i.og, i.cs); o.src = CLASS; }
        o.base = gait;
        o.lock = (own || (hum && hg >= 0)) && i.kin;
        if (c.natgad && i.moving && !i.soft && i.sp > 50)
        {
            double rr = i.csp / i.sp;
            s.rs = s.rs <= 0 ? rr : 0.8 * s.rs + 0.2 * rr;
            if (o.lock) s.gb = 0;
            else if (i.latAt < 0 && i.now - s.gbAt >= c.natgadt)
            {
                if (s.rs < c.natgadlo && i.err > c.natok && gait + s.gb < gmax) { ++s.gb; s.gbAt = i.now; ++o.gbUp; }
                else if (s.gb > 0 && s.rs > c.natgadhi && i.err < c.natok) { --s.gb; s.gbAt = i.now; ++o.gbDn; }
            }
            gait += s.gb;
        }
        else if (!i.moving) { s.gb = 0; s.rs = 0; }
        if (!i.moving) s.catchUp = false;
        else if (i.err > c.natcatch) s.catchUp = true;
        else if (i.err < c.natcatch2) s.catchUp = false;
        if (s.catchUp && !o.lock) ++gait;
        if (!i.moving && i.err > 2 * c.natcatch && !o.lock) gait = std::max(gait, 3);
        o.gait = std::min(gait, gmax);
        return o;
    }

    enum KoWhy { KO_NONE = 0, KO_NEW = 1, KO_GAIT = 2, KO_TURN = 3, KO_NEAR = 4, KO_STOP = 5 };

    struct KoIn
    {
        bool moving = false, has = false;
        double hx = 0, hy = 0, vx = 0, vy = 0, cx = 0, cy = 0, ox = 0, oy = 0;
        int gait = 2, og = 0;
        double lead = 3, re = 80, ang = 25, succ = 30;
    };

    struct KoOut
    {
        int why = KO_NONE;
        double tx = 0, ty = 0;
    };

    inline KoOut kin_order(const KoIn& i)
    {
        KoOut o;
        double sp = std::sqrt(i.vx * i.vx + i.vy * i.vy);
        if (!i.moving || sp <= 0)
        {
            o.tx = i.hx; o.ty = i.hy;
            if (i.has && std::hypot(i.ox - i.hx, i.oy - i.hy) > std::max(i.succ, i.re)) o.why = KO_STOP;
            return o;
        }
        o.tx = i.hx + i.vx * i.lead; o.ty = i.hy + i.vy * i.lead;
        if (!i.has) { o.why = KO_NEW; return o; }
        if (i.gait != i.og) { o.why = KO_GAIT; return o; }
        double rx = i.ox - i.cx, ry = i.oy - i.cy, rem = std::hypot(rx, ry);
        if (rem < i.re) { o.why = KO_NEAR; return o; }
        double d = std::remainder(std::atan2(ry, rx) - std::atan2(i.vy, i.vx), 2 * 3.14159265358979) * 57.2957795;
        if (std::fabs(d) > i.ang) { o.why = KO_TURN; return o; }
        return o;
    }
}
