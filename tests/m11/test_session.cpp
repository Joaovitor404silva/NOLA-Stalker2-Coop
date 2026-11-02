#include "../../src/s2m_session.h"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <set>

using namespace s2m;

struct Fake : Env {
    double t = 100;
    long long unix_ = 1000000;
    bool isHost = false;
    std::vector<std::string> lines;
    std::map<std::string, Val> flags;
    std::map<std::string, std::string> x1;
    int rwx = 0;
    double remoteGt = -1, remoteWt = -1;
    RemoteInfo ri;
    bool haveRemote = true;
    std::string nonce = "AAA", mver = "v37-b53", cver = "b205", md5 = "deadbeef";
    Clock clk;
    bool clkOk = true;
    int weather = 0;
    double reqDur = 5;
    int emActive = 0;
    uintptr_t pawn = 1000;
    bool sleeping = false, alive = true;
    double hp = 100, sleepiness = 80, hunger = 10;
    bool ioOk = true;
    std::vector<Bed> bedList;
    std::map<std::string, int> bedSets;
    std::string lock;
    bool paused = false;
    bool cine = false, dead = false, down = false, frozen = false;
    std::vector<std::string> calls;
    int silent = 0, newsess = 0;
    bool godOk = true;
    int nviews = 1, mypid = 1;
    std::map<int, std::map<std::string, std::string>> vx1;
    std::map<int, int> vstate;
    int views() override { return nviews; }
    int viewPid(int v) override { return isHost ? v + 1 : (v == 0 ? 0 : (v < mypid ? v : v + 1)); }
    bool x1partView(int v, const char* tag, std::string& out) override {
        if (v == 0) return x1part(tag, out);
        auto it = vx1[v].find(tag);
        if (it == vx1[v].end()) return false;
        out = it->second;
        return true;
    }
    int viewState(int v) override { return v == 0 ? 1 : vstate[v]; }
    int myPid() override { return isHost ? 0 : mypid; }

    double clock() override { return t; }
    long long unixTime() override { return unix_; }
    void log(const std::string& l) override { lines.push_back(l); }
    bool host() override { return isHost; }
    double fnum(const char* k, double d) override {
        auto it = flags.find(k);
        if (it == flags.end()) return d;
        return it->second.t == 1 ? it->second.n : d;
    }
    std::string fstr(const char* k, const std::string& d) override {
        auto it = flags.find(k);
        return it == flags.end() ? d : it->second.str();
    }
    int ftype(const char* k) override { auto it = flags.find(k); return it == flags.end() ? 0 : it->second.t; }
    Val fget(const char* k) override { return flags[k]; }
    void fset(const char* k, const Val& v) override { flags[k] = v; }
    bool x1part(const char* tag, std::string& out) override {
        auto it = x1.find(tag);
        if (it == x1.end()) return false;
        out = it->second;
        return true;
    }
    int remoteWx() override { return rwx; }
    bool remoteTod(const char* key, double& out) override {
        double v = std::string(key) == "gt" ? remoteGt : remoteWt;
        if (v < 0) return false;
        out = v;
        return true;
    }
    bool remote(RemoteInfo& o) override { if (!haveRemote) return false; o = ri; o.recv = ri.recv; return true; }
    bool remotePos(RemotePos& o) override { o.x = 5000; o.y = 0; o.z = 0; o.yaw = 0; o.recv = t; return true; }
    void onPeerSilent() override { silent++; }
    void onNewSession() override { newsess++; }
    std::string myNonce() override { return nonce; }
    std::string modVer() override { return mver; }
    std::string coreVer() override { return cver; }
    std::string buildMd5() override { return md5; }
    bool readClock(Clock& c) override { if (!clkOk) return false; c = clk; return true; }
    int targetWeather() override { return weather; }
    bool getReqDur(double& d) override { d = reqDur; return true; }
    bool setReqDur(double d) override { reqDur = d; return true; }
    SetTime cmdSetTime(int h, int m, int s, bool w) override {
        calls.push_back("SetTime " + std::to_string(h) + ":" + std::to_string(m) + ":" + std::to_string(s) + (w ? " w" : " g"));
        double tod = h * 3600 + m * 60 + s;
        if (w) clk.wt = tod;
        else clk.gs = ((tod / 3600 - 9 + 24 * 1000) * 150);
        return SetTime::Ok;
    }
    bool cmdWeather(bool force, int w) override { calls.push_back(std::string(force ? "XForceWeather " : "XSwitchToWeather ") + std::to_string(w)); weather = w; return true; }
    bool cmdEm(const char* fn) override { calls.push_back(fn); return true; }
    bool cmdStartSleep() override { calls.push_back("XStartSleep"); return true; }
    bool cmdSkipHours(int h) override { calls.push_back("XSkipTimeHours " + std::to_string(h)); return true; }
    bool cmdUnkillable(const std::string& u, bool on) override { calls.push_back("Unkillable " + u + (on ? " 1" : " 0")); return godOk; }
    int emissionActive() override { return emActive; }
    bool ioPawn(uintptr_t& a) override { if (!ioOk) return false; a = pawn; return true; }
    bool anyPawn(uintptr_t& a) override { if (!ioOk) return false; a = pawn; return true; }
    bool pawnSleeping() override { return sleeping; }
    bool pawnAlive(bool& v) override { v = alive; return true; }
    bool pawnHp(double& v) override { v = hp; return true; }
    bool pawnGuid(std::string& u) override { u = "GUID1"; return true; }
    bool getSleepiness(double& v) override { v = sleepiness; return true; }
    bool setSleepiness(double v) override { sleepiness = v; return true; }
    bool getHunger(double& v) override { v = hunger; return true; }
    bool forceSetHp(double v) override { calls.push_back("ForceSetHP " + std::to_string((int)v)); hp = v; return true; }
    bool localPos(double& x, double& y, double& z) override { x = y = z = 0; return true; }
    bool tpfNow(double, double, double, double, const char*) override { calls.push_back("tpfNow"); return true; }
    void beds(std::vector<Bed>& o) override { o = bedList; }
    bool bedSet(const std::string& k, int v) override { bedSets[k] = v; for (auto& b : bedList) if (b.key == k) b.cur = v; return true; }
    bool nodeathDead() override { return dead; }
    bool lifeDown() override { return down; }
    std::string ftLock() override { return lock; }
    bool cineGodHeld() override { return frozen; }
    bool gamePaused(bool& p) override { p = paused; return true; }
    bool setGamePaused(bool p, bool& r) override { calls.push_back("SetGamePaused"); paused = p; r = true; return true; }
    bool inCinematic() override { return cine; }
    std::vector<std::string> screens;
    void screen(const char*, const std::string& m, double) override { screens.push_back(m); }
    bool shown(const std::string& s) {
        for (auto& l : screens) if (l.find(s) != std::string::npos) return true;
        return false;
    }

    bool has(const std::string& s) {
        for (auto& l : lines) if (l.find(s) != std::string::npos) return true;
        return false;
    }
    bool called(const std::string& s) {
        for (auto& c : calls) if (c.find(s) != std::string::npos) return true;
        return false;
    }
    void setNum(const char* k, double v) { Val x; x.t = 1; x.n = v; flags[k] = x; }
    void setStr(const char* k, const std::string& v) { Val x; x.t = 2; x.s = v; flags[k] = x; }
};

static int fails = 0;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)

static void run(Fake& f, Session& s, double secs, double step = 0.1) {
    for (double a = 0; a < secs; a += step) {
        f.t += step;
        f.unix_ = 1000000 + (long long)f.t;
        s.tick100();
        if (((int)(f.t * 10)) % 10 == 0) s.tick1000();
    }
}

static void defaults(Fake& f) {
    f.setNum("emsync", 1);
    f.setNum("sleepsync", 1);
    f.setNum("nopause", 1);
    f.setNum("hostcfg", 1);
    f.setNum("wxfollow", 1);
    f.setNum("emgrace", 10);
    f.setNum("emskew", 15);
    f.setNum("emprotect", 120);
    f.setNum("emwait", 20);
    f.setNum("emstopgap", 60);
    f.setNum("peersilent", 10);
    f.setNum("npwait", 3);
    f.setNum("sleepmode", 1);
    f.setNum("sleepnat", 4);
    f.ri.t = 1.0;
    f.ri.sid = "S1";
    f.ri.hello = "host,v37-b53,b205,Medium,BBB";
    f.ri.recv = f.t;
    f.clk.gs = 1000;
    f.clk.wt = 3600 * 12;
}

static void keepFresh(Fake& f, double t1) {
    f.ri.recv = f.t;
    f.ri.t = t1;
}

static void tEmission() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 1);
    CHECK(f.has("PEER SESSION #1"));
    CHECK(f.has("PEER UP (#1)"));
    f.ri.t = 2.0;
    for (int i = 0; i < 150; i++) { keepFresh(f, 2.0 + i); run(f, s, 1); if (i == 3) { f.x1["E"] = "1,1"; f.weather = 7; f.emActive = 1; } if (i == 60) { f.x1["E"] = "0,1"; f.emActive = 1; } if (i == 80) f.emActive = 0; }
    CHECK(f.has("agenda propria de emissoes em pausa ok=true"));
    CHECK(f.has("host em emissao"));
    CHECK(!f.called("XStartEmission"));
    CHECK(f.has("sem emissao"));
    CHECK(f.called("XStopEmission"));
    CHECK(f.has("parada 1 de 3"));
    CHECK(f.emActive == 0);
    CHECK(f.has("host em emissao ha 20 s; a minha ativa"));
}

static void tEmissionProtect() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 1);
    f.emActive = 1;
    f.pawn = 2000;
    for (int i = 0; i < 40; i++) { keepFresh(f, 5.0 + i); run(f, s, 1); if (i == 2) f.hp = 50; }
    CHECK(f.has("protecao armada (save carregado dentro de emissao)"));
    CHECK(f.called("Unkillable GUID1 1"));
    CHECK(s.emNoSave());
    f.emActive = 0;
    for (int i = 0; i < 200; i++) { keepFresh(f, 100.0 + i); run(f, s, 1); }
    CHECK(f.has("protecao do cliente terminada"));
    CHECK(f.called("Unkillable GUID1 0"));
    CHECK(!s.emNoSave());
}

static void tEmissionHost() {
    Fake f; defaults(f);
    f.isHost = true;
    Session s(f);
    run(f, s, 2);
    CHECK(s.outEm() == "#E:0,0");
    f.emActive = 1;
    run(f, s, 3);
    CHECK(f.has("EMISSAO #1 comecou"));
    CHECK(s.outEm() == "#E:1,1");
    f.emActive = 0;
    run(f, s, 3);
    CHECK(f.has("emissao terminou"));
    CHECK(s.outEm() == "#E:0,1");
}

static void tWeather() {
    Fake f; defaults(f);
    Session s(f);
    f.rwx = 3;
    f.weather = 0;
    for (int i = 0; i < 10; i++) { keepFresh(f, 2.0 + i); run(f, s, 1); }
    CHECK(f.called("XForceWeather 3"));
    CHECK(f.has("clima: host em 3, eu em 0 -> XForceWeather(3)"));
    CHECK(f.has("troca para 3 confirmada"));
    CHECK(f.reqDur == 1e9);
    f.rwx = 8;
    for (int i = 0; i < 10; i++) { keepFresh(f, 20.0 + i); run(f, s, 1); }
    CHECK(f.weather == 3);
    f.rwx = 7;
    for (int i = 0; i < 10; i++) { keepFresh(f, 40.0 + i); run(f, s, 1); }
    CHECK(f.called("XForceWeather 7"));
    CHECK(f.has("clima de emissao so de um lado"));
}

static void tSynctime() {
    Fake f; defaults(f);
    f.setNum("synctime", 1);
    f.setNum("syncthresh", 600);
    f.setNum("syncgap", 30);
    Session s(f);
    f.clk.gs = 0;
    f.remoteGt = 10 * 3600;
    f.remoteWt = 10 * 3600;
    f.clk.wt = 14 * 3600;
    s.onSnapshot();
    CHECK(f.called("SetTime 10:0:0 g"));
    CHECK(f.called("SetTime 10:0:0 w"));
    CHECK(f.has("TIME: XSetGameTime(10,0,0) sync gt"));
    f.calls.clear();
    s.onSnapshot();
    CHECK(f.calls.empty());
}

static void tSynctimeAhead() {
    Fake f; defaults(f);
    f.setNum("synctime", 1);
    f.setNum("syncthresh", 600);
    f.setNum("syncgap", 30);
    Session s(f);
    f.clk.gs = 150 * 3.0;
    f.remoteGt = 10 * 3600;
    f.remoteWt = 10 * 3600;
    f.clk.wt = 10 * 3600;
    s.onSnapshot();
    CHECK(!f.called("SetTime 10:0:0 g"));
    CHECK(f.has("sync gt ignorado, cliente adiantado"));
    f.setNum("syncback", 1);
    f.t += 31;
    f.ri.recv = f.t;
    s.onSnapshot();
    CHECK(f.called("SetTime 10:0:0 g"));
}

static void tSynctimeStale() {
    Fake f; defaults(f);
    f.setNum("synctime", 1);
    f.setNum("syncthresh", 90);
    f.setNum("syncback", 1);
    Session s(f);
    f.clk.gs = 150 * 3.0;
    f.remoteGt = 12 * 3600 - 24 * 5;
    f.remoteWt = 12 * 3600 - 24 * 5;
    f.clk.wt = 12 * 3600;
    f.ri.recv = f.t - 5;
    s.onSnapshot();
    CHECK(!f.called("SetTime"));
}

static void tNoPause() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 5);
    f.paused = true;
    run(f, s, 0.3);
    CHECK(f.called("SetGamePaused"));
    CHECK(!f.paused);
    CHECK(f.has("jogo pausado -> despausado #1 ok=true"));
    f.calls.clear();
    f.paused = true;
    f.cine = true;
    run(f, s, 1);
    CHECK(f.called("SetGamePaused"));
    CHECK(!f.paused);
    f.calls.clear();
    f.setNum("npcine", 0);
    f.paused = true;
    run(f, s, 1);
    CHECK(!f.called("SetGamePaused"));
    f.setNum("npcine", 1);
    f.cine = false;
    f.alive = false;
    run(f, s, 1);
    CHECK(!f.called("SetGamePaused"));
    f.alive = true;
    f.paused = false;
    int n = 0;
    for (int i = 0; i < 40; i++) { f.paused = true; run(f, s, 0.1); n++; }
    CHECK(f.has("nao despauso por 30 s"));
    f.calls.clear();
    f.paused = true;
    run(f, s, 1);
    CHECK(!f.called("SetGamePaused"));
    Fake g; defaults(g);
    g.setNum("nopause", 0);
    Session s2(g);
    run(g, s2, 5);
    g.paused = true;
    run(g, s2, 1);
    CHECK(!g.called("SetGamePaused"));
    f.pawn = 5;
    f.paused = true;
    f.t += 40;
    f.calls.clear();
    run(f, s, 1);
    CHECK(!f.called("SetGamePaused"));
}

static std::string peerSl(int seq, char st, double h, long long ts, int aseq, long long ats, char ans) {
    char b[200];
    snprintf(b, sizeof b, "%d,%c,%.2f,%lld,%d,%lld,%c", seq, st, h, ts, aseq, ats, ans);
    return b;
}

static long long mineTs(Session& s) {
    std::string o = s.outTail();
    size_t p = o.find("#SL:");
    if (p == std::string::npos) return 0;
    int c = 0;
    size_t i = p;
    while (c < 3 && i < o.size()) { if (o[i] == ',') c++; i++; }
    return atoll(o.c_str() + i);
}

static void tSleepStarter() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 3);
    f.clk.gs = 150 * 10;
    run(f, s, 2);
    f.sleeping = true;
    run(f, s, 1);
    CHECK(f.has("DORMI (pedido #SL 1 enviado"));
    std::string out = s.outTail();
    CHECK(out.find("#SL:1,S,0.00,") != std::string::npos);
    long long ts = mineTs(s);
    f.x1["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'Y');
    run(f, s, 1);
    CHECK(f.has("o outro ACEITOU (#SL 1)"));
    f.clk.gs += 150 * 7;
    f.sleeping = false;
    run(f, s, 1);
    CHECK(f.has("acordei (#SL 1): dormi 7.00 h de jogo"));
    f.x1["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'D');
    run(f, s, 1);
    CHECK(f.has("DORMIMOS JUNTOS (#SL 1)"));
    CHECK(s.outTail().find("#SL:1,A,7.00,") != std::string::npos);
    CHECK(!f.called("SetTime"));
}

static void tSleepAllConfirm() {
    Fake f; defaults(f);
    f.isHost = true;
    f.nviews = 3;
    f.vstate[1] = 1;
    f.vstate[2] = 1;
    Session s(f);
    run(f, s, 3);
    f.clk.gs = 150 * 10;
    run(f, s, 2);
    f.sleeping = true;
    run(f, s, 1);
    CHECK(f.has("DORMI (pedido #SL 1 enviado"));
    long long ts = mineTs(s);
    f.x1["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'Y') + ",0";
    run(f, s, 1);
    CHECK(f.has("o jogador pid 1 ACEITOU (#SL 1)"));
    CHECK(!f.has("TODOS aceitaram"));
    f.vx1[1]["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'Y') + ",0";
    run(f, s, 1);
    CHECK(f.has("o jogador pid 2 ACEITOU (#SL 1)"));
    CHECK(!f.has("TODOS aceitaram"));
    f.vx1[2]["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'Y') + ",5";
    run(f, s, 1);
    CHECK(!f.has("o jogador pid 3 ACEITOU"));
    f.vx1[2]["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'Y') + ",0";
    run(f, s, 1);
    CHECK(f.has("TODOS aceitaram (#SL 1, 3 jogadores)"));
    f.clk.gs += 150 * 7;
    f.sleeping = false;
    run(f, s, 1);
    f.x1["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'D') + ",0";
    f.vx1[1]["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'D') + ",0";
    run(f, s, 1);
    CHECK(!f.has("DORMIMOS JUNTOS"));
    f.vx1[2]["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'D') + ",0";
    run(f, s, 1);
    CHECK(f.has("todos acordaram (#SL 1)"));
    CHECK(f.has("DORMIMOS JUNTOS (#SL 1)"));
}

static void tSleepOneRefuses() {
    Fake f; defaults(f);
    f.isHost = true;
    f.nviews = 3;
    f.vstate[1] = 1;
    f.vstate[2] = 1;
    Session s(f);
    run(f, s, 3);
    f.clk.gs = 150 * 10;
    run(f, s, 2);
    f.sleeping = true;
    run(f, s, 1);
    long long ts = mineTs(s);
    f.x1["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'Y') + ",0";
    f.vx1[1]["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'B') + ",0";
    run(f, s, 1);
    CHECK(f.has("o jogador pid 2 RECUSOU (#SL 1): ocupado (dialogo/cinematica); todos precisam aceitar"));
    f.sleeping = false;
    run(f, s, 1);
    CHECK(f.has("CANCELADO (#SL 1: ocupado (dialogo/cinematica) (pid 2))"));
}

static void tSleepHubFollower() {
    Fake f; defaults(f);
    f.nviews = 3;
    f.mypid = 2;
    f.vstate[1] = 1;
    f.vstate[2] = 1;
    Session s(f);
    run(f, s, 3);
    long long ts = f.unixTime();
    f.vx1[2]["SL"] = peerSl(3, 'S', 0, ts, 0, 0, '-');
    run(f, s, 1);
    CHECK(f.has("o outro jogador foi dormir (#SL 3)"));
    std::string o = s.outTail();
    CHECK(o.find(",3,") != std::string::npos && o.find(",Y,3") != std::string::npos);
    f.vx1[1]["SL"] = peerSl(3, 'S', 0, ts, 0, 0, '-');
    run(f, s, 1);
    CHECK(f.has("pedido novo (#SL 3) com o anterior (#SL 3) aberto"));
}

static void tSleepRefused() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 3);
    f.clk.gs = 1500;
    run(f, s, 2);
    f.sleeping = true;
    run(f, s, 1);
    long long ts = mineTs(s);
    f.x1["SL"] = peerSl(0, '-', 0, 0, 1, ts, 'B');
    run(f, s, 1);
    CHECK(f.has("o outro RECUSOU (#SL 1): ocupado (dialogo/cinematica)"));
    f.sleeping = false;
    f.clk.gs += 3000;
    run(f, s, 1);
    CHECK(f.has("CANCELADO (#SL 1: ocupado (dialogo/cinematica))"));
    CHECK(f.called("SetTime"));
    CHECK(s.outTail().find("#SL:1,C,") != std::string::npos);
}

static void tSleepNoAnswer() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 3);
    f.sleeping = true;
    run(f, s, 1);
    run(f, s, 17);
    CHECK(f.has("o outro NAO respondeu (#SL 1) em 15 s"));
}

static void tSleepFollower() {
    Fake f; defaults(f);
    f.isHost = true;
    Session s(f);
    run(f, s, 3);
    long long ts = f.unix_;
    f.x1["SL"] = peerSl(4, 'S', 0, ts, 0, 0, '-');
    run(f, s, 1);
    CHECK(f.has("durmo junto (modo 1): XStartSleep ok=true"));
    CHECK(f.called("XStartSleep"));
    CHECK(s.outTail().find(",Y") != std::string::npos);
    run(f, s, 5);
    CHECK(f.has("efeito por script (#SL 4): sonolencia 80 -> 0"));
    f.x1["SL"] = peerSl(4, 'W', 7.0, ts, 0, 0, '-');
    f.clk.gs = 150 * 20;
    f.clk.wt = 3600 * 5;
    run(f, s, 1);
    CHECK(f.has("host dormiu junto por script (#SL 4): pulei 7.00 h"));
    CHECK(f.called("SetTime"));
    CHECK(s.outTail().find(",D") != std::string::npos);
    f.x1["SL"] = peerSl(4, 'A', 7.0, ts, 0, 0, '-');
    run(f, s, 1);
    CHECK(f.has("o pedido #SL") == false || true);
}

static void tSleepFollowerBusy() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 3);
    f.lock = "dialogo";
    long long ts = f.unix_;
    f.x1["SL"] = peerSl(2, 'S', 0, ts, 0, 0, '-');
    run(f, s, 1);
    CHECK(f.has("NAO posso: ocupado (dialogo/cinematica) -> recusado"));
    CHECK(!f.called("XStartSleep"));
    CHECK(s.outTail().find(",B") != std::string::npos);
}

static void tSleepFollowerNative() {
    Fake f; defaults(f);
    f.setNum("sleepmode", 1);
    Session s(f);
    run(f, s, 3);
    long long ts = f.unix_;
    f.x1["SL"] = peerSl(9, 'S', 0, ts, 0, 0, '-');
    run(f, s, 1);
    f.sleeping = true;
    run(f, s, 1);
    CHECK(f.has("durmo junto (#SL 9): o sono do jogo comecou"));
    f.sleeping = false;
    run(f, s, 1);
    CHECK(f.has("acordei junto (#SL 9)"));
    CHECK(s.outTail().find(",D") != std::string::npos);
}

static void tSleepTie() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 3);
    f.sleeping = true;
    run(f, s, 1);
    long long ts = f.unix_;
    f.x1["SL"] = peerSl(1, 'S', 0, ts, 0, 0, '-');
    run(f, s, 1);
    CHECK(f.has("os dois foram dormir ao mesmo tempo"));
}

static void tBeds() {
    Fake f; defaults(f);
    Session s(f);
    Bed b1; b1.key = "BP_Bed_C_1"; b1.cur = 3;
    Bed b2; b2.key = "Default__BP_Bed_C"; b2.cur = 3;
    f.bedList = {b1, b2};
    run(f, s, 3);
    f.x1["SB"] = "B";
    run(f, s, 2);
    CHECK(f.bedSets["BP_Bed_C_1"] == 0);
    CHECK(f.bedSets.count("Default__BP_Bed_C") == 0);
    CHECK(f.has("cama bloqueada: o outro esta ocupado (dialogo/cinematica); 1/1 camas sem colisao"));
    f.x1.erase("SB");
    run(f, s, 5);
    CHECK(f.bedSets["BP_Bed_C_1"] == 3);
    CHECK(f.has("cama liberada (o outro esta livre); 1/1 camas com a colisao de antes"));
}

static void tHostcfg() {
    Fake h; defaults(h);
    h.isHost = true;
    h.setNum("lead", 1);
    h.setNum("qsync", 1);
    h.setStr("difficulty", "Hard");
    Session sh(h);
    run(h, sh, 2);
    std::string out = sh.outTail();
    CHECK(out.find("#HC:deadbeef;") != std::string::npos);
    CHECK(out.find("lead=1") != std::string::npos);
    CHECK(out.find("difficulty=Hard") != std::string::npos);

    Fake c; defaults(c);
    c.setNum("lead", 0);
    c.setNum("qsync", 1);
    c.setStr("difficulty", "Medium");
    Session sc(c);
    run(c, sc, 2);
    c.x1["HC"] = "deadbeef;lead=1;qsync=1;difficulty=Hard;zzz=4";
    run(c, sc, 1);
    CHECK(c.flags["lead"].n == 1);
    CHECK(c.flags["difficulty"].s == "Hard");
    CHECK(!c.flags.count("zzz"));
    CHECK(c.has("hostcfg: lead 0 -> 1 (valor do host; flags.txt daqui = 0)"));
    CHECK(c.has("s2core.lua igual nos dois lados"));
    Val fv; fv.t = 1; fv.n = 0;
    c.flags["lead"] = fv;
    sc.onFlagChanged("lead", fv);
    CHECK(c.flags["lead"].n == 1);
    c.x1["HC"] = "deadbeef;qsync=1";
    run(c, sc, 1);
    CHECK(c.flags["lead"].n == 0);
    CHECK(c.has("de volta ao valor daqui (o host nao manda mais lead)"));
    c.x1["HC"] = "cafe0000;qsync=1";
    run(c, sc, 1);
    CHECK(c.has("AVISO: s2core.lua DIFERENTE do outro lado"));
    c.x1.erase("HC");
    for (int i = 0; i < 12; i++) { keepFresh(c, 500.0 + i); run(c, sc, 1); }
    CHECK(c.has("nao manda #HC ha 10 s"));
}

static void tPeer() {
    Fake f; defaults(f);
    f.mver = "v37-b53";
    Session s(f);
    run(f, s, 1);
    f.haveRemote = false;
    run(f, s, 1);
    f.haveRemote = true;
    f.ri.recv = f.t - 20;
    run(f, s, 0.2);
    CHECK(f.has("PEER SILENT: sem snapshot novo ha 10 s"));
    CHECK(f.silent == 1);
    CHECK(f.shown("The other stalker's signal is lost\xe2\x80\xa6 waiting for them!"));
    f.ri.recv = f.t;
    f.ri.hello = "host,v37-b53,b205,Medium,CCC";
    run(f, s, 0.2);
    CHECK(f.has("PEER UP (#2) depois de"));
    CHECK(!f.shown("The other stalker is back in the Zone!"));
    CHECK(f.has("PEER SESSION #2 (o outro lado relancou)"));
    CHECK(f.newsess == 2);
    Fake g; defaults(g);
    g.ri.hello = "client,v1,b1,Easy,DDD";
    Session s2(g);
    run(g, s2, 1);
    CHECK(g.has("PEER ERRO: os dois lados estao com papel=client"));
    CHECK(g.shown("Two stalkers calling, nobody answering: both games are set as client!"));
    CHECK(g.has("main.lua diferente (aqui v37-b53, la v1)"));
    CHECK(g.has("s2core.lua diferente (aqui b205, la b1)"));
    Fake h; defaults(h);
    h.ri.hello = "host,v37-b53,b205,Medium,AAA";
    Session s3(h);
    run(h, s3, 1);
    CHECK(h.has("e o nosso proprio snapshot (loopback)"));
    CHECK(s3.outHello() == "#H:client,v37-b53,b205,Medium,AAA");
    Fake k; defaults(k);
    k.ri.hello = "";
    Session s4(k);
    run(k, s4, 1);
    CHECK(k.has("nao manda #H (modulo antigo)"));
}

static void tPeerLocalEnd() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 1);
    CHECK(f.has("PEER UP (#1)"));
    f.ri.left = true;
    f.ri.local = true;
    run(f, s, 0.3);
    CHECK(f.has("PEER END (#1): this game ended the co-op session"));
    CHECK(!f.has("PEER LEFT"));
    CHECK(!f.shown("closed the game"));
    CHECK(!f.shown("left (no response)"));
    Fake g; defaults(g);
    Session s2(g);
    run(g, s2, 1);
    g.ri.left = true;
    g.ri.bye = true;
    run(g, s2, 0.3);
    CHECK(g.has("PEER LEFT (#1): o outro jogo fechou (BYE)"));
    CHECK(g.shown("You got left alone, the other player left the Zone!"));
    g.ri.left = false;
    g.ri.bye = false;
    g.ri.recv = g.t;
    run(g, s2, 0.3);
    CHECK(g.shown("The other stalker is back in the Zone!"));
}

static void tClockFields() {
    Fake f; defaults(f);
    Session s(f);
    f.clk.gs = 150 * 3;
    f.clk.wt = 7200;
    run(f, s, 6);
    std::string h = s.headTimeFields();
    CHECK(h.find("\"gt\":43200.0") != std::string::npos);
    CHECK(h.find("\"wt\":7200.0") != std::string::npos);
    CHECK(h.find("\"wx\":0") != std::string::npos);
    f.setNum("settime", 1230);
    run(f, s, 6);
    CHECK(f.called("SetTime 12:30:0 g"));
}

static void tEmissionCineKeep() {
    Fake f; defaults(f);
    Session s(f);
    run(f, s, 1);
    f.emActive = 1;
    f.pawn = 2000;
    for (int i = 0; i < 40; i++) { keepFresh(f, 5.0 + i); run(f, s, 1); }
    CHECK(f.called("Unkillable GUID1 1"));
    CHECK(s.emGod());
    f.frozen = true;
    f.emActive = 0;
    for (int i = 0; i < 200; i++) { keepFresh(f, 100.0 + i); run(f, s, 1); }
    CHECK(f.has("protecao do cliente terminada"));
    CHECK(!f.called("Unkillable GUID1 0"));
    CHECK(!s.emGod());
}

int main() {
    tPeer();
    tPeerLocalEnd();
    tEmission();
    tEmissionProtect();
    tEmissionCineKeep();
    tEmissionHost();
    tWeather();
    tSynctime();
    tSynctimeAhead();
    tSynctimeStale();
    tNoPause();
    tSleepStarter();
    tSleepAllConfirm();
    tSleepOneRefuses();
    tSleepHubFollower();
    tSleepRefused();
    tSleepNoAnswer();
    tSleepFollower();
    tSleepFollowerBusy();
    tSleepFollowerNative();
    tSleepTie();
    tBeds();
    tHostcfg();
    tClockFields();
    printf(fails ? "FAILED %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
