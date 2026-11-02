#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace qobj
{
    struct Row
    {
        std::string quest;
        std::string node;
    };
    using Table = std::unordered_map<std::string, Row>;

    struct CacheHit
    {
        std::string name;
        const Row* row = nullptr;
    };
    using RowCache = std::unordered_map<uint64_t, CacheHit>;

    inline bool cache_get(const RowCache& c, uint64_t addr, const std::string& name, const Row*& out)
    {
        auto it = c.find(addr);
        if (it == c.end() || it->second.name != name) return false;
        out = it->second.row;
        return true;
    }

    inline void cache_put(RowCache& c, uint64_t addr, const std::string& name, const Row* row)
    {
        if (c.size() >= 4096 && !c.count(addr)) c.clear();
        c[addr] = CacheHit{name, row};
    }

    inline std::string guid_norm(const std::string& g)
    {
        std::string s;
        for (char c : g)
        {
            if (c == '-' || c == '{' || c == '}' || c == ' ') continue;
            s += (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
        }
        return s.size() == 32 ? s : std::string();
    }

    inline int parse(const std::string& text, Table& out)
    {
        int n = 0;
        size_t i = 0;
        bool first = true;
        while (i < text.size())
        {
            size_t e = text.find('\n', i);
            if (e == std::string::npos) e = text.size();
            std::string ln = text.substr(i, e - i);
            i = e + 1;
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (first) { first = false; continue; }
            size_t a = ln.find('\t');
            if (a == std::string::npos) continue;
            size_t b = ln.find('\t', a + 1);
            std::string g = guid_norm(ln.substr(0, a));
            if (g.empty()) continue;
            Row r;
            r.quest = ln.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
            r.node = b == std::string::npos ? std::string() : ln.substr(b + 1);
            if (r.quest.empty()) continue;
            out[g] = r;
            ++n;
        }
        return n;
    }

    struct Gate
    {
        int blocked = 0;
        int warns = 0;
        double warnAt = -1e9;
    };

    inline const Row* lookup(const Table& t, const std::string& guid)
    {
        auto it = t.find(guid_norm(guid));
        return it == t.end() ? nullptr : &it->second;
    }

    inline std::string guid_hex(int32_t a, int32_t b, int32_t c, int32_t d)
    {
        char buf[40];
        std::snprintf(buf, sizeof buf, "%08X%08X%08X%08X", (uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d);
        return buf;
    }

    inline const Row* lookup_any(const Table& t, const std::vector<std::string>& guids)
    {
        for (const std::string& g : guids)
            if (const Row* r = lookup(t, g)) return r;
        return nullptr;
    }

    inline bool block(bool flagOn, bool isClient, const Row* row, bool questMuted)
    {
        return flagOn && isClient && row && questMuted;
    }

    inline bool warn_due(Gate& g, double now, double gap)
    {
        ++g.blocked;
        if (now - g.warnAt < gap) return false;
        g.warnAt = now;
        ++g.warns;
        return true;
    }

    struct ObjState
    {
        int hid = 0;
        int en = 1;
        int br = 0;
        int st = -1;
    };

    struct Step
    {
        std::string fn;
        bool arg = false;
    };

    inline bool skip_sync(bool flagOn, bool objective) { return flagOn && objective; }

    inline std::string enc(const ObjState& s)
    {
        std::string o = std::to_string(s.hid ? 1 : 0) + "," + std::to_string(s.en ? 1 : 0) + "," + std::to_string(s.br ? 1 : 0) + ",";
        return o + (s.st < 0 ? std::string("-") : std::to_string(s.st));
    }

    inline bool dec(const std::string& t, ObjState& o)
    {
        std::vector<std::string> f;
        size_t i = 0;
        while (true)
        {
            size_t e = t.find(',', i);
            f.push_back(t.substr(i, e == std::string::npos ? std::string::npos : e - i));
            if (e == std::string::npos) break;
            i = e + 1;
        }
        if (f.size() != 4) return false;
        int v[3];
        for (int k = 0; k < 3; ++k)
        {
            if (f[(size_t)k] != "0" && f[(size_t)k] != "1") return false;
            v[k] = f[(size_t)k] == "1";
        }
        if (f[3] == "-") o.st = -1;
        else
        {
            if (f[3].empty() || f[3].size() > 2) return false;
            for (char c : f[3]) if (c < '0' || c > '9') return false;
            o.st = std::stoi(f[3]);
        }
        o.hid = v[0];
        o.en = v[1];
        o.br = v[2];
        return true;
    }

    inline std::string wire(const std::string& key, const ObjState& s) { return key + ";" + enc(s); }

    inline bool wire_parse(const std::string& ent, std::string& key, ObjState& s)
    {
        size_t sc = ent.find(';');
        if (sc == std::string::npos || sc == 0) return false;
        key = ent.substr(0, sc);
        for (char c : key) if (!((c >= '0' && c <= '9') || c == ',' || c == '-')) return false;
        return dec(ent.substr(sc + 1), s);
    }

    inline std::string line(const std::string& cls, const std::string& key, const ObjState& s)
    {
        return "NOLA objstate " + cls + " " + key + " vis=" + std::to_string(s.hid ? 0 : 1) + " ativo=" + std::to_string(s.en ? 1 : 0) +
               " quebrado=" + std::to_string(s.br ? 1 : 0) + " estado=" + std::to_string(s.st);
    }

    inline int plan(const ObjState& have, const ObjState& want, std::vector<Step>& calls)
    {
        if ((have.hid != 0) != (want.hid != 0)) calls.push_back({"SetActorHiddenInGame", want.hid != 0});
        if ((have.en != 0) != (want.en != 0)) calls.push_back({"SetEnabled", want.en != 0});
        if ((have.br != 0) != (want.br != 0)) calls.push_back({"SetBroken", want.br != 0});
        if (have.st < 0 || want.st < 0 || want.st <= have.st) return 0;
        return want.st - have.st;
    }
}
