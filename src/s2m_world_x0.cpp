#include "s2x.h"
#include "s2m_world.h"
#include "s2m_bodies.h"
#include "s2m_lock.h"
#include "s2m_qobj.h"

#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
using namespace s2m;

std::wstring W(const std::string& s)
{
    std::wstring r;
    r.reserve(s.size());
    for (unsigned char c : s) r += (wchar_t)c;
    return r;
}

struct ParamSpec
{
    const char* fn;
    const wchar_t* name;
    char kind;
};

const ParamSpec kParams[] = {
    {"SetEnabled", L"bInEnabled", 'b'},
    {"SetBroken", L"bInBroken", 'b'},
    {"PauseSkeletalComponent", L"bPause", 'b'},
    {"SetInteractionActive", L"bActive", 'b'},
    {"SetActorHiddenInGame", L"bNewHidden", 'b'},
    {"SetActorEnableCollision", L"bNewActorEnableCollision", 'b'},
    {"SetSimulatePhysics", L"bSimulate", 'b'},
    {"SetStaticMesh", L"NewMesh", 'o'},
};

class XEnv : public Env
{
  public:
    struct Slot
    {
        x0::Ref r;
        int32_t gen = 1;
        bool used = false;
    };
    std::vector<Slot> slots_;
    std::vector<int32_t> free_;
    std::map<std::tuple<int32_t, void*, int32_t>, int32_t> index_;
    double prune_at_ = 0;

    Ref to_s(const x0::Ref& r)
    {
        if (r.idx < 0 || !r.raw) return Ref{};
        auto k = std::make_tuple(r.idx, r.raw, r.serial);
        auto it = index_.find(k);
        if (it != index_.end() && slots_[it->second].used) return Ref{it->second, slots_[it->second].gen};
        int32_t at;
        if (!free_.empty()) { at = free_.back(); free_.pop_back(); }
        else { at = (int32_t)slots_.size(); slots_.emplace_back(); }
        Slot& s = slots_[at];
        s.r = r;
        s.used = true;
        index_[k] = at;
        return Ref{at, s.gen};
    }
    x0::Ref to_x(Ref r) const
    {
        if (r.idx < 0 || r.idx >= (int32_t)slots_.size()) return x0::Ref{};
        const Slot& s = slots_[r.idx];
        if (!s.used || s.gen != r.ser) return x0::Ref{};
        return s.r;
    }
    void prune()
    {
        for (size_t i = 0; i < slots_.size(); ++i)
        {
            Slot& s = slots_[i];
            if (!s.used || s.r.valid()) continue;
            index_.erase(std::make_tuple(s.r.idx, s.r.raw, s.r.serial));
            s.r.reset();
            s.used = false;
            ++s.gen;
            free_.push_back((int32_t)i);
        }
    }

    double now() override { return x0::now(); }
    double flag(const char* name, double def) override
    {
        x0::Val v = x0::flag(name);
        return v.k == x0::Val::i || v.k == x0::Val::f ? v.num() : def;
    }
    bool is_host() override { return x0::is_host(); }
    void log(const std::string& line) override { x0::log(line); }
    bool remote(const char* tag, std::string& out) override { return x0::rpart(tag, out); }
    bool peer_loopback() override { return x0::peer().loopback; }
    int peer_sessions() override { return x0::peer().sessions; }
    int views() override { return x0::views(); }
    bool remote_view(const char* tag, std::string& out, int view) override { return x0::rpart(tag, out, view); }
    int view_sessions(int view) override { return x0::peer(view).sessions; }
    bool view_loopback(int view) override { return x0::peer(view).loopback; }

    bool local_pawn(Vec3& pos, int64_t& id) override
    {
        x0::Ref pc = x0::first_of(L"PlayerController");
        x0::Ref pw;
        if (!pc.valid() || !x0::prop<x0::Ref>(pc, "Pawn", pw) || !pw.valid()) return false;
        x0::Vec3 v;
        if (!x0::call_v(pw, L"K2_GetActorLocation", v)) return false;
        pos = {v.x, v.y, v.z};
        id = (int64_t)pw.address();
        return true;
    }
    bool gate(std::string& key, bool& open) override
    {
        x0::Ref pw = x0::first_of(L"BP_Stalker2Character_C");
        if (!pw.valid()) return false;
        key = std::to_string(pw.address()) + "|" + x0::full_name(pw);
        const auto& f = x0::iface().body_gate;
        open = f ? f() : true;
        return true;
    }
    bool level_name(std::string& out) override
    {
        x0::Ref w = x0::first_of(L"World");
        if (!w.valid()) return false;
        out = x0::full_name(w);
        return !out.empty();
    }
    bool read_data(const char* name, std::string& out) override
    {
        if (x0::data(name, out)) return true;
        return x0::read_text(x0::bridge_file(W(name)), out);
    }

    void list_class(const char* cls, std::vector<Ref>& out) override
    {
        out.clear();
        for (const x0::Ref& r : x0::all_of(W(cls).c_str(), 0.1))
        {
            Ref s = to_s(r);
            if (s) out.push_back(s);
        }
    }
    bool valid(Ref r) override { return to_x(r).valid(); }
    bool location(Ref r, Vec3& out) override
    {
        x0::Vec3 v;
        if (!x0::call_v(to_x(r), L"K2_GetActorLocation", v)) return false;
        out = {v.x, v.y, v.z};
        return true;
    }
    std::string full_name(Ref r) override
    {
        x0::Ref x = to_x(r);
        return x.valid() ? x0::full_name(x) : std::string();
    }
    std::string class_name(Ref r) override
    {
        x0::Ref x = to_x(r);
        return x.valid() ? x0::class_name(x) : std::string();
    }
    bool class_chain_has(Ref r, const char* cls, int) override
    {
        x0::Ref x = to_x(r);
        return x.valid() && x0::is_a(x, W(cls).c_str());
    }
    bool prop_bool(Ref r, const char* prop, bool& out) override
    {
        x0::Ref x = to_x(r);
        return x.valid() && x0::prop<bool>(x, prop, out);
    }
    bool prop_obj(Ref r, const char* prop, Ref& out) override
    {
        x0::Ref x = to_x(r);
        x0::Ref o;
        if (!x.valid() || !x0::prop<x0::Ref>(x, prop, o) || !o.valid()) return false;
        out = to_s(o);
        return (bool)out;
    }
    bool call_vec(Ref r, const char* fn, Vec3& out) override
    {
        x0::Ref x = to_x(r);
        if (!x.valid()) return false;
        std::wstring f = W(fn);
        x0::Call c(x, f.c_str());
        if (!c.run()) return false;
        if (std::string(fn) == "K2_GetComponentRotation")
        {
            x0::Rot rot;
            if (!c.rr(rot)) return false;
            out = {rot.pitch, rot.yaw, rot.roll};
            return true;
        }
        x0::Vec3 v;
        if (!c.rv(v)) return false;
        out = {v.x, v.y, v.z};
        return true;
    }
    bool call_bool(Ref r, const char* fn, bool& out) override
    {
        x0::Ref x = to_x(r);
        if (!x.valid()) return false;
        std::wstring f = W(fn);
        x0::Call c(x, f.c_str());
        if (!c.run()) return false;
        out = c.rb();
        return true;
    }
    bool call_int(Ref r, const char* fn, int& out) override
    {
        x0::Ref x = to_x(r);
        if (!x.valid()) return false;
        std::wstring f = W(fn);
        x0::Call c(x, f.c_str());
        if (!c.run()) return false;
        out = (int)c.ri();
        return true;
    }
    bool call(Ref r, const char* fn, const std::vector<Arg>& args) override
    {
        x0::Ref x = to_x(r);
        if (!x.valid()) return false;
        std::wstring f = W(fn);
        x0::Call c(x, f.c_str());
        if (!args.empty())
        {
            const ParamSpec* spec = nullptr;
            for (const ParamSpec& p : kParams)
                if (std::string(p.fn) == fn) { spec = &p; break; }
            if (!spec || args.size() != 1) return false;
            const Arg& a = args[0];
            if (spec->kind == 'b' && a.kind == Arg::Bool) c.b(spec->name, a.b);
            else if (spec->kind == 'o' && a.kind == Arg::Obj) c.o(spec->name, to_x(a.o));
            else return false;
        }
        return c.run();
    }
    MeshRead mesh_name(Ref actor, std::string& out) override
    {
        x0::Ref a = to_x(actor);
        if (!a.valid()) return MeshRead::NoActor;
        x0::Ref comp;
        if (!x0::prop<x0::Ref>(a, "StaticMeshComponent", comp) || !comp.valid()) return MeshRead::NoComponent;
        x0::Ref mesh;
        if (!x0::prop<x0::Ref>(comp, "StaticMesh", mesh) || !mesh.valid()) return MeshRead::NullMesh;
        out = x0::obj_name(mesh);
        return out.empty() ? MeshRead::NullMesh : MeshRead::Ok;
    }
    Ref asset(const char* path) override
    {
        x0::Ref a = x0::find_path(W(path).c_str());
        return a.valid() ? to_s(a) : Ref{};
    }
    bool spawn_fx(Ref at, Ref niagara) override
    {
        x0::Ref a = to_x(at), ns = to_x(niagara);
        x0::Ref lib = x0::find_path(L"/Script/Niagara.Default__NiagaraFunctionLibrary");
        if (!a.valid() || !ns.valid() || !lib.valid()) return false;
        x0::Vec3 loc;
        x0::Rot rot;
        if (!x0::call_v(a, L"K2_GetActorLocation", loc)) return false;
        x0::Call rc(a, L"K2_GetActorRotation");
        if (!rc.run() || !rc.rr(rot)) return false;
        x0::Call c(lib, L"SpawnSystemAtLocation");
        c.o(L"WorldContextObject", a).o(L"SystemTemplate", ns).v(L"Location", loc).r(L"Rotation", rot);
        c.v(L"Scale", x0::Vec3{1, 1, 1}).b(L"bAutoDestroy", true).b(L"bAutoActivate", true).i(L"PoolingMethod", 0).b(L"bPreCullCheck", true);
        return c.run();
    }
    bool play_sound(Ref at, Ref event) override
    {
        x0::Ref a = to_x(at), ev = to_x(event);
        x0::Ref ak = x0::find_path(L"/Script/AkAudio.Default__AkGameplayStatics");
        if (!a.valid() || !ev.valid() || !ak.valid()) return false;
        x0::Vec3 loc;
        x0::Rot rot;
        if (!x0::call_v(a, L"K2_GetActorLocation", loc)) return false;
        x0::Call rc(a, L"K2_GetActorRotation");
        if (!rc.run() || !rc.rr(rot)) return false;
        x0::Call c(ak, L"PostEventAtLocation");
        c.o(L"AkEvent", ev).v(L"Location", loc).r(L"Orientation", rot).o(L"WorldContextObject", a);
        return c.run();
    }
    void owned_components(Ref actor, const char* comp_class, std::vector<NamedRef>& out) override
    {
        out.clear();
        x0::Ref a = to_x(actor);
        if (!a.valid()) return;
        for (const x0::Ref& c : x0::all_of(W(comp_class).c_str(), 0.5))
        {
            x0::Ref own = x0::call_o(c, L"GetOwner");
            if (!own.valid() || !own.same(a)) continue;
            NamedRef n;
            n.name = x0::obj_name(c);
            n.ref = to_s(c);
            out.push_back(n);
        }
    }
    bool set_anim_door_state(Ref actor, const char* comp_prop, int value) override
    {
        x0::Ref a = to_x(actor);
        x0::Ref comp;
        if (!a.valid() || !x0::prop<x0::Ref>(a, comp_prop, comp) || !comp.valid()) return false;
        x0::Ref ai = x0::call_o(comp, L"GetAnimInstance");
        if (!ai.valid()) return false;
        uint8_t u8;
        if (x0::prop<uint8_t>(ai, "DoorState", u8)) return x0::prop_set<uint8_t>(ai, "DoorState", (uint8_t)value);
        return false;
    }
    void note_anom_quiet(double x, double y, double r, double until) override { m01::note_anom_quiet(x, y, r, until); }
    bool quest_obj(Ref r) override
    {
        x0::Ref x = to_x(r);
        return x.valid() && s2m_qobj_is_objective(x);
    }
};

XEnv g_env;
std::unique_ptr<World> g_world;
}

#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
namespace s2m_world_cen
{
    void census(std::vector<s2m::World::CenRow>& out)
    {
        if (g_world) g_world->census(out);
    }
}
#endif

X0_MODULE(world)
{
    g_world = std::make_unique<World>(g_env);
    World* w = g_world.get();
    w->set_use_deny([](Ref hit, const std::string& key) {
        x0::Ref x = g_env.to_x(hit);
        return x.valid() && s2m_lock::use_deny(x, key);
    });
    x0::every(x0::Mod::world, "worldFrame", x0::Rate::frame, [w] { if (x0::port(x0::Mod::world)) w->frame(); });
    x0::every(x0::Mod::world, "worldT100", x0::Rate::t100, [w] { if (x0::port(x0::Mod::world)) w->tick100(); });
    x0::every(x0::Mod::world, "worldPrune", x0::Rate::t1000, [] { g_env.prune(); });
    x0::own_tag(x0::Mod::world, "O", [w] { return w->out_o(); });
    x0::own_tag(x0::Mod::world, "Z", [w] { return w->out_z(); });
    x0::own_tag(x0::Mod::world, "P", [w] { return w->out_p(); });
    x0::own_tag(x0::Mod::world, "QO", [w] { return w->out_qo(); });
    x0::own_tag(x0::Mod::world, "AN", [w] { return w->out_an(); });
    x0::status(x0::Mod::world, [w] { return w->status(); });
}
