#pragma once
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "s2m_savefile.h"

namespace lsh
{
    enum class Why : uint8_t { hold = 0, off, host, norelay, joined, notload, nobegin, badbuf };

    inline const char* why_name(Why w)
    {
        switch (w)
        {
        case Why::hold: return "hold";
        case Why::off: return "off";
        case Why::host: return "host";
        case Why::norelay: return "norelay";
        case Why::joined: return "joined";
        case Why::notload: return "notload";
        case Why::nobegin: return "nobegin";
        case Why::badbuf: return "badbuf";
        }
        return "?";
    }

    struct Gate
    {
        int64_t flag = 1, sxjoin = 0, wpkg = 1;
        bool client = false, relay = false, joined = false;
        bool loadDelegate = false;
        bool loadBegan = false;
        uint8_t code = 0;
        uint64_t rawNum = 0;
        uint32_t rawHead = 0;
    };

    inline Why decide(const Gate& g)
    {
        if (g.flag != 1 || g.sxjoin != 1 || g.wpkg != 1) return Why::off;
        if (!g.client) return Why::host;
        if (!g.loadDelegate) return Why::notload;
        if (!g.loadBegan) return Why::nobegin;
        if (g.code != 0 || g.rawNum < 1024 || !s2m_savefile::known_version(g.rawHead)) return Why::badbuf;
        if (!g.relay) return Why::norelay;
        if (g.joined) return Why::joined;
        return Why::hold;
    }

    inline bool strip_wanted(const Gate& g, int64_t flag)
    {
        return flag == 1 && g.loadDelegate && g.loadBegan && g.code == 0 && g.rawNum >= 1024 && s2m_savefile::known_version(g.rawHead);
    }

    inline long long epoch_ms()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

    enum class Step : uint8_t { wait = 0, timeout, silent };

    inline Step step(double t, double t0, double wait, double rxAt, double silentMax)
    {
        if (t - t0 > wait) return Step::timeout;
        if (t - rxAt > silentMax) return Step::silent;
        return Step::wait;
    }

    struct Ap
    {
        std::optional<std::string> cid, w;
        int n = 0;
        bool stamped = false;
    };

    inline bool word(const std::string& s)
    {
        if (s.empty()) return false;
        for (char c : s)
            if (!std::isalnum((unsigned char)c)) return false;
        return true;
    }

    inline Ap parse_ap(const std::string& d)
    {
        Ap a;
        std::vector<std::string> ls;
        size_t p = 0;
        while (p <= d.size())
        {
            size_t e = d.find('\n', p);
            if (e == std::string::npos) { ls.push_back(d.substr(p)); break; }
            ls.push_back(d.substr(p, e - p));
            p = e + 1;
        }
        if (!d.empty() && d.back() == '\n' && !ls.empty() && ls.back().empty()) ls.pop_back();
        const std::string fmt = "#S2CA 1";
        std::string head = ls.empty() ? std::string() : ls[0];
        std::string pre = fmt + " ";
        if (head.size() > pre.size() && head.compare(0, pre.size(), pre) == 0 && word(head.substr(pre.size()))) a.cid = head.substr(pre.size());
        if (head != fmt && !a.cid) return a;
        a.stamped = true;
        for (size_t i = 1; i < ls.size(); ++i)
        {
            const std::string& ln = ls[i];
            size_t q = 0;
            while (q < ln.size() && ln[q] >= '0' && ln[q] <= '9') ++q;
            if (q > 0 && q + 1 < ln.size() && ln[q] == '\t') a.n += std::atoi(ln.substr(0, q).c_str());
            else if (!a.w && ln.size() > 2 && ln[0] == 'W' && ln[1] == '\t' && word(ln.substr(2))) a.w = ln.substr(2);
        }
        return a;
    }

    inline bool is_other(const Ap& a, const std::string& cid, int64_t cswc)
    {
        return (a.cid && *a.cid != cid) || (!a.cid && a.n > 0) || (cswc == 1 && a.w != std::optional<std::string>(cid));
    }
}
