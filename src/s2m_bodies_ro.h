#pragma once
#include "s2m_bodies_logic.h"

#include <array>
#include <map>

namespace m01::ro
{
    struct Tf
    {
        double q[4];
        double t[4];
        double s[4];
    };
    static_assert(sizeof(Tf) == 0x60, "FTransform double layout");
    struct Loc
    {
        double q[4] = {0, 0, 0, 1};
        double t[3] = {0, 0, 0};
    };
    struct Row
    {
        const char* fam;
        const char* bone;
        double q[4];
        double t[3];
    };
#include "s2m_bodies_pose.inc"

    inline void qmul(const double a[4], const double b[4], double o[4])
    {
        double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        o[0] = x; o[1] = y; o[2] = z; o[3] = w;
    }
    inline void qrot(const double q[4], const double v[3], double o[3])
    {
        double tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
        o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    }
    inline void qnorm(double q[4])
    {
        double m = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (m > 0)
            for (int k = 0; k < 4; k++) q[k] /= m;
    }
    inline Loc local(const Tf& p, const Tf& c)
    {
        Loc o;
        double ip[4] = {-p.q[0], -p.q[1], -p.q[2], p.q[3]};
        qmul(ip, c.q, o.q);
        qnorm(o.q);
        double d[3] = {c.t[0] - p.t[0], c.t[1] - p.t[1], c.t[2] - p.t[2]};
        qrot(ip, d, o.t);
        return o;
    }
    inline void compose(const Tf& p, const Loc& l, Tf& c)
    {
        qmul(p.q, l.q, c.q);
        qnorm(c.q);
        double r[3];
        qrot(p.q, l.t, r);
        for (int k = 0; k < 3; k++) c.t[k] = p.t[k] + r[k];
    }
    inline Loc blend(const Loc& a, const Loc& b, double w)
    {
        Loc o;
        double d = a.q[0] * b.q[0] + a.q[1] * b.q[1] + a.q[2] * b.q[2] + a.q[3] * b.q[3];
        double sg = d < 0 ? -1 : 1;
        for (int k = 0; k < 4; k++) o.q[k] = a.q[k] * (1 - w) + sg * b.q[k] * w;
        qnorm(o.q);
        for (int k = 0; k < 3; k++) o.t[k] = a.t[k] * (1 - w) + b.t[k] * w;
        return o;
    }

    inline std::string lower(std::string s)
    {
        for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
        return s;
    }
    inline std::string rh_fam(const std::string& w)
    {
        if (lg::pa_fam(w) == "pt") return "pt";
        std::string l = lower(w);
        if (l.find("/weapons/knifes/") != std::string::npos) return "kn";
        if (l.find("/weapons/bolt/") != std::string::npos || l.find("/weapons/grenades/") != std::string::npos) return "bt";
        return "";
    }
    inline std::vector<std::string> fams(const lg::Ctx& C, const std::string& rh, bool det, const std::string& use, bool isAc, bool detBase)
    {
        std::vector<std::string> v;
        if (C.f("paro", 1) != 1 || isAc) return v;
        if (lg::pa_lad(use) || use == "fall" || (use.size() == 4 && (use.compare(0, 2, "eq") == 0 || use.compare(0, 2, "uq") == 0))) return v;
        if (rh == "pt" && det) v.push_back("pt");
        if ((rh == "kn" || rh == "bt") && det && C.f("paroarm", 0) == 1) v.push_back("pt");
        if ((rh == "kn" || rh == "bt") && C.f("parofist", 1) == 1) v.push_back(rh == "bt" && C.f("pabarm", 1) == 1 ? "bo" : rh);
        if (det && !detBase && C.f("parodet", 1) == 1) v.push_back("de");
        return v;
    }
    inline const char* samp_clip(const std::string& f)
    {
        if (f == "pt") return "pt/pm/idle/AS_tp_pt_combat_pm_idle";
        if (f == "kn" || f == "bt" || f == "pc") return "pt/combat/Idles/AS_tp_pt_combat_idle";
        if (f == "de") return "bh/Contextual/stand/tp_bh_ca_detectors/echo/AS_tp_bh_ca_detector_echo_idle_a";
        if (f == "bo") return "bh/Contextual/stand/tp_bh_ca_bolt_playing_action/AS_tp_bh_ca_bolt_playing_idle";
        if (f == "bd") return "bh/Contextual/stand/tp_bh_ca_dalin_bolt/AS_tp_bh_ca_dalin_bolt_idle";
        return nullptr;
    }
    inline std::string samp_fam(const std::string& k)
    {
        if (k == "pc") return "pt";
        if (k == "bd") return "bo";
        return k;
    }
    inline std::string key(const std::vector<std::string>& v)
    {
        std::string k;
        for (const auto& s : v) k += s + ",";
        return k;
    }

    struct Pick
    {
        std::vector<int> bones;
        std::vector<int> cp;
        int rot = -1;
    };
    struct Skel
    {
        std::vector<std::string> names;
        std::vector<int> par;
        int idx(const std::string& s) const
        {
            for (int i = 0; i < (int)names.size(); i++)
                if (names[i] == s) return i;
            return -1;
        }
        bool under(int i, int a) const
        {
            for (int k = 0; i >= 0 && k <= (int)par.size(); k++, i = par[i])
                if (i == a) return true;
            return false;
        }
    };
    inline std::string mirror(const std::string& s)
    {
        std::string o = s;
        size_t k = o.find("_r_");
        if (k != std::string::npos) { o[k + 1] = 'l'; return o; }
        if (o.size() > 2 && o.compare(o.size() - 2, 2, "_r") == 0) { o[o.size() - 1] = 'l'; return o; }
        return "";
    }
    inline int hand_r(const Skel& k)
    {
        int w = k.idx("jnt_r_weapon");
        if (w >= 0 && k.par[w] >= 0 && lower(k.names[k.par[w]]).find("hand") != std::string::npos) return k.par[w];
        return k.idx("jnt_r_hand");
    }
    inline int clav(const Skel& k, int h)
    {
        if (h < 0) return -1;
        int c = h;
        for (int g = 0; g <= (int)k.par.size(); g++)
        {
            int p = k.par[c];
            if (p < 0) return -1;
            std::string n = lower(k.names[p]);
            if (n.find("spine") != std::string::npos || n.find("neck") != std::string::npos || n.find("chest") != std::string::npos) return c;
            c = p;
        }
        return -1;
    }
    inline Pick pick(const Skel& k, const std::string& fam, int clavMode = 1)
    {
        Pick o;
        int n = (int)k.names.size();
        o.cp = k.par;
        int hr = hand_r(k);
        if (hr < 0) return o;
        int w = k.idx("jnt_r_weapon");
        auto add_sub = [&](int a, bool self, int skip) {
            for (int i = 0; i < n; i++)
            {
                if (!self && i == a) continue;
                if (skip >= 0 && k.under(i, skip)) continue;
                if (k.under(i, a)) o.bones.push_back(i);
            }
        };
        auto follow = [&](int b, int anchor) {
            if (b < 0 || anchor < 0) return;
            for (int i : o.bones)
                if (i == b) return;
            o.bones.push_back(b);
            o.cp[b] = anchor;
        };
        if (fam == "pt" || fam == "bo")
        {
            int c = clav(k, hr);
            if (c < 0) return o;
            add_sub(c, clavMode != 0, -1);
            int hp = k.idx("jnt_hips");
            if (clavMode == 2 && hp >= 0 && k.under(c, hp)) { o.cp[c] = hp; o.rot = c; }
            follow(w, hr);
            if (fam == "bo") follow(k.idx("jnt_item"), hr);
        }
        else if (fam == "kn" || fam == "bt")
        {
            add_sub(hr, false, w);
            follow(w, hr);
        }
        else if (fam == "de")
        {
            int hl = k.idx(mirror(k.names[hr]));
            int c = clav(k, hl);
            if (c < 0) return o;
            add_sub(c, true, -1);
            follow(k.idx("jnt_item"), hl);
        }
        std::sort(o.bones.begin(), o.bones.end());
        return o;
    }

    struct Tgt
    {
        int bone;
        int cp;
        Loc l;
        double w = 1;
        bool rot = false;
    };
    inline bool order_ok(const std::vector<int>& cp, int i) { return cp[i] >= 0 && cp[i] != i; }
    inline std::vector<Loc> capture(const Tf* b, int n, const Pick& p)
    {
        std::vector<Loc> v;
        for (int i : p.bones)
            v.push_back((i >= 0 && i < n && p.cp[i] >= 0 && p.cp[i] < n) ? local(b[p.cp[i]], b[i]) : Loc{});
        return v;
    }
    inline void apply(Tf* b, int n, const std::vector<int>& par, const std::vector<Tgt>& tg)
    {
        if (n <= 0 || (int)par.size() != n || tg.empty()) return;
        std::vector<int> cp(par);
        std::vector<signed char> mark(n, 0);
        std::vector<const Tgt*> at(n, nullptr);
        for (const Tgt& t : tg)
        {
            if (t.bone < 0 || t.bone >= n || t.cp < 0 || t.cp >= n || t.cp == t.bone || t.w <= 0) continue;
            at[t.bone] = &t;
            cp[t.bone] = t.cp;
            mark[t.bone] = cp[t.bone] < t.bone ? 1 : 2;
        }
        for (int pass = 1; pass <= 2; pass++)
            for (int i = 0; i < n; i++)
                if (!mark[i] && par[i] >= 0 && par[i] < i && mark[par[i]]) mark[i] = mark[par[i]];
        std::vector<Loc> loc(n);
        for (int i = 0; i < n; i++)
            if (mark[i] && cp[i] >= 0) loc[i] = local(b[cp[i]], b[i]);
        for (int i = 0; i < n; i++)
            if (at[i])
            {
                Loc o = blend(loc[i], at[i]->l, std::min(1.0, at[i]->w));
                if (at[i]->rot)
                    for (int q = 0; q < 3; q++) o.t[q] = loc[i].t[q];
                loc[i] = o;
            }
        for (int pass = 1; pass <= 2; pass++)
            for (int i = 0; i < n; i++)
                if (mark[i] == pass && cp[i] >= 0) compose(b[cp[i]], loc[i], b[i]);
    }
    inline std::vector<Tgt> targets(const Skel& k, const std::string& fam, const std::map<int, Loc>* cap, int clavMode = 1)
    {
        std::vector<Tgt> v;
        Pick p = pick(k, fam, clavMode);
        for (int i : p.bones)
        {
            if (cap)
            {
                auto it = cap->find(i);
                if (it != cap->end()) v.push_back({i, p.cp[i], it->second, 1, i == p.rot});
                continue;
            }
            std::string rf = i == p.rot ? fam + "h" : fam;
            for (const Row& r : kPose)
            {
                if (!r.fam || rf != r.fam || k.names[i] != r.bone) continue;
                Tgt t{i, p.cp[i], {}, 1, i == p.rot};
                for (int q = 0; q < 4; q++) t.l.q[q] = r.q[q];
                for (int q = 0; q < 3; q++) t.l.t[q] = r.t[q];
                v.push_back(t);
                break;
            }
        }
        return v;
    }
    inline bool item_free(const lg::Ctx& C, const std::string& fam, bool det)
    {
        return fam == "bo" && det && C.f("pabdet", 1) == 1;
    }
    inline void drop_bone(std::vector<Tgt>& v, int bone)
    {
        v.erase(std::remove_if(v.begin(), v.end(), [bone](const Tgt& t) { return t.bone == bone; }), v.end());
    }
    inline const Row* row(const std::string& fam, const std::string& bone)
    {
        for (const Row& r : kPose)
            if (r.fam && fam == r.fam && bone == r.bone) return &r;
        return nullptr;
    }
    inline void q2r(const double q[4], double& pitch, double& yaw, double& roll)
    {
        const double d = 57.29577951308232;
        double x = q[0], y = q[1], z = q[2], w = q[3];
        double s = z * x - w * y;
        double yy = 2 * (w * z + x * y), yx = 1 - 2 * (y * y + z * z);
        auto norm = [](double a) { a = std::fmod(a + 180.0, 360.0); if (a < 0) a += 360.0; return a - 180.0; };
        yaw = std::atan2(yy, yx) * d;
        if (s < -0.4999995) { pitch = -90; roll = norm(-yaw - 2 * std::atan2(x, w) * d); }
        else if (s > 0.4999995) { pitch = 90; roll = norm(yaw - 2 * std::atan2(x, w) * d); }
        else { pitch = std::asin(2 * s) * d; roll = std::atan2(-2 * (w * x + y * z), 1 - 2 * (x * x + y * y)) * d; }
    }
    inline void r2q(double pitch, double yaw, double roll, double q[4])
    {
        const double h = 3.14159265358979323846 / 360.0;
        double sp = std::sin(pitch * h), cp = std::cos(pitch * h), sy = std::sin(yaw * h), cy = std::cos(yaw * h), sr = std::sin(roll * h), cr = std::cos(roll * h);
        q[0] = cr * sp * sy - sr * cp * cy;
        q[1] = -cr * sp * cy - sr * cp * sy;
        q[2] = cr * cp * sy - sr * sp * cy;
        q[3] = cr * cp * cy + sr * sp * sy;
    }
    inline Loc inv(const Loc& a)
    {
        Loc o;
        o.q[0] = -a.q[0]; o.q[1] = -a.q[1]; o.q[2] = -a.q[2]; o.q[3] = a.q[3];
        double r[3];
        qrot(o.q, a.t, r);
        for (int k = 0; k < 3; k++) o.t[k] = -r[k];
        return o;
    }
    inline Loc cmp(const Loc& p, const Loc& l)
    {
        Loc o;
        qmul(p.q, l.q, o.q);
        qnorm(o.q);
        double r[3];
        qrot(p.q, l.t, r);
        for (int k = 0; k < 3; k++) o.t[k] = p.t[k] + r[k];
        return o;
    }
    inline Loc grip_cancel(const Loc& grip, const Tf& off)
    {
        Loc l;
        for (int k = 0; k < 4; k++) l.q[k] = off.q[k];
        for (int k = 0; k < 3; k++) l.t[k] = off.t[k];
        qnorm(l.q);
        return cmp(grip, inv(l));
    }
    inline double ramp(double cur, double want, double dt, double bt)
    {
        if (bt <= 0) return want;
        double s = dt / bt;
        if (cur < want) return std::min(want, cur + s);
        return std::max(want, cur - s);
    }
    inline void weights(std::map<std::string, double>& a, const std::vector<std::string>& want, double dt, double bt)
    {
        for (const auto& f : want) a.emplace(f, 0.0);
        for (auto it = a.begin(); it != a.end();)
        {
            bool on = std::find(want.begin(), want.end(), it->first) != want.end();
            it->second = ramp(it->second, on ? 1 : 0, dt, bt);
            if (!on && it->second <= 0) it = a.erase(it);
            else ++it;
        }
    }
}
