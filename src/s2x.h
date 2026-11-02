#pragma once
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace RC::Unreal
{
    class UObject;
    class UClass;
    class UFunction;
}

namespace x0
{
    using UObject = RC::Unreal::UObject;
    using UClass = RC::Unreal::UClass;

    enum class Mod : uint8_t { x0 = 0, bodies, damage, npc, items, world, pop, quest, lock, save, travel, session, count };
    const char* mod_name(Mod m);
    bool mod_from_name(std::string_view name, Mod& out);
    bool port(Mod m);

    struct ModReg
    {
        ModReg(Mod m, const char* id, void (*init)());
    };
#define X0_MODULE(mod)                                                                         \
    static void x0_mod_init_##mod();                                                           \
    static ::x0::ModReg x0_mod_reg_##mod(::x0::Mod::mod, #mod, &x0_mod_init_##mod);           \
    static void x0_mod_init_##mod()

    double now();
    int64_t wall_ms();
    bool on_game_thread();
    const std::string& role();
    bool is_host();
    bool is_client();
    const std::string& boot_role();
    bool set_session_role(std::string_view role, std::string_view why);
    using RoleCb = std::function<void(const std::string& before, const std::string& after)>;
    void on_role(Mod m, RoleCb cb);
    const std::wstring& bridge_dir();
    std::wstring bridge_file(std::wstring_view name);

    void log(std::string_view text);
    void logf(const char* fmt, ...);
    void log_every(std::string_view key, double secs, std::string_view text);
    bool log_n(int& counter, int max, std::string_view text);

    struct Val
    {
        enum Kind : uint8_t { nil = 0, i, f, s } k = nil;
        int64_t iv = 0;
        double fv = 0;
        std::string sv;
        double num() const;
        int64_t integer() const;
        std::string str() const;
        bool operator==(const Val& o) const;
        bool operator!=(const Val& o) const { return !(*this == o); }
        static Val I(int64_t v);
        static Val F(double v);
        static Val S(std::string v);
    };
    Val flag(std::string_view key);
    int64_t fi(std::string_view key, int64_t def = 0);
    double fd(std::string_view key, double def = 0);
    std::string fs(std::string_view key, std::string_view def = {});
    bool fon(std::string_view key);
    void fset(std::string_view key, const Val& v, std::string_view why);
    using FlagCb = std::function<void(const std::string& key, const Val& before, const Val& after)>;
    void on_flag(Mod m, std::string_view key, FlagCb cb);
    class Flag
    {
      public:
        explicit Flag(const char* key) : key_(key) {}
        int64_t i() const;
        double d() const;
        std::string s() const;
        bool on() const { return i() == 1; }
        const char* key() const { return key_; }
      private:
        const Val* res() const;
        const char* key_;
        mutable const Val* p_ = nullptr;
    };

    struct Ref
    {
        int32_t idx = -1;
        int32_t serial = 0;
        void* raw = nullptr;
        uint64_t name = 0;
        static Ref of(const UObject* o);
        static Ref addr(uint64_t a);
        UObject* get() const;
        bool valid() const { return get() != nullptr; }
        explicit operator bool() const { return valid(); }
        void reset() { *this = Ref{}; }
        bool same(const Ref& o) const { return idx == o.idx && raw == o.raw && serial == o.serial; }
        uint64_t address() const { return reinterpret_cast<uint64_t>(raw); }
    };

    std::vector<Ref> all_of(const wchar_t* cls, double ttl_s = -1);
    Ref first_of(const wchar_t* cls);
    Ref find_path(const wchar_t* path);
    std::string class_name(const Ref& r);
    std::string full_name(const Ref& r);
    std::string obj_name(const Ref& r);
    bool is_a(const Ref& r, const wchar_t* cls);

    struct Vec3 { double x = 0, y = 0, z = 0; };
    struct Rot { double pitch = 0, yaw = 0, roll = 0; };
    class Call
    {
      public:
        Call(const Ref& self, const wchar_t* fn);
        ~Call();
        Call(const Call&) = delete;
        Call& operator=(const Call&) = delete;
        Call& i(const wchar_t* p, int64_t v);
        Call& f(const wchar_t* p, float v);
        Call& d(const wchar_t* p, double v);
        Call& b(const wchar_t* p, bool v);
        Call& o(const wchar_t* p, const Ref& v);
        Call& n(const wchar_t* p, const wchar_t* fname);
        Call& v(const wchar_t* p, const Vec3& v);
        Call& r(const wchar_t* p, const Rot& v);
        Call& s(const wchar_t* p, const std::wstring& v);
        Call& raw(const wchar_t* p, const void* bytes, int size);
        Call& xf(const wchar_t* p, const Vec3& loc, const Rot& rot, const Vec3& scale = {1, 1, 1});
        bool run();
        bool ok() const { return !bad_; }
        bool rb(const wchar_t* p = L"ReturnValue") const;
        int64_t ri(const wchar_t* p = L"ReturnValue") const;
        float rf(const wchar_t* p = L"ReturnValue") const;
        double rd(const wchar_t* p = L"ReturnValue") const;
        Ref ro(const wchar_t* p = L"ReturnValue") const;
        bool rv(Vec3& out, const wchar_t* p = L"ReturnValue") const;
        bool rr(Rot& out, const wchar_t* p = L"ReturnValue") const;
        std::wstring rs(const wchar_t* p = L"ReturnValue") const;
        std::vector<Ref> rarr(const wchar_t* p = L"ReturnValue") const;
        bool rraw(void* out, int size, const wchar_t* p = L"ReturnValue") const;
        std::string rn(const wchar_t* p = L"ReturnValue") const;
      private:
        void* at(const wchar_t* p, int need, bool* isBool = nullptr) const;
        Ref self_;
        void* fn_ = nullptr;
        bool bad_ = false;
        bool ran_ = false;
        std::vector<void*> strs_;
        alignas(16) uint8_t buf_[0x600];
    };
    bool call0(const Ref& self, const wchar_t* fn);
    bool call_b(const Ref& self, const wchar_t* fn, bool def);
    double call_d(const Ref& self, const wchar_t* fn, double def);
    Ref call_o(const Ref& self, const wchar_t* fn);
    bool call_v(const Ref& self, const wchar_t* fn, Vec3& out);
    Ref world();
    std::string fname_str(uint64_t raw);
    bool trace_line(const Vec3& a, const Vec3& b, bool& hit, Vec3& impact);

    template <class T> bool prop(const Ref& self, const char* path, T& out);
    template <class T> bool prop_set(const Ref& self, const char* path, const T& v);
    int64_t prop_off(const Ref& self, const char* path);
    int64_t field_off(const Ref& st, const char* path);
    int struct_size(const Ref& st);
    int array_stride(const Ref& st, const char* name);
    bool sparse_of(const Ref& cls, uint64_t& data, Ref& st);
    std::vector<std::pair<uint64_t, int64_t>> struct_props(const Ref& cls, const wchar_t* structName);
}
#include "s2x_calls.h"
namespace x0
{
    struct Snap
    {
        bool ok = false;
        int v = -1;
        std::string text;
        std::string head;
        std::string sid, x1raw, shots, need, avmesh, avparts;
        std::string x1;
        double t = -1, x = 0, y = 0, z = 0, vx = 0, vy = 0, yaw = 0, pt = 0, gt = -1, wt = -1;
        int wx = -1;
        bool has_pos = false, has_pt = false;
        uint64_t seq = 0;
        double recv = -1;
        int pid = -1;
    };
    const Snap& remote();
    bool rpart(std::string_view tag, std::string& out);
    struct Peer
    {
        enum State : uint8_t { none = 0, up, silent } state = none;
        std::string id, sid, role, mod, core, diff, nonce;
        bool live = false, loopback = false;
        bool left = false, bye = false, local = false;
        int ups = 0, sessions = 0, lefts = 0;
        double silent_at = 0, left_at = 0;
        int pid = -1;
    };
    const Peer& peer();
    bool coop();
    enum class SaveStore : uint8_t { normal = 0, coop = 1 };
    void save_store_want(SaveStore s);
    SaveStore save_store();
    bool save_store_ready();
    bool save_store_switching();
    int save_store_has_save();
    void save_store_world_exit();
    enum class CoopMode : uint8_t { none = 0, host = 1, join = 2 };
    CoopMode coop_mode();
    void coop_session_reset();

    constexpr int kMaxClients = 4;
    struct RosterEnt
    {
        int pid = 0;
        std::string sid, st;
    };
    struct Roster
    {
        bool on = false;
        bool full = false;
        int you = -1;
        int max = 1;
        uint64_t seq = 0;
        std::vector<RosterEnt> peers;
    };
    int views();
    int max_clients();
    bool hub();
    const Roster& roster();
    const Snap& remote(int view);
    const Peer& peer(int view);
    bool rpart(std::string_view tag, std::string& out, int view);
    int view_pid(int view);
    int view_of_pid(int pid);
    int my_pid();
    void each_view(const std::function<void(int view, const Snap& s, const Peer& p)>& fn);
    const std::string& my_nonce();
    bool in_world(double x, double y, double z);
    bool bridge_on();

    using Producer = std::function<std::string()>;
    bool own_tag(Mod m, std::string_view tag, Producer p);
    using Consumer = std::function<void(std::string_view value, const Snap& s)>;
    void on_tag(Mod m, std::string_view tag, Consumer c);
    void on_snap(Mod m, std::function<void(const Snap& s)> c);
    using ProducerV = std::function<std::string(int view)>;
    bool own_tag_view(Mod m, std::string_view tag, ProducerV p);
    bool own_tag_hub(Mod m, std::string_view tag, ProducerV p);
    using ConsumerV = std::function<void(std::string_view value, const Snap& s, int view)>;
    void on_tag_view(Mod m, std::string_view tag, ConsumerV c);
    void on_snap_view(Mod m, std::function<void(const Snap& s, int view)> c);
    void on_view_session(Mod m, std::function<void(int view, const Peer& p)> c);
    struct BodiesV
    {
        std::function<Ref(int view)> puppet, target;
    };
    BodiesV& bodies_v();
    std::string look_put(const std::string& text);
    bool look_get(const std::string& hash, std::string& text);

    enum class Rate : uint8_t { frame = 0, t100, t1000 };
    namespace ord
    {
        enum : int
        {
#define X0_ORDER(n, v) n = v,
#include "s2x_order.inc"
#undef X0_ORDER
            none = 0x7fffffff
        };
    }
    int every(Mod m, const char* name, Rate r, std::function<void()> fn);
    int every(Mod m, const char* name, Rate r, std::function<void()> fn, int order);
    void after(Mod m, double secs, std::function<void()> fn);
    void post(std::function<void()> fn);

    int key(Mod m, int vk, std::function<void()> on_press, std::initializer_list<int> mods = {}, bool debug = false);
    int key_hold(Mod m, int vk, double secs, std::function<void()> on_hold, std::function<void()> on_release = {});
    bool key_down(int vk);

    int hook(Mod m, const wchar_t* ufunc, bool post_hook, std::function<void(const Ref& self)> cb);

    bool read_text(const std::wstring& path, std::string& out);
    bool read_bin(const std::wstring& path, std::string& out);
    bool write_text(const std::wstring& path, std::string_view text, bool atomic = true);
    bool append_text(const std::wstring& path, std::string_view text);
    bool write_bin(const std::wstring& path, std::string_view bytes, bool atomic = true);
    bool file_exists(const std::wstring& path);
    bool remove_file(const std::wstring& path);
    std::vector<std::string> split(std::string_view s, char sep, bool keep_empty = true);
    struct TsvRow
    {
        std::vector<std::string> key;
        std::string ktype;
        char t = 's';
        std::string v;
    };
    bool tsv_d1(const std::wstring& path, std::vector<TsvRow>& rows, std::string* err = nullptr);
    bool tsv_plain(const std::wstring& path, std::vector<std::vector<std::string>>& rows, bool skip_header = true);
    std::wstring data_file(std::wstring_view name);
    bool data(std::string_view name, std::string& out);
    bool data_tsv(std::string_view name, std::vector<TsvRow>& rows, std::string* err = nullptr);
    bool table_res(std::string_view name, std::string& out);
    bool table(std::string_view name, std::string& out);
    bool release_build();
    bool tsv_d1_text(std::string_view text, std::vector<TsvRow>& rows, std::string* err = nullptr);
    std::vector<std::string> data_names();

    using CmdFn = std::function<std::string(const std::vector<std::string>& args)>;
    void cmd(Mod m, const char* name, CmdFn fn);
    void status(Mod m, std::function<std::string()> line);

    struct Iface
    {
        std::function<Ref()> puppet;
        std::function<Ref()> target;
        std::function<bool(const Ref&)> is_own_body;
        std::function<bool()> body_gate;
        std::function<bool()> story_locked;
        std::function<std::string()> lead_owner;
        std::function<bool(int32_t uid)> is_vendor;
        std::function<bool(std::string_view sid)> is_quest_item;
        std::function<bool(std::string_view sid)> is_quest_npc;
        std::function<bool()> local_downed;
        std::function<std::string()> ft_lock;
        std::function<bool()> nd_dead;
        std::function<bool()> cine_frozen;
        std::function<bool(double x, double y, double z, double yaw)> pull_now;
    };
    Iface& iface();
    bool is_own_body(const Ref& r);
    bool story_locked();

    void screen_text(std::string_view id, std::string_view text);
    struct TextKeep
    {
        alignas(8) uint8_t b[0x30]{};
        int n = 0;
    };
    bool text_keep(const Ref& tb, TextKeep& out);
    bool text_put(const Ref& tb, const TextKeep& k);
    bool text_set(const Ref& tb, std::string_view text);
    void screen_text_clear(std::string_view id);
    void ui_msg(std::string_view id, std::string_view text, double secs = 0);
    void ui_session_end();
    void ui_session_begin();
    bool ui_session_blocked();

    void dll_register_lua(void* lua_state_wrapper);
    void dll_unreal_init();
    void dll_lua_mod_started(const wchar_t* mod_name);
}
