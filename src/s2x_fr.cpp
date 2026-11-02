#if defined(S2X_FR) && !defined(S2X_RELEASE)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include "s2x.h"
#include "s2x_fr.h"
#include "s2x_fr_core.h"

namespace
{
    using namespace frc;

    struct FamRow
    {
        const char* word;
        Fam fam;
    };
    const FamRow kFam[] = {
#define FR_FAM(w, f) {w, f},
#include "s2x_fr_tab.inc"
#undef FR_FAM
    };

    std::atomic<int> g_state{0};
    std::atomic<bool> g_on{false};
    Ring g_ring;
    HANDLE g_file = INVALID_HANDLE_VALUE;
    HANDLE g_map = nullptr;
    void* g_view = nullptr;
    std::wstring g_dir;
    std::wstring g_path;
    std::mutex g_mu;
    TagTab g_tx[x0::kMaxClients];
    TagTab g_rx[x0::kMaxClients];
    TagTab g_hd;
    std::vector<std::pair<std::string, double>> g_trig;
    double g_lastDump = -1e9;
    std::string g_lastFile;
    uint64_t g_freq = 0;
    std::wstring g_logSrc;
    uint64_t g_logOff = 0;
    std::mutex g_logMu;

    uint64_t qpc()
    {
        LARGE_INTEGER v;
        QueryPerformanceCounter(&v);
        return (uint64_t)v.QuadPart;
    }

    int64_t wall_us()
    {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return (int64_t)((u.QuadPart - 116444736000000000ULL) / 10ULL);
    }

    Clock clk()
    {
        Clock c;
        c.qpc = qpc();
        c.wall_us = wall_us();
        c.t = g_freq ? (double)c.qpc / (double)g_freq : 0;
        return c;
    }

    bool live() { return g_state.load(std::memory_order_acquire) == 2 && g_on.load(std::memory_order_relaxed); }

    void put(uint8_t kind, uint8_t fam, int peer, std::string_view tag, uint32_t seq, uint32_t hash, uint32_t size, uint16_t aux, std::string_view text)
    {
        Clock c = clk();
        g_ring.put(c.qpc, c.wall_us, kind, fam, peer, tag, seq, hash, size, aux, text);
    }

    std::string w2u(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string o((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), o.data(), n, nullptr, nullptr);
        return o;
    }

    std::wstring u2w(std::string_view s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring o((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), o.data(), n);
        return o;
    }

    std::string clean(std::string_view s, size_t max)
    {
        std::string o;
        for (char ch : s)
        {
            if (o.size() >= max) break;
            bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '_' || ch == '-';
            o += ok ? ch : '_';
        }
        return o.empty() ? std::string("x") : o;
    }

    bool session_dir_name(const std::wstring& n)
    {
        if (n.size() < 17) return false;
        for (int i = 0; i < 15; ++i)
        {
            bool dig = n[(size_t)i] >= L'0' && n[(size_t)i] <= L'9';
            if (i == 8 ? n[(size_t)i] != L'_' : !dig) return false;
        }
        return n[15] == L'_';
    }

    void rm_tree(const std::wstring& dir)
    {
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileW((dir + L"*").c_str(), &d);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                DeleteFileW((dir + d.cFileName).c_str());
            } while (FindNextFileW(h, &d));
            FindClose(h);
        }
        RemoveDirectoryW(dir.substr(0, dir.size() - 1).c_str());
    }

    void prune_sessions(const std::wstring& root, size_t keep)
    {
        std::vector<std::wstring> v;
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileW((root + L"*").c_str(), &d);
        if (h == INVALID_HANDLE_VALUE) return;
        do
        {
            if ((d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && session_dir_name(d.cFileName)) v.push_back(d.cFileName);
        } while (FindNextFileW(h, &d));
        FindClose(h);
        std::sort(v.begin(), v.end(), [](const std::wstring& a, const std::wstring& b) { return a > b; });
        for (size_t i = keep; i < v.size(); ++i) rm_tree(root + v[i] + L"\\");
    }

    std::wstring ue4ss_log()
    {
        HMODULE m = GetModuleHandleW(L"UE4SS.dll");
        if (!m) return {};
        wchar_t b[MAX_PATH] = {};
        DWORD n = GetModuleFileNameW(m, b, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return {};
        std::wstring p(b, n);
        size_t s = p.find_last_of(L"\\/");
        return s == std::wstring::npos ? std::wstring() : p.substr(0, s + 1) + L"UE4SS.log";
    }

    void mirror_log()
    {
        if (g_logSrc.empty() || g_dir.empty()) return;
        std::lock_guard<std::mutex> g(g_logMu);
        HANDLE in = CreateFileW(g_logSrc.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (in == INVALID_HANDLE_VALUE) return;
        LARGE_INTEGER sz;
        if (!GetFileSizeEx(in, &sz) || (uint64_t)sz.QuadPart == g_logOff) { CloseHandle(in); return; }
        std::wstring dst = g_dir + L"UE4SS.log";
        if ((uint64_t)sz.QuadPart < g_logOff)
        {
            g_logOff = 0;
            DeleteFileW(dst.c_str());
        }
        HANDLE out = CreateFileW(dst.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (out == INVALID_HANDLE_VALUE) { CloseHandle(in); return; }
        LARGE_INTEGER off;
        off.QuadPart = (LONGLONG)g_logOff;
        SetFilePointerEx(in, off, nullptr, FILE_BEGIN);
        std::vector<char> buf(1 << 16);
        uint64_t left = (uint64_t)sz.QuadPart - g_logOff;
        while (left > 0)
        {
            DWORD want = (DWORD)std::min<uint64_t>(left, buf.size()), got = 0, put = 0;
            if (!ReadFile(in, buf.data(), want, &got, nullptr) || got == 0) break;
            if (!WriteFile(out, buf.data(), got, &put, nullptr) || put != got) break;
            g_logOff += got;
            left -= got;
        }
        CloseHandle(out);
        CloseHandle(in);
    }

    std::string host_name()
    {
        wchar_t b[128] = {};
        DWORD n = 127;
        if (!GetComputerNameExW(ComputerNameDnsHostname, b, &n) || n == 0) return "pc";
        return w2u(std::wstring(b, n));
    }

    bool open_ring()
    {
        int64_t want = x0::fi("flightn", 131072);
        uint32_t cap = (uint32_t)std::clamp<int64_t>(want, 1024, 1 << 20);
        std::wstring base = x0::bridge_dir() + L"nola_debug\\";
        std::wstring root = base + L"sessions\\";
        CreateDirectoryW(base.c_str(), nullptr);
        CreateDirectoryW(root.c_str(), nullptr);
        int64_t keep = std::clamp<int64_t>(x0::fi("dbgsessions", 10), 1, 500);
        prune_sessions(root, (size_t)keep - 1);
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t sn[64];
        std::swprintf(sn, 64, L"%04u%02u%02u_%02u%02u%02u_", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        g_dir = root + sn + u2w(clean(x0::role(), 8)) + L"\\";
        CreateDirectoryW(g_dir.c_str(), nullptr);
        DWORD pid = GetCurrentProcessId();
        g_path = g_dir + L"fr_" + std::to_wstring(pid) + L".ring";
        if (x0::fi("dbglogcopy", 1) == 1) g_logSrc = ue4ss_log();
        size_t bytes = bytes_for(cap);
        g_file = CreateFileW(g_path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (g_file == INVALID_HANDLE_VALUE) return false;
        g_map = CreateFileMappingW(g_file, nullptr, PAGE_READWRITE, (DWORD)((uint64_t)bytes >> 32), (DWORD)(bytes & 0xffffffffULL), nullptr);
        if (!g_map) return false;
        g_view = MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
        if (!g_view) return false;
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        g_freq = (uint64_t)f.QuadPart;
        if (!g_ring.attach(g_view, bytes, cap, true, g_freq, qpc(), wall_us(), pid)) return false;
        Hdr* h = g_ring.hdr();
        put_str(h->role, sizeof h->role, x0::role());
        put_str(h->host, sizeof h->host, host_name());
        put_str(h->build, sizeof h->build, __DATE__ " " __TIME__);
        return true;
    }

    void dump(std::string_view reason)
    {
        if (!g_ring.ok()) return;
        std::vector<uint8_t> buf(g_ring.bytes());
        if (!g_ring.copy_to(buf.data(), buf.size())) return;
        Hdr* h = reinterpret_cast<Hdr*>(buf.data());
        Clock c = clk();
        h->dump_wall_us = c.wall_us;
        h->dump_qpc = c.qpc;
        put_str(h->reason, sizeof h->reason, reason);
        SYSTEMTIME st;
        GetLocalTime(&st);
        char ts[32];
        std::snprintf(ts, sizeof ts, "%04u%02u%02u_%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        std::string name = "fr_" + clean(x0::role(), 8) + "_" + std::to_string(GetCurrentProcessId()) + "_" + ts + "_" + clean(reason, 24) + ".frd";
        std::wstring path = g_dir + u2w(name);
        bool ok = x0::write_bin(path, std::string_view(reinterpret_cast<const char*>(buf.data()), buf.size()), true);
        mirror_log();
        g_ring.hdr()->dumps += ok ? 1 : 0;
        g_lastFile = name;
        x0::logf("NOLA fr: dump %s reason=%.*s records=%llu ok=%d", name.c_str(), (int)reason.size(), reason.data(), (unsigned long long)g_ring.count(), (int)ok);
    }

    std::string status_line()
    {
        if (!g_ring.ok()) return "fr off";
        uint64_t n = g_ring.count();
        return std::string("fr ") + (g_on ? "on" : "paused") + " records=" + std::to_string(n) + " cap=" + std::to_string(g_ring.cap()) +
               " dumps=" + std::to_string(g_ring.hdr()->dumps) + (g_lastFile.empty() ? "" : " last=" + g_lastFile);
    }

    std::string cmd_fr(const std::vector<std::string>& a)
    {
        std::string sub = a.size() >= 2 ? a[1] : "status";
        if (sub == "dump")
        {
            std::string why = "cmd_" + clean(a.size() >= 3 ? a[2] : "now", 24);
            if (g_state.load() != 2) return status_line();
            std::lock_guard<std::mutex> g(g_mu);
            put(k_trigger, f_session, -1, "trigger", 0, fnv(why), 0, 0, why);
            dump(why);
            return status_line();
        }
        if (sub == "dir") return w2u(g_dir);
        if (sub == "mark")
        {
            std::string t;
            for (size_t i = 2; i < a.size(); ++i) t += (i > 2 ? " " : "") + a[i];
            if (live()) put(k_state, f_session, -1, "mark", 0, fnv(t), (uint32_t)t.size(), 0, t);
            return "marked " + t;
        }
        return status_line();
    }

    Fam fam_of(std::string_view w)
    {
        for (const auto& r : kFam)
            if (w == r.word) return r.fam;
        return f_other;
    }

    void init()
    {
        int z = 0;
        if (!g_state.compare_exchange_strong(z, 1)) return;
        bool ok = open_ring();
        if (!ok)
        {
            g_state = 3;
            x0::logf("NOLA fr: flight recorder NOT started (file %s, error %lu)", w2u(g_path).c_str(), GetLastError());
            return;
        }
        g_on = x0::fi("flightrec", 1) == 1;
        g_state = 2;
        x0::on_flag(x0::Mod::x0, "flightrec", [](const std::string&, const x0::Val&, const x0::Val& after) { g_on = after.integer() == 1; });
        x0::cmd(x0::Mod::x0, "fr", [](const std::vector<std::string>& a) { return cmd_fr(a); });
        x0::status(x0::Mod::x0, [] { return status_line(); });
        x0::key(x0::Mod::x0, VK_F11, [] { x0::fr::trigger("hotkey"); }, {VK_CONTROL});
        x0::every(x0::Mod::x0, "frLogCopy", x0::Rate::t1000, [] {
            static double at = 0;
            double t = x0::now();
            if (t - at < 5) return;
            at = t;
            mirror_log();
        });
        put(k_meta, f_session, -1, "start", 0, 0, 0, 0, std::string("role=") + x0::role());
        x0::logf("NOLA fr: flight recorder on=%d file %s cap=%u records (%zu MB), dumps on crash (ring file), Ctrl+F11, devcmd fr dump, INV line, session end; log copy %s",
                 (int)g_on.load(), w2u(g_path).c_str(), g_ring.cap(), g_ring.bytes() >> 20, g_logSrc.empty() ? "off" : w2u(g_logSrc).c_str());
    }

    void reg_init() { init(); }
    ::x0::ModReg g_reg(::x0::Mod::x0, "fr", &reg_init);

    int64_t json_int(std::string_view text, std::string_view key)
    {
        std::string pat = "\"" + std::string(key) + "\":";
        size_t p = text.substr(0, std::min<size_t>(text.size(), 400)).find(pat);
        if (p == std::string_view::npos) return -1;
        p += pat.size();
        int64_t v = 0;
        bool any = false;
        while (p < text.size() && text[p] >= '0' && text[p] <= '9')
        {
            v = v * 10 + (text[p] - '0');
            ++p;
            any = true;
        }
        return any ? v : -1;
    }

    std::string json_num_str(std::string_view text, std::string_view key)
    {
        std::string pat = "\"" + std::string(key) + "\":";
        size_t p = text.substr(0, std::min<size_t>(text.size(), 400)).find(pat);
        if (p == std::string_view::npos) return "?";
        p += pat.size();
        size_t e = p;
        while (e < text.size() && e - p < 16 && ((text[e] >= '0' && text[e] <= '9') || text[e] == '.' || text[e] == '-')) ++e;
        return std::string(text.substr(p, e - p));
    }
}

namespace x0::fr
{
    void trigger(std::string_view reason)
    {
        if (!live()) return;
        std::string r = clean(reason, 40);
        Clock c = clk();
        std::lock_guard<std::mutex> g(g_mu);
        put(k_trigger, f_session, -1, "trigger", 0, fnv(r), 0, 0, r);
        bool seen = false;
        for (auto& [k, at] : g_trig)
            if (k == r)
            {
                seen = true;
                if (c.t - at < 20) return;
                at = c.t;
            }
        if (!seen) g_trig.push_back({r, c.t});
        if (c.t - g_lastDump < 3) return;
        g_lastDump = c.t;
        dump(r);
    }

    void track(std::string_view tag)
    {
        if (g_state.load() != 2) return;
        std::lock_guard<std::mutex> g(g_mu);
        Hdr* h = g_ring.hdr();
        std::string cur(h->tracked, strnlen(h->tracked, sizeof h->tracked));
        std::string t = clean(tag, 7);
        for (auto& p : split(cur, ',', false))
            if (p == t) return;
        cur += (cur.empty() ? "" : ",") + t;
        put_str(h->tracked, sizeof h->tracked, cur);
    }

    void handled(std::string_view tag, std::string_view value)
    {
        if (!live() || tag.size() > 7) return;
        std::lock_guard<std::mutex> g(g_mu);
        TagTab::E* e = g_hd.find(tag);
        uint32_t h = fnv(value);
        if (!e || e->hash == h) return;
        e->hash = h;
        put(k_handled, f_net, -1, tag, 0, h, (uint32_t)value.size(), 0, value);
    }

    void event(uint8_t fam, std::string_view tag, std::string_view text)
    {
        if (!live()) return;
        put(k_log, fam < f_count ? fam : f_other, -1, tag, 0, fnv(text), (uint32_t)text.size(), 0, text);
    }

    void on_log(std::string_view line)
    {
        if (!live()) return;
        std::string_view s = line;
        bool net = false;
        auto word = [](std::string_view& v) {
            while (!v.empty() && v.front() == ' ') v.remove_prefix(1);
            size_t e = 0;
            while (e < v.size() && v[e] != ' ' && v[e] != ':' && v[e] != '(' && v[e] != ',') ++e;
            std::string_view w = v.substr(0, e);
            v.remove_prefix(e);
            return w;
        };
        std::string_view w = word(s);
        if (w == "NOLA") w = word(s);
        else if (w == "S2NET") { net = true; w = word(s); }
        Fam f = net ? f_net : fam_of(w);
        put(k_log, f, -1, w, 0, fnv(line), (uint32_t)line.size(), 0, line);
        if (f == f_inv)
        {
            std::string_view rest = s;
            std::string_view id = word(rest);
            trigger("inv_" + std::string(id.empty() ? std::string_view("x") : id));
        }
    }

    void tx_snap(std::string_view sid, int64_t f, size_t bytes, std::string_view x1)
    {
        if (!live()) return;
        Hdr* h = g_ring.hdr();
        if (!sid.empty() && std::strncmp(h->sid, std::string(sid).c_str(), sizeof h->sid) != 0)
        {
            put_str(h->sid, sizeof h->sid, sid);
            put(k_meta, f_session, -1, "sid", 0, fnv(sid), 0, 0, std::string("sid=") + std::string(sid));
        }
        h->net_pid = x0::my_pid();
        Clock c = clk();
        uint32_t seq = (uint32_t)(f & 0xffffffff);
        if ((f & 7) == 0)
        {
            char b[80];
            std::snprintf(b, sizeof b, "f=%lld x1=%zuB", (long long)f, x1.size());
            g_ring.put(c.qpc, c.wall_us, k_tx_snap, f_net, -1, "snap", seq, 0, (uint32_t)bytes, 0, b);
        }
        tag_diff(g_ring, g_tx[0], c, k_tx_tag, x0::view_pid(0), seq, x1, 0.25);
    }

    void tx_view(int view, int peer_pid, int64_t f, std::string_view x1)
    {
        if (!live() || view <= 0 || view >= x0::kMaxClients) return;
        Clock c = clk();
        tag_diff(g_ring, g_tx[view], c, k_tx_tag, peer_pid, (uint32_t)(f & 0xffffffff), x1, 0.25);
    }

    void rx_snap(int view, int peer_pid, std::string_view text, std::string_view sid, std::string_view x1raw)
    {
        if (!live() || view < 0 || view >= x0::kMaxClients) return;
        Clock c = clk();
        int64_t f = json_int(text, "f");
        uint32_t seq = (uint32_t)(f < 0 ? 0 : (f & 0xffffffff));
        if (f >= 0 && (f & 7) == 0)
        {
            std::string b = "sid=" + std::string(sid) + " f=" + std::to_string(f) + " t=" + json_num_str(text, "t");
            g_ring.put(c.qpc, c.wall_us, k_rx_snap, f_net, peer_pid, "snap", seq, fnv(sid), (uint32_t)text.size(), (uint16_t)view, b);
        }
        tag_diff(g_ring, g_rx[view], c, k_rx_tag, peer_pid, seq, x1raw, 0.25);
    }

    void rx_drop(int view, int peer_pid, std::string_view why)
    {
        if (!live()) return;
        put(k_drop, f_net, peer_pid, "drop", 0, fnv(why), 0, (uint16_t)(view < 0 ? 0 : view), why);
    }

    void state(std::string_view what)
    {
        if (!live()) return;
        put(k_state, f_session, -1, "state", 0, fnv(what), 0, 0, what);
    }

    void session_end(std::string_view why)
    {
        if (g_state.load() != 2) return;
        put(k_state, f_session, -1, "end", 0, fnv(why), 0, 0, why);
        {
            std::lock_guard<std::mutex> g(g_mu);
            dump(std::string("end_") + clean(why, 20));
            g_lastDump = clk().t;
        }
        mirror_log();
        if (why == "quit")
        {
            g_ring.hdr()->state = s_closed;
            FlushViewOfFile(g_view, 0);
        }
        for (auto& t : g_rx) t.clear();
        for (auto& t : g_tx) t.clear();
    }
}
#endif
