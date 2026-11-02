#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

namespace m01::ab
{
    struct Tf
    {
        double q[4];
        double t[4];
        double s[4];
    };
    static_assert(sizeof(Tf) == 0x60, "FTransform double layout");

    struct Sel
    {
        std::vector<int> idx;
        std::vector<uint8_t> kind;
        uint32_t hash = 0;
    };

    struct Pose
    {
        double at = 0;
        std::vector<std::array<double, 4>> q;
        std::vector<std::array<double, 3>> t;
    };

    constexpr double kTUnit = 0.05;
    constexpr int kHeader = 7;
    constexpr int kVer = 1;

    inline std::string lowers(std::string s)
    {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    inline Sel select(const std::vector<std::string>& names, const std::vector<int>& par, int handR, int handL)
    {
        Sel s;
        int n = (int)names.size();
        std::vector<char> drop(n, 0);
        for (int i = 0; i < n; i++)
        {
            int p = par[i];
            if (p >= 0 && (p == handR || p == handL || drop[p])) drop[i] = 1;
        }
        uint32_t h = 2166136261u;
        for (int i = 1; i < n; i++)
        {
            if (drop[i] || names[i].empty()) continue;
            std::string ln = lowers(names[i]);
            uint8_t kind = 0;
            if (par[i] == 0)
            {
                if (ln.find("hips") != std::string::npos || ln.find("pelvis") != std::string::npos || ln.find("weapon") != std::string::npos) kind = 1;
                else continue;
            }
            s.idx.push_back(i);
            s.kind.push_back(kind);
            for (char c : names[i]) h = (h ^ (uint8_t)c) * 16777619u;
            h = (h ^ kind) * 16777619u;
        }
        s.hash = h;
        return s;
    }

    inline void qnorm(double* q)
    {
        double l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (l < 1e-12) { q[0] = q[1] = q[2] = 0; q[3] = 1; return; }
        for (int i = 0; i < 4; i++) q[i] /= l;
    }
    inline void qmul(const double* a, const double* b, double* o)
    {
        double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        o[0] = x; o[1] = y; o[2] = z; o[3] = w;
    }
    inline void qconj(const double* a, double* o)
    {
        o[0] = -a[0]; o[1] = -a[1]; o[2] = -a[2]; o[3] = a[3];
    }
    inline void qrot(const double* q, const double* v, double* o)
    {
        double tx = 2 * (q[1] * v[2] - q[2] * v[1]);
        double ty = 2 * (q[2] * v[0] - q[0] * v[2]);
        double tz = 2 * (q[0] * v[1] - q[1] * v[0]);
        o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    }
    inline void qnlerp(const double* a, const double* b, double w, double* o)
    {
        double d = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
        double sg = d < 0 ? -1.0 : 1.0;
        for (int i = 0; i < 4; i++) o[i] = a[i] * (1 - w) + b[i] * sg * w;
        qnorm(o);
    }

    constexpr double kR2 = 0.70710678118654752;

    inline void pack_q(const double* q, uint8_t* o)
    {
        double a[4] = {q[0], q[1], q[2], q[3]};
        qnorm(a);
        int m = 0;
        for (int i = 1; i < 4; i++)
            if (std::fabs(a[i]) > std::fabs(a[m])) m = i;
        if (a[m] < 0)
            for (int i = 0; i < 4; i++) a[i] = -a[i];
        uint64_t bits = (uint64_t)m;
        for (int i = 0; i < 4; i++)
        {
            if (i == m) continue;
            double v = std::max(-kR2, std::min(kR2, a[i]));
            uint64_t u = (uint64_t)std::llround((v / kR2 * 0.5 + 0.5) * 32767.0);
            bits = (bits << 15) | (u & 0x7fff);
        }
        for (int k = 0; k < 6; k++) o[k] = (uint8_t)(bits >> (8 * (5 - k)));
    }
    inline void unpack_q(const uint8_t* in, double* q)
    {
        uint64_t bits = 0;
        for (int k = 0; k < 6; k++) bits = (bits << 8) | in[k];
        int m = (int)((bits >> 45) & 3);
        uint64_t u[3] = {(bits >> 30) & 0x7fff, (bits >> 15) & 0x7fff, bits & 0x7fff};
        double s = 0;
        int j = 0;
        for (int i = 0; i < 4; i++)
        {
            if (i == m) continue;
            double v = ((double)u[j++] / 32767.0 - 0.5) * 2.0 * kR2;
            q[i] = v;
            s += v * v;
        }
        q[m] = std::sqrt(std::max(0.0, 1.0 - s));
    }
    inline void pack_t(const double* t, uint8_t* o)
    {
        for (int k = 0; k < 3; k++)
        {
            long v = std::lround(t[k] / kTUnit);
            v = std::max(-32767L, std::min(32767L, v));
            o[k * 2] = (uint8_t)(v & 0xff);
            o[k * 2 + 1] = (uint8_t)((v >> 8) & 0xff);
        }
    }
    inline void unpack_t(const uint8_t* in, double* t)
    {
        for (int k = 0; k < 3; k++)
        {
            int16_t v = (int16_t)(in[k * 2] | (in[k * 2 + 1] << 8));
            t[k] = v * kTUnit;
        }
    }

    inline size_t payload_bytes(const Sel& s)
    {
        size_t n = kHeader;
        for (uint8_t k : s.kind) n += k ? 12 : 6;
        return n;
    }

    inline std::string b64(const std::vector<uint8_t>& d)
    {
        static const char* A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string o;
        o.reserve((d.size() * 4 + 2) / 3);
        size_t i = 0;
        for (; i + 2 < d.size(); i += 3)
        {
            uint32_t v = (d[i] << 16) | (d[i + 1] << 8) | d[i + 2];
            o += A[v >> 18]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63]; o += A[v & 63];
        }
        if (i + 1 == d.size())
        {
            uint32_t v = d[i] << 16;
            o += A[v >> 18]; o += A[(v >> 12) & 63];
        }
        else if (i + 2 == d.size())
        {
            uint32_t v = (d[i] << 16) | (d[i + 1] << 8);
            o += A[v >> 18]; o += A[(v >> 12) & 63]; o += A[(v >> 6) & 63];
        }
        return o;
    }
    inline bool unb64(const std::string& s, std::vector<uint8_t>& d)
    {
        d.clear();
        uint32_t acc = 0;
        int bits = 0;
        for (char c : s)
        {
            int v;
            if (c >= 'A' && c <= 'Z') v = c - 'A';
            else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
            else if (c >= '0' && c <= '9') v = c - '0' + 52;
            else if (c == '+') v = 62;
            else if (c == '/') v = 63;
            else return false;
            acc = (acc << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8)
            {
                bits -= 8;
                d.push_back((uint8_t)((acc >> bits) & 0xff));
            }
        }
        return true;
    }

    inline std::string encode(const Sel& s, uint8_t seq, const Tf* b, int n)
    {
        std::vector<uint8_t> d(payload_bytes(s));
        d[0] = (uint8_t)kVer;
        d[1] = seq;
        d[2] = (uint8_t)s.idx.size();
        for (int k = 0; k < 4; k++) d[3 + k] = (uint8_t)(s.hash >> (8 * k));
        size_t o = kHeader;
        for (size_t k = 0; k < s.idx.size(); k++)
        {
            int i = s.idx[k];
            if (i >= n) return "";
            pack_q(b[i].q, &d[o]);
            o += 6;
            if (s.kind[k]) { pack_t(b[i].t, &d[o]); o += 6; }
        }
        return b64(d);
    }
    inline bool header(const std::string& str, uint8_t& seq, int& cnt, uint32_t& hash)
    {
        std::vector<uint8_t> d;
        if (str.size() < 12 || !unb64(str.substr(0, 12), d) || d.size() < (size_t)kHeader || d[0] != kVer) return false;
        seq = d[1];
        cnt = d[2];
        hash = (uint32_t)d[3] | ((uint32_t)d[4] << 8) | ((uint32_t)d[5] << 16) | ((uint32_t)d[6] << 24);
        return true;
    }
    inline bool decode(const std::string& str, const Sel& s, Pose& p)
    {
        std::vector<uint8_t> d;
        if (!unb64(str, d) || d.size() != payload_bytes(s) || d[0] != kVer) return false;
        p.q.assign(s.idx.size(), {0, 0, 0, 1});
        p.t.assign(s.idx.size(), {0, 0, 0});
        size_t o = kHeader;
        for (size_t k = 0; k < s.idx.size(); k++)
        {
            unpack_q(&d[o], p.q[k].data());
            o += 6;
            if (s.kind[k]) { unpack_t(&d[o], p.t[k].data()); o += 6; }
        }
        return true;
    }

    inline bool sample(const std::deque<Pose>& ring, double rt, Pose& out)
    {
        if (ring.empty()) return false;
        const Pose* a = &ring.front();
        const Pose* b = &ring.back();
        if (rt <= a->at) { out = *a; return true; }
        if (rt >= b->at) { out = *b; return true; }
        for (size_t i = 1; i < ring.size(); i++)
            if (ring[i].at >= rt) { a = &ring[i - 1]; b = &ring[i]; break; }
        double span = b->at - a->at;
        double w = span > 1e-6 ? (rt - a->at) / span : 1.0;
        out.at = rt;
        out.q.resize(a->q.size());
        out.t.resize(a->t.size());
        for (size_t k = 0; k < a->q.size(); k++)
        {
            qnlerp(a->q[k].data(), b->q[k].data(), w, out.q[k].data());
            for (int c = 0; c < 3; c++) out.t[k][c] = a->t[k][c] * (1 - w) + b->t[k][c] * w;
        }
        return true;
    }

    inline double weight(double now, double first, double last)
    {
        double up = std::max(0.0, std::min(1.0, (now - first) / 0.15));
        double dn = std::max(0.0, std::min(1.0, 1.0 - ((now - last) - 0.35) / 0.15));
        return std::min(up, dn);
    }

    inline void apply(Tf* b, int n, const std::vector<int>& par, const Sel& s, const Pose& p, double w, double yawDeg)
    {
        if (n <= 0 || w <= 0 || p.q.size() != s.idx.size()) return;
        thread_local std::vector<std::array<double, 4>> oq, nq;
        thread_local std::vector<std::array<double, 3>> ot, nt;
        thread_local std::vector<int> pos;
        thread_local std::vector<char> ch;
        oq.resize(n); nq.resize(n); ot.resize(n); nt.resize(n);
        pos.assign(n, -1);
        ch.assign(n, 0);
        for (size_t k = 0; k < s.idx.size(); k++)
            if (s.idx[k] < n) pos[s.idx[k]] = (int)k;
        double a = yawDeg * 3.14159265358979323846 / 180.0;
        double zq[4] = {0, 0, std::sin(a * 0.5), std::cos(a * 0.5)};
        for (int i = 0; i < n; i++)
        {
            for (int c = 0; c < 4; c++) oq[i][c] = nq[i][c] = b[i].q[c];
            for (int c = 0; c < 3; c++) ot[i][c] = nt[i][c] = b[i].t[c];
        }
        for (int i = 1; i < n; i++)
        {
            int pp = par[i];
            int k = pos[i];
            if (k >= 0)
            {
                double tq[4];
                if (a != 0) qmul(zq, p.q[k].data(), tq);
                else std::memcpy(tq, p.q[k].data(), sizeof tq);
                qnlerp(oq[i].data(), tq, w, nq[i].data());
                if (s.kind[k] || pp < 0)
                {
                    double tt[3];
                    if (a != 0) qrot(zq, p.t[k].data(), tt);
                    else std::memcpy(tt, p.t[k].data(), sizeof tt);
                    for (int c = 0; c < 3; c++) nt[i][c] = ot[i][c] * (1 - w) + tt[c] * w;
                }
                else
                {
                    double ic[4], d[3], loc[3], r[3];
                    qconj(oq[pp].data(), ic);
                    for (int c = 0; c < 3; c++) d[c] = ot[i][c] - ot[pp][c];
                    qrot(ic, d, loc);
                    qrot(nq[pp].data(), loc, r);
                    for (int c = 0; c < 3; c++) nt[i][c] = nt[pp][c] + r[c];
                }
                ch[i] = 1;
            }
            else if (pp >= 0 && ch[pp])
            {
                double ic[4], dq[4], d[3], r[3];
                qconj(oq[pp].data(), ic);
                qmul(nq[pp].data(), ic, dq);
                qmul(dq, oq[i].data(), nq[i].data());
                qnorm(nq[i].data());
                for (int c = 0; c < 3; c++) d[c] = ot[i][c] - ot[pp][c];
                qrot(dq, d, r);
                for (int c = 0; c < 3; c++) nt[i][c] = nt[pp][c] + r[c];
                ch[i] = 1;
            }
        }
        for (int i = 1; i < n; i++)
        {
            if (!ch[i]) continue;
            for (int c = 0; c < 4; c++) b[i].q[c] = nq[i][c];
            for (int c = 0; c < 3; c++) b[i].t[c] = nt[i][c];
        }
    }
}
