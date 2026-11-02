#include "s2m_pop.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace s2m_pop {

namespace {

Env* g_env = nullptr;

struct VRow { double x, y, z; std::string kind, obj; };
struct VEnt { size_t row; double d; };

struct Cfg {
    double alifeoff, aimask, alifekeephubs, alifekeepquest, alifetrader, alifekeepnat;
    double vendmir, vendr, vendone;
    double hidnpc, hidnpcd, hidnpcvia, hidnpchp, clinert, npcvis;
    double orphsweep, orphrm, orphsec, orphsave, orphmax, orphvia, bodysafe;
    double qfall, qfallr, qfallwin, qfalldz, qfallup, qdead, qmset;
};

Cfg g_cfg;

const long long AI_ALL = 4194303;
const double HUBS[14][2] = {{421180, 412370}, {478540, 526470}, {665202, 617990}, {445964, 657845}, {498260, 379420}, {321267, 412054}, {215900, 270210},
    {107594, 471669}, {318986, 526376}, {398700, 545500}, {232150, 159790}, {259517, 599445}, {409220, 258260}, {384673, 341937}};
const double HUB_R = 12000;

struct Hn {
    struct U { double t = 0; int k = 0; bool lg = false; };
    Id pa = 0;
    std::map<long long, U> u;
    int n = 0;
    std::unordered_set<Id> sk;
};

struct Alife {
    bool off = false;
    bool hasPawn = false;
    Id pawn = 0;
    int fails = 0;
    int dh = 0;
    long long hidden = 0;
    long long total = 0;
    long long kept = 0;
    struct HidRec { long long uid = 0; bool pool = false; };
    std::unordered_set<Id> hid;
    std::unordered_map<Id, HidRec> hm;
    std::unordered_map<Id, int> inr;
    int vn = 0;
    int inN = 0;
    Hn hn;
    bool tradeLoaded = false;
    std::vector<std::pair<double, double>> trade;
};

struct Orph {
    struct Gone { double t; std::string key; };
    double at = 0;
    int n = 0;
    bool hasKey = false;
    std::string key;
    bool ready = false;
    std::unordered_map<Id, std::string> ra;
    std::unordered_map<Id, long long> ru;
    std::map<long long, std::string> guid;
    std::map<std::string, long long> pid;
    std::unordered_set<Id> np;
    int proof = 0;
    bool pidBad = false;
    std::unordered_map<Id, int> done;
    std::set<long long> back;
    std::map<long long, Gone> gone;
    bool tk = false;
};

struct QF {
    bool active = false;
    Id pa = 0;
    double px = 0, py = 0, pz = 0;
    double at = 0, untilT = 0;
    std::string why;
    std::unordered_map<Id, V3> base;
    std::unordered_map<Id, int> fix;
};

Alife A;
Orph O;
QF F;
int g_beat = 0;

std::vector<VRow> g_rows;
std::map<std::pair<long long, long long>, std::vector<size_t>> g_bucket;
bool g_rowsLoaded = false;
bool g_vsValid = false;
double g_vst = 0;
std::unordered_map<Id, VEnt> g_vs;
std::set<std::string> g_vl;

std::string sf(const char* f, double a) { char b[256]; std::snprintf(b, sizeof b, f, a); return b; }

std::string num(double v)
{
    char b[64];
    if (v == std::floor(v) && std::fabs(v) < 9e15) std::snprintf(b, sizeof b, "%lld", (long long)v);
    else std::snprintf(b, sizeof b, "%.14g", v);
    return b;
}

const char* tf(bool b) { return b ? "true" : "false"; }

double fl(const char* n, double d) { return g_env->flag(n, d); }

void loadCfg()
{
    Cfg& c = g_cfg;
    c.alifeoff = fl("alifeoff", 0);
    c.aimask = fl("aimask", 203647);
    c.alifekeephubs = fl("alifekeephubs", 1);
    c.alifekeepquest = fl("alifekeepquest", 1);
    c.alifetrader = fl("alifetrader", 12000);
    c.alifekeepnat = fl("alifekeepnat", 1);
    c.vendmir = fl("vendmir", 0);
    c.vendr = fl("vendr", 4000);
    c.vendone = fl("vendone", 1);
    c.hidnpc = fl("hidnpc", 1);
    c.hidnpcd = fl("hidnpcd", 0);
    c.hidnpcvia = fl("hidnpcvia", 1);
    c.hidnpchp = fl("hidnpchp", 4999);
    c.clinert = fl("clinert", 1);
    c.npcvis = fl("npcvis", 1);
    c.orphsweep = fl("orphsweep", 0);
    c.orphrm = fl("orphrm", 2);
    c.orphsec = fl("orphsec", 3);
    c.orphsave = fl("orphsave", 0);
    c.orphmax = fl("orphmax", 4);
    c.orphvia = fl("orphvia", 1);
    c.bodysafe = fl("bodysafe", 0);
    c.qfall = fl("qfall", 1);
    c.qfallr = fl("qfallr", 400);
    c.qfallwin = fl("qfallwin", 8);
    c.qfalldz = fl("qfalldz", 500);
    c.qfallup = fl("qfallup", 5);
    c.qdead = fl("qdead", 1);
    c.qmset = fl("qmset", 3);
}

std::vector<std::string> splitTab(const std::string& s)
{
    std::vector<std::string> r;
    size_t p = 0;
    for (;;) {
        size_t q = s.find('\t', p);
        if (q == std::string::npos) { r.push_back(s.substr(p)); break; }
        r.push_back(s.substr(p, q - p));
        p = q + 1;
    }
    return r;
}

std::vector<std::string> lines(const std::string& s)
{
    std::vector<std::string> r;
    size_t p = 0;
    while (p <= s.size()) {
        size_t q = s.find('\n', p);
        if (q == std::string::npos) q = s.size();
        std::string l = s.substr(p, q - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!l.empty()) r.push_back(l);
        p = q + 1;
    }
    return r;
}

std::pair<long long, long long> cell(double x, double y)
{
    return {(long long)std::floor(x / 10000), (long long)std::floor(y / 10000)};
}

void buildBuckets()
{
    g_bucket.clear();
    for (size_t i = 0; i < g_rows.size(); ++i) g_bucket[cell(g_rows[i].x, g_rows[i].y)].push_back(i);
}

void ensureRows()
{
    if (g_rowsLoaded) return;
    g_rowsLoaded = true;
    std::string txt;
    bool ok = g_env->readFile("s2u_vendor.tsv", txt);
    if (ok) loadVendorRows(txt);
    g_env->log("NOLA vend: " + std::to_string(g_rows.size()) + " vendedores em s2u_vendor.lua" + (ok ? "" : " (arquivo ausente ou com erro: nil)"));
}

template <class Fn>
void scanRows(const V3& q, double r, Fn fn)
{
    if (r > 10000) {
        for (size_t i = 0; i < g_rows.size(); ++i) fn(i);
        return;
    }
    auto c = cell(q.x, q.y);
    for (long long i = -1; i <= 1; ++i)
        for (long long j = -1; j <= 1; ++j) {
            auto it = g_bucket.find({c.first + i, c.second + j});
            if (it == g_bucket.end()) continue;
            for (size_t k : it->second) fn(k);
        }
}

bool vendorAt(const V3& q, double rr, size_t& rowOut, double& dOut)
{
    ensureRows();
    double r = rr > 0 ? rr : g_cfg.vendr;
    double bd = r * r;
    bool found = false;
    size_t best = 0;
    scanRows(q, r, [&](size_t i) {
        const VRow& v = g_rows[i];
        double dx = q.x - v.x, dy = q.y - v.y, dz = q.z - v.z;
        double d = dx * dx + dy * dy + dz * dz;
        if (d < bd) { best = i; bd = d; found = true; }
    });
    if (!found) return false;
    rowOut = best;
    dOut = std::sqrt(bd);
    return true;
}

const std::unordered_map<Id, VEnt>& vendSet()
{
    double now = g_env->now();
    if (g_vsValid && now >= g_vst && now - g_vst < 2) return g_vs;
    g_vst = now;
    ensureRows();
    double r = g_cfg.vendr;
    double r2 = r * r;
    std::unordered_set<Id> skip;
    Id lpId; V3 lpAt;
    if (g_env->localPawn(lpId, lpAt)) skip.insert(lpId);
    for (int v = 0; v < g_env->bodyViews(); ++v) {
        Body pu = g_env->puppet(v), tg = g_env->target(v);
        if (pu.ok) skip.insert(pu.pawn);
        if (tg.ok) skip.insert(tg.pawn);
    }
    std::unordered_map<size_t, std::pair<Id, double>> best;
    std::vector<Id> ps;
    g_env->pawns(PawnClass::All, ps);
    for (Id p : ps) {
        if (!g_env->valid(p) || skip.count(p) || !g_env->alive(p)) continue;
        V3 q;
        if (!g_env->loc(p, q)) continue;
        scanRows(q, r, [&](size_t i) {
            const VRow& v = g_rows[i];
            double d = (q.x - v.x) * (q.x - v.x) + (q.y - v.y) * (q.y - v.y) + (q.z - v.z) * (q.z - v.z);
            if (d < r2) {
                auto it = best.find(i);
                if (it == best.end() || d < it->second.second) best[i] = {p, d};
            }
        });
    }
    g_vs.clear();
    for (auto& kv : best) {
        double d = std::sqrt(kv.second.second);
        auto it = g_vs.find(kv.second.first);
        if (it == g_vs.end() || d < it->second.d) g_vs[kv.second.first] = {kv.first, d};
    }
    g_vsValid = true;
    return g_vs;
}

bool vendOf(Id p, const V3& q, size_t& row, double& d)
{
    if (g_cfg.vendone == 1) {
        const auto& s = vendSet();
        auto it = s.find(p);
        if (it == s.end()) return false;
        row = it->second.row;
        d = it->second.d;
        return true;
    }
    return vendorAt(q, 0, row, d);
}

void ensureTrade()
{
    if (A.tradeLoaded) return;
    A.tradeLoaded = true;
    std::string txt;
    if (!g_env->readFile("s2u_trade.tsv", txt)) {
        g_env->log("NOLA alife: s2u_trade.tsv nao esta na ponte: so os hubs mantem NPCs locais");
        return;
    }
    for (const std::string& ln : lines(txt)) {
        auto c = splitTab(ln);
        if (c.size() < 7) continue;
        char* e1; char* e2;
        double x = std::strtod(c[3].c_str(), &e1), y = std::strtod(c[4].c_str(), &e2);
        if (e1 == c[3].c_str() || e2 == c[4].c_str()) continue;
        if (c[6].compare(0, 11, "BasicTrader") == 0) continue;
        A.trade.push_back({x, y});
    }
    g_env->log("NOLA alife: " + std::to_string(A.trade.size()) + " lugares de comerciantes de s2u_trade.tsv mantem NPCs locais (raio " + num(g_cfg.alifetrader) + " cm)");
}

enum HnC { HN_LISTED, HN_NOHP, HN_BOSS, HN_STORY, HN_HOSTUID, HN_NOUID, HN_COMMON };

struct HnRow { const char* tag; bool kill; bool tell; const char* why; };

const HnRow HN_ROWS[] = {
    {"lista_natnpc", false, false, "o host lista este NPC"},
    {"sem_hp", false, false, "sem maxhp legivel"},
    {"chefe", false, false, "maxhp acima de hidnpchp"},
    {"historia", false, true, "NPC de historia: matar dispararia eventos de quest locais"},
    {"lista_natnpc_uid", false, true, "uid na lista natnpc do host (b162)"},
    {"sem_uid", false, true, "sem uid"},
    {"comum", true, false, "NPC comum"},
};

int hnCat(Id o, long long& uid, double& mh, std::string& note)
{
    uid = 0;
    mh = 0;
    note.clear();
    if (!g_env->guid(o, uid) || uid < 0) uid = 0;
    if (g_env->natListed(o)) return HN_LISTED;
    if (!g_env->maxHp(o, mh)) return HN_NOHP;
    if (mh >= g_cfg.hidnpchp) return HN_BOSS;
    if (g_env->className(o).find("BP_AI_Human") != std::string::npos && g_env->storyNpc(o, note)) return HN_STORY;
    if (uid > 0 && g_env->natListedUid(uid)) return HN_HOSTUID;
    if (uid <= 0) return HN_NOUID;
    return HN_COMMON;
}

void inertSet(Id o, bool on)
{
    auto it = A.inr.find(o);
    if (!on) {
        if (it != A.inr.end()) { g_env->inert(o, 0, false); A.inr.erase(it); }
        return;
    }
    if (it != A.inr.end()) { g_env->inert(o, 0, true); return; }
    long long uid;
    double mh;
    std::string note;
    int c = hnCat(o, uid, mh, note);
    if (!g_env->inert(o, uid, true)) return;
    A.inr[o] = c;
    A.inN++;
    char b[512];
    std::snprintf(b, sizeof b, "NOLA clinert: NPC local escondido %s (%s) inerte: categoria=%s uid=%lld%s%s (IA presa no lugar, ataque cortado na fonte) #%d", g_env->shortName(o).c_str(),
        g_env->className(o).c_str(), HN_ROWS[c].tag, uid, note.empty() ? "" : " s2u_choice=", note.c_str(), A.inN);
    g_env->log(b);
}

void inertClear()
{
    for (auto& kv : A.inr) g_env->inert(kv.first, 0, false);
    A.inr.clear();
}

void hnSafe(Id o, const V3& lpAt)
{
    if (g_cfg.hidnpc != 1 || !A.hid.count(o)) return;
    Hn& H = A.hn;
    if (H.pa != A.pawn) { H = Hn(); H.pa = A.pawn; }
    long long uid;
    double mh;
    std::string g;
    int c = hnCat(o, uid, mh, g);
    const HnRow& R = HN_ROWS[c];
    if (!R.kill && !R.tell) return;
    bool thr = g_env->inThreat(o), foc = false;
    if (!thr) foc = g_env->focusIsLocal(o);
    V3 q;
    if (!g_env->loc(o, q)) return;
    double d = std::sqrt((q.x - lpAt.x) * (q.x - lpAt.x) + (q.y - lpAt.y) * (q.y - lpAt.y) + (q.z - lpAt.z) * (q.z - lpAt.z));
    if (!(thr || foc || (g_cfg.hidnpcd > 0 && d <= g_cfg.hidnpcd))) return;
    std::string name = g_env->shortName(o);
    const char* keep = A.inr.count(o) ? "fica escondido e inerte" : "fica so escondido";
    if (!R.kill) {
        if (!H.sk.count(o)) {
            H.sk.insert(o);
            g_env->log("NOLA hidnpc: " + name + (uid > 0 ? " (uid " + std::to_string(uid) + ")" : std::string()) + " categoria=" + R.tag + " (" + R.why + (g.empty() ? "" : ", s2u_choice " + g) + "): nao morto, " + keep);
        }
        return;
    }
    if (g_env->className(o).find("BP_AI_Human") != std::string::npos && g_env->natPark(o, uid)) return;
    double now = g_env->now();
    auto it = H.u.find(uid);
    bool has = it != H.u.end();
    if (has && now - it->second.t < 5) return;
    if (has && it->second.k >= 2) {
        if (!it->second.lg) {
            it->second.lg = true;
            g_env->log("NOLA hidnpc: " + name + " (uid " + std::to_string(uid) + ") ainda vivo depois de 2 tentativas: " + keep);
        }
        return;
    }
    int k = has ? it->second.k + 1 : 1;
    int via = g_cfg.hidnpcvia == 2 ? 2 : 1;
    if (k == 2) via = 3 - via;
    bool ok = via == 2 ? g_env->destroyModel(uid) : g_env->killByUid(uid);
    Hn::U& U = H.u[uid];
    U.t = now;
    U.k = k;
    H.n++;
    char b[512];
    std::snprintf(b, sizeof b, "NOLA hidnpc: NPC local escondido %s (%s, %.0f m, maxhp %.0f) -> %s(%lld) ok=%s #%d", name.c_str(),
        thr ? "em ameaca" : (foc ? "foco em mim" : "perto"), d / 100, mh, via == 2 ? "XDestroyModel" : "XKillNpcByUID", uid, tf(ok), H.n);
    g_env->log(b);
}

bool atPool(Id o, bool& pool)
{
    V3 q;
    if (!g_env->loc(o, q)) return false;
    pool = std::fabs(q.x) < 100 && std::fabs(q.y) < 100;
    return true;
}

void hidMark(Id o)
{
    Alife::HidRec r;
    long long uid = 0;
    if (g_env->guid(o, uid) && uid > 0) r.uid = uid;
    atPool(o, r.pool);
    A.hm[o] = r;
}

void hidDrop(Id o)
{
    A.hid.erase(o);
    A.hm.erase(o);
}

void visCheck(Id o)
{
    auto it = A.hm.find(o);
    if (it == A.hm.end()) { hidMark(o); return; }
    Alife::HidRec& r = it->second;
    long long uid = 0;
    bool hu = g_env->guid(o, uid) && uid > 0;
    bool pool = r.pool;
    bool hl = atPool(o, pool);
    const char* why = nullptr;
    if (hu && r.uid > 0 && uid != r.uid) why = "new_uid";
    else if (hl && r.pool && !pool) why = "left_pool";
    else if (hl && !pool && !g_env->hiddenFlag(o)) why = "unhidden";
    long long was = r.uid;
    if (hu) r.uid = uid;
    if (hl) r.pool = pool;
    if (!why) return;
    g_env->setHidden(o, true);
    g_env->setCollision(o, false);
    if (A.inr.count(o)) inertSet(o, false);
    A.vn++;
    if (A.vn > 20 && A.vn % 50 != 0) return;
    char b[512];
    std::snprintf(b, sizeof b, "NOLA npcvis: local NPC %s (%s) uid=%lld (was %lld) visible again on the client (reason=%s): hidden again, collision off, inert refreshed #%d",
        g_env->shortName(o).c_str(), g_env->className(o).c_str(), hu ? uid : 0, was, why, A.vn);
    g_env->log(b);
}

void alifeTick()
{
    bool want = g_cfg.alifeoff == 1 && g_env->coop();
    Id lp = 0;
    V3 lpAt;
    bool lpOk = g_env->localPawn(lp, lpAt);
    if (lpOk) {
        if (A.hasPawn && lp != A.pawn && A.off) {
            A.off = false;
            A.hid.clear();
            A.hm.clear();
            inertClear();
            g_env->log("NOLA alife: pawn local novo; reaplicando");
        }
        A.pawn = lp;
        A.hasPawn = true;
    }
    if (want != A.off && (lpOk || !want)) {
        bool ok1 = g_env->aiFeatures(want, want ? (long long)g_cfg.aimask : AI_ALL);
        bool ok2 = g_env->aLife(want);
        bool retry = false;
        if (ok1 && ok2) { A.off = want; A.fails = 0; }
        else {
            A.fails++;
            if (A.fails <= 3 || A.fails % 30 == 0)
                g_env->log(std::string("NOLA alife: chamadas falharam (ok=") + tf(ok1) + "," + tf(ok2) + ") tentativa " + std::to_string(A.fails) + "; tento de novo");
            if (want) retry = true; else A.off = want;
        }
        int back = 0;
        if (!want) {
            std::vector<Id> ps;
            g_env->pawns(PawnClass::All, ps);
            for (Id o : ps) {
                if (o == lp || !g_env->valid(o) || ownBody(o)) continue;
                g_env->setHidden(o, false);
                g_env->setCollision(o, true);
                back++;
            }
            A.hid.clear();
            A.hm.clear();
            inertClear();
        }
        if (!retry)
            g_env->log(std::string("NOLA alife: ") + (want ? "DESLIGADO" : "LIGADO") + " (AIFeature " + num(g_cfg.aimask) + ", XALife" + (want ? "Disable" : "Enable") + ") ok=" + tf(ok1) + "," + tf(ok2) +
                (want ? "" : " npcs_mostrados=" + std::to_string(back)));
    }
    if (!want || !lpOk) return;
    if (g_cfg.clinert != 1 && !A.inr.empty()) inertClear();

    long long nw = 0, total = 0;
    std::vector<std::string> names;
    std::vector<Id> ps;
    g_env->pawns(PawnClass::All, ps);
    std::unordered_set<Id> seen;
    std::vector<Id> hv;
    g_env->pawns(PawnClass::Humans, hv);
    std::unordered_set<Id> humans(hv.begin(), hv.end());
    for (Id o : ps) {
        if (!g_env->valid(o)) continue;
        seen.insert(o);
        if (o == lp) continue;
        bool alive = g_env->alive(o);
        if (A.hid.count(o) && (ownBody(o) || g_env->natOwn(o))) { hidDrop(o); inertSet(o, false); }
        if (A.hid.count(o) && !alive) {
            if (!g_env->hiddenFlag(o)) {
                g_env->setHidden(o, true);
                g_env->setCollision(o, false);
                A.dh++;
                if (A.dh <= 10) g_env->log("NOLA alife: NPC local morto escondido de novo " + g_env->shortName(o));
            }
        }
        if (!alive && A.inr.count(o)) inertSet(o, false);
        if (!alive || ownBody(o) || g_env->natOwn(o)) continue;
        total++;
        bool keep = false;
        if (g_cfg.alifekeephubs == 1 || g_cfg.alifekeepquest == 1 || g_cfg.vendmir > 0) {
            V3 q;
            if (!g_env->loc(o, q)) continue;
            size_t row; double d;
            bool zones = g_cfg.alifekeepnat != 1;
            keep = humans.count(o) && ((zones && g_cfg.alifekeephubs == 1 && nearHub(q.x, q.y)) || (zones && g_cfg.alifekeepquest == 1 && nearTrade(q.x, q.y)) ||
                (g_cfg.vendmir > 0 && vendOf(o, q, row, d)));
            if (keep && A.hid.count(o)) {
                hidDrop(o);
                inertSet(o, false);
                g_env->setHidden(o, false);
                g_env->setCollision(o, true);
            }
            if (keep) A.kept++;
        }
        if (!keep && !A.hid.count(o)) {
            A.hid.insert(o);
            nw++;
            g_env->setHidden(o, true);
            g_env->setCollision(o, false);
            hidMark(o);
            if (names.size() < 4) names.push_back(g_env->className(o));
        }
        else if (!keep && g_cfg.npcvis == 1) visCheck(o);
        if (A.hid.count(o) && g_cfg.clinert == 1) inertSet(o, true);
        if (A.hid.count(o)) hnSafe(o, lpAt);
    }
    for (auto it = A.hid.begin(); it != A.hid.end();) {
        if (!seen.count(*it)) { inertSet(*it, false); A.hm.erase(*it); it = A.hid.erase(it); } else ++it;
    }
    A.total = total;
    if (nw > 0) {
        A.hidden += nw;
        std::string nm;
        for (size_t i = 0; i < names.size(); ++i) nm += (i ? " " : "") + names[i];
        char b[512];
        std::snprintf(b, sizeof b, "NOLA alife: %lld NPC(s) locais escondidos e sem colisao [%s] (vivos nao nossos agora=%lld, escondidos no total=%lld)", nw, nm.c_str(), total, A.hidden);
        g_env->log(b);
    }
}

std::string orphGone(long long g, bool hasG, Id ho, const std::string& hname, double hd, double now)
{
    if (!hasG || g <= 0) return " alife=sem guid (so escondido)";
    for (int v = 0; v < g_env->bodyViews(); ++v) {
        Body bodies[2] = {g_env->target(v), g_env->puppet(v)};
        for (const Body& B : bodies)
            if (B.hasUid && B.uid == g && B.ok) return " alife=guid " + std::to_string(g) + " e do corpo adotado (so escondido)";
    }
    if (O.back.count(g)) return " alife=guid " + std::to_string(g) + " so escondido";
    auto it = O.gone.find(g);
    if (it != O.gone.end() && (it->second.key == O.key || now - it->second.t < 60)) {
        O.back.insert(g);
        char b[400];
        std::snprintf(b, sizeof b, "NOLA orfao: guid %lld voltou %.0f s depois da remocao (%s) -> so esconder este guid daqui em diante", g, now - it->second.t, hname.c_str());
        g_env->log(b);
        return " alife=guid " + std::to_string(g) + " voltou (so escondido)";
    }
    O.gone[g] = {now, O.key};
    bool v2 = g_cfg.orphvia == 2;
    std::string via = v2 ? "XKillNpcByUID+XDespawnDeadNPCsAtRadius" : "XDestroyModel";
    bool ok = true;
    std::string err;
    if (!v2) ok = g_env->destroyModel(g);
    else {
        Id lp; V3 lpAt;
        if (!g_env->localPawn(lp, lpAt) || hd < 0) { ok = false; err = "sem jogador local"; }
        else {
            double R = hd * 100 + 300;
            std::vector<Id> hs;
            g_env->pawns(PawnClass::Humans, hs);
            for (Id o : hs) {
                if (o == ho || !g_env->valid(o) || g_env->alive(o)) continue;
                V3 q;
                if (!g_env->loc(o, q)) continue;
                if (std::fabs(q.x) < 100 && std::fabs(q.y) < 100) continue;
                if ((q.x - lpAt.x) * (q.x - lpAt.x) + (q.y - lpAt.y) * (q.y - lpAt.y) <= (R + 500) * (R + 500)) {
                    ok = false;
                    err = "outro corpo a menos de " + std::to_string((long long)std::floor(R / 100 + 5)) + " m";
                    break;
                }
            }
            if (ok) ok = g_env->killByUid(g) && g_env->despawnDeadAtRadius(R);
        }
    }
    if (!ok) O.back.insert(g);
    return " alife=" + via + "(" + std::to_string(g) + ") ok=" + tf(ok) + (ok ? "" : (err.empty() ? "" : " " + err));
}

void orphSweep()
{
    int mode = (int)g_cfg.orphsweep;
    if (mode != 1 && mode != 2) return;
    int nbv = std::max(g_env->bodyViews(), 1);
    std::vector<Body> Ts((size_t)nbv), Ps((size_t)nbv);
    for (int v = 0; v < nbv; ++v) { Ts[(size_t)v] = g_env->target(v); Ps[(size_t)v] = g_env->puppet(v); }
    const Body& T = Ts[0];
    std::string wkey;
    bool hasW = g_env->worldKey(wkey);
    if (!O.ready || hasW != O.hasKey || (hasW && wkey != O.key)) {
        O.hasKey = hasW;
        O.key = wkey;
        O.ready = true;
        O.ra.clear(); O.ru.clear(); O.guid.clear(); O.pid.clear(); O.np.clear();
        O.proof = 0; O.pidBad = false; O.done.clear(); O.back.clear();
    }
    double now = g_env->now();
    std::unordered_set<Id> keep;
    struct KB { const Body* b; const char* kind; };
    std::vector<KB> kbs;
    for (int v = 0; v < nbv; ++v) { kbs.push_back({&Ts[(size_t)v], "alvo"}); kbs.push_back({&Ps[(size_t)v], "boneco"}); }
    for (auto& kb : kbs) {
        const Body& b = *kb.b;
        if (!b.ok || !g_env->valid(b.pawn)) continue;
        keep.insert(b.pawn);
        if (O.ra.count(b.pawn)) continue;
        O.ra[b.pawn] = kb.kind;
        if (b.hasUid) { O.ru[b.pawn] = b.uid; O.guid[b.uid] = kb.kind; }
        long long id = 0;
        bool hasId = g_env->protoId(b.pawn, id) && id > 0;
        if (hasId) O.pid[kb.kind] = id;
        g_env->log(std::string("NOLA orfao: ") + kb.kind + " registrado " + g_env->shortName(b.pawn) + " guid=" + (b.hasUid ? std::to_string(b.uid) : "nil") +
            " prototipo=" + (hasId ? std::to_string(id) : "nil"));
    }
    bool tOk = T.ok && g_env->valid(T.pawn);
    if (O.tk && !tOk) O.at = 0;
    O.tk = tOk;
    if (now < O.at) return;
    if (g_cfg.bodysafe == 1 && !g_env->worldOpen()) return;
    bool pend = g_env->clPending();
    for (int v = 0; v < nbv; ++v) pend = pend || Ts[(size_t)v].spawnPending || Ps[(size_t)v].spawnPending;
    if (pend) return;
    bool allPup = true;
    for (int v = 0; v < nbv; ++v)
        if (g_env->bodyViewLive(v) && !(Ps[(size_t)v].ok && g_env->valid(Ps[(size_t)v].pawn))) allPup = false;
    O.at = now + std::max(g_cfg.orphsec, 1.0);
    bool pidOk = !O.pidBad && O.proof >= 3;
    Id lpId; V3 lpAt;
    bool hasLp = g_env->localPawn(lpId, lpAt);
    struct Hit { Id o; std::string name, kind, why; bool alive; double mh, d; long long g; bool hasG; };
    std::vector<Hit> hits;
    int samples = 0;
    std::vector<Id> hs;
    g_env->pawns(PawnClass::Humans, hs);
    for (Id o : hs) {
        if (!g_env->valid(o)) continue;
        if (keep.count(o)) continue;
        auto dn = O.done.find(o);
        if (dn != O.done.end() && dn->second == mode) continue;
        if (g_env->natOwn(o)) continue;
        double mh;
        if (!g_env->maxHp(o, mh)) continue;
        const char* kind = nullptr;
        if (mh >= 4000 && mh <= (g_cfg.orphsave == 1 ? 6500 : 6000)) kind = "alvo";
        else if (mh >= 90000) kind = "boneco";
        bool alive = g_env->alive(o);
        if (kind && (long long)hits.size() >= std::max<long long>((long long)g_cfg.orphmax, 1)) continue;
        std::string name = g_env->shortName(o);
        if (!kind) {
            if (alive && !pidOk && !O.pidBad && (O.pid.count("alvo") || O.pid.count("boneco")) && samples < 3 && !O.np.count(o)) {
                samples++;
                O.np.insert(o);
                long long id;
                if (g_env->protoId(o, id) && id > 0) {
                    bool ours = (O.pid.count("alvo") && id == O.pid["alvo"]) || (O.pid.count("boneco") && id == O.pid["boneco"]);
                    if (ours) {
                        O.pidBad = true;
                        g_env->log("NOLA orfao: prototipo " + std::to_string(id) + " tambem num NPC real " + name + " (maxhp " + num(mh) + "); identificacao por prototipo DESLIGADA");
                    } else {
                        O.proof++;
                        if (O.proof == 3)
                            g_env->log("NOLA orfao: prototipo dos nossos (alvo=" + (O.pid.count("alvo") ? std::to_string(O.pid["alvo"]) : std::string("nil")) + " boneco=" +
                                (O.pid.count("boneco") ? std::to_string(O.pid["boneco"]) : std::string("nil")) + ") difere de 3 NPCs reais; identificacao por prototipo ligada");
                    }
                }
            }
            continue;
        }
        V3 q;
        if (!g_env->loc(o, q)) continue;
        if (std::fabs(q.x) < 100 && std::fabs(q.y) < 100) continue;
        std::string kd = kind, why;
        auto ra = O.ra.find(o);
        if (ra != O.ra.end() && ra->second == kd) why = "corpo adotado antes";
        else if (alive) {
            long long g = 0;
            bool hasG = g_env->guid(o, g);
            auto gi = O.guid.find(g);
            if (hasG && gi != O.guid.end() && gi->second == kd) why = "guid " + std::to_string(g);
            else if (pidOk) {
                long long id;
                if (g_env->protoId(o, id) && id > 0 && O.pid.count(kd) && id == O.pid[kd]) why = "prototipo " + std::to_string(id);
            }
            if (why.empty() && g_cfg.orphsave == 1 && hasG) {
                const Body* Bp = nullptr;
                for (int v = 0; v < nbv && !Bp; ++v) {
                    const Body& c = kd == "alvo" ? Ts[(size_t)v] : Ps[(size_t)v];
                    if (c.ok && g_env->valid(c.pawn)) Bp = &c;
                }
                if (Bp) {
                    const Body& B = *Bp;
                    std::string m1 = g_env->meshName(o, "Mesh"), m2 = g_env->meshName(B.pawn, "Mesh");
                    if (!m1.empty() && (m1 == m2 || kd == "boneco")) {
                        why = "guid " + std::to_string(g) + " sobra de save (malha " + m1 + ")";
                        O.guid[g] = kd;
                    }
                }
            }
        }
        if (why.empty()) continue;
        if (kd == "boneco" && !allPup) continue;
        double d = hasLp ? std::sqrt((q.x - lpAt.x) * (q.x - lpAt.x) + (q.y - lpAt.y) * (q.y - lpAt.y)) / 100 : -1;
        Hit h{o, name, kd, why, alive, mh, d, 0, false};
        auto ru = O.ru.find(o);
        if (ru != O.ru.end()) { h.g = ru->second; h.hasG = true; }
        else if (why.compare(0, 5, "guid ") == 0) { h.g = std::atoll(why.c_str() + 5); h.hasG = true; }
        hits.push_back(h);
    }
    for (Hit& h : hits) {
        O.done[h.o] = mode;
        O.n++;
        std::string act;
        if (mode == 2) {
            act = "TESTE: faria esconder+sem colisao+sem interacao";
            if (g_cfg.orphrm == 2) act += "+K2_DestroyActor";
            else if (g_cfg.orphrm == 3) act += "+remocao A-Life guid=" + (h.hasG ? std::to_string(h.g) : std::string("nil"));
        } else {
            bool okH = g_env->setHidden(h.o, true);
            bool okC = g_env->setCollision(h.o, false);
            bool okI = g_env->setInteractionOff(h.o);
            act = std::string("escondido=") + tf(okH) + " sem_colisao=" + tf(okC) + " sem_interacao=" + tf(okI);
            if (g_cfg.orphrm == 2) act += std::string(" destruido=") + tf(g_env->destroyActor(h.o));
            if (g_cfg.orphrm == 3) act += orphGone(h.g, h.hasG, h.o, h.name, h.d, now);
        }
        if (O.n <= 30 || O.n % 50 == 0) {
            char b[700];
            std::snprintf(b, sizeof b, "NOLA orfao: %s (%s, vivo=%s, maxhp=%.0f, %.0f m, por %s) -> %s (total=%d)", h.name.c_str(), h.kind.c_str(), tf(h.alive), h.mh, h.d, h.why.c_str(), act.c_str(), O.n);
            g_env->log(b);
        }
    }
}

void qfScan(double px, double py, double pz, std::vector<Cont>& out)
{
    std::vector<Cont> all;
    g_env->containers(all);
    double r2 = g_cfg.qfallr * g_cfg.qfallr;
    for (const Cont& c : all)
        if ((c.x - px) * (c.x - px) + (c.y - py) * (c.y - py) <= r2 && c.z <= pz + 300 && c.z >= pz - 30000) out.push_back(c);
}

void qfFix(const Cont& c)
{
    int k = F.fix[c.id] + 1;
    if (k > 3) return;
    F.fix[c.id] = k;
    bool frz = g_env->simPhysicsOff(c.id);
    double feet = F.pz - 90;
    bool hit = false;
    double iz = 0;
    bool ran = g_env->trace(c.x, c.y, F.pz + 50, c.x, c.y, F.pz - 1000, hit, iz);
    double tz;
    std::string how;
    if (ran && hit && iz >= feet - 300 && iz <= feet + 150) { tz = iz + g_cfg.qfallup; how = "chao pelo trace"; }
    else {
        tz = feet + g_cfg.qfallup;
        how = "pes do jogador (trace " + (!ran ? std::string("falhou") : (hit ? sf("z %.0f", iz) + " fora" : std::string("sem chao"))) + ")";
    }
    bool ok = g_env->setLoc(c.id, c.x, c.y, tz);
    F.base[c.id] = {c.x, c.y, tz};
    std::string m = g_env->meshName(c.id, "MeshComponent");
    if (m.empty()) m = "?";
    char b[600];
    std::snprintf(b, sizeof b, "NOLA qfall: item no chao caiu %.0f m abaixo do jogador (%s, %s) -> fisica off=%s, de volta em z %.0f (%s) ok=%s #%d",
        (F.pz - c.z) / 100, m.c_str(), F.why.c_str(), tf(frz), tz, how.c_str(), tf(ok), k);
    g_env->log(b);
}

void qfTick()
{
    if (g_cfg.qfall != 1) { F = QF(); return; }
    Id lp; V3 at;
    if (!g_env->localPawn(lp, at)) { F = QF(); return; }
    if (g_env->inventoryOpen()) qfArm("inventario");
    if (!F.active) return;
    double now = g_env->now();
    if (now > F.untilT || F.pa != lp) { F = QF(); return; }
    std::vector<Cont> cs;
    qfScan(F.px, F.py, F.pz, cs);
    for (const Cont& c : cs) {
        auto b = F.base.find(c.id);
        bool isNew = b == F.base.end() || (b->second.x - c.x) * (b->second.x - c.x) + (b->second.y - c.y) * (b->second.y - c.y) + (b->second.z - c.z) * (b->second.z - c.z) > 100.0 * 100.0;
        if (isNew && c.z < F.pz - g_cfg.qfalldz) qfFix(c);
    }
}

}

size_t loadVendorRows(const std::string& tsv)
{
    g_rows.clear();
    g_rowsLoaded = true;
    std::map<long long, std::vector<std::string>> cols;
    for (const std::string& ln : lines(tsv)) {
        auto c = splitTab(ln);
        if (c.size() < 5 || c[0] != "srows") continue;
        if (c[1].size() < 2 || c[2].size() < 2) continue;
        long long i = std::atoll(c[1].c_str() + 1), j = std::atoll(c[2].c_str() + 1);
        if (i < 1 || j < 1 || j > 5) continue;
        auto& r = cols[i];
        if (r.size() < 5) r.resize(5);
        r[j - 1] = c[4];
    }
    for (auto& kv : cols) {
        const auto& r = kv.second;
        g_rows.push_back({std::atof(r[0].c_str()), std::atof(r[1].c_str()), std::atof(r[2].c_str()), r[3], r[4]});
    }
    buildBuckets();
    g_vsValid = false;
    return g_rows.size();
}

size_t vendorRowCount() { return g_rows.size(); }

bool vendorNear(double x, double y, double z, double r, VendHit& out)
{
    if (!g_env || r <= 0) return false;
    V3 q;
    q.x = x;
    q.y = y;
    q.z = z;
    size_t row = 0;
    double d = 0;
    if (!vendorAt(q, r, row, d)) return false;
    out.kind = g_rows[row].kind;
    out.obj = g_rows[row].obj;
    out.dist = d;
    return true;
}

bool nearHub(double x, double y)
{
    for (auto& h : HUBS) {
        double dx = x - h[0], dy = y - h[1];
        if (dx * dx + dy * dy < HUB_R * HUB_R) return true;
    }
    return false;
}

bool nearTrade(double x, double y)
{
    ensureTrade();
    double r2 = g_cfg.alifetrader * g_cfg.alifetrader;
    for (auto& p : A.trade) {
        double dx = x - p.first, dy = y - p.second;
        if (dx * dx + dy * dy < r2) return true;
    }
    return false;
}

bool isHiddenLocal(Id p) { return A.hid.count(p) != 0; }

bool isVendor(Id p, double* distCm)
{
    V3 q;
    if (!g_env->loc(p, q)) return false;
    size_t row; double d;
    if (!vendOf(p, q, row, d)) return false;
    if (distCm) *distCm = d;
    return true;
}

bool vendSkip(Id p)
{
    V3 q;
    if (!g_env->loc(p, q)) return false;
    size_t row = 0; double d = 0;
    bool v = vendOf(p, q, row, d);
    std::string why;
    if (v) {
        char b[256];
        std::snprintf(b, sizeof b, " a %.0f m", d / 100);
        why = g_rows[row].kind + " " + g_rows[row].obj + b;
    } else if (g_cfg.vendmir == 2 && (nearHub(q.x, q.y) || nearTrade(q.x, q.y))) why = "hub/lugar de comercio (vendmir=2)";
    if (!why.empty()) {
        char k[64];
        std::snprintf(k, sizeof k, "%.0f,%.0f", q.x / 500, q.y / 500);
        if (!g_vl.count(k)) {
            g_vl.insert(k);
            char b[512];
            std::snprintf(b, sizeof b, "NOLA vend: NPC em %.0f,%.0f,%.0f sem espelho: %s", q.x, q.y, q.z, why.c_str());
            g_env->log(b);
        }
    }
    return !why.empty();
}

bool ownBody(Id p)
{
    if (g_env->ownBodyBase(p)) return true;
    return g_cfg.vendmir > 0 && g_env->isHost() && vendSkip(p);
}

QRes qgDead(QGuard& G, const QSense& s, bool rebase)
{
    QRes r;
    if (g_cfg.qdead != 1) { G = QGuard(); return r; }
    double now = g_env->now();
    bool dead = !s.alive || (s.hasHp && s.hp <= 0) || s.ndDead;
    bool down = s.lifeDown;
    if (dead || down) {
        if (!G.active || G.pawn != s.pawn) {
            G = QGuard();
            G.active = true;
            G.pawn = s.pawn;
            G.at = now;
            g_env->log(std::string("NOLA qdead: jogador local ") + (dead ? "morto (hp 0 / tela de morte)" : "caido") + ": contagens de itens de missao NAO enviadas (pendentes descartados)");
        }
        if (dead) G.dead = true;
        G.hasOk = false;
        r.skip = true;
        r.dropPending = true;
        return r;
    }
    if (!G.active) return r;
    std::string why;
    if (G.pawn != s.pawn) why = "pawn novo";
    else {
        if (!G.hasOk) { G.hasOk = true; G.ok = now; }
        if (now - G.ok < g_cfg.qmset) { r.skip = true; return r; }
        why = G.dead ? "mesmo pawn de volta depois da morte" : "de pe de novo";
    }
    QGuard old = G;
    G = QGuard();
    if (rebase && (old.dead || old.pawn != s.pawn)) { r.rebase = true; why += "; base relida, nada enviado"; }
    char b[400];
    std::snprintf(b, sizeof b, "NOLA qdead: %s depois de %.0f s: contagens voltam a ser enviadas", why.c_str(), now - old.at);
    g_env->log(b);
    return r;
}

void qfArm(const char* why)
{
    if (g_cfg.qfall != 1) return;
    Id lp; V3 q;
    if (!g_env->localPawn(lp, q)) return;
    double now = g_env->now();
    if (F.active && F.pa == lp && now <= F.untilT) { F.untilT = std::max(F.untilT, now + g_cfg.qfallwin); return; }
    F = QF();
    F.active = true;
    F.pa = lp;
    F.px = q.x; F.py = q.y; F.pz = q.z;
    F.at = now;
    F.untilT = now + g_cfg.qfallwin;
    F.why = why;
    std::vector<Cont> cs;
    qfScan(q.x, q.y, q.z, cs);
    for (const Cont& c : cs) F.base[c.id] = {c.x, c.y, c.z};
}

void init(Env* env)
{
    g_env = env;
    A = Alife();
    O = Orph();
    F = QF();
    g_beat = 0;
    g_vsValid = false;
    g_vl.clear();
    g_rowsLoaded = false;
    g_rows.clear();
    g_bucket.clear();
    loadCfg();
}

void shutdown() { g_env = nullptr; }

void tick1000()
{
    if (!g_env) return;
    loadCfg();
    g_beat++;
    try { orphSweep(); } catch (...) {}
    if (g_beat % 2 == 0) { try { alifeTick(); } catch (...) {} }
    try { qfTick(); } catch (...) {}
}

}
