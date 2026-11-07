#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../../src/s2r_net.h"
#include "../../src/s2r_steam.h"
#include "../../src/s2r_x0.h"
#include "../../src/s2x.h"
#include "fake_world.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" __declspec(dllimport) void SteamAPI_RunCallbacks();

namespace stub
{
    std::map<std::string, std::string> flags;
    std::map<std::string, std::vector<x0::FlagCb>> cbs;
    std::vector<void (*)()> inits;
    std::vector<std::function<std::string()>> stats;
    std::map<int, std::function<void()>> keys;
    std::map<std::string, x0::CmdFn> cmds;
    std::mutex mu;
    std::vector<std::function<void()>> posted;
    std::string role = "host";
    std::string boot = "host";
    std::wstring dir;
    std::mutex lmu;
    std::vector<std::string> logs;
    int resets = 0;
}

namespace x0
{
    ModReg::ModReg(Mod, const char*, void (*init)()) { stub::inits.push_back(init); }
    const std::string& role() { return stub::role; }
    bool is_host() { return stub::role == "host"; }
    bool is_client() { return stub::role == "client"; }
    Iface& iface()
    {
        static Iface i;
        return i;
    }
    bool set_session_role(std::string_view r, std::string_view)
    {
        std::string w = r.empty() ? stub::boot : std::string(r);
        if (w == stub::role) return false;
        stub::role = w;
        return true;
    }
    const std::wstring& bridge_dir() { return stub::dir; }
    std::wstring bridge_file(std::wstring_view name) { return stub::dir + std::wstring(name); }
    void log(std::string_view text)
    {
        {
            std::lock_guard<std::mutex> g(stub::lmu);
            stub::logs.emplace_back(text);
        }
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
    int key(Mod, int vk, std::function<void()> on_press, std::initializer_list<int>, bool)
    {
        stub::keys[vk] = std::move(on_press);
        return vk;
    }
    void cmd(Mod, const char* name, CmdFn fn) { stub::cmds[name] = std::move(fn); }
    void post(std::function<void()> fn)
    {
        std::lock_guard<std::mutex> g(stub::mu);
        stub::posted.push_back(std::move(fn));
    }
    bool data(std::string_view, std::string&) { return false; }
    void coop_session_reset() { ++stub::resets; }
    void ui_session_begin() {}
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

static bool has_log(const std::string& sub)
{
    std::lock_guard<std::mutex> g(stub::lmu);
    for (auto& s : stub::logs)
        if (s.find(sub) != std::string::npos) return true;
    return false;
}

template <class F> static bool wait_for(F f, double secs)
{
    double t = s2r::clock_s();
    while (s2r::clock_s() - t < secs)
    {
        pump();
        if (f()) return true;
        Sleep(5);
    }
    pump();
    return f();
}

static int g_fail = 0;

static void check(const char* name, bool ok, const std::string& d = {})
{
    std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, d.c_str());
    std::fflush(stdout);
    if (!ok) g_fail++;
}

static bool peer_is(const std::string& p) { return s2r::x0_status().find("peer=" + p + ":") != std::string::npos; }
static bool st_is(const char* s) { return s2r::steam_status().rfind(std::string("steam ") + s, 0) == 0; }

struct Notes
{
    std::mutex mu;
    std::vector<s2r::SteamNote> v;
    bool find(s2r::SteamNote::Kind k, s2r::SteamNote* out = nullptr)
    {
        std::lock_guard<std::mutex> g(mu);
        for (auto& n : v)
            if (n.kind == k)
            {
                if (out) *out = n;
                return true;
            }
        return false;
    }
    void clear()
    {
        std::lock_guard<std::mutex> g(mu);
        v.clear();
    }
};

static int free_port()
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int al = sizeof(a);
    bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    getsockname(s, reinterpret_cast<sockaddr*>(&a), &al);
    int p = ntohs(a.sin_port);
    closesocket(s);
    return p;
}

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    stub::dir = std::wstring(cwd) + L"\\glue\\";
    CreateDirectoryW(stub::dir.c_str(), nullptr);
    int lp = free_port();
    stub::flags["cpp_net"] = "1";
    stub::flags["netfiles"] = "0";
    stub::flags["netpeer"] = "127.0.0.9";
    stub::flags["netlocal"] = "1";
    stub::flags["netlisten"] = std::to_string(lp);
    stub::flags["joinmenu"] = "1";
    stub::flags["maxclients"] = "1";
    stub::flags["steamdev"] = "1";
    stub::flags["steamtag"] = "glue";
    Notes notes;
    s2r::steam_menu([&](const s2r::SteamNote& n) {
        std::lock_guard<std::mutex> g(notes.mu);
        notes.v.push_back(n);
    });
    for (auto f : stub::inits) f();
    pump();
    check("Home key and steam command registered, defaults net=steam", stub::keys.count(VK_HOME) == 1 && stub::cmds.count("steam") == 1);
    check("boot: relay dormant (solo until the menu), Steam service waits for the game's steam_api64", s2r::x0_dormant() && !s2r::x0_active() && st_is("waiting_for_steam"),
          s2r::steam_status());

    World w;
    FakeDll A;
    bool ok = A.load(LoadLibraryW(L"steam_api64.dll"));
    check("fake steam_api64 loaded", ok);
    if (!ok) return 1;
    const uint64_t A_ID = 76561198000000001ULL, B_ID = 76561198000000002ULL, S_ID = 76561198000000099ULL;
    w.names[A_ID] = "HostGuy";
    w.names[B_ID] = "FriendGal";
    w.names[S_ID] = "HostPal";
    A.init(&w.be, A_ID, 1, 1, 1);
    A.app(480);
    std::atomic<bool> run{true};
    std::thread pt([&] {
        while (run.load())
        {
            SteamAPI_RunCallbacks();
            Sleep(2);
        }
    });
    check("Steam service ready at the main menu, no lobby yet (role chosen in the menu)", wait_for([&] { return st_is("ready"); }, 5) && w.lobbies.empty(), s2r::steam_status());

    std::string why;
    bool hs = s2r::x0_session(1, "", 0, why);
    bool hup = wait_for([&] { return st_is("hosting") && notes.find(s2r::SteamNote::Kind::hosting); }, 5);
    uint64_t lobby = 0;
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        if (!w.lobbies.empty()) lobby = w.lobbies.begin()->first;
    }
    s2r::SteamNote hn;
    notes.find(s2r::SteamNote::Kind::hosting, &hn);
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        auto& L = w.lobbies[lobby];
        check("Host co-op (x0_session 1) -> lobby for maxclients+1 = 2, public with nola_dev tag (steamdev=1), hosting note 1/2",
              hs && hup && L.max == 2 && L.type == 2 && L.data["nola_dev"] == "glue" && L.data["nola"] == "2" && hn.n == 1 && hn.max == 2 && hn.lobby == lobby, s2r::steam_status());
    }
    check("steaminvite=1 -> invite dialog opened once on hosting", wait_for([&] { return w.invites.size() == 1; }, 2));
    check("relay host on netpeer while no Steam member (LAN co-op unchanged)", s2r::x0_active() && peer_is("127.0.0.9"), s2r::x0_status());

    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        w.lobbies[lobby].members.push_back(B_ID);
    }
    s2r::SteamNote mi;
    bool in = wait_for([&] { return notes.find(s2r::SteamNote::Kind::member_in, &mi) && peer_is("127.0.0.1"); }, 3);
    check("Steam member joins a 1-slot host -> note member_in, relay peer rerouted to the Steam link", in && mi.name == "FriendGal" && mi.n == 2 && mi.max == 2 &&
                                                                                                          has_log("the one co-op slot follows the Steam lobby member"),
          s2r::x0_status());
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        auto& m = w.lobbies[lobby].members;
        m.erase(std::remove(m.begin(), m.end(), B_ID), m.end());
    }
    check("member leaves -> note member_out, relay back on netpeer", wait_for([&] { return notes.find(s2r::SteamNote::Kind::member_out) && peer_is("127.0.0.9"); }, 3),
          s2r::x0_status());

    stub::keys[VK_HOME]();
    check("Home key opens the invite dialog", wait_for([&] { return w.invites.size() == 2; }, 2));
    std::string st = stub::cmds["steam"]({"steam", "status"});
    check("steam status command", st.rfind("steam hosting", 0) == 0, st);

    setflag("maxclients", "3");
    pump();
    bool l4 = wait_for([&] {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        auto it = w.lobbies.find(lobby);
        return it != w.lobbies.end() && it->second.max == 4 && it->second.data["max"] == "4";
    }, 3);
    check("maxclients 3 while hosting -> same lobby, limit 4, relay becomes a hub, Steam link multi", l4 && s2r::x0_status().find(" hub clients=") != std::string::npos &&
                                                                                                        s2r::steam_status().find("multi=1") != std::string::npos,
          s2r::steam_status());
    setflag("maxclients", "1");
    pump();

    notes.clear();
    s2r::x0_session(0, "", 0, why);
    bool closed = wait_for([&] { return notes.find(s2r::SteamNote::Kind::closed) && st_is("ready"); }, 3);
    check("end co-op (x0_session 0) -> lobby closed, Steam back to ready, relay dormant", closed && !w.lobbies.count(lobby) && s2r::x0_dormant() && !s2r::x0_active(),
          s2r::steam_status());

    stub::role = "client";
    stub::boot = "client";
    uint64_t hl = w.be.create_lobby(&w, S_ID, 2, 4);
    w.be.set_data(&w, S_ID, hl, "nola", "2");
    w.be.set_data(&w, S_ID, hl, "host", std::to_string(S_ID).c_str());
    w.be.set_data(&w, S_ID, hl, "name", "HostPal");
    w.be.set_data(&w, S_ID, hl, "nola_dev", "glue");
    notes.clear();
    {
        std::string d(16, '\0');
        std::memcpy(d.data(), &hl, 8);
        std::memcpy(d.data() + 8, &S_ID, 8);
        A.post(333, d.data(), 16);
    }
    s2r::SteamNote jn;
    bool jd = wait_for([&] { return notes.find(s2r::SteamNote::Kind::joined, &jn); }, 3);
    check("Join Game / invite (333) while at the menu -> note joined with the local link port for the menu", jd && jn.name == "HostPal" && jn.port > 0 && jn.lobby == hl &&
                                                                                                                 w.member_of(hl, A_ID),
          s2r::steam_status());
    bool js = s2r::x0_session(2, "127.0.0.1", jn.port, why);
    check("menu connects the relay to the Steam link -> relay joined via 127.0.0.1:<link>, Steam stays in the lobby",
          js && peer_is("127.0.0.1") && s2r::x0_status().find(":" + std::to_string(jn.port) + " ") != std::string::npos && st_is("joined"), s2r::x0_status() + " | " + why);
    s2r::x0_session(0, "", 0, why);
    check("end co-op as a client -> leaves the Steam lobby", wait_for([&] { return !w.member_of(hl, A_ID) && st_is("ready"); }, 3), s2r::steam_status());

    notes.clear();
    bool fq = s2r::steam_find();
    s2r::SteamNote fj;
    bool fjd = wait_for([&] { return notes.find(s2r::SteamNote::Kind::joined, &fj); }, 3);
    check("Join co-op with no invite -> steam_find searches the tagged lobbies and joins", fq && fjd && fj.lobby == hl && fj.why == "find", s2r::steam_status());
    s2r::x0_session(0, "", 0, why);
    wait_for([&] { return st_is("ready"); }, 3);
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        w.lobbies.erase(hl);
    }
    notes.clear();
    s2r::steam_find();
    check("steam_find with no host anywhere -> note none (the menu falls back to the LAN scan)", wait_for([&] { return notes.find(s2r::SteamNote::Kind::none); }, 3) &&
                                                                                                    !notes.find(s2r::SteamNote::Kind::join_fail));
    stub::flags["steamjoin"] = "0";
    check("steamjoin=0 -> steam_find off (LAN scan only)", !s2r::steam_find());
    stub::flags["steamjoin"] = "1";

    std::string jc = stub::cmds["steam"]({"steam", "join", "12345"});
    notes.clear();
    check("steam join <id> command queues a join; a missing lobby -> join_fail note", jc == "join queued" && wait_for([&] {
              s2r::SteamNote f;
              return notes.find(s2r::SteamNote::Kind::join_fail, &f) && f.code == 2;
          }, 3));

    A.init(&w.be, A_ID, 0, 1, 1);
    stub::flags["netsteamwait"] = "1";
    notes.clear();
    setflag("net", "steam");
    bool un = wait_for([&] { return notes.find(s2r::SteamNote::Kind::unavailable); }, 4);
    check("Steam never ready -> note unavailable after netsteamwait, status failed, Join co-op falls back to LAN", un && st_is("failed") && !s2r::steam_find(), s2r::steam_status());

    A.init(&w.be, A_ID, 1, 1, 1);
    setflag("netsteamwait", "60");
    check("changing a Steam flag retries Steam", wait_for([&] { return st_is("ready"); }, 5), s2r::steam_status());

    {
        auto block = reinterpret_cast<void (*)(int)>(GetProcAddress(A.mod, "Fake_BlockReceive"));
        auto mark_free = reinterpret_cast<void (*)(int)>(GetProcAddress(A.mod, "Fake_Free"));
        auto calls = reinterpret_cast<void (*)(uint64_t*, uint64_t*)>(GetProcAddress(A.mod, "Fake_RecvCalls"));
        void** slot = s2r::steam::find_import_slot(GetModuleHandleW(nullptr), "steam_api64.dll", "SteamAPI_RunCallbacks");
        void* real = reinterpret_cast<void*>(GetProcAddress(A.mod, "SteamAPI_RunCallbacks"));
        auto recv_n = [&] {
            uint64_t c = 0, f = 0;
            calls(&c, &f);
            return c;
        };
        auto after_free = [&] {
            uint64_t c = 0, f = 0;
            calls(&c, &f);
            return f;
        };
        auto log_at = [&](const std::string& sub) {
            std::lock_guard<std::mutex> g(stub::lmu);
            for (size_t i = 0; i < stub::logs.size(); ++i)
                if (stub::logs[i].find(sub) != std::string::npos) return (long long)i;
            return -1LL;
        };
        auto polling = [&] {
            uint64_t c0 = recv_n();
            return wait_for([&] { return recv_n() > c0 + 3; }, 3);
        };
        check("exit test setup: fake exports, RunCallbacks import slot hooked, link polls Receive", block && mark_free && calls && slot && *slot != real && polling(),
              s2r::steam_status());

        block(400);
        Sleep(60);
        double t = s2r::clock_s();
        s2r::x0_pre_exit();
        double el = s2r::clock_s() - t;
        mark_free(1);
        Sleep(300);
        long long ib = log_at("GameEngine::PreExit -> BYE to the other game");
        long long is = log_at("GameEngine::PreExit -> Steam link thread stopped and joined before the Steam modules unload");
        check("PreExit while Receive blocks 400 ms: BYE line first, then the link thread joined (< 1.5 s), slot restored, no Steam call after the free, status off",
              ib >= 0 && is > ib && el >= 0.25 && el < 1.5 && after_free() == 0 && *slot == real && st_is("off"), std::to_string(el) + " s, " + s2r::steam_status());
        mark_free(0);
        block(0);

        setflag("netsteamwait", "60");
        bool back = wait_for([&] { return st_is("ready"); }, 5) && polling();
        block(-1);
        Sleep(60);
        t = s2r::clock_s();
        s2r::x0_pre_exit();
        el = s2r::clock_s() - t;
        check("PreExit while Receive never returns: bounded by the 1000 ms join, did-not-stop line, slot restored, no deadlock",
              back && has_log("Steam link thread did not stop in time") && el >= 0.9 && el < 3.5 && *slot == real && st_is("off"), std::to_string(el) + " s");
        setflag("netsteamwait", "60");
        pump();
        check("restart refused while the left thread still runs", has_log("not started: the last link thread did not stop"));
        block(0);
        Sleep(200);
        setflag("netsteamwait", "60");
        check("left thread ends once Receive returns; Steam restarts", wait_for([&] { return st_is("ready"); }, 5) && polling(), s2r::steam_status());

        stub::flags["steamexitstop"] = "0";
        s2r::x0_pre_exit();
        check("steamexitstop=0: PreExit leaves the link running (old behaviour)", s2r::steam_status().rfind("steam ready", 0) == 0, s2r::steam_status());
        stub::flags.erase("steamexitstop");
    }

    setflag("net", "ip");
    pump();
    check("net=ip -> Steam service off", s2r::steam_status() == "steam off", s2r::steam_status());
    {
        size_t n0;
        {
            std::lock_guard<std::mutex> g(stub::lmu);
            n0 = stub::logs.size();
        }
        double t = s2r::clock_s();
        s2r::x0_pre_exit();
        double el = s2r::clock_s() - t;
        bool steam_line = false;
        {
            std::lock_guard<std::mutex> g(stub::lmu);
            for (size_t i = n0; i < stub::logs.size(); ++i)
                if (stub::logs[i].find("Steam link") != std::string::npos) steam_line = true;
        }
        check("PreExit with no Steam link (net=ip): no-op, no Steam line, returns at once", !steam_line && el < 0.1, std::to_string(el) + " s");
    }
    setflag("menurole", "0");
    check("menurole=0: x0_session(1) refused on a client install", !s2r::x0_session(1, "", 0, why));
    stub::flags.erase("menurole");

    s2r::steam_stop();
    s2r::x0_stop();
    pump();
    auto k = A.counters();
    check("nothing left registered in steam_api64, every call on the pump thread", k.registered_cbs == 0 && k.registered_results == 0 && k.off_pump == 0,
          std::to_string(k.registered_cbs) + " " + std::to_string(k.registered_results) + " " + std::to_string(k.off_pump));
    run = false;
    pt.join();
    std::printf("GLUE STEAM %s\n", g_fail ? "FAIL" : "OK");
    return g_fail ? 1 : 0;
}
