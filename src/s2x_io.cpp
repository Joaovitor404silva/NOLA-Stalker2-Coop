#include "s2x_int.h"
#include <algorithm>
#include "s2x_fr.h"
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace x0::in
{
    std::string w2u(std::wstring_view w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }
    std::wstring u2w(std::string_view s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    LogSink& log_sink()
    {
        static LogSink s;
        return s;
    }

    namespace
    {
        struct RoleState
        {
            bool init = false;
            std::string role;
            std::wstring dir;
            std::atomic<const std::string*> cur{nullptr};
        };
        const std::string kRoleHost = "host";
        const std::string kRoleClient = "client";
        std::vector<std::pair<Mod, RoleCb>>& role_cbs()
        {
            static std::vector<std::pair<Mod, RoleCb>> v;
            return v;
        }
        RoleState& rs()
        {
            static RoleState r;
            return r;
        }
        bool file_ok(const std::wstring& p)
        {
            DWORD a = GetFileAttributesW(p.c_str());
            return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
        }
        std::wstring env(const wchar_t* n, bool& has)
        {
            wchar_t b[2048];
            DWORD r = GetEnvironmentVariableW(n, b, 2048);
            has = r > 0 && r < 2048;
            return has ? std::wstring(b, r) : std::wstring();
        }
        void role_init()
        {
            auto& r = rs();
            if (r.init) return;
            r.init = true;
            bool has = false;
            std::wstring e = env(L"S2_BRIDGE", has);
            r.dir = has ? e : std::wstring(L"C:\\nola_bridge\\");
            std::wstring er = env(L"S2_ROLE", has);
            if (has) r.role = w2u(er);
            else
            {
                r.role = "host";
#ifndef S2X_RELEASE
                std::string t;
                if (read_text(r.dir + L"role.txt", t))
                {
                    size_t nl = t.find('\n');
                    std::string l = t.substr(0, nl);
                    std::string o;
                    for (char c : l) if (!(c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f')) o += c;
                    r.role = o;
                }
#endif
            }
            r.cur = &r.role;
        }
    }
    void set_role_for_test(const std::string& r, const std::wstring& dir)
    {
        auto& s = rs();
        s.init = true;
        s.role = r;
        s.dir = dir;
        s.cur = &s.role;
    }

    bool read_all(const std::wstring& path, std::string& out)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart > (1LL << 30)) { CloseHandle(h); return false; }
        out.resize((size_t)sz.QuadPart);
        DWORD got = 0;
        bool ok = sz.QuadPart == 0 || ReadFile(h, out.data(), (DWORD)sz.QuadPart, &got, nullptr);
        CloseHandle(h);
        if (!ok) return false;
        out.resize(got);
        return true;
    }

    static bool write_raw(const std::wstring& path, std::string_view bytes, DWORD disp, bool append)
    {
        HANDLE h = CreateFileW(path.c_str(), append ? FILE_APPEND_DATA : GENERIC_WRITE, FILE_SHARE_READ, nullptr, disp, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD put = 0;
        bool ok = bytes.empty() || (WriteFile(h, bytes.data(), (DWORD)bytes.size(), &put, nullptr) && put == bytes.size());
        CloseHandle(h);
        return ok;
    }

    bool write_atomic(const std::wstring& path, std::string_view bytes)
    {
        std::wstring tmp = path + L".tmp";
        if (!write_raw(tmp, bytes, CREATE_ALWAYS, false)) return false;
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        DeleteFileW(path.c_str());
        return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
    }

    static std::string to_crlf(std::string_view t)
    {
        std::string o;
        o.reserve(t.size() + t.size() / 16);
        for (char c : t)
        {
            if (c == '\n') o += '\r';
            o += c;
        }
        return o;
    }

    std::string tsv_unescape(std::string_view v)
    {
        std::string o;
        o.reserve(v.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            if (v[i] == '\\' && i + 1 < v.size())
            {
                char n = v[i + 1];
                if (n == '\\') { o += '\\'; ++i; continue; }
                if (n == 't') { o += '\t'; ++i; continue; }
                if (n == 'r') { o += '\r'; ++i; continue; }
                if (n == 'n') { o += '\n'; ++i; continue; }
            }
            o += v[i];
        }
        return o;
    }

    bool tsv_parse(std::string_view text, std::vector<TsvRow>& rows, std::string* err)
    {
        rows.clear();
        size_t pos = 0;
        int ln = 0;
        int kcols = -1;
        std::set<std::string> paths;
        while (pos < text.size())
        {
            size_t nl = text.find('\n', pos);
            std::string_view line = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
            pos = nl == std::string_view::npos ? text.size() : nl + 1;
            ++ln;
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty()) continue;
            std::vector<std::string_view> c;
            size_t b = 0;
            while (true)
            {
                size_t t = line.find('\t', b);
                c.push_back(line.substr(b, t == std::string_view::npos ? std::string_view::npos : t - b));
                if (t == std::string_view::npos) break;
                b = t + 1;
            }
            if (ln == 1)
            {
                if (c.size() < 3 || c[c.size() - 2] != "t" || c.back() != "v") { if (err) *err = "bad header"; return false; }
                kcols = (int)c.size() - 2;
                continue;
            }
            if ((int)c.size() != kcols + 2) { if (err) *err = "line " + std::to_string(ln) + ": " + std::to_string(c.size()) + " cells"; return false; }
            TsvRow r;
            std::string path;
            for (int k = 0; k < kcols; ++k)
            {
                if (c[k].empty()) break;
                char ty = c[k][0];
                if (ty != 's' && ty != 'i') { if (err) *err = "line " + std::to_string(ln) + ": key cell type"; return false; }
                r.ktype += ty;
                r.key.push_back(tsv_unescape(c[k].substr(1)));
                path += std::string(c[k]) + '\x1f';
            }
            if (c[kcols].size() != 1) { if (err) *err = "line " + std::to_string(ln) + ": t"; return false; }
            r.t = c[kcols][0];
            r.v = tsv_unescape(c[kcols + 1]);
            if (!paths.insert(path).second) { if (err) *err = "line " + std::to_string(ln) + ": duplicate path"; return false; }
            rows.push_back(std::move(r));
        }
        if (kcols < 0) { if (err) *err = "empty"; return false; }
        return true;
    }

    static HMODULE self_module()
    {
        HMODULE h = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&self_module), &h);
        return h;
    }
    static std::wstring res_name(std::string_view name)
    {
        std::wstring w;
        for (char c : name)
        {
            char u = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
            if (!((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9'))) u = '_';
            w += (wchar_t)u;
        }
        return w;
    }

    bool data_res(std::string_view name, std::string& out)
    {
        HMODULE m = self_module();
        if (!m) return false;
        std::wstring n = res_name(name);
        HRSRC r = FindResourceW(m, n.c_str(), MAKEINTRESOURCEW(10));
        if (!r) return false;
        HGLOBAL g = LoadResource(m, r);
        DWORD sz = SizeofResource(m, r);
        const void* p = g ? LockResource(g) : nullptr;
        if (!p) return false;
        out.assign(static_cast<const char*>(p), sz);
        return true;
    }

    static BOOL CALLBACK enum_names(HMODULE, LPCWSTR, LPWSTR name, LONG_PTR lp)
    {
        if (!IS_INTRESOURCE(name)) reinterpret_cast<std::vector<std::string>*>(lp)->push_back(w2u(name));
        return TRUE;
    }
    std::vector<std::string> data_res_names()
    {
        std::vector<std::string> v;
        if (HMODULE m = self_module()) EnumResourceNamesW(m, MAKEINTRESOURCEW(10), enum_names, reinterpret_cast<LONG_PTR>(&v));
        return v;
    }
}

namespace x0
{
    double now()
    {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    int64_t wall_ms()
    {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return (int64_t)(u.QuadPart / 10000ULL);
    }
    const std::string& role()
    {
        in::role_init();
        const std::string* p = in::rs().cur.load();
        return p ? *p : in::rs().role;
    }
    const std::string& boot_role()
    {
        in::role_init();
        return in::rs().role;
    }
    bool set_session_role(std::string_view r, std::string_view why)
    {
        in::role_init();
        auto& s = in::rs();
        const std::string* want = r == "host" ? &in::kRoleHost : r == "client" ? &in::kRoleClient : &s.role;
        const std::string* was = s.cur.load();
        if (!was) was = &s.role;
        if (*was == *want) { s.cur = want; return false; }
        s.cur = want;
        logf("NOLA role: %s -> %s (%.*s; boot role %s)", was->c_str(), want->c_str(), (int)why.size(), why.data(), s.role.c_str());
        std::string a = *was, b = *want;
        for (auto& [m, cb] : in::role_cbs())
        {
            if (m != Mod::x0 && !port(m)) continue;
            try { cb(a, b); }
            catch (...) { logf("NOLA role: callback of %s threw", mod_name(m)); }
        }
        return true;
    }
    void on_role(Mod m, RoleCb cb) { in::role_cbs().push_back({m, std::move(cb)}); }
    bool is_host() { return role() == "host"; }
    bool is_client() { return role() == "client"; }
    const std::wstring& bridge_dir()
    {
        in::role_init();
        return in::rs().dir;
    }
    std::wstring bridge_file(std::wstring_view name) { return bridge_dir() + std::wstring(name); }

    void log(std::string_view text)
    {
        S2FR(fr::on_log(text));
        std::string line = "[NOLA:" + role() + "] " + std::string(text);
        auto& s = in::log_sink();
        if (s.out) s.out(line);
        else
        {
            std::fwrite(line.data(), 1, line.size(), stdout);
            std::fputc('\n', stdout);
        }
    }
    void logf(const char* fmt, ...)
    {
        char b[2048];
        va_list a;
        va_start(a, fmt);
        int n = std::vsnprintf(b, sizeof b, fmt, a);
        va_end(a);
        if (n < 0) return;
        log(std::string_view(b, (size_t)std::min<int>(n, (int)sizeof b - 1)));
    }
    void log_every(std::string_view key, double secs, std::string_view text)
    {
        static std::unordered_map<std::string, std::pair<double, int>> m;
        auto& e = m[std::string(key)];
        double t = now();
        if (e.first != 0 && t - e.first < secs) { ++e.second; return; }
        if (e.second > 0) log(std::string(text) + " (+" + std::to_string(e.second) + " dropped)");
        else log(text);
        e.first = t == 0 ? 1e-9 : t;
        e.second = 0;
    }
    bool log_n(int& counter, int max, std::string_view text)
    {
        if (++counter > max) return false;
        log(text);
        return true;
    }

    bool read_bin(const std::wstring& path, std::string& out) { return in::read_all(path, out); }
    bool read_text(const std::wstring& path, std::string& out)
    {
        std::string raw;
        if (!in::read_all(path, raw)) return false;
        out.clear();
        out.reserve(raw.size());
        for (size_t i = 0; i < raw.size(); ++i)
        {
            if (raw[i] == '\r' && i + 1 < raw.size() && raw[i + 1] == '\n') continue;
            if (raw[i] == 0x1A) break;
            out += raw[i];
        }
        return true;
    }
    bool write_bin(const std::wstring& path, std::string_view bytes, bool atomic)
    {
        if (atomic) return in::write_atomic(path, bytes);
        return in::write_raw(path, bytes, CREATE_ALWAYS, false);
    }
    bool write_text(const std::wstring& path, std::string_view text, bool atomic) { return write_bin(path, in::to_crlf(text), atomic); }
    bool append_text(const std::wstring& path, std::string_view text) { return in::write_raw(path, in::to_crlf(text), OPEN_ALWAYS, true); }
    bool file_exists(const std::wstring& path) { return in::file_ok(path); }
    bool remove_file(const std::wstring& path) { return DeleteFileW(path.c_str()) != 0; }
    std::vector<std::string> split(std::string_view s, char sep, bool keep_empty)
    {
        std::vector<std::string> v;
        size_t b = 0;
        while (true)
        {
            size_t e = s.find(sep, b);
            std::string_view p = s.substr(b, e == std::string_view::npos ? std::string_view::npos : e - b);
            if (keep_empty || !p.empty()) v.emplace_back(p);
            if (e == std::string_view::npos) break;
            b = e + 1;
        }
        return v;
    }

    bool tsv_d1_text(std::string_view text, std::vector<TsvRow>& rows, std::string* err) { return in::tsv_parse(text, rows, err); }
    bool tsv_d1(const std::wstring& path, std::vector<TsvRow>& rows, std::string* err)
    {
        std::string t;
        if (!in::read_all(path, t)) { if (err) *err = "cannot read"; return false; }
        return in::tsv_parse(t, rows, err);
    }
    bool tsv_plain(const std::wstring& path, std::vector<std::vector<std::string>>& rows, bool skip_header)
    {
        std::string t;
        if (!read_text(path, t)) return false;
        rows.clear();
        bool first = true;
        for (auto& l : split(t, '\n', false))
        {
            if (first && skip_header) { first = false; continue; }
            first = false;
            rows.push_back(split(l, '\t', true));
        }
        return true;
    }

    std::wstring data_file(std::wstring_view name)
    {
#ifdef S2X_RELEASE
        (void)name;
        return {};
#else
        return bridge_dir() + L"s2x_data\\" + std::wstring(name);
#endif
    }
    bool data(std::string_view name, std::string& out)
    {
#ifndef S2X_RELEASE
        if (in::read_all(data_file(in::u2w(name)), out)) return true;
#endif
        return in::data_res(name, out);
    }
    bool data_tsv(std::string_view name, std::vector<TsvRow>& rows, std::string* err)
    {
        std::string t;
        if (!data(name, t)) { if (err) *err = "no data " + std::string(name); return false; }
        return in::tsv_parse(t, rows, err);
    }
    bool table_res(std::string_view name, std::string& out)
    {
        if (fi("taball", 1) != 1 || !data(name, out)) return false;
        out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
        return true;
    }
    bool table(std::string_view name, std::string& out)
    {
        if (read_text(bridge_file(in::u2w(name)), out)) return true;
        return table_res(name, out);
    }
    bool release_build()
    {
#ifdef S2X_RELEASE
        return true;
#else
        return false;
#endif
    }
    std::vector<std::string> data_names() { return in::data_res_names(); }
}
