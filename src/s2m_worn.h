#pragma once
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace s2m_worn
{
    using Worn = std::map<int, std::string>;

    inline bool sidOk(const std::string& s)
    {
        if (s.empty() || s.size() > 95) return false;
        for (char c : s)
            if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
        return true;
    }

    inline Worn parse(const std::string& s)
    {
        Worn w;
        size_t p = 0;
        while (p < s.size())
        {
            size_t e = s.find(',', p);
            std::string t = s.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? s.size() : e + 1;
            size_t q = t.find('=');
            if (q == std::string::npos || q == 0) continue;
            char* end = nullptr;
            long slot = std::strtol(t.c_str(), &end, 10);
            if (end != t.c_str() + q || slot < 1 || slot > 16) continue;
            std::string sid = t.substr(q + 1);
            if (sidOk(sid)) w[(int)slot] = sid;
        }
        return w;
    }

    inline std::string str(const Worn& w)
    {
        std::string o;
        for (auto& [s, sid] : w)
        {
            if (!o.empty()) o += ",";
            o += std::to_string(s) + "=" + sid;
        }
        return o;
    }

    struct Plan
    {
        std::vector<int> off;
        std::vector<std::pair<int, std::string>> on;
        std::vector<std::string> kept;
    };

    inline Plan plan(const Worn& cur, const Worn& want)
    {
        Plan p;
        for (auto& [s, sid] : cur)
        {
            auto it = want.find(s);
            if (it != want.end() && it->second == sid) p.kept.push_back(sid);
            else p.off.push_back(s);
        }
        for (auto& [s, sid] : want)
        {
            auto it = cur.find(s);
            if (it == cur.end() || it->second != sid) p.on.push_back({s, sid});
        }
        return p;
    }
}
