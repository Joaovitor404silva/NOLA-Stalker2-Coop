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
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#include "s2r_steam.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace s2r::steam
{
    namespace
    {
        std::string fmt(const char* f, ...)
        {
            char buf[1024];
            va_list ap;
            va_start(ap, f);
            std::vsnprintf(buf, sizeof buf, f, ap);
            va_end(ap);
            return buf;
        }

        double now_s()
        {
            static LARGE_INTEGER fq = [] {
                LARGE_INTEGER f;
                QueryPerformanceFrequency(&f);
                return f;
            }();
            LARGE_INTEGER c;
            QueryPerformanceCounter(&c);
            return (double)c.QuadPart / (double)fq.QuadPart;
        }

        template <class T> bool get(HMODULE m, const char* name, T& out, std::string& missing)
        {
            out = reinterpret_cast<T>(GetProcAddress(m, name));
            if (out) return true;
            if (!missing.empty()) missing += ",";
            missing += name;
            return false;
        }

        struct Hook
        {
            std::atomic<void*> link{nullptr};
            std::atomic<void (*)(void*)> pre{nullptr};
            std::atomic<void (*)()> real{nullptr};
            std::atomic<int> inflight{0};
        };
        constexpr int kHooks = 8;
        Hook g_hooks[kHooks];

        template <int I> void tramp()
        {
            Hook& h = g_hooks[I];
            h.inflight.fetch_add(1);
            void* l = h.link.load();
            auto pre = h.pre.load();
            if (l && pre) pre(l);
            auto r = h.real.load();
            if (r) r();
            h.inflight.fetch_sub(1);
        }
        void (*const kTramps[kHooks])() = {&tramp<0>, &tramp<1>, &tramp<2>, &tramp<3>, &tramp<4>, &tramp<5>, &tramp<6>, &tramp<7>};

        bool write_slot(void** slot, void* v)
        {
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
            InterlockedExchangePointer(slot, v);
            DWORD tmp = 0;
            VirtualProtect(slot, sizeof(void*), old, &tmp);
            return true;
        }

        bool restore_slot(void** slot, void* expect, void* v)
        {
            DWORD old = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
            void* prev = InterlockedCompareExchangePointer(slot, v, expect);
            DWORD tmp = 0;
            VirtualProtect(slot, sizeof(void*), old, &tmp);
            return prev == expect;
        }

        const IMAGE_NT_HEADERS64* nt_of(const uint8_t* base)
        {
            auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
            auto nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) return nullptr;
            return nt;
        }

        void** walk_thunks(uint8_t* base, DWORD int_rva, DWORD iat_rva, const char* fn)
        {
            if (!int_rva || !iat_rva) return nullptr;
            auto names = reinterpret_cast<const IMAGE_THUNK_DATA64*>(base + int_rva);
            for (size_t i = 0; names[i].u1.AddressOfData; ++i)
            {
                if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
                auto ibn = reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names[i].u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), fn) == 0) return reinterpret_cast<void**>(base + iat_rva) + i;
            }
            return nullptr;
        }
    }

    bool Api::load(void* module, std::string& missing)
    {
        missing.clear();
        mod = module;
        HMODULE m = static_cast<HMODULE>(module);
        if (!m)
        {
            missing = "module";
            return false;
        }
        bool ok = true;
        ok &= get(m, "SteamAPI_GetHSteamUser", GetHSteamUser, missing);
        ok &= get(m, "SteamAPI_RunCallbacks", RunCallbacks, missing);
        ok &= get(m, "SteamAPI_RegisterCallback", RegisterCallback, missing);
        ok &= get(m, "SteamAPI_UnregisterCallback", UnregisterCallback, missing);
        ok &= get(m, "SteamAPI_RegisterCallResult", RegisterCallResult, missing);
        ok &= get(m, "SteamAPI_UnregisterCallResult", UnregisterCallResult, missing);
        ok &= get(m, "SteamAPI_SteamUser_v021", User, missing);
        ok &= get(m, "SteamAPI_ISteamUser_BLoggedOn", BLoggedOn, missing);
        ok &= get(m, "SteamAPI_ISteamUser_GetSteamID", GetSteamID, missing);
        ok &= get(m, "SteamAPI_SteamFriends_v017", Friends, missing);
        ok &= get(m, "SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog", InviteDialog, missing);
        ok &= get(m, "SteamAPI_ISteamFriends_GetPersonaName", PersonaName, missing);
        ok &= get(m, "SteamAPI_ISteamFriends_GetFriendPersonaName", FriendPersonaName, missing);
        ok &= get(m, "SteamAPI_ISteamFriends_GetFriendCount", FriendCount, missing);
        ok &= get(m, "SteamAPI_ISteamFriends_GetFriendByIndex", FriendByIndex, missing);
        ok &= get(m, "SteamAPI_ISteamFriends_GetFriendGamePlayed", FriendGamePlayed, missing);
        ok &= get(m, "SteamAPI_SteamMatchmaking_v009", Matchmaking, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit", SetMemberLimit, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyMemberLimit", GetMemberLimit, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_RequestLobbyList", RequestLobbyList, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter", ListStringFilter, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter", ListDistanceFilter, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyByIndex", LobbyByIndex, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_CreateLobby", CreateLobby, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_JoinLobby", JoinLobby, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_LeaveLobby", LeaveLobby, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers", NumMembers, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex", MemberByIndex, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_SetLobbyData", SetLobbyData, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyData", GetLobbyData, missing);
        ok &= get(m, "SteamAPI_ISteamMatchmaking_GetLobbyOwner", LobbyOwner, missing);
        ok &= get(m, "SteamAPI_SteamUtils_v010", Utils, missing);
        ok &= get(m, "SteamAPI_ISteamUtils_GetAppID", AppID, missing);
        ok &= get(m, "SteamAPI_ISteamUtils_IsOverlayEnabled", OverlayEnabled, missing);
        ok &= get(m, "SteamAPI_SteamNetworkingMessages_SteamAPI_v002", Messages, missing);
        ok &= get(m, "SteamAPI_ISteamNetworkingMessages_SendMessageToUser", SendToUser, missing);
        ok &= get(m, "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel", Receive, missing);
        ok &= get(m, "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser", Accept, missing);
        ok &= get(m, "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser", Close, missing);
        ok &= get(m, "SteamAPI_SteamNetworkingMessage_t_Release", Release, missing);
        ok &= get(m, "SteamAPI_SteamNetworkingIdentity_SetSteamID64", IdSet, missing);
        ok &= get(m, "SteamAPI_SteamNetworkingIdentity_GetSteamID64", IdGet, missing);
        ok &= get(m, "SteamAPI_SteamNetworkingIdentity_Clear", IdClear, missing);
        ok &= get(m, "SteamAPI_SteamNetworkingUtils_SteamAPI_v004", NetUtils, missing);
        ok &= get(m, "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess", InitRelay, missing);
        return ok;
    }

    bool parse_net(std::string_view v, Net& out)
    {
        if (v == "steam")
        {
            out = Net::steam;
            return true;
        }
        if (v == "ip")
        {
            out = Net::ip;
            return true;
        }
        return false;
    }

    bool parse_connect_lobby(std::wstring_view cmd, uint64_t& lobby)
    {
        static const wchar_t* keys[] = {L"+connect_lobby", L"-connect_lobby"};
        for (const wchar_t* k : keys)
        {
            size_t kl = std::wcslen(k);
            size_t pos = 0;
            while ((pos = cmd.find(k, pos)) != std::wstring_view::npos)
            {
                size_t i = pos + kl;
                bool sep = false;
                while (i < cmd.size() && (cmd[i] == L' ' || cmd[i] == L'\t' || cmd[i] == L'=' || cmd[i] == L'"'))
                {
                    sep = true;
                    ++i;
                }
                uint64_t v = 0;
                size_t digits = 0;
                while (i < cmd.size() && cmd[i] >= L'0' && cmd[i] <= L'9' && digits < 20)
                {
                    v = v * 10 + (uint64_t)(cmd[i] - L'0');
                    ++i;
                    ++digits;
                }
                bool end = i >= cmd.size() || cmd[i] == L' ' || cmd[i] == L'\t' || cmd[i] == L'"';
                if (sep && digits > 0 && end && v != 0)
                {
                    lobby = v;
                    return true;
                }
                pos += kl;
            }
        }
        return false;
    }

    void** find_import_slot(void* module, const char* dll, const char* fn)
    {
        auto base = static_cast<uint8_t*>(module);
        if (!base) return nullptr;
        auto nt = nt_of(base);
        if (!nt) return nullptr;
        const auto& dd = nt->OptionalHeader.DataDirectory;
        const auto& dl = dd[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
        if (dl.VirtualAddress && dl.Size)
        {
            auto d = reinterpret_cast<const IMAGE_DELAYLOAD_DESCRIPTOR*>(base + dl.VirtualAddress);
            for (; d->DllNameRVA; ++d)
            {
                if (!d->Attributes.RvaBased) continue;
                if (_stricmp(reinterpret_cast<const char*>(base + d->DllNameRVA), dll) != 0) continue;
                if (void** s = walk_thunks(base, d->ImportNameTableRVA, d->ImportAddressTableRVA, fn)) return s;
            }
        }
        const auto& im = dd[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (im.VirtualAddress && im.Size)
        {
            auto d = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + im.VirtualAddress);
            for (; d->Name; ++d)
            {
                if (_stricmp(reinterpret_cast<const char*>(base + d->Name), dll) != 0) continue;
                if (void** s = walk_thunks(base, d->OriginalFirstThunk, d->FirstThunk, fn)) return s;
            }
        }
        return nullptr;
    }

    const char* state_name(State s)
    {
        switch (s)
        {
        case State::off: return "off";
        case State::waiting: return "waiting_for_steam";
        case State::ready: return "ready";
        case State::creating: return "creating_lobby";
        case State::hosting: return "hosting";
        case State::finding: return "finding";
        case State::joining: return "joining";
        case State::joined: return "joined";
        case State::failed: return "failed";
        }
        return "?";
    }

    const char* enter_text(int code)
    {
        switch (code)
        {
        case ENTER_SUCCESS: return "ok";
        case ENTER_DOESNT_EXIST: return "the lobby no longer exists";
        case ENTER_NOT_ALLOWED: return "not allowed (friends-only lobby, or Steam privacy settings)";
        case ENTER_FULL: return "the lobby is full";
        case ENTER_ERROR: return "Steam error";
        case 6: return "banned from the lobby";
        case ENTER_LIMITED: return "this Steam account is limited and cannot join lobbies";
        case 8: return "clan disabled";
        case 9: return "community ban";
        case 10: return "a lobby member blocked you";
        case 11: return "you blocked a lobby member";
        }
        return "unknown Steam response";
    }

    namespace
    {
        bool open_loop_sock(SOCKET& out, int& port, std::string& err)
        {
            SOCKET t = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (t == INVALID_SOCKET)
            {
                err = fmt("loopback socket failed: WSA %d", WSAGetLastError());
                return false;
            }
            sockaddr_in a{};
            a.sin_family = AF_INET;
            a.sin_port = 0;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int al = sizeof(a);
            if (bind(t, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0 || getsockname(t, reinterpret_cast<sockaddr*>(&a), &al) != 0)
            {
                err = fmt("loopback bind failed: WSA %d", WSAGetLastError());
                closesocket(t);
                return false;
            }
            u_long nb = 1;
            ioctlsocket(t, FIONBIO, &nb);
            BOOL off = FALSE;
            DWORD ret = 0;
            WSAIoctl(t, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
            int rb = 1 << 20;
            setsockopt(t, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rb), sizeof(rb));
            out = t;
            port = ntohs(a.sin_port);
            return true;
        }

        bool has(const std::vector<uint64_t>& v, uint64_t x)
        {
            for (uint64_t y : v)
                if (y == x) return true;
            return false;
        }
    }

    struct Link::Impl
    {
        struct Cb final : CbBase
        {
            std::atomic<Impl*> p{nullptr};
            void (*fn)(Impl*, void*, bool, uint64_t) = nullptr;
            int size = 0;
            void Run(void* d) override
            {
                if (Impl* o = p.load()) fn(o, d, false, 0);
            }
            void Run(void* d, bool io, uint64_t h) override
            {
                if (Impl* o = p.load()) fn(o, d, io, h);
            }
            int GetCallbackSizeBytes() override { return size; }
        };
        struct Cbs
        {
            Cb join_req, sess_req, created, enter, listed;
        };
        enum Cmd : int { c_invite = 1, c_join, c_leave, c_host_open, c_host_close, c_host_limit, c_find };
        struct Q
        {
            int c = 0;
            uint64_t v = 0;
            HostOpts h;
            FindOpts f;
        };

        Config cfg;
        LogFn log;
        FailFn on_fail;
        EventFn on_ev;
        Api api;
        std::thread th;
        std::atomic<bool> alive{false};
        std::atomic<bool> looping{false};
        std::atomic<bool> stop_req{false};
        HANDLE wake = nullptr;
        SOCKET s = INVALID_SOCKET;
        int port = 0;
        bool wsa = false;
        double t0 = 0;
        void** slot = nullptr;
        std::atomic<bool> hooked{false};
        bool bind_said = false;
        std::atomic<State> st{State::off};
        std::atomic<uint64_t> self_id{0}, lobby_id{0}, peer_id{0};
        std::atomic<void*> msgs{nullptr};
        std::atomic<bool> ready{false}, unhook_req{false}, unhook_done{false}, failed{false};
        std::atomic<bool> host_on{false}, multi_on{false};
        std::atomic<int> relay_port{0}, lmax{0};
        std::atomic<uint64_t> pumps{0};
        std::atomic<DWORD> pump_tid{0};
        std::unique_ptr<Cbs> cbs;
        void* user = nullptr;
        void* fr = nullptr;
        void* mm = nullptr;
        void* ut = nullptr;
        bool cb_reg = false;
        bool creating = false, joining = false, listing = false;
        uint64_t create_h = 0, join_h = 0, list_h = 0, joining_id = 0;
        std::string joining_via;
        bool want_host = false;
        HostOpts hopt;
        std::vector<uint64_t> cur_mem;
        std::vector<uint64_t> cands;
        size_t cand_i = 0;
        bool find_mode = false;
        int full_seen = 0;
        int last_code = 0;
        std::string last_why;
        double next_poll = 0, retry_at = 0;
        std::mutex qmu;
        std::vector<Q> q;
        std::mutex smu;
        std::vector<uint64_t> sreq;
        mutable std::mutex pmu;
        Stats pub;
        std::string peer_name;
        mutable std::mutex mmu;
        std::vector<uint64_t> mem;
        std::map<uint64_t, int> mports;
        std::atomic<uint64_t> mem_ver{0};
        std::atomic<uint64_t> invites{0}, joins{0};
        std::unordered_map<std::string, double> last_warn;

        void say(const std::string& m)
        {
            if (log) log("S2NET steam: " + m);
        }
        void warn(const char* key, const std::string& m)
        {
            double t = now_s();
            auto it = last_warn.find(key);
            if (it != last_warn.end() && t - it->second < 5) return;
            last_warn[key] = t;
            say(m);
        }
        void emit(Event e)
        {
            if (on_ev) on_ev(e);
        }
        std::string name_of(uint64_t id)
        {
            if (!fr || !id) return "?";
            const char* n = api.FriendPersonaName(fr, id);
            return n && *n ? n : "?";
        }
        void set_peer(uint64_t id, const std::string& name)
        {
            peer_id.store(id);
            std::lock_guard<std::mutex> g(pmu);
            peer_name = name;
        }
        void publish_mem(const std::vector<uint64_t>& v)
        {
            {
                std::lock_guard<std::mutex> g(mmu);
                mem = v;
            }
            mem_ver.fetch_add(1);
            if (wake) SetEvent(wake);
        }
        std::vector<uint64_t> lobby_members(uint64_t lb)
        {
            std::vector<uint64_t> v;
            int n = api.NumMembers(mm, lb);
            for (int i = 0; i < n; ++i)
                if (uint64_t mb = api.MemberByIndex(mm, lb, i)) v.push_back(mb);
            return v;
        }

        static void pre_thunk(void* self) { static_cast<Impl*>(self)->pre_pump(); }
        static void on_join_req_s(Impl* p, void* d, bool, uint64_t) { p->on_join_req(d); }
        static void on_sess_req_s(Impl* p, void* d, bool, uint64_t) { p->on_sess_req(d); }
        static void on_created_s(Impl* p, void* d, bool io, uint64_t h) { p->on_created(d, io, h); }
        static void on_enter_s(Impl* p, void* d, bool io, uint64_t h) { p->on_enter(d, io, h); }
        static void on_listed_s(Impl* p, void* d, bool io, uint64_t h) { p->on_listed(d, io, h); }

        void make_cbs()
        {
            cbs = std::make_unique<Cbs>();
            auto init = [&](Cb& c, void (*fn)(Impl*, void*, bool, uint64_t), int id, int size)
            {
                c.fn = fn;
                c.id = id;
                c.size = size;
                c.p.store(this);
            };
            init(cbs->join_req, &on_join_req_s, CB_LOBBY_JOIN_REQ, SIZE_LOBBY_JOIN_REQ);
            init(cbs->sess_req, &on_sess_req_s, CB_SESSION_REQ, SIZE_IDENTITY);
            init(cbs->created, &on_created_s, CB_LOBBY_CREATED, SIZE_LOBBY_CREATED);
            init(cbs->enter, &on_enter_s, CB_LOBBY_ENTER, SIZE_LOBBY_ENTER);
            init(cbs->listed, &on_listed_s, CB_LOBBY_MATCH_LIST, SIZE_LOBBY_MATCH_LIST);
        }

        void push(Q x)
        {
            {
                std::lock_guard<std::mutex> g(qmu);
                q.push_back(std::move(x));
            }
        }

        void pre_pump()
        {
            pump_tid.store(GetCurrentThreadId());
            pumps.fetch_add(1);
            try
            {
                if (unhook_req.load())
                {
                    if (!unhook_done.load()) do_unhook();
                    return;
                }
                if (failed.load()) return;
                double t = now_s();
                if (!ready.load())
                {
                    try_ready();
                    if (!ready.load()) return;
                }
                std::vector<Q> cmds;
                {
                    std::lock_guard<std::mutex> g(qmu);
                    cmds.swap(q);
                }
                for (auto& c : cmds)
                {
                    if (c.c == c_invite) do_invite();
                    else if (c.c == c_join) do_join(c.v, 0, "command");
                    else if (c.c == c_leave) do_leave("left by command");
                    else if (c.c == c_host_open) do_host_open(c.h);
                    else if (c.c == c_host_close) do_host_close("closed by the game");
                    else if (c.c == c_host_limit) do_host_limit(c.h.members, c.h.multi);
                    else if (c.c == c_find) do_find(c.f);
                }
                if (t >= next_poll)
                {
                    next_poll = t + cfg.poll_s;
                    poll(t);
                }
            }
            catch (...)
            {
            }
        }

        void try_ready()
        {
            if (api.GetHSteamUser() == 0) return;
            void* u = api.User();
            void* f = api.Friends();
            void* m = api.Matchmaking();
            void* tl = api.Utils();
            void* ms = api.Messages();
            if (!u || !f || !m || !tl || !ms) return;
            if (!api.BLoggedOn(u)) return;
            user = u;
            fr = f;
            mm = m;
            ut = tl;
            uint64_t me = api.GetSteamID(u);
            if (!me) return;
            self_id.store(me);
            api.RegisterCallback(&cbs->join_req, CB_LOBBY_JOIN_REQ);
            api.RegisterCallback(&cbs->sess_req, CB_SESSION_REQ);
            cb_reg = true;
            if (void* nu = api.NetUtils()) api.InitRelay(nu);
            msgs.store(ms);
            const char* pn = api.PersonaName(f);
            say(fmt("ready: user %s (%llu), app %u, role %s, overlay %s", pn && *pn ? pn : "?", (unsigned long long)me, api.AppID(tl), cfg.role.c_str(),
                    api.OverlayEnabled(tl) ? "on" : "off"));
            ready.store(true);
            st.store(State::ready);
            Event e;
            e.kind = Event::Kind::ready;
            e.id = me;
            e.name = pn ? pn : "";
            emit(e);
            uint64_t l = 0;
            if (parse_connect_lobby(cfg.cmdline, l))
            {
                say(fmt("launched by Steam to join lobby %llu (+connect_lobby)", (unsigned long long)l));
                do_join(l, 0, "+connect_lobby");
            }
        }

        void poll(double t)
        {
            if (want_host)
            {
                host_poll(t);
                return;
            }
            if (st.load() == State::joined) client_poll();
        }

        void create_lobby(double t)
        {
            int type = hopt.open ? LOBBY_PUBLIC : LOBBY_FRIENDS_ONLY;
            uint64_t h = api.CreateLobby(mm, type, hopt.members);
            if (!h)
            {
                retry_at = t + cfg.retry_s;
                say(fmt("CreateLobby returned no call handle; retry in %.0f s", cfg.retry_s));
                return;
            }
            create_h = h;
            creating = true;
            st.store(State::creating);
            api.RegisterCallResult(&cbs->created, h);
        }

        void host_poll(double t)
        {
            uint64_t lb = lobby_id.load();
            if (!lb)
            {
                if (!creating && t >= retry_at) create_lobby(t);
                return;
            }
            uint64_t me = self_id.load();
            std::vector<uint64_t> now;
            for (uint64_t mb : lobby_members(lb))
                if (mb != me && !has(now, mb)) now.push_back(mb);
            if (now == cur_mem) return;
            std::vector<uint64_t> gone, came;
            for (uint64_t x : cur_mem)
                if (!has(now, x)) gone.push_back(x);
            for (uint64_t x : now)
                if (!has(cur_mem, x)) came.push_back(x);
            cur_mem = now;
            publish_mem(cur_mem);
            uint64_t first = cur_mem.empty() ? 0 : cur_mem.front();
            if (!multi_on.load()) set_peer(first, first ? name_of(first) : std::string());
            else set_peer(0, std::string());
            int n = (int)cur_mem.size() + 1 + (int)gone.size() - (int)came.size();
            for (uint64_t x : gone)
            {
                --n;
                std::string nm = name_of(x);
                say(fmt("%s (%llu) left the lobby (%d/%d players)", nm.c_str(), (unsigned long long)x, n, hopt.members));
                Event e;
                e.kind = Event::Kind::member_out;
                e.lobby = lb;
                e.id = x;
                e.name = nm;
                e.n = n;
                e.max = hopt.members;
                emit(e);
            }
            for (uint64_t x : came)
            {
                ++n;
                std::string nm = name_of(x);
                say(fmt("%s (%llu) joined the lobby (%d/%d players): co-op link up", nm.c_str(), (unsigned long long)x, n, hopt.members));
                Event e;
                e.kind = Event::Kind::member_in;
                e.lobby = lb;
                e.id = x;
                e.name = nm;
                e.n = n;
                e.max = hopt.members;
                emit(e);
            }
        }

        bool host_present(uint64_t lb, uint64_t host)
        {
            for (uint64_t mb : lobby_members(lb))
                if (mb == host) return true;
            return false;
        }

        void client_poll()
        {
            uint64_t lb = lobby_id.load(), host = peer_id.load();
            if (!lb) return;
            if (host && host_present(lb, host)) return;
            std::string nm;
            {
                std::lock_guard<std::mutex> g(pmu);
                nm = peer_name;
            }
            do_leave("the host left the lobby");
            Event e;
            e.kind = Event::Kind::host_left;
            e.lobby = lb;
            e.id = host;
            e.name = nm;
            emit(e);
        }

        void do_invite()
        {
            invites.fetch_add(1);
            uint64_t lb = lobby_id.load();
            if (!want_host)
            {
                say("invite: this game is not hosting a co-op session (pick Host co-op first)");
                return;
            }
            if (!lb)
            {
                say("invite: the lobby is not ready yet");
                return;
            }
            if (!api.OverlayEnabled(ut)) say("invite: the Steam overlay is off for this game; friends can still use Join Game in the Steam friends list");
            api.InviteDialog(fr, lb);
            say(fmt("invite dialog opened for lobby %llu", (unsigned long long)lb));
        }

        void do_host_open(const HostOpts& o)
        {
            if (lobby_id.load() && !want_host) do_leave("hosting instead");
            if (joining)
            {
                api.UnregisterCallResult(&cbs->enter, join_h);
                joining = false;
            }
            bool had = want_host && lobby_id.load();
            HostOpts old = hopt;
            hopt = o;
            if (hopt.members < 2) hopt.members = 2;
            want_host = true;
            host_on.store(true);
            multi_on.store(hopt.multi);
            if (hopt.relay_port > 0) relay_port.store(hopt.relay_port);
            if (had && old.open == hopt.open && old.tag == hopt.tag)
            {
                do_host_limit(hopt.members, hopt.multi);
                return;
            }
            if (had) do_host_close("lobby type changed", true);
            retry_at = 0;
            next_poll = 0;
            st.store(State::creating);
            say(fmt("hosting: creating a %s lobby for up to %d players (%s)", hopt.open ? "public (steamdev)" : "friends-only", hopt.members,
                    hopt.multi ? "one loopback socket per member" : "one member"));
        }

        void do_host_close(const char* why, bool reopen = false)
        {
            if (!want_host) return;
            if (creating)
            {
                api.UnregisterCallResult(&cbs->created, create_h);
                creating = false;
            }
            uint64_t lb = lobby_id.exchange(0);
            if (lb) api.LeaveLobby(mm, lb);
            cur_mem.clear();
            publish_mem(cur_mem);
            set_peer(0, std::string());
            lmax.store(0);
            if (reopen) return;
            want_host = false;
            host_on.store(false);
            multi_on.store(false);
            st.store(State::ready);
            say(fmt("hosting closed%s%s: %s", lb ? ", left lobby " : "", lb ? std::to_string(lb).c_str() : "", why));
            Event e;
            e.kind = Event::Kind::closed;
            e.lobby = lb;
            emit(e);
        }

        void do_host_limit(int members, bool multi)
        {
            if (!want_host) return;
            if (members < 2) members = 2;
            hopt.members = members;
            hopt.multi = multi;
            multi_on.store(multi);
            uint64_t lb = lobby_id.load();
            if (lb)
            {
                bool ok = api.SetMemberLimit(mm, lb, members);
                api.SetLobbyData(mm, lb, KEY_MAX, std::to_string(members).c_str());
                lmax.store(members);
                say(fmt("lobby %llu player limit now %d (%s)%s", (unsigned long long)lb, members, multi ? "one loopback socket per member" : "one member",
                        ok ? "" : ": SetLobbyMemberLimit failed"));
            }
            uint64_t first = cur_mem.empty() ? 0 : cur_mem.front();
            if (!multi) set_peer(first, first ? name_of(first) : std::string());
            else set_peer(0, std::string());
            publish_mem(cur_mem);
        }

        void join_failed(uint64_t lb, int code, const std::string& why)
        {
            last_code = code;
            last_why = why;
            st.store(State::ready);
            if (cand_i < cands.size())
            {
                try_next();
                return;
            }
            cands.clear();
            cand_i = 0;
            Event e;
            e.kind = Event::Kind::join_fail;
            e.lobby = lb;
            e.code = code;
            e.why = why;
            e.n = find_mode ? 1 : 0;
            find_mode = false;
            emit(e);
        }

        void do_join(uint64_t lb, uint64_t from, const char* via)
        {
            if (!lb) return;
            if (want_host)
            {
                say(fmt("join request for lobby %llu ignored: this game is hosting a co-op session", (unsigned long long)lb));
                return;
            }
            if (lb == lobby_id.load() && st.load() == State::joined)
            {
                say(fmt("already in lobby %llu", (unsigned long long)lb));
                Event e;
                e.kind = Event::Kind::joined;
                e.lobby = lb;
                e.id = peer_id.load();
                {
                    std::lock_guard<std::mutex> g(pmu);
                    e.name = peer_name;
                }
                e.n = api.NumMembers(mm, lb);
                e.max = api.GetMemberLimit(mm, lb);
                emit(e);
                return;
            }
            if (joining)
            {
                api.UnregisterCallResult(&cbs->enter, join_h);
                joining = false;
            }
            if (lobby_id.load()) do_leave("switching lobby");
            uint64_t h = api.JoinLobby(mm, lb);
            if (!h)
            {
                say(fmt("JoinLobby %llu returned no call handle", (unsigned long long)lb));
                join_failed(lb, ENTER_ERROR, "JoinLobby returned no call handle");
                return;
            }
            joins.fetch_add(1);
            join_h = h;
            joining = true;
            joining_id = lb;
            joining_via = via ? via : "";
            st.store(State::joining);
            api.RegisterCallResult(&cbs->enter, h);
            if (from) say(fmt("joining lobby %llu of %s (%llu) (%s)", (unsigned long long)lb, name_of(from).c_str(), (unsigned long long)from, joining_via.c_str()));
            else say(fmt("joining lobby %llu (%s)", (unsigned long long)lb, joining_via.c_str()));
        }

        void do_leave(const char* why)
        {
            if (joining)
            {
                api.UnregisterCallResult(&cbs->enter, join_h);
                joining = false;
            }
            uint64_t lb = want_host ? 0 : lobby_id.exchange(0);
            if (lb) api.LeaveLobby(mm, lb);
            if (!want_host)
            {
                set_peer(0, std::string());
                st.store(State::ready);
            }
            if (lb) say(fmt("left lobby %llu: %s", (unsigned long long)lb, why));
        }

        void do_find(const FindOpts& o)
        {
            if (want_host)
            {
                say("find: this game is hosting a co-op session");
                join_failed(0, ENTER_NOT_ALLOWED, "this game is hosting a co-op session");
                return;
            }
            if (st.load() == State::joined && lobby_id.load())
            {
                do_join(lobby_id.load(), 0, "find");
                return;
            }
            if (listing)
            {
                api.UnregisterCallResult(&cbs->listed, list_h);
                listing = false;
            }
            cands.clear();
            cand_i = 0;
            full_seen = 0;
            last_code = 0;
            last_why.clear();
            find_mode = true;
            if (o.friends)
            {
                uint32_t app = api.AppID(ut);
                int n = api.FriendCount(fr, FRIEND_FLAG_IMMEDIATE);
                int playing = 0;
                for (int i = 0; i < n && i < 2000; ++i)
                {
                    uint64_t id = api.FriendByIndex(fr, i, FRIEND_FLAG_IMMEDIATE);
                    FriendGame g{};
                    if (!id || !api.FriendGamePlayed(fr, id, &g)) continue;
                    if ((uint32_t)(g.game & 0xffffff) != app) continue;
                    ++playing;
                    if (!g.lobby || has(cands, g.lobby)) continue;
                    cands.push_back(g.lobby);
                    say(fmt("friend %s (%llu) is in lobby %llu", name_of(id).c_str(), (unsigned long long)id, (unsigned long long)g.lobby));
                }
                say(fmt("find: %d friend(s), %d in this game, %d lobby(s)", n, playing, (int)cands.size()));
            }
            if (o.search)
            {
                api.ListStringFilter(mm, LOBBY_KEY, LOBBY_VAL, LOBBY_CMP_EQUAL);
                if (!o.tag.empty()) api.ListStringFilter(mm, KEY_DEV, o.tag.c_str(), LOBBY_CMP_EQUAL);
                api.ListDistanceFilter(mm, LOBBY_DIST_WORLDWIDE);
                uint64_t h = api.RequestLobbyList(mm);
                if (h)
                {
                    list_h = h;
                    listing = true;
                    st.store(State::finding);
                    api.RegisterCallResult(&cbs->listed, h);
                    say(fmt("find: lobby search %s=%s%s%s (worldwide)", LOBBY_KEY, LOBBY_VAL, o.tag.empty() ? "" : " nola_dev=", o.tag.c_str()));
                    return;
                }
                say("find: RequestLobbyList returned no call handle");
            }
            finish_find();
        }

        void on_listed(void* d, bool io, uint64_t h)
        {
            if (h != list_h) return;
            listing = false;
            uint32_t n = 0;
            std::memcpy(&n, d, 4);
            if (io) n = 0;
            uint64_t me = self_id.load();
            for (uint32_t i = 0; i < n && i < 200; ++i)
            {
                uint64_t lb = api.LobbyByIndex(mm, (int)i);
                if (!lb || has(cands, lb)) continue;
                const char* v = api.GetLobbyData(mm, lb, LOBBY_KEY);
                if (!v || std::strcmp(v, LOBBY_VAL) != 0) continue;
                const char* hs = api.GetLobbyData(mm, lb, KEY_HOST);
                uint64_t host = hs ? std::strtoull(hs, nullptr, 10) : 0;
                if (host && host == me) continue;
                const char* nm = api.GetLobbyData(mm, lb, KEY_NAME);
                int mem = api.NumMembers(mm, lb), lim = api.GetMemberLimit(mm, lb);
                if (lim > 0 && mem >= lim)
                {
                    ++full_seen;
                    say(fmt("find: lobby %llu of %s is full (%d/%d): skipped", (unsigned long long)lb, nm && *nm ? nm : "?", mem, lim));
                    continue;
                }
                cands.push_back(lb);
                say(fmt("find: lobby %llu of %s (%d/%d players)", (unsigned long long)lb, nm && *nm ? nm : "?", mem, lim));
            }
            say(fmt("find: lobby search returned %u lobby(s)%s", n, io ? " (io failure)" : ""));
            finish_find();
        }

        void finish_find()
        {
            st.store(State::ready);
            Event e;
            e.kind = Event::Kind::found;
            e.n = (int)cands.size();
            e.code = full_seen;
            emit(e);
            if (!cands.empty())
            {
                try_next();
                return;
            }
            if (full_seen > 0) join_failed(0, ENTER_FULL, "every Steam co-op host found is full");
            else find_mode = false;
        }

        void try_next()
        {
            if (cand_i >= cands.size()) return;
            uint64_t lb = cands[cand_i++];
            do_join(lb, 0, "find");
        }

        void on_join_req(void* d)
        {
            uint64_t lb = 0, from = 0;
            std::memcpy(&lb, d, 8);
            std::memcpy(&from, static_cast<uint8_t*>(d) + 8, 8);
            cands.clear();
            cand_i = 0;
            find_mode = false;
            do_join(lb, from, "Steam invite or Join Game");
        }

        void on_sess_req(void* d)
        {
            uint64_t id = api.IdGet(static_cast<const Identity*>(d));
            {
                std::lock_guard<std::mutex> g(smu);
                sreq.push_back(id);
            }
            if (wake) SetEvent(wake);
        }

        void on_created(void* d, bool io, uint64_t h)
        {
            if (h != create_h) return;
            creating = false;
            int32_t res = 0;
            uint64_t lb = 0;
            std::memcpy(&res, d, 4);
            std::memcpy(&lb, static_cast<uint8_t*>(d) + 8, 8);
            if (io || res != RESULT_OK || !lb)
            {
                retry_at = now_s() + cfg.retry_s;
                say(fmt("CreateLobby failed (io=%d result=%d); retry in %.0f s", (int)io, res, cfg.retry_s));
                return;
            }
            if (!want_host)
            {
                api.LeaveLobby(mm, lb);
                return;
            }
            uint64_t me = self_id.load();
            const char* pn = api.PersonaName(fr);
            api.SetLobbyData(mm, lb, LOBBY_KEY, LOBBY_VAL);
            api.SetLobbyData(mm, lb, KEY_HOST, std::to_string(me).c_str());
            api.SetLobbyData(mm, lb, KEY_MAX, std::to_string(hopt.members).c_str());
            api.SetLobbyData(mm, lb, KEY_NAME, pn && *pn ? pn : "host");
            if (hopt.open) api.SetLobbyData(mm, lb, KEY_DEV, hopt.tag.c_str());
            lobby_id.store(lb);
            lmax.store(hopt.members);
            cur_mem.clear();
            st.store(State::hosting);
            say(fmt("%s lobby %llu ready (up to %d players)%s%s", hopt.open ? "public" : "friends-only", (unsigned long long)lb, hopt.members,
                    hopt.open ? ", nola_dev=" : "", hopt.open ? hopt.tag.c_str() : ""));
            Event e;
            e.kind = Event::Kind::hosting;
            e.lobby = lb;
            e.id = me;
            e.name = pn ? pn : "";
            e.n = 1;
            e.max = hopt.members;
            emit(e);
        }

        void on_enter(void* d, bool io, uint64_t h)
        {
            if (h != join_h) return;
            joining = false;
            uint64_t lb = 0;
            uint32_t resp = 0;
            std::memcpy(&lb, d, 8);
            std::memcpy(&resp, static_cast<uint8_t*>(d) + 16, 4);
            if (io || resp != ENTER_SUCCESS || !lb)
            {
                int code = io ? ENTER_ERROR : (int)resp;
                std::string why = io ? std::string("Steam io failure") : enter_text(code);
                say(fmt("could not join lobby %llu: %s (io=%d response=%u)", (unsigned long long)joining_id, why.c_str(), (int)io, resp));
                join_failed(joining_id, code, why);
                return;
            }
            const char* v = api.GetLobbyData(mm, lb, LOBBY_KEY);
            uint64_t me = self_id.load();
            if (!v || std::strcmp(v, LOBBY_VAL) != 0)
            {
                api.LeaveLobby(mm, lb);
                say(fmt("lobby %llu is not a NOLA lobby (%s=%s): left it", (unsigned long long)lb, LOBBY_KEY, v ? v : ""));
                join_failed(lb, ENTER_NOT_NOLA, "not a NOLA co-op lobby");
                return;
            }
            const char* hs = api.GetLobbyData(mm, lb, KEY_HOST);
            uint64_t host = hs && *hs ? std::strtoull(hs, nullptr, 10) : 0;
            if (!host) host = api.LobbyOwner(mm, lb);
            if (!host || host == me || !host_present(lb, host))
            {
                api.LeaveLobby(mm, lb);
                say(fmt("lobby %llu has no host: left it", (unsigned long long)lb));
                join_failed(lb, ENTER_NO_HOST, "the host is not in the lobby");
                return;
            }
            lobby_id.store(lb);
            std::string nm = name_of(host);
            if (nm == "?")
            {
                const char* ln = api.GetLobbyData(mm, lb, KEY_NAME);
                if (ln && *ln) nm = ln;
            }
            set_peer(host, nm);
            st.store(State::joined);
            cands.clear();
            cand_i = 0;
            find_mode = false;
            int n = api.NumMembers(mm, lb), mx = api.GetMemberLimit(mm, lb);
            say(fmt("joined the lobby of %s (%llu): co-op link up (%d/%d players, %s)", nm.c_str(), (unsigned long long)host, n, mx, joining_via.c_str()));
            Event e;
            e.kind = Event::Kind::joined;
            e.lobby = lb;
            e.id = host;
            e.name = nm;
            e.n = n;
            e.max = mx;
            e.why = joining_via;
            emit(e);
        }

        void do_unhook()
        {
            if (cb_reg)
            {
                api.UnregisterCallback(&cbs->join_req);
                api.UnregisterCallback(&cbs->sess_req);
                cb_reg = false;
            }
            if (creating)
            {
                api.UnregisterCallResult(&cbs->created, create_h);
                creating = false;
            }
            if (joining)
            {
                api.UnregisterCallResult(&cbs->enter, join_h);
                joining = false;
            }
            if (listing)
            {
                api.UnregisterCallResult(&cbs->listed, list_h);
                listing = false;
            }
            uint64_t lb = lobby_id.exchange(0);
            if (lb && mm) api.LeaveLobby(mm, lb);
            unhook_done.store(true);
        }

        void try_hook()
        {
            if (!api.mod)
            {
                HMODULE m = GetModuleHandleW(L"steam_api64.dll");
                if (!m) return;
                std::string missing;
                if (!api.load(m, missing))
                {
                    fail("steam_api64.dll lacks " + missing);
                    return;
                }
                say("steam_api64.dll is loaded");
            }
            void* cur = *const_cast<void* volatile*>(slot);
            if (cur != (void*)api.RunCallbacks)
            {
                if (!bind_said) say(fmt("waiting for the game's first SteamAPI_RunCallbacks (import slot holds %p, not %p)", cur, (void*)api.RunCallbacks));
                bind_said = true;
                return;
            }
            Hook& h = g_hooks[cfg.hook];
            h.real.store(api.RunCallbacks);
            h.pre.store(&Impl::pre_thunk);
            h.link.store(this);
            if (!write_slot(slot, (void*)kTramps[cfg.hook]))
            {
                h.link.store(nullptr);
                fail("could not write the SteamAPI_RunCallbacks import slot");
                return;
            }
            hooked.store(true);
            say("hooked the game's SteamAPI_RunCallbacks: Steam calls run on the game's Steam thread");
        }

        void fail(const std::string& why)
        {
            if (failed.exchange(true)) return;
            st.store(State::failed);
            say(why + ": Steam co-op is off this session (LAN co-op still works)");
            if (on_fail) on_fail();
        }

        void publish(const Stats& x)
        {
            std::lock_guard<std::mutex> g(pmu);
            pub = x;
            pub.pumps = pumps.load();
            pub.invites = invites.load();
            pub.joins = joins.load();
        }

        struct Ms
        {
            SOCKET s = INVALID_SOCKET;
            int port = 0;
            double close_at = 0;
            Identity id{};
        };

        void run()
        {
            Stats stt;
            sockaddr_in dst{};
            dst.sin_family = AF_INET;
            dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            WSAEVENT ev = WSACreateEvent();
            if (ev != WSA_INVALID_EVENT) WSAEventSelect(s, ev, FD_READ);
            HANDLE tmr = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            if (!tmr) tmr = CreateWaitableTimerW(nullptr, FALSE, nullptr);
            std::vector<char> buf(65536);
            std::map<uint64_t, double> pending;
            std::map<uint64_t, Ms> socks;
            uint64_t seen_ver = ~0ull;
            bool was_multi = false;
            uint64_t last_peer = 0;
            Identity pid{};
            Message* arr[32];
            auto close_sess = [&](void* ms, uint64_t id)
            {
                if (!ms || !id) return;
                Identity o{};
                api.IdClear(&o);
                api.IdSet(&o, id);
                api.Close(ms, &o);
            };
            auto pub_ports = [&]
            {
                std::lock_guard<std::mutex> g(mmu);
                mports.clear();
                for (auto& [id, m] : socks)
                    if (m.close_at == 0) mports[id] = m.port;
            };
            auto pump_sock = [&](SOCKET so, void* ms, const Identity& to, bool ok_to)
            {
                while (true)
                {
                    sockaddr_in from{};
                    int fl = sizeof(from);
                    int r = recvfrom(so, buf.data(), (int)buf.size(), 0, reinterpret_cast<sockaddr*>(&from), &fl);
                    if (r == SOCKET_ERROR)
                    {
                        int e = WSAGetLastError();
                        if (e == WSAEWOULDBLOCK || e == WSAECONNRESET) break;
                        if (e == WSAEMSGSIZE) continue;
                        warn("lrecv", fmt("loopback recv failed: WSA %d", e));
                        break;
                    }
                    if (from.sin_addr.s_addr != dst.sin_addr.s_addr || from.sin_port != dst.sin_port)
                    {
                        stt.local_foreign++;
                        continue;
                    }
                    if (!ms || !ok_to)
                    {
                        stt.nopeer++;
                        continue;
                    }
                    int res = api.SendToUser(ms, &to, buf.data(), (uint32_t)r, SEND_UNRELIABLE_NO_NAGLE_AUTORESTART, cfg.channel);
                    if (res == RESULT_OK)
                    {
                        stt.sent++;
                        stt.sent_bytes += (uint64_t)r;
                    }
                    else
                    {
                        stt.send_err++;
                        stt.last_err = res;
                        warn("send", fmt("SendMessageToUser failed: EResult %d (%d bytes)", res, r));
                    }
                }
            };
            while (!stop_req.load())
            {
                double t = now_s();
                if (!hooked.load() && !failed.load()) try_hook();
                if (!ready.load() && !failed.load() && t - t0 > cfg.wait_s)
                {
                    uint64_t pc = pumps.load();
                    if (!api.mod) fail(fmt("the game did not load steam_api64.dll within %.0f s (not the Steam version of the game, or Steam not running?)", cfg.wait_s));
                    else if (!hooked.load()) fail(fmt("the game did not call SteamAPI_RunCallbacks within %.0f s", cfg.wait_s));
                    else if (pc == 0) fail(fmt("the game did not pump Steam callbacks within %.0f s", cfg.wait_s));
                    else fail(fmt("Steam user not ready within %.0f s (Steam offline or not logged on; %llu pumps)", cfg.wait_s, (unsigned long long)pc));
                }
                dst.sin_port = htons((u_short)relay_port.load());
                void* ms = msgs.load();
                uint64_t pr = peer_id.load();
                bool mul = host_on.load() && multi_on.load();
                if (pr != last_peer)
                {
                    if (last_peer && !(socks.count(last_peer) && socks[last_peer].close_at == 0)) close_sess(ms, last_peer);
                    last_peer = pr;
                    api.IdClear(&pid);
                    if (pr) api.IdSet(&pid, pr);
                }
                uint64_t ver = mem_ver.load();
                if (ver != seen_ver || mul != was_multi)
                {
                    seen_ver = ver;
                    was_multi = mul;
                    std::vector<uint64_t> cur;
                    if (mul)
                    {
                        std::lock_guard<std::mutex> g(mmu);
                        cur = mem;
                    }
                    bool changed = false;
                    for (uint64_t id : cur)
                    {
                        auto it = socks.find(id);
                        if (it != socks.end())
                        {
                            if (it->second.close_at != 0)
                            {
                                it->second.close_at = 0;
                                changed = true;
                            }
                            continue;
                        }
                        Ms m;
                        std::string err;
                        if (!open_loop_sock(m.s, m.port, err))
                        {
                            warn("msock", "member socket: " + err);
                            continue;
                        }
                        if (ev != WSA_INVALID_EVENT) WSAEventSelect(m.s, ev, FD_READ);
                        api.IdClear(&m.id);
                        api.IdSet(&m.id, id);
                        say(fmt("member %llu: loopback 127.0.0.1:%d <-> relay 127.0.0.1:%d", (unsigned long long)id, m.port, relay_port.load()));
                        socks[id] = m;
                        changed = true;
                    }
                    for (auto& [id, m] : socks)
                        if (m.close_at == 0 && !has(cur, id))
                        {
                            m.close_at = t + cfg.linger_s;
                            changed = true;
                        }
                    if (changed) pub_ports();
                }
                for (auto it = socks.begin(); it != socks.end();)
                {
                    if (it->second.close_at != 0 && t >= it->second.close_at)
                    {
                        if (it->first != pr) close_sess(ms, it->first);
                        closesocket(it->second.s);
                        say(fmt("member %llu: loopback 127.0.0.1:%d closed", (unsigned long long)it->first, it->second.port));
                        it = socks.erase(it);
                    }
                    else ++it;
                }
                auto allowed = [&](uint64_t id)
                {
                    if (pr && id == pr) return true;
                    auto it = socks.find(id);
                    return mul && it != socks.end() && it->second.close_at == 0;
                };
                std::vector<uint64_t> reqs;
                {
                    std::lock_guard<std::mutex> g(smu);
                    reqs.swap(sreq);
                }
                for (uint64_t id : reqs)
                {
                    if (pending.count(id)) continue;
                    pending[id] = t;
                    if (!allowed(id))
                    {
                        stt.sess_rej++;
                        say(fmt("session request from %llu not accepted: not %s", (unsigned long long)id, host_on.load() ? "a lobby member" : "the lobby host"));
                    }
                }
                for (auto it = pending.begin(); it != pending.end();)
                {
                    if (ms && allowed(it->first))
                    {
                        Identity o{};
                        api.IdClear(&o);
                        api.IdSet(&o, it->first);
                        api.Accept(ms, &o);
                        stt.sess_ok++;
                        say(fmt("session with %llu accepted", (unsigned long long)it->first));
                        it = pending.erase(it);
                    }
                    else if (t - it->second > 30) it = pending.erase(it);
                    else ++it;
                }
                pump_sock(s, ms, pid, pr != 0);
                for (auto& [id, m] : socks) pump_sock(m.s, ms, m.id, true);
                while (ms)
                {
                    int n = api.Receive(ms, cfg.channel, arr, 32);
                    if (n <= 0) break;
                    for (int i = 0; i < n; ++i)
                    {
                        Message* m = arr[i];
                        if (!m) continue;
                        uint64_t from = api.IdGet(&m->peer);
                        SOCKET out = INVALID_SOCKET;
                        auto it = mul ? socks.find(from) : socks.end();
                        if (it != socks.end()) out = it->second.s;
                        else if (pr && from == pr) out = s;
                        if (out != INVALID_SOCKET && m->size >= 0)
                        {
                            sendto(out, static_cast<const char*>(m->data), m->size, 0, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
                            stt.recv++;
                            stt.recv_bytes += (uint64_t)m->size;
                        }
                        else stt.foreign++;
                        api.Release(m);
                    }
                    if (n < 32) break;
                }
                stt.socks = (int)socks.size();
                publish(stt);
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG)cfg.loop_ms * 10000;
                if (tmr && SetWaitableTimer(tmr, &due, 0, nullptr, nullptr, FALSE))
                {
                    HANDLE hs[3] = {wake, tmr, ev};
                    DWORD cnt = ev != WSA_INVALID_EVENT ? 3 : 2;
                    WaitForMultipleObjects(cnt, hs, FALSE, (DWORD)cfg.loop_ms + 50);
                }
                else WaitForSingleObject(wake, (DWORD)cfg.loop_ms);
                if (ev != WSA_INVALID_EVENT) WSAResetEvent(ev);
            }
            void* ms = msgs.load();
            if (ms && last_peer) close_sess(ms, last_peer);
            for (auto& [id, m] : socks)
            {
                if (id != last_peer) close_sess(ms, id);
                closesocket(m.s);
            }
            socks.clear();
            pub_ports();
            if (tmr) CloseHandle(tmr);
            if (ev != WSA_INVALID_EVENT) WSACloseEvent(ev);
        }
    };

    Link::Link() : p_(std::make_unique<Impl>()) { p_->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr); }

    Link::~Link()
    {
        stop();
        if (p_ && p_->wake) CloseHandle(p_->wake);
    }

    bool Link::start(const Config& c, LogFn log, FailFn on_fail, EventFn on_event)
    {
        stop();
        Impl& I = *p_;
        if (I.looping.load())
        {
            if (I.log) I.log("S2NET steam: not started: the last link thread did not stop");
            return false;
        }
        I.cfg = c;
        if (I.cfg.loop_ms < 1) I.cfg.loop_ms = 1;
        if (I.cfg.hook < 0 || I.cfg.hook >= kHooks) I.cfg.hook = 0;
        if (I.cfg.linger_s < 0) I.cfg.linger_s = 0;
        I.log = std::move(log);
        I.on_fail = std::move(on_fail);
        I.on_ev = std::move(on_event);
        I.stop_req = false;
        I.ready = false;
        I.unhook_req = false;
        I.unhook_done = false;
        I.failed = false;
        I.pumps = 0;
        I.invites = 0;
        I.joins = 0;
        I.self_id = 0;
        I.lobby_id = 0;
        I.peer_id = 0;
        I.host_on = false;
        I.multi_on = false;
        I.relay_port = c.net_port;
        I.lmax = 0;
        I.msgs = nullptr;
        I.user = I.fr = I.mm = I.ut = nullptr;
        I.cb_reg = I.creating = I.joining = I.listing = false;
        I.want_host = false;
        I.hopt = HostOpts{};
        I.cur_mem.clear();
        I.cands.clear();
        I.cand_i = 0;
        I.find_mode = false;
        I.full_seen = 0;
        I.last_code = 0;
        I.last_why.clear();
        I.next_poll = I.retry_at = 0;
        I.last_warn.clear();
        {
            std::lock_guard<std::mutex> g(I.qmu);
            I.q.clear();
        }
        {
            std::lock_guard<std::mutex> g(I.smu);
            I.sreq.clear();
        }
        {
            std::lock_guard<std::mutex> g(I.mmu);
            I.mem.clear();
            I.mports.clear();
        }
        I.publish(Stats{});
        I.set_peer(0, std::string());
        I.st = State::off;
        if (c.net_port <= 0 || c.net_port > 65535)
        {
            I.say(fmt("not started: bad relay port %d", c.net_port));
            return false;
        }
        I.api = Api{};
        if (c.module)
        {
            std::string missing;
            if (!I.api.load(c.module, missing))
            {
                I.say("steam_api64.dll lacks " + missing);
                return false;
            }
        }
        void** slot = c.pump_slot ? c.pump_slot : find_import_slot(GetModuleHandleW(nullptr), "steam_api64.dll", "SteamAPI_RunCallbacks");
        if (!slot)
        {
            I.say("the game exe does not import SteamAPI_RunCallbacks");
            return false;
        }
        if (!I.wsa)
        {
            WSADATA w;
            if (WSAStartup(MAKEWORD(2, 2), &w) != 0)
            {
                I.say("WSAStartup failed");
                return false;
            }
            I.wsa = true;
        }
        SOCKET t = INVALID_SOCKET;
        int tp = 0;
        std::string err;
        if (!open_loop_sock(t, tp, err))
        {
            I.say(err);
            return false;
        }
        I.s = t;
        I.port = tp;
        I.make_cbs();
        I.slot = slot;
        I.hooked = false;
        I.bind_said = false;
        I.t0 = now_s();
        I.st = State::waiting;
        I.alive = true;
        I.say(fmt("started: role=%s relay 127.0.0.1:%d <-> 127.0.0.1:%d <-> Steam P2P, waiting up to %.0f s for Steam", I.cfg.role.c_str(), I.cfg.net_port, I.port,
                  I.cfg.wait_s));
        I.looping = true;
        I.th = std::thread([&I] {
            I.run();
            I.looping = false;
        });
        return true;
    }

    void Link::stop() { halt(false); }

    bool Link::stop_bounded() { return halt(true); }

    bool Link::halt(bool bounded)
    {
        if (!p_) return true;
        Impl& I = *p_;
        bool joined = true;
        if (I.th.joinable())
        {
            I.stop_req = true;
            SetEvent(I.wake);
            if (!bounded || WaitForSingleObject(I.th.native_handle(), (DWORD)(I.cfg.join_ms > 0 ? I.cfg.join_ms : 0)) == WAIT_OBJECT_0) I.th.join();
            else
            {
                I.th.detach();
                joined = false;
                I.say(fmt("the link thread did not stop within %d ms: left running", I.cfg.join_ms));
            }
        }
        if (I.hooked)
        {
            Hook& h = g_hooks[I.cfg.hook];
            bool on_pump = I.pump_tid.load() == GetCurrentThreadId();
            I.unhook_req = true;
            if (on_pump) I.do_unhook();
            double t = now_s();
            while (!I.unhook_done.load() && now_s() - t < I.cfg.unhook_wait_s) Sleep(5);
            bool done = I.unhook_done.load();
            restore_slot(I.slot, (void*)kTramps[I.cfg.hook], (void*)I.api.RunCallbacks);
            h.link.store(nullptr);
            t = now_s();
            while (!on_pump && h.inflight.load() > 0 && now_s() - t < I.cfg.unhook_wait_s) Sleep(1);
            if (!done && (I.cb_reg || I.creating || I.joining || I.listing))
            {
                I.cbs->join_req.p.store(nullptr);
                I.cbs->sess_req.p.store(nullptr);
                I.cbs->created.p.store(nullptr);
                I.cbs->enter.p.store(nullptr);
                I.cbs->listed.p.store(nullptr);
                I.cbs.release();
                I.say("the game did not pump Steam callbacks while stopping: callback objects kept registered and inert");
            }
            I.hooked = false;
        }
        I.slot = nullptr;
        if (I.s != INVALID_SOCKET)
        {
            closesocket(I.s);
            I.s = INVALID_SOCKET;
        }
        if (I.alive.exchange(false)) I.say("stopped");
        I.st = State::off;
        I.ready = false;
        I.msgs = nullptr;
        I.peer_id = 0;
        I.lobby_id = 0;
        I.host_on = false;
        I.multi_on = false;
        I.want_host = false;
        return joined;
    }

    bool Link::running() const { return p_->alive.load(); }
    bool Link::ready() const { return p_->alive.load() && p_->ready.load() && !p_->failed.load(); }
    int Link::port() const { return p_->port; }
    int Link::net_port() const { return p_->cfg.net_port; }
    void Link::set_relay_port(int port)
    {
        if (port > 0 && port <= 65535) p_->relay_port.store(port);
    }

    void Link::host_open(const HostOpts& o)
    {
        Impl::Q x;
        x.c = Impl::c_host_open;
        x.h = o;
        p_->push(std::move(x));
    }

    void Link::host_close()
    {
        Impl::Q x;
        x.c = Impl::c_host_close;
        p_->push(std::move(x));
    }

    void Link::host_limit(int members, bool multi)
    {
        Impl::Q x;
        x.c = Impl::c_host_limit;
        x.h.members = members;
        x.h.multi = multi;
        p_->push(std::move(x));
    }

    void Link::find(const FindOpts& o)
    {
        Impl::Q x;
        x.c = Impl::c_find;
        x.f = o;
        p_->push(std::move(x));
    }

    void Link::invite()
    {
        Impl::Q x;
        x.c = Impl::c_invite;
        p_->push(std::move(x));
    }

    void Link::join(uint64_t lobby)
    {
        Impl::Q x;
        x.c = Impl::c_join;
        x.v = lobby;
        p_->push(std::move(x));
    }

    void Link::leave()
    {
        Impl::Q x;
        x.c = Impl::c_leave;
        p_->push(std::move(x));
    }

    State Link::state() const { return p_->st.load(); }
    bool Link::hosting() const { return p_->host_on.load(); }
    bool Link::multi() const { return p_->host_on.load() && p_->multi_on.load(); }
    uint64_t Link::self() const { return p_->self_id.load(); }
    uint64_t Link::lobby() const { return p_->lobby_id.load(); }
    uint64_t Link::peer() const { return p_->peer_id.load(); }

    std::vector<uint64_t> Link::members() const
    {
        std::lock_guard<std::mutex> g(p_->mmu);
        return p_->mem;
    }

    int Link::member_port(uint64_t id) const
    {
        std::lock_guard<std::mutex> g(p_->mmu);
        auto it = p_->mports.find(id);
        return it == p_->mports.end() ? 0 : it->second;
    }

    Stats Link::stats() const
    {
        std::lock_guard<std::mutex> g(p_->pmu);
        Stats s = p_->pub;
        s.pumps = p_->pumps.load();
        return s;
    }

    std::string Link::status() const
    {
        Stats s = stats();
        std::string pn;
        {
            std::lock_guard<std::mutex> g(p_->pmu);
            pn = p_->peer_name;
        }
        size_t nm = members().size();
        return fmt("steam %s self=%llu lobby=%llu peer=%llu(%s) members=%d max=%d multi=%d socks=%d port=%d relay=%d sent=%llu recv=%llu foreign=%llu nopeer=%llu send_err=%llu last_err=%d "
                   "sess_ok=%llu sess_rej=%llu pumps=%llu",
                   state_name(state()), (unsigned long long)self(), (unsigned long long)lobby(), (unsigned long long)peer(), pn.c_str(), (int)nm, p_->lmax.load(), (int)multi(),
                   s.socks, p_->port, p_->relay_port.load(), (unsigned long long)s.sent, (unsigned long long)s.recv, (unsigned long long)s.foreign, (unsigned long long)s.nopeer,
                   (unsigned long long)s.send_err, s.last_err, (unsigned long long)s.sess_ok, (unsigned long long)s.sess_rej, (unsigned long long)s.pumps);
    }
}
