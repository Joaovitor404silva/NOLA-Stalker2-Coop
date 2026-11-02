#pragma once
#include "s2x.h"
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace x0::in
{
    bool lua_tonumber(std::string_view s, Val& out);
    std::string lua_num_str(double v);
    std::string lua_num_str_or_int(double v);
    std::string w2u(std::wstring_view w);
    std::wstring u2w(std::string_view s);

    struct FlagEnt
    {
        Val def;
        Val v;
        bool lua_known = false;
        bool has_def = false;
    };
    struct FlagLine
    {
        std::string k;
        std::string v;
        int line = 0;
    };
    std::vector<FlagLine> parse_flags_text(std::string_view text);

    struct Flags
    {
        std::unordered_map<std::string, FlagEnt> m;
        std::map<std::string, Val> own;
        std::map<std::string, Val> ad;
        std::string last_dup_sig;
        std::vector<std::pair<std::string, std::string>> dup_defaults;
        FlagEnt& ent(std::string_view k);
        const Val* find(std::string_view k) const;
        void set_default(std::string_view k, const Val& v, bool lua_known);
        int apply_text(std::string_view text, std::vector<std::string>* logs, bool all = false);
        bool set(std::string_view k, const Val& v, std::vector<std::string>* logs, std::string_view why);
        int hc_adopt(std::string_view rest, const std::string& hc_keys, std::vector<std::string>* logs, const std::string& peer_id, std::string& sum_key);
        void hc_give_back(std::string_view why, std::string_view only, std::vector<std::string>* logs);
    };
    Flags& flags();
    extern const char* const kHcKeys;
    bool load_default_tables(Flags& f, std::vector<std::string>* logs);
    int flags_role_switch(Flags& f, const std::string& to, std::vector<std::string>* logs);

    using FlagNotify = std::function<void(const std::string&, const Val&, const Val&)>;
    void flags_set_notify(FlagNotify n);

    struct TagSpan
    {
        std::string_view tag;
        std::string_view pay;
        size_t begin = 0;
        size_t end = 0;
    };
    std::vector<TagSpan> x1_tags(std::string_view x1);
    bool x1_first(std::string_view x1, std::string_view tag, std::string& out);
    std::string_view x1_lead(std::string_view x1);
    std::string x1_strip(std::string_view x1, const std::set<std::string>& tags, bool strip_lead);
    std::vector<std::string_view> split_nonempty(std::string_view s, char sep);

    bool json_str(std::string_view head, std::string_view key, std::string& out, size_t* vb = nullptr, size_t* ve = nullptr);
    bool json_num(std::string_view head, std::string_view key, double& out);
    std::string_view snap_head(std::string_view text);
    bool parse_snap(std::string_view text, Snap& s);
    struct App
    {
        std::string a, mesh, parts, mats, statics;
    };
    std::vector<App> parse_apps(std::string_view text);
    uint32_t djb2(std::string_view s);
    std::string hash_hex(std::string_view s);

    struct NetRel
    {
        struct Q
        {
            std::string k;
            double at = 0;
            int64_t o = 0;
            int64_t n0 = 0;
            bool done = false, run = false, inq = false, rs = false, inc = false;
        };
        struct Hold
        {
            std::string k;
            double at = 0;
        };
        int64_t n = 0, ack = 0, ins = 0;
        std::map<std::string, Q> p;
        std::vector<std::pair<std::string, int64_t>> q;
        std::unordered_map<std::string, double> seen;
        std::vector<Hold> hold;
        bool has_pid = false;
        std::string pid;
        bool acked = false;
        std::string ack_out;
        bool has_raw = false;
        std::string raw, merged;
        double raw_t = 0, prune_at = -1e9;
        struct S { int rs = 0, rin = 0, dup = 0, gone = 0; double at = 0; } s;
        std::vector<std::string>* logs = nullptr;
        bool inited = false;
        bool sess(const std::string* peer_id);
        std::string x1in(const std::string& x1, double now, const std::string* peer_id, bool peer_ok, const std::string& my_nonce, double hold_s, double seen_s, bool nrdoor);
        std::string out(const std::string& s, double now, const std::string& remote_x1, const std::string* peer_id, bool peer_ok, const std::string& my_nonce,
                        double mx, int per, double hold_s, double seen_s, bool nrdoor);
    };
    bool nr_tag(std::string_view t, bool nrdoor);

    struct PeerMirror
    {
        Peer p;
        bool has_first = false;
        bool first_is_t = false;
        double first_t = 0;
        bool bye_has = false;
        std::string bye_sid;
        void update(const Snap& s, double now, double silent_s, double left_s, const std::string& my_nonce);
        void on_bye(double now, const std::string& sid);
        void on_local_end(double now);
    };

    struct Row
    {
        Mod m;
        std::string name;
        Rate r;
        std::function<void()> fn;
        int order = 0x7fffffff;
        double acc = 0;
        int64_t n = 0;
        double ms = 0, mx = 0;
        int over = 0, errs = 0;
        bool off = false;
    };
    struct Sched
    {
        std::deque<Row> rows;
        std::vector<size_t> run;
        struct After { Mod m; double at; std::function<void()> fn; };
        std::vector<After> afters;
        std::mutex qmu;
        std::deque<std::function<void()>> posted;
        int64_t dropped = 0;
        double last = -1, a100 = 0, a1000 = 0;
        int errmax = 5;
        std::vector<std::string>* logs = nullptr;
        std::function<bool(Mod)> active;
        std::function<double()> clock;
        std::string topName;
        double topMs = 0;
        void tick(double now);
        bool guard(const char* what, Mod m, int& errs, bool& off, const std::function<void()>& fn);
    };
    Sched& sched();

    struct ModEnt
    {
        Mod m;
        const char* id;
        void (*init)();
    };
    std::vector<ModEnt>& mod_regs();
    int order_of(std::string_view row);
    void run_mod_inits();

    struct LogSink
    {
        std::function<void(const std::string&)> out;
    };
    LogSink& log_sink();
    void set_role_for_test(const std::string& r, const std::wstring& dir);
    bool dev_locked(std::string_view k);

    bool tsv_parse(std::string_view text, std::vector<TsvRow>& rows, std::string* err);
    std::string tsv_unescape(std::string_view v);

    struct Bridge
    {
        std::recursive_mutex mu;
        struct View
        {
            Snap snap;
            std::string last_in_text;
            std::string merged_text;
            bool has_merged = false;
            bool pending = false;
            PeerMirror peer;
            NetRel nr;
            uint64_t bye_seen = 0;
            std::string mb_last;
            std::set<std::string> rneed;
            int sess_seen = 0;
            bool new_sess = false;
        };
        std::array<View, kMaxClients> vw;
        Snap& snap = vw[0].snap;
        std::string last_raw_x1;
        std::string& last_in_text = vw[0].last_in_text;
        std::string& merged_text = vw[0].merged_text;
        bool& has_merged = vw[0].has_merged;
        bool& pending = vw[0].pending;
        std::string my_x1;
        std::vector<std::string> vx1;
        std::string nonce;
        PeerMirror& peer = vw[0].peer;
        NetRel& nr = vw[0].nr;
        Roster ros;
        struct Own { Mod m; std::string tag; Producer p; std::string cache; bool has = false; int errs = 0; bool off = false; ProducerV pv; std::array<std::string, kMaxClients> vcache; std::array<bool, kMaxClients> vhas{}; };
        std::deque<Own> own;
        struct OwnV { Mod m; std::string tag; ProducerV p; std::array<std::string, kMaxClients> cache; std::array<bool, kMaxClients> has{}; int errs = 0; bool off = false; };
        std::deque<OwnV> ownv;
        struct Con { Mod m; std::string tag; Consumer c; int errs = 0; bool off = false; };
        std::deque<Con> cons;
        struct ConV { Mod m; std::string tag; ConsumerV c; int errs = 0; bool off = false; };
        std::deque<ConV> consv;
        struct SnapCb { Mod m; std::function<void(const Snap&)> c; int errs = 0; bool off = false; };
        std::deque<SnapCb> snapcbs;
        struct SnapCbV { Mod m; std::function<void(const Snap&, int)> c; int errs = 0; bool off = false; };
        std::deque<SnapCbV> snapcbsv;
        struct SessCb { Mod m; std::function<void(int, const Peer&)> c; int errs = 0; bool off = false; };
        std::deque<SessCb> sesscbs;
        struct LApp { std::string text, h, js; double sent = -100, used = -100; };
        std::map<std::string, LApp> lapps;
        struct RApp { std::string parts; double at = 0; };
        std::map<std::string, RApp> rapps;
        std::map<std::string, double> need;
        std::set<std::string> rneed;
        std::string put_text, put_h;
        int64_t outs = 0, ins = 0, outfail = 0;
        std::vector<std::string> logs;
        struct LocalIn { bool ok = false; double x = 0, y = 0, z = 0, vx = 0, vy = 0, yaw = 0, pt = 0; };
        std::string sid;
        int64_t f = 0;
        double proto_log_at = -100;
        std::string proto_err;
        int64_t nopawn = 0, noin = 0, reads = 0;
        struct Mbox
        {
            std::function<bool()> on;
            std::function<void(std::string)> put;
            std::function<bool(std::string&)> get;
            std::function<bool(uint64_t&, std::string&)> bye;
            std::function<bool(int, std::string&)> vget;
            std::function<bool(int, uint64_t&, std::string&)> vbye;
            std::function<bool(Roster&)> roster;
        };
        Mbox mbox;
        uint64_t& bye_seen = vw[0].bye_seen;
        std::string& mb_last = vw[0].mb_last;
        int views_n() const;
        bool hub_host() const;
        int view_pid(int v) const;
        void roster_step(double now);
        std::string personal(int v) const;
        const Own* pseudo(std::string_view tag) const;
        std::string make_x1(double now);
        std::string make_body(const LocalIn& L, double now);
        bool io_step(const LocalIn& L, double now);
        std::string take_in(const std::string& text, double now, bool& changed);
        std::string take_in_view(int v, const std::string& text, double now, bool& changed);
        void run_producers();
        void dispatch();
        void dispatch_view(int v);
        void dispatch_legacy(const Snap& s);
        std::string needs_out(double now);
        std::string apps_out(double now, int appsper);
        std::string apps_out(double now, int appsper, int& count);
        bool pseudo_lead(std::string_view tag) const { return tag == "@W"; }
    };
    Bridge& bridge();
    bool owner_ported(std::string_view tag);

    struct Cfg
    {
        bool netrel = false;
        bool nrdoor = false;
        double netrelmax = 120, netrelhold = 6, netrelseen = 300, peersilent = 5, netlockkeep = 0, peerleft = 60;
        bool peerbye = true;
        int netrelper = 40, appsper = 2;
    };
    Cfg bridge_cfg();

    bool write_atomic(const std::wstring& path, std::string_view bytes);
    bool read_all(const std::wstring& path, std::string& out);
    bool data_res(std::string_view name, std::string& out);
    std::vector<std::string> data_res_names();
    std::string cmd_run(const std::string& line);
    void cmd_poll();
    void flags_poll(double now, bool force);
    std::string status_text(std::string_view mod);
    void x0_tick_frame();
    void x0_tick_end();
    void io_tick();
    bool coop_lua_seen();
    void nat_after_read();
    void nat_feed_tick();
    void state_tick();
    std::string state_json(bool in_world, const double* hp, const double* mx, const bool* alive, const std::string* level, const bool* paused);
    void hooks_tick();
    void keys_tick();

    struct SlotView
    {
        bool ok = false;
        const void* obj = nullptr;
        int32_t serial = 0;
        bool dead = false;
        const void* cls = nullptr;
        uint64_t name = 0;
    };
    namespace tut
    {
        bool available();
        int show(const std::string& body, const std::string& title, std::string& why);
        bool set(const std::string& body);
        bool mine();
        void release();
        void tick();
        std::string status();
    }

    inline bool ref_check(const Ref& r, const SlotView& s)
    {
        if (!r.raw || r.idx < 0 || !s.ok) return false;
        if (s.obj != r.raw) return false;
        if (r.serial != 0 && s.serial != r.serial) return false;
        if (s.dead) return false;
        if (!s.cls) return false;
        if (s.name != r.name) return false;
        return true;
    }
}
