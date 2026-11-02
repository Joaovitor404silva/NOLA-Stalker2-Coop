#include "s2r_x0.h"

#include "s2r_net.h"
#include "s2m_menu_logic.h"
#include "s2m_text.h"
#include "s2r_steam.h"
#include "s2x.h"
#include "s2x_fr.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <polyhook2/Detour/x64Detour.hpp>
#include "s2x_hot.h"

namespace s2r
{
    namespace
    {
        std::mutex g_mu;
        Net* g_net = nullptr;
        uint64_t g_seq = 0;
        uint64_t g_vseq[proto::MAXV] = {};
        bool g_hooked = false;
        int g_mode = 0;
        std::string g_speer;
        int g_sport = 0;
        bool g_dormantLogged = false;
        size_t g_pidx = 0;
        double g_pstart = 0;
        bool g_ptick = false;
        long long g_pticks = 0;
        const char* g_pwhy = "-";

        bool menu_on() { return x0::fi("joinmenu", 1) == 1; }

        std::string host_name()
        {
            wchar_t b[256] = {};
            DWORD n = 255;
            if (!GetComputerNameExW(ComputerNameDnsHostname, b, &n) || n == 0) return "host";
            std::string o;
            for (DWORD i = 0; i < n; ++i) o += (b[i] > 0 && b[i] < 0x80) ? (char)b[i] : '?';
            return o;
        }

        Net& net()
        {
            if (!g_net) g_net = new Net();
            return *g_net;
        }

        const char* const kKeys[] = {"cpp_net", "netpeer", "netlisten", "netpeerport", "netdx", "netloss", "netrecord", "netfiles", "netloop", "maxclients", "netlocal", "joinmenu"};

        void say(const std::string& s)
        {
            x0::post([s] { x0::log(s); });
        }

        using PreExitFn = void (*)(void*);
        uint64_t g_peTramp = 0;
        PLH::x64Detour* g_peDet = nullptr;

        bool mem_eq_seh(const uint8_t* p, const uint8_t* b, size_t n)
        {
            __try { return std::memcmp(p, b, n) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void pe_detour(void* eng)
        {
            x0_pre_exit();
            reinterpret_cast<PreExitFn>(g_peTramp)(eng);
        }

        void pe_install()
        {
            static bool tried = false;
            if (tried) return;
            tried = true;
            static const uint8_t kPE[] = {0x56, 0x57, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x89, 0xCE, 0x80, 0x3D};
            auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
            uint8_t* at = base + 0x56cca96;
            if (!mem_eq_seh(at, kPE, sizeof kPE))
            {
                x0::log("NOLA peer: GameEngine::PreExit bytes differ (new exe?): no BYE on exit, the other side uses the peerleft timeout");
                return;
            }
            g_peDet = new x0::hot::Detour((uint64_t)at, (uint64_t)&pe_detour, &g_peTramp);
            bool ok = g_peDet->hook();
            x0::log(std::string("NOLA peer: GameEngine::PreExit detour (BYE on exit) ") + (ok ? "on" : "FAILED"));
            if (!ok) { delete g_peDet; g_peDet = nullptr; g_peTramp = 0; }
        }
    }

    void x0_pre_exit()
    {
        S2FR(x0::fr::session_end("quit"));
        if (x0::fi("peerbye", 1) == 1)
        {
            bool ok = x0_bye(300);
            x0::logf("NOLA peer: GameEngine::PreExit -> BYE to the other game ok=%s", ok ? "true" : "false");
        }
        if (x0::fi("steamexitstop", 1) != 1) return;
        double t = clock_s();
        int r = steam_stop_exit();
        if (r == 1) x0::logf("NOLA peer: GameEngine::PreExit -> Steam link thread stopped and joined before the Steam modules unload (%.0f ms)", (clock_s() - t) * 1000);
        else if (r == 2) x0::logf("NOLA peer: GameEngine::PreExit -> Steam link thread did not stop in time (%.0f ms): left running", (clock_s() - t) * 1000);
    }

    Config x0_config() { return x0_config_role(x0::is_host()); }

    Config x0_config_role(bool host)
    {
        Config c;
        c.role = host ? "host" : "client";
        int64_t l = x0::fi("netlisten", 0);
        int64_t p = x0::fi("netpeerport", 0);
        c.listen = (int)(l > 0 ? l : (host ? 7779 : 7778));
        c.peer_port = (int)(p > 0 ? p : (host ? 7778 : 7779));
        auto ps = nola::peers(x0::fs("netpeer", "127.0.0.1"));
        c.peer_host = ps.empty() ? std::string() : ps[g_pidx % ps.size()];
        if (g_mode == 2 && !g_speer.empty() && g_sport > 0)
        {
            c.peer_host = g_speer;
            c.peer_port = g_sport;
        }
        c.disc_name = host_name();
        c.dir = x0::bridge_dir();
        c.dx = x0::fd("netdx", 0);
        c.loss = x0::fd("netloss", 0);
#ifndef S2X_RELEASE
        if (x0::fi("netrecord", 0) == 1) c.record = x0::bridge_file(L"relay_record.txt");
#endif
        int64_t lp = x0::fi("netloop", 20);
        c.loop_ms = (int)(lp >= 1 && lp <= 1000 ? lp : 20);
        c.files = x0::fi("netfiles", 1) == 1;
        int64_t mc = x0::fi("maxclients", 4);
        c.maxclients = (int)(mc < 1 ? 1 : mc > proto::MAXV ? proto::MAXV : mc);
        c.local_only = x0::fi("netlocal", 0) == 1;
        if (g_mode == 1 && c.maxclients <= 1)
        {
            std::string sh;
            int sp = 0;
            if (steam_host_peer(sh, sp))
            {
                c.peer_host = sh;
                c.peer_port = sp;
            }
        }
        c.slot_silent = x0::fd("peersilent", 5);
        c.slot_left = x0::fd("peerleft", 60);
        if (c.maxclients > 1)
        {
            std::string t;
            if (x0::data("mc_tags.tsv", t)) c.relay_tags = relay_tags_d1(t);
            else x0::log("S2NET relay hub: mc_tags.tsv missing from the DLL data: PEER datagrams carry no x1 tags");
        }
        return c;
    }

    bool x0_start()
    {
        std::lock_guard<std::mutex> g(g_mu);
        Net& n = net();
        n.stop();
        g_seq = 0;
        for (auto& q : g_vseq) q = 0;
        if (!x0::fon("cpp_net"))
        {
            x0::log("S2NET relay in the DLL off (cpp_net=0): the bridge files go to an external relay_v2.py");
            return false;
        }
        if (menu_on() && g_mode == 0)
        {
            if (!g_dormantLogged) x0::log("S2NET relay dormant (joinmenu=1): solo game until the main menu entry NOLA starts co-op");
            g_dormantLogged = true;
            return false;
        }
        g_dormantLogged = false;
        Config c = x0_config();
        if (c.peer_host.empty())
        {
            x0::log("S2NET relay in the DLL not started: netpeer is empty");
            return false;
        }
        bool ok = n.start(c, say);
        g_pstart = clock_s();
        x0::logf("S2NET relay in the DLL %s: role=%s listen=%d peer=%s:%d files=%d loop=%d ms", ok ? "started" : "NOT started (bad port)", c.role.c_str(), c.listen,
                 c.peer_host.c_str(), c.peer_port, (int)c.files, c.loop_ms);
        if (ok && n.hub())
            x0::logf("S2NET relay hub: this host takes up to %d clients (maxclients), %d relay tags; clients join on UDP %d, netpeer is not used", c.maxclients, (int)c.relay_tags.size(),
                     c.listen);
        return ok;
    }

    void x0_stop()
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (g_net) g_net->stop();
    }

    bool x0_active()
    {
        std::lock_guard<std::mutex> g(g_mu);
        return g_net && g_net->running();
    }

    bool x0_mailbox()
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (x0::fon("cpp_net") && menu_on() && g_mode == 0) return true;
        return g_net && g_net->running() && !g_net->config().files;
    }

    void x0_out(std::string text)
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (g_net && g_net->running()) g_net->put_out(std::move(text));
    }

    bool x0_in(std::string& text)
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_net || !g_net->running()) return false;
        return g_net->take_in(text, g_seq);
    }

    bool x0_bye(int wait_ms)
    {
        std::lock_guard<std::mutex> g(g_mu);
        return g_net && g_net->running() && g_net->bye(wait_ms);
    }

    bool x0_take_bye(uint64_t& seen, std::string& sid)
    {
        std::lock_guard<std::mutex> g(g_mu);
        return g_net && g_net->take_bye(seen, sid);
    }

    bool x0_in_view(int view, std::string& text)
    {
        if (view == 0) return x0_in(text);
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_net || !g_net->running() || view < 0 || view >= proto::MAXV) return false;
        return g_net->take_in(view, text, g_vseq[view]);
    }

    bool x0_take_bye_view(int view, uint64_t& seen, std::string& sid)
    {
        std::lock_guard<std::mutex> g(g_mu);
        return g_net && g_net->take_bye(view, seen, sid);
    }

    bool x0_roster(Roster& r)
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_net || !g_net->running()) return false;
        r = g_net->roster();
        return r.hub;
    }

    std::string x0_status()
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_net || !g_net->running()) return menu_on() && g_mode == 0 ? "net dormant (joinmenu: solo until the NOLA menu entry)" : "net off";
        Stats s = g_net->stats();
        const Config& c = g_net->config();
        double now = clock_s();
        char b[320];
        std::snprintf(b, sizeof b, "net on listen=%d peer=%s:%d bound=%d files=%d sent=%llu recv=%llu in=%llu stale=%llu apps=%llu npcs=%llu rx_age=%.1f", c.listen,
                      c.peer_host.c_str(), c.peer_port, (int)s.bound, (int)c.files, (unsigned long long)s.tot_sent, (unsigned long long)s.tot_recv,
                      (unsigned long long)s.tot_written, (unsigned long long)s.stale, (unsigned long long)s.apps, (unsigned long long)s.npcs,
                      s.last_rx < 0 ? -1.0 : now - s.last_rx);
        std::string o = std::string(b) + " bye_tx=" + std::to_string(s.byes_tx) + " bye_rx=" + std::to_string(s.byes_rx);
        if (g_net->hub())
            o += " hub clients=" + std::to_string(s.conns) + "/" + std::to_string(c.maxclients) + " peer_tx=" + std::to_string(s.peer_tx) + " roster_tx=" + std::to_string(s.roster_tx) +
                 " full_tx=" + std::to_string(s.full_tx) + " app_fwd=" + std::to_string(s.app_fwd);
        else if (s.roster_rx || s.full_rx)
            o += " views you=" + std::to_string(g_net->roster().you) + " peer_rx=" + std::to_string(s.peer_rx) + " roster_rx=" + std::to_string(s.roster_rx) + " full_rx=" + std::to_string(s.full_rx);
        if (s.disc_rx || s.disc_tx) o += " disc_rx=" + std::to_string(s.disc_rx) + " disc_tx=" + std::to_string(s.disc_tx);
        if (g_mode) o += std::string(" nola=") + (g_mode == 1 ? "host" : "join");
        o += " ptick=" + std::to_string(g_pticks) + "/" + g_pwhy + " pidx=" + std::to_string(g_pidx) + " pstart_age=" + std::to_string((int)(now - g_pstart));
        return o;
    }

    bool x0_session(int mode, const std::string& peer, int port, std::string& why)
    {
        if (mode < 0 || mode > 2) { why = nola_text::tx(nola_text::T::why_bad_mode); return false; }
        bool rt = x0::fi("menurole", 1) == 1;
        if (!rt && mode == 1 && !x0::is_host()) { why = nola_text::tx(nola_text::T::why_not_host); return false; }
        if (!rt && mode == 2 && !x0::is_client()) { why = nola_text::tx(nola_text::T::why_not_client); return false; }
        if (mode == 2 && (peer.empty() || port < 1 || port > 65535)) { why = nola_text::tx(nola_text::T::why_no_addr); return false; }
        int was;
        {
            std::lock_guard<std::mutex> g(g_mu);
            was = g_mode;
        }
        if (mode == 0 && was != 0)
        {
            bool ok = x0_bye(300);
            S2FR(x0::fr::session_end("coop_end"));
            x0::logf("S2NET NOLA: co-op %s ended: BYE ok=%s, relay stopped, solo game", was == 1 ? "host" : "join", ok ? "true" : "false");
        }
        {
            std::lock_guard<std::mutex> g(g_mu);
            g_mode = mode;
            g_speer = mode == 2 ? peer : std::string();
            g_sport = mode == 2 ? port : 0;
        }
        if (was != mode) x0::coop_session_reset();
        steam_session(mode, peer, port);
        if (mode == 0)
        {
            x0_stop();
            if (rt) x0::set_session_role("", "co-op session ended");
            if (!menu_on()) x0_start();
            return true;
        }
        x0::ui_session_begin();
        if (rt) x0::set_session_role(mode == 1 ? "host" : "client", mode == 1 ? "co-op host session" : "co-op join session");
        x0::logf("S2NET NOLA: co-op %s started%s", mode == 1 ? "host" : "join", mode == 2 ? (" to " + peer + ":" + std::to_string(port)).c_str() : "");
        bool ok = x0_start();
        if (!ok) why = nola_text::tx(nola_text::T::why_relay);
        return ok;
    }

    int x0_session_mode()
    {
        std::lock_guard<std::mutex> g(g_mu);
        return g_mode;
    }

    std::string x0_session_peer()
    {
        std::lock_guard<std::mutex> g(g_mu);
        return g_mode == 2 ? g_speer + ":" + std::to_string(g_sport) : std::string();
    }

    bool x0_dormant()
    {
        std::lock_guard<std::mutex> g(g_mu);
        return menu_on() && g_mode == 0;
    }

    void x0_world(const std::string& w)
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (g_net && g_net->running()) g_net->put_world(w);
    }

    bool x0_stats(Stats& out)
    {
        std::lock_guard<std::mutex> g(g_mu);
        if (!g_net || !g_net->running()) return false;
        out = g_net->stats();
        return true;
    }

    void peer_tick()
    {
        x0::after(x0::Mod::x0, 2, [] { peer_tick(); });
        auto ps = nola::peers(x0::fs("netpeer", "127.0.0.1"));
        ++g_pticks;
        g_pwhy = ps.size() < 2 ? "one" : x0::is_host() ? "host" : "chk";
        if (ps.size() < 2 || x0::is_host()) return;
        size_t cur, nxt;
        {
            std::lock_guard<std::mutex> g(g_mu);
            g_pwhy = g_mode == 2 ? "menu" : !g_net || !g_net->running() ? "off" : g_net->hub() ? "hub" : "wait";
            if (g_mode == 2 || !g_net || !g_net->running() || g_net->hub()) return;
            Stats s = g_net->stats();
            cur = g_pidx % ps.size();
            nxt = nola::peer_pick(ps.size(), cur, clock_s(), g_pstart, s.last_rx, x0::fd("netpeerwait", 20));
            if (nxt == cur) return;
            g_pidx = nxt;
            g_pwhy = "moved";
        }
        x0::logf("S2NET direct: no answer from %s in %.0f s, trying %s (%d/%d)", ps[cur].c_str(), x0::fd("netpeerwait", 20), ps[nxt].c_str(), (int)nxt + 1, (int)ps.size());
        x0_start();
    }

    void x0_init()
    {
        if (!g_hooked)
        {
            g_hooked = true;
            for (const char* k : kKeys)
                x0::on_flag(x0::Mod::x0, k, [](const std::string&, const x0::Val&, const x0::Val&) { x0_start(); });
            x0::status(x0::Mod::x0, [] { return x0_status(); });
            pe_install();
        }
        if (!g_ptick)
        {
            g_ptick = true;
            x0::after(x0::Mod::x0, 2, [] { peer_tick(); });
        }
        x0_start();
    }

    namespace
    {
        void reg_init() { x0_init(); }
        ::x0::ModReg g_reg(::x0::Mod::x0, "net", &reg_init);
    }
}
