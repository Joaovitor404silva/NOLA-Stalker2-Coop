#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace s2r
{
    namespace js
    {
        struct V;
        using Obj = std::vector<std::pair<std::string, V>>;
        struct V
        {
            enum T : uint8_t { null = 0, boolean, integer, real, string, array, object } t = null;
            bool b = false;
            double d = 0;
            std::string s;
            std::vector<V> a;
            Obj o;
            static V Str(std::string v);
            static V Int(int64_t v);
            static V Real(double v);
            static V Arr();
            static V Object();
            bool is_num() const { return t == boolean || t == integer || t == real; }
            double num() const;
            const V* get(std::string_view k) const;
            V* get(std::string_view k);
            void set(std::string_view k, V v);
            bool pop(std::string_view k, V* out = nullptr);
        };
        bool parse(std::string_view text, V& out, std::string* err = nullptr);
        void dump(const V& v, std::string& out, bool compact);
        std::string dumps(const V& v, bool compact);
        std::string float_repr(double x);
        std::string py_repr(const V& v);
        std::string py_str(const V& v);
        bool py_eq(const V& a, const V& b);
        bool py_lt(const V& a, const V& b, bool* type_error = nullptr);
        bool key_of(const V& v, std::string& out);
        bool utf8_valid(std::string_view s);
    }

    namespace z
    {
        std::string compress(std::string_view data, int level = 6);
        bool decompress(std::string_view data, std::string& out, std::string* err = nullptr);
        uint32_t crc32(std::string_view data, uint32_t crc = 0);
        uint32_t adler32(std::string_view data, uint32_t a = 1);
    }

    namespace proto
    {
        constexpr int PROTO = 2;
        constexpr uint8_t T_HDR = 1, T_NPC = 2, T_APP = 3, T_SXW = 4, T_SXI = 5, T_SXC = 6, T_BYE = 7, T_PEER = 8, T_ROSTER = 9, T_FULL = 10, T_DISC = 11, T_DISCR = 12;
        constexpr int MAXV = 4;
        constexpr size_t MTU_SAFE = 1200;
        constexpr double KEEPALIVE = 1.0;
        constexpr double APP_MIN_GAP = 0.5;
        constexpr double NPC_STALE = 5.0;
        constexpr size_t SX_CH = 1100;
        constexpr int64_t SX_MAX_SIZE = 512ll << 20;
        constexpr int64_t SX_MAX_CH = 65536;
        constexpr uint64_t SX_MAX_N = 1u << 20;
        constexpr int SX_WIN = 64;
        constexpr double SX_GAP = 0.05;
        constexpr double SX_RETRY = 0.6;
        constexpr double SX_REQ_LIVE = 10.0;

        std::string pack(uint8_t typ, const js::V& obj);
        enum class Unpack : uint8_t { ok = 0, v1, unknown, version, corrupt };
        Unpack unpack(std::string_view data, uint8_t& typ, js::V& obj, std::string& err);

        struct AppCache
        {
            std::unordered_map<std::string, js::V> map;
            std::vector<std::string> order;
            bool has(const std::string& key) const { return map.count(key) != 0; }
            void put(const std::string& key, js::V v);
            size_t size() const { return map.size(); }
        };

        class Sender
        {
          public:
            enum class Err : uint8_t { none = 0, json, proto, shape };
            Err split(std::string_view txt, double now, const AppCache& have, std::vector<std::string>& out, std::string& msg);
            Err split_n(std::string_view txt, double now, const std::vector<const AppCache*>& haves, std::vector<std::string>& common, std::vector<std::string>& hdrs,
                        std::string& msg);
            const std::set<std::string>& local_need() const { return local_need_; }
            const std::string& sid() const { return sid_; }

          private:
            struct Sent
            {
                std::vector<js::V> sig;
                double at = 0;
            };
            std::unordered_map<std::string, Sent> npc_sent_;
            std::unordered_map<std::string, double> app_sent_;
            std::set<std::string> local_need_;
            std::string sid_;
        };

        class Assembler
        {
          public:
            enum class Err : uint8_t { none = 0, proto, corrupt };
            Err feed(std::string_view data, double now, std::string& msg, uint8_t* typ_out = nullptr);
            bool render(const std::set<std::string>& local_need, double now, double dx, std::string& out, int pid = -1);
            AppCache apps;
            size_t npc_count() const { return npcs_.size(); }
            const js::V* hdr() const { return has_hdr_ ? &hdr_ : nullptr; }
            uint64_t stale = 0;
            uint64_t hdr_n = 0;

          private:
            struct Npc
            {
                js::V sid;
                js::V f;
                js::V n;
                double at = 0;
            };
            bool has_hdr_ = false;
            js::V hdr_;
            std::unordered_map<std::string, Npc> npcs_;
        };

        struct SxStats
        {
            uint64_t sent = 0, recv = 0, done = 0, crcbad = 0, req = 0;
            bool any() const { return sent || recv || done || crcbad || req; }
        };

        class SaveXfer
        {
          public:
            using Send = std::function<void(const std::string&)>;
            using Say = std::function<void(const std::string&)>;
            explicit SaveXfer(std::wstring dir, Say say);
            ~SaveXfer();
            void feed(std::string_view data, const Send& send);
            void tick(double now, const Send& send);
            SxStats stats;

            static void serve(const std::string& wid, const std::string& pkg, uint32_t crc, const js::V& obj, const Send& send, SxStats& stats);

          private:
            std::wstring p(const wchar_t* name) const;
            void load_pkg();
            void on_want(const js::V& obj, const Send& send);
            void reset();
            void start(const js::V& info);
            void save_map();
            void finish();
            void on_info(const js::V& obj);
            void on_chunk(std::string_view data);
            std::wstring dir_;
            Say say_;
            bool has_want_ = false;
            std::string want_id_;
            double want_chk_ = 0;
            bool has_info_ = false;
            uint64_t size_ = 0;
            uint32_t crc_ = 0;
            uint64_t ch_ = 0;
            uint64_t n_ = 0;
            std::string head_;
            std::vector<uint8_t> got_;
            std::map<uint64_t, double> asked_;
            void* part_ = nullptr;
            double next_want_ = 0, map_at_ = 0, last_info_ask_ = 0;
            bool has_pkg_ = false;
            std::string pkg_;
            int64_t pkg_mtime_ = 0;
            uint64_t pkg_size_ = 0;
            bool has_pkg_id_ = false;
            std::string pkg_id_;
            uint32_t pkg_crc_ = 0;
            std::string req_id_;
            double req_at_ = 0, now_ = 0;
        };
    }

    struct Config
    {
        std::string role;
        int listen = 0;
        std::string peer_host;
        int peer_port = 0;
        std::wstring dir;
        double dx = 0;
        double loss = 0;
        std::wstring record;
        int loop_ms = 20;
        bool files = false;
        int maxclients = 1;
        bool local_only = false;
        double slot_silent = 5;
        double slot_left = 60;
        double peer_gap = 0.1;
        std::set<std::string> relay_tags;
        std::string disc_name;
    };

    struct RosterEnt
    {
        int pid = 0;
        std::string sid;
        std::string st;
        std::string addr;
    };

    struct Roster
    {
        bool hub = false;
        bool full = false;
        int you = -1;
        int max = 1;
        uint64_t seq = 0;
        std::vector<RosterEnt> peers;
        std::string world;
    };

    std::set<std::string> relay_tags_d1(std::string_view tsv);
    std::string x1_relay(std::string_view x1, const std::set<std::string>& keep);

    struct Stats
    {
        uint64_t sent = 0, bytes = 0, big = 0, maxlen = 0, recv = 0, lost = 0, senderr = 0, badlocal = 0, badpeer = 0, written = 0;
        uint64_t stale = 0, apps = 0, npcs = 0;
        uint64_t tot_sent = 0, tot_recv = 0, tot_written = 0;
        uint64_t byes_rx = 0, byes_tx = 0;
        uint64_t peer_tx = 0, roster_tx = 0, full_tx = 0, peer_rx = 0, roster_rx = 0, full_rx = 0, app_fwd = 0;
        uint64_t disc_rx = 0, disc_tx = 0;
        int conns = 0;
        bool bound = false;
        double last_rx = -1;
        double last_in = -1;
    };

    using LogFn = std::function<void(const std::string&)>;

    class Net
    {
      public:
        Net();
        ~Net();
        Net(const Net&) = delete;
        Net& operator=(const Net&) = delete;
        bool start(const Config& c, LogFn log);
        void stop();
        bool running() const;
        void put_out(std::string text);
        void put_world(std::string w);
        bool take_in(std::string& text, uint64_t& seq);
        bool take_in(int view, std::string& text, uint64_t& seq);
        bool bye(int wait_ms);
        bool take_bye(uint64_t& seen, std::string& sid);
        bool take_bye(int view, uint64_t& seen, std::string& sid);
        Roster roster() const;
        bool hub() const;
        Stats stats() const;
        const Config& config() const;

      private:
        struct Impl;
        std::unique_ptr<Impl> p_;
    };

    namespace disc
    {
        struct Info
        {
            std::string name, id;
            int n = 1, max = 2, port = 0;
            bool hub = false;
        };
        struct Host
        {
            std::string ip, name, id;
            int port = 0, n = 0, max = 0;
            bool hub = false;
            double ms = 0;
            bool full() const { return max > 0 && n >= max; }
            std::string addr() const { return ip + ":" + std::to_string(port); }
        };
        struct Target
        {
            std::string ip;
            int port = 0;
        };
        std::string probe(int64_t nonce);
        bool is_probe(std::string_view dg, int64_t& nonce);
        std::string reply(const Info& i, int64_t nonce);
        bool parse_reply(std::string_view dg, Host& out, int64_t& nonce);
        bool add_host(std::vector<Host>& v, const Host& h);
        int pick(const std::vector<Host>& v, const std::vector<std::string>& pref);
        bool parse_addr(std::string_view s, std::string& ip, int& port);
        std::vector<Target> targets(const std::vector<int>& ports, const std::vector<Target>& extra, bool bcast);
        std::vector<Host> run(const std::vector<Target>& t, int wait_ms, std::string* err);
    }

    double clock_s();
    std::string narrow(const std::wstring& w);
    std::wstring widen(const std::string& s);
}
