#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "s2x_buildchk.h"

namespace bchk
{
    namespace
    {
#include "s2x_buildchk_tab.inc"

        constexpr size_t kBcN = sizeof kBcSigs / sizeof kBcSigs[0];

        int man_count()
        {
            int k = 0;
            for (const Sig& s : kBcSigs) k += s.man ? 1 : 0;
            return k;
        }

        const Expect kTable{kBcTs, kBcTextVs, kBcVer, kBcBuild, man_count(), (int)kBcN - man_count(), kBcSigs, kBcN};

        bool seh_copy(void* dst, const void* src, size_t n)
        {
            __try
            {
                std::memcpy(dst, src, n);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool readable(const uint8_t* p, size_t n)
        {
            const uint8_t* end = p + n;
            while (p < end)
            {
                MEMORY_BASIC_INFORMATION mi{};
                if (!VirtualQuery(p, &mi, sizeof mi)) return false;
                if (mi.State != MEM_COMMIT) return false;
                DWORD pr = mi.Protect & 0xff;
                if (pr == PAGE_NOACCESS || pr == 0 || (mi.Protect & PAGE_GUARD)) return false;
                const uint8_t* next = static_cast<const uint8_t*>(mi.BaseAddress) + mi.RegionSize;
                if (next <= p) return false;
                p = next;
            }
            return true;
        }

        template <class T> bool rd(const Image& img, uint32_t rva, T& out) { return img.read(rva, &out, sizeof out); }

        struct Sec
        {
            char name[9];
            uint32_t va, vsize, rsize, raw;
        };

        struct Pe
        {
            uint32_t ts = 0;
            uint32_t size_of_image = 0;
            uint32_t size_of_headers = 0;
            uint32_t res_rva = 0, res_size = 0;
            std::vector<Sec> secs;
        };

        bool parse_pe(const Image& img, Pe& pe)
        {
            uint32_t lfanew = 0;
            uint16_t mz = 0;
            if (!rd(img, 0, mz) || mz != 0x5A4D || !rd(img, 0x3C, lfanew) || lfanew > 0x1000) return false;
            uint32_t sig = 0;
            if (!rd(img, lfanew, sig) || sig != 0x4550) return false;
            uint16_t nsec = 0, optsize = 0, magic = 0;
            if (!rd(img, lfanew + 6, nsec) || !rd(img, lfanew + 8, pe.ts) || !rd(img, lfanew + 20, optsize)) return false;
            uint32_t opt = lfanew + 24;
            if (!rd(img, opt, magic) || magic != 0x20B) return false;
            uint32_t ndir = 0;
            if (!rd(img, opt + 56, pe.size_of_image) || !rd(img, opt + 60, pe.size_of_headers) || !rd(img, opt + 108, ndir)) return false;
            if (ndir > 2 && (!rd(img, opt + 112 + 16, pe.res_rva) || !rd(img, opt + 112 + 20, pe.res_size))) return false;
            if (nsec == 0 || nsec > 96) return false;
            uint32_t so = opt + optsize;
            for (uint16_t i = 0; i < nsec; ++i)
            {
                uint8_t h[40];
                if (!img.read(so + 40u * i, h, sizeof h)) return false;
                Sec s{};
                std::memcpy(s.name, h, 8);
                s.name[8] = 0;
                std::memcpy(&s.vsize, h + 8, 4);
                std::memcpy(&s.va, h + 12, 4);
                std::memcpy(&s.rsize, h + 16, 4);
                std::memcpy(&s.raw, h + 20, 4);
                pe.secs.push_back(s);
            }
            return true;
        }

        bool res_child(const Image& img, uint32_t root, uint32_t dir, uint32_t want, bool any, uint32_t& out)
        {
            uint16_t nn = 0, ni = 0;
            if (!rd(img, root + dir + 12, nn) || !rd(img, root + dir + 14, ni)) return false;
            uint32_t n = (uint32_t)nn + ni;
            if (n > 4096) return false;
            for (uint32_t k = 0; k < n; ++k)
            {
                uint32_t id = 0, off = 0;
                if (!rd(img, root + dir + 16 + 8 * k, id) || !rd(img, root + dir + 20 + 8 * k, off)) return false;
                if (any || (!(id & 0x80000000u) && id == want))
                {
                    out = off;
                    return true;
                }
            }
            return false;
        }

        std::string file_version(const Image& img, const Pe& pe)
        {
            if (!pe.res_rva || !pe.res_size) return {};
            uint32_t root = pe.res_rva, o = 0;
            if (!res_child(img, root, 0, 16, false, o) || !(o & 0x80000000u)) return {};
            if (!res_child(img, root, o & 0x7fffffffu, 0, true, o) || !(o & 0x80000000u)) return {};
            if (!res_child(img, root, o & 0x7fffffffu, 0, true, o) || (o & 0x80000000u)) return {};
            uint32_t drva = 0, dsize = 0;
            if (!rd(img, root + o, drva) || !rd(img, root + o + 4, dsize)) return {};
            static const wchar_t kKey[] = L"VS_VERSION_INFO";
            const uint32_t klen = sizeof kKey;
            uint32_t fixed = (6 + klen + 3) & ~3u;
            if (dsize < fixed + 52) return {};
            uint16_t wlen = 0, vlen = 0;
            if (!rd(img, drva, wlen) || !rd(img, drva + 2, vlen) || vlen < 52 || wlen > dsize) return {};
            wchar_t key[sizeof kKey / sizeof kKey[0]];
            if (!img.read(drva + 6, key, klen) || std::memcmp(key, kKey, klen) != 0) return {};
            uint32_t v[4]{};
            if (!img.read(drva + fixed, v, sizeof v) || v[0] != 0xFEEF04BDu) return {};
            char t[64];
            std::snprintf(t, sizeof t, "%u.%u.%u.%u", v[2] >> 16, v[2] & 0xffff, v[3] >> 16, v[3] & 0xffff);
            return t;
        }

        int hexv(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        }

        bool parse_pat(const char* p, std::vector<uint8_t>& bytes, std::vector<uint8_t>& mask)
        {
            bytes.clear();
            mask.clear();
            while (*p)
            {
                if (*p == ' ') { ++p; continue; }
                if (p[0] == '?' && p[1] == '?') { bytes.push_back(0); mask.push_back(0); p += 2; continue; }
                int a = hexv(p[0]), b = a < 0 ? -1 : hexv(p[1]);
                if (a < 0 || b < 0) return false;
                bytes.push_back((uint8_t)(a * 16 + b));
                mask.push_back(1);
                p += 2;
            }
            return !bytes.empty();
        }

        std::string hex(uint32_t v, int w)
        {
            char t[16];
            std::snprintf(t, sizeof t, w ? "%08x" : "%x", v);
            return t;
        }

        std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

        bool lua_space(char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f'; }
        bool word(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
    }

    bool FileImage::load(const wchar_t* path)
    {
        data.clear();
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (1ll << 31);
        if (ok)
        {
            data.resize((size_t)sz.QuadPart);
            size_t got = 0;
            while (ok && got < data.size())
            {
                DWORD n = 0;
                DWORD want = (DWORD)std::min<size_t>(data.size() - got, 1u << 26);
                ok = ReadFile(h, data.data() + got, want, &n, nullptr) && n > 0;
                got += n;
            }
        }
        CloseHandle(h);
        if (!ok) data.clear();
        return ok;
    }

    bool FileImage::read(uint32_t rva, void* out, size_t n) const
    {
        if (data.size() < 0x200) return false;
        uint32_t lfanew = 0;
        std::memcpy(&lfanew, data.data() + 0x3C, 4);
        if (lfanew > 0x1000 || (size_t)lfanew + 24 + 240 > data.size()) return false;
        uint16_t nsec = 0, optsize = 0;
        std::memcpy(&nsec, data.data() + lfanew + 6, 2);
        std::memcpy(&optsize, data.data() + lfanew + 20, 2);
        uint32_t hdrs = 0;
        std::memcpy(&hdrs, data.data() + lfanew + 24 + 60, 4);
        if ((uint64_t)rva + n <= hdrs)
        {
            if ((uint64_t)rva + n > data.size()) return false;
            std::memcpy(out, data.data() + rva, n);
            return true;
        }
        size_t so = (size_t)lfanew + 24 + optsize;
        for (uint16_t i = 0; i < nsec; ++i)
        {
            const uint8_t* h = data.data() + so + 40u * i;
            if (so + 40u * (i + 1) > data.size()) return false;
            uint32_t vsize, va, rsize, raw;
            std::memcpy(&vsize, h + 8, 4);
            std::memcpy(&va, h + 12, 4);
            std::memcpy(&rsize, h + 16, 4);
            std::memcpy(&raw, h + 20, 4);
            uint32_t span = std::max(vsize, rsize);
            if (rva < va || (uint64_t)rva + n > (uint64_t)va + span) continue;
            uint8_t* o = static_cast<uint8_t*>(out);
            for (size_t k = 0; k < n; ++k)
            {
                uint32_t off = rva - va + (uint32_t)k;
                size_t fo = (size_t)raw + off;
                o[k] = off < rsize && fo < data.size() ? data[fo] : 0;
            }
            return true;
        }
        return false;
    }

    MemImage::MemImage(const void* module) : base(static_cast<const uint8_t*>(module))
    {
        if (!base || !readable(base, 0x400)) { base = nullptr; return; }
        uint32_t lfanew = 0;
        if (!seh_copy(&lfanew, base + 0x3C, 4) || lfanew > 0x1000) { base = nullptr; return; }
        if (!seh_copy(&size, base + lfanew + 24 + 56, 4)) { base = nullptr; size = 0; }
    }

    bool MemImage::read(uint32_t rva, void* out, size_t n) const
    {
        if (!base || (uint64_t)rva + n > size) return false;
        if (!readable(base + rva, n)) return false;
        return seh_copy(out, base + rva, n);
    }

    const Expect& table() { return kTable; }

    Result run_check(const Image& img, const Expect& e)
    {
        Result r;
        r.total = (int)e.n;
        Pe pe;
        r.headers = parse_pe(img, pe);
        if (r.headers)
        {
            r.ts = pe.ts;
            for (auto& s : pe.secs)
                if (std::strcmp(s.name, ".text") == 0) { r.text_vs = s.vsize; break; }
            r.ver = file_version(img, pe);
        }
        r.ident = r.headers && r.ts == e.ts && r.text_vs == e.text_vs && r.ver == e.ver;
        std::vector<uint8_t> pat, mask, buf;
        for (size_t i = 0; i < e.n; ++i)
        {
            const Sig& s = e.sigs[i];
            bool good = false;
            if (parse_pat(s.pat, pat, mask))
            {
                buf.assign(pat.size(), 0);
                if (r.headers && img.read(s.rva, buf.data(), buf.size()))
                {
                    good = true;
                    for (size_t k = 0; k < pat.size() && good; ++k)
                        if (mask[k] && buf[k] != pat[k]) good = false;
                }
                else ++r.unread;
            }
            if (!good)
            {
                if (!r.bad) { r.first = s.name; r.first_rva = s.rva; }
                ++r.bad;
            }
        }
        r.ok = r.headers && r.ident && r.bad == 0;
        return r;
    }

    std::string found_text(const Result& r, const Expect& e)
    {
        if (!r.headers) return "unreadable (no PE headers)";
        std::string t = (r.ver.empty() ? std::string("no-version") : r.ver) + " " + hex(r.ts, 8);
        if (r.text_vs != e.text_vs) t += " text " + hex(r.text_vs, 0);
        return t;
    }

    std::string expect_text(const Expect& e) { return std::string(e.ver) + " " + hex(e.ts, 8) + " (Steam build " + e.build + ")"; }

    std::string log_line(const Result& r, const Expect& e)
    {
        char t[160];
        if (r.ok)
        {
            std::snprintf(t, sizeof t, ", %d of %d signatures match (%d hook manifest + %d source)", r.total - r.bad, r.total, e.manifest, e.extra);
            return "NOLA buildchk: OK game build " + expect_text(e) + t;
        }
        std::string s = "NOLA buildchk: FAIL game build " + found_text(r, e) + " != " + expect_text(e);
        std::snprintf(t, sizeof t, " (%d of %d signatures differ", r.bad, r.total);
        s += t;
        if (r.bad) s += ", first " + r.first + "@" + hex(r.first_rva, 0);
        if (r.unread) s += ", " + std::to_string(r.unread) + " unreadable";
        s += "): no hooks installed, NOLA is off, the game runs unmodded";
        return s;
    }

    std::wstring message(const Result& r, const Expect& e)
    {
        std::wstring m = L"NOLA needs game version " + widen(expect_text(e)) + L", this game is " + widen(found_text(r, e));
        if (r.ident && r.bad)
            m += L" with " + std::to_wstring(r.bad) + L" of " + std::to_wstring(r.total) + L" code signatures changed (another mod or tool may have patched the game)";
        m += L".\n\nNOLA is off: no hooks were installed and the game runs without it.\n\nInstall the NOLA release made for this game version, or set flag buildchk=0 in C:\\nola_bridge\\flags.txt to skip this check at your own risk.";
        return m;
    }

    bool enabled(std::string_view text)
    {
        bool on = true;
        size_t pos = 0;
        if (text.size() >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) pos = 3;
        while (pos < text.size())
        {
            size_t nl = text.find('\n', pos);
            std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
            pos = nl == std::string_view::npos ? text.size() : nl + 1;
            size_t i = 0;
            while (i < line.size() && lua_space(line[i])) ++i;
            size_t kb = i;
            while (i < line.size() && word(line[i])) ++i;
            if (line.substr(kb, i - kb) != "buildchk") continue;
            while (i < line.size() && lua_space(line[i])) ++i;
            if (i >= line.size() || line[i] != '=') continue;
            ++i;
            while (i < line.size() && lua_space(line[i])) ++i;
            size_t vb = i;
            while (i < line.size() && (word(line[i]) || line[i] == '.' || line[i] == '-')) ++i;
            if (i == vb) continue;
            std::string v(line.substr(vb, i - vb));
            char* end = nullptr;
            double d = std::strtod(v.c_str(), &end);
            if (end && *end == 0) on = d != 0;
        }
        return on;
    }
}
