#include "s2m_pop.h"
#include "s2x.h"
#include "s2m_bodies.h"

#include <cstring>
#include <map>
#include <optional>
#include <tuple>
#include <unordered_map>

extern "C" {
bool s2m_pop_false_addr(uint64_t) { return false; }
bool s2m_pop_false_uid(int32_t) { return false; }
bool s2m_pop_false_story(uint64_t, char*, int) { return false; }
bool s2m_pop_false_trace(double, double, double, double, double, double, bool*, double*) { return false; }
bool s2n_nat_is_own(uint64_t);
bool s2n_nat_is_listed_addr(uint64_t);
bool s2n_nat_is_listed_uid(int32_t);
bool s2m_pop_false_park(uint64_t, long long) { return false; }
bool s2m_pop_false_inert(uint64_t, long long, int) { return false; }
bool s2n_nat_inert(uint64_t, long long, int);
bool s2n_nat_park_twin(uint64_t, long long);
bool s2m_story_npc(uint64_t, char*, int);
bool s2m_trace_line(double, double, double, double, double, double, bool*, double*);
}
#pragma comment(linker, "/alternatename:s2n_nat_is_own=s2m_pop_false_addr")
#pragma comment(linker, "/alternatename:s2n_nat_is_listed_addr=s2m_pop_false_addr")
#pragma comment(linker, "/alternatename:s2n_nat_is_listed_uid=s2m_pop_false_uid")
#pragma comment(linker, "/alternatename:s2n_nat_park_twin=s2m_pop_false_park")
#pragma comment(linker, "/alternatename:s2n_nat_inert=s2m_pop_false_inert")
#pragma comment(linker, "/alternatename:s2m_story_npc=s2m_pop_false_story")
#pragma comment(linker, "/alternatename:s2m_trace_line=s2m_pop_false_trace")

namespace s2m_pop {
namespace {

std::wstring widen(const char* s)
{
    std::wstring w;
    while (*s) w += (wchar_t)(unsigned char)*s++;
    return w;
}

class XEnv : public Env {
public:
    std::map<std::tuple<int32_t, void*, uint64_t>, Id> ids;
    std::unordered_map<Id, x0::Ref> reg;
    Id next = 1;

    void beginTick()
    {
        if (ids.size() > 40000) { ids.clear(); reg.clear(); }
    }

    Id idOf(const x0::Ref& r)
    {
        auto k = std::make_tuple(r.idx, r.raw, r.name);
        auto it = ids.find(k);
        Id id;
        if (it == ids.end()) { id = next++; ids[k] = id; }
        else id = it->second;
        reg[id] = r;
        return id;
    }

    x0::Ref ref(Id id)
    {
        auto it = reg.find(id);
        if (it == reg.end() || !it->second.valid()) return x0::Ref();
        return it->second;
    }

    x0::Ref mediator() { return x0::find_path(L"/Script/Stalker2.Default__CppMediator"); }

    double now() override { return x0::now(); }
    double flag(const char* name, double def) override
    {
        x0::Val v = x0::flag(name);
        return v.k == x0::Val::nil ? def : v.num();
    }
    bool isHost() override { return x0::is_host(); }
    bool coop() override { return x0::coop(); }
    void log(const std::string& l) override { x0::log(l); }
    bool readFile(const char* name, std::string& out) override
    {
        if (x0::data(name, out)) return true;
        return x0::read_text(x0::bridge_file(widen(name)), out);
    }

    bool localPawn(Id& id, V3& at) override
    {
        x0::Ref pc = x0::first_of(L"PlayerController");
        x0::Ref pw;
        if (!pc.valid() || !x0::prop<x0::Ref>(pc, "Pawn", pw) || !pw.valid()) return false;
        x0::Vec3 v;
        if (!x0::call_v(pw, L"K2_GetActorLocation", v)) return false;
        id = idOf(pw);
        at = {v.x, v.y, v.z};
        return true;
    }
    void pawns(PawnClass c, std::vector<Id>& out) override
    {
        for (const x0::Ref& r : x0::all_of(c == PawnClass::Humans ? L"BP_AI_Human_C" : L"Pawn")) out.push_back(idOf(r));
    }
    bool valid(Id p) override { return ref(p).valid(); }
    bool alive(Id p) override
    {
        x0::Ref r = ref(p);
        return r.valid() && x0::call_b(r, L"IsAlive", true);
    }
    bool loc(Id p, V3& at) override
    {
        x0::Ref r = ref(p);
        x0::Vec3 v;
        if (!r.valid() || !x0::call_v(r, L"K2_GetActorLocation", v)) return false;
        at = {v.x, v.y, v.z};
        return true;
    }
    bool hiddenFlag(Id p) override
    {
        bool b = false;
        x0::Ref r = ref(p);
        return r.valid() && x0::prop<bool>(r, "bHidden", b) && b;
    }
    bool maxHp(Id p, double& hp) override
    {
        x0::Ref r = ref(p);
        if (!r.valid()) return false;
        x0::Call c(r, L"GetMaxHP");
        if (!c.run()) return false;
        hp = c.rf();
        return true;
    }
    std::string className(Id p) override { return x0::class_name(ref(p)); }
    std::string shortName(Id p) override { return x0::obj_name(ref(p)); }
    std::string meshName(Id p, const char* comp) override
    {
        x0::Ref r = ref(p), m;
        if (!r.valid()) return std::string();
        std::string path = std::string(comp) + (std::strcmp(comp, "Mesh") == 0 ? ".SkeletalMesh" : ".StaticMesh");
        if (!x0::prop<x0::Ref>(r, path.c_str(), m) || !m.valid()) return std::string();
        return x0::obj_name(m);
    }

    bool ownBodyBase(Id p) override
    {
        x0::Ref r = ref(p);
        return r.valid() && m01::is_own_body(r);
    }
    bool natOwn(Id p) override
    {
        x0::Ref r = ref(p);
        return r.valid() && s2n_nat_is_own(r.address());
    }
    bool natListed(Id p) override
    {
        x0::Ref r = ref(p);
        return r.valid() && s2n_nat_is_listed_addr(r.address());
    }
    bool natListedUid(long long uid) override { return s2n_nat_is_listed_uid((int32_t)uid); }
    bool natPark(Id p, long long uid) override
    {
        x0::Ref r = ref(p);
        return r.valid() && s2n_nat_park_twin(r.address(), uid);
    }
    bool inert(Id p, long long uid, bool on) override
    {
        if (!on) {
            auto it = reg.find(p);
            return it != reg.end() && it->second.raw && s2n_nat_inert(reinterpret_cast<uint64_t>(it->second.raw), 0, 0);
        }
        x0::Ref r = ref(p);
        return r.valid() && s2n_nat_inert(r.address(), uid, 1);
    }
    bool storyNpc(Id p, std::string& guid) override
    {
        x0::Ref r = ref(p);
        char buf[96] = {0};
        if (!r.valid() || !s2m_story_npc(r.address(), buf, sizeof buf)) return false;
        guid = buf;
        return true;
    }

    bool setHidden(Id p, bool h) override
    {
        x0::Ref r = ref(p);
        return r.valid() && x0::Call(r, L"SetActorHiddenInGame").b(L"bNewHidden", h).run();
    }
    bool setCollision(Id p, bool on) override
    {
        x0::Ref r = ref(p);
        return r.valid() && x0::Call(r, L"SetActorEnableCollision").b(L"bNewActorEnableCollision", on).run();
    }
    bool setInteractionOff(Id p) override
    {
        x0::Ref r = ref(p), c;
        if (!r.valid() || !x0::prop<x0::Ref>(r, "NPCComponent", c) || !c.valid()) return false;
        return x0::Call(c, L"SetInteractionActive").b(L"bActive", false).run();
    }
    bool destroyActor(Id p) override
    {
        x0::Ref r = ref(p);
        return r.valid() && x0::call0(r, L"K2_DestroyActor");
    }

    bool aiFeatures(bool off, long long mask) override
    {
        x0::Ref am = x0::first_of(L"CustomConsoleManagerAM");
        return am.valid() && x0::Call(am, off ? L"XDeactivateAIFeature" : L"XActivateAIFeature").i(L"Feature", mask).run();
    }
    bool aLife(bool off) override
    {
        x0::Ref ss = x0::first_of(L"CustomConsoleManagerSS");
        return ss.valid() && x0::call0(ss, off ? L"XALifeDisable" : L"XALifeEnable");
    }
    bool inThreat(Id p) override
    {
        x0::Ref r = ref(p);
        return r.valid() && x0::call_b(r, L"IsInThreat", false);
    }
    bool focusIsLocal(Id p) override
    {
        x0::Ref r = ref(p), med = mediator();
        Id lp;
        V3 at;
        if (!r.valid() || !med.valid() || !localPawn(lp, at)) return false;
        x0::Call c(med, L"GetFocusedEnemy");
        c.o(L"Obj", r);
        if (!c.run()) return false;
        x0::Ref f = c.ro();
        return f.valid() && f.same(ref(lp));
    }
    bool guid(Id p, long long& uid) override
    {
        x0::Ref r = ref(p), med = mediator();
        if (!r.valid() || !med.valid()) return false;
        x0::Call c(med, L"GetGUID");
        c.o(L"Actor", r);
        if (!c.run()) return false;
        uid = c.ri();
        return true;
    }
    bool protoId(Id p, long long& id) override
    {
        x0::Ref r = ref(p), med = mediator();
        if (!r.valid() || !med.valid()) return false;
        x0::Call c(med, L"GetPrototypeID");
        c.o(L"Actor", r);
        if (!c.run()) return false;
        id = c.ri();
        return true;
    }
    bool killByUid(long long uid) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerDC");
        return m.valid() && x0::Call(m, L"XKillNpcByUID").i(L"NpcUID", uid).run();
    }
    bool destroyModel(long long uid) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerAI2");
        return m.valid() && x0::Call(m, L"XDestroyModel").i(L"UID", uid).run();
    }
    bool despawnDeadAtRadius(double r) override
    {
        x0::Ref m = x0::first_of(L"CustomConsoleManagerAA");
        return m.valid() && x0::Call(m, L"XDespawnDeadNPCsAtRadius").f(L"Radius", (float)r).run();
    }

    Body body(const x0::Ref& r, std::optional<int64_t> uid)
    {
        Body b;
        if (!r.valid()) return b;
        b.ok = true;
        b.pawn = idOf(r);
        b.hasUid = uid.has_value();
        b.uid = uid ? *uid : 0;
        b.spawnPending = m01::spawn_pending();
        return b;
    }
    Body target() override { return body(m01::target(), m01::target_uid()); }
    Body puppet() override { return body(m01::puppet(), m01::puppet_uid()); }
    int bodyViews() override { return x0::kMaxClients; }
    bool bodyViewLive(int view) override { return m01::view_live(view); }
    Body target(int view) override { return body(m01::target(view), m01::target_uid(view)); }
    Body puppet(int view) override { return body(m01::puppet(view), m01::puppet_uid(view)); }
    bool worldKey(std::string& key) override
    {
        Id lp;
        V3 at;
        if (!localPawn(lp, at)) return false;
        key = std::to_string(lp);
        return true;
    }
    bool worldOpen() override { return m01::body_gate(); }
    bool clPending() override
    {
        auto& f = m01::ext().loot_pending;
        return f && f();
    }

    bool inventoryOpen() override
    {
        x0::Ref w = x0::first_of(L"InventoryNew");
        return w.valid() && x0::call_b(w, L"IsVisible", false);
    }
    void containers(std::vector<Cont>& out) override
    {
        for (const x0::Ref& r : x0::all_of(L"BP_StaticItemContainer_C")) {
            bool h = false;
            if (!r.valid() || (x0::prop<bool>(r, "bHidden", h) && h)) continue;
            if (!x0::call_b(r, L"GetActorEnableCollision", true)) continue;
            x0::Vec3 v;
            if (!x0::call_v(r, L"K2_GetActorLocation", v)) continue;
            out.push_back({idOf(r), v.x, v.y, v.z});
        }
    }
    bool trace(double ax, double ay, double az, double bx, double by, double bz, bool& hit, double& iz) override
    {
        return s2m_trace_line(ax, ay, az, bx, by, bz, &hit, &iz);
    }
    bool simPhysicsOff(Id p) override
    {
        x0::Ref r = ref(p), m;
        if (!r.valid() || !x0::prop<x0::Ref>(r, "MeshComponent", m) || !m.valid()) return false;
        return x0::Call(m, L"SetSimulatePhysics").b(L"bSimulate", false).run();
    }
    bool setLoc(Id p, double x, double y, double z) override
    {
        x0::Ref r = ref(p);
        return r.valid() && x0::Call(r, L"K2_SetActorLocation").v(L"NewLocation", x0::Vec3{x, y, z}).b(L"bSweep", false).b(L"bTeleport", true).run();
    }
};

XEnv g_xenv;

}
}

X0_MODULE(pop)
{
    s2m_pop::init(&s2m_pop::g_xenv);
    x0::every(x0::Mod::pop, "pop", x0::Rate::t1000, [] {
        s2m_pop::g_xenv.beginTick();
        s2m_pop::tick1000();
    });
    x0::iface().is_vendor = [](int32_t uid) -> bool {
        std::vector<s2m_pop::Id> ps;
        s2m_pop::g_xenv.pawns(s2m_pop::PawnClass::All, ps);
        for (s2m_pop::Id p : ps) {
            long long g = 0;
            if (s2m_pop::isVendor(p) && s2m_pop::g_xenv.guid(p, g) && g == uid) return true;
        }
        return false;
    };
    m01::add_own_check(x0::Mod::pop, [](const x0::Ref& r) -> bool {
        return x0::fd("vendmir", 0) > 0 && x0::is_host() && s2m_pop::vendSkip(s2m_pop::g_xenv.idOf(r));
    });
}
