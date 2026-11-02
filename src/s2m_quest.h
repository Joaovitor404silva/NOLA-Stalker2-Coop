#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "s2m_save.h"

namespace s2m_quest
{
    using Id = uint64_t;

    struct V3
    {
        double x = 0, y = 0, z = 0;
    };

    struct Applied
    {
        bool ok = true;
        int r = -1;
        std::optional<std::string> why;
        std::string err;
    };

    struct DeadRes
    {
        bool skip = false;
        bool drop_pending = false;
        bool rebase = false;
    };

    struct Inv
    {
        std::map<std::string, int> items;
        double money = 0;
    };

    enum Mgr : int { mgr_missing = -1, call_failed = 0, call_ok = 1 };

    struct BusyIn
    {
        bool lead_dlg = false;
        bool host_dlg = false;
        bool cine = false;
        bool pc_cine = false;
        std::vector<std::pair<bool, bool>> views;
    };
    bool busy_of(const BusyIn& in, std::string& why);

    class Env
    {
      public:
        virtual ~Env() = default;
        virtual double now() = 0;
        virtual bool host() = 0;
        virtual double fl(const char* key, double def) = 0;
        virtual std::string fls(const char* key) = 0;
        virtual void fset(const char* key, double v, const char* why) = 0;
        virtual void log(const std::string& line) = 0;
        virtual void screen(const char*, const std::string&, double) {}

        virtual bool part(const char* tag, std::string& out) = 0;
        virtual int peer_state() = 0;
        virtual int peer_sessions() = 0;
        virtual bool peer_loopback() = 0;
        virtual int nviews() { return 1; }
        virtual bool part_v(int v, const char* tag, std::string& out) { return v == 0 && part(tag, out); }
        virtual int peer_state_v(int v) { return v == 0 ? peer_state() : 0; }
        virtual int peer_sessions_v(int v) { return v == 0 ? peer_sessions() : 0; }
        virtual bool peer_loopback_v(int v) { return v == 0 && peer_loopback(); }
        virtual int view_pid(int v) { return v == 0 ? (host() ? 1 : 0) : -1; }
        virtual bool world_held() { return false; }
        virtual bool look_ok() = 0;
        virtual std::string look_put(const std::string& text) = 0;

        virtual bool read(const std::string& name, std::string& out) = 0;
        virtual bool write(const std::string& name, const std::string& text) = 0;
        virtual bool append(const std::string& name, const std::string& text) = 0;
        virtual bool exists(const std::string& name) = 0;
        virtual bool remove(const std::string& name) = 0;
        virtual bool rename(const std::string& from, const std::string& to) = 0;
        virtual bool data(const char* name, std::string& out) = 0;
        virtual int64_t unix_time() = 0;
        virtual int rnd(int lo, int hi) = 0;
        virtual std::optional<double> game_secs() = 0;

        virtual Id pawn() = 0;
        virtual Id pawn_pc() = 0;
        virtual bool pos(Id a, V3& out) = 0;
        virtual Id interacted(Id pawn) = 0;
        virtual int count(Id obj, const std::string& sid) = 0;
        virtual bool give(const std::string& sid, int n, std::string& err) = 0;
        virtual bool aa_present() = 0;
        virtual int aa(const char* fn, const std::string& sid, std::string& err) = 0;
        virtual int dk(const char* fn, const std::string& a, const std::string* b, int win, std::string& err) = 0;
        virtual int rk_bool(const std::string& var, bool v, std::string& err) = 0;
        virtual bool load_last_save(std::string& err) = 0;
        virtual bool journal(int out[4]) = 0;
        virtual Inv inv_snap(Id pawn) = 0;
        virtual double money(Id pawn) = 0;
        virtual bool add_money(Id pawn, int64_t delta) = 0;

        virtual bool qi_npcadd(const std::string& sid) = 0;
        virtual std::optional<std::string> qr_npc(const std::string& sid) = 0;
        virtual bool nat_guid_listed(const std::string& guid) = 0;

        virtual bool inv_open() = 0;
        virtual Id hovered() = 0;
        virtual bool inv_call(const char* fn, std::string& err) = 0;
        virtual bool key_d() = 0;
        virtual std::string bind_d() = 0;

        virtual bool lead_mine() = 0;
        virtual bool lead_dlg() = 0;
        virtual bool host_dlg_open() = 0;
        virtual bool busy(std::string& why) = 0;
        virtual bool trig_deny(const std::string& sid, const std::string& seq) = 0;
        virtual void trig_entered(const std::string& sid, double x, double y, double z) = 0;
        virtual void mq_seen(const std::string& sid) = 0;
        virtual bool mq_sid(const std::string& sid) = 0;
        virtual void cj_cause(const std::string& sid) = 0;
        virtual void cj_before(const std::string& sid) = 0;
        virtual DeadRes qg_dead(Id pawn, bool rebase) = 0;
        virtual void qf_arm(const char* why) = 0;
        virtual bool n7_idle() = 0;
        virtual void n7_follow() = 0;
        virtual bool n7_rw(const std::string& quest) = 0;
        virtual std::string n7_b() = 0;
        virtual bool ql_settled() = 0;
        virtual bool join_pending() { return false; }
        virtual int trig_enter(const std::string&, int, std::string& info) { info = "sem motor nativo"; return -10; }
        virtual bool quest_muted(const std::string&) { return false; }
        virtual void cp_flush(const char* why) = 0;
        virtual void campaign_rotated() = 0;
        virtual s2m_save::QState& sq() = 0;
        virtual s2m_save::Counts& applied() = 0;
        virtual int qb_force(const std::string& node, const std::string& q, const std::string& pin, bool fin, std::string& err) = 0;
        virtual bool dlg_has(const std::string& phrase) = 0;
        virtual bool dlg_pick(const std::string& phrase, std::string& err) = 0;
    };

    struct Hooks
    {
        std::function<void(const char* why)> cp_flush;
        std::function<void()> campaign_rotated;
        std::function<bool(std::string_view sid)> qi_npcadd;
        std::function<std::optional<std::string>(std::string_view sid)> qr_npc;
        std::function<bool(const std::string& guid)> nat_guid_listed;
    };
    Hooks& hooks();

    void bind(Env* env);
    Env* env();
    void reset();

    void tick100();
    void tick100_rb();
    void tick100_trig();
    void tick100_qp();
    void tick100_qi();
    void tick100_qb();
    int qb_fire(const std::string& node, const std::string& pin, bool fin, std::string& sub);
    bool qb_answer(const std::string& phrase);
    bool qb_on();
    void tick1000();
    void console(std::string_view full);
    void key_d_pressed();

    std::string out_qn();
    std::string out_w();
    std::string out_j();
    std::string out_qd();
    std::string out_qh();
    std::string out_s();
    std::string out_b();
    std::string out_qi();
    std::string out_qp();
    std::string out_qf();
    std::string out_qb();
    std::string out_qd_v(int v);
    std::string out_s_v(int v);

    bool qi_table_on();
    bool is_quest_item(std::string_view sid);
    int qi_need(std::string_view sid);
    const std::vector<std::string>& qi_list();
    const std::map<std::string, int>& qi_needs();
    bool qi_corpse(Id corpse, const std::string& id);
    bool qi_host_keep(const std::string& id, Id pawn);
    void qi_reset();

    Applied quest_apply(const std::string& kind, const std::optional<std::string>& a, const std::optional<std::string>& b,
                        const std::optional<std::string>& c, const std::optional<std::string>& sid, bool live);
    Applied quest_apply_ev(const std::string& ev, bool live);
    void rb_done(const std::string& why);
    bool rb_ready();
    void rb_bump(const std::string& why);
    void camp_rotate(const std::string& why);
    std::string camp_id();
    std::size_t host_log_size();
    std::optional<int64_t> host_gen();
    void quest_finished(const std::string& quest);
    bool qp_skip(const std::string& sid);
    std::string status();
}
