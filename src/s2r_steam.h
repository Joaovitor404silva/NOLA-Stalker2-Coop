#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace s2r
{
    struct SteamNote
    {
        enum class Kind : uint8_t { hosting = 0, closed, member_in, member_out, joined, join_fail, none, host_left, unavailable };
        Kind kind = Kind::none;
        uint64_t lobby = 0, id = 0;
        std::string name;
        int n = 0, max = 0, code = 0, port = 0;
        std::string why;
    };
    using SteamNoteFn = std::function<void(const SteamNote&)>;

    void steam_menu(SteamNoteFn fn);
    bool steam_find();
    void steam_session(int mode, const std::string& peer, int port);
    bool steam_host_peer(std::string& host, int& port);
    void steam_stop();
    int steam_stop_exit();
    void steam_invite();
    std::string steam_status();
}

namespace s2r::steam
{
    constexpr int CB_LOBBY_JOIN_REQ = 333;
    constexpr int CB_LOBBY_ENTER = 504;
    constexpr int CB_LOBBY_MATCH_LIST = 510;
    constexpr int CB_LOBBY_CREATED = 513;
    constexpr int CB_SESSION_REQ = 1251;
    constexpr int SIZE_LOBBY_JOIN_REQ = 16;
    constexpr int SIZE_LOBBY_ENTER = 24;
    constexpr int SIZE_LOBBY_MATCH_LIST = 4;
    constexpr int SIZE_LOBBY_CREATED = 16;
    constexpr int SIZE_IDENTITY = 136;
    constexpr int SIZE_FRIEND_GAME = 24;
    constexpr int SEND_UNRELIABLE_NO_NAGLE_AUTORESTART = 33;
    constexpr int LOBBY_FRIENDS_ONLY = 1;
    constexpr int LOBBY_PUBLIC = 2;
    constexpr int RESULT_OK = 1;
    constexpr int ENTER_SUCCESS = 1;
    constexpr int ENTER_DOESNT_EXIST = 2;
    constexpr int ENTER_NOT_ALLOWED = 3;
    constexpr int ENTER_FULL = 4;
    constexpr int ENTER_ERROR = 5;
    constexpr int ENTER_LIMITED = 7;
    constexpr int ENTER_NOT_NOLA = 100;
    constexpr int ENTER_NO_HOST = 101;
    constexpr int FRIEND_FLAG_IMMEDIATE = 4;
    constexpr int LOBBY_CMP_EQUAL = 0;
    constexpr int LOBBY_DIST_WORLDWIDE = 3;
    constexpr const char* LOBBY_KEY = "nola";
    constexpr const char* LOBBY_VAL = "2";
    constexpr const char* KEY_HOST = "host";
    constexpr const char* KEY_MAX = "max";
    constexpr const char* KEY_NAME = "name";
    constexpr const char* KEY_DEV = "nola_dev";

    class CbBase
    {
      public:
        virtual void Run(void* param) = 0;
        virtual void Run(void* param, bool io_failure, uint64_t call) = 0;
        virtual int GetCallbackSizeBytes() = 0;
        uint8_t flags = 0;
        int id = 0;
    };

    struct Identity
    {
        alignas(8) uint8_t b[SIZE_IDENTITY] = {};
    };

    struct Message
    {
        void* data;
        int size;
        uint32_t conn;
        Identity peer;
    };
    static_assert(offsetof(Message, size) == 8);
    static_assert(offsetof(Message, peer) == 16);

    struct FriendGame
    {
        uint64_t game;
        uint32_t ip;
        uint16_t port;
        uint16_t query;
        uint64_t lobby;
    };
    static_assert(sizeof(FriendGame) == SIZE_FRIEND_GAME);
    static_assert(offsetof(FriendGame, lobby) == 16);

    struct Api
    {
        void* mod = nullptr;
        int32_t (*GetHSteamUser)() = nullptr;
        void (*RunCallbacks)() = nullptr;
        void (*RegisterCallback)(CbBase*, int) = nullptr;
        void (*UnregisterCallback)(CbBase*) = nullptr;
        void (*RegisterCallResult)(CbBase*, uint64_t) = nullptr;
        void (*UnregisterCallResult)(CbBase*, uint64_t) = nullptr;
        void* (*User)() = nullptr;
        bool (*BLoggedOn)(void*) = nullptr;
        uint64_t (*GetSteamID)(void*) = nullptr;
        void* (*Friends)() = nullptr;
        void (*InviteDialog)(void*, uint64_t) = nullptr;
        const char* (*PersonaName)(void*) = nullptr;
        const char* (*FriendPersonaName)(void*, uint64_t) = nullptr;
        int (*FriendCount)(void*, int) = nullptr;
        uint64_t (*FriendByIndex)(void*, int, int) = nullptr;
        bool (*FriendGamePlayed)(void*, uint64_t, FriendGame*) = nullptr;
        void* (*Matchmaking)() = nullptr;
        uint64_t (*CreateLobby)(void*, int, int) = nullptr;
        uint64_t (*JoinLobby)(void*, uint64_t) = nullptr;
        void (*LeaveLobby)(void*, uint64_t) = nullptr;
        int (*NumMembers)(void*, uint64_t) = nullptr;
        uint64_t (*MemberByIndex)(void*, uint64_t, int) = nullptr;
        bool (*SetLobbyData)(void*, uint64_t, const char*, const char*) = nullptr;
        const char* (*GetLobbyData)(void*, uint64_t, const char*) = nullptr;
        uint64_t (*LobbyOwner)(void*, uint64_t) = nullptr;
        bool (*SetMemberLimit)(void*, uint64_t, int) = nullptr;
        int (*GetMemberLimit)(void*, uint64_t) = nullptr;
        uint64_t (*RequestLobbyList)(void*) = nullptr;
        void (*ListStringFilter)(void*, const char*, const char*, int) = nullptr;
        void (*ListDistanceFilter)(void*, int) = nullptr;
        uint64_t (*LobbyByIndex)(void*, int) = nullptr;
        void* (*Utils)() = nullptr;
        uint32_t (*AppID)(void*) = nullptr;
        bool (*OverlayEnabled)(void*) = nullptr;
        void* (*Messages)() = nullptr;
        int (*SendToUser)(void*, const Identity*, const void*, uint32_t, int, int) = nullptr;
        int (*Receive)(void*, int, Message**, int) = nullptr;
        bool (*Accept)(void*, const Identity*) = nullptr;
        bool (*Close)(void*, const Identity*) = nullptr;
        void (*Release)(Message*) = nullptr;
        void (*IdSet)(Identity*, uint64_t) = nullptr;
        uint64_t (*IdGet)(const Identity*) = nullptr;
        void (*IdClear)(Identity*) = nullptr;
        void* (*NetUtils)() = nullptr;
        void (*InitRelay)(void*) = nullptr;
        bool load(void* module, std::string& missing);
    };

    enum class Net : uint8_t { steam = 0, ip };
    bool parse_net(std::string_view v, Net& out);
    bool parse_connect_lobby(std::wstring_view cmdline, uint64_t& lobby);
    void** find_import_slot(void* module, const char* dll, const char* fn);
    const char* enter_text(int code);

    enum class State : uint8_t { off = 0, waiting, ready, creating, hosting, finding, joining, joined, failed };
    const char* state_name(State s);

    struct Config
    {
        std::string role;
        int net_port = 0;
        double wait_s = 60;
        std::wstring cmdline;
        void* module = nullptr;
        void** pump_slot = nullptr;
        int hook = 0;
        int channel = 0;
        int loop_ms = 2;
        double poll_s = 0.5;
        double retry_s = 10;
        double unhook_wait_s = 2;
        int join_ms = 1000;
        double linger_s = 2;
    };

    struct HostOpts
    {
        int members = 2;
        bool multi = false;
        bool open = false;
        std::string tag;
        int relay_port = 0;
    };

    struct FindOpts
    {
        bool friends = true;
        bool search = false;
        std::string tag;
    };

    struct Event
    {
        enum class Kind : uint8_t { ready = 0, hosting, closed, member_in, member_out, joined, join_fail, found, host_left };
        Kind kind = Kind::ready;
        uint64_t lobby = 0, id = 0;
        std::string name;
        int n = 0, max = 0, code = 0;
        std::string why;
    };

    struct Stats
    {
        uint64_t sent = 0, sent_bytes = 0, recv = 0, recv_bytes = 0, foreign = 0, local_foreign = 0, nopeer = 0, send_err = 0;
        uint64_t sess_ok = 0, sess_rej = 0, pumps = 0, invites = 0, joins = 0;
        int last_err = 0;
        int socks = 0;
    };

    using LogFn = std::function<void(const std::string&)>;
    using FailFn = std::function<void()>;
    using EventFn = std::function<void(const Event&)>;

    class Link
    {
      public:
        Link();
        ~Link();
        Link(const Link&) = delete;
        Link& operator=(const Link&) = delete;
        bool start(const Config& c, LogFn log, FailFn on_fail, EventFn on_event = {});
        void stop();
        bool stop_bounded();
        bool running() const;
        bool ready() const;
        int port() const;
        int net_port() const;
        void set_relay_port(int p);
        void host_open(const HostOpts& o);
        void host_close();
        void host_limit(int members, bool multi);
        void find(const FindOpts& o);
        void invite();
        void join(uint64_t lobby);
        void leave();
        State state() const;
        bool hosting() const;
        bool multi() const;
        uint64_t self() const;
        uint64_t lobby() const;
        uint64_t peer() const;
        std::vector<uint64_t> members() const;
        int member_port(uint64_t id) const;
        Stats stats() const;
        std::string status() const;

      private:
        bool halt(bool bounded);
        struct Impl;
        std::unique_ptr<Impl> p_;
    };
}
