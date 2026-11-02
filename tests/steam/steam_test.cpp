#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#include <windows.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include "../../src/s2r_net.h"
#include "../../src/s2r_steam.h"
#include "../../src/s2m_steam_logic.h"
#include "fake_world.h"

#include <atomic>
#include <cstdio>
#include <fstream>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" __declspec(dllimport) void SteamAPI_RunCallbacks();

namespace st = s2r::steam;
using s2r::clock_s;

static int g_fail = 0;
static int g_pass = 0;

static void check(const char* name, bool ok, const std::string& detail = {})
{
    std::printf("%s %s %s\n", ok ? "PASS" : "FAIL", name, detail.c_str());
    std::fflush(stdout);
    if (ok) g_pass++;
    else g_fail++;
}

template <class F> static bool wait_for(F f, double secs)
{
    double t = clock_s();
    while (clock_s() - t < secs)
    {
        if (f()) return true;
        Sleep(2);
    }
    return f();
}

struct Logs
{
    std::mutex mu;
    std::vector<std::string> v;
    st::LogFn fn(const char* tag)
    {
        return [this, tag](const std::string& s)
        {
            {
                std::lock_guard<std::mutex> g(mu);
                v.push_back(s);
            }
            std::printf("  [%s] %s\n", tag, s.c_str());
            std::fflush(stdout);
        };
    }
    size_t count(const std::string& sub)
    {
        std::lock_guard<std::mutex> g(mu);
        size_t n = 0;
        for (auto& s : v)
            if (s.find(sub) != std::string::npos) n++;
        return n;
    }
    bool has(const std::string& sub) { return count(sub) > 0; }
};

struct Evs
{
    std::mutex mu;
    std::vector<st::Event> v;
    st::EventFn fn()
    {
        return [this](const st::Event& e)
        {
            std::lock_guard<std::mutex> g(mu);
            v.push_back(e);
        };
    }
    bool find(st::Event::Kind k, st::Event* out = nullptr, int code = -1)
    {
        std::lock_guard<std::mutex> g(mu);
        for (auto& e : v)
            if (e.kind == k && (code < 0 || e.code == code))
            {
                if (out) *out = e;
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

struct Pump
{
    std::atomic<bool> run{false};
    std::atomic<bool> hold{false};
    std::atomic<uint64_t> n{0};
    void* volatile* slot = nullptr;
    std::thread th;
    DWORD tid = 0;
    void start(void** s)
    {
        slot = s;
        run = true;
        th = std::thread([this] {
            tid = GetCurrentThreadId();
            while (run.load())
            {
                if (!hold.load())
                {
                    if (slot) reinterpret_cast<void (*)()>(*slot)();
                    else SteamAPI_RunCallbacks();
                    n++;
                }
                Sleep(1);
            }
        });
    }
    void stop()
    {
        run = false;
        if (th.joinable()) th.join();
    }
};

struct Udp
{
    SOCKET s = INVALID_SOCKET;
    int port = 0;
    bool open(int want = 0)
    {
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)want);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int al = sizeof(a);
        if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || getsockname(s, reinterpret_cast<sockaddr*>(&a), &al) != 0) return false;
        port = ntohs(a.sin_port);
        u_long nb = 1;
        ioctlsocket(s, FIONBIO, &nb);
        BOOL off = FALSE;
        DWORD ret = 0;
        WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
        int rb = 8 << 20;
        setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rb), sizeof(rb));
        return true;
    }
    bool send_to(int p, const std::string& d)
    {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons((u_short)p);
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return sendto(s, d.data(), (int)d.size(), 0, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == (int)d.size();
    }
    bool recv(std::string& out, int& from_port, double secs)
    {
        static std::vector<char> buf(70000);
        double t = clock_s();
        while (true)
        {
            sockaddr_in f{};
            int fl = sizeof(f);
            int r = recvfrom(s, buf.data(), (int)buf.size(), 0, reinterpret_cast<sockaddr*>(&f), &fl);
            if (r >= 0)
            {
                out.assign(buf.data(), (size_t)r);
                from_port = ntohs(f.sin_port);
                return true;
            }
            if (clock_s() - t > secs) return false;
            Sleep(1);
        }
    }
    void close()
    {
        if (s != INVALID_SOCKET) closesocket(s);
        s = INVALID_SOCKET;
    }
};

static int free_port()
{
    Udp u;
    u.open();
    int p = u.port;
    u.close();
    return p;
}

static std::string read_file(const char* p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static std::vector<std::string> make_frames(const char* path, int n, uint64_t seed, int npcs, const char* sid)
{
    using s2r::js::V;
    V b;
    std::string txt = read_file(path);
    if (txt.empty() || !s2r::js::parse(txt, b) || b.t != V::object)
        s2r::js::parse(R"({"v":2,"sid":"s","f":1,"t":1.0,"x":1.0,"y":2.0,"z":3.0,"yaw":0.0,"need":"","apps":[],"npcs":[]})", b);
    b.set("sid", V::Str(sid));
    std::mt19937_64 r(seed);
    double x0 = b.get("x") && b.get("x")->is_num() ? b.get("x")->num() : 0;
    double t0 = b.get("t") && b.get("t")->is_num() ? b.get("t")->num() : 0;
    int64_t f0 = b.get("f") && b.get("f")->is_num() ? (int64_t)b.get("f")->num() : 0;
    std::vector<std::string> out;
    for (int i = 0; i < n; ++i)
    {
        V m = b;
        m.set("f", V::Int(f0 + i + 1));
        m.set("t", V::Real(t0 + 0.1 * (i + 1)));
        m.set("x", V::Real(x0 + 3.3 * i));
        V arr = V::Arr();
        for (int k = 0; k < npcs; ++k)
        {
            V o = V::Object();
            char a[16];
            std::snprintf(a, sizeof a, "%08x", (unsigned)(k * 7919));
            o.set("id", V::Str("BP_AI_Human_C_" + std::to_string(100 + k)));
            o.set("x", V::Real(1000.0 + k + (double)(r() % 1000) / 10.0));
            o.set("y", V::Real(2000.0 + (double)(r() % 1000) / 10.0));
            o.set("z", V::Real(50.0));
            o.set("yaw", V::Real((double)(r() % 3600) / 10.0 - 180.0));
            o.set("a", V::Str(a));
            o.set("s", V::Int(k));
            o.set("pose", V::Str("0,1,2"));
            arr.a.push_back(std::move(o));
        }
        m.set("npcs", std::move(arr));
        out.push_back(s2r::js::dumps(m, true));
    }
    return out;
}

struct Probe final : st::CbBase
{
    int which = -1;
    void* p = nullptr;
    bool io = false;
    uint64_t h = 0;
    void Run(void* d) override
    {
        which = 1;
        p = d;
    }
    void Run(void* d, bool i, uint64_t c) override
    {
        which = 0;
        p = d;
        io = i;
        h = c;
    }
    int GetCallbackSizeBytes() override { return 77; }
};

static void test_layout()
{
    Probe pr;
    void** vt = *reinterpret_cast<void***>(&pr);
    reinterpret_cast<void (*)(void*, void*)>(vt[1])(&pr, reinterpret_cast<void*>(0x1234));
    bool a = pr.which == 1 && pr.p == reinterpret_cast<void*>(0x1234);
    reinterpret_cast<void (*)(void*, void*, bool, uint64_t)>(vt[0])(&pr, reinterpret_cast<void*>(0x55), true, 99);
    bool b = pr.which == 0 && pr.io && pr.h == 99;
    int sz = reinterpret_cast<int (*)(void*)>(vt[2])(&pr);
    size_t of = (size_t)(reinterpret_cast<uint8_t*>(&pr.flags) - reinterpret_cast<uint8_t*>(&pr));
    size_t oi = (size_t)(reinterpret_cast<uint8_t*>(&pr.id) - reinterpret_cast<uint8_t*>(&pr));
    check("CbBase vtable: [0]=Run(p,io,h) [1]=Run(p) [2]=size, flags@8 id@12 (matches steam_api64 dispatch)", a && b && sz == 77 && of == 8 && oi == 12,
          "of=" + std::to_string(of) + " oi=" + std::to_string(oi));
}

static void test_parse()
{
    uint64_t l = 0;
    bool ok = true;
    ok &= st::parse_connect_lobby(L"\"C:\\G\\Stalker2.exe\" +connect_lobby 109775241234567890", l) && l == 109775241234567890ULL;
    ok &= st::parse_connect_lobby(L"x.exe -connect_lobby=42 -log", l) && l == 42;
    ok &= st::parse_connect_lobby(L"x.exe +connect_lobby \"77\"", l) && l == 77;
    ok &= !st::parse_connect_lobby(L"x.exe +connect_lobby", l);
    ok &= !st::parse_connect_lobby(L"x.exe +connect_lobby abc", l);
    ok &= !st::parse_connect_lobby(L"x.exe +connect_lobby 0", l);
    ok &= !st::parse_connect_lobby(L"x.exe +connect_lobby12", l);
    ok &= !st::parse_connect_lobby(L"x.exe +connect_lobby 12x", l);
    ok &= !st::parse_connect_lobby(L"x.exe -log", l);
    check("parse_connect_lobby (+/-connect_lobby, =, quotes, rejects junk)", ok);
    st::Net n = st::Net::ip;
    bool a = st::parse_net("steam", n) && n == st::Net::steam;
    bool b = st::parse_net("ip", n) && n == st::Net::ip;
    bool c = !st::parse_net("Steam", n) && !st::parse_net("", n);
    check("parse_net steam|ip", a && b && c);
}

static void test_slots()
{
    HMODULE exe = GetModuleHandleW(nullptr);
    void** k = st::find_import_slot(exe, "KERNEL32.dll", "GetCurrentThreadId");
    void* real = (void*)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetCurrentThreadId");
    check("find_import_slot finds a normal import (kernel32 GetCurrentThreadId)", k && *k == real);
    void** d = st::find_import_slot(exe, "steam_api64.dll", "SteamAPI_RunCallbacks");
    check("find_import_slot finds the delay import SteamAPI_RunCallbacks", d != nullptr);
    check("find_import_slot: unknown name -> null", !st::find_import_slot(exe, "steam_api64.dll", "SteamAPI_Nope") && !st::find_import_slot(exe, "nope.dll", "x"));
}

#include "steam_multi.inc"

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    const uint64_t A_ID = 76561198000000001ULL, B_ID = 76561198000000002ULL, S_ID = 76561198000000099ULL;
    test_layout();
    test_parse();
    test_slots();
    test_texts();

    Logs la, lb, lf;
    {
        st::Link L;
        st::Config c;
        c.role = "host";
        c.net_port = 40001;
        c.wait_s = 1;
        std::atomic<int> nf{0};
        bool absent = !GetModuleHandleW(L"steam_api64.dll");
        bool s = L.start(c, la.fn("absent"), [&] { nf++; });
        bool f = wait_for([&] { return nf == 1; }, 3);
        check("steam_api64 never loaded -> waits netsteamwait, then fails (LAN co-op stays)",
              absent && s && f && la.has("the game did not load steam_api64.dll within 1 s") && la.has("Steam co-op is off this session (LAN co-op still works)"));
        L.stop();
    }

    World w;
    w.names[A_ID] = "HostGuy";
    w.names[B_ID] = "FriendGal";
    w.names[S_ID] = "Stranger";
    void** aslot = st::find_import_slot(GetModuleHandleW(nullptr), "steam_api64.dll", "SteamAPI_RunCallbacks");
    void* thunk = *aslot;
    Udp na, nb;
    na.open();
    nb.open();
    st::Link LA, LB;
    st::Config ca;
    ca.role = "host";
    ca.net_port = na.port;
    ca.wait_s = 10;
    ca.loop_ms = 1;
    ca.poll_s = 0.05;
    ca.unhook_wait_s = 2;
    ca.linger_s = 0.3;
    std::atomic<int> fail_a{0}, fail_b{0};
    Evs ea, eb;
    check("host link starts before the game loads steam_api64", LA.start(ca, la.fn("A"), [&] { fail_a++; }, ea.fn()) && LA.state() == st::State::waiting);
    {
        st::HostOpts ho;
        ho.members = 2;
        ho.relay_port = na.port;
        LA.host_open(ho);
    }
    Sleep(100);
    FakeDll A, B;
    bool la_ok = A.load(LoadLibraryW(L"steam_api64.dll"));
    bool lb_ok = B.load(LoadLibraryW(L"fake_b.dll"));
    check("two fake steam_api64 instances loaded", la_ok && lb_ok && A.mod != B.mod);
    if (!la_ok || !lb_ok) return 1;
    w.route[A_ID] = A.deliver;
    w.route[B_ID] = B.deliver;
    A.init(&w.be, A_ID, 1, 1, 1);
    B.init(&w.be, B_ID, 1, 1, 1);
    void* areal = (void*)GetProcAddress(A.mod, "SteamAPI_RunCallbacks");
    void* breal = (void*)GetProcAddress(B.mod, "SteamAPI_RunCallbacks");
    bool waits = wait_for([&] { return la.has("waiting for the game's first SteamAPI_RunCallbacks"); }, 2);
    check("dll loaded but import not bound yet -> link waits, slot untouched", waits && *aslot == thunk && thunk != areal && !la.has("hooked"));
    void* bslot = breal;
    Pump pa, pb;
    pa.start(nullptr);
    bool hooked = wait_for([&] { return la.has("hooked the game's SteamAPI_RunCallbacks"); }, 3);
    check("the game's first call binds the slot, then the link hooks it", hooked && *aslot != areal && *aslot != thunk);
    pb.start(&bslot);
    st::Config cb = ca;
    cb.role = "client";
    cb.net_port = nb.port;
    cb.module = B.mod;
    cb.pump_slot = &bslot;
    cb.hook = 1;
    check("client link starts (injected slot, second instance)", LB.start(cb, lb.fn("B"), [&] { fail_b++; }, eb.fn()));
    check("client slot hooked", wait_for([&] { return bslot != breal; }, 2));
    bool hosting = wait_for([&] { return LA.state() == st::State::hosting && LA.lobby() != 0; }, 5);
    check("host: ready -> friends-only lobby created -> hosting", hosting, st::state_name(LA.state()));
    uint64_t lobby = LA.lobby();
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        auto& L = w.lobbies[lobby];
        check("lobby is friends-only, max 2, nola=2, host/max/name data, no dev tag", L.type == 1 && L.max == 2 && L.data["nola"] == "2" && L.data["host"] == std::to_string(A_ID) &&
                                                                                          L.data["max"] == "2" && L.data["name"] == "HostGuy" && !L.data.count("nola_dev"));
    }
    st::Event he;
    check("hosting event names the host, 1/2 players", ea.find(st::Event::Kind::hosting, &he) && he.lobby == lobby && he.n == 1 && he.max == 2 && he.name == "HostGuy");
    check("client: ready, not hosting (role-agnostic link)", wait_for([&] { return LB.state() == st::State::ready; }, 5) && !LB.hosting() && LB.lobby() == 0);
    check("ready log names the Steam user", la.has("ready: user HostGuy (76561198000000001), app 1643320, role host, overlay on"));

    LB.invite();
    check("invite on a game that is not hosting -> told to pick Host co-op", wait_for([&] { return lb.has("not hosting a co-op session"); }, 2));
    LA.invite();
    check("host invite key -> overlay invite dialog for the lobby", wait_for([&] {
              std::lock_guard<std::recursive_mutex> g(w.mu);
              return w.invites.size() == 1 && w.invites[0].first == A_ID && w.invites[0].second == lobby;
          }, 2));

    {
        std::string d(16, '\0');
        std::memcpy(d.data(), &lobby, 8);
        std::memcpy(d.data() + 8, &A_ID, 8);
        B.post(333, d.data(), 16);
    }
    bool joined = wait_for([&] { return LB.state() == st::State::joined && LB.peer() == A_ID && LA.peer() == B_ID; }, 5);
    check("friend accepts invite (GameLobbyJoinRequested 333) -> joined, both peers set", joined, LB.status() + " | " + LA.status());
    check("join logs name the peer", lb.has("joined the lobby of HostGuy (76561198000000001): co-op link up") && la.has("FriendGal (76561198000000002) joined the lobby"));
    {
        st::Event j, mi;
        bool a = wait_for([&] { return ea.find(st::Event::Kind::member_in, &mi); }, 2);
        bool b = eb.find(st::Event::Kind::joined, &j);
        check("events: client joined (host, 2/2, via invite), host member_in (FriendGal 2/2)", a && b && j.id == A_ID && j.name == "HostGuy" && j.n == 2 && j.max == 2 &&
                                                                                                 j.why == "Steam invite or Join Game" && mi.id == B_ID && mi.name == "FriendGal" && mi.n == 2 && mi.max == 2);
    }

    std::vector<std::string> dgs;
    {
        auto fr = make_frames("C:\\s2coop_inst\\A\\out.json", 20, 7, 60, "raw-A");
        s2r::proto::Sender snd;
        s2r::proto::AppCache have;
        double now = 100;
        for (auto& f : fr)
        {
            std::vector<std::string> out;
            std::string msg;
            snd.split(f, now, have, out, msg);
            now += 0.1;
            for (auto& o : out) dgs.push_back(o);
        }
    }
    size_t relay_n = dgs.size();
    std::mt19937_64 rng(12345);
    for (int sz : {1, 2, 1199, 1200, 1201, 3921, 8192, 60000, 65000})
    {
        std::string s((size_t)sz, '\0');
        for (auto& ch : s) ch = (char)(rng() & 0xff);
        dgs.push_back(s);
    }
    for (int i = 0; i < 150; ++i)
    {
        size_t sz = 1 + (size_t)(rng() % 60000);
        std::string s(sz, '\0');
        for (auto& ch : s) ch = (char)(rng() & 0xff);
        dgs.push_back(s);
    }
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        w.sends.clear();
        w.record = true;
    }
    size_t ok_ab = 0, port_ok = 0;
    std::string first_bad;
    for (size_t i = 0; i < dgs.size(); ++i)
    {
        na.send_to(LA.port(), dgs[i]);
        std::string got;
        int fp = 0;
        if (nb.recv(got, fp, 2) && got == dgs[i]) ok_ab++;
        else if (first_bad.empty()) first_bad = "index " + std::to_string(i) + " size " + std::to_string(dgs[i].size()) + " got " + std::to_string(got.size());
        if (fp == LB.port()) port_ok++;
    }
    check("host->client: every datagram arrives byte-identical and in order", ok_ab == dgs.size(),
          std::to_string(ok_ab) + "/" + std::to_string(dgs.size()) + " (" + std::to_string(relay_n) + " real relay_v2 datagrams) " + first_bad);
    check("client game socket sees datagrams from 127.0.0.1:<client shim port>", port_ok == dgs.size());
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        bool same = w.sends.size() == dgs.size();
        for (size_t i = 0; same && i < dgs.size(); ++i) same = w.sends[i].from == A_ID && w.sends[i].to == B_ID && w.sends[i].ch == 0 && w.sends[i].data == dgs[i];
        check("Steam message payloads == UDP datagrams (no framing added)", same, std::to_string(w.sends.size()) + " sends");
        w.sends.clear();
    }
    size_t ok_ba = 0;
    for (size_t i = 0; i < dgs.size(); i += 3)
    {
        nb.send_to(LB.port(), dgs[i]);
        std::string got;
        int fp = 0;
        if (na.recv(got, fp, 2) && got == dgs[i] && fp == LA.port()) ok_ba++;
    }
    check("client->host: byte-identical", ok_ba == (dgs.size() + 2) / 3, std::to_string(ok_ba));
    check("client accepted the host's session (SteamNetworkingMessagesSessionRequest 1251)", LB.stats().sess_ok >= 1 || LA.stats().sess_ok >= 1,
          "B sess_ok=" + std::to_string(LB.stats().sess_ok) + " A sess_ok=" + std::to_string(LA.stats().sess_ok));
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        w.record = false;
    }

    B.deliver(S_ID, "zz", 2, 0);
    bool rej = wait_for([&] { return lb.has("session request from 76561198000000099 not accepted: not the lobby host"); }, 2);
    std::string junk;
    int jp = 0;
    bool none = !nb.recv(junk, jp, 0.3);
    check("stranger session request rejected, nothing reaches the game socket", rej && none && LB.stats().sess_rej == 1);
    {
        Udp x;
        x.open();
        uint64_t sent0 = LB.stats().sent;
        x.send_to(LB.port(), "junk");
        bool lf_ok = wait_for([&] { return LB.stats().local_foreign >= 1; }, 2);
        bool nofwd = !na.recv(junk, jp, 0.3) && LB.stats().sent == sent0;
        check("loopback datagram from a foreign local port is dropped", lf_ok && nofwd);
        x.close();
    }

    na.close();
    nb.close();
    {
        auto fa = make_frames("C:\\s2coop_inst\\A\\out.json", 40, 1, 60, "e2e-A");
        auto fb = make_frames("C:\\s2coop_inst\\B\\out.json", 40, 2, 3, "e2e-B");
        CreateDirectoryW(L"e2e", nullptr);
        for (const wchar_t* d : {L"e2e\\uh", L"e2e\\uc", L"e2e\\sh", L"e2e\\sc"}) CreateDirectoryW(d, nullptr);
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        std::wstring base = std::wstring(cwd) + L"\\e2e\\";
        int u1 = free_port(), u2 = free_port();
        s2r::Config uh, uc, sh, sc;
        uh.role = "host";
        uh.listen = u1;
        uh.peer_host = "127.0.0.1";
        uh.peer_port = u2;
        uh.dir = base + L"uh\\";
        uh.loop_ms = 5;
        uc = uh;
        uc.role = "client";
        uc.listen = u2;
        uc.peer_port = u1;
        uc.dir = base + L"uc\\";
        sh = uh;
        sh.listen = LA.net_port();
        sh.peer_port = LA.port();
        sh.dir = base + L"sh\\";
        sc = uc;
        sc.listen = LB.net_port();
        sc.peer_port = LB.port();
        sc.dir = base + L"sc\\";
        s2r::Net NUh, NUc, NSh, NSc;
        Logs ln;
        bool started = NUh.start(uh, ln.fn("uh")) && NUc.start(uc, ln.fn("uc")) && NSh.start(sh, ln.fn("sh")) && NSc.start(sc, ln.fn("sc"));
        check("relay Net pairs start (UDP direct and via Steam shim)", started);
        auto run_phase = [&](int gap_ms, size_t n, double settle, const char* name)
        {
            for (size_t i = 0; i < n; ++i)
            {
                NUh.put_out(fa[i % fa.size()] + std::string(i / fa.size(), ' '));
                NSh.put_out(fa[i % fa.size()] + std::string(i / fa.size(), ' '));
                NUc.put_out(fb[i % fb.size()]);
                NSc.put_out(fb[i % fb.size()]);
                Sleep((DWORD)gap_ms);
            }
            NUh.put_out(fa.back());
            NSh.put_out(fa.back());
            NUc.put_out(fb.back());
            NSc.put_out(fb.back());
            Sleep((DWORD)(settle * 1000));
            std::string a1, a2, b1, b2;
            uint64_t q1 = 0, q2 = 0, q3 = 0, q4 = 0;
            NUc.take_in(a1, q1);
            NSc.take_in(a2, q2);
            NUh.take_in(b1, q3);
            NSh.take_in(b2, q4);
            s2r::js::V v;
            size_t npcs = 0;
            if (s2r::js::parse(a2, v))
                if (auto* np = v.get("npcs")) npcs = np->a.size();
            std::string d = std::string(name) + ": host->client " + std::to_string(a2.size()) + " B, " + std::to_string(npcs) + " npcs; client->host " +
                            std::to_string(b2.size()) + " B";
            check((std::string("relay_v2 end to end: Steam route in.json == direct UDP in.json (") + name + ")").c_str(),
                  !a1.empty() && a1 == a2 && !b1.empty() && b1 == b2 && npcs == 60, d);
        };
        run_phase(50, 40, 0.8, "settled 20 fps");
        run_phase(5, 200, 1.0, "fast stream");
        auto s = LA.stats();
        check("Steam shim stats clean during relay", s.send_err == 0 && s.sent > 0 && LB.stats().recv > 0 && LB.stats().send_err == 0,
              "A sent=" + std::to_string(s.sent) + " bytes=" + std::to_string(s.sent_bytes) + " B recv=" + std::to_string(LB.stats().recv));
        NUh.stop();
        NUc.stop();
        NSh.stop();
        NSc.stop();
    }

    {
        std::string d(16, '\0');
        std::memcpy(d.data(), &lobby, 8);
        std::memcpy(d.data() + 8, &S_ID, 8);
        A.post(333, d.data(), 16);
    }
    check("hosting game ignores a join request (333)", wait_for([&] { return la.has("ignored: this game is hosting a co-op session"); }, 2) && LA.lobby() == lobby);
    A.init(&w.be, A_ID, 1, 1, 0);
    LA.invite();
    check("overlay off -> invite still called, user told about Join Game", wait_for([&] { return la.has("Steam overlay is off"); }, 2) && w.invites.size() == 2);

    LB.leave();
    bool left = wait_for([&] { return LB.state() == st::State::ready && !w.member_of(lobby, B_ID) && LA.peer() == 0; }, 3);
    check("client leave -> idle, host sees the peer leave", left && la.has("FriendGal (76561198000000002) left the lobby"));

    LB.stop();
    cb.cmdline = L"\"C:\\G\\Stalker2-Win64-Shipping.exe\" +connect_lobby " + std::to_wstring(lobby);
    check("client restart with +connect_lobby", LB.start(cb, lb.fn("B"), [&] { fail_b++; }, eb.fn()));
    bool rj = wait_for([&] { return LB.state() == st::State::joined && LB.peer() == A_ID && LA.peer() == B_ID; }, 5);
    check("+connect_lobby on the command line -> joins the lobby", rj && lb.has("(+connect_lobby)"));

    uint64_t other = w.be.create_lobby(&w, S_ID, 1, 4);
    LB.join(other);
    bool nl = wait_for([&] { return lb.has("is not a NOLA lobby"); }, 3);
    check("non-NOLA lobby -> left, back to ready", nl && wait_for([&] { return LB.state() == st::State::ready; }, 2) && !w.member_of(other, B_ID) && !w.member_of(lobby, B_ID));
    w.be.set_data(&w, S_ID, other, "nola", "2");
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        w.lobbies[other].owner = B_ID;
        w.lobbies[other].members = {B_ID};
    }
    LB.join(other);
    check("lobby without another owner -> left", wait_for([&] { return lb.has("has no host: left it"); }, 3) && wait_for([&] { return LB.state() == st::State::ready; }, 2));
    LB.join(lobby);
    check("rejoin by command", wait_for([&] { return LB.state() == st::State::joined && LA.peer() == B_ID; }, 3));

    multi_test(w, A, B, LA, LB, la, lb, ea, eb, A_ID, B_ID, S_ID);
    LB.join(LA.lobby());
    check("B back in the host lobby before the host quits", wait_for([&] { return LB.state() == st::State::joined; }, 3));
    eb.clear();
    LA.stop();
    check("host stop restores the exe import slot", *aslot == areal);
    bool gone = wait_for([&] { return LB.state() == st::State::ready && lb.has("the host left the lobby") && eb.find(st::Event::Kind::host_left); }, 3);
    check("host leaves -> client back to ready, host_left event", gone);
    LB.stop();
    check("client stop restores its slot", bslot == breal);
    auto ka = A.counters(), kb = B.counters();
    check("after stop no callbacks or call results stay registered", ka.registered_cbs == 0 && kb.registered_cbs == 0 && ka.registered_results == 0 && kb.registered_results == 0,
          std::to_string(ka.registered_cbs) + "/" + std::to_string(kb.registered_cbs) + "/" + std::to_string(ka.registered_results) + "/" +
              std::to_string(kb.registered_results));
    check("call results: size and id matched the dll (no io_failure)", ka.results_io_fail == 0 && kb.results_io_fail == 0 && ka.results_run >= 1 && kb.results_run >= 3,
          "A run=" + std::to_string(ka.results_run) + " B run=" + std::to_string(kb.results_run));
    check("every matchmaking/friends/callback call ran on the game's pump thread", ka.off_pump == 0 && kb.off_pump == 0 && ka.api_calls > 10 && kb.api_calls > 10,
          "A calls=" + std::to_string(ka.api_calls) + " off=" + std::to_string(ka.off_pump) + " B calls=" + std::to_string(kb.api_calls) + " off=" +
              std::to_string(kb.off_pump));
    check("relay access initialised once per start", ka.relay_init == 1 && kb.relay_init == 2);
    check("no failure callbacks in the happy path", fail_a == 0 && fail_b == 0);

    {
        B.init(&w.be, B_ID, 0, 1, 1);
        st::Link F;
        st::Config c = cb;
        c.cmdline.clear();
        c.wait_s = 1;
        c.net_port = free_port();
        std::atomic<int> nf{0};
        bool s = F.start(c, lf.fn("F1"), [&] { nf++; });
        bool f = wait_for([&] { return nf == 1; }, 4);
        check("Steam user never ready (hsu=0) -> failed after netsteamwait, on_fail once", s && f && F.state() == st::State::failed && lf.has("Steam user not ready within 1 s") &&
                                                                                                 lf.has("Steam co-op is off this session"));
        F.stop();
        check("failed link stop restores slot", bslot == breal);
    }
    {
        B.init(&w.be, B_ID, 1, 1, 1);
        pb.hold = true;
        st::Link F;
        st::Config c = cb;
        c.cmdline.clear();
        c.wait_s = 1;
        c.unhook_wait_s = 0.3;
        c.net_port = free_port();
        std::atomic<int> nf{0};
        F.start(c, lf.fn("F2"), [&] { nf++; });
        bool f = wait_for([&] { return nf == 1; }, 4);
        check("game never pumps -> failed with the no-pump reason", f && lf.has("did not pump Steam callbacks within 1 s"));
        F.stop();
        check("no-pump stop: slot restored, no inert-callback warning (nothing registered)", bslot == breal && !lf.has("kept registered"));
        pb.hold = false;
    }
    {
        st::Link F;
        st::Config c = cb;
        c.net_port = free_port();
        c.module = GetModuleHandleW(L"kernel32.dll");
        check("module without the Steam exports -> start fails, names the missing ones", !F.start(c, lf.fn("F3"), {}) && lf.has("lacks SteamAPI_GetHSteamUser"));
        c.module = B.mod;
        c.net_port = 0;
        check("bad relay port -> start fails", !F.start(c, lf.fn("F4"), {}) && lf.has("bad relay port 0"));
    }
    {
        pb.hold = true;
        B.init(&w.be, B_ID, 1, 1, 1);
        st::Link F;
        st::Config c = cb;
        c.cmdline.clear();
        c.unhook_wait_s = 0.3;
        c.net_port = free_port();
        pb.hold = false;
        F.start(c, lf.fn("F5"), {});
        wait_for([&] { return F.state() == st::State::ready; }, 3);
        pb.hold = true;
        Sleep(50);
        F.stop();
        bool warned = lf.has("kept registered and inert");
        pb.hold = false;
        Sleep(50);
        auto k = B.counters();
        check("pump stops while stopping -> callbacks left registered but inert, no crash on later pumps", warned && k.registered_cbs == 2 && bslot == breal,
              std::to_string(k.registered_cbs));
        uint64_t r0 = pb.n.load();
        B.deliver(S_ID, "q", 1, 0);
        wait_for([&] { return pb.n.load() > r0 + 20; }, 2);
        check("inert callbacks dispatched safely", true);
    }
    pa.stop();
    pb.stop();
    std::printf("STEAM TEST %s: %d passed, %d failed\n", g_fail ? "FAIL" : "OK", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
