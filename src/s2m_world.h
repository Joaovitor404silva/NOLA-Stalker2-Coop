#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "s2m_qobj_logic.h"

namespace s2m
{

struct Ref
{
    int32_t idx = -1;
    int32_t ser = 0;
    explicit operator bool() const { return idx >= 0; }
    bool operator==(const Ref& o) const { return idx == o.idx && ser == o.ser; }
};

struct Vec3
{
    double x = 0, y = 0, z = 0;
};

struct Arg
{
    enum Kind { Nil, Bool, Obj } kind = Nil;
    bool b = false;
    Ref o;
    static Arg nil() { return Arg(); }
    static Arg boolean(bool v) { Arg a; a.kind = Bool; a.b = v; return a; }
    static Arg object(Ref r) { Arg a; a.kind = Obj; a.o = r; return a; }
};

struct NamedRef
{
    std::string name;
    Ref ref;
};

enum class MeshRead { Ok, NoActor, NoComponent, NullMesh };

class Env
{
  public:
    virtual ~Env() {}
    virtual double now() = 0;
    virtual double flag(const char* name, double def) = 0;
    virtual bool is_host() = 0;
    virtual void log(const std::string& line) = 0;
    virtual bool remote(const char* tag, std::string& out) = 0;
    virtual bool peer_loopback() = 0;
    virtual int peer_sessions() = 0;
    virtual bool local_pawn(Vec3& pos, int64_t& id) = 0;
    virtual bool gate(std::string& key, bool& open) = 0;
    virtual bool level_name(std::string& out) = 0;
    virtual bool read_data(const char* name, std::string& out) = 0;
    virtual void list_class(const char* cls, std::vector<Ref>& out) = 0;
    virtual bool valid(Ref r) = 0;
    virtual bool location(Ref r, Vec3& out) = 0;
    virtual std::string full_name(Ref r) = 0;
    virtual std::string class_name(Ref r) = 0;
    virtual bool class_chain_has(Ref r, const char* cls, int depth) = 0;
    virtual bool prop_bool(Ref r, const char* prop, bool& out) = 0;
    virtual bool prop_obj(Ref r, const char* prop, Ref& out) = 0;
    virtual bool call_vec(Ref r, const char* fn, Vec3& out) = 0;
    virtual bool call_bool(Ref r, const char* fn, bool& out) = 0;
    virtual bool call_int(Ref r, const char* fn, int& out) = 0;
    virtual bool call(Ref r, const char* fn, const std::vector<Arg>& args) = 0;
    virtual MeshRead mesh_name(Ref actor, std::string& out) = 0;
    virtual Ref asset(const char* path) = 0;
    virtual bool spawn_fx(Ref at, Ref niagara) = 0;
    virtual bool play_sound(Ref at, Ref event) = 0;
    virtual void owned_components(Ref actor, const char* comp_class, std::vector<NamedRef>& out) = 0;
    virtual bool set_anim_door_state(Ref actor, const char* comp_prop, int value) = 0;
    virtual void flush_out() {}
    virtual void note_anom_quiet(double, double, double, double) {}
    virtual int views() { return 1; }
    virtual bool remote_view(const char* tag, std::string& out, int view) { return view == 0 && remote(tag, out); }
    virtual int view_sessions(int view) { return view == 0 ? peer_sessions() : 0; }
    virtual bool view_loopback(int view) { return view == 0 && peer_loopback(); }
    virtual bool quest_obj(Ref) { return false; }
};

struct ViewMerge
{
    std::map<std::string, std::map<int, std::pair<std::string, double>>> m;
    std::vector<std::string> merge(const std::vector<std::vector<std::pair<std::string, std::string>>>& per, double now);
};

struct DestructRow
{
    std::string kind;
    std::string mesh_asset;
    std::string fx;
    std::string au;
    int phases = 1;
};

struct ClickStep
{
    std::string comp;
    std::string ev;
    std::string fn;
    std::string tag;
};

struct ActionRow
{
    std::string rule;
    std::string cc;
    std::string on;
    std::string off;
    std::vector<ClickStep> click;
};

struct AnomalyRow
{
    int cd = 0;
    int h = 0;
    int r = 0;
    std::string n;
};

struct Tables
{
    std::unordered_map<std::string, DestructRow> destruct;
    std::unordered_map<std::string, ActionRow> actions;
    std::unordered_map<std::string, AnomalyRow> anomaly;
    std::unordered_map<std::string, std::string> lockbrk;
    bool have_destruct = false;
    bool have_actions = false;
    bool have_anomaly = false;
    bool have_lockbrk = false;
};

bool load_destruct(Env& env, std::unordered_map<std::string, DestructRow>& out);
bool load_actions(Env& env, std::unordered_map<std::string, ActionRow>& out);
bool load_anomaly(Env& env, std::unordered_map<std::string, AnomalyRow>& out);
bool load_lockbrk(Env& env, std::unordered_map<std::string, std::string>& out);
bool load_tables(Env& env, Tables& t);
std::string dump_tables(const Tables& t);

using UseDeny = std::function<bool(Ref hit, const std::string& key)>;

class World
{
  public:
    explicit World(Env& env);
    bool load();
    void frame();
    void tick100();
    void door_tick();
    void dobj_tick();
    void ien_tick();
    void anom_tick();
    std::string out_o();
    std::string out_z();
    std::string out_p();
    std::string out_qo();
    std::string out_an();
    bool anom_quiet(double x, double y);
    void set_use_deny(UseDeny f) { use_deny_ = std::move(f); }
    std::string status();
    const Tables& tables() const { return tab_; }
#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
    struct CenRow { std::string fam, key; int v = 0; };
    void census(std::vector<CenRow>& out) const;
#endif

  private:
    struct DoorSeen { double y0 = 0; bool open = false; bool chg = false; };
    struct OutEnt { std::string key; bool open = false; double at = 0; };
    struct DoorPend { std::string st; double at = 0; int n = 0; };
    struct LockSeen { bool locked = false; bool chg = false; };
    struct Door
    {
        std::map<std::string, DoorSeen> seen;
        std::vector<OutEnt> out;
        std::unordered_map<std::string, std::string> applied;
        std::map<std::string, DoorPend> pend;
        std::map<std::string, LockSeen> lseen;
        std::vector<OutEnt> lout;
        std::unordered_map<std::string, char> lapp;
        std::map<std::string, DoorPend> lpend;
        std::map<std::string, double> lans;
        double list_at = 0;
        std::optional<int> sess;
        std::optional<std::string> nl;
        std::optional<int64_t> pw;
    };

    struct DCls { std::string k; const DestructRow* e = nullptr; bool w = false; };
    struct DSeen { Ref ref; double x = 0, y = 0; std::string k; bool watched = false; int p = 0; std::string m; double at = 0; };
    struct DMiss { int n = 0; double x = 0, y = 0; std::string k; double since = 0; double last_seen = 0; };
    struct DSig { int p = 0; std::string m; int n = 0; };
    struct DPend { double at = 0; int p = 0; std::string m; std::string k; std::optional<int> ph; };
    struct DOut { std::string key; double at = 0; };
    struct Dobj
    {
        bool init = false;
        std::optional<std::string> wkey;
        std::optional<std::string> level;
        std::unordered_set<std::string> broken;
        std::map<std::string, DSeen> seen;
        std::map<std::string, DMiss> miss;
        std::vector<DOut> out;
        std::unordered_map<std::string, DCls> cls;
        std::unordered_map<std::string, std::string> home;
        std::unordered_map<std::string, DSig> sigs;
        std::unordered_map<std::string, double> quiet;
        std::map<std::string, DPend> pend;
        std::vector<std::string> mine;
        std::string last_z;
        double list_at = 0;
        double poll_at = 0;
        double rot_at = 0;
        int rot = 0;
        int rehid = 0;
        double st_at = 0;
        int st_n = 0;
        double st_ms = 0;
        int st_all = 0, st_near = 0, st_w = 0;
        int poll_n = 0;
        double poll_gap_max = 0;
        double fx_at = 0;
        int fx_n = 0;
        int mesh_null = 0;
    };

    struct IenApp { std::string w; double at = 0; };
    struct IenOut { std::string key; bool en = false; double at = 0; std::string mask; };
    struct IenPm { std::string mask; double at = 0; };
    struct Ien
    {
        std::map<std::string, bool> seen;
        std::vector<IenOut> out;
        std::map<std::string, IenApp> app;
        std::map<std::string, bool> brk;
        std::map<std::string, bool> bseen;
        std::vector<std::pair<std::string, double>> bout;
        std::map<std::string, IenPm> pm;
        std::map<std::string, DoorPend> bpend;
        std::optional<int> sess;
        double list_at = 0;
        std::map<std::string, std::string> qst;
        std::map<std::string, std::string> qapp;
        std::unordered_set<std::string> qmiss;
        double qlist_at = 0;
    };

    struct AnomEnt { std::string key; std::string cls; double x = 0, y = 0, z = 0, r = 0, h = 0, cd = 0; };
    struct AnomOut { std::string e; double until = 0; };
    struct AnomQuiet { double until = 0, x = 0, y = 0, r = 0; };
    struct Anom
    {
        int seq = 0;
        std::vector<AnomOut> out;
        std::unordered_map<std::string, double> seen;
        std::vector<AnomEnt> list;
        double list_at = 0;
        std::unordered_map<std::string, bool> inside;
        std::unordered_map<std::string, double> sent;
        AnomQuiet quiet;
    };

    std::string key_of(const Vec3& q) const;
    bool key_parts(const std::string& k, std::string& p, long& x, long& y, long& z) const;
    bool key_close(const std::string& a, const std::string& b);
    Ref key_find(const std::vector<Ref>& objs, const std::string& k);
    bool door_key(Ref o, std::string& k);
    bool door_leaf_yaw(Ref o, double& yaw);
    bool lock_on();
    std::string brk_event(Ref o);
    void lock_receive(const std::string& k, char st, double now);
    void lock_scan(Ref o, const std::string& k, double now);
    bool ien_break(const std::string& k, double now);

    void dobj_reset();
    std::string dg_name(Ref o);
    std::string dg_mesh(Ref o);
    void dg_sig(Ref o, int& p, std::string& m);
    DCls dg_info(Ref o, const std::string& name);
    std::string dg_replay(Ref o, const DCls& c, std::optional<int> ph, double now);
    std::string dg_apply(Ref o, const std::string& name, const std::string& k, std::optional<int> ph, double now);
    bool dg_find(const std::string& k, Ref& o, std::string& name);
    bool dg_alive(const std::string& name, bool has_pos, double x, double y);
    void dg_report(const std::string& k, double now, const std::string& why);
    std::string dobj_hide(Ref o);
    bool dobj_hidden(Ref o);
    void dobj_receive(double now);
    void dobj_evaluate(std::map<std::string, DSeen>& cur, double now, const Vec3& lpos, double r2);
    void dobj_scan(double now, const Vec3& lpos, double r2);
    void dobj_poll(double now, const Vec3& lpos, double r2);

    bool ien_table();
    std::string ien_apply_act(Ref hit, const std::string& cls, bool want);
    void ien_apply_mask(Ref o, const std::string& mask, const std::string& k);
    std::string ien_mask(Ref o);
    void ien_receive(double now);
    void ien_scan(double now, const Vec3& lpos);
    bool qobj_on();
    bool qobj_read(Ref o, qobj::ObjState& s);
    void qobj_tick(double now);

    bool mc_on();
    bool rem_merge(const char* tag, std::string& out, ViewMerge& vm, bool door);
    bool rem_union(const char* tag, std::string& out);
    int sessions_all();
    void anom_recv(const std::string& rem, const std::string& pre, double now);

    Env& env_;
    Tables tab_;
    bool loaded_ = false;
    bool ien_logged_ = false;
    bool anom_logged_ = false;
    Door door_;
    Dobj dobj_;
    Ien ien_;
    Anom anom_;
    ViewMerge vm_o_, vm_p_;
    UseDeny use_deny_;
};

}
