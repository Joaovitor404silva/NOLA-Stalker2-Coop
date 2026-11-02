#include "../../src/s2m_world.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace s2m;

namespace
{

std::vector<std::string> toks(const std::string& line)
{
    std::vector<std::string> t;
    std::istringstream is(line);
    std::string w;
    while (is >> w) t.push_back(w);
    return t;
}

std::string str_num(double v)
{
    char b[64];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

struct Obj
{
    std::string id, cls, kind;
    std::vector<std::string> anc;
    bool has_pos = false;
    Vec3 pos;
    double sc[3] = {1, 1, 1};
    bool dead = false;
    bool asset = false;
    bool listed = false;
    std::map<std::string, bool> pb;
    bool has_phase = false;
    int phase = 0;
    std::map<std::string, int> comps;
    int owner = -1;
    std::string fname;
    double yaw = 0;
    int mesh = -1;
    bool vis = false;
    int anim = -1;
    int doorstate = 1;
    std::map<std::string, std::string> fns;
    long addr = 0;
};

const std::map<std::string, std::set<std::string>>& methods()
{
    static const std::map<std::string, std::set<std::string>> m = {
        {"door", {"ForceOpenDoor", "ForceCloseDoor"}},
        {"inter", {"SetEnabled", "Toggle", "StateChange", "IsEnabled", "SetBroken", "PauseSkeletalComponent"}},
        {"lamp", {"SetEnabled", "Toggle", "StateChange", "IsEnabled", "SetBroken", "PauseSkeletalComponent", "LightOn", "LightOff"}},
        {"destr", {"StartDestructActions", "SetActorHiddenInGame", "SetActorEnableCollision"}},
        {"anom", {"ForceActivateOnce"}},
        {"comp", {"SetInteractionActive"}},
        {"meshcomp", {"SetStaticMesh", "SetSimulatePhysics"}},
    };
    return m;
}

class FakeEnv : public Env
{
  public:
    std::vector<Obj> objs;
    std::map<std::string, double> flags;
    std::map<std::string, std::string> remotes;
    std::vector<std::string> trace;
    std::string datadir;
    double t = 0;
    bool host = true;
    bool has_gate = true;
    std::string gate_key = "k1";
    bool gate_open = true;
    std::string level;
    bool has_level = false;
    int sessions = 1;
    bool loopback = false;
    int nviews = 1;
    std::map<int, std::map<std::string, std::string>> vrem;
    std::map<int, int> vsess;
    std::set<int> vloop;
    int pawn = -1;
    bool pawn_set = true;
    long pawn_id = 0;
    std::set<std::string> deny;
    std::map<std::string, int> assets;
    int flushes = 0;
    int list_calls = 0;
    std::vector<std::string> quiets;

    void rec(const std::string& s) { trace.push_back(s); }

    int add(const std::string& id, const std::string& cls, const std::vector<std::string>& anc, const std::string& kind)
    {
        Obj o;
        o.id = id;
        o.cls = cls;
        o.anc = anc;
        o.kind = kind;
        o.addr = 1001 + (long)objs.size();
        objs.push_back(o);
        return (int)objs.size() - 1;
    }
    int find(const std::string& id) const
    {
        for (size_t i = 0; i < objs.size(); ++i)
            if (objs[i].id == id) return (int)i;
        return -1;
    }
    Obj* get(Ref r)
    {
        if (r.idx < 0 || r.idx >= (int)objs.size()) return nullptr;
        return &objs[(size_t)r.idx];
    }
    Obj* live(Ref r)
    {
        Obj* o = get(r);
        return (o && !o->dead) ? o : nullptr;
    }
    static Ref ref(int i) { Ref r; r.idx = i; r.ser = 1; return r; }

    double now() override { return t; }
    double flag(const char* name, double def) override
    {
        auto it = flags.find(name);
        return it == flags.end() ? def : it->second;
    }
    bool is_host() override { return host; }
    void log(const std::string& line) override { rec("LOG " + line); }
    bool remote(const char* tag, std::string& out) override
    {
        auto it = remotes.find(tag);
        if (it == remotes.end()) return false;
        out = it->second;
        return true;
    }
    bool peer_loopback() override { return loopback; }
    int peer_sessions() override { return sessions; }
    int views() override { return nviews; }
    bool remote_view(const char* tag, std::string& out, int view) override
    {
        if (view == 0) return remote(tag, out);
        auto v = vrem.find(view);
        if (v == vrem.end()) return false;
        auto it = v->second.find(tag);
        if (it == v->second.end()) return false;
        out = it->second;
        return true;
    }
    int view_sessions(int view) override
    {
        if (view == 0) return sessions;
        auto it = vsess.find(view);
        return it == vsess.end() ? 0 : it->second;
    }
    bool view_loopback(int view) override { return view == 0 ? loopback : vloop.count(view) > 0; }
    bool local_pawn(Vec3& pos, int64_t& id) override
    {
        if (!pawn_set || pawn < 0) return false;
        Obj* o = live(ref(pawn));
        if (!o) return false;
        pos = o->pos;
        id = pawn_id;
        return true;
    }
    bool gate(std::string& key, bool& open) override
    {
        if (!has_gate) return false;
        key = gate_key;
        open = gate_open;
        return true;
    }
    bool level_name(std::string& out) override
    {
        if (!has_level) return false;
        out = level;
        return true;
    }
    bool read_data(const char* name, std::string& out) override
    {
        std::ifstream f(datadir + "/" + name, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }
    void list_class(const char* cls, std::vector<Ref>& out) override
    {
        out.clear();
        ++list_calls;
        for (size_t i = 0; i < objs.size(); ++i)
        {
            const Obj& o = objs[i];
            if (!o.listed) continue;
            bool hit = o.cls == cls;
            for (auto& a : o.anc) if (a == cls) hit = true;
            if (hit) out.push_back(ref((int)i));
        }
    }
    bool valid(Ref r) override { return live(r) != nullptr; }
    bool location(Ref r, Vec3& out) override
    {
        Obj* o = live(r);
        if (!o || !o->has_pos) return false;
        out = o->pos;
        return true;
    }
    std::string full_name(Ref r) override
    {
        Obj* o = live(r);
        if (!o) return std::string();
        if (o->asset) return "Asset " + o->id;
        return o->cls + " /Game/Test." + o->id;
    }
    std::string class_name(Ref r) override
    {
        Obj* o = live(r);
        return o ? o->cls : std::string();
    }
    bool class_chain_has(Ref r, const char* cls, int depth) override
    {
        Obj* o = live(r);
        if (!o) return false;
        std::vector<std::string> seq;
        seq.push_back(o->cls);
        for (auto& a : o->anc) seq.push_back(a);
        for (int i = 0; i < depth && i < (int)seq.size(); ++i)
            if (seq[(size_t)i] == cls) return true;
        return false;
    }
    bool prop_bool(Ref r, const char* prop, bool& out) override
    {
        Obj* o = live(r);
        if (!o) return false;
        auto it = o->pb.find(prop);
        if (it == o->pb.end()) return false;
        out = it->second;
        return true;
    }
    bool prop_obj(Ref r, const char* prop, Ref& out) override
    {
        Obj* o = live(r);
        if (!o) return false;
        auto it = o->comps.find(prop);
        if (it == o->comps.end()) return false;
        out = ref(it->second);
        return true;
    }
    bool call_vec(Ref r, const char* fn, Vec3& out) override
    {
        Obj* o = live(r);
        if (!o) return false;
        std::string f = fn;
        if (f == "K2_GetComponentRotation" && o->kind == "leaf")
        {
            out = {0, o->yaw, 0};
            return true;
        }
        if (f == "GetActorScale3D")
        {
            out = {o->sc[0], o->sc[1], o->sc[2]};
            return true;
        }
        return false;
    }
    bool call_bool(Ref r, const char* fn, bool& out) override
    {
        Obj* o = live(r);
        if (!o) return false;
        std::string f = fn;
        if (f == "IsVisible" && o->kind == "light")
        {
            out = o->vis;
            return true;
        }
        if (f == "IsLocked" && o->kind == "door")
        {
            auto it = o->pb.find("bLocked");
            if (it == o->pb.end()) return false;
            out = it->second;
            return true;
        }
        if (!allowed(*o, f)) return false;
        rec("CALL " + o->id + "." + f + "()");
        if (f == "IsEnabled")
        {
            auto it = o->pb.find("bEnabled");
            out = it != o->pb.end() && it->second;
            return true;
        }
        return false;
    }
    bool call_int(Ref r, const char* fn, int& out) override
    {
        Obj* o = live(r);
        if (!o) return false;
        if (std::string(fn) == "GetCurrentPhaseIndex" && o->has_phase)
        {
            out = o->phase;
            return true;
        }
        return false;
    }

    bool allowed(const Obj& o, const std::string& f) const
    {
        auto m = methods().find(o.kind);
        if (m != methods().end() && m->second.count(f)) return true;
        if ((o.kind == "inter" || o.kind == "lamp") && f.rfind("BndEvt__", 0) == 0) return true;
        if (o.kind == "door" && o.pb.count("bLocked") && (f == "LockDoor" || f == "UnlockDoor")) return true;
        return o.fns.count(f) > 0;
    }
    static std::string argtext(FakeEnv& e, const Arg& a)
    {
        if (a.kind == Arg::Bool) return a.b ? "true" : "false";
        if (a.kind == Arg::Obj)
        {
            Obj* o = e.get(a.o);
            return o ? o->id : "?";
        }
        return "nil";
    }
    bool call(Ref r, const char* fn, const std::vector<Arg>& args) override
    {
        Obj* o = live(r);
        if (!o) return false;
        std::string f = fn;
        if (!allowed(*o, f)) return false;
        std::string a;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i) a += ',';
            a += argtext(*this, args[i]);
        }
        rec("CALL " + o->id + "." + f + "(" + a + ")");
        auto fi = o->fns.find(f);
        if (fi != o->fns.end())
        {
            bool cur = o->pb.count("bEnabled") && o->pb["bEnabled"];
            if (fi->second == "toggle") o->pb["bEnabled"] = !cur;
            else if (fi->second == "on") o->pb["bEnabled"] = true;
            else if (fi->second == "off") o->pb["bEnabled"] = false;
            return true;
        }
        bool b0 = !args.empty() && args[0].kind == Arg::Bool && args[0].b;
        if (f == "ForceOpenDoor")
        {
            auto lf = o->comps.find("FirstLeafMeshComponent");
            if (lf != o->comps.end()) objs[(size_t)lf->second].yaw = objs[(size_t)lf->second].sc[0] + 90;
        }
        else if (f == "ForceCloseDoor")
        {
            auto lf = o->comps.find("FirstLeafMeshComponent");
            if (lf != o->comps.end()) objs[(size_t)lf->second].yaw = objs[(size_t)lf->second].sc[0];
        }
        else if (f == "SetEnabled") o->pb["bEnabled"] = b0;
        else if (f == "Toggle") o->pb["bEnabled"] = !(o->pb.count("bEnabled") && o->pb["bEnabled"]);
        else if (f == "LightOn" || f == "LightOff")
        {
            auto ml = o->comps.find("MainLight");
            if (ml != o->comps.end()) objs[(size_t)ml->second].vis = (f == "LightOn");
        }
        else if (f == "SetBroken") o->pb["bBroken"] = b0;
        else if (f == "LockDoor" || f == "UnlockDoor") o->pb["bLocked"] = f == "LockDoor";
        else if (f.rfind("BndEvt__", 0) == 0 && f.find("OnDamageAccumulated") != std::string::npos)
        {
            if (!(o->pb.count("bUnbreakable") && o->pb["bUnbreakable"])) o->pb["bBroken"] = true;
        }
        else if (f == "SetActorHiddenInGame") o->pb["bHidden"] = b0;
        else if (f == "SetInteractionActive") o->pb["bIsInteractionActive"] = b0;
        else if (f == "SetStaticMesh") o->mesh = (!args.empty() && args[0].kind == Arg::Obj) ? args[0].o.idx : -1;
        else if (f.rfind("BndEvt__", 0) == 0 && f.find("InteractSignature") != std::string::npos)
            o->pb["bEnabled"] = !(o->pb.count("bEnabled") && o->pb["bEnabled"]);
        return true;
    }
    MeshRead mesh_name(Ref actor, std::string& out) override
    {
        Obj* a = live(actor);
        if (!a) return MeshRead::NoActor;
        auto it = a->comps.find("StaticMeshComponent");
        if (it == a->comps.end()) return MeshRead::NoComponent;
        Obj& mc = objs[(size_t)it->second];
        if (mc.mesh < 0) return MeshRead::NullMesh;
        out = objs[(size_t)mc.mesh].fname;
        return MeshRead::Ok;
    }
    Ref asset(const char* path) override
    {
        std::string p = path;
        if (p.rfind("/Game/", 0) != 0) return Ref{};
        auto it = assets.find(p);
        if (it == assets.end())
        {
            int i = add(p, "Asset", {}, "other");
            objs[(size_t)i].asset = true;
            assets[p] = i;
            return ref(i);
        }
        return ref(it->second);
    }
    bool spawn_fx(Ref at, Ref niagara) override
    {
        Obj* n = get(niagara);
        if (!live(at) || !n) return false;
        rec("CALL Default__NiagaraFunctionLibrary.SpawnSystemAtLocation(" + n->id + ")");
        return true;
    }
    bool play_sound(Ref at, Ref event) override
    {
        Obj* e = get(event);
        if (!live(at) || !e) return false;
        rec("CALL Default__AkGameplayStatics.PostEventAtLocation(" + e->id + ")");
        return true;
    }
    void owned_components(Ref actor, const char*, std::vector<NamedRef>& out) override
    {
        out.clear();
        Obj* a = live(actor);
        if (!a) return;
        for (size_t i = 0; i < objs.size(); ++i)
        {
            const Obj& c = objs[i];
            if (c.kind != "comp" || c.dead || c.owner != actor.idx) continue;
            out.push_back(NamedRef{c.fname, ref((int)i)});
        }
    }
    bool set_anim_door_state(Ref actor, const char* comp_prop, int value) override
    {
        Obj* a = live(actor);
        if (!a) return false;
        auto it = a->comps.find(comp_prop);
        if (it == a->comps.end()) return false;
        Obj& c = objs[(size_t)it->second];
        if (c.anim < 0) return false;
        Obj& an = objs[(size_t)c.anim];
        an.doorstate = value;
        rec("ANIM DoorState=" + std::to_string(value));
        return true;
    }
    void flush_out() override { ++flushes; }
    void note_anom_quiet(double x, double y, double r, double until) override
    {
        quiets.push_back(str_num(x) + "," + str_num(y) + "," + str_num(r) + "," + str_num(until));
    }
};

std::string sorted_tag(const std::string& s)
{
    if (s.empty()) return std::string();
    size_t c = s.find(':');
    std::string tag = s.substr(0, c + 1);
    std::string body = s.substr(c + 1);
    std::vector<std::string> items;
    size_t i = 0;
    while (i <= body.size())
    {
        size_t j = body.find('|', i);
        if (j == std::string::npos) j = body.size();
        if (j > i) items.push_back(body.substr(i, j - i));
        i = j + 1;
    }
    std::sort(items.begin(), items.end());
    std::string r = tag;
    for (size_t k = 0; k < items.size(); ++k)
    {
        if (k) r += '|';
        r += items[k];
    }
    return r;
}

struct Runner
{
    FakeEnv env;
    World* w = nullptr;
    int ticks = 0;
    int fails = 0;
    size_t tick_from = 0;
    bool use_deny_set = false;

    void tick()
    {
        tick_from = env.trace.size();
        env.rec("TICK " + std::to_string(++ticks));
        w->frame();
        w->tick100();
        std::string o[4] = {w->out_o(), w->out_z(), w->out_p(), w->out_an()};
        for (auto& s : o)
        {
            std::string n = sorted_tag(s);
            if (!n.empty()) env.rec("OUT " + n);
        }
    }

    bool any_since(const std::string& sub) const
    {
        for (size_t i = tick_from; i < env.trace.size(); ++i)
            if (env.trace[i].find(sub) != std::string::npos) return true;
        return false;
    }

    void exec(const std::string& line)
    {
        auto t = toks(line);
        if (t.empty() || t[0][0] == '#') return;
        const std::string& c = t[0];
        FakeEnv& e = env;
        auto num = [&](size_t i) { return std::atof(t[i].c_str()); };
        auto idx = [&](size_t i) { return e.find(t[i]); };
        if (c == "role") e.host = t[1] == "host";
        else if (c == "flag") e.flags[t[1]] = num(2);
        else if (c == "gate") { e.has_gate = true; e.gate_key = t[1]; e.gate_open = t[2] == "1"; }
        else if (c == "nogate") e.has_gate = false;
        else if (c == "level") { e.has_level = t[1] != "-"; e.level = t[1]; }
        else if (c == "sessions") e.sessions = (int)num(1);
        else if (c == "loopback") e.loopback = t[1] == "1";
        else if (c == "views") e.nviews = (int)num(1);
        else if (c == "vremote")
        {
            int v = (int)num(1);
            if (v == 0) { if (t[3] == "-") e.remotes.erase(t[2]); else e.remotes[t[2]] = t[3]; }
            else if (t[3] == "-") e.vrem[v].erase(t[2]);
            else e.vrem[v][t[2]] = t[3];
        }
        else if (c == "vsessions") { if ((int)num(1) == 0) e.sessions = (int)num(2); else e.vsess[(int)num(1)] = (int)num(2); }
        else if (c == "vloopback") { if (t[2] == "1") e.vloop.insert((int)num(1)); else e.vloop.erase((int)num(1)); }
        else if (c == "time") e.t = num(1);
        else if (c == "remote")
        {
            if (t[2] == "-") e.remotes.erase(t[1]);
            else e.remotes[t[1]] = t[2];
        }
        else if (c == "pawn")
        {
            int i = e.find("PAWN");
            if (i < 0) { i = e.add("PAWN", "Pawn", {}, "other"); e.pawn_id = e.objs[(size_t)i].addr; }
            e.objs[(size_t)i].has_pos = true;
            e.objs[(size_t)i].pos = {num(1), num(2), num(3)};
            e.pawn = i;
            e.pawn_set = true;
        }
        else if (c == "pawnid") e.pawn_id = (long)num(1);
        else if (c == "nopawn") e.pawn_set = false;
        else if (c == "obj")
        {
            std::vector<std::string> anc;
            if (t[3] != "-")
            {
                std::string a = t[3];
                size_t i = 0;
                while (i < a.size())
                {
                    size_t j = a.find(',', i);
                    if (j == std::string::npos) j = a.size();
                    anc.push_back(a.substr(i, j - i));
                    i = j + 1;
                }
            }
            int i = e.add(t[1], t[2], anc, t[4]);
            Obj& o = e.objs[(size_t)i];
            if (t[5] != "-") { o.has_pos = true; o.pos = {num(5), num(6), num(7)}; }
            const std::string& k = t[4];
            if (k != "comp" && k != "leaf" && k != "mesh" && k != "light" && k != "meshcomp" && k != "cdo") o.listed = true;
        }
        else if (c == "scale") { Obj& o = e.objs[(size_t)idx(1)]; o.sc[0] = num(2); o.sc[1] = num(3); o.sc[2] = num(4); }
        else if (c == "yaw")
        {
            Obj& o = e.objs[(size_t)idx(1)];
            auto it = o.comps.find("FirstLeafMeshComponent");
            if (it == o.comps.end())
            {
                int li = e.add(t[1] + "_leaf", "Leaf", {}, "leaf");
                e.objs[(size_t)li].sc[0] = num(2);
                e.objs[(size_t)idx(1)].comps["FirstLeafMeshComponent"] = li;
                it = e.objs[(size_t)idx(1)].comps.find("FirstLeafMeshComponent");
            }
            e.objs[(size_t)it->second].yaw = num(2);
        }
        else if (c == "prop")
        {
            Obj& o = e.objs[(size_t)idx(1)];
            if (t[3] == "1") o.pb[t[2]] = true;
            else if (t[3] == "0") o.pb[t[2]] = false;
        }
        else if (c == "phase") { Obj& o = e.objs[(size_t)idx(1)]; o.has_phase = true; o.phase = (int)num(2); }
        else if (c == "mesh")
        {
            int oi = idx(1);
            if (t[2] == "none") e.objs[(size_t)oi].comps.erase("StaticMeshComponent");
            else
            {
                auto it = e.objs[(size_t)oi].comps.find("StaticMeshComponent");
                int mc;
                if (it == e.objs[(size_t)oi].comps.end())
                {
                    mc = e.add(t[1] + "/StaticMeshComponent", "StaticMeshComponent", {}, "meshcomp");
                    e.objs[(size_t)oi].comps["StaticMeshComponent"] = mc;
                }
                else mc = it->second;
                if (t[2] == "null") e.objs[(size_t)mc].mesh = -1;
                else
                {
                    int m = e.add("mesh_" + t[2], "StaticMesh", {}, "mesh");
                    e.objs[(size_t)m].fname = t[2];
                    e.objs[(size_t)mc].mesh = m;
                }
            }
        }
        else if (c == "comp")
        {
            int own = idx(2);
            int ci = e.add(t[1], "InteractionComponent", {}, "comp");
            Obj& co = e.objs[(size_t)ci];
            co.owner = own;
            co.fname = t[3];
            co.pb["bIsInteractionActive"] = t[4] == "1";
            co.listed = true;
        }
        else if (c == "compprop") e.objs[(size_t)idx(1)].comps[t[2]] = idx(3);
        else if (c == "mainlight")
        {
            int li = e.add(t[1] + "/MainLight", "Light", {}, "light");
            e.objs[(size_t)li].vis = t[2] == "1";
            e.objs[(size_t)idx(1)].comps["MainLight"] = li;
        }
        else if (c == "anim")
        {
            int ai = e.add(t[1] + "/Anim", "Anim", {}, "anim");
            int hi = e.add(t[1] + "/" + t[2], "AnimHost", {}, "animhost");
            e.objs[(size_t)hi].anim = ai;
            e.objs[(size_t)idx(1)].comps[t[2]] = hi;
        }
        else if (c == "fn") e.objs[(size_t)idx(1)].fns[t[2]] = t[3];
        else if (c == "move") { Obj& o = e.objs[(size_t)idx(1)]; o.pos = {num(2), num(3), num(4)}; }
        else if (c == "setyaw") e.objs[(size_t)e.objs[(size_t)idx(1)].comps["FirstLeafMeshComponent"]].yaw = num(2);
        else if (c == "destroy") { Obj& o = e.objs[(size_t)idx(1)]; o.dead = true; o.listed = false; }
        else if (c == "kill") e.objs[(size_t)idx(1)].dead = true;
        else if (c == "deny") e.deny.insert(t[1]);
        else if (c == "tick") tick();
        else if (c == "loop")
        {
            int n = (int)num(1);
            double dt = num(2);
            for (int i = 0; i < n; ++i) { e.t += dt; tick(); }
        }
        else if (c == "mark") e.rec("MARK " + t[1]);
        else if (c == "expect_new")
        {
            std::string sub = line.substr(line.find(t[1]));
            if (!any_since(sub)) { ++fails; std::printf("EXPECT FAIL (missing in last tick): %s\n", sub.c_str()); }
        }
        else if (c == "expect_none")
        {
            std::string sub = line.substr(line.find(t[1]));
            if (any_since(sub)) { ++fails; std::printf("EXPECT FAIL (present in last tick): %s\n", sub.c_str()); }
        }
        else if (c == "expect_flushes")
        {
            if (e.flushes != (int)num(1)) { ++fails; std::printf("EXPECT FAIL flushes=%d want %d\n", e.flushes, (int)num(1)); }
        }
        else if (c == "expect_lists")
        {
            if (e.list_calls != (int)num(1)) { ++fails; std::printf("EXPECT FAIL list_class calls=%d want %d\n", e.list_calls, (int)num(1)); }
        }
        else if (c == "reset_lists") e.list_calls = 0;
        else if (c == "expect_status")
        {
            std::string sub = line.substr(line.find(t[1]));
            if (w->status().find(sub) == std::string::npos) { ++fails; std::printf("EXPECT FAIL status %s: %s\n", w->status().c_str(), sub.c_str()); }
        }
        else
        {
            std::printf("BAD SCENARIO LINE: %s\n", line.c_str());
            ++fails;
        }
    }
};

bool load_flag_file(FakeEnv& e, const std::string& path)
{
    std::ifstream f(path);
    if (!f) return false;
    std::string name;
    double v;
    while (f >> name >> v) e.flags[name] = v;
    return true;
}

}

int main(int argc, char** argv)
{
    if (argc >= 3 && std::string(argv[1]) == "dump")
    {
        FakeEnv env;
        env.datadir = argv[2];
        Tables t;
        if (!load_tables(env, t)) { std::printf("load_tables failed\n"); return 2; }
        std::printf("%s", dump_tables(t).c_str());
        return 0;
    }
    if (argc < 5 || std::string(argv[1]) != "run")
    {
        std::printf("usage: test_world run <scenario> <flags.txt> <datadir> | dump <datadir>\n");
        return 2;
    }
    Runner r;
    r.env.datadir = argv[4];
    if (!load_flag_file(r.env, argv[3])) { std::printf("flags file missing\n"); return 2; }
    World w(r.env);
    r.w = &w;
    w.set_use_deny([&r](Ref hit, const std::string&) {
        Obj* o = r.env.get(hit);
        return o && r.env.deny.count(o->id) > 0;
    });
    std::ifstream f(argv[2]);
    if (!f) { std::printf("scenario missing\n"); return 2; }
    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        r.exec(line);
    }
    for (auto& s : r.env.trace) std::printf("%s\n", s.c_str());
    for (auto& q : r.env.quiets) std::printf("QUIET %s\n", q.c_str());
    std::printf("END fails=%d flushes=%d\n", r.fails, r.env.flushes);
    return r.fails ? 1 : 0;
}
