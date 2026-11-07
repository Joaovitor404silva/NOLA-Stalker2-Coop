#include "../../src/s2m_pop.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace s2m_pop;

struct P {
    V3 at;
    bool alive = true, hidden = false, coll = true, human = true, threat = false, focus = false, inter = true, destroyed = false;
    double maxhp = 100;
    std::string cls = "BP_AI_Human_C", name;
    long long uid = 0, proto = 0;
    std::string story, mesh;
    bool nat = false, listed = false, own = false;
};

struct Fake : Env {
    double t = 0;
    std::map<std::string, double> flags;
    std::map<Id, P> ps;
    std::vector<std::string> logs;
    std::map<std::string, std::string> files;
    Id lp = 1;
    bool hasLp = true;
    bool host = false;
    bool aiOk = true;
    std::vector<std::string> calls;
    Body tg, pu;
    std::string wk = "w1";
    std::vector<Cont> conts;
    bool invOpen = false;
    bool traceRan = true, traceHit = true;
    double traceZ = 0;
    std::set<long long> listedUid;
    std::map<Id, long long> inr;
    bool inertOk = true;
    int inOff = 0;

    double now() override { return t; }
    double flag(const char* n, double d) override { auto i = flags.find(n); return i == flags.end() ? d : i->second; }
    bool isHost() override { return host; }
    void log(const std::string& l) override { logs.push_back(l); }
    bool readFile(const char* n, std::string& o) override { auto i = files.find(n); if (i == files.end()) return false; o = i->second; return true; }
    bool localPawn(Id& id, V3& at) override { if (!hasLp) return false; id = lp; at = ps[lp].at; return true; }
    void pawns(PawnClass c, std::vector<Id>& out) override { for (auto& kv : ps) if (c == PawnClass::All || kv.second.human) out.push_back(kv.first); }
    bool valid(Id p) override { return ps.count(p) && !ps[p].destroyed; }
    bool alive(Id p) override { return ps[p].alive; }
    bool loc(Id p, V3& at) override { at = ps[p].at; return true; }
    bool hiddenFlag(Id p) override { return ps[p].hidden; }
    bool maxHp(Id p, double& h) override { h = ps[p].maxhp; return true; }
    std::string className(Id p) override { return ps[p].cls; }
    std::string shortName(Id p) override { return ps[p].name; }
    std::string meshName(Id p, const char*) override { return ps.count(p) ? ps[p].mesh : ""; }
    bool ownBodyBase(Id p) override { return ps[p].own; }
    bool natOwn(Id p) override { return ps[p].nat; }
    bool natListed(Id p) override { return ps[p].listed; }
    bool natListedUid(long long u) override { return listedUid.count(u) != 0; }
    bool inert(Id p, long long u, bool on) override
    {
        if (!on) { inOff += (int)inr.erase(p); return true; }
        if (!inertOk) return false;
        if (!inr.count(p) || u) inr[p] = u;
        return true;
    }
    bool storyNpc(Id p, std::string& g) override { g = ps[p].story; return !g.empty(); }
    bool setHidden(Id p, bool h) override { ps[p].hidden = h; return true; }
    bool setCollision(Id p, bool c) override { ps[p].coll = c; return true; }
    bool setInteractionOff(Id p) override { ps[p].inter = false; return true; }
    bool destroyActor(Id p) override { ps[p].destroyed = true; return true; }
    bool aiFeatures(bool off, long long m) override { calls.push_back(std::string(off ? "deact " : "act ") + std::to_string(m)); return aiOk; }
    bool aLife(bool off) override { calls.push_back(off ? "alife off" : "alife on"); return aiOk; }
    bool inThreat(Id p) override { return ps[p].threat; }
    bool focusIsLocal(Id p) override { return ps[p].focus; }
    bool guid(Id p, long long& u) override { u = ps[p].uid; return u != 0; }
    bool protoId(Id p, long long& i) override { i = ps[p].proto; return i != 0; }
    bool killByUid(long long u) override { calls.push_back("kill " + std::to_string(u)); return true; }
    bool destroyModel(long long u) override { calls.push_back("destroymodel " + std::to_string(u)); return true; }
    bool despawnDeadAtRadius(double r) override { calls.push_back("despawn " + std::to_string((long long)r)); return true; }
    Body target() override { return tg; }
    Body puppet() override { return pu; }
    int nv = 1;
    std::set<int> live = {0};
    std::map<int, Body> tgv, puv;
    int bodyViews() override { return nv; }
    bool bodyViewLive(int v) override { return live.count(v) != 0; }
    Body target(int v) override { return v == 0 ? tg : tgv.count(v) ? tgv[v] : Body{}; }
    Body puppet(int v) override { return v == 0 ? pu : puv.count(v) ? puv[v] : Body{}; }
    bool worldKey(std::string& k) override { k = wk; return true; }
    bool worldOpen() override { return true; }
    bool clPending() override { return false; }
    bool inventoryOpen() override { return invOpen; }
    void containers(std::vector<Cont>& o) override { o = conts; }
    bool trace(double, double, double, double, double, double, bool& h, double& z) override { h = traceHit; z = traceZ; return traceRan; }
    bool simPhysicsOff(Id) override { return true; }
    bool setLoc(Id id, double x, double y, double z) override { for (auto& c : conts) if (c.id == id) { c.x = x; c.y = y; c.z = z; } return true; }
};

int fails = 0;
void check(bool c, const char* what)
{
    if (!c) { std::printf("FAIL %s\n", what); fails++; } else std::printf("ok   %s\n", what);
}

bool has(const Fake& f, const std::string& sub)
{
    for (auto& l : f.logs) if (l.find(sub) != std::string::npos) return true;
    return false;
}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : "D:/modding/cpp/S2CoopPort/data";
    std::ifstream in(dir + "/s2u_vendor.tsv", std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string vtsv = ss.str();

    {
        Fake f;
        init(&f);
        size_t n = loadVendorRows(vtsv);
        check(n == 96, "vendor tsv loads 96 rows");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["alifekeepnat"] = 0;
        f.files["s2u_vendor.tsv"] = vtsv;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 0}; f.ps[1].name = "BP_Player";
        P far; far.at = {900000, 900000, 0}; far.name = "Far_1"; far.cls = "BP_AI_Human_C"; far.maxhp = 100;
        P hub; hub.at = {421180, 412370, 0}; hub.name = "Hub_1";
        P own; own.at = {5, 5, 0}; own.own = true; own.name = "Own_1";
        P dog; dog.at = {800000, 800000, 0}; dog.name = "Dog_1"; dog.cls = "BP_AI_Dog_C"; dog.maxhp = 150; dog.threat = true; dog.uid = 777;
        f.ps[2] = far; f.ps[3] = hub; f.ps[4] = own; f.ps[5] = dog;
        init(&f);
        tick1000();
        check(f.calls.empty(), "first beat does nothing (beat%2)");
        tick1000();
        check(f.calls.size() >= 2 && f.calls[0] == "deact 203647" && f.calls[1] == "alife off", "AI features off once");
        check(has(f, "NOLA alife: DESLIGADO (AIFeature 203647, XALifeDisable) ok=true,true"), "log DESLIGADO");
        check(f.ps[2].hidden && !f.ps[2].coll, "far NPC hidden, no collision");
        check(!f.ps[3].hidden, "hub NPC kept");
        check(!f.ps[4].hidden, "own body untouched");
        check(isHiddenLocal(2) && !isHiddenLocal(3), "hidden set");
        check(has(f, "NOLA alife: 2 NPC(s) locais escondidos e sem colisao [BP_AI_Human_C BP_AI_Dog_C] (vivos nao nossos agora=3, escondidos no total=2)"), "hide log");
        check(has(f, "NOLA hidnpc: NPC local escondido Dog_1 (em ameaca, 11314 m, maxhp 150) -> XKillNpcByUID(777) ok=true #1"), "hidnpc kill log");
        check(f.calls.size() == 3 && f.calls[2] == "kill 777", "hidnpc killed by uid");
        f.t = 2;
        tick1000(); tick1000();
        check(f.calls.size() == 3, "hidnpc retry waits 5 s");
        f.t = 10;
        tick1000(); tick1000();
        check(f.calls.size() == 4 && f.calls[3] == "destroymodel 777", "hidnpc second try uses the other call");
        f.t = 20;
        tick1000(); tick1000();
        check(f.calls.size() == 4, "hidnpc stops after 2 tries");
        check(has(f, "ainda vivo depois de 2 tentativas"), "give up log");
        f.ps[2].alive = false; f.ps[2].hidden = false;
        f.t = 30;
        tick1000(); tick1000();
        check(f.ps[2].hidden, "dead hidden NPC stays hidden");
        check(has(f, "NOLA alife: NPC local morto escondido de novo Far_1"), "dead rehide log");
        f.flags["alifeoff"] = 0;
        f.t = 40;
        tick1000(); tick1000();
        check(f.calls.size() == 6 && f.calls[4] == "act 4194303", "AI back on");
        check(!f.ps[5].hidden && f.ps[5].coll, "NPCs shown again");
        check(has(f, "LIGADO (AIFeature 203647, XALifeEnable) ok=true,true npcs_mostrados=3"), "log LIGADO");
        check(f.inr.empty(), "AI back on: no NPC left inert");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["alifekeepnat"] = 0;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 0}; f.ps[1].name = "BP_Player";
        P st; st.at = {3000, 0, 0}; st.name = "Bandit_Story"; st.threat = true; st.uid = 501; st.story = "CF5DB084";
        P calm; calm.at = {900000, 900000, 0}; calm.name = "Calm_1"; calm.uid = 502;
        P nou; nou.at = {4000, 0, 0}; nou.name = "NoUid_1"; nou.threat = true; nou.cls = "BP_AI_Poltergeist_C";
        P boss; boss.at = {5000, 0, 0}; boss.name = "Boss_1"; boss.maxhp = 9000; boss.uid = 504; boss.threat = true;
        P lst; lst.at = {6000, 0, 0}; lst.name = "Listed_1"; lst.uid = 505; lst.threat = true;
        P hub; hub.at = {421180, 412370, 0}; hub.name = "Hub_1"; hub.uid = 506; hub.threat = true;
        P own; own.at = {5, 5, 0}; own.nat = true; own.name = "Copy_1"; own.uid = 507; own.threat = true;
        f.ps[2] = st; f.ps[3] = calm; f.ps[4] = nou; f.ps[5] = boss; f.ps[6] = lst; f.ps[7] = hub; f.ps[8] = own;
        f.listedUid.insert(505);
        init(&f);
        tick1000(); tick1000();
        check(f.inr.count(2) && f.inr[2] == 501, "story NPC in combat is inert (uid passed)");
        check(f.inr.count(3) && f.inr[3] == 502, "calm hidden NPC is inert too");
        check(f.inr.count(4) && f.inr[4] == 0, "no-uid NPC (poltergeist) is inert without uid");
        check(f.inr.count(5) && f.inr.count(6), "boss and host-listed hidden NPCs are inert");
        check(!f.inr.count(7) && !f.inr.count(8) && !f.inr.count(1), "kept hub NPC, native copy and local player never inert");
        bool killSafe = false;
        for (auto& c : f.calls) if (c == "kill 501" || c == "destroymodel 501" || c == "kill 505" || c == "kill 504") killSafe = true;
        check(!killSafe, "story, listed and boss NPCs are not killed");
        check(has(f, "NOLA clinert: NPC local escondido Bandit_Story (BP_AI_Human_C) inerte: categoria=historia uid=501 s2u_choice=CF5DB084"), "story inert log");
        check(has(f, "NOLA hidnpc: Bandit_Story (uid 501) categoria=historia (NPC de historia: matar dispararia eventos de quest locais, s2u_choice CF5DB084): nao morto, fica escondido e inerte"), "story hidnpc log");
        check(has(f, "NOLA clinert: NPC local escondido NoUid_1 (BP_AI_Poltergeist_C) inerte: categoria=sem_uid uid=0"), "no-uid inert log");
        size_t n = f.logs.size();
        f.t = 2;
        tick1000(); tick1000();
        int again = 0;
        for (size_t i = n; i < f.logs.size(); ++i) if (f.logs[i].find("NOLA clinert: NPC local escondido") != std::string::npos) again++;
        check(again == 0, "inert log once per NPC");
        f.ps[2].alive = false;
        f.ps[3].nat = true;
        f.t = 4;
        tick1000(); tick1000();
        check(!f.inr.count(2), "dead NPC dropped from inert set");
        check(!f.inr.count(3), "NPC adopted as copy leaves the inert set");
        f.ps.erase(4);
        f.t = 6;
        tick1000(); tick1000();
        check(!f.inr.count(4) && f.inr.count(5), "gone NPC leaves the inert set, the others stay");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["clinert"] = 0;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 0}; f.ps[1].name = "BP_Player";
        P st; st.at = {3000, 0, 0}; st.name = "Bandit_Story"; st.threat = true; st.uid = 501; st.story = "CF5DB084";
        f.ps[2] = st;
        init(&f);
        tick1000(); tick1000();
        check(f.inr.empty(), "clinert=0: nothing inert");
        check(f.ps[2].hidden, "clinert=0: still hidden");
        check(has(f, "categoria=historia") && has(f, "nao morto, fica so escondido"), "clinert=0: story NPC only hidden (old behaviour)");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.inertOk = false;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 0}; f.ps[1].name = "BP_Player";
        P st; st.at = {3000, 0, 0}; st.name = "Bandit_Story"; st.uid = 501; st.story = "CF5DB084";
        f.ps[2] = st;
        init(&f);
        tick1000(); tick1000();
        check(!has(f, "NOLA clinert: NPC local escondido"), "native refused (host or flag off): no inert log");
        f.inertOk = true;
        f.t = 2;
        tick1000(); tick1000();
        check(f.inr.count(2) && has(f, "NOLA clinert: NPC local escondido Bandit_Story"), "inert retried once native accepts");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["alifekeepnat"] = 0;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 0}; f.ps[1].name = "BP_Player";
        P q; q.at = {3000, 0, 0}; q.name = "Bandit_Quest"; q.uid = 601;
        P pl; pl.at = {4000, 0, 0}; pl.name = "Pool_1"; pl.uid = 602;
        P re; re.at = {5000, 0, 0}; re.name = "Reused_1"; re.uid = 603;
        P hub; hub.at = {421180, 412370, 0}; hub.name = "Hub_1"; hub.uid = 604;
        f.ps[2] = q; f.ps[3] = pl; f.ps[4] = re; f.ps[5] = hub;
        init(&f);
        tick1000(); tick1000();
        check(f.ps[2].hidden && !f.ps[2].coll && f.inr.count(2), "npcvis: quest NPC hidden, no collision, inert on first sight");
        check(!has(f, "NOLA npcvis"), "npcvis: no re-hide log on first sight");
        f.ps[2].hidden = false; f.ps[2].coll = true;
        f.ps[3].at = {50, 50, 0}; f.ps[3].hidden = false;
        f.ps[4].uid = 699; f.ps[4].hidden = false;
        f.t = 2;
        tick1000(); tick1000();
        check(f.ps[2].hidden && !f.ps[2].coll && f.inr.count(2), "npcvis: NPC unhidden by the game is hidden again, collision off, still inert");
        check(has(f, "NOLA npcvis: local NPC Bandit_Quest (BP_AI_Human_C) uid=601 (was 601) visible again on the client (reason=unhidden): hidden again, collision off, inert refreshed #1"), "npcvis: unhidden log");
        check(f.ps[4].hidden && f.inr.count(4) && f.inr[4] == 699 && has(f, "Reused_1 (BP_AI_Human_C) uid=699 (was 603) visible again on the client (reason=new_uid)"), "npcvis: actor reused for a new unit is hidden again and inert with the new uid");
        check(!f.ps[3].hidden && !has(f, "NOLA npcvis: local NPC Pool_1"), "npcvis: actor parked at the pool is left alone");
        f.ps[3].at = {7000, 0, 0};
        f.t = 4;
        tick1000(); tick1000();
        check(f.ps[3].hidden && !f.ps[3].coll && has(f, "Pool_1 (BP_AI_Human_C) uid=602 (was 602) visible again on the client (reason=left_pool)"), "npcvis: actor back from the pool is hidden again");
        check(!f.ps[5].hidden && f.ps[5].coll, "npcvis: kept hub NPC stays visible");
        size_t n = f.logs.size();
        f.t = 6;
        tick1000(); tick1000();
        int again = 0;
        for (size_t i = n; i < f.logs.size(); ++i) if (f.logs[i].find("NOLA npcvis") != std::string::npos) again++;
        check(again == 0, "npcvis: stable hidden NPCs give no more logs");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["npcvis"] = 0;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 0}; f.ps[1].name = "BP_Player";
        P q; q.at = {3000, 0, 0}; q.name = "Bandit_Quest"; q.uid = 601;
        f.ps[2] = q;
        init(&f);
        tick1000(); tick1000();
        f.ps[2].hidden = false; f.ps[2].coll = true;
        f.t = 2;
        tick1000(); tick1000();
        check(!f.ps[2].hidden && f.ps[2].coll && !has(f, "NOLA npcvis"), "npcvis=0: old behaviour, an unhidden NPC stays visible");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.aiOk = false;
        f.ps[1] = P(); f.ps[1].name = "BP_Player";
        init(&f);
        for (int i = 0; i < 8; ++i) tick1000();
        check(has(f, "chamadas falharam (ok=false,false) tentativa 1; tento de novo") && !has(f, "DESLIGADO"), "failed calls retried, no DESLIGADO");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["vendmir"] = 1;
        f.flags["alifekeephubs"] = 0;
        f.flags["alifekeepquest"] = 0;
        f.files["s2u_vendor.tsv"] = vtsv;
        f.ps[1] = P(); f.ps[1].name = "BP_Player";
        P v1; v1.at = {664295, 618840, 287}; v1.name = "Vend_near";
        P v2; v2.at = {667295, 618840, 287}; v2.name = "Vend_far";
        P x; x.at = {10, 10, 0}; x.name = "Rnd";
        f.ps[2] = v1; f.ps[3] = v2; f.ps[4] = x;
        init(&f);
        tick1000(); tick1000();
        check(!f.ps[2].hidden, "nearest NPC to a vendor row is kept (vendone)");
        check(f.ps[3].hidden, "second NPC near the same row hidden");
        check(f.ps[4].hidden, "other NPC hidden");
        check(isVendor(2) && !isVendor(3), "isVendor");
        VendHit info;
        check(vendorInfo(2, info) && info.kind == "bartender" && info.obj == "BanditPonuryj", "vendorInfo returns selected vendor metadata");
        check(!vendorInfo(3, info) && !vendorInfo(999, info), "vendorInfo rejects unselected and invalid actors");
        f.host = true;
        check(vendSkip(2) && !vendSkip(4), "host vendSkip");
        check(has(f, "NOLA vend: NPC em 664295,618840,287 sem espelho: bartender BanditPonuryj a 0 m"), "vendSkip log");
        check(has(f, "NOLA vend: 96 vendedores em s2u_vendor.lua"), "vend rows log");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["alifekeepnat"] = 0;
        f.flags["vendmir"] = 1;
        f.flags["alifekeephubs"] = 1;
        f.flags["alifekeepquest"] = 1;
        f.files["s2u_vendor.tsv"] = vtsv;
        f.ps[1] = P(); f.ps[1].name = "BP_Player";
        P v1; v1.at = {664295, 618840, 287}; v1.name = "Vend_near";
        P m; m.at = {664295, 618840, 287}; m.name = "Bloodsucker_local"; m.cls = "BP_AI_Bloodsucker_C"; m.human = false;
        f.ps[2] = v1; f.ps[3] = m;
        init(&f);
        tick1000(); tick1000();
        check(!f.ps[2].hidden && f.ps[2].coll, "keep zone still keeps the local human");
        check(f.ps[3].hidden && !f.ps[3].coll, "keep zone no longer keeps a local mutant");
    }

    {
        Fake f;
        f.flags["alifeoff"] = 1;
        f.flags["vendmir"] = 1;
        f.files["s2u_vendor.tsv"] = vtsv;
        f.ps[1] = P(); f.ps[1].name = "BP_Player";
        P v1; v1.at = {664295, 618840, 287}; v1.name = "Vend_near";
        P hub; hub.at = {430180, 412370, 0}; hub.name = "Hub_1";
        P tr; tr.at = {664295, 619840, 287}; tr.name = "Town_1";
        f.ps[2] = v1; f.ps[3] = hub; f.ps[4] = tr;
        init(&f);
        tick1000(); tick1000();
        check(!f.ps[2].hidden && f.ps[2].coll, "alifekeepnat=1: the vendor stays local");
        check(f.ps[3].hidden && !f.ps[3].coll, "alifekeepnat=1: a local hub human is hidden like everywhere else");
        check(f.ps[4].hidden && !f.ps[4].coll, "alifekeepnat=1: a local human near a trade place is hidden");
    }

    {
        Fake f;
        f.flags["orphsweep"] = 1;
        f.flags["orphrm"] = 3;
        f.ps[1] = P(); f.ps[1].name = "BP_Player"; f.ps[1].human = false;
        P t; t.maxhp = 5000; t.uid = 900; t.proto = 55; t.name = "Tgt_A"; t.at = {1000, 1000, 0};
        f.ps[10] = t;
        f.tg.ok = true; f.tg.pawn = 10; f.tg.hasUid = true; f.tg.uid = 900;
        P o; o.maxhp = 5000; o.uid = 900; o.proto = 55; o.name = "Tgt_B"; o.at = {2000, 2000, 0};
        f.ps[11] = o;
        P r; r.maxhp = 100; r.uid = 5; r.proto = 8; r.name = "Real_1"; r.at = {3000, 3000, 0};
        f.ps[12] = r;
        init(&f);
        f.t = 1;
        tick1000();
        check(has(f, "NOLA orfao: alvo registrado Tgt_A guid=900 prototipo=55"), "adopted body registered");
        check(f.ps[11].hidden && !f.ps[11].coll && !f.ps[11].inter, "orphan hidden, no collision, no interaction");
        check(has(f, "NOLA orfao: Tgt_B (alvo, vivo=true, maxhp=5000, 28 m, por guid 900) -> escondido=true sem_colisao=true sem_interacao=true alife=guid 900 e do corpo adotado (so escondido)"), "orphan log");
        check(!f.ps[10].hidden && !f.ps[12].hidden, "adopted and real NPC untouched");
        f.t = 10;
        P o2; o2.maxhp = 5000; o2.uid = 900; o2.proto = 55; o2.name = "Tgt_C"; o2.at = {2500, 2500, 0};
        f.ps[13] = o2;
        tick1000();
        check(f.ps[13].hidden, "second orphan with the same guid hidden");
        bool noRemove = true;
        for (auto& c : f.calls) if (c.find("destroymodel 900") != std::string::npos) noRemove = false;
        check(noRemove, "adopted guid never removed from A-Life");
        P o3; o3.maxhp = 5000; o3.uid = 900; o3.name = "Tgt_D"; o3.at = {2500, 2600, 0};
        f.ps[14] = o3;
        f.flags["orphsweep"] = 2;
        f.t = 20;
        tick1000();
        check(!f.ps[14].hidden && has(f, "TESTE: faria esconder+sem colisao+sem interacao+remocao A-Life guid=900"), "dry run does not hide");
    }

    {
        Fake f;
        f.flags["orphsweep"] = 1;
        f.flags["orphrm"] = 3;
        f.ps[1] = P(); f.ps[1].name = "BP_Player"; f.ps[1].human = false;
        P t; t.maxhp = 5000; t.uid = 900; t.proto = 55; t.name = "Tgt_A"; t.at = {1000, 1000, 0};
        f.ps[10] = t;
        f.tg.ok = true; f.tg.pawn = 10; f.tg.hasUid = true; f.tg.uid = 900;
        P o; o.maxhp = 5000; o.uid = 901; o.proto = 55; o.name = "Tgt_E"; o.at = {2000, 2000, 0};
        f.ps[11] = o;
        init(&f);
        f.t = 1;
        tick1000();
        check(!f.ps[11].hidden, "unknown guid and prototype not yet proven: not ours");
        f.ps[11].uid = 900;
        f.t = 5;
        tick1000();
        check(f.ps[11].hidden, "orphan sharing the adopted guid is hidden");
        check(has(f, "alife=guid 900 e do corpo adotado (so escondido)"), "adopted guid protected from removal");
    }

    {
        Fake f;
        f.flags["orphsweep"] = 1;
        f.flags["orphrm"] = 3;
        f.nv = 4;
        f.live = {0, 1, 2};
        f.ps[1] = P(); f.ps[1].name = "BP_Player"; f.ps[1].human = false;
        P t; t.maxhp = 5000; t.uid = 900; t.proto = 55; t.name = "Tgt_A"; t.at = {1000, 1000, 0};
        f.ps[10] = t;
        f.tg.ok = true; f.tg.pawn = 10; f.tg.hasUid = true; f.tg.uid = 900;
        P t2 = t; t2.uid = 920; t2.name = "Tgt_V1"; t2.at = {1500, 1500, 0};
        f.ps[20] = t2;
        f.tgv[1].ok = true; f.tgv[1].pawn = 20; f.tgv[1].hasUid = true; f.tgv[1].uid = 920;
        P p0; p0.maxhp = 100000; p0.uid = 950; p0.name = "Pup_V0"; p0.at = {1200, 1200, 0};
        f.ps[30] = p0;
        f.pu.ok = true; f.pu.pawn = 30; f.pu.hasUid = true; f.pu.uid = 950;
        P p1 = p0; p1.uid = 960; p1.name = "Pup_V1"; p1.at = {1300, 1300, 0};
        f.ps[31] = p1;
        f.puv[1].ok = true; f.puv[1].pawn = 31; f.puv[1].hasUid = true; f.puv[1].uid = 960;
        P o; o.maxhp = 5000; o.uid = 920; o.proto = 55; o.name = "Tgt_V1_orf"; o.at = {2000, 2000, 0};
        f.ps[21] = o;
        P po = p0; po.uid = 960; po.name = "Pup_V1_orf"; po.at = {2200, 2200, 0};
        f.ps[32] = po;
        init(&f);
        f.t = 1;
        tick1000();
        check(has(f, "NOLA orfao: alvo registrado Tgt_V1 guid=920"), "view 1 target registered");
        check(has(f, "NOLA orfao: boneco registrado Pup_V1 guid=960"), "view 1 puppet registered");
        check(!f.ps[10].hidden && !f.ps[20].hidden && !f.ps[30].hidden && !f.ps[31].hidden, "every view's bodies untouched");
        check(f.ps[21].hidden, "orphan of view 1's target hidden");
        check(!f.ps[32].hidden, "puppet orphans wait while view 2 has no puppet");
        f.live = {0, 1};
        f.t = 10;
        tick1000();
        check(f.ps[32].hidden, "puppet orphan hidden once every live view holds a puppet");
    }

    {
        Fake f;
        f.t = 100;
        init(&f);
        QGuard g;
        QSense s; s.pawn = 7;
        QRes r = qgDead(g, s, true);
        check(!r.skip, "alive and no guard: send");
        s.alive = false;
        r = qgDead(g, s, true);
        check(r.skip && r.dropPending && has(f, "jogador local morto (hp 0 / tela de morte): contagens de itens de missao NAO enviadas"), "dead: skip + drop");
        s.alive = true; s.pawn = 8;
        f.t = 104;
        r = qgDead(g, s, true);
        check(!r.skip && r.rebase && has(f, "NOLA qdead: pawn novo; base relida, nada enviado depois de 4 s: contagens voltam a ser enviadas"), "new pawn: rebase");
        s.pawn = 9; s.lifeDown = true;
        r = qgDead(g, s, false);
        check(r.skip && has(f, "jogador local caido"), "downed");
        s.lifeDown = false;
        f.t = 110;
        r = qgDead(g, s, false);
        check(r.skip, "standing again: wait qmset");
        f.t = 114;
        r = qgDead(g, s, false);
        check(!r.skip && has(f, "NOLA qdead: de pe de novo depois de 10 s"), "released after qmset");
    }

    {
        Fake f;
        f.t = 0;
        f.ps[1] = P(); f.ps[1].at = {0, 0, 1000}; f.ps[1].name = "BP_Player";
        f.conts.push_back({50, 100, 100, 990});
        init(&f);
        f.invOpen = true;
        tick1000();
        f.invOpen = false;
        f.conts.push_back({51, 200, 200, 300});
        f.traceHit = true; f.traceZ = 920;
        f.t = 2;
        tick1000();
        check(has(f, "NOLA qfall: item no chao caiu 7 m abaixo do jogador (?, inventario) -> fisica off=true, de volta em z 925 (chao pelo trace) ok=true #1"), "qfall fix log");
        check(f.conts[1].z == 925, "container put back on the ground");
        f.t = 3;
        tick1000();
        size_t c = 0;
        for (auto& l : f.logs) if (l.find("qfall") != std::string::npos) c++;
        check(c == 1, "container fixed once while it rests");
        f.t = 20;
        tick1000();
        qfArm("teste");
    }

    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
