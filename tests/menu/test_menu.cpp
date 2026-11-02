#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "s2m_menu_logic.h"
#include "s2m_steam_logic.h"
#include "s2m_text.h"
#include "s2r_net.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using namespace s2r;

static int g_fail = 0;
static int g_n = 0;

static void check(bool ok, const char* what)
{
    ++g_n;
    if (!ok) ++g_fail;
    std::printf("%s %s\n", ok ? "OK  " : "FAIL", what);
}

static disc::Host H(const char* ip, int port, const char* name, int n, int max, const char* id = "")
{
    disc::Host h;
    h.ip = ip;
    h.port = port;
    h.name = name;
    h.n = n;
    h.max = max;
    h.id = id;
    return h;
}

static void test_plan()
{
    std::vector<nola::Entry> mm = {{"LoadLastSaveAction", "sid_cont"}, {"GotoViewAction", "sid_new"}, {"GotoViewAction", "sid_load"}, {"QuitAction", "sid_exit"}};
    nola::Plan p = nola::plan(mm, 2);
    check(p.at == 1 && p.tmpl == 1, "plan: entry goes right after Continue, styled like the entry it pushes down");
    p = nola::plan(mm, 1);
    check(p.at == 1, "plan: host entry at the same place");
    std::vector<nola::Entry> nocont = {{"GotoViewAction", "sid_new"}, {"QuitAction", "sid_exit"}};
    p = nola::plan(nocont, 2);
    check(p.at == 0 && p.tmpl == 0, "plan: no Continue (no save yet): entry first");
    std::vector<nola::Entry> last = {{"GotoViewAction", "a"}, {"LoadLastSaveAction", "b"}};
    p = nola::plan(last, 2);
    check(p.at == 2 && p.tmpl == 1, "plan: Continue is last: entry appended, template is the last entry");
    check(nola::plan(mm, 0).at < 0, "plan: role neither host nor client: no entry");
    check(nola::plan({}, 2).at < 0, "plan: empty list: no entry");
}

static void test_both()
{
    std::vector<int> b = nola::shown(true, false, true);
    check(b.size() == 2 && b[0] == 1 && b[1] == 2, "both: menurole=1 shows Host then Join on a client boot");
    b = nola::shown(true, true, false);
    check(b.size() == 2 && b[0] == 1 && b[1] == 2, "both: menurole=1 shows Host then Join on a host boot");
    b = nola::shown(true, false, false);
    check(b.size() == 2, "both: menurole=1 shows both with no role file");
    check(nola::shown(false, true, false) == std::vector<int>{1}, "both: menurole=0 host boot: Host only (old)");
    check(nola::shown(false, false, true) == std::vector<int>{2}, "both: menurole=0 client boot: Join only (old)");
    check(nola::shown(false, false, false).empty(), "both: menurole=0 no role: no entry (old)");
    std::vector<nola::Entry> mm = {{"LoadLastSaveAction", "sid_cont"}, {"GotoViewAction", "sid_new"}, {"QuitAction", "sid_exit"}};
    nola::Plan p = nola::plan(mm, nola::shown(true, false, false)[0]);
    check(p.at == 1 && p.tmpl == 1, "both: block starts right after Continue, New Game style");
    nola::Step s = nola::click(nola::Mode::none, 1);
    check(s.arm_host && !s.end_first && !s.disarm && !s.join, "click: Host from solo arms host");
    s = nola::click(nola::Mode::host, 1);
    check(s.disarm && !s.arm_host, "click: Host again disarms");
    s = nola::click(nola::Mode::join, 1);
    check(s.end_first && s.arm_host, "click: Host while joining ends the join first");
    s = nola::click(nola::Mode::none, 2);
    check(s.join && !s.end_first, "click: Join from solo starts the search");
    s = nola::click(nola::Mode::join, 2);
    check(s.disarm && !s.join, "click: Join again disarms");
    s = nola::click(nola::Mode::host, 2);
    check(s.end_first && s.join, "click: Join while hosting ends the host mode first");
    check(std::string(nola::role_of(nola::Mode::host)) == "host" && std::string(nola::role_of(nola::Mode::join)) == "client" && std::string(nola::role_of(nola::Mode::none)).empty(),
          "role: host -> host, join -> client, none -> boot role");
}

static std::string E(nola_text::T id) { return nola_text::tx(id, nola_text::Lang::en); }
static std::string P(nola_text::T id) { return nola_text::tx(id, nola_text::Lang::pt); }

static void test_steam_texts()
{
    using K = s2r::SteamNote::Kind;
    using nola_text::Lang;
    using nola_text::T;
    auto note = [](K k, const char* name, int n, int max) {
        s2r::SteamNote s;
        s.kind = k;
        s.name = name;
        s.n = n;
        s.max = max;
        s.lobby = 77;
        return s;
    };
    check(nola_steam::text(note(K::hosting, "", 1, 4), Lang::en, false) == "Your call for help is on the air on Steam (1/4): press Home to invite a friend!", "steam: zone open");
    check(nola_steam::text(note(K::hosting, "", 1, 4), Lang::en, true) == "Your call for help is on the air on Steam (1/4): press Home to invite a friend! Lobby 77", "steam: zone open with lobby id");
    check(nola_steam::text(note(K::member_in, "Ana", 2, 4), Lang::en, false) == "Stalker Ana entered the Zone!", "steam: stalker entered");
    check(nola_steam::text(note(K::member_out, "Ana", 1, 4), Lang::en, false) == "Stalker Ana left the Zone! (1/4 stalkers)", "steam: stalker left");
    check(nola_steam::text(note(K::closed, "", 0, 0), Lang::en, false) == "Your radio is off on Steam!", "steam: zone closed");
    check(nola_steam::text(note(K::host_left, "Ana", 0, 0), Lang::en, false) == "Stalker Ana stopped calling!", "steam: host closed the zone");
    check(nola_steam::text(note(K::unavailable, "", 0, 0), Lang::en, false) == "Steam is silent right now. LAN co-op still works!", "steam: not available");
    check(nola_steam::fill(E(T::steam_quit_to_join), note(K::joined, "Ana", 0, 0)) == "To answer stalker Ana's call, leave your Zone first!", "steam: leave your zone");
    check(E(T::steam_searching) == "Scanning the radio for a call for help on Steam\xe2\x80\xa6", "steam: searching");
    check(nola_steam::text(note(K::join_fail, "Ana", 0, 0), Lang::en, false).find("Couldn't reach that call on Steam") == 0, "steam: popup text unchanged");
    s2r::SteamNote nn = note(K::join_fail, "Ana", 0, 0);
    nn.code = s2r::steam::ENTER_NOT_NOLA;
    check(nola_steam::text(nn, Lang::pt, false) == "N\xc3\xa3o deu para alcan\xc3\xa7" "ar esse chamado pela Steam: n\xc3\xa3o \xc3\xa9 um lobby de co-op do NOLA", "steam: not a NOLA lobby reason in pt");
    check(nola_steam::text(nn, Lang::es, false) == "No se pudo alcanzar ese llamado por Steam: no es un lobby de co-op de NOLA", "steam: not a NOLA lobby reason in es");
    nn.code = 9;
    check(nola_steam::text(nn, Lang::en, false) == "Couldn't reach that call on Steam: community ban", "steam: community ban reason");
    nn.code = 99;
    nn.why = "unknown Steam response";
    check(nola_steam::text(nn, Lang::en, false) == "Couldn't reach that call on Steam: unknown Steam response", "steam: unknown response reason");
}

static void test_texts()
{
    using nola_text::T;
    check(E(T::menu_host) == "Host co-op" && E(T::menu_join) == "Join co-op", "texts: English entry labels");
    check(P(T::menu_host) == "Hospedar co-op" && P(T::menu_join) == "Entrar no co-op", "texts: pt-BR entry labels");
    check(E(T::menu_host).find("NOLA") == std::string::npos && P(T::menu_join_on).find("NOLA") == std::string::npos && E(T::popup_title_join).find("NOLA") == std::string::npos, "texts: no NOLA in entry labels or popup titles");
    check(E(T::menu_title) == "NOLA \xe2\x80\x94 No One Left Alone" && P(T::menu_title) == E(T::menu_title), "texts: menu title is the mod name in both languages");
    check(nola::fmt(E(T::popup_full), "PC1", 2, 2) == "PC1's channel is full (2/2 stalkers). No room for one more.", "fmt: host full text");
    check(nola::fmt("{name}{name}", "ab", 0, 0) == "abab", "fmt: repeated key");
    check(nola::fmt(E(T::say_full), "h", 1, 2) == "The channel is full! (1/2 stalkers)", "say: zone full");
    check(nola::fmt(E(T::say_fail), "", 0, 0, "relay not started") == "The signal died\xe2\x80\xa6 relay not started", "say: connection lost with the reason");
    check(nola::fmt(E(T::say_joining), "PC1", 1, 2) == "Answering PC1's call on the radio (1/2 stalkers)\xe2\x80\xa6", "say: contacting");
    check(E(T::say_searching) == "Scanning the radio for a call for help on LAN\xe2\x80\xa6" && E(T::say_no_answer) == "Only static so far, still calling\xe2\x80\xa6", "say: searching and no answer");
    check(E(T::say_host_open) == "Calling for help on the radio, waiting for stalkers\xe2\x80\xa6", "say: zone open");
    check(E(T::popup_full) == "{name}'s channel is full ({n}/{max} stalkers). No room for one more." && E(T::popup_fail) == "The Zone jammed the signal. Co-op could not start.", "popup texts");
    check(nola::fmt("{name} {n}", "{max}", 3, 0) == "{max} 3", "fmt: a host name with a key inside is not expanded again");
    check(P(T::popup_fail) == "A Zona embaralhou o sinal. O co-op n\xc3\xa3o p\xc3\xb4" "de come\xc3\xa7" "ar.", "texts: pt-BR accents not eaten by hex escapes");
}

static void test_wire()
{
    std::string pr = disc::probe(123456789);
    int64_t nonce = 0;
    check(disc::is_probe(pr, nonce) && nonce == 123456789, "probe: round trip with nonce");
    disc::Host h;
    check(!disc::parse_reply(pr, h, nonce), "probe is not a reply");
    disc::Info in;
    in.name = "PC-HOST";
    in.id = "1730000000-42";
    in.n = 2;
    in.max = 5;
    in.port = 7799;
    in.hub = true;
    std::string rp = disc::reply(in, 77);
    check(!disc::is_probe(rp, nonce), "reply is not a probe");
    nonce = 0;
    check(disc::parse_reply(rp, h, nonce) && nonce == 77 && h.name == "PC-HOST" && h.id == "1730000000-42" && h.n == 2 && h.max == 5 && h.port == 7799 && h.hub,
          "reply: round trip of name, id, players, port, hub");
    check(!h.full(), "reply: 2/5 is not full");
    in.n = 5;
    disc::parse_reply(disc::reply(in, 1), h, nonce);
    check(h.full(), "reply: 5/5 is full");
    in.n = 1;
    in.port = 0;
    check(!disc::parse_reply(disc::reply(in, 1), h, nonce), "reply: port 0 rejected");
    in.port = 70000;
    check(!disc::parse_reply(disc::reply(in, 1), h, nonce), "reply: port 70000 rejected");
    in.port = 7799;
    in.max = 0;
    check(!disc::parse_reply(disc::reply(in, 1), h, nonce), "reply: max 0 rejected");
    in.max = 17;
    check(!disc::parse_reply(disc::reply(in, 1), h, nonce), "reply: max 17 rejected");
    in.max = 2;
    in.name = std::string(200, 'x') + "\n";
    check(disc::parse_reply(disc::reply(in, 1), h, nonce) && h.name.size() == 64, "reply: long name cut to 64");
    in.name = "a\x01" "b";
    check(disc::parse_reply(disc::reply(in, 1), h, nonce) && h.name == "a b", "reply: control chars replaced");
    in.name = "";
    check(disc::parse_reply(disc::reply(in, 1), h, nonce) && h.name == "host", "reply: empty name becomes host");
    std::string bad = rp;
    bad[2] = (char)((uint8_t)bad[2] + 1);
    check(!disc::parse_reply(bad, h, nonce), "reply: other protocol version rejected");
    check(!disc::parse_reply(rp.substr(0, rp.size() / 2), h, nonce), "reply: truncated datagram rejected");
    check(!disc::parse_reply("", h, nonce) && !disc::is_probe("S2", nonce), "empty and short datagrams rejected");
}

static void test_pick()
{
    std::vector<disc::Host> v;
    check(disc::add_host(v, H("10.0.0.2", 7779, "B", 1, 2, "s1")), "add: first host");
    check(!disc::add_host(v, H("10.0.0.9", 7779, "B", 1, 2, "s1")), "add: same id on another interface deduped");
    check(!disc::add_host(v, H("10.0.0.2", 7779, "B2", 1, 2, "")), "add: same ip:port deduped");
    check(disc::add_host(v, H("10.0.0.3", 7779, "A", 2, 2, "s2")), "add: second host");
    check(disc::add_host(v, H("10.0.0.4", 7779, "C", 1, 4, "s3")), "add: third host");
    check(disc::pick(v, {}) == 0, "pick: full host A skipped, B before C by name");
    check(disc::pick(v, {"10.0.0.4:7779"}) == 2, "pick: last host first");
    check(disc::pick(v, {"10.0.0.3:7779", "10.0.0.4:7779"}) == 2, "pick: last host full, next preference");
    check(disc::pick(v, {"1.2.3.4:1"}) == 0, "pick: unknown preference falls back");
    std::vector<disc::Host> full = {H("10.0.0.3", 7779, "A", 2, 2), H("10.0.0.5", 7779, "D", 3, 3)};
    check(disc::pick(full, {}) == -1, "pick: all full");
    nola::Pick pk = nola::choose(full, {});
    check(pk.why == nola::Why::full, "choose: all full -> Host full");
    pk = nola::choose({}, {});
    check(pk.why == nola::Why::none, "choose: nothing -> No host found");
    pk = nola::choose(v, {});
    check(pk.why == nola::Why::ok && pk.i == 0, "choose: ok");
}

static void test_addr()
{
    std::string ip;
    int port = 0;
    check(disc::parse_addr(" 10.0.50.105:7779\r\n", ip, port) && ip == "10.0.50.105" && port == 7779, "addr: ip:port with spaces and newline");
    check(disc::parse_addr("myhost:1", ip, port) && ip == "myhost" && port == 1, "addr: name:port");
    check(!disc::parse_addr("10.0.0.1", ip, port), "addr: no port rejected");
    check(!disc::parse_addr(":7779", ip, port), "addr: no ip rejected");
    check(!disc::parse_addr("a:0", ip, port) && !disc::parse_addr("a:65536", ip, port) && !disc::parse_addr("a:7x", ip, port), "addr: bad ports rejected");
    auto t = disc::targets({7799}, {{"10.0.0.2", 7799}, {"127.0.0.1", 7799}, {"", 7799}, {"x", 0}}, true);
    check(t.size() == 3 && t[0].ip == "10.0.0.2" && t[1].ip == "127.0.0.1" && t[2].ip == "*", "targets: extras first, dedup, broadcast, bad dropped");
    t = disc::targets({7799}, {}, false);
    check(t.size() == 1 && t[0].ip == "127.0.0.1", "targets: no broadcast when off");
}

struct Fake
{
    SOCKET s = INVALID_SOCKET;
    int port = 0;
    std::atomic<bool> stop{false};
    std::atomic<int> probes{0};
    int64_t nonce_add = 0;
    bool answer = true;
    disc::Info info;
    std::thread th;
    bool start()
    {
        s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) return false;
        int l = sizeof a;
        getsockname(s, reinterpret_cast<sockaddr*>(&a), &l);
        port = ntohs(a.sin_port);
        info.port = port;
        th = std::thread([this] {
            char b[2048];
            while (!stop)
            {
                fd_set rs;
                FD_ZERO(&rs);
                FD_SET(s, &rs);
                timeval tv{0, 50000};
                if (select(0, &rs, nullptr, nullptr, &tv) <= 0) continue;
                sockaddr_in f{};
                int fl = sizeof f;
                int n = recvfrom(s, b, sizeof b, 0, reinterpret_cast<sockaddr*>(&f), &fl);
                int64_t nonce = 0;
                if (n <= 0 || !disc::is_probe(std::string_view(b, (size_t)n), nonce)) continue;
                ++probes;
                if (!answer) continue;
                std::string r = disc::reply(info, nonce + nonce_add);
                sendto(s, r.data(), (int)r.size(), 0, reinterpret_cast<sockaddr*>(&f), fl);
            }
        });
        return true;
    }
    ~Fake()
    {
        stop = true;
        if (th.joinable()) th.join();
        if (s != INVALID_SOCKET) closesocket(s);
    }
};

static void test_run()
{
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    Fake a, b, c;
    a.info.name = "FAKE-A";
    a.info.id = "fa";
    a.info.n = 1;
    a.info.max = 2;
    b.info.name = "FAKE-B";
    b.info.id = "fb";
    b.nonce_add = 1;
    c.answer = false;
    check(a.start() && b.start() && c.start(), "run: three loopback fakes bound");
    std::vector<disc::Target> t = {{"127.0.0.1", a.port}, {"127.0.0.1", b.port}, {"127.0.0.1", c.port}};
    std::string err;
    auto hs = disc::run(t, 900, &err);
    check(err.empty(), "run: no socket error");
    check(hs.size() == 1 && hs[0].name == "FAKE-A" && hs[0].ip == "127.0.0.1" && hs[0].port == a.port && hs[0].n == 1 && hs[0].max == 2,
          "run: the answering fake found with its ip, port and players");
    check(b.probes >= 2 && c.probes >= 2, "run: probes resent while waiting");
    check(hs.size() == 1, "run: wrong-nonce reply ignored, silent fake absent");
    check(!hs.empty() && hs[0].ms >= 0 && hs[0].ms < 900, "run: reply time measured");
    auto none = disc::run({{"127.0.0.1", c.port}}, 300, &err);
    check(none.empty(), "run: nobody answers -> empty list");
    auto bc = disc::run(disc::targets({a.port}, {}, true), 500, &err);
    check(bc.size() == 1 && bc[0].name == "FAKE-A", "run: default targets (broadcast + loopback) reach the loopback host once");
    WSACleanup();
}

static void test_peer_pick()
{
    check(nola::peer_pick(1, 0, 100, 0, -1, 10) == 0, "peer_pick: one address never moves");
    check(nola::peer_pick(3, 0, 5, 0, -1, 10) == 0, "peer_pick: waits before moving on");
    check(nola::peer_pick(3, 0, 11, 0, -1, 10) == 1, "peer_pick: no answer in time moves to the next");
    check(nola::peer_pick(3, 2, 11, 0, -1, 10) == 0, "peer_pick: wraps to the first");
    check(nola::peer_pick(3, 1, 50, 20, 45, 10) == 1, "peer_pick: a recent answer keeps the address");
    check(nola::peer_pick(3, 1, 50, 20, 25, 10) == 2, "peer_pick: an answer that stopped moves on");
    check(nola::peer_pick(3, 1, 50, 20, 15, 10) == 2, "peer_pick: an answer from before this start does not count");
}
static void test_peers()
{
    check(nola::peers("10.0.1.20") == std::vector<std::string>{"10.0.1.20"}, "peers: one address");
    check(nola::peers("10.0.1.20,26.1.2.3; 192.168.0.5  host-b") == std::vector<std::string>{"10.0.1.20", "26.1.2.3", "192.168.0.5", "host-b"}, "peers: comma, semicolon and space lists");
    check(nola::peers(" ,10.0.1.20,,10.0.1.20 ") == std::vector<std::string>{"10.0.1.20"}, "peers: empty items and repeats dropped");
    check(nola::peers("").empty() && nola::peers(" , ").empty(), "peers: empty list");
}

int main()
{
    test_peers();
    test_peer_pick();
    test_plan();
    test_both();
    test_texts();
    test_steam_texts();
    test_wire();
    test_pick();
    test_addr();
    test_run();
    std::printf("%d/%d %s\n", g_n - g_fail, g_n, g_fail ? "FAIL" : "PASS");
    return g_fail ? 1 : 0;
}
