#include "s2x_istate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace s2x_is
{
    namespace
    {
        auto isW(char c) -> bool { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
        auto split(const std::string& s, char sep) -> std::vector<std::string>
        {
            std::vector<std::string> r;
            size_t p = 0;
            while (true)
            {
                size_t e = s.find(sep, p);
                r.push_back(s.substr(p, e == std::string::npos ? std::string::npos : e - p));
                if (e == std::string::npos) break;
                p = e + 1;
            }
            return r;
        }
        auto lst(const std::vector<std::string>& v) -> std::string
        {
            std::string r;
            for (size_t i = 0; i < v.size(); ++i) { if (i) r += '.'; r += v[i]; }
            return r;
        }
        auto unl(const std::string& s, std::vector<std::string>& out) -> bool
        {
            out.clear();
            if (s.empty()) return true;
            for (auto& x : split(s, '.'))
            {
                if (!sidOk(x)) return false;
                out.push_back(x);
            }
            return true;
        }
        auto sorted(std::vector<std::string> v) -> std::vector<std::string>
        {
            std::sort(v.begin(), v.end());
            return v;
        }
        auto digits(const std::string& s, bool neg) -> bool
        {
            if (s.empty()) return false;
            for (size_t i = 0; i < s.size(); ++i)
                if (!((s[i] >= '0' && s[i] <= '9') || (neg && i == 0 && s[i] == '-' && s.size() > 1))) return false;
            return true;
        }
        auto hex8(const std::string& s) -> bool
        {
            if (s.size() != 8) return false;
            for (char c : s)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            return true;
        }
        auto durDiff(const IState& a, const IState& b) -> double
        {
            if (a.hasDur != b.hasDur) return 1.0;
            if (!a.hasDur) return 0.0;
            return std::fabs((double)durF(a.dur) - (double)durF(b.dur));
        }
    }

    auto IState::plain() const -> bool { return !hasDur && upg.empty() && att.empty() && !hasAmmo && ext.empty(); }

    auto durOf(float f) -> uint32_t
    {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    }
    auto durF(uint32_t bits) -> float
    {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }

    auto sidOk(const std::string& s) -> bool
    {
        if (s.empty() || s.size() > 96) return false;
        for (char c : s)
            if (!isW(c)) return false;
        return true;
    }

    auto textOk(const std::string& s) -> bool
    {
        if (s.size() > kMaxText) return false;
        for (char c : s)
            if (!(isW(c) || c == '~' || c == '.' || c == '-' || c == '=' || c == ',')) return false;
        return true;
    }

    auto enc(const IState& s) -> std::string
    {
        std::vector<std::string> f;
        if (s.hasDur)
        {
            char b[16];
            std::snprintf(b, sizeof b, "d%08x", s.dur);
            f.push_back(b);
        }
        if (!s.upg.empty()) f.push_back("u" + lst(sorted(s.upg)));
        if (!s.att.empty()) f.push_back("a" + lst(s.att));
        if (s.hasAmmo) f.push_back("r" + std::to_string(s.rounds) + "." + s.ammo);
        for (auto& e : s.ext) f.push_back(std::string(1, e.first) + e.second);
        std::string r;
        for (size_t i = 0; i < f.size(); ++i) { if (i) r += '~'; r += f[i]; }
        return r;
    }

    auto dec(const std::string& text, IState& out) -> bool
    {
        IState s;
        s.sid = out.sid;
        if (text.empty()) { out = s; return true; }
        if (!textOk(text)) return false;
        for (auto& f : split(text, '~'))
        {
            if (f.empty()) return false;
            char t = f[0];
            std::string v = f.substr(1);
            if (t == 'd')
            {
                if (!hex8(v) || s.hasDur) return false;
                s.hasDur = true;
                s.dur = (uint32_t)std::strtoul(v.c_str(), nullptr, 16);
            }
            else if (t == 'u') { if (v.empty() || !unl(v, s.upg)) return false; }
            else if (t == 'a') { if (v.empty() || !unl(v, s.att)) return false; }
            else if (t == 'r')
            {
                size_t p = v.find('.');
                if (p == std::string::npos || s.hasAmmo) return false;
                std::string n = v.substr(0, p), a = v.substr(p + 1);
                if (!digits(n, true) || (!a.empty() && !sidOk(a))) return false;
                s.hasAmmo = true;
                s.rounds = (int32_t)std::strtol(n.c_str(), nullptr, 10);
                s.ammo = a;
            }
            else if (isW(t) && t != '_')
            {
                for (char c : v)
                    if (!(isW(c) || c == '.' || c == '-')) return false;
                s.ext.push_back({t, v});
            }
            else return false;
        }
        out = s;
        return true;
    }

    auto encSid(const IState& s) -> std::string { return s.sid + "=" + enc(s); }

    auto decSid(const std::string& text, IState& out) -> bool
    {
        size_t p = text.find('=');
        if (p == std::string::npos) return false;
        IState s;
        s.sid = text.substr(0, p);
        if (!sidOk(s.sid) || !dec(text.substr(p + 1), s)) return false;
        out = s;
        return true;
    }

    auto encList(const std::vector<IState>& v) -> std::string
    {
        std::string r;
        for (auto& s : v)
        {
            std::string e = encSid(s);
            if (r.size() + e.size() + 1 > kMaxText) break;
            if (!r.empty()) r += ',';
            r += e;
        }
        return r;
    }

    auto decList(const std::string& text, std::vector<IState>& out) -> bool
    {
        out.clear();
        if (text.empty()) return true;
        if (!textOk(text)) return false;
        for (auto& e : split(text, ','))
        {
            IState s;
            if (!decSid(e, s)) return false;
            out.push_back(s);
        }
        return true;
    }

    auto same(const IState& a, const IState& b) -> bool
    {
        return a.sid == b.sid && a.hasDur == b.hasDur && durDiff(a, b) <= 1e-4 && sorted(a.upg) == sorted(b.upg) && sorted(a.att) == sorted(b.att) &&
               a.hasAmmo == b.hasAmmo && (!a.hasAmmo || (a.rounds == b.rounds && a.ammo == b.ammo)) && a.ext == b.ext;
    }

    auto score(const IState& want, const IState& have) -> int
    {
        if (want.sid != have.sid) return -1;
        int s = 0;
        if (sorted(want.upg) == sorted(have.upg)) s += 1000;
        if (sorted(want.att) == sorted(have.att)) s += 1000;
        if (want.hasAmmo == have.hasAmmo && (!want.hasAmmo || (want.rounds == have.rounds && want.ammo == have.ammo))) s += 100;
        if (want.ext == have.ext) s += 100;
        double d = durDiff(want, have);
        s += d <= 1e-4 ? 1000 : (int)(900.0 * (1.0 - std::min(1.0, d)));
        return s;
    }

    auto pick(const IState& want, const std::vector<Inst>& cands, const std::vector<bool>& used) -> int
    {
        int best = -1, bs = -1;
        for (size_t i = 0; i < cands.size(); ++i)
        {
            if (i < used.size() && used[i]) continue;
            int s = score(want, cands[i].st);
            if (s > bs) { bs = s; best = (int)i; }
        }
        return best;
    }

    auto show(const IState& s) -> std::string
    {
        std::string r = s.sid;
        if (s.hasDur)
        {
            char b[32];
            std::snprintf(b, sizeof b, " dur=%.4f", (double)durF(s.dur));
            r += b;
        }
        if (!s.upg.empty()) r += " upg=" + lst(sorted(s.upg));
        if (!s.att.empty()) r += " att=" + lst(s.att);
        if (s.hasAmmo) r += " ammo=" + std::to_string(s.rounds) + "x" + (s.ammo.empty() ? std::string("-") : s.ammo);
        for (auto& e : s.ext) r += std::string(" ") + e.first + "=" + e.second;
        return r;
    }

    auto expand(const std::vector<Inst>& v, bool statefulOnly) -> std::vector<IState>
    {
        std::vector<IState> r;
        for (auto& i : v)
        {
            if (statefulOnly && i.st.plain()) continue;
            for (long long k = 0; k < std::min<long long>(i.n, 256); ++k) r.push_back(i.st);
        }
        return r;
    }

    auto ofSid(const std::vector<IState>& v, const std::string& sid) -> std::vector<IState>
    {
        std::vector<IState> r;
        for (auto& s : v)
            if (s.sid == sid) r.push_back(s);
        return r;
    }

    auto minus(const std::vector<IState>& a, const std::vector<IState>& b) -> std::vector<IState>
    {
        std::vector<IState> r = a;
        for (auto& y : b)
            for (size_t i = 0; i < r.size(); ++i)
                if (same(r[i], y))
                {
                    r.erase(r.begin() + (long)i);
                    break;
                }
        return r;
    }

    auto common(const std::vector<IState>& a, const std::vector<IState>& b) -> std::vector<IState>
    {
        std::vector<IState> r;
        std::vector<bool> u(b.size(), false);
        for (auto& x : a)
            for (size_t i = 0; i < b.size(); ++i)
                if (!u[i] && same(x, b[i]))
                {
                    u[i] = true;
                    r.push_back(x);
                    break;
                }
        return r;
    }

    auto plan(const std::vector<IState>& want, const std::vector<Inst>& have) -> Plan
    {
        Plan p;
        for (auto& h : have) p.left.push_back(std::max<long long>(0, h.n));
        std::vector<bool> got(want.size(), false);
        for (size_t w = 0; w < want.size(); ++w)
            for (size_t i = 0; i < have.size(); ++i)
                if (p.left[i] > 0 && same(want[w], have[i].st))
                {
                    p.left[i]--;
                    p.keep.push_back((int)i);
                    got[w] = true;
                    break;
                }
        for (size_t w = 0; w < want.size(); ++w)
        {
            if (got[w]) continue;
            int best = -1;
            long long bs = -1;
            for (size_t i = 0; i < have.size(); ++i)
            {
                if (p.left[i] <= 0 || have[i].st.sid != want[w].sid) continue;
                long long s = (have[i].st.plain() ? 1000000 : 0) + score(want[w], have[i].st);
                if (s > bs) { bs = s; best = (int)i; }
            }
            if (best < 0) p.create.push_back(want[w]);
            else
            {
                p.left[(size_t)best]--;
                p.apply.push_back({best, want[w]});
            }
        }
        return p;
    }

    auto pickRm(const IState& want, const std::vector<Inst>& have, std::vector<long long>& left) -> int
    {
        int best = -1, bs = -1;
        for (size_t i = 0; i < have.size(); ++i)
        {
            if (i >= left.size() || left[i] <= 0) continue;
            int s = same(want, have[i].st) ? 1000000 : score(want, have[i].st);
            if (s > bs) { bs = s; best = (int)i; }
        }
        if (best >= 0) left[(size_t)best]--;
        return best;
    }

    auto pickSpare(const std::string& sid, const std::vector<Inst>& have, std::vector<long long>& left) -> int
    {
        int best = -1;
        for (size_t i = 0; i < have.size(); ++i)
        {
            if (i >= left.size() || left[i] <= 0 || have[i].st.sid != sid) continue;
            if (best < 0 || (have[i].st.plain() && !have[(size_t)best].st.plain())) best = (int)i;
        }
        if (best >= 0) left[(size_t)best]--;
        return best;
    }
}
