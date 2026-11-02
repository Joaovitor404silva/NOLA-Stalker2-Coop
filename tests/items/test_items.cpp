#include "../../src/s2m_items.h"
#include "../../src/s2m_inter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <set>
#include <vector>

using namespace s2m_items;

namespace
{
struct Act
{
    std::string name, cls;
    V3 pos;
    bool alive = true;
    bool live = true;
    bool hid = false;
    bool cont = false;
    bool lb = false;
    bool cc = false;
    bool ion = true;
    std::string mesh;
    double mh = 0;
    long long uid = 0;
    std::map<std::string, long long> inv;
    std::vector<s2x_is::Inst> st;
    long long money = 0;
    std::vector<std::pair<std::string, bool>> ic;
};

std::string g_root;
std::vector<std::string> g_out;
std::string g_nm;
void emit(const std::string& s) { g_out.push_back(g_nm + s); }

struct FlagV
{
    bool num = false;
    double d = 0;
    std::string s;
};

class Fake : public Env
{
  public:
    double t = 0;
    bool isHost = true;
    std::map<std::string, FlagV> fl;
    std::map<std::string, std::string> parts;
    std::vector<Act> acts;
    std::vector<int> order;
    std::map<std::string, int> byName;
    bool vis = false;
    int ia = -1;
    bool keyDown = false;
    std::optional<double> latch;
    bool aa = true;
    bool open = true;
    int rnd = 0;
    std::string lootTsv, qTsv, dropTsv, shareTsv;
    std::map<std::string, std::string> res;
    uint64_t pawnGen = 0;
    int wkGen = 0;
    std::optional<double> gs;
    std::map<std::string, std::string> sid2mesh;
    bool nodeApply = false;
    int spawned = 0;
    bool mcOn = false;
    bool mcWorld = false;
    bool fakeSp = false, natStub = false, swapOk = false, remOn = false;
    double remX = 0, remY = 0;
    int spN = 0;
    int nviews = 1;
    int me = 1;
    std::map<int, std::map<std::string, std::string>> vparts;
    std::map<int, bool> vdown;
    std::map<int, int> vsess;
    std::map<int, V3> vpos;

    static constexpr Obj ME = 1, PC = 2, WID = 3, RK = 4, AA = 5, BASE = 100;
    Fake()
    {
        Act me;
        me.name = "BP_Stalker2Character_C_0";
        me.cls = "BP_Stalker2Character_C";
        acts.push_back(me);
        order.push_back(0);
    }
    Act* act(Obj o)
    {
        if (o == ME) return &acts[0];
        if (o >= BASE && o - BASE < acts.size()) return &acts[(size_t)(o - BASE)];
        return nullptr;
    }
    Obj objOf(int i) const { return i == 0 ? ME : BASE + (Obj)i; }

    double now() override { return t; }
    bool host() override { return isHost; }
    double fnum(const char* key, double def) override
    {
        auto f = fl.find(key);
        return f != fl.end() && f->second.num ? f->second.d : def;
    }
    bool fhas(const char* key) override { return fl.count(key) != 0; }
    std::string fstr(const char* key, const std::string& def) override
    {
        auto f = fl.find(key);
        if (f == fl.end()) return def;
        return f->second.s;
    }
    void log(const std::string& line) override { emit("LOG " + line); }

    bool part(const char* tag, std::string& out) override
    {
        if (mcOn && vdown[0]) return false;
        auto f = parts.find(tag);
        if (f == parts.end()) return false;
        out = f->second;
        return true;
    }
    bool peerLoopback() override { return false; }
    int peerSessions() override { return 0; }
    bool peerSilent() override { return false; }
    bool remote(double& x, double& y, double& r) override
    {
        if (!remOn) return false;
        x = remX;
        y = remY;
        r = t;
        return true;
    }
    bool lookOn() override { return false; }
    std::string lookPut(const std::string&) override { return std::string(); }
    bool lookGet(const std::string&, std::string&) override { return false; }
    std::optional<double> gameSecs() override { return gs; }
    double wallSecs() override { return 0; }
    int random(int, int) override { return 1000 + ++rnd; }
    std::optional<double> keyLatch() override { return latch; }
    bool keyDownG() override { return keyDown; }

    std::string pfx;
    std::string path(const std::string& n) { return g_root + pfx + n; }
    bool readText(const std::string& name, std::string& out) override
    {
        std::ifstream f(path(name), std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }
    bool appendText(const std::string& name, const std::string& text) override
    {
        std::ofstream f(path(name), std::ios::binary | std::ios::app);
        if (!f) return false;
        f << text;
        return true;
    }
    bool writeText(const std::string& name, const std::string& text) override
    {
        std::ofstream f(path(name), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f << text;
        return true;
    }
    bool dataText(const std::string& name, std::string& out) override
    {
        if (auto r = res.find(name); r != res.end())
        {
            out = r->second;
            return true;
        }
        const std::string& p = name == "loot_sids.tsv" ? lootTsv : name == "s2u_qitems.tsv" ? qTsv : name == "s2u_drop.tsv" ? dropTsv : name == "s2u_qshare.tsv" ? shareTsv : std::string();
        if (p.empty()) return false;
        std::ifstream f(p, std::ios::binary);
        if (!f) return false;
        std::stringstream ss;
        ss << f.rdbuf();
        out = ss.str();
        return true;
    }

    bool valid(Obj o) override
    {
        if (o == PC || o == WID || o == RK) return true;
        if (o == AA) return aa;
        Act* a = act(o);
        return a && a->live;
    }
    Obj pawn() override { return ME; }
    Obj controller() override { return PC; }
    Obj first(const char* cls) override
    {
        std::string c = cls;
        if (c == "InventoryNew") return WID;
        if (c == "PlayerController") return PC;
        if (c == "CustomConsoleManagerRK") return RK;
        if (c == "CustomConsoleManagerAA") return aa ? AA : 0;
        return 0;
    }
    std::vector<Obj> all(const char* cls) override
    {
        std::vector<Obj> r;
        if (std::string(cls) == "BP_AI_Human_C")
        {
            for (size_t i = 1; i < acts.size(); ++i)
                if (acts[i].lb && acts[i].live) r.push_back(objOf((int)i));
            return r;
        }
        if (mcWorld && std::string(cls) != "Pawn")
        {
            for (size_t i = 1; i < acts.size(); ++i)
                if (acts[i].cls == cls && acts[i].live) r.push_back(objOf((int)i));
            return r;
        }
        if (std::string(cls) != "Pawn") return r;
        for (int i : order) r.push_back(objOf(i));
        return r;
    }
    uint64_t addr(Obj o) override { return o * 16 + (o == ME ? pawnGen * 1000000 : 0); }
    std::string name(Obj o) override
    {
        Act* a = act(o);
        return a ? a->name : std::string();
    }
    std::string cls(Obj o) override
    {
        Act* a = act(o);
        return a ? a->cls : std::string();
    }
    std::string full(Obj o) override
    {
        Act* a = act(o);
        return a ? a->cls + " /Game/W/World.World:PersistentLevel." + a->name : std::string();
    }
    bool loc(Obj o, V3& out) override
    {
        Act* a = act(o);
        if (!a) return false;
        out = a->pos;
        return true;
    }
    bool setLoc(Obj o, const V3& v) override
    {
        Act* a = act(o);
        if (!a) return false;
        a->pos = v;
        return true;
    }
    bool alive(Obj o, bool& out) override
    {
        Act* a = act(o);
        if (!a) return false;
        out = a->alive;
        return true;
    }
    bool maxHp(Obj o, double& out) override
    {
        Act* a = act(o);
        if (!a || a->mh <= 0) return false;
        out = a->mh;
        return true;
    }
    bool hidden(Obj o, bool& out) override
    {
        Act* a = mcWorld ? act(o) : nullptr;
        out = a ? a->hid : false;
        return true;
    }
    bool setHidden(Obj o, bool on) override
    {
        Act* a = mcWorld ? act(o) : nullptr;
        if (a && a->hid != on)
        {
            a->hid = on;
            emit("STUB hide " + a->name + " " + (on ? "1" : "0"));
        }
        return true;
    }
    bool setCollision(Obj, bool) override { return true; }
    bool setActorTick(Obj, bool) override { return true; }
    Obj interacted(Obj) override { return ia < 0 ? 0 : objOf(ia); }
    bool closeLoot(Obj) override
    {
        emit("STUB close");
        return true;
    }
    bool visible(Obj, bool& out) override
    {
        out = vis;
        return true;
    }
    bool natFail = false;
    Act* natAct(Obj o, int where) { return where == 1 ? (ia < 0 ? nullptr : &acts[(size_t)ia]) : act(o); }
    bool natItems(Obj o, int where, bool, Counts& out) override
    {
        out.clear();
        Act* a = natFail ? nullptr : natAct(o, where);
        if (!a) return false;
        for (auto& kv : a->inv)
            if (kv.second > 0) out[kv.first] = kv.second;
        return true;
    }
    bool natPut(Obj, const std::string& sid, long long d, long long& done) override
    {
        done = 0;
        Act* a = natFail ? nullptr : natAct(0, 1);
        if (!a) return false;
        long long& c = a->inv[sid];
        if (d < 0) d = -std::min(c, -d);
        c += d;
        done = d;
        if (c <= 0) a->inv.erase(sid);
        emit("STUB nput " + sid + " " + std::to_string(d) + " " + a->name);
        return true;
    }
    bool natRefresh(Obj) override
    {
        emit("STUB refresh");
        return true;
    }
    uint32_t nextUid = 5000;
    Act* instAct(Obj o, int where) { return where == 1 ? (ia < 0 ? nullptr : &acts[(size_t)ia]) : act(o); }
    uint32_t synth(const Act* a, const std::string& sid) const
    {
        uint32_t h = 2166136261u;
        for (char c : a->name + "|" + sid) h = (h ^ (uint8_t)c) * 16777619u;
        return 0x40000000u | (h & 0x0fffffffu);
    }
    void fixInst(Act* a)
    {
        std::map<std::string, long long> k;
        std::vector<s2x_is::Inst> keep;
        for (auto& i : a->st)
        {
            auto f = a->inv.find(i.st.sid);
            long long have = f == a->inv.end() ? 0 : f->second;
            if (k[i.st.sid] + 1 > have) continue;
            k[i.st.sid] += 1;
            keep.push_back(i);
        }
        a->st = keep;
    }
    bool natInst(Obj o, int where, bool, std::vector<s2x_is::Inst>& out) override
    {
        out.clear();
        Act* a = natFail ? nullptr : instAct(o, where);
        if (!a) return false;
        fixInst(a);
        std::map<std::string, long long> k;
        for (auto& i : a->st)
        {
            out.push_back(i);
            k[i.st.sid] += 1;
        }
        for (auto& kv : a->inv)
        {
            long long r = kv.second - k[kv.first];
            if (r <= 0) continue;
            s2x_is::Inst p;
            p.uid = synth(a, kv.first);
            p.n = r;
            p.st.sid = kv.first;
            out.push_back(p);
        }
        return true;
    }
    bool natPutI(Obj o, int where, const s2x_is::IState& s, long long n, uint32_t& uid) override
    {
        uid = 0;
        Act* a = natFail ? nullptr : instAct(o, where);
        if (!a || n <= 0) return false;
        fixInst(a);
        a->inv[s.sid] += n;
        if (!s.plain())
        {
            s2x_is::Inst i;
            i.uid = uid = ++nextUid;
            i.n = 1;
            i.st = s;
            a->st.push_back(i);
        }
        else uid = synth(a, s.sid);
        emit("STUB iput " + s2x_is::show(s) + " x" + std::to_string(n) + " " + a->name);
        return true;
    }
    bool natRmI(Obj o, int where, uint32_t uid, long long n, long long& done) override
    {
        done = 0;
        Act* a = natFail ? nullptr : instAct(o, where);
        if (!a) return false;
        fixInst(a);
        for (size_t k = 0; k < a->st.size(); ++k)
            if (a->st[k].uid == uid)
            {
                std::string sid = a->st[k].st.sid;
                emit("STUB irm " + s2x_is::show(a->st[k].st) + " " + a->name);
                a->st.erase(a->st.begin() + (long)k);
                if (--a->inv[sid] <= 0) a->inv.erase(sid);
                done = 1;
                return true;
            }
        for (auto& kv : a->inv)
            if (synth(a, kv.first) == uid)
            {
                std::string sid = kv.first;
                long long c = 0;
                for (auto& i : a->st) if (i.st.sid == sid) ++c;
                done = std::min(n, kv.second - c);
                if (done <= 0) { done = 0; return false; }
                kv.second -= done;
                emit("STUB irm " + sid + " x" + std::to_string(done) + " " + a->name);
                if (kv.second <= 0) a->inv.erase(sid);
                return true;
            }
        return false;
    }
    bool natApplyI(uint32_t uid, const s2x_is::IState& s) override
    {
        for (auto& a : acts)
        {
            fixInst(&a);
            for (auto& i : a.st)
                if (i.uid == uid)
                {
                    i.st = s;
                    emit("STUB iapply " + s2x_is::show(s) + " " + a.name);
                    return true;
                }
            for (auto& kv : a.inv)
                if (synth(&a, kv.first) == uid && kv.first == s.sid)
                {
                    s2x_is::Inst i;
                    i.uid = ++nextUid;
                    i.n = 1;
                    i.st = s;
                    a.st.push_back(i);
                    emit("STUB iapply " + s2x_is::show(s) + " " + a.name);
                    return true;
                }
        }
        return false;
    }
    std::set<Obj> unread;
    bool invReadable(Obj o) override { return act(o) != nullptr && !unread.count(o); }
    bool count(Obj o, const std::string& sid, long long& out) override
    {
        Act* a = act(o);
        if (!a) return false;
        if (unread.count(o)) emit("BADREAD " + a->name);
        auto f = a->inv.find(sid);
        out = f == a->inv.end() ? 0 : f->second;
        return true;
    }
    std::optional<long long> guid(Obj o) override
    {
        Act* a = act(o);
        if (!a || a->uid <= 0) return std::nullopt;
        return a->uid;
    }
    bool money(Obj o, long long& out) override
    {
        Act* a = act(o);
        if (!a) return false;
        out = a->money;
        return true;
    }
    bool guidHex(Obj, std::string&) override { return false; }
    std::optional<std::string> meshName(Obj o, const char*) override
    {
        Act* a = act(o);
        if (!a || a->mesh.empty()) return std::nullopt;
        return a->mesh;
    }
    bool meshVisible(Obj, bool&) override { return false; }
    bool setMeshVisible(Obj, bool) override { return false; }
    int setInteraction(Obj o, bool on) override
    {
        if (Act* b = act(o))
            for (auto& k : b->ic)
                if (s2inter::flip(s2inter::stockOwned([&](const char* n) { return k.first == n; }), true, k.second, on)) k.second = on;
        Act* a = mcWorld ? act(o) : nullptr;
        if (!a) return 0;
        a->ion = on;
        emit("STUB inter " + a->name + " " + (on ? "1" : "0"));
        return 1;
    }
    std::vector<Obj> comps(Obj o, const char* path) override
    {
        Act* a = act(o);
        if (!a) return {};
        std::string p = path;
        if (a->cc && (p == "/Script/Stalker2.ItemContainerInteractionComponent" || p == "/Script/Stalker2.ItemContainerHoldComponent")) return {o};
        if (a->lb && p == "/Script/Stalker2.DeadBody") return {o};
        return {};
    }
    bool compActive(Obj, bool&) override { return false; }
    bool compQuiet(Obj, bool) override { return false; }
    bool compWake(Obj, bool) override { return false; }

    bool startNode(const std::string& sid, std::string&) override
    {
        emit("STUB node " + sid);
        if (nodeApply && sid.rfind("S2U_P_", 0) == 0)
        {
            size_t u = sid.rfind('_');
            std::string item = sid.substr(6, u - 6);
            long long k = (long long)std::strtod(sid.substr(u + 1).c_str(), nullptr);
            acts[0].inv[item] -= k;
        }
        return true;
    }
    void loadMeshes()
    {
        if (!sid2mesh.empty() || dropTsv.empty()) return;
        std::ifstream f(dropTsv, std::ios::binary);
        std::string l;
        while (std::getline(f, l))
        {
            if (!l.empty() && l.back() == '') l.pop_back();
            size_t a = l.find('	');
            if (a == std::string::npos) continue;
            size_t b = l.find('	', a + 1);
            sid2mesh[l.substr(0, a)] = l.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
        }
    }
    bool uiDrop(uint32_t uid, long long n) override
    {
        long long done = 0;
        if (!natRmI(pawn(), 0, uid, n, done)) return false;
        emit("STUB uidrop " + std::to_string(done));
        return true;
    }
    bool spawnItem(const std::string& sid, double x, double y, double z) override
    {
        loadMeshes();
        emit("STUB spawnitem " + sid + " " + std::to_string((long long)x) + " " + std::to_string((long long)y) + " " + std::to_string((long long)z));
        Act a;
        a.name = "BP_StaticItemContainer_C_s" + std::to_string(++spawned);
        a.cls = "BP_StaticItemContainer_C";
        a.mesh = sid2mesh[sid];
        a.pos = {x, y, z};
        a.cont = true;
        a.inv[sid] = 1;
        acts.push_back(a);
        byName[a.name] = (int)acts.size() - 1;
        return true;
    }
    bool natSpawnI(const s2x_is::IState& s, double x, double y, double z) override
    {
        loadMeshes();
        emit("STUB ispawn " + s2x_is::show(s) + " " + std::to_string((long long)x) + " " + std::to_string((long long)y) + " " + std::to_string((long long)z));
        Act a;
        a.name = "BP_StaticItemContainer_C_s" + std::to_string(++spawned);
        a.cls = "BP_StaticItemContainer_C";
        a.mesh = sid2mesh[s.sid];
        a.pos = {x, y, z};
        a.cont = true;
        a.inv[s.sid] = 1;
        if (!s.plain())
        {
            s2x_is::Inst i;
            i.uid = ++nextUid;
            i.n = 1;
            i.st = s;
            a.st.push_back(i);
        }
        acts.push_back(a);
        byName[a.name] = (int)acts.size() - 1;
        return true;
    }
    bool createItem(const std::string& sid, long long uid, long long n) override
    {
        emit("STUB create " + sid + " " + std::to_string(uid) + " " + std::to_string(n));
        if (uid == 0) acts[0].inv[sid] += n;
        else if (fakeSp || !natB.empty()) for (auto& a : acts) if (a.uid == uid) a.inv[sid] += n;
        return true;
    }
    bool spawnObj(const std::string& sid, double x, double y, double z, std::string&) override
    {
        emit("STUB spawn " + sid + " " + std::to_string((long long)x) + " " + std::to_string((long long)y) + " " + std::to_string((long long)z));
        if (fakeSp)
        {
            ++spN;
            Act a;
            a.name = "BP_AI_Human_C_" + std::to_string(5000 + spN);
            a.cls = "BP_AI_Human_C";
            a.pos = {x, y, z};
            a.alive = true;
            a.lb = true;
            a.mh = 5000;
            a.uid = 900 + spN;
            acts.push_back(a);
            int i = (int)acts.size() - 1;
            byName[a.name] = i;
            order.push_back(i);
        }
        return true;
    }
    bool killUid(long long u) override
    {
        if (!fakeSp) return false;
        for (auto& a : acts) if (a.uid == u) a.alive = false;
        return true;
    }
    bool setUnkillable(long long, bool) override { return false; }
    bool destroyModel(long long u) override
    {
        emit("STUB destroy " + std::to_string(u));
        for (auto& a : acts)
            if (a.uid == u) a.live = false;
        return true;
    }

    bool isOwnBody(Obj) override { return false; }
    Obj targetPawn() override { return 0; }
    Obj puppetPawn() override { return 0; }
    bool bodySpawning() override { return false; }
    std::string worldKey() override { return "1|me" + std::to_string(wkGen); }
    bool worldOpen() override { return open; }
    bool storyNpc(Obj) override { return true; }
    std::optional<long long> npcUid(uint64_t, const std::string&) override { return std::nullopt; }
    bool natOn() override { return natStub; }
    bool natDead(const std::string&) override { return false; }
    bool natCorpse(const std::string&, V3&) override { return false; }
    std::string lkHold(const std::string&, const std::string&, double, double, double) override { return std::string(); }
    int lkOff(uint64_t, int, double) override { return 0; }
    std::optional<double> clSwap(const std::string& h, uint64_t, uint64_t) override
    {
        if (!swapOk) return std::nullopt;
        emit("STUB swap " + h);
        return 0.0;
    }
    void clSwapTick() override {}
    std::map<std::string, std::pair<std::string, int>> natB;
    int gcMode = 0;
    std::vector<uint64_t> gcKeep;
    bool rmOff = false;
    bool removeItem(Obj o, const std::string& sid, long long n, long long& removed) override
    {
        removed = 0;
        Act* a = act(o);
        if (!a || rmOff) return false;
        long long& c = a->inv[sid];
        removed = std::min(c, n);
        c -= removed;
        if (c <= 0) a->inv.erase(sid);
        emit("STUB remove " + sid + " " + a->name + " " + std::to_string(removed));
        return true;
    }
    uint64_t natBody(const std::string& hid, int& st) override
    {
        st = 0;
        auto f = natB.find(hid);
        if (f == natB.end()) return 0;
        auto b = byName.find(f->second.first);
        if (b == byName.end()) return 0;
        st = f->second.second;
        return addr(objOf(b->second));
    }
    void natClOwn(uint64_t a, bool on) override
    {
        Act* x = act(a / 16);
        emit("STUB natown " + (x ? x->name : std::string("?")) + " " + (on ? "1" : "0"));
    }
    void gcFeed(int mode, const std::vector<uint64_t>& keep) override
    {
        gcMode = mode;
        gcKeep = keep;
    }
    std::string gcStat() override { return "gc=" + std::to_string(gcMode) + " keep=" + std::to_string(gcKeep.size()); }
    void natGcKeep(std::vector<uint64_t>&) override {}
    bool qiCorpse(Obj, const std::string&) override { return false; }
    bool qiHostKeep(const std::string&, Obj) override { return false; }

    std::string peerId() override { return "P1"; }
    std::string cid;
    std::string campaign() override { return cid; }
    bool cuOn() override { return false; }
    s2m_save::QState* cuQ() override { return nullptr; }
    std::map<std::string, int>* cuApplied() override { return nullptr; }
    int cuApply(const std::string&) override { return -1; }
    bool cuRbReady() override { return true; }
    void cuRbDone(const std::string&) override {}

    bool mc() override { return mcOn; }
    int views() override { return mcOn ? nviews : 1; }
    int viewPid(int v) override
    {
        if (isHost) return v + 1;
        return v == 0 ? 0 : -1;
    }
    int myPid() override { return isHost ? 0 : me; }
    bool partV(const char* tag, std::string& out, int v) override
    {
        if (v == 0) return part(tag, out);
        if (vdown[v]) return false;
        auto& m = vparts[v];
        auto f = m.find(tag);
        if (f == m.end()) return false;
        out = f->second;
        return true;
    }
    bool remoteV(int v, double& x, double& y, double& recv) override
    {
        auto f = vpos.find(v);
        if (f == vpos.end() || vdown[v]) return false;
        x = f->second.x;
        y = f->second.y;
        recv = t;
        return true;
    }
    int viewSessions(int v) override { return v == 0 ? peerSessions() + vsess[0] : vsess[v]; }
};

std::vector<std::string> words(const std::string& l)
{
    std::vector<std::string> w;
    std::istringstream is(l);
    std::string x;
    while (is >> x) w.push_back(x);
    return w;
}

double num(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

bool isNum(const std::string& s)
{
    if (s.empty()) return false;
    char* e = nullptr;
    std::strtod(s.c_str(), &e);
    return e && *e == 0;
}

void cmd(Fake& F, const std::string& l, const std::vector<std::string>& w, const std::string& root)
{
    const std::string& c = w[0];
    if (c == "role") F.isHost = w[1] == "host";
    else if (c == "flag")
    {
        FlagV v;
        v.s = w.size() > 2 ? w[2] : "";
        v.num = isNum(v.s);
        v.d = v.num ? num(v.s) : 0;
        F.fl[w[1]] = v;
    }
    else if (c == "t") F.t = num(w[1]);
    else if (c == "me") F.acts[0].pos = {num(w[1]), num(w[2]), num(w[3])};
    else if (c == "myinv") F.acts[0].inv[w[1]] = (long long)num(w[2]);
    else if (c == "money") F.acts[0].money = (long long)num(w[1]);
    else if (c == "npc")
    {
        Act a;
        a.name = "BP_AI_Human_C_" + w[1];
        a.cls = "BP_AI_Human_C";
        a.pos = {num(w[2]), num(w[3]), num(w[4])};
        a.alive = w[5] == "1";
        a.ic = {{"DeadBody", false}, {"NPCComponent", true}, {"DeadBodyHoldComponent", true}};
        F.acts.push_back(a);
        int i = (int)F.acts.size() - 1;
        F.order.push_back(i);
        F.byName[a.name] = i;
    }
    else if (c == "npcinv") F.acts[(size_t)F.byName["BP_AI_Human_C_" + w[1]]].inv[w[2]] = (long long)num(w[3]);
    else if (c == "kill") F.acts[(size_t)F.byName["BP_AI_Human_C_" + w[1]]].alive = false;
    else if (c == "npcuid") F.acts[(size_t)F.byName["BP_AI_Human_C_" + w[1]]].uid = (long long)num(w[2]);
    else if (c == "natbody")
    {
        if (w[2] == "-") F.natB.erase(w[1]);
        else F.natB[w[1]] = {w[2], (int)num(w[3])};
    }
    else if (c == "rmoff") F.rmOff = w[1] == "1";
    else if (c == "natfail") F.natFail = w[1] == "1";
    else if (c == "res")
    {
        std::string v;
        for (size_t i = 2; i < w.size(); ++i) v += (i > 2 ? "\r\n" : "") + w[i];
        for (size_t i = 0; (i = v.find("\\t", i)) != std::string::npos;) v.replace(i, 2, "\t");
        F.res[w[1]] = v + "\r\n";
    }
    else if (c == "file")
    {
        std::string v;
        for (size_t i = 2; i < w.size(); ++i) v += (i > 2 ? "\n" : "") + w[i];
        for (size_t i = 0; (i = v.find("\\t", i)) != std::string::npos;) v.replace(i, 2, "\t");
        F.writeText(w[1], v + "\n");
    }
    else if (c == "newpawn") F.pawnGen += 1;
    else if (c == "newworld") F.wkGen += 1;
    else if (c == "gs") { if (w[1] == "-") F.gs.reset(); else F.gs = num(w[1]); }
    else if (c == "ainv")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end()) F.acts[(size_t)f->second].inv[w[2]] = (long long)num(w[3]);
    }
    else if (c == "take")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end())
        {
            long long& c2 = F.acts[(size_t)f->second].inv[w[2]];
            long long n = std::min(c2, (long long)num(w[3]));
            c2 -= n;
            if (c2 <= 0) F.acts[(size_t)f->second].inv.erase(w[2]);
            F.acts[0].inv[w[2]] += n;
        }
    }
    else if (c == "put")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end())
        {
            long long& c2 = F.acts[0].inv[w[2]];
            long long n = std::min(c2, (long long)num(w[3]));
            c2 -= n;
            if (c2 <= 0) F.acts[0].inv.erase(w[2]);
            F.acts[(size_t)f->second].inv[w[2]] += n;
        }
    }
    else if (c == "myst" || c == "ast")
    {
        size_t o = c == "ast" ? 1 : 0;
        Act* a = o ? (F.byName.count(w[1]) ? &F.acts[(size_t)F.byName[w[1]]] : nullptr) : &F.acts[0];
        s2x_is::Inst i;
        i.st.sid = w[1 + o];
        if (a && s2x_is::dec(w.size() > 2 + o ? w[2 + o] : "", i.st))
        {
            F.fixInst(a);
            i.uid = ++F.nextUid;
            i.n = 1;
            a->inv[i.st.sid] += 1;
            a->st.push_back(i);
        }
        else emit("BAD " + l);
    }
    else if (c == "puti" || c == "takei" || c == "dropi")
    {
        Act* box = nullptr;
        if (c == "dropi")
        {
            F.loadMeshes();
            Act g;
            g.name = w[1];
            g.cls = "BP_StaticItemContainer_C";
            g.mesh = F.sid2mesh[w[2]];
            g.pos = {num(w[4]), num(w[5]), num(w[6])};
            g.cont = true;
            F.acts.push_back(g);
            F.byName[g.name] = (int)F.acts.size() - 1;
        }
        if (F.byName.count(w[1])) box = &F.acts[(size_t)F.byName[w[1]]];
        Act* me = &F.acts[0];
        Act* from = c == "takei" ? box : me;
        Act* to = c == "takei" ? me : box;
        s2x_is::IState want;
        want.sid = w[2];
        if (!from || !to || !s2x_is::dec(w.size() > 3 ? w[3] : "", want)) { emit("BAD " + l); return; }
        F.fixInst(from);
        F.fixInst(to);
        int best = -1;
        for (size_t k = 0; k < from->st.size(); ++k)
            if (s2x_is::same(from->st[k].st, want)) { best = (int)k; break; }
        if (want.plain() || best < 0)
        {
            if (!want.plain()) { emit("BAD no instance " + l); return; }
            if (from->inv[want.sid] <= 0) { emit("BAD none " + l); return; }
            if (--from->inv[want.sid] <= 0) from->inv.erase(want.sid);
            to->inv[want.sid] += 1;
            return;
        }
        s2x_is::Inst i = from->st[(size_t)best];
        from->st.erase(from->st.begin() + best);
        if (--from->inv[want.sid] <= 0) from->inv.erase(want.sid);
        to->inv[want.sid] += 1;
        to->st.push_back(i);
    }
    else if (c == "pickupi")
    {
        auto f = F.byName.find(w[1]);
        if (f == F.byName.end()) { emit("BAD " + l); return; }
        Act& g = F.acts[(size_t)f->second];
        F.fixInst(&g);
        F.fixInst(&F.acts[0]);
        for (auto& kv : g.inv) F.acts[0].inv[kv.first] += kv.second;
        for (auto& i : g.st) F.acts[0].st.push_back(i);
        g.inv.clear();
        g.st.clear();
        g.pos = {0, 0, 0};
    }
    else if (c == "insts")
    {
        Act* a = w[1] == "me" ? &F.acts[0] : (F.byName.count(w[1]) ? &F.acts[(size_t)F.byName[w[1]]] : nullptr);
        std::string k;
        if (a)
        {
            std::vector<s2x_is::Inst> v;
            F.natInst(w[1] == "me" ? Fake::ME : F.objOf(F.byName[w[1]]), 0, true, v);
            std::vector<std::string> r;
            for (auto& i : v) r.push_back(s2x_is::show(i.st) + (i.n != 1 ? " x" + std::to_string(i.n) : std::string()));
            std::sort(r.begin(), r.end());
            for (auto& x : r) k += " | " + x;
        }
        emit("INSTS " + w[1] + k);
    }
    else if (c == "mine")
    {
        std::string k;
        for (auto& kv : F.acts[0].inv)
            if (kv.second != 0) k += " " + kv.first + "*" + std::to_string(kv.second);
        emit("MINE" + k);
    }
    else if (c == "myqi")
    {
        std::string k;
        for (auto& kv : F.acts[0].inv)
            if (kv.second != 0 && (isQuestItem(kv.first) || kv.first == "EchoE01")) k += " " + kv.first + "*" + std::to_string(kv.second);
        emit("MYQI" + k);
    }
    else if (c == "gcstat")
    {
        std::string k;
        for (uint64_t a : F.gcKeep)
        {
            Act* x = F.act(a / 16);
            k += " " + (x ? x->name : std::string("?"));
        }
        emit("GC mode=" + std::to_string(F.gcMode) + k);
    }
    else if (c == "inv")
    {
        auto f = F.byName.find(w[1]);
        std::string k;
        if (f != F.byName.end())
            for (auto& kv : F.acts[(size_t)f->second].inv) k += " " + kv.first + "*" + std::to_string(kv.second);
        emit("INV " + w[1] + k);
    }
    else if (c == "gone")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end()) F.acts[(size_t)f->second].live = false;
    }
    else if (c == "cont")
    {
        Act a;
        a.name = w[1] + "_" + w[2];
        a.cls = w[1];
        a.pos = {num(w[3]), num(w[4]), num(w[5])};
        a.cont = true;
        a.cc = true;
        F.acts.push_back(a);
        F.byName[a.name] = (int)F.acts.size() - 1;
    }
    else if (c == "open") F.ia = w[1] == "-" ? -1 : F.byName.count(w[1]) ? F.byName[w[1]] : -1;
    else if (c == "vis") F.vis = w[1] == "1";
    else if (c == "use")
    {
        auto f = F.byName.find(w[1]);
        if (f == F.byName.end()) emit("STUB use ? " + w[1]);
        else if (!F.acts[(size_t)f->second].ion) emit("STUB no prompt on " + w[1] + ": cannot open");
        else
        {
            F.ia = f->second;
            F.vis = true;
            emit("STUB window open " + w[1]);
        }
    }
    else if (c == "key") F.keyDown = w[1] == "1";
    else if (c == "latch") F.latch = F.t;
    else if (c == "part")
    {
        size_t p = l.find(w[1], 4) + w[1].size();
        while (p < l.size() && l[p] == ' ') ++p;
        F.parts[w[1]] = l.substr(p);
    }
    else if (c == "nopart") F.parts.erase(w[1]);
    else if (c == "aa") F.aa = w[1] == "1";
    else if (c == "worldopen") F.open = w[1] == "1";
    else if (c == "mark") emit("MARK t=" + w[1]);
    else if (c == "lootbody")
    {
        Act a;
        a.name = "BP_AI_Human_C_" + w[1];
        a.cls = "BP_AI_Human_C";
        a.pos = {num(w[2]), num(w[3]), num(w[4])};
        a.alive = w[5] == "1";
        a.lb = true;
        a.mh = 5000;
        a.uid = (long long)num(w[6]);
        F.acts.push_back(a);
        F.byName[a.name] = (int)F.acts.size() - 1;
    }
    else if (c == "send" || c == "remove")
    {
        std::string a = w.size() > 2 ? w[2] : "", b = w.size() > 3 ? w[3] : "", d = w.size() > 4 ? w[4] : "", e = w.size() > 5 ? w[5] : "";
        bool ok = c == "send" ? itemSend(w[1], a, b, d) : itemRemove(w[1], a, b, d);
        (void)e;
        emit(std::string("RET ") + (ok ? "true" : "false"));
    }
    else if (c == "idropnow") emit("IDROPNOW " + idropNow(w[1], (long long)num(w[2])));
    else if (c == "camp") F.cid = w.size() > 1 ? w[1] : "";
    else if (c == "rotate") campaignRotated();
    else if (c == "rm") std::remove(F.path(w[1]).c_str());
    else if (c == "status") emit("STATUS " + status());
    else if (c == "tick") tick100();
    else if (c == "outcl") emit("OUTCL " + outCl());
    else if (c == "out") emit("OUT " + outK() + "|" + outLoot() + "|" + outU());
    else if (c == "mc")
    {
        F.mcOn = w[1] != "0";
        F.nviews = w.size() > 2 ? (int)num(w[2]) : 1;
        if (w.size() > 3) F.me = (int)num(w[3]);
    }
    else if (c == "mcworld") F.mcWorld = w[1] == "1";
    else if (c == "remote") { F.remOn = true; F.remX = num(w[1]); F.remY = num(w[2]); }
    else if (c == "fakespawn") F.fakeSp = w[1] == "1";
    else if (c == "natstub") F.natStub = w[1] == "1";
    else if (c == "swapok") F.swapOk = w[1] == "1";
    else if (c == "vpart")
    {
        int v = (int)num(w[1]);
        std::string val;
        for (size_t i = 3; i < w.size(); ++i) val += (i > 3 ? " " : "") + w[i];
        if (v == 0) F.parts[w[2]] = val;
        else F.vparts[v][w[2]] = val;
    }
    else if (c == "vnopart")
    {
        int v = (int)num(w[1]);
        if (v == 0) F.parts.erase(w[2]);
        else F.vparts[v].erase(w[2]);
    }
    else if (c == "vdown") F.vdown[(int)num(w[1])] = w[2] == "1";
    else if (c == "vsess") F.vsess[(int)num(w[1])] = (int)num(w[2]);
    else if (c == "vpos") F.vpos[(int)num(w[1])] = {num(w[2]), num(w[3]), num(w[4])};
    else if (c == "item")
    {
        Act a;
        a.name = w[1];
        a.cls = w[2];
        a.pos = {num(w[3]), num(w[4]), num(w[5])};
        a.cont = true;
        F.acts.push_back(a);
        F.byName[a.name] = (int)F.acts.size() - 1;
    }
    else if (c == "nodeapply") F.nodeApply = w[1] == "1";
    else if (c == "dropitem")
    {
        F.loadMeshes();
        F.acts[0].inv[w[2]] -= (long long)num(w[3]);
        Act a;
        a.name = w[1];
        a.cls = "BP_StaticItemContainer_C";
        a.mesh = F.sid2mesh[w[2]];
        a.pos = {num(w[4]), num(w[5]), num(w[6])};
        a.cont = true;
        F.acts.push_back(a);
        F.byName[a.name] = (int)F.acts.size() - 1;
    }
    else if (c == "worlditem")
    {
        F.loadMeshes();
        Act a;
        a.name = w[1];
        a.cls = "BP_StaticItemContainer_C";
        a.mesh = F.sid2mesh[w[2]];
        a.pos = {num(w[3]), num(w[4]), num(w[5])};
        a.cont = true;
        F.acts.push_back(a);
        F.byName[a.name] = (int)F.acts.size() - 1;
    }
    else if (c == "pickup")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end()) F.acts[(size_t)f->second].pos = {0, 0, 0};
        F.acts[0].inv[w[2]] += (long long)num(w[3]);
    }
    else if (c == "dstat") emit("DSTAT " + status());
    else if (c == "pick")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end()) F.acts[(size_t)f->second].live = false;
    }
    else if (c == "hidden")
    {
        auto f = F.byName.find(w[1]);
        emit("HIDDEN " + w[1] + " " + (f != F.byName.end() && F.acts[(size_t)f->second].hid ? "1" : "0"));
    }
    else if (c == "inter")
    {
        auto f = F.byName.find(w[1]);
        std::string s;
        if (f != F.byName.end())
            for (auto& k : F.acts[(size_t)f->second].ic) s += " " + k.first + "=" + (k.second ? "1" : "0");
        emit("INTER " + w[1] + s);
    }
    else if (c == "press")
    {
        auto f = F.byName.find(w[1]);
        std::string r = "none";
        if (f != F.byName.end())
            for (auto& k : F.acts[(size_t)f->second].ic)
                if (k.second && (k.first == "DeadBody" || k.first == "NPCComponent")) { r = k.first == "DeadBody" ? "drag" : "search"; break; }
        emit("PRESS " + w[1] + " " + r);
    }
    else if (c == "drag")
    {
        auto f = F.byName.find(w[1]);
        if (f != F.byName.end())
            for (auto& k : F.acts[(size_t)f->second].ic)
                if (k.first == "DeadBody") k.second = w[2] == "1";
    }
    else if (c == "mcout") emit("MCOUT " + outKH() + outGH() + outGC() + outGR() + outUR() + outYR() + outCLH() + "|" + outGround() + "|" + outStash() + "|" + outU());
    else emit("BAD " + l);
}

struct Node
{
    std::string nm;
    Fake F;
    void* st = nullptr;
    std::string x1, inject;
    bool up = true, drop = false;
};

std::string x1Of()
{
    return outK() + outLoot() + outGround() + outU() + outStash() + outArt() + outCl() + outKH() + outGH() + outGC() + outGR() + outUR() + outYR() + outCLH() + outIDR() + outIDS() + outIDU();
}

std::map<std::string, std::string> tagsOf(const std::string& x1)
{
    std::map<std::string, std::string> r;
    size_t i = 0;
    std::vector<std::pair<std::string, size_t>> at;
    while ((i = x1.find('#', i)) != std::string::npos)
    {
        size_t j = i + 1;
        while (j < x1.size() && ((x1[j] >= 'A' && x1[j] <= 'Z') || (x1[j] >= '0' && x1[j] <= '9'))) ++j;
        if (j > i + 1 && j < x1.size() && x1[j] == ':' && x1[i + 1] >= 'A' && x1[i + 1] <= 'Z') at.push_back({x1.substr(i + 1, j - i - 1), i});
        i = j;
    }
    for (size_t k = 0; k < at.size(); ++k)
    {
        size_t b = at[k].second + at[k].first.size() + 2;
        size_t e = k + 1 < at.size() ? at[k + 1].second : x1.size();
        if (!r.count(at[k].first)) r[at[k].first] = x1.substr(b, e - b);
    }
    return r;
}

void enter(Node& n)
{
    g_nm = n.nm + " ";
    bind(&n.F);
    swapState(n.st);
}
void leaveNode(Node& n)
{
    n.st = swapState(nullptr);
    g_nm.clear();
}
void setView(Fake& F, int v, const std::map<std::string, std::string>& m)
{
    if (v == 0) F.parts = m;
    else F.vparts[v] = m;
}

std::string dataOf(const std::string& q, const char* n)
{
    size_t p = q.rfind("s2u_qitems.tsv");
    return p == std::string::npos ? std::string() : q.substr(0, p) + n;
}
std::string dropOf(const std::string& q) { return dataOf(q, "s2u_drop.tsv"); }

int runMc(std::ifstream& in, int n, const std::string& root, const std::string& lootTsv, const std::string& qTsv)
{
    std::vector<Node> N((size_t)n);
    for (int k = 0; k < n; ++k)
    {
        Node& d = N[(size_t)k];
        d.nm = k == 0 ? "h" : "c" + std::to_string(k);
        d.F.isHost = k == 0;
        d.F.me = k;
        d.F.mcOn = true;
        d.F.mcWorld = true;
        d.F.nviews = std::max(1, n - 1);
        d.F.lootTsv = lootTsv;
        d.F.qTsv = qTsv;
        d.F.dropTsv = dropOf(qTsv);
        d.F.shareTsv = dataOf(qTsv, "s2u_qshare.tsv");
        d.F.pfx = d.nm + "_";
    }
    auto byName = [&](const std::string& s2) -> Node* {
        for (auto& d : N)
            if (d.nm == s2) return &d;
        return nullptr;
    };
    double T = 0;
    std::string l;
    while (std::getline(in, l))
    {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        auto w = words(l);
        if (w.empty() || w[0][0] == '#') continue;
        if (w[0][0] == '@')
        {
            std::string who = w[0].substr(1);
            std::string rest = l.substr(l.find(w[0]) + w[0].size());
            while (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
            auto w2 = words(rest);
            if (w2.empty()) continue;
            for (auto& d : N)
            {
                if (who != "all" && who != d.nm) continue;
                d.F.t = T;
                enter(d);
                cmd(d.F, rest, w2, root);
                leaveNode(d);
            }
            continue;
        }
        const std::string& c = w[0];
        if (c == "run")
        {
            double end = T + num(w[1]) - 1e-9;
            while (T < end)
            {
                T = std::round((T + 0.1) * 10) / 10;
                for (auto& d : N)
                {
                    if (!d.up) continue;
                    d.F.t = T;
                    enter(d);
                    tick100();
                    d.x1 = x1Of() + d.inject;
                    leaveNode(d);
                }
                Node& h = N[0];
                for (int k = 1; k < n; ++k)
                {
                    Node& d = N[(size_t)k];
                    int v = k - 1;
                    h.F.vdown[v] = !d.up;
                    if (d.up && !d.drop)
                    {
                        setView(h.F, v, tagsOf(d.x1));
                        h.F.vpos[v] = d.F.acts[0].pos;
                        setView(d.F, 0, tagsOf(h.x1));
                        d.F.vpos[0] = h.F.acts[0].pos;
                    }
                }
            }
        }
        else if (c == "mark") emit("MARK t=" + w[1]);
        else if (c == "leave" || c == "join")
        {
            Node* d = byName(w[1]);
            if (!d || d == &N[0])
            {
                emit("BAD " + l);
                continue;
            }
            int v = (int)(d - &N[0]) - 1;
            if (c == "leave")
            {
                d->up = false;
                N[0].F.vdown[v] = true;
                N[0].F.vparts[v].clear();
            }
            else
            {
                enter(*d);
                reset();
                leaveNode(*d);
                d->st = nullptr;
                d->F.parts.clear();
                d->up = true;
                N[0].F.vsess[v] += 1;
                N[0].F.vdown[v] = false;
            }
            emit(std::string(c == "leave" ? "LEAVE " : "JOIN ") + w[1]);
        }
        else if (c == "restart")
        {
            Node* d = byName(w[1]);
            if (!d)
            {
                emit("BAD " + l);
                continue;
            }
            enter(*d);
            reset();
            leaveNode(*d);
            d->st = nullptr;
            d->F.parts.clear();
            d->F.vparts.clear();
            d->F.pawnGen += 1;
            if (d != &N[0]) N[0].F.vsess[(int)(d - &N[0]) - 1] += 1;
            emit("RESTART " + w[1]);
        }
        else if (c == "drop")
        {
            Node* d = byName(w[1]);
            if (d) d->drop = w[2] == "1";
        }
        else if (c == "inject")
        {
            Node* d = byName(w[1]);
            if (d) d->inject = w[2] == "-" ? std::string() : w[2];
        }
        else if (c == "x1")
        {
            Node* d = byName(w[1]);
            if (d) emit("X1 " + w[1] + " " + d->x1);
        }
        else emit("BAD " + l);
    }
    for (auto& d : N)
    {
        enter(d);
        reset();
        leaveNode(d);
    }
    std::ofstream o(root + "run.log", std::ios::binary);
    for (auto& s2 : g_out) o << s2 << "\n";
    return 0;
}

int runScenario(const std::string& scen, const std::string& root, const std::string& lootTsv, const std::string& qTsv)
{
    g_root = root;
    Fake F;
    F.lootTsv = lootTsv;
    F.qTsv = qTsv;
    F.dropTsv = dropOf(qTsv);
    F.shareTsv = dataOf(qTsv, "s2u_qshare.tsv");
    bind(&F);
    reset();
    std::ifstream in(scen, std::ios::binary);
    if (!in) return 2;
    std::string l;
    while (std::getline(in, l))
    {
        if (!l.empty() && l.back() == '\r') l.pop_back();
        auto w = words(l);
        if (w.empty() || w[0][0] == '#') continue;
        if (w[0] == "nodes") return runMc(in, (int)num(w[1]), root, lootTsv, qTsv);
        cmd(F, l, w, root);
    }
    std::ofstream o(root + "run.log", std::ios::binary);
    for (auto& s : g_out) o << s << "\n";
    return 0;
}

int fails = 0;
void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}

int unit()
{
    using namespace pure;
    auto xi = xItems("A545Ax30|MedkitCx2|badx");
    check(xi.size() == 2 && xi[0].sid == "A545A" && xi[0].n == 30 && xi[1].n == 2, "xItems");
    check(fmt0(2.5) == "2" || fmt0(2.5) == "3", "fmt0");
    check(lockId("BP_Box_C", 123.4, -56.7, 0) == "BP_Box_C@12,-6,0", "lockId");
    Counts a{{"A", 1}, {"B", 2}}, b{{"A", 3}, {"B", 2}, {"C", 1}};
    check(invDelta(a, b) == "Ax2,Cx1", "invDelta");
    check(invDelta(b, a) == "", "invDelta none");
    check(clEnc({}) == "-", "clEnc empty");
    check(clEnc({{"B", 2}, {"A", 1}}) == "A*1.B*2", "clEnc order");
    Counts d = clDec("A*1.B*2");
    check(d.size() == 2 && d["A"] == 1 && d["B"] == 2, "clDec");
    check(clDec("-").empty(), "clDec dash");
    Counts s = clAdd({{"A", 1}}, {{"A", 2}, {"B", 1}});
    check(s["A"] == 3 && s["B"] == 1, "clAdd");
    long long kx, ky, kz;
    std::string kp;
    check(keyParts("BP_Box_C@12,-6,0", kp, kx, ky, kz) && kp == "BP_Box_C@" && kx == 12 && ky == -6 && kz == 0, "keyParts");
    check(keyClose("P@1,2,3", "P@2,2,3", true, 1) && !keyClose("P@1,2,3", "P@3,2,3", true, 1) && !keyClose("P@1,2,3", "P@2,2,3", false, 1), "keyClose");
    std::set<std::string> m{"P@2,2,3"};
    check(keyGet(m, "P@1,2,3", true, 1).value_or("") == "P@2,2,3" && !keyGet(m, "P@1,2,3", false, 1), "keyGet");
    auto nb = nodeBits("S2U_P_", "Medkit", 1500);
    check(nb.size() == 17 && nb[0] == "S2U_P_Medkit_512", "nodeBits");
    double x, y, z;
    check(idCoords("BP_Box_C@12,-6,0", x, y, z) && x == 12 && y == -6 && z == 0, "idCoords");
    check(tokOk("Ab_1-2.3") && !tokOk("a b") && !tokOk(""), "tokOk");
    check(stripWs(" a b\t\n") == "ab", "stripWs");
    check(qgGive(3, 3, 0) == 3 && qgGive(3, 3, 3) == 0 && qgGive(3, 4, 2) == 2 && qgGive(1, 1, 5) == 0 && qgGive(2, 5, 4) == 1, "qgGive");
    std::string qk = qgKey("qc", "wBP_Box_C@1,2,3", "EchoE01", 2, 2, "c1");
    check(qk == "qc@wBP_Box_C@1,2,3;EchoE01;2;2;c1", "qgKey");
    std::string k1, k2, k3, k4;
    long long n1 = 0, n2 = 0;
    check(qgParse(qk, k1, k2, k3, n1, n2, k4) && k1 == "qc" && k2 == "wBP_Box_C@1,2,3" && k3 == "EchoE01" && n1 == 2 && n2 == 2 && k4 == "c1", "qgParse");
    check(qgParse("qg@bh7;EchoE01;1;1;h.1", k1, k2, k3, n1, n2, k4) && k1 == "qg" && k4 == "h.1", "qgParse qg");
    check(!qgParse("qg@bh7;EchoE01;0;1;h.1", k1, k2, k3, n1, n2, k4), "qgParse n0");
    check(!qgParse("qg@bh7;Echo E01;1;1;h", k1, k2, k3, n1, n2, k4) && !qgParse("qg@bh7;EchoE01;1;1;h|x", k1, k2, k3, n1, n2, k4), "qgParse bad chars");
    check(!qgParse("qg@b|7;EchoE01;1;1;h", k1, k2, k3, n1, n2, k4) && !qgParse("qg@bh7;EchoE01;1;1", k1, k2, k3, n1, n2, k4) && !qgParse("qx@bh7;EchoE01;1;1;h", k1, k2, k3, n1, n2, k4), "qgParse shape");
    check(!qgParse("qg@bh7;EchoE01;1x;1;h", k1, k2, k3, n1, n2, k4) && !qgParse("qg@bh7;EchoE01;1234567890;1;h", k1, k2, k3, n1, n2, k4), "qgParse digits");
    Counts led;
    ssOps(led, "+A545Ax30,+MedkitCx2,-A545Ax10");
    check(led["A545A"] == 20 && led["MedkitC"] == 2, "ssOps");
    std::printf("unit: %s\n", fails ? "FAIL" : "ok");
    return fails;
}

std::string slurp(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int roleSwitch()
{
    std::filesystem::path rp = std::filesystem::temp_directory_path() / "nola_items_role";
    std::filesystem::create_directories(rp);
    std::string root = rp.string() + "\\";
    auto put = [&](const char* n, const std::string& t) { std::ofstream(root + n, std::ios::binary | std::ios::trunc) << t; };
    int before = fails;
    for (int pass = 0; pass < 2; ++pass)
    {
        bool on = pass == 0;
        put("stash_ledger.txt", "1.0\t+HostKitx5\n");
        put("art_taken.txt", "2\t99999\n");
        put("s2u_artspawn.tsv", "A1\tone\t100\t200\t300\nB2\ttwo\t5000\t6000\t700\n");
        put("taken.txt", "");
        g_root = root;
        Fake F;
        F.isHost = false;
        F.fl["artsync"] = FlagV{true, 2, ""};
        F.fl["rolereset"] = FlagV{true, on ? 1.0 : 0.0, ""};
        bind(&F);
        reset();
        std::string c = stashNote("+ClientKitx1");
        outArt();
        F.isHost = true;
        roleChanged();
        std::string h = stashNote("+NewKitx2");
        bool art = artPlaceTaken(1, "host");
        std::string at = slurp(root + "art_taken.txt");
        std::string led = slurp(root + "stash_ledger.txt");
        if (on)
        {
            check(c == "ClientKitx1", "role: client stash before the switch");
            check(h == "HostKitx5,NewKitx2", "role: client -> host reloads the host stash book");
            check(art && at.find("2\t") != std::string::npos && at.find("1\t") != std::string::npos, "role: client -> host keeps the art_taken.txt rows on the next save");
            check(led.find("+HostKitx5") != std::string::npos && led.find("+NewKitx2") != std::string::npos, "role: host stash ledger keeps the old ops");
            F.isHost = false;
            roleChanged();
            check(stashNote("+Ax1") == "Ax1", "role: host -> client drops the host book from the client stash");
        }
        else
            check(h == "ClientKitx1,NewKitx2" && at.find("2\t") == std::string::npos, "role: rolereset=0 keeps the old behaviour");
        reset();
        bind(nullptr);
    }
    std::printf("role: %s\n", fails > before ? "FAIL" : "ok");
    return fails - before;
}

class GroundWorld : public Fake
{
  public:
    std::map<std::string, std::string> files;
    bool hideFails = false;
    GroundWorld()
    {
        mcWorld = true;
        acts[0].pos = {5000, 100, 0};
        for (const char* k : {"ground", "taken", "loot"}) fl[k] = FlagV{true, 1, ""};
        bind(this);
        reset();
    }
    ~GroundWorld() { reset(); bind(nullptr); }
    bool readText(const std::string& n, std::string& out) override
    {
        auto i = files.find(n);
        if (i == files.end()) return false;
        out = i->second;
        return true;
    }
    bool writeText(const std::string& n, const std::string& s) override { files[n] = s; return true; }
    bool appendText(const std::string& n, const std::string& s) override { files[n] += s; return true; }
    bool dataText(const std::string&, std::string&) override { return false; }
    bool setHidden(Obj o, bool on) override { return !hideFails && Fake::setHidden(o, on); }
    int item(double x, const std::string& mesh = "Bandage", const std::string& cls = "BP_ItemContainer_C")
    {
        Act a;
        a.name = "ground_" + std::to_string(acts.size());
        a.cls = cls;
        a.mesh = mesh;
        a.pos = {x, 300, 10};
        a.cont = true;
        acts.push_back(a);
        return (int)acts.size() - 1;
    }
    void step(double at) { t = at; tick100(); }
};

int groundIdentity()
{
    int before = fails;
    const std::string take = "1;5200,300,10;BP_ItemContainer_C;Bandage";
    for (double gap : {14.0, 18.0, 100.0})
    {
        GroundWorld w;
        int a = w.item(5200), b = w.item(5200 + gap);
        w.ia = a;
        w.step(1);
        w.acts[a].live = false;
        w.ia = -1;
        w.step(2);
        w.step(7);
        w.step(12);
        check(!w.acts[b].hid, "ground: own pickup never sweeps a neighbour");
        check(w.files["taken.txt"].find("item@5200,300,10;") != std::string::npos, "ground: own pickup remains recorded");
    }
    for (bool host : {false, true})
    {
        GroundWorld w;
        w.isHost = host;
        int a = w.item(5204), b = w.item(5214);
        w.parts["G"] = take;
        w.step(1);
        check(w.acts[a].hid && !w.acts[b].hid, "ground: remote take tolerates settling without hiding its neighbour");
        w.parts.clear();
        w.step(7);
        check(!w.acts[b].hid, "ground: later sweep cannot select the next item");
        if (host)
        {
            w.acts[a].live = false;
            int streamed = w.item(5204);
            w.step(13);
            check(w.acts[streamed].hid && !w.acts[b].hid, "ground: streamed copy is still removed by its saved key");
        }
    }
    {
        GroundWorld w;
        int b = w.item(5218);
        w.parts["G"] = take;
        w.step(1);
        check(!w.acts[b].hid, "ground: missing copy cannot consume a neighbour");
        w.parts.clear();
        int a = w.item(5208);
        w.step(4);
        check(w.acts[a].hid && !w.acts[b].hid, "ground: pending take resolves when the actual copy appears");
    }
    {
        GroundWorld w;
        int a = w.item(5207), b = w.item(5192);
        w.parts["G"] = take;
        w.step(1);
        w.step(7);
        check(!w.acts[a].hid && !w.acts[b].hid, "ground: nearly equidistant copies hide neither actor");
    }
    {
        GroundWorld w;
        int a = w.item(5200, "Water"), b = w.item(5200, "Bandage", "OtherItemContainer_C"), c = w.item(5216);
        w.parts["G"] = take;
        w.step(1);
        w.step(7);
        check(!w.acts[a].hid && !w.acts[b].hid && !w.acts[c].hid, "ground: mesh, class and out-of-range copies stay distinct");
    }
    {
        GroundWorld w;
        w.files["taken.txt"] = "item@5200,300,10;BP_ItemContainer_C;Bandage\n";
        int a = w.item(5204), b = w.item(5214);
        w.step(1);
        w.acts[a].hid = false;
        w.step(7);
        check(w.acts[a].hid && !w.acts[b].hid, "ground: legacy taken key re-hides its settled copy without sweeping a neighbour");
    }
    {
        GroundWorld w;
        w.mcOn = true;
        w.nviews = 3;
        w.fl["mcitems"] = FlagV{true, 1, ""};
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.vparts[1]["G"] = "1;5300,300,10;BP_ItemContainer_C;Bandage";
        w.step(2);
        check(outGR().find("5300,300,10") != std::string::npos && status().find("gdup=0") != std::string::npos,
              "ground: different nearby items can belong to different players");
        w.vparts[1].clear();
        w.vparts[2]["G"] = take;
        w.step(3);
        check(status().find("gdup=1") != std::string::npos && outGR().find("3.0;") == std::string::npos,
              "ground: a second player taking the same key is still a duplicate");
    }
    for (const std::string cls : {"BP_ItemContainer_C", "BP_StaticItemContainer_C"})
    for (bool host : {false, true})
    for (double offset : {1.0, 3.0, 8.0, 13.0, 15.0})
    {
        GroundWorld w;
        w.isHost = host;
        int a = w.item(5200 + offset, "Bandage", cls), b = w.item(5230, "Bandage", cls);
        w.parts["G"] = "1;5200,300,10;" + cls + ";Bandage";
        w.step(1);
        w.parts.clear();
        w.step(7);
        check(w.acts[a].hid && !w.acts[b].hid, "ground: real settling offsets match once on either role");
    }
    for (double offset : {15.01, 16.0, 124.0})
    {
        GroundWorld w;
        int a = w.item(5200 + offset);
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.step(7);
        check(!w.acts[a].hid, "ground: outliers never widen the search radius");
    }
    for (bool reverse : {false, true})
    {
        GroundWorld w;
        int a = w.item(reverse ? 5192 : 5205), b = w.item(reverse ? 5205 : 5192);
        w.parts["G"] = take;
        w.step(1);
        w.step(7);
        check(!w.acts[a].hid && !w.acts[b].hid, "ground: 3 cm ambiguity margin is independent of enumeration order");
    }
    {
        GroundWorld w;
        int a = w.item(5200), b = w.item(5200.3);
        w.acts[a].hid = true;
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.step(7);
        w.acts[a].live = false;
        w.step(10);
        check(!w.acts[b].hid, "ground: hidden nearest copy cannot redirect a take to a visible sibling");
    }
    for (bool host : {false, true})
    for (int offset : {1, 4, 8})
    {
        GroundWorld w;
        w.isHost = host;
        int a = w.item(5200), b = w.item(5214);
        w.ia = a;
        w.step(1);
        w.acts[a].live = false;
        w.ia = -1;
        w.step(2);
        w.parts["G"] = "2;" + std::to_string(5200 + offset) + ",300,10;BP_ItemContainer_C;Bandage";
        w.step(3);
        w.parts.clear();
        w.step(7);
        w.step(64);
        check(!w.acts[b].hid, "ground: late take of a locally collected copy cannot consume a neighbour");
    }
    {
        GroundWorld w;
        int a = w.item(5204), b = w.item(5214);
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.acts[a].live = false;
        w.acts[b].pos.x = 5201;
        w.step(7);
        check(!w.acts[b].hid, "ground: resolved key remains bound when a neighbour moves closer");
        w.acts[b].pos.x = 5230;
        int reloaded = w.item(5208);
        ++w.wkGen;
        w.step(13);
        check(w.acts[reloaded].hid, "ground: new world resolves saved keys with fresh settling positions");
    }
    for (int offset : {1, 8, 13})
    {
        GroundWorld w;
        w.mcOn = true;
        w.nviews = 2;
        w.fl["mcitems"] = FlagV{true, 1, ""};
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.vparts[1]["G"] = "1;" + std::to_string(5200 + offset) + ",300,10;BP_ItemContainer_C;Bandage";
        w.step(2);
        check(status().find("gdup=1") != std::string::npos && outGR().find("2.0;") == std::string::npos,
              "ground: ownership tolerates real cross-PC offsets when the local copy is absent");
    }
    {
        GroundWorld w;
        w.mcOn = true;
        w.nviews = 3;
        w.fl["mcitems"] = FlagV{true, 1, ""};
        int a = w.item(5204), b = w.item(5218);
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.vparts[1]["G"] = "1;5215,300,10;BP_ItemContainer_C;Bandage";
        w.step(2);
        check(w.acts[a].hid && w.acts[b].hid && status().find("gdup=0") != std::string::npos,
              "ground: two nearby actors retain separate owners despite overlapping tolerances");
        w.vparts[1].clear();
        w.vparts[2]["G"] = "1;5208,300,10;BP_ItemContainer_C;Bandage";
        w.step(3);
        check(status().find("gdup=1") != std::string::npos && outGR().find("3.0;") == std::string::npos,
              "ground: settled copy still has its first owner after a neighbour is taken");
    }
    {
        GroundWorld w;
        int a = w.item(5214), b = w.item(5216);
        w.parts["G"] = take;
        w.step(1);
        check(!w.acts[a].hid && !w.acts[b].hid, "ground: near tie just outside the radius is still ambiguous");
    }
    {
        GroundWorld w;
        w.mcOn = true;
        w.nviews = 3;
        w.fl["mcitems"] = FlagV{true, 1, ""};
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.vparts[1]["G"] = "1;5230,300,10;BP_ItemContainer_C;Bandage";
        w.step(2);
        w.vparts[1].clear();
        w.vparts[2]["G"] = "1;5215,300,10;BP_ItemContainer_C;Bandage";
        w.step(3);
        check(status().find("gdup=0") != std::string::npos && outGR().find("3.0;") == std::string::npos,
              "ground: ambiguous ownership neither invents a duplicate nor relays a guessed take");
    }
    for (bool role : {false, true})
    {
        GroundWorld w;
        int a = w.item(5204);
        w.parts["G"] = take;
        w.step(1);
        w.parts.clear();
        w.acts[a].live = false;
        int b = w.item(5208);
        if (role) roleChanged(); else campaignRotated();
        w.step(7);
        check(w.acts[b].hid, "ground: role/campaign reset discards local bindings before reading saved keys");
    }
    {
        GroundWorld w;
        int a = w.item(5200);
        w.acts[a].pos.y += 8;
        w.acts[a].pos.z += 8;
        w.parts["G"] = take;
        w.step(1);
        check(w.acts[a].hid, "ground: matching uses three-dimensional distance");
    }
    {
        GroundWorld w;
        int a = w.item(5204), b = w.item(5214);
        w.hideFails = true;
        w.parts["G"] = take;
        w.step(1);
        check(!w.acts[a].hid && !w.acts[b].hid, "ground: failed hide changes neither copy");
        w.parts.clear();
        w.hideFails = false;
        w.step(4);
        check(w.acts[a].hid && !w.acts[b].hid, "ground: failed hide remains retryable, never recorded as consumed");
    }
    {
        GroundWorld w;
        w.mcOn = true;
        w.nviews = 2;
        w.fl["mcitems"] = FlagV{true, 1, ""};
        w.item(5204);
        w.item(5218);
        w.parts["G"] = take;
        w.step(1);
        w.parts["G"] = "2;5215,300,10;BP_ItemContainer_C;Bandage";
        w.step(2);
        w.parts.clear();
        w.vparts[1]["G"] = "1;5219,300,10;BP_ItemContainer_C;Bandage";
        w.step(3);
        check(status().find("gdup=1") != std::string::npos && outGR().find("2.0;") == std::string::npos,
              "ground: one player taking neighbours records both ownerships");
    }
    std::printf("ground identity: %s\n", fails > before ? "FAIL" : "ok");
    return fails - before;
}
}

int main(int argc, char** argv)
{
    if (argc >= 5) return runScenario(argv[1], argv[2], argv[3], argv[4]);
    int u = unit();
    int r = roleSwitch();
    int g = groundIdentity();
    return u || r || g ? 1 : 0;
}
