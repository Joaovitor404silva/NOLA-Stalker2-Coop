#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "s2m_pid.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <Windows.h>
#include <bcrypt.h>

#pragma comment(lib, "bcrypt.lib")

namespace s2m_pid
{
    namespace
    {
        constexpr const char* kPlayerHeader = "#NOLA player 1";
        constexpr const char* kCampaignHeader = "#NOLA campaign 1";
        constexpr size_t kNameChars = 32;
        constexpr const wchar_t* kPlayerFile = L"nola_player.txt";
        constexpr const wchar_t* kCampaignFile = L"campaign.txt";

        auto lower_ascii(std::string s) -> std::string
        {
            for (char& c : s)
                if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            return s;
        }

        auto iequal(const std::string& a, const std::string& b) -> bool
        {
            return lower_ascii(a) == lower_ascii(b);
        }

        auto clean(const std::string& s) -> bool
        {
            for (char ch : s)
            {
                unsigned char c = static_cast<unsigned char>(ch);
                if (c < 0x20 || c == 0x7F) return false;
            }
            return true;
        }

        auto cp_len(const std::string& s, size_t i) -> size_t
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n = 1;
            unsigned char lo = 0x80, hi = 0xBF;
            if (c < 0x80) return 1;
            if (c >= 0xC2 && c <= 0xDF) n = 2;
            else if (c >= 0xE0 && c <= 0xEF)
            {
                n = 3;
                if (c == 0xE0) lo = 0xA0;
                if (c == 0xED) hi = 0x9F;
            }
            else if (c >= 0xF0 && c <= 0xF4)
            {
                n = 4;
                if (c == 0xF0) lo = 0x90;
                if (c == 0xF4) hi = 0x8F;
            }
            else return 0;
            if (i + n > s.size()) return 0;
            unsigned char c1 = static_cast<unsigned char>(s[i + 1]);
            if (c1 < lo || c1 > hi) return 0;
            for (size_t k = 2; k < n; ++k)
                if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return 0;
            return n;
        }

        auto split_tabs(const std::string& line) -> std::vector<std::string>
        {
            std::vector<std::string> out;
            size_t b = 0;
            for (;;)
            {
                size_t t = line.find('\t', b);
                if (t == std::string::npos)
                {
                    out.push_back(line.substr(b));
                    break;
                }
                out.push_back(line.substr(b, t - b));
                b = t + 1;
            }
            return out;
        }

        auto join_path(const std::wstring& dir, const wchar_t* name) -> std::wstring
        {
            std::wstring p = dir;
            if (!p.empty() && p.back() != L'\\' && p.back() != L'/') p += L'\\';
            return p + name;
        }

        void make_dirs(const std::wstring& dir)
        {
            for (size_t i = 0; i < dir.size(); ++i)
            {
                if (dir[i] != L'\\' && dir[i] != L'/') continue;
                if (i == 0 || dir[i - 1] == L':') continue;
                CreateDirectoryW(dir.substr(0, i).c_str(), nullptr);
            }
            CreateDirectoryW(dir.c_str(), nullptr);
        }

        auto win_err(const char* what, DWORD code) -> std::string
        {
            char b[96];
            std::snprintf(b, sizeof b, "%s (win32 error %lu)", what, static_cast<unsigned long>(code));
            return b;
        }

        auto read_file(const std::wstring& path, std::string& out, std::string& err) -> Load
        {
            HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                DWORD e = GetLastError();
                if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return Load::absent;
                err = win_err("cannot open file", e);
                return Load::bad;
            }
            out.clear();
            char buf[4096];
            for (;;)
            {
                DWORD got = 0;
                if (!ReadFile(h, buf, sizeof buf, &got, nullptr))
                {
                    err = win_err("cannot read file", GetLastError());
                    CloseHandle(h);
                    return Load::bad;
                }
                if (got == 0) break;
                out.append(buf, got);
                if (out.size() > (1u << 24))
                {
                    err = "file too large";
                    CloseHandle(h);
                    return Load::bad;
                }
            }
            CloseHandle(h);
            return Load::ok;
        }

        auto write_atomic(const std::wstring& path, const std::string& bytes, bool replace, std::string& err) -> bool
        {
            std::wstring tmp = path + L".tmp";
            HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE)
            {
                err = win_err("cannot create temp file", GetLastError());
                return false;
            }
            size_t off = 0;
            while (off < bytes.size())
            {
                DWORD put = 0;
                if (!WriteFile(h, bytes.data() + off, static_cast<DWORD>(bytes.size() - off), &put, nullptr) || put == 0)
                {
                    err = win_err("cannot write temp file", GetLastError());
                    CloseHandle(h);
                    DeleteFileW(tmp.c_str());
                    return false;
                }
                off += put;
            }
            FlushFileBuffers(h);
            CloseHandle(h);
            DWORD fl = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
            if (!MoveFileExW(tmp.c_str(), path.c_str(), fl))
            {
                err = win_err("cannot move temp file into place", GetLastError());
                DeleteFileW(tmp.c_str());
                return false;
            }
            return true;
        }

        auto find_by_iid(Campaign& c, const std::string& iid) -> Member*
        {
            for (Member& m : c.members)
                if (m.iid == iid) return &m;
            return nullptr;
        }

        auto key_taken(const Campaign& c, const std::string& key) -> bool
        {
            for (const Member& m : c.members)
                if (iequal(m.key, key)) return true;
            return false;
        }

        auto reserved_stem(const std::string& s) -> size_t
        {
            size_t dot = s.find('.');
            std::string stem = lower_ascii(s.substr(0, dot));
            bool res = stem == "con" || stem == "prn" || stem == "aux" || stem == "nul";
            if (stem.size() == 4 && (stem.compare(0, 3, "com") == 0 || stem.compare(0, 3, "lpt") == 0) && stem[3] >= '1' && stem[3] <= '9') res = true;
            return res ? stem.size() : std::string::npos;
        }

        auto sanitize(const std::string& key) -> std::string
        {
            std::string o;
            for (char ch : key)
            {
                bool keep = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '_' || ch == '.' || ch == '-';
                o += keep ? ch : '_';
            }
            if (!o.empty() && (o.back() == '.' || o.back() == ' ')) o.back() = '_';
            size_t r = reserved_stem(o);
            if (r != std::string::npos) o.insert(r, "_");
            return o;
        }

        auto fnv6(const std::string& s) -> std::string
        {
            uint32_t h = 2166136261u;
            for (char c : s)
            {
                h ^= static_cast<unsigned char>(c);
                h *= 16777619u;
            }
            char b[16];
            std::snprintf(b, sizeof b, "%06x", h & 0xFFFFFFu);
            return b;
        }

        auto parse_kh(const std::string& s, int& out) -> bool
        {
            if (s.empty() || s.size() > 9) return false;
            if (s.size() > 1 && s[0] == '0') return false;
            int v = 0;
            for (char c : s)
            {
                if (c < '0' || c > '9') return false;
                v = v * 10 + (c - '0');
            }
            out = v;
            return true;
        }

        auto fail(std::string& err, size_t line, const char* why) -> bool
        {
            char b[160];
            std::snprintf(b, sizeof b, "line %zu: %s", line, why);
            err = b;
            return false;
        }
    }

    auto valid_iid(const std::string& iid) -> bool
    {
        if (iid.size() != 16) return false;
        for (char c : iid)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        return true;
    }

    auto make_iid(std::string& iid, std::string& err) -> bool
    {
        unsigned char b[8];
        NTSTATUS st = BCryptGenRandom(nullptr, b, sizeof b, BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (st != 0)
        {
            char t[64];
            std::snprintf(t, sizeof t, "BCryptGenRandom failed (0x%08lx)", static_cast<unsigned long>(st));
            err = t;
            return false;
        }
        static const char hex[] = "0123456789abcdef";
        iid.clear();
        for (unsigned char v : b)
        {
            iid += hex[v >> 4];
            iid += hex[v & 15];
        }
        return true;
    }

    auto dump_player(const std::string& iid) -> std::string
    {
        return std::string(kPlayerHeader) + "\niid=" + iid + "\n";
    }

    auto parse_player(const std::string& text, std::string& iid, std::string& err) -> bool
    {
        std::string found;
        int n = 0;
        size_t b = 0;
        while (b <= text.size())
        {
            size_t e = text.find('\n', b);
            std::string line = text.substr(b, e == std::string::npos ? std::string::npos : e - b);
            b = e == std::string::npos ? text.size() + 1 : e + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            if (line.compare(0, 4, "iid=") == 0)
            {
                ++n;
                found = line.substr(4);
            }
        }
        if (n == 0)
        {
            err = "no iid line";
            return false;
        }
        if (n > 1)
        {
            err = "more than one iid line";
            return false;
        }
        if (!valid_iid(found))
        {
            err = "iid is not 16 lowercase hex characters";
            return false;
        }
        iid = found;
        return true;
    }

    auto load_iid(const std::wstring& dir, std::string& iid, std::string& err) -> Load
    {
        std::string text;
        Load r = read_file(join_path(dir, kPlayerFile), text, err);
        if (r != Load::ok) return r;
        std::string v;
        if (!parse_player(text, v, err)) return Load::bad;
        iid = v;
        return Load::ok;
    }

    auto ensure_iid(const std::wstring& dir, std::string& iid, std::string& err) -> bool
    {
        Load r = load_iid(dir, iid, err);
        if (r == Load::ok) return true;
        if (r == Load::bad) return false;
        std::string fresh;
        if (!make_iid(fresh, err)) return false;
        make_dirs(dir);
        std::wstring path = join_path(dir, kPlayerFile);
        std::string werr;
        if (!write_atomic(path, dump_player(fresh), false, werr))
        {
            std::string again;
            Load r2 = load_iid(dir, iid, again);
            if (r2 == Load::ok) return true;
            err = r2 == Load::bad ? again : werr;
            return false;
        }
        iid = fresh;
        return true;
    }

    auto normalize(const std::string& name) -> std::string
    {
        std::string out;
        size_t count = 0;
        bool gap = false;
        size_t i = 0;
        while (i < name.size())
        {
            unsigned char c = static_cast<unsigned char>(name[i]);
            if (c <= 0x20 || c == 0x7F)
            {
                gap = true;
                ++i;
                continue;
            }
            size_t n = cp_len(name, i);
            size_t need = (gap && !out.empty() ? 1 : 0) + 1;
            if (count + need > kNameChars) break;
            if (gap && !out.empty())
            {
                out += ' ';
                ++count;
            }
            gap = false;
            if (n == 0)
            {
                out += '?';
                ++i;
            }
            else
            {
                out.append(name, i, n);
                i += n;
            }
            ++count;
        }
        return out;
    }

    auto compare_form(const std::string& name) -> std::string
    {
        return lower_ascii(normalize(name));
    }

    auto find_member(const Campaign& c, const std::string& key) -> const Member*
    {
        for (const Member& m : c.members)
            if (m.key == key) return &m;
        return nullptr;
    }

    auto resolve(Campaign& c, const std::string& name, const std::string& iid, const std::vector<Peer>& peers) -> std::string
    {
        if (!valid_iid(iid)) return std::string();
        std::string n = normalize(name);
        if (Member* m = find_by_iid(c, iid))
        {
            if (!n.empty()) m->name = n;
            return m->key;
        }
        Member add;
        add.iid = iid;
        add.name = n;
        if (n.empty())
        {
            add.key = "#" + iid;
            c.members.push_back(add);
            return add.key;
        }
        std::string cn = lower_ascii(n);
        bool clash = false;
        for (const Member& m : c.members)
            if (compare_form(m.name) == cn) clash = true;
        for (const Peer& p : peers)
            if (p.second != iid && compare_form(p.first) == cn) clash = true;
        if (!clash && !key_taken(c, n)) add.key = n;
        else
        {
            for (size_t len = 6; len <= iid.size(); ++len)
            {
                add.key = n + "#" + iid.substr(0, len);
                if (!key_taken(c, add.key)) break;
            }
        }
        c.members.push_back(add);
        return add.key;
    }

    auto resolve_all(Campaign& c, const std::vector<Peer>& players) -> std::vector<std::string>
    {
        std::vector<std::string> keys(players.size());
        std::vector<size_t> pending;
        for (size_t i = 0; i < players.size(); ++i)
        {
            const std::string& iid = players[i].second;
            if (!valid_iid(iid)) continue;
            if (find_by_iid(c, iid) || normalize(players[i].first).empty()) keys[i] = resolve(c, players[i].first, iid, {});
            else pending.push_back(i);
        }
        std::stable_sort(pending.begin(), pending.end(), [&](size_t a, size_t b) { return players[a].second < players[b].second; });
        for (size_t i : pending) keys[i] = resolve(c, players[i].first, players[i].second, {});
        return keys;
    }

    auto file_form(const Campaign& c, const std::string& key) -> std::string
    {
        std::string mine = sanitize(key);
        size_t idx = c.members.size();
        for (size_t i = 0; i < c.members.size(); ++i)
            if (c.members[i].key == key)
            {
                idx = i;
                break;
            }
        for (size_t i = 0; i < idx; ++i)
        {
            if (!iequal(sanitize(c.members[i].key), mine)) continue;
            return mine + "_" + (idx < c.members.size() ? c.members[idx].iid.substr(0, 6) : fnv6(key));
        }
        return mine;
    }

    auto dump_campaign(const Campaign& c) -> std::string
    {
        std::string o = std::string(kCampaignHeader) + "\n";
        o += "cid\t" + c.cid + "\n";
        for (const Member& m : c.members) o += "member\t" + m.key + "\t" + m.name + "\t" + m.iid + "\n";
        for (const Entry& e : c.entries) o += "entry\t" + e.guid + "\t" + e.owner + "\t" + std::to_string(e.kh) + "\t" + e.made + "\n";
        return o;
    }

    auto parse_campaign(const std::string& text, Campaign& out, std::string& err) -> bool
    {
        Campaign c;
        std::vector<std::string> lines;
        size_t b = 0;
        while (b < text.size())
        {
            size_t e = text.find('\n', b);
            if (e == std::string::npos)
            {
                lines.push_back(text.substr(b));
                break;
            }
            lines.push_back(text.substr(b, e - b));
            b = e + 1;
        }
        if (lines.empty() || lines[0] != kCampaignHeader)
        {
            err = "line 1: missing header";
            return false;
        }
        bool haveCid = false;
        for (size_t i = 1; i < lines.size(); ++i)
        {
            size_t ln = i + 1;
            std::vector<std::string> f = split_tabs(lines[i]);
            for (const std::string& s : f)
                if (!clean(s)) return fail(err, ln, "control character in field");
            if (f[0] == "cid")
            {
                if (haveCid) return fail(err, ln, "second cid line");
                if (f.size() != 2 || f[1].empty()) return fail(err, ln, "cid needs exactly one non-empty value");
                if (i != 1) return fail(err, ln, "cid must be the first line after the header");
                c.cid = f[1];
                haveCid = true;
            }
            else if (!haveCid) return fail(err, ln, "cid line missing");
            else if (f[0] == "member")
            {
                if (f.size() != 4) return fail(err, ln, "member needs key, name, iid");
                if (f[1].empty()) return fail(err, ln, "empty member key");
                if (normalize(f[2]) != f[2]) return fail(err, ln, "member name is not normalized");
                if (!valid_iid(f[3])) return fail(err, ln, "bad member iid");
                for (const Member& m : c.members)
                {
                    if (iequal(m.key, f[1])) return fail(err, ln, "duplicate member key");
                    if (m.iid == f[3]) return fail(err, ln, "duplicate member iid");
                }
                c.members.push_back({f[1], f[2], f[3]});
            }
            else if (f[0] == "entry")
            {
                if (f.size() != 5) return fail(err, ln, "entry needs guid, owner, kh, made");
                if (f[1].empty()) return fail(err, ln, "empty entry guid");
                if (f[2].empty()) return fail(err, ln, "empty entry owner");
                Entry en;
                if (!parse_kh(f[3], en.kh)) return fail(err, ln, "bad entry kh");
                if (f[4] != "host" && f[4] != "splice" && f[4] != "local") return fail(err, ln, "entry made must be host, splice or local");
                en.guid = f[1];
                en.owner = f[2];
                en.made = f[4];
                c.entries.push_back(en);
            }
            else return fail(err, ln, "unknown line");
        }
        if (!haveCid)
        {
            err = "cid line missing";
            return false;
        }
        out = c;
        return true;
    }

    auto load_campaign(const std::wstring& dir, Campaign& out, std::string& err) -> Load
    {
        std::string text;
        Load r = read_file(join_path(dir, kCampaignFile), text, err);
        if (r != Load::ok) return r;
        return parse_campaign(text, out, err) ? Load::ok : Load::bad;
    }

    auto save_campaign(const std::wstring& dir, const Campaign& c, std::string& err) -> bool
    {
        return write_atomic(join_path(dir, kCampaignFile), dump_campaign(c), true, err);
    }
}
