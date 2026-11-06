#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "s2r_steam.h"

#include "s2r_net.h"
#include "s2r_x0.h"
#include "s2x.h"

#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

namespace s2r
{
    namespace
    {
        std::mutex g_smu;
        steam::Link* g_link = nullptr;
        bool g_failed = false;
        bool g_hooked = false;
        bool g_invited = false;
        bool g_badnet = false;
        SteamNoteFn g_menu;

        void say(const std::string& s)
        {
            x0::post([s] { x0::log(s); });
        }

        bool wanted()
        {
            if (!x0::fon("cpp_net")) return false;
            std::string v = x0::fs("net", "steam");
            steam::Net n = steam::Net::steam;
            if (!steam::parse_net(v, n))
            {
                if (!g_badnet) x0::logf("S2NET net=%s is not steam or ip: using steam", v.c_str());
                g_badnet = true;
                n = steam::Net::steam;
            }
            return n == steam::Net::steam;
        }

        steam::HostOpts host_opts()
        {
            Config c = x0_config();
            steam::HostOpts o;
            o.members = c.maxclients + 1;
            o.multi = c.maxclients > 1;
            o.open = x0::fi("steamdev", 0) == 1;
            o.tag = x0::fs("steamtag", "nola-dev");
            o.relay_port = c.listen;
            return o;
        }

        void note(SteamNote n)
        {
            if (g_menu) g_menu(n);
            else x0::logf("S2NET steam: note %d for %s (no menu handler)", (int)n.kind, n.name.c_str());
        }

        steam::Link* link_ready()
        {
            std::lock_guard<std::mutex> g(g_smu);
            return g_link && g_link->ready() ? g_link : nullptr;
        }

        void handle(const steam::Event& e)
        {
            using K = steam::Event::Kind;
            SteamNote n;
            n.lobby = e.lobby;
            n.id = e.id;
            n.name = e.name;
            n.n = e.n;
            n.max = e.max;
            n.code = e.code;
            n.why = e.why;
            switch (e.kind)
            {
            case K::ready: return;
            case K::hosting:
            {
                n.kind = SteamNote::Kind::hosting;
                note(n);
                if (x0::fi("steaminvite", 1) == 1 && !g_invited)
                {
                    g_invited = true;
                    if (steam::Link* l = link_ready()) l->invite();
                }
                return;
            }
            case K::closed: n.kind = SteamNote::Kind::closed; note(n); return;
            case K::member_in:
            case K::member_out:
            {
                n.kind = e.kind == K::member_in ? SteamNote::Kind::member_in : SteamNote::Kind::member_out;
                note(n);
                if (x0_session_mode() == 1 && x0_config().maxclients <= 1)
                {
                    x0::log("S2NET route: the one co-op slot follows the Steam lobby member: relay restarted");
                    x0_start();
                }
                return;
            }
            case K::joined:
            {
                n.kind = SteamNote::Kind::joined;
                int lp = x0_config().listen;
                steam::Link* l = link_ready();
                if (!l)
                {
                    x0::log("S2NET steam: joined a lobby but the link stopped");
                    return;
                }
                l->set_relay_port(lp);
                n.port = l->port();
                x0::logf("S2NET steam: Steam join ready: host %s (%llu), lobby %llu, %d/%d players, relay 127.0.0.1:%d <-> 127.0.0.1:%d", n.name.c_str(), (unsigned long long)n.id,
                         (unsigned long long)n.lobby, n.n, n.max, n.port, lp);
                note(n);
                return;
            }
            case K::join_fail:
            {
                bool soft = e.n == 1 && (e.code == 0 || e.code == steam::ENTER_DOESNT_EXIST || e.code == steam::ENTER_NOT_NOLA || e.code == steam::ENTER_NO_HOST);
                n.kind = soft ? SteamNote::Kind::none : SteamNote::Kind::join_fail;
                note(n);
                return;
            }
            case K::found:
                if (e.n == 0 && e.code == 0)
                {
                    n.kind = SteamNote::Kind::none;
                    note(n);
                }
                return;
            case K::host_left: n.kind = SteamNote::Kind::host_left; note(n); return;
            }
        }

        void on_event(const steam::Event& e)
        {
            x0::post([e] { handle(e); });
        }

        void on_fail()
        {
            x0::post([] {
                {
                    std::lock_guard<std::mutex> g(g_smu);
                    g_failed = true;
                }
                SteamNote n;
                n.kind = SteamNote::Kind::unavailable;
                note(n);
            });
        }

        void restart()
        {
            {
                std::lock_guard<std::mutex> g(g_smu);
                if (g_link && g_link->running()) g_link->stop();
                g_failed = false;
            }
            if (!wanted())
            {
                x0::log("S2NET steam: off (net=ip or cpp_net=0): LAN co-op only");
                return;
            }
            Config c = x0_config();
            steam::Config sc;
            sc.role = x0::role();
            sc.net_port = c.listen;
            double w = x0::fd("netsteamwait", 60);
            sc.wait_s = w > 0 ? w : 60;
            const wchar_t* cl = GetCommandLineW();
            sc.cmdline = cl ? cl : L"";
            bool host = x0_session_mode() == 1;
            steam::HostOpts ho;
            if (host) ho = host_opts();
            std::lock_guard<std::mutex> g(g_smu);
            if (!g_link) g_link = new steam::Link();
            if (!g_link->start(sc, say, on_fail, on_event))
            {
                g_failed = true;
                x0::log("S2NET steam: not available this session: LAN co-op only");
                return;
            }
            if (host) g_link->host_open(ho);
            x0::log("S2NET steam: Steam co-op service on (lobbies, invites, Join Game; LAN co-op stays on too)");
        }
    }

    void steam_menu(SteamNoteFn fn) { g_menu = std::move(fn); }

    bool steam_find()
    {
        if (x0::fi("steamjoin", 1) != 1) return false;
        steam::Link* l = link_ready();
        if (!l) return false;
        steam::FindOpts o;
        o.friends = true;
        o.search = x0::fi("steamdev", 0) == 1;
        o.tag = x0::fs("steamtag", "nola-dev");
        l->find(o);
        return true;
    }

    void steam_session(int mode, const std::string& peer, int port)
    {
        steam::Link* l = nullptr;
        {
            std::lock_guard<std::mutex> g(g_smu);
            if (g_link && g_link->running()) l = g_link;
        }
        if (!l) return;
        if (mode == 1)
        {
            g_invited = false;
            l->host_open(host_opts());
            return;
        }
        if (mode == 0)
        {
            l->host_close();
            l->leave();
            return;
        }
        if (peer == "127.0.0.1" && port == l->port()) l->set_relay_port(x0_config().listen);
        else l->leave();
    }

    bool steam_host_peer(std::string& host, int& port)
    {
        std::lock_guard<std::mutex> g(g_smu);
        if (!g_link || !g_link->running() || !g_link->hosting() || g_link->multi() || !g_link->peer()) return false;
        host = "127.0.0.1";
        port = g_link->port();
        return true;
    }

    void steam_stop()
    {
        std::lock_guard<std::mutex> g(g_smu);
        if (g_link && g_link->running()) g_link->stop();
    }

    int steam_stop_exit()
    {
        std::lock_guard<std::mutex> g(g_smu);
        if (!g_link || !g_link->running()) return 0;
        return g_link->stop_bounded() ? 1 : 2;
    }

    void steam_invite()
    {
        std::lock_guard<std::mutex> g(g_smu);
        if (!g_link || !g_link->running())
        {
            x0::log("S2NET steam: invite: Steam co-op is off (net=ip or Steam not available)");
            return;
        }
        g_link->invite();
    }

    std::string steam_status()
    {
        std::lock_guard<std::mutex> g(g_smu);
        if (!g_link || !g_link->running()) return g_failed ? "steam failed (LAN only)" : "steam off";
        return g_link->status();
    }

    namespace
    {
        std::string cmd(const std::vector<std::string>& a)
        {
            std::string sub = a.size() >= 2 ? a[1] : "status";
            if (sub == "invite")
            {
                steam_invite();
                return "invite queued";
            }
            if (sub == "find") return steam_find() ? "find queued" : "steam not ready (or steamjoin=0)";
            if (sub == "restart")
            {
                restart();
                return steam_status();
            }
            std::lock_guard<std::mutex> g(g_smu);
            if (!g_link || !g_link->running()) return "steam off";
            if (sub == "join" && a.size() >= 3)
            {
                g_link->join(std::strtoull(a[2].c_str(), nullptr, 10));
                return "join queued";
            }
            if (sub == "leave")
            {
                g_link->leave();
                return "leave queued";
            }
            return g_link->status();
        }

        void reg_init()
        {
            if (g_hooked) return;
            g_hooked = true;
            for (const char* k : {"net", "netsteamwait", "cpp_net", "netlisten", "steamdev", "steamtag"})
                x0::on_flag(x0::Mod::x0, k, [](const std::string&, const x0::Val&, const x0::Val&) { restart(); });
            x0::on_flag(x0::Mod::x0, "maxclients", [](const std::string&, const x0::Val&, const x0::Val&) {
                if (x0_session_mode() != 1) return;
                steam::HostOpts o = host_opts();
                std::lock_guard<std::mutex> g(g_smu);
                if (g_link && g_link->running() && g_link->hosting()) g_link->host_limit(o.members, o.multi);
            });
            x0::key(x0::Mod::x0, VK_HOME, [] {
                // HOME is reserved for the hold-to-rescue action while downed.
                const auto& downed = x0::iface().local_downed;
                if (downed && downed()) return;
                steam_invite();
            });
            x0::cmd(x0::Mod::x0, "steam", &cmd);
            x0::status(x0::Mod::x0, [] { return steam_status(); });
            restart();
        }
        ::x0::ModReg g_reg(::x0::Mod::x0, "steam", &reg_init);
    }
}
