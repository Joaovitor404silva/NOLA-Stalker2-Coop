#include "s2m_bodies_logic.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace lg = m01::lg;

static std::map<std::string, double> g_fl;
static double FL(const char* k, double d)
{
    auto it = g_fl.find(k);
    return it == g_fl.end() ? d : it->second;
}
static std::string FS(const char* k)
{
    auto it = g_fl.find(k);
    return it == g_fl.end() ? std::string("nil") : lg::num(it->second);
}
static void LOG(const std::string&) {}
static const lg::Ctx C{&FL, &FS, &LOG};

struct Seg
{
    double dur, speed, heading, turn;
};

struct Truth
{
    double x = 0, y = 0, vx = 0, vy = 0;
};

static std::vector<Seg> path()
{
    const double D = 3.14159265358979 / 180;
    return {
        {2, 0, 0, 0},
        {4, 400, 0, 0},
        {1.5, 0, 0, 0},
        {5, 600, 0, 0},
        {5, 600, 90 * D, 0},
        {4, 600, 0, 0},
        {1.2, 0, 0, 0},
        {3, 250, -90 * D, 0},
        {2, 250, 90 * D, 0},
        {1, 0, 0, 0},
        {1, 450, 0, 0},
        {0.6, 0, 0, 0},
        {1, 450, 0, 0},
        {0.6, 0, 0, 0},
        {1, 450, 0, 0},
        {1.5, 0, 0, 0},
        {6, 500, 0, 0},
        {6, 500, 120 * D, 0},
        {3, 0, 0, 0},
    };
}

static Truth truth_at(double t)
{
    static std::vector<Seg> P = path();
    static std::vector<double> t0, hd;
    static std::vector<Truth> st;
    if (t0.empty())
    {
        double tt = 0, cur = 0;
        Truth s;
        for (auto& g : P)
        {
            t0.push_back(tt);
            st.push_back(s);
            double h = g.heading;
            hd.push_back(h);
            (void)cur;
            s.x += g.speed * std::cos(h) * g.dur;
            s.y += g.speed * std::sin(h) * g.dur;
            tt += g.dur;
        }
        t0.push_back(tt);
    }
    if (t >= t0.back()) t = t0.back() - 1e-6;
    size_t i = 0;
    while (i + 1 < P.size() && t >= t0[i + 1]) i++;
    double u = t - t0[i];
    Truth o;
    o.x = st[i].x + P[i].speed * std::cos(hd[i]) * u;
    o.y = st[i].y + P[i].speed * std::sin(hd[i]) * u;
    o.vx = P[i].speed * std::cos(hd[i]);
    o.vy = P[i].speed * std::sin(hd[i]);
    return o;
}

static double total_time()
{
    double t = 0;
    for (auto& g : path()) t += g.dur;
    return t;
}

struct Pkt
{
    double arrive, sent, x, y, vx, vy;
};

static int sim(int argc, char** argv, int* oA15, int* oA60, double* oMax)
{
    g_fl.clear();
    g_fl["simdrive"] = 0.001;
    g_fl["simav"] = 1;
    g_fl["avacc"] = 5000;
    g_fl["avjit"] = 1;
    g_fl["avjitmax"] = 0.15;
    g_fl["avxtra"] = 0.25;
    g_fl["avsnap"] = 700;
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "old=1")
        {
            g_fl["simdrive"] = 0.1;
            g_fl["simav"] = 0;
            g_fl["avacc"] = 0;
        }
    g_fl["nsip"] = 1;
    g_fl["nsnoback"] = 1;
    g_fl["nscap"] = 0;
    g_fl["nsgain"] = 7;
    g_fl["nsdead"] = 3;
    g_fl["nssnap"] = 300;
    g_fl["nsvmax"] = 1500;
    g_fl["nsipmax"] = 0.5;
    g_fl["nsippad"] = 0.05;
    g_fl["nsbhold"] = 0.5;
    g_fl["nsbmax"] = 60;
    g_fl["avoffx"] = 0;
    double jit = 0, spike = 0, stall = 0, lat = 0.03, fps = 60, seed = 1;
    std::string csv;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        size_t e = a.find('=');
        if (e == std::string::npos) continue;
        std::string k = a.substr(0, e), v = a.substr(e + 1);
        if (k == "jit") jit = std::atof(v.c_str());
        else if (k == "spike") spike = std::atof(v.c_str());
        else if (k == "stall") stall = std::atof(v.c_str());
        else if (k == "lat") lat = std::atof(v.c_str());
        else if (k == "fps") fps = std::atof(v.c_str());
        else if (k == "seed") seed = std::atof(v.c_str());
        else if (k == "csv") csv = v;
        else g_fl[k] = std::atof(v.c_str());
    }
    std::mt19937 rng((unsigned)seed);
    std::uniform_real_distribution<double> U(0, 1);
    double T = total_time();
    std::deque<Pkt> inflight;
    std::vector<Pkt> arrived;
    double nextSend = 0;
    lg::Mover K;
    lg::NB nb;
    lg::NsS ns = lg::ns_s0(0);
    double px = 0, py = 0, vx = 0, vy = 0;
    bool init = false;
    double now = 0, dt = 1.0 / fps, tickAt = -1;
    std::optional<double> lastRecv;
    double lastDriveAt = 0;
    std::vector<std::array<double, 5>> fr;
    int snaps = 0;
    double stallStart = stall > 0 ? 24.0 : 1e9;
    while (now < T + 1.0)
    {
        while (nextSend <= now)
        {
            Truth s = truth_at(nextSend);
            double d = lat + (jit > 0 ? jit * U(rng) : 0) + (spike > 0 && U(rng) < 0.04 ? spike : 0);
            double arr = nextSend + d;
            if (nextSend >= stallStart && nextSend < stallStart + stall) arr = stallStart + stall + 0.02 + 0.002 * (nextSend - stallStart);
            inflight.push_back({arr, nextSend, s.x, s.y, s.vx, s.vy});
            nextSend += 0.1;
        }
        std::vector<Pkt> keep;
        for (auto& p : inflight)
        {
            if (p.arrive <= now) arrived.push_back(p);
            else keep.push_back(p);
        }
        inflight.assign(keep.begin(), keep.end());
        if (!arrived.empty() && !init)
        {
            init = true;
            px = arrived.back().x;
            py = arrived.back().y;
        }
        double per = FL("simdrive", 0.1);
        if (init && now - tickAt >= per - 1e-9)
        {
            tickAt = now;
            const Pkt* best = nullptr;
            for (auto& p : arrived)
                if (!best || p.sent > best->sent) best = &p;
            if (best && (!lastRecv || *lastRecv != best->arrive))
            {
                lastRecv = best->arrive;
                K.av = FL("simav", 1) == 1;
                lg::ns_sample(C, K, best->x, best->y, 0, std::nullopt, best->sent, best->arrive, best->vx, best->vy, 0.0, false);
            }
            lg::DriveOut o = lg::ns_drive(C, nb, ns, K, px, py, 0.0, std::nullopt, now);
            if (o.ok)
            {
                if (o.snap)
                {
                    px = o.sx;
                    py = o.sy;
                    snaps++;
                }
                vx = o.vx;
                vy = o.vy;
                lastDriveAt = now;
            }
        }
        if (now - lastDriveAt > 0.15) { vx = 0; vy = 0; }
        px += vx * dt;
        py += vy * dt;
        Truth tr = truth_at(now);
        fr.push_back({now, px, py, tr.x, tr.y});
        now += dt;
    }
    if (!csv.empty())
    {
        FILE* f = std::fopen(csv.c_str(), "w");
        for (auto& r : fr) std::fprintf(f, "%.4f,%.2f,%.2f,%.2f,%.2f\n", r[0], r[1], r[2], r[3], r[4]);
        std::fclose(f);
    }
    double sp = 0.05;
    std::vector<std::array<double, 3>> s20;
    double nextS = 0;
    for (auto& r : fr)
        if (r[0] >= nextS)
        {
            s20.push_back({r[0], r[1], r[2]});
            nextS += sp;
        }
    int s15 = 0, s60 = 0, a15 = 0, a60 = 0;
    double maxstep = 0, maxacc = 0, prev = -1;
    for (size_t i = 1; i < s20.size(); i++)
    {
        double d = std::hypot(s20[i][1] - s20[i - 1][1], s20[i][2] - s20[i - 1][2]);
        if (d > maxstep) maxstep = d;
        if (d > 15) s15++;
        if (d > 60) s60++;
        if (prev >= 0)
        {
            double a = std::fabs(d - prev);
            if (a > 15) a15++;
            if (a > 60) a60++;
            if (a > maxacc) maxacc = a;
        }
        prev = d;
    }
    double fprev = -1, fmaxacc = 0, fmaxstep = 0;
    int f5 = 0, f15 = 0;
    for (size_t i = 1; i < fr.size(); i++)
    {
        double d = std::hypot(fr[i][1] - fr[i - 1][1], fr[i][2] - fr[i - 1][2]);
        if (d > fmaxstep) fmaxstep = d;
        if (fprev >= 0)
        {
            double a = std::fabs(d - fprev);
            if (a > fmaxacc) fmaxacc = a;
            if (a > 5) f5++;
            if (a > 15) f15++;
        }
        fprev = d;
    }
    int backs = 0;
    double backmax = 0;
    {
        double bx = 0, by = 0;
        bool inb = false;
        for (size_t i = 2; i < fr.size(); i++)
        {
            double ax = fr[i - 1][1] - fr[i - 2][1], ay = fr[i - 1][2] - fr[i - 2][2], cx = fr[i][1] - fr[i - 1][1], cy = fr[i][2] - fr[i - 1][2];
            double sa = std::hypot(ax, ay) * fps, sc = std::hypot(cx, cy) * fps;
            bool rev = sa > 40 && sc > 40 && (ax * cx + ay * cy) < -0.3 * std::hypot(ax, ay) * std::hypot(cx, cy);
            if (rev) { backs++; if (sc > backmax) backmax = sc; }
            (void)bx; (void)by; (void)inb;
        }
    }
    double lagSum = 0, lagMax = 0;
    int ln = 0;
    for (auto& r : fr)
    {
        if (r[0] < 2) continue;
        double e = std::hypot(r[1] - r[3], r[2] - r[4]);
        lagSum += e;
        ln++;
        if (e > lagMax) lagMax = e;
    }
    double mins = T / 60.0;
    std::printf("dur=%.1fs snaps=%d | 20Hz: a15=%d (%.1f/min) a60=%d (%.1f/min) maxstep=%.1f maxjerk=%.1f | 60Hz: jerk>5=%d jerk>15=%d maxjerk=%.1f maxstep=%.1f | err_avg=%.0f err_max=%.0f cm | reverse frames=%d\n",
                T, snaps, a15, a15 / mins, a60, a60 / mins, maxstep, maxacc, f5, f15, fmaxacc, fmaxstep, lagSum / std::max(1, ln), lagMax, backs);
    if (oA15) *oA15 = a15;
    if (oA60) *oA60 = a60;
    if (oMax) *oMax = maxacc;
    return 0;
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "selftest")
    {
        const char* sc[][6] = {{"x", "seed=1", "", "", "", ""}, {"x", "jit=0.04", "seed=2", "", "", ""}, {"x", "jit=0.08", "spike=0.2", "seed=3", "", ""}, {"x", "stall=1", "seed=4", "", "", ""}, {"x", "jit=0.04", "stall=1", "spike=0.15", "seed=5", ""}};
        int bad = 0;
        for (auto& s : sc)
        {
            std::vector<char*> a;
            for (auto p : s)
                if (*p) a.push_back(const_cast<char*>(p));
            int a15 = 0, a60 = 0;
            double mx = 0;
            sim((int)a.size(), a.data(), &a15, &a60, &mx);
            if (a15 > 1 || a60 > 0 || mx > 25) { std::printf("FAIL a15=%d a60=%d maxjerk=%.1f\n", a15, a60, mx); bad++; }
        }
        std::printf(bad ? "dr_sim selftest FAIL\n" : "dr_sim selftest PASS\n");
        return bad ? 1 : 0;
    }
    return sim(argc, argv, nullptr, nullptr, nullptr);
}
