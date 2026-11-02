#pragma once
#include <cstdint>
#include <cstdio>
#include <tuple>
#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

#ifndef S2X_RELEASE
extern "C" __declspec(dllimport) int __stdcall SetEnvironmentVariableA(const char*, const char*);
extern "C" __declspec(dllimport) unsigned long __stdcall GetEnvironmentVariableA(const char*, char*, unsigned long);
#endif

namespace x0::hot::logic
{
    inline std::string trim(std::string s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return {};
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    inline bool enabled(std::string_view flags)
    {
        bool on = true;
        size_t i = 0;
        while (i <= flags.size())
        {
            size_t e = flags.find('\n', i);
            if (e == std::string_view::npos) e = flags.size();
            std::string l = trim(std::string(flags.substr(i, e - i)));
            if (l.rfind("hotreload=", 0) == 0) on = trim(l.substr(10)) != "0";
            i = e + 1;
        }
        return on;
    }

    inline std::wstring copy_name(uint32_t pid, int gen) { return L"hot_" + std::to_wstring(pid) + L"_" + std::to_wstring(gen) + L".dll"; }

    inline bool is_copy_name(std::wstring_view n)
    {
        if (n.size() < 11 || n.substr(0, 4) != L"hot_" || n.substr(n.size() - 4) != L".dll") return false;
        std::wstring_view mid = n.substr(4, n.size() - 8);
        size_t u = mid.find(L'_');
        if (u == std::wstring_view::npos || u == 0 || u + 1 >= mid.size()) return false;
        for (size_t k = 0; k < mid.size(); ++k)
            if (k != u && !std::iswdigit(mid[k])) return false;
        return true;
    }

    struct Kept
    {
        bool ok = false;
        long long seq = 0;
        std::vector<std::string> keys;
    };

    inline std::string pack(long long seq, const std::vector<std::string>& keys)
    {
        std::string s = "v1 " + std::to_string(seq);
        for (auto& k : keys)
            if (!k.empty() && k.find('\n') == std::string::npos)
            {
                s += '\n';
                s += k;
            }
        return s;
    }

    inline Kept unpack(std::string_view s)
    {
        Kept r;
        if (s.substr(0, 3) != "v1 ") return r;
        size_t e = s.find('\n');
        std::string head(s.substr(3, e == std::string_view::npos ? std::string_view::npos : e - 3));
        if (head.empty() || head.size() > 18 || head.find_first_not_of("0123456789") != std::string::npos) return r;
        r.seq = std::stoll(head);
        r.ok = true;
        while (e != std::string_view::npos)
        {
            size_t a = e + 1;
            e = s.find('\n', a);
            std::string_view k = s.substr(a, e == std::string_view::npos ? std::string_view::npos : e - a);
            if (!k.empty()) r.keys.emplace_back(k);
        }
        return r;
    }

    struct QiX
    {
        bool based = false;
        std::vector<std::tuple<std::string, double, int>> sent;
        std::vector<std::tuple<std::string, bool, double>> out;
    };

    inline bool qix_field_ok(const std::string& s) { return !s.empty() && s.find_first_of("\t\n") == std::string::npos; }

    inline std::string qix_num(double d)
    {
        char b[40];
        std::snprintf(b, sizeof b, "%.17g", d);
        return b;
    }

    inline std::string qix_pack(const QiX& x)
    {
        std::vector<std::string> l;
        l.push_back(x.based ? "B\t1" : "B\t0");
        for (auto& [sid, at, n] : x.sent)
            if (qix_field_ok(sid)) l.push_back("S\t" + sid + "\t" + qix_num(at) + "\t" + std::to_string(n));
        for (auto& [e, has, t] : x.out)
            if (qix_field_ok(e)) l.push_back("O\t" + e + "\t" + (has ? qix_num(t) : std::string("-")));
        return pack(0, l);
    }

    inline std::vector<std::string> qix_split(const std::string& s)
    {
        std::vector<std::string> p;
        size_t a = 0;
        for (;;)
        {
            size_t e = s.find('\t', a);
            p.push_back(s.substr(a, e == std::string::npos ? std::string::npos : e - a));
            if (e == std::string::npos) return p;
            a = e + 1;
        }
    }

    inline QiX qix_unpack(std::string_view s, bool& ok)
    {
        QiX x;
        Kept k = unpack(s);
        ok = k.ok;
        for (auto& l : k.keys)
        {
            auto p = qix_split(l);
            try
            {
                if (p[0] == "B" && p.size() == 2) x.based = p[1] == "1";
                else if (p[0] == "S" && p.size() == 4) x.sent.emplace_back(p[1], std::stod(p[2]), std::stoi(p[3]));
                else if (p[0] == "O" && p.size() == 3) x.out.emplace_back(p[1], p[2] != "-", p[2] == "-" ? 0.0 : std::stod(p[2]));
            }
            catch (...) {}
        }
        return x;
    }

    template <class M> std::vector<std::string> keys_of(const M& m, const std::string& pre = {})
    {
        std::vector<std::string> o;
        for (auto& kv : m) o.push_back(pre + kv.first);
        return o;
    }

    inline std::string env_name(const char* name) { return std::string("NOLA_HOT_") + name; }

#ifndef S2X_RELEASE
    inline std::string env_get(const std::string& n)
    {
        unsigned long sz = ::GetEnvironmentVariableA(n.c_str(), nullptr, 0);
        if (sz == 0) return {};
        std::string o(sz, '\0');
        unsigned long r = ::GetEnvironmentVariableA(n.c_str(), o.data(), sz);
        o.resize(r < sz ? r : 0);
        return o;
    }

    inline long long cur_gen()
    {
        std::string g = env_get(env_name("GEN"));
        if (g.empty() || g.size() > 9 || g.find_first_not_of("0123456789") != std::string::npos) return 0;
        return std::stoll(g);
    }
#endif

    inline bool slot_matches(const void* at_slot, const void* f, int idx) { return f != nullptr && idx >= 0 && at_slot == f; }

    inline bool keep(const char* name, const std::string& v)
    {
#ifdef S2X_RELEASE
        (void)name;
        (void)v;
        return true;
#else
        constexpr size_t kMax = 32000;
        bool whole = v.size() <= kMax;
        std::string c = whole ? v : v.substr(0, v.rfind('\n', kMax));
        ::SetEnvironmentVariableA(env_name(name).c_str(), (std::to_string(cur_gen()) + "\n" + c).c_str());
        return whole;
#endif
    }

    inline std::string take(const char* name)
    {
#ifdef S2X_RELEASE
        (void)name;
        return {};
#else
        std::string n = env_name(name);
        std::string o = env_get(n);
        size_t e = o.find('\n');
        if (e == std::string::npos || e == 0 || e > 9 || o.find_first_not_of("0123456789") < e) return {};
        if (std::stoll(o.substr(0, e)) >= cur_gen()) return {};
        ::SetEnvironmentVariableA(n.c_str(), nullptr);
        return o.substr(e + 1);
#endif
    }
}
