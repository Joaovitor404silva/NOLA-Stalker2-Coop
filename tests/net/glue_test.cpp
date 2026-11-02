#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../../src/s2r_net.h"
#include "../../src/s2r_x0.h"
#include "../../src/s2x.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace stub
{
    std::map<std::string, std::string> flags;
    std::map<std::string, std::vector<x0::FlagCb>> cbs;
    std::vector<void (*)()> inits;
    std::vector<std::function<std::string()>> stats;
    std::mutex mu;
    std::vector<std::function<void()>> posted;
    std::string role = "host";
    std::wstring dir;
    std::vector<std::string> logs;
}

namespace x0
{
    ModReg::ModReg(Mod, const char*, void (*init)()) { stub::inits.push_back(init); }
    const std::string& role() { return stub::role; }
    bool is_host() { return stub::role == "host"; }
    bool is_client() { return stub::role == "client"; }
    const std::string& boot_role()
    {
        static const std::string b = "host";
        return b;
    }
    bool set_session_role(std::string_view r, std::string_view)
    {
        std::string w = r.empty() ? std::string("host") : std::string(r);
        if (w == stub::role) return false;
        stub::role = w;
        return true;
    }
    const std::wstring& bridge_dir() { return stub::dir; }
    std::wstring bridge_file(std::wstring_view name) { return stub::dir + std::wstring(name); }
    void log(std::string_view text)
    {
        stub::logs.emplace_back(text);
        std::printf("LOG %.*s\n", (int)text.size(), text.data());
        std::fflush(stdout);
    }
    void logf(const char* fmt, ...)
    {
        char b[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(b, sizeof b, fmt, ap);
        va_end(ap);
        log(b);
    }
    int64_t fi(std::string_view key, int64_t def)
    {
        auto it = stub::flags.find(std::string(key));
        return it == stub::flags.end() ? def : std::atoll(it->second.c_str());
    }
    double fd(std::string_view key, double def)
    {
        auto it = stub::flags.find(std::string(key));
        return it == stub::flags.end() ? def : std::atof(it->second.c_str());
    }
    std::string fs(std::string_view key, std::string_view def)
    {
        auto it = stub::flags.find(std::string(key));
        return it == stub::flags.end() ? std::string(def) : it->second;
    }
    bool fon(std::string_view key) { return fi(key, 0) == 1; }
    void on_flag(Mod, std::string_view key, FlagCb cb) { stub::cbs[std::string(key)].push_back(std::move(cb)); }
    void status(Mod, std::function<std::string()> line) { stub::stats.push_back(std::move(line)); }
void after(Mod, double, std::function<void()>) {}
    void post(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> g(stub::mu);
        stub::posted.push_back(std::move(fn));
    }
    bool data(std::string_view name, std::string& out)
    {
        wchar_t mod[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, mod, MAX_PATH);
        std::wstring p(mod, n);
        for (int i = 0; i < 3; ++i)
        {
            size_t k = p.find_last_of(L"\\/");
            if (k == std::wstring::npos) return false;
            p.resize(k);
        }
        std::ifstream f(p + L"\\data\\" + std::wstring(name.begin(), name.end()), std::ios::binary);
        if (!f) return false;
        out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        return true;
    }
    int g_resets = 0;
    void coop_session_reset() { ++g_resets; }
    void ui_session_begin() {}
}

namespace s2r
{
    void steam_session(int, const std::string&, int) {}
    bool steam_host_peer(std::string&, int&) { return false; }
    int steam_stop_exit() { return 0; }
}

static void pump()
{
    std::vector<std::function<void()>> q;
    {
        std::lock_guard<std::mutex> g(stub::mu);
        q.swap(stub::posted);
    }
    for (auto& f : q) f();
}

static void setflag(const std::string& k, const std::string& v)
{
    stub::flags[k] = v;
    for (auto& cb : stub::cbs[k]) cb(k, x0::Val{}, x0::Val{});
}

static std::string unhex(const std::string& s)
{
    auto v = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    std::string o;
    for (size_t i = 0; i + 1 < s.size(); i += 2) o.push_back((char)(v(s[i]) * 16 + v(s[i + 1])));
    return o;
}

static std::string hex(const std::string& s)
{
    static const char* h = "0123456789abcdef";
    std::string o;
    for (unsigned char c : s)
    {
        o.push_back(h[c >> 4]);
        o.push_back(h[c & 15]);
    }
    return o;
}

int main(int argc, char** argv)
{
    if (argc < 6) return 2;
    std::string listen = argv[1], peerport = argv[2], frames = argv[3], outp = argv[4];
    stub::dir = s2r::widen(argv[5]);
    if (!stub::dir.empty() && stub::dir.back() != L'\\') stub::dir.push_back(L'\\');
    std::vector<std::string> fr;
    {
        std::ifstream f(frames);
        std::string l;
        while (std::getline(f, l))
            if (!l.empty()) fr.push_back(unhex(l));
    }
    int rc = 0;
    stub::flags["joinmenu"] = "0";
    stub::flags["cpp_net"] = "0";
    for (auto f : stub::inits) f();
    pump();
    if (s2r::x0_active())
    {
        std::printf("FAIL active with cpp_net=0\n");
        rc = 1;
    }
    stub::flags["netfiles"] = "0";
    stub::flags["netlisten"] = listen;
    stub::flags["netpeerport"] = peerport;
    setflag("cpp_net", "1");
    pump();
    if (!s2r::x0_active() || !s2r::x0_mailbox())
    {
        std::printf("FAIL not active in mailbox mode\n");
        rc = 1;
    }
    std::ofstream out(outp);
    double t0 = s2r::clock_s();
    size_t i = 0;
    double next = t0;
    std::vector<double> sent_at;
    while (true)
    {
        double now = s2r::clock_s();
        if (i < fr.size() && now >= next)
        {
            s2r::x0_out(fr[i]);
            out << "O\t" << i << "\t" << (now - t0) << "\n";
            ++i;
            next += 0.1;
        }
        std::string t;
        if (s2r::x0_in(t)) out << "I\t" << (now - t0) << "\t" << hex(t) << "\n";
        pump();
        if (i >= fr.size() && now - next > 1.0) break;
        Sleep(2);
    }
    for (auto& s : stub::stats) std::printf("STATUS %s\n", s().c_str());
    setflag("cpp_net", "0");
    pump();
    if (s2r::x0_active())
    {
        std::printf("FAIL still active after cpp_net=0\n");
        rc = 1;
    }
    stub::flags["netfiles"] = "1";
    setflag("cpp_net", "1");
    pump();
    bool fm = s2r::x0_active() && !s2r::x0_mailbox();
    std::printf("%s file mode after restart\n", fm ? "OK" : "FAIL");
    if (!fm) rc = 1;
    s2r::x0_stop();
    pump();
    stub::flags["netfiles"] = "0";
    setflag("joinmenu", "1");
    pump();
    bool dorm = !s2r::x0_active() && s2r::x0_dormant() && s2r::x0_mailbox() && s2r::x0_status().find("dormant") != std::string::npos;
    std::printf("%s joinmenu=1 boot: relay dormant, mailbox without files\n", dorm ? "OK" : "FAIL");
    if (!dorm) rc = 1;
    std::string why;
    stub::flags["menurole"] = "0";
    bool bad = !s2r::x0_session(2, "127.0.0.1", 7000, why) && stub::role == "host";
    std::printf("%s menurole=0: join refused on a host install (%s)\n", bad ? "OK" : "FAIL", why.c_str());
    if (!bad) rc = 1;
    stub::flags.erase("menurole");
    bool on = s2r::x0_session(1, "", 0, why) && s2r::x0_active() && s2r::x0_session_mode() == 1 && x0::g_resets == 1 && stub::role == "host";
    std::printf("%s host session starts the relay\n", on ? "OK" : "FAIL");
    if (!on) rc = 1;
    bool off = s2r::x0_session(0, "", 0, why) && !s2r::x0_active() && s2r::x0_dormant() && x0::g_resets == 2;
    std::printf("%s session end: relay stopped, dormant again\n", off ? "OK" : "FAIL");
    if (!off) rc = 1;
    bool cj = s2r::x0_session(2, "127.0.0.1", std::atoi(listen.c_str()), why) && s2r::x0_active() && s2r::x0_session_peer() == "127.0.0.1:" + listen && stub::role == "client";
    s2r::Config cc = s2r::x0_config();
    cj = cj && cc.peer_host == "127.0.0.1" && cc.peer_port == std::atoi(listen.c_str());
    std::printf("%s client session uses the runtime peer, not the flags (%s)\n", cj ? "OK" : "FAIL", s2r::x0_session_peer().c_str());
    if (!cj) rc = 1;
    s2r::x0_session(0, "", 0, why);
    bool back = stub::role == "host";
    std::printf("%s menurole=1: join on a host-boot install runs as client, session end restores the boot role\n", back ? "OK" : "FAIL");
    if (!back) rc = 1;
    stub::flags.erase("netlisten");
    stub::flags.erase("netpeerport");
    s2r::Config sc = s2r::x0_config_role(false);
    s2r::Config hc = s2r::x0_config_role(true);
    bool sp = sc.role == "client" && hc.role == "host" && sc.peer_port == hc.listen && sc.listen == hc.peer_port && sc.listen != hc.listen;
    std::printf("%s scan config: a joiner targets the host listen port\n", sp ? "OK" : "FAIL");
    if (!sp) rc = 1;
    stub::role = "host";
    pump();
    std::printf("GLUE %s\n", rc ? "FAIL" : "OK");
    return rc;
}
