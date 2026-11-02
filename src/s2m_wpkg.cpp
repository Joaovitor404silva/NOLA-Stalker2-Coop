#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "s2m_wpkg.h"
#include "s2m_savefile.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>

namespace s2m_wpkg
{
    namespace
    {
        auto narrow(const std::wstring& w) -> std::string
        {
            if (w.empty()) return {};
            int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
            std::string s(n, '\0');
            WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
            return s;
        }
        auto widen(const std::string& s) -> std::wstring
        {
            if (s.empty()) return {};
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
            return w;
        }
        auto slash(const std::wstring& d) -> std::wstring
        {
            if (d.empty() || d.back() == L'\\' || d.back() == L'/') return d;
            return d + L"\\";
        }
        auto tok(const std::string& s) -> bool
        {
            if (s.empty() || s.size() > 64) return false;
            for (char c : s)
                if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) return false;
            return true;
        }
        auto first_line(const std::string& d) -> std::string
        {
            size_t e = d.find('\n');
            std::string s = e == std::string::npos ? d : d.substr(0, e);
            while (!s.empty() && (s.back() == '\r' || s.back() == ' ')) s.pop_back();
            return s;
        }
    }

    auto fnv64(const char* d, size_t n) -> uint64_t
    {
        uint64_t h = 1469598103934665603ULL;
        for (size_t i = 0; i < n; ++i)
        {
            h ^= (uint8_t)d[i];
            h *= 1099511628211ULL;
        }
        return h;
    }
    auto hex16(uint64_t v) -> std::string
    {
        char b[24];
        std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v);
        return b;
    }
    auto field(const std::string& hdr, const char* k) -> std::string
    {
        std::string key = std::string(" ") + k + "=";
        size_t p = hdr.find(key);
        if (p == std::string::npos) return {};
        p += key.size();
        size_t e = hdr.find(' ', p);
        return hdr.substr(p, e == std::string::npos ? std::string::npos : e - p);
    }
    auto build_ap(const std::string& log, int kh, const std::string& cid, int& have) -> std::string
    {
        std::vector<std::string> order;
        std::unordered_map<std::string, int> cnt;
        have = 0;
        size_t p = 0;
        while (p < log.size() && have < kh)
        {
            size_t e = log.find('\n', p);
            std::string ln = log.substr(p, e == std::string::npos ? std::string::npos : e - p);
            p = e == std::string::npos ? log.size() : e + 1;
            while (!ln.empty() && ln.back() == '\r') ln.pop_back();
            size_t i = 0;
            while (i < ln.size() && (std::isdigit((unsigned char)ln[i]) || ln[i] == '.')) ++i;
            if (i == 0 || i >= ln.size() || ln[i] != '\t' || i + 1 >= ln.size()) continue;
            std::string ev = ln.substr(i + 1);
            if (!cnt.count(ev)) order.push_back(ev);
            ++cnt[ev];
            ++have;
        }
        std::string o = "#S2CA 1" + (cid.empty() ? std::string() : " " + cid) + "\n";
        if (!cid.empty()) o += "W\t" + cid + "\n";
        for (auto& ev : order) o += std::to_string(cnt[ev]) + "\t" + ev + "\n";
        return o;
    }
    auto pack(const Pkg& k) -> std::string
    {
        char h[640];
        int n = std::snprintf(h, sizeof h, "S2SX1 id=%s guid=%s size=%lld hash=%s kh=%d cid=%s ap=%zu", k.id.c_str(), k.guid.c_str(), (long long)k.data.size(),
            hex16(fnv64(k.data.data(), k.data.size())).c_str(), k.kh, k.cid.empty() ? "-" : k.cid.c_str(), k.ap.size());
        std::string s(h, (size_t)std::max(0, n));
        if (k.hs >= 0) s += " hs=" + std::to_string(k.hs) + " own=" + (k.own.empty() ? std::string("-") : k.own);
        return s + "\n" + k.ap + k.data;
    }
    auto parse(const std::string& b, Pkg& k, std::string& err) -> bool
    {
        size_t e = b.find('\n');
        if (b.compare(0, 6, "S2SX1 ") != 0 || e == std::string::npos || e > 600) { err = "bad header"; return false; }
        std::string hdr = b.substr(0, e);
        k.id = field(hdr, "id");
        k.guid = field(hdr, "guid");
        k.cid = field(hdr, "cid");
        k.size = _atoi64(field(hdr, "size").c_str());
        k.kh = atoi(field(hdr, "kh").c_str());
        k.hash = _strtoui64(field(hdr, "hash").c_str(), nullptr, 16);
        std::string hs = field(hdr, "hs");
        k.hs = hs.empty() ? -1 : atoi(hs.c_str());
        k.own = field(hdr, "own");
        if (k.own == "-") k.own.clear();
        long long apn = _atoi64(field(hdr, "ap").c_str());
        if (k.cid == "-") k.cid.clear();
        if (k.id.empty() || k.size <= 0 || apn <= 0 || k.kh < 0) { err = "header fields"; return false; }
        if ((long long)(b.size() - e - 1) != apn + k.size) { err = "size " + std::to_string(b.size()) + " does not match"; return false; }
        k.ap = b.substr(e + 1, (size_t)apn);
        k.data = b.substr(e + 1 + (size_t)apn);
        if (fnv64(k.data.data(), k.data.size()) != k.hash) { err = "save hash does not match"; return false; }
        if (k.ap.compare(0, 7, "#S2CA 1") != 0) { err = "bad applied record"; return false; }
        return true;
    }

    auto world_id(long long wallMs) -> std::string
    {
        std::string d = std::to_string(wallMs < 0 ? 0 : wallMs);
        if (d.size() < 13) d = std::string(13 - d.size(), '0') + d;
        return "9" + d;
    }
    auto is_world_id(const std::string& id) -> bool
    {
        if (id.size() != 14 || id[0] != '9') return false;
        for (char c : id)
            if (c < '0' || c > '9') return false;
        return true;
    }

    auto read_file(const std::wstring& p, std::string& out) -> bool
    {
        out.clear();
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1LL << 30))
        {
            CloseHandle(h);
            return false;
        }
        size_t n = (size_t)sz.QuadPart;
        out.resize(n);
        size_t got = 0;
        while (got < n)
        {
            DWORD r = 0;
            DWORD want = (DWORD)std::min<size_t>(n - got, 1 << 24);
            if (!ReadFile(h, out.data() + got, want, &r, nullptr) || r == 0) break;
            got += r;
        }
        CloseHandle(h);
        out.resize(got);
        return got == n;
    }
    auto write_atomic(const std::wstring& p, const std::string& data) -> bool
    {
        std::wstring t = p + L".wptmp";
        HANDLE h = CreateFileW(t.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        size_t put = 0;
        bool ok = true;
        while (put < data.size())
        {
            DWORD w = 0;
            DWORD want = (DWORD)std::min<size_t>(data.size() - put, 1 << 24);
            if (!WriteFile(h, data.data() + put, want, &w, nullptr) || w == 0)
            {
                ok = false;
                break;
            }
            put += w;
        }
        if (ok) FlushFileBuffers(h);
        CloseHandle(h);
        if (!ok || !MoveFileExW(t.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            DeleteFileW(t.c_str());
            return false;
        }
        std::string back;
        return read_file(p, back) && back.size() == data.size() && fnv64(back.data(), back.size()) == fnv64(data.data(), data.size());
    }

    auto freeze(const std::wstring& br0, const std::wstring& savFile, const std::string& guid, int hs, int kh, const std::string& cid, std::string& err) -> bool
    {
        std::wstring br = slash(br0);
        if (br.empty()) { err = "no bridge folder"; return false; }
        if (guid.size() != 32 || !tok(guid)) { err = "bad guid"; return false; }
        if (kh < 0 || hs < 0) { err = "no kh/hs"; return false; }
        std::string d;
        if (!read_file(savFile, d) || d.size() < 1024) { err = "save " + guid.substr(0, 8) + " unreadable"; return false; }
        uint32_t rs = 0;
        std::string st, e;
        if (!s2m_savefile::file_split(d, rs, st, e)) { err = "save " + guid.substr(0, 8) + ": " + e; return false; }
        uint64_t h = fnv64(d.data(), d.size());
        if (!write_atomic(br + L"wp_host.sav", d)) { err = "wp_host.sav not written"; return false; }
        char hd[400];
        std::snprintf(hd, sizeof hd, "#S2WP 1 guid=%s kh=%d hs=%d cid=%s size=%lld hash=%s\n", guid.c_str(), kh, hs, cid.empty() ? "-" : cid.c_str(), (long long)d.size(), hex16(h).c_str());
        if (!write_atomic(br + L"wp_host.txt", hd)) { err = "wp_host.txt not written"; return false; }
        return true;
    }
    auto frozen(const std::wstring& br0, Frozen& f, std::string* data, std::string& err) -> bool
    {
        std::wstring br = slash(br0);
        std::string t;
        if (!read_file(br + L"wp_host.txt", t)) { err = "no frozen host save"; return false; }
        std::string hd = first_line(t);
        if (hd.compare(0, 8, "#S2WP 1 ") != 0) { err = "bad wp_host.txt"; return false; }
        f.guid = field(hd, "guid");
        f.cid = field(hd, "cid");
        if (f.cid == "-") f.cid.clear();
        f.kh = atoi(field(hd, "kh").c_str());
        f.hs = atoi(field(hd, "hs").c_str());
        f.size = _atoi64(field(hd, "size").c_str());
        f.hash = _strtoui64(field(hd, "hash").c_str(), nullptr, 16);
        if (f.guid.size() != 32 || f.size <= 0 || f.kh < 0 || f.hs < 0) { err = "wp_host.txt fields"; return false; }
        if (!data) return true;
        if (!read_file(br + L"wp_host.sav", *data) || (long long)data->size() != f.size || fnv64(data->data(), data->size()) != f.hash)
        {
            err = "wp_host.sav does not match wp_host.txt";
            return false;
        }
        return true;
    }
    auto pack_world(const std::wstring& br, const std::string& req, const std::string& questLog, const std::string& own, std::string& out, Frozen& f, std::string& err) -> bool
    {
        if (!is_world_id(req)) { err = "not a world request"; return false; }
        Pkg k;
        if (!frozen(br, f, &k.data, err)) return false;
        int have = 0;
        k.ap = build_ap(questLog, f.kh, f.cid, have);
        if (have < f.kh) { err = "quest_log.txt has " + std::to_string(have) + " events, kh=" + std::to_string(f.kh); return false; }
        k.id = req;
        k.guid = f.guid;
        k.cid = f.cid;
        k.kh = f.kh;
        k.hs = f.hs;
        k.own = own;
        out = pack(k);
        return true;
    }

    auto store(const std::wstring& br0, const std::string& blob, int keep, Stored& s, std::string& err) -> bool
    {
        std::wstring br = slash(br0);
        Pkg k;
        if (!parse(blob, k, err)) return false;
        if (!is_world_id(k.id)) { err = "package " + k.id + " is not a world package"; return false; }
        if (k.hs < 0) { err = "world package without hs"; return false; }
        if (!k.cid.empty() && !tok(k.cid)) { err = "bad campaign id"; return false; }
        uint32_t rs = 0;
        std::string st, e;
        if (!s2m_savefile::file_split(k.data, rs, st, e)) { err = "world save: " + e; return false; }
        std::wstring dir = br + L"wp_in\\";
        CreateDirectoryW(dir.c_str(), nullptr);
        std::string cid = k.cid.empty() ? std::string("nocid") : k.cid;
        std::string name = cid + "__" + k.id + ".pkg";
        if (!write_atomic(dir + widen(name), blob)) { err = name + " not written"; return false; }
        s.id = k.id;
        s.cid = k.cid;
        s.guid = k.guid;
        s.kh = k.kh;
        s.hs = k.hs;
        s.size = (long long)k.data.size();
        s.file = name;
        char ln[600];
        std::snprintf(ln, sizeof ln, "#S2WPIN 1 id=%s cid=%s kh=%d hs=%d guid=%s size=%lld file=%s own=%s\n", s.id.c_str(), cid.c_str(), s.kh, s.hs, s.guid.c_str(), s.size, name.c_str(),
            k.own.empty() ? "-" : k.own.c_str());
        if (!write_atomic(dir + L"last.txt", ln)) { err = "last.txt not written"; return false; }
        std::vector<std::string> mine;
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((dir + widen(cid) + L"__*.pkg").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) mine.push_back(narrow(fd.cFileName));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        std::sort(mine.begin(), mine.end());
        int drop = (int)mine.size() - std::max(1, keep);
        for (int i = 0; i < drop; ++i)
            if (mine[(size_t)i] != name) DeleteFileW((dir + widen(mine[(size_t)i])).c_str());
        return true;
    }
    auto last(const std::wstring& br0, Stored& s) -> bool
    {
        std::wstring br = slash(br0);
        std::string t;
        if (!read_file(br + L"wp_in\\last.txt", t)) return false;
        std::string hd = first_line(t);
        if (hd.compare(0, 10, "#S2WPIN 1 ") != 0) return false;
        s.id = field(hd, "id");
        s.cid = field(hd, "cid");
        if (s.cid == "nocid") s.cid.clear();
        s.kh = atoi(field(hd, "kh").c_str());
        s.hs = atoi(field(hd, "hs").c_str());
        s.guid = field(hd, "guid");
        s.size = _atoi64(field(hd, "size").c_str());
        s.file = field(hd, "file");
        return is_world_id(s.id) && !s.file.empty();
    }
}
