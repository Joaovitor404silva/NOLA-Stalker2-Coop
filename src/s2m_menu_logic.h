#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "s2m_text.h"
#include "s2r_net.h"

namespace nola
{
    enum class Mode : uint8_t { none = 0, host = 1, join = 2 };

    inline const char* mode_name(Mode m) { return m == Mode::host ? "host" : m == Mode::join ? "join" : "none"; }

    inline std::vector<std::string> peers(const std::string& s)
    {
        std::vector<std::string> r;
        std::string cur;
        auto flush = [&] {
            if (!cur.empty())
            {
                bool seen = false;
                for (auto& x : r) seen = seen || x == cur;
                if (!seen) r.push_back(cur);
            }
            cur.clear();
        };
        for (char c : s)
        {
            if (c == ',' || c == ';' || c == ' ' || c == '\t') flush();
            else cur += c;
        }
        flush();
        return r;
    }

    inline size_t peer_pick(size_t n, size_t cur, double now, double started, double last_rx, double wait)
    {
        if (n < 2) return 0;
        cur %= n;
        if (now - started < wait) return cur;
        if (last_rx >= started && now - last_rx < wait) return cur;
        return (cur + 1) % n;
    }

    struct Entry
    {
        std::string cls, sid;
    };

    struct Plan
    {
        int at = -1;
        int tmpl = -1;
    };

    inline Plan plan(const std::vector<Entry>& es, int which)
    {
        Plan p;
        if ((which != 1 && which != 2) || es.empty()) return p;
        int n = (int)es.size();
        int c = -1;
        for (int i = 0; i < n; ++i)
            if (es[(size_t)i].cls == "LoadLastSaveAction") { c = i; break; }
        p.at = c >= 0 ? c + 1 : 0;
        p.tmpl = p.at < n ? p.at : n - 1;
        return p;
    }

    inline std::vector<int> shown(bool both, bool host, bool client)
    {
        if (both) return {1, 2};
        if (host) return {1};
        if (client) return {2};
        return {};
    }

    struct Step
    {
        bool end_first = false;
        bool disarm = false;
        bool arm_host = false;
        bool join = false;
    };

    inline Step click(Mode cur, int which)
    {
        Step s;
        if (which == 1)
        {
            if (cur == Mode::host) s.disarm = true;
            else
            {
                s.end_first = cur == Mode::join;
                s.arm_host = true;
            }
        }
        else if (which == 2)
        {
            if (cur == Mode::join) s.disarm = true;
            else
            {
                s.end_first = cur == Mode::host;
                s.join = true;
            }
        }
        return s;
    }

    inline const char* role_of(Mode m) { return m == Mode::host ? "host" : m == Mode::join ? "client" : ""; }

    inline std::string fmt(const std::string& t, const std::string& name, int n, int max, const std::string& why = {})
    {
        const std::pair<const char*, std::string> keys[] = {{"{name}", name}, {"{n}", std::to_string(n)}, {"{max}", std::to_string(max)}, {"{why}", why}};
        std::string o;
        size_t i = 0;
        while (i < t.size())
        {
            bool hit = false;
            for (auto& [k, v] : keys)
            {
                size_t kl = std::char_traits<char>::length(k);
                if (t.compare(i, kl, k) == 0)
                {
                    o += v;
                    i += kl;
                    hit = true;
                    break;
                }
            }
            if (!hit) o += t[i++];
        }
        return o;
    }

    enum class Why : uint8_t { ok = 0, none, full };

    struct Pick
    {
        int i = -1;
        Why why = Why::none;
    };

    inline Pick choose(const std::vector<s2r::disc::Host>& v, const std::vector<std::string>& pref)
    {
        Pick p;
        if (v.empty()) return p;
        p.i = s2r::disc::pick(v, pref);
        p.why = p.i < 0 ? Why::full : Why::ok;
        return p;
    }
}
