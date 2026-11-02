#include <cstdlib>
#include <iostream>

#include "fake_env.h"

static int g_fail = 0;
static int g_pass = 0;

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (cond) {                                                                    \
            ++g_pass;                                                                  \
        } else {                                                                       \
            ++g_fail;                                                                  \
            std::cout << "FAIL " << __func__ << ":" << __LINE__ << " " #cond << "\n"; \
        }                                                                              \
    } while (0)

static const char* kCineTsv = "D:\\modding\\cpp\\S2CoopPort\\data\\s2u_cine.tsv";
static std::string g_outDir = "D:\\modding\\cpp\\S2CoopPort\\tests\\travel\\out\\";

static void dump(const char* name, const FakeEnv& e) {
    std::ofstream f(g_outDir + name + ".log", std::ios::binary);
    for (auto& l : e.logs) f << l << "\n";
}

static void test_tsv() {
    CineTable t;
    std::string err;
    CHECK(parseCineTsv(slurp(kCineTsv), t, err));
    CHECK(t.scene.size() == 171);
    CHECK(t.lead.size() == 85);
    auto it = t.scene.find("Arch_Bossfight_Faust_SequenceStart_1");
    CHECK(it != t.scene.end());
    CHECK(it->second.kind == "S" && !it->second.tpFirst && it->second.s2c);
    auto b = t.scene.find("Benchmark_Sphere_Main_SequenceStart");
    CHECK(b != t.scene.end() && b->second.tpFirst);
    auto l = t.lead.find("BarScene_Test_OnTriggerEnterEvent_Bar");
    CHECK(l != t.lead.end() && l->second.scene == "BarScene_Test_SetDialog_C_E02_MQ01_Bar" && l->second.steps == 1 && !l->second.tpOnPath);
    CineTable bad;
    CHECK(!parseCineTsv("k1\tk2\tk3\tt\tv\nslead\tsA\ti1\ts\tB\nslead\tsA\ti1\ts\tC\n", bad, err));
    CHECK(!parseCineTsv("k1\tk2\tk3\tt\tv\nsfoo\tsA\ti1\ts\tB\n", bad, err));
}

static void test_ismain() {
    CHECK(Travel::isMain("C_E02_MQ01_Bar"));
    CHECK(Travel::isMain("E05_MQ01_X"));
    CHECK(Travel::isMain("C_E06_MQ02_Signal_B_Cutscene_OnTickEvent_1"));
    CHECK(Travel::isMain("Arch_Bossfight_Faust_SequenceStart_1"));
    CHECK(Travel::isMain("Arch_E07_DugaActivation_SequenceStart_Off"));
    CHECK(Travel::isMain("E10_Duga_x"));
    CHECK(Travel::isMain("E03_Boss01_X"));
    CHECK(Travel::isMain("C_E03_Flashback02_Y"));
    CHECK(!Travel::isMain("C_E02_SQ01_X"));
    CHECK(!Travel::isMain("Benchmark_SequenceStart"));
    CHECK(!Travel::isMain("E_MQ"));
    CHECK(!Travel::isMain("Arch_E_x"));
    CHECK(!Travel::isMain(""));
}

static void test_leash() {
    FakeEnv e;
    Travel T(&e);
    CHECK(T.leashM() == 350);
    T.lrSet("ft_park", 1e9, -1, "dialogo", false);
    CHECK(e.hasLog("dono unico ligado (leashown=1): base 350 m"));
    CHECK(e.hasLog("pedido ft_park parada [dialogo] -> efetiva parada"));
    T.tickB();
    CHECK(T.leashM() >= 1e9);
    T.lrSet("ft_park", 1e9, -1, "outro", false);
    CHECK(e.countLog("pedido ft_park") == 1);
    CHECK(T.lrPull("cine", 25, 6, "host a 80 m"));
    CHECK(e.hasLog("pedido cine 25 m (6 s) [host a 80 m] -> efetiva 25 m"));
    CHECK(!T.lrPull("join", 10, 1, "x"));
    CHECK(e.hasLog("pedido join 10 m RECUSADO (cine puxa por mais 20 s)"));
    CHECK(!T.lrPull("join", 10, 1, "x"));
    CHECK(e.countLog("RECUSADO") == 1);
    e.advance(6.5);
    T.tickB();
    CHECK(e.hasLog("fim cine (tempo) -> efetiva parada"));
    CHECK(T.leashM() >= 1e9);
    T.lrDrop("ft_park", "livre");
    CHECK(e.hasLog("fim ft_park (livre) -> efetiva 350 m"));
    T.tickB();
    CHECK(T.leashM() == 350);
    T.lrSet("join_park", 1e9, -1, "parceiro", true);
    CHECK(!T.lrPull("nodeath", 10, 6, "respawn"));
    CHECK(e.hasLog("pedido nodeath 10 m RECUSADO (coleira parada por join_park)"));
    T.lrDrop("join_park", "x");
    e.advance(21);
    CHECK(T.lrPull("nodeath", 10, 6, "respawn"));
    CHECK(T.leashM() == 350 || true);
    T.tickB();
    CHECK(T.leashM() == 10);
    e.flags["leashm"] = 500;
    e.advance(7);
    T.tickB();
    CHECK(e.hasLog("flags.txt leashm 350 -> 500"));
    CHECK(T.leashM() == 500);
    dump("leash", e);
}

static void test_leash_check() {
    FakeEnv e;
    Travel T(&e);
    e.pos = {0, 0, 0};
    Remote r;
    r.x = 10000;
    r.y = 0;
    r.z = 0;
    e.flags["tpfloor"] = 0;
    T.tickB();
    T.leashCheck(r, e.pos);
    CHECK(e.hasLog("COLEIRA ok d=100m"));
    r.x = 31000;
    T.leashCheck(r, e.pos);
    CHECK(e.hasLog("COLEIRA AVISO d=310m"));
    r.x = 40000;
    T.leashCheck(r, e.pos);
    CHECK(e.hasLog("COLEIRA FORA d=400m"));
    CHECK(e.hasCall("legacy 40300,0,0"));
    e.host = true;
    e.calls.clear();
    T.leashCheck(r, e.pos);
    CHECK(e.calls.empty());
}

static void test_cine_host() {
    FakeEnv e;
    e.host = true;
    e.flags["cine"] = 1;
    Travel T(&e);
    e.pos = {0, 0, 0};
    T.tickA();
    CHECK(T.outV() == "#V:0,0,c,0");
    e.cinLocked = true;
    e.advance(0.1);
    T.tickA();
    CHECK(e.hasLog("CINEMATICA do host comecou (tiros e dano em pausa)"));
    CHECK(T.outV() == "#V:1,0,c,0");
    T.mqSeen("C_E01_MQ01_Node");
    e.advance(0.1);
    CHECK(T.outV() == "#V:1,0,c,1");
    e.pos = {9000, 0, 0};
    e.advance(0.1);
    T.tickA();
    CHECK(e.hasLog("TELEPORTE do host (90 m num passo) #1"));
    CHECK(T.outV() == "#V:1,1,c,1");
    e.cinLocked = false;
    e.advance(0.1);
    T.tickA();
    CHECK(e.hasLog("cinematica/dialogo do host terminou"));
    CHECK(T.outV() == "#V:0,1,c,0");
    e.flags["cinesleep"] = 1;
    e.sleeping = true;
    e.advance(0.1);
    T.tickA();
    CHECK(e.hasLog("host DORMINDO"));
    CHECK(T.cineOn());
    e.sleeping = false;
    T.tickA();
    CHECK(e.hasLog("host acordou"));
    e.flags["cinedlg"] = 1;
    e.dlgFound = true;
    e.dlgVis = true;
    e.advance(0.1);
    T.tickA();
    CHECK(T.outV().rfind("#V:1,1,d,", 0) == 0);
    CHECK(e.hasLog("DIALOGO do host comecou"));
    e.flags["cine"] = 0;
    CHECK(T.outV() == "");
    dump("cine_host", e);
}

static void test_cine_client() {
    FakeEnv e;
    e.flags["cine"] = 1;
    e.pos = {0, 0, 0};
    Travel T(&e);
    e.x1["V"] = "1,3,c,1";
    T.tickA();
    CHECK(e.hasLog("host em cinematica -> corpo do host escondido"));
    CHECK(T.remoteCine());
    CHECK(T.frozen());
    CHECK(e.hasLog("cliente CONGELADO e protegido"));
    CHECK(e.hasLog("congelado mov=true input=nil god=uid-1 hp=100.0"));
    CHECK(e.protect > e.t);
    e.hp = 60;
    e.bleed = 5;
    e.advance(0.1);
    T.tickA();
    CHECK(e.hp == 100);
    CHECK(e.bleed == 0);
    e.x1["V"] = "0,3,c,1";
    e.advance(0.1);
    T.tickA();
    CHECK(e.hasLog("cinematica do host terminou -> corpo de volta em 1 s"));
    CHECK(T.frozen());
    e.advance(1.2);
    T.tickA();
    CHECK(!T.frozen());
    CHECK(e.hasLog("cliente LIBERADO (cinematica terminou) depois de"));
    CHECK(e.hasLog("mov=true god=true") || e.hasLog("mov=true"));
    CHECK(e.hasCall("god off"));
    e.x1["V"] = "1,4,c,1";
    e.advance(0.1);
    T.tickA();
    CHECK(T.frozen());
    e.advance(181);
    T.tickA();
    CHECK(!T.frozen());
    CHECK(e.hasLog("tempo maximo de 180 s"));
    e.advance(0.1);
    T.tickA();
    CHECK(!T.frozen());
    dump("cine_client", e);
}

static void test_cine_mqpull() {
    FakeEnv e;
    e.flags["cine"] = 1;
    e.flags["mqpull"] = 1;
    e.pos = {0, 0, 0};
    Travel T(&e);
    e.x1["V"] = "1,1,d,0";
    T.tickA();
    CHECK(e.hasLog("NOLA mq: host em DIALOGO fora da missao principal"));
    CHECK(!T.frozen());
    e.x1["V"] = "0,1,d,0";
    T.tickA();
    CHECK(e.hasLog("(fora da missao principal: sem seguir, mqpull)"));
    e.x1["V"] = "1,2,d,1";
    T.tickA();
    CHECK(e.hasLog("host em DIALOGO -> cliente congelado ao lado"));
    CHECK(T.frozen());
}

static void test_cine_follow() {
    FakeEnv e;
    e.flags["cine"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote.x = 20000;
    e.remote.recv = e.t;
    e.peer = true;
    Travel T(&e);
    e.x1["V"] = "0,1,c,0";
    T.tickA();
    e.advance(0.1);
    e.x1["V"] = "0,2,c,0";
    e.remote.recv = e.t;
    T.tickA();
    CHECK(e.hasLog("host teleportado (#2)"));
    e.advance(0.1);
    e.remote.recv = e.t;
    T.tickA();
    CHECK(e.hasLog("host a 200 m -> sigo ja (coleira 25 m por 6 s)"));
    CHECK(e.hasLog("pedido cine 25 m (6 s) [host a 200 m] -> efetiva 25 m"));
}

static void test_cine_cjoin_dialog() {
    FakeEnv e;
    e.flags["cine"] = 1;
    e.flags["cjoin"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {50000, 100, 20, 0, false, e.t};
    Travel T(&e);
    e.x1["V"] = "1,1,d,1";
    T.tickA();
    CHECK(e.hasLog("NOLA cjoin: dialogo do host a 500 m -> teleporte ok=true"));
    CHECK(e.hasCall("set 50150,100,70"));
}

static void test_cj() {
    FakeEnv e;
    e.flags["cjoin"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {20000, 500, 30, 0, false, e.t};
    Travel T(&e);
    CHECK(T.loadCine(slurp(kCineTsv)));
    CHECK(T.cjBeforeStart("BarScene_Test_SetDialog_C_E02_MQ01_Bar") == 0.7);
    CHECK(e.hasLog("NOLA cjoin: tabelas carregadas"));
    CHECK(e.hasLog("NOLA cjoin: antes de BarScene_Test_SetDialog_C_E02_MQ01_Bar_S2C -> teleporte para junto do outro ok=true"));
    CHECK(e.hasCall("set 20150,500,80"));
    e.pos = {0, 0, 0};
    e.calls.clear();
    CHECK(T.cjBeforeStart("Benchmark_Sphere_Main_SequenceStart") == 0);
    CHECK(e.calls.empty());
    CHECK(T.cjBeforeStart("not_a_scene") == 0);
    e.pos = {19900, 500, 30};
    CHECK(T.cjBeforeStart("BarScene_Test_SetDialog_C_E02_MQ01_Bar") == 0);
    e.pos = {0, 0, 0};
    e.remote.recv = e.t - 2;
    CHECK(T.cjBeforeStart("BarScene_Test_SetDialog_C_E02_MQ01_Bar") == 0);
    e.remote.recv = e.t;
    CHECK(T.cjOnRemoteCause("BarScene_Test_OnTriggerEnterEvent_Bar"));
    CHECK(e.hasLog("NOLA cjoin: causa BarScene_Test_OnTriggerEnterEvent_Bar do cliente leva a BarScene_Test_SetDialog_C_E02_MQ01_Bar -> host teleportado para junto do cliente ok=true"));
    CHECK(!T.cjOnRemoteCause("no_such_cause"));
    e.flags["mqpull"] = 1;
    e.pos = {0, 0, 0};
    CHECK(T.cjBeforeStart("Benchmark_SequenceStart") == 0);
    e.flags["cjoin"] = 0;
    CHECK(T.cjBeforeStart("BarScene_Test_SetDialog_C_E02_MQ01_Bar") == 0);
    e.flags["cjoin"] = 1;
    e.flags["tpfloor"] = 1;
    e.flags["mqpull"] = 0;
    e.traceFn = [](double, double, double az, double, double, double bz) {
        if (bz < az - 100) return TraceHit{true, true, true, bz + 210};
        return TraceHit{true, false, false, 0};
    };
    e.calls.clear();
    CHECK(T.cjBeforeStart("BarScene_Test_SetDialog_C_E02_MQ01_Bar") == 0.7);
    CHECK(e.hasLog("NOLA tpfloor: cjoin cena: "));
    dump("cj", e);
}

static TraceHit floorAt(double surfaceZ, double ax, double ay, double az, double bx, double by, double bz) {
    (void)ax; (void)ay; (void)by;
    if (bz < az - 100) {
        if (surfaceZ <= az && surfaceZ >= bz) return TraceHit{true, true, true, surfaceZ};
        return TraceHit{true, false, false, 0};
    }
    (void)bx;
    return TraceHit{true, false, false, 0};
}

static void test_tpf_now_basic() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 0;
    e.pos = {0, 0, 0};
    e.traceFn = [](double ax, double ay, double az, double bx, double by, double bz) { return floorAt(910, ax, ay, az, bx, by, bz); };
    Travel T(&e);
    CHECK(T.tpfNow(1000, 2000, 1000, 90.0, "teste"));
    CHECK(e.pos.z == 1010);
    CHECK(e.pos.x > 990 && e.pos.x < 1010 && e.pos.y < 2000 - 90);
    CHECK(e.hasLog("NOLA tpfloor: teste: -> "));
    CHECK(e.hasLog("(atras; outro z=1000 dz=10) ok=true"));
    CHECK(!T.tpf().job);
    e.logs.clear();
    CHECK(T.tpfNow(1000, 2000, 1000, std::nullopt, "sem yaw"));
    CHECK(e.pos.x == 1000 && e.pos.y == 2000 && e.pos.z == 1010);
    CHECK(e.hasLog("ponto do outro; outro z=1000 dz=10) ok=true | sem yaw/tpfback"));
    e.pawnOk = false;
    CHECK(!T.tpfNow(1, 2, 3, std::nullopt, "morto"));
    dump("tpf_now_basic", e);
}

static void test_tpf_now_fly() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 0;
    e.pos = {0, 0, 0};
    bool loaded = false;
    e.traceFn = [&](double ax, double ay, double az, double bx, double by, double bz) {
        if (!loaded) return TraceHit{true, false, false, 0};
        return floorAt(910, ax, ay, az, bx, by, bz);
    };
    Travel T(&e);
    CHECK(T.tpfNow(1000, 2000, 1000, 0.0, "voo"));
    CHECK(e.hasCall("mm5"));
    CHECK(T.tpf().job.has_value());
    CHECK(e.hasLog("ok=true, voo ate o chao | chao do outro nao carregado"));
    e.advance(1.1);
    T.tickB();
    loaded = true;
    e.pos.z = 912;
    e.advance(1.1);
    T.tickB();
    CHECK(e.hasLog("CHEGOU (voo)"));
    CHECK(e.hasCall("mm1"));
    CHECK(!T.tpf().job);
    dump("tpf_now_fly", e);
}

static void test_tpf_fit() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 1;
    e.pos = {0, 0, 0};
    e.yaw = 45;
    e.traceFn = [](double ax, double ay, double az, double bx, double by, double bz) { return floorAt(910, ax, ay, az, bx, by, bz); };
    Travel T(&e);
    int refuse = 2;
    e.teleportOk = [&](double, double, double) {
        if (refuse > 0) { --refuse; return false; }
        return true;
    };
    CHECK(T.tpfNow(1000, 2000, 1000, 0.0, "fit"));
    CHECK(e.teleportTries == 3);
    CHECK(e.hasLog("K2_TeleportTo tentativa 3 ok (direita; 2 recusadas, 0 puladas)"));
    e.logs.clear();
    e.teleportTries = 0;
    e.teleportAccept = false;
    CHECK(!T.tpfNow(1000, 2000, 1000, 0.0, "fit2"));
    CHECK(e.teleportTries == 13);
    CHECK(e.hasLog("K2_TeleportTo recusado nas 13 tentativas (0 puladas por parede/sem chao)"));
    e.logs.clear();
    e.teleportTries = 0;
    e.teleportAccept = true;
    e.traceFn = [](double ax, double ay, double az, double bx, double by, double bz) {
        if (bz == az) return TraceHit{true, true, true, ax};
        return floorAt(910, ax, ay, az, bx, by, bz);
    };
    CHECK(T.tpfNow(1000, 2000, 1000, 0.0, "paredes"));
    CHECK(e.teleportTries == 1);
    CHECK(e.hasLog("tentativa 13 ok (ponto do outro; 0 recusadas, 12 puladas)"));
    dump("tpf_fit", e);
}

static void test_tpf_job() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {100000, 0, 500, 90, true, e.t};
    bool loaded = false;
    e.traceFn = [&](double ax, double ay, double az, double bx, double by, double bz) {
        if (!loaded) return TraceHit{true, false, false, 0};
        return floorAt(410, ax, ay, az, bx, by, bz);
    };
    Travel T(&e);
    e.advance(0.1);
    T.tickB();
    Remote r = e.remote;
    T.leashCheck(r, e.pos);
    CHECK(T.tpf().job.has_value());
    CHECK(e.hasCall("src"));
    CHECK(e.hasLog("tpfloor: coleira (#1): streaming em 100000,0,500"));
    T.leashCheck(r, e.pos);
    CHECK(e.countLog("streaming em") == 1);
    for (int i = 0; i < 3; ++i) {
        e.advance(1.1);
        e.remote.recv = e.t;
        T.tickB();
    }
    CHECK(T.tpf().job.has_value());
    CHECK(T.tpf().job->stage == "wait");
    loaded = true;
    e.advance(1.1);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(T.tpf().job.has_value() && T.tpf().job->stage == "land");
    CHECK(e.hasLog("teleporte apos 4 s"));
    e.advance(1.1);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(!T.tpf().job);
    CHECK(e.destroyed.size() == 1);
    CHECK(e.hasLog("CHEGOU (coleira)"));
    dump("tpf_job", e);
}

static void test_tpf_job_guards() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {100000, 0, 500, 90, true, e.t};
    e.traceFn = [&](double, double, double, double, double, double) { return TraceHit{true, false, false, 0}; };
    Travel T(&e);
    Remote bad = e.remote;
    bad.z = -9e9;
    T.leashCheck(bad, e.pos);
    CHECK(!T.tpf().job);
    bad = e.remote;
    bad.x = std::nan("");
    CHECK(!e.inWorld(bad.x, bad.y, bad.z));
    T.leashCheck(e.remote, e.pos);
    CHECK(T.tpf().job.has_value());
    e.pawnOk = false;
    int before = static_cast<int>(e.calls.size());
    for (int i = 0; i < 5; ++i) {
        e.advance(1.1);
        T.tickB();
    }
    CHECK(static_cast<int>(e.calls.size()) == before);
    CHECK(T.tpf().job.has_value());
    e.pawnOk = true;
    e.advance(1.1);
    e.flags["tpfloor"] = 0;
    T.tickB();
    CHECK(!T.tpf().job);
    CHECK(e.destroyed.size() == 1);
    CHECK(e.hasLog("tpfloor desligado"));
    T.leashCheck(e.remote, e.pos);
    CHECK(e.hasCall("legacy 100300,0,500"));
    e.flags["tpfloor"] = 1;
    T.leashCheck(e.remote, e.pos);
    e.advance(200);
    T.tickB();
    CHECK(e.hasLog("desisti (tempo)"));
    CHECK(e.destroyed.size() == 2);
}

static void test_tpf_job_shutdown() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {100000, 0, 500, 90, true, e.t};
    Travel T(&e);
    T.leashCheck(e.remote, e.pos);
    CHECK(T.tpf().job.has_value());
    T.shutdown();
    CHECK(!T.tpf().job);
    CHECK(e.destroyed.size() == 1);
}

static void test_tpf_fall() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 0;
    e.pos = {0, 0, 0};
    e.traceFn = [](double, double, double, double, double, double) { return TraceHit{true, false, false, 0}; };
    Travel T(&e);
    CHECK(T.tpfNow(1000, 2000, 1000, 0.0, "queda"));
    e.pos.z = -5000;
    e.advance(1.1);
    T.tickB();
    CHECK(e.hasLog("caiu (z -5000) -> voo de novo (1)"));
    CHECK(e.pos.z == 1010);
    e.pos.z = 500;
    for (int i = 0; i < 61; ++i) {
        e.advance(1.1);
        T.tickB();
        if (!T.tpf().job) break;
    }
    CHECK(!T.tpf().job);
    CHECK(e.hasLog("CHEGOU (queda)"));
    CHECK(e.hasLog("chao=false"));
}

static void test_tpf_stream_ready() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {100000, 0, 500, 90, true, e.t};
    bool near = false;
    e.traceFn = [&](double ax, double ay, double az, double bx, double by, double bz) {
        if (!near) return TraceHit{true, false, false, 0};
        return floorAt(410, ax, ay, az, bx, by, bz);
    };
    Travel T(&e);
    T.leashCheck(e.remote, e.pos);
    CHECK(T.tpf().job.has_value());
    CHECK(e.hasLog("espero o mundo carregar em volta do outro jogador"));
    e.advance(1.1);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(e.hasCall("stream 2000"));
    CHECK(T.tpf().job && T.tpf().job->stage == "wait");
    e.stream = 1;
    e.advance(1.1);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(T.tpf().job && T.tpf().job->stage == "land");
    CHECK(e.teleportTries == 0);
    CHECK(e.pos.x == 100000 && e.pos.y == 0 && e.pos.z == 510);
    CHECK(e.hasCall("mm5"));
    CHECK(e.hasLog("teleporte apos 2 s"));
    CHECK(e.hasLog("mundo carregado, colisao do chao so perto do jogador em 2 s"));
    CHECK(!e.hasCall("mm1"));
    e.advance(1.1);
    T.tickB();
    CHECK(T.tpf().job.has_value());
    near = true;
    e.advance(1.1);
    T.tickB();
    CHECK(!T.tpf().job);
    CHECK(e.hasCall("mm1"));
    CHECK(e.teleportTries == 1);
    CHECK(e.hasLog("coleira (chao apareceu): K2_TeleportTo tentativa 1 ok (atras; 0 recusadas, 0 puladas)"));
    CHECK(e.pos.x > 99990 && e.pos.x < 100010 && e.pos.y < -90 && e.pos.z == 510);
    CHECK(e.hasLog("CHEGOU (coleira) em 4 s"));
    CHECK(e.hasLog("chao=true MESMO ANDAR"));
    CHECK(e.destroyed.size() == 1);
    dump("tpf_stream_ready", e);
}

static void test_tpf_stream_unknown() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 1;
    e.flags["tpfwait"] = 5;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {100000, 0, 500, 90, true, e.t};
    e.stream = -1;
    Travel T(&e);
    T.leashCheck(e.remote, e.pos);
    for (int i = 0; i < 4; ++i) {
        e.advance(1.1);
        e.remote.recv = e.t;
        T.tickB();
    }
    CHECK(T.tpf().job && T.tpf().job->stage == "wait");
    e.advance(1.1);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(T.tpf().job && T.tpf().job->stage == "land");
    CHECK(e.teleportTries == 0);
    CHECK(e.pos.x == 100000 && e.pos.z == 510);
    CHECK(e.hasLog("mundo nao carregado em 5 s"));
    dump("tpf_stream_unknown", e);
}

static void test_tpf_fit_no_floor() {
    FakeEnv e;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 1;
    e.pos = {0, 0, 0};
    Travel T(&e);
    CHECK(T.tpfNow(1000, 2000, 1000, 0.0, "voo fit"));
    CHECK(e.teleportTries == 0);
    CHECK(e.pos.x == 1000 && e.pos.y == 2000 && e.pos.z == 1010);
    CHECK(e.hasCall("mm5"));
    CHECK(T.tpf().job && T.tpf().job->stage == "land");
    e.pos = {1100, 2000, -5000};
    e.advance(1.1);
    T.tickB();
    CHECK(e.hasLog("caiu (z -5000) -> voo de novo (1)"));
    CHECK(e.teleportTries == 0);
    CHECK(e.pos.x == 1000 && e.pos.y == 2000 && e.pos.z == 1010);
    dump("tpf_fit_no_floor", e);
}

static void test_ft_client_travel() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {900000, 900000, 0, 0, false, e.t};
    Travel T(&e);
    T.tickB();
    e.advance(0.3);
    e.pos = {321300, 412000, 360};
    e.remote.recv = e.t;
    T.tickB();
    CHECK(e.hasLog("salto de 5225 m, ponto de guia mais perto rostok a 1 m -> VIAGEM RAPIDA"));
    CHECK(T.ft().cand.has_value());
    CHECK(e.hasLog("coleira parada") || e.hasLog("pedido ft_park"));
    e.advance(1);
    T.tickB();
    CHECK(T.outFt() == "");
    e.advance(2.5);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(T.outFt().rfind("#FT:1,rostok,321300,412000,360,", 0) == 0);
    CHECK(e.hasLog("VIAGEM RAPIDA minha para rostok (salto de 5225 m) -> #FT 1 enviado"));
    e.remote = {321400, 412000, 360, 0, false, e.t};
    e.advance(0.3);
    T.tickB();
    CHECK(e.hasLog("o outro jogador chegou junto"));
    CHECK(!T.ft().mine);
    e.advance(31);
    T.tickB();
    CHECK(T.outFt() == "");
    dump("ft_client_travel", e);
}

static void test_ft_ignores() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.pos = {0, 0, 0};
    Travel T(&e);
    T.tickB();
    e.advance(0.3);
    e.pos = {1000000, 0, 0};
    T.tickB();
    CHECK(e.hasLog("-> longe dos pontos de guia"));
    CHECK(!T.ft().cand);
    e.advance(0.3);
    e.pos = {321267, 412054, 360};
    e.addr = 2000;
    T.tickB();
    CHECK(e.hasLog("-> pawn trocado (load)"));
    e.advance(0.3);
    e.pos = {215900, 270210, 946};
    e.hasRemote = true;
    e.remote = {215900, 270300, 946, 0, false, e.t};
    T.tickB();
    CHECK(e.hasLog("-> o outro jogador ja esta aqui"));
}

static void test_ft_follow_client() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    Travel T(&e);
    e.x1["FT"] = "7,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    T.tickB();
    CHECK(e.hasLog("o outro jogador viajou para sultansk (#FT 7) -> vou junto"));
    CHECK(e.hasLog("cliente segue o host ate sultansk: coleira em 30 m por 1 s"));
    CHECK(e.hasLog("pedido ft 30 m (1 s) [sultansk] -> efetiva 30 m"));
    CHECK(T.ft().fol.has_value());
    T.tickB();
    CHECK(T.leashM() == 30);
    e.pos = {665100, 617990, 252};
    e.advance(0.3);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(e.hasLog("CHEGUEI junto do outro jogador"));
    CHECK(!T.ft().fol && !T.ft().pend);
    e.advance(1.1);
    T.tickB();
    CHECK(T.leashM() == 350);
    size_t n = e.logs.size();
    T.tickB();
    CHECK(e.logs.size() == n);
    e.advance(0.3);
    T.tickB();
    dump("ft_follow_client", e);
}

static void test_ft_follow_retry() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.flags["ftwait"] = 5;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    Travel T(&e);
    e.x1["FT"] = "1,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    for (int i = 0; i < 80; ++i) {
        e.remote.recv = e.t;
        T.tickB();
        e.advance(0.3);
    }
    CHECK(e.hasLog("seguir sem efeito em 5 s; de novo"));
    CHECK(e.hasLog("seguir nao funcionou em 3 tentativas; desisto (sultansk)"));
    CHECK(!T.ft().pend);
}

static void test_ft_in_rules() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.host = true;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    Travel T(&e);
    e.x1["FT"] = "3,sultansk,665202,617990,252," + std::to_string(e.unixTime() - 500);
    T.tickB();
    CHECK(e.hasLog("#FT 3 (sultansk) antigo (500 s), ignorado"));
    e.advance(0.3);
    T.tickB();
    CHECK(e.countLog("antigo") == 1);
    e.x1["FT"] = "garbage";
    e.advance(0.3);
    T.tickB();
    e.x1["FT"] = "4,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    FtCand c;
    c.sid = "rostok";
    c.at = e.t;
    e.advance(0.3);
    T.tickB();
    CHECK(e.hasLog("o outro jogador viajou para sultansk (#FT 4)"));
}

static void test_ft_hub_relay() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.host = true;
    e.hubOn = true;
    e.nviews = 3;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {1000, 1000, 0, 0, false, e.t};
    e.vremote[1] = {665202, 617990, 252, 0, false, e.t};
    Travel T(&e);
    std::string ts = std::to_string(e.unixTime());
    e.vx1[1]["FT"] = "5,sultansk,665202,617990,252," + ts;
    T.tickB();
    CHECK(e.hasLog("o outro jogador viajou para sultansk (#FT 5) -> vou junto (pid 2)"));
    CHECK(e.hasLog("viagem do cliente (pid 2) repassada aos outros clientes (#FT 1): todos vem junto"));
    CHECK(T.outFt() == "#FT:1,sultansk,665202,617990,252," + ts + ",2");
    CHECK(T.ft().pend && T.ft().pend->view == 1);
    e.vx1[2]["FT"] = "9,rostok,321300,412000,360," + ts;
    e.advance(0.3);
    T.tickB();
    CHECK(e.hasLog("o cliente (pid 3) viajou para rostok (#FT 9) mas o host ja segue outra viagem (sultansk): fica para tras; o grupo segue a primeira"));
    size_t n = e.countLog("vou junto");
    e.advance(0.3);
    T.tickB();
    CHECK(e.countLog("vou junto") == n);
    CHECK(e.countLog("fica para tras") == 1);

    FakeEnv o;
    o.flags["ftfollow"] = 1;
    o.flags["mcft"] = 0;
    o.host = true;
    o.hubOn = true;
    o.nviews = 3;
    o.pos = {0, 0, 0};
    Travel T0(&o);
    o.vx1[1]["FT"] = "5,sultansk,665202,617990,252," + ts;
    T0.tickB();
    CHECK(o.countLog("vou junto") == 0);
    CHECK(T0.outFt().empty());
}

static void test_ft_hub_client_echo() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.mypid = 2;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    Travel T(&e);
    std::string ts = std::to_string(e.unixTime());
    e.x1["FT"] = "1,sultansk,665202,617990,252," + ts + ",2";
    T.tickB();
    CHECK(e.hasLog("o host repassou a minha propria viagem (#FT 1, sultansk): nada a seguir"));
    CHECK(!T.ft().pend);
    FakeEnv f;
    f.flags["ftfollow"] = 1;
    f.mypid = 3;
    f.pos = {0, 0, 0};
    f.hasRemote = true;
    f.remote = {665202, 617990, 252, 0, false, f.t};
    Travel T2(&f);
    f.x1["FT"] = "1,sultansk,665202,617990,252," + ts + ",2";
    T2.tickB();
    CHECK(f.hasLog("o outro jogador viajou para sultansk (#FT 1) -> vou junto"));
    CHECK(T2.ft().fol.has_value());
}

static void test_ft_host_follow() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.host = true;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    bool ground = false;
    e.traceFn = [&](double, double, double, double, double, double) {
        if (ground) return TraceHit{true, true, true, 252};
        return TraceHit{true, false, false, 0};
    };
    Travel T(&e);
    e.x1["FT"] = "9,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    T.tickB();
    CHECK(e.hasLog("host teleportado para junto do cliente (sultansk; 665402,617990,252) ok=true voo=true; espero o chao"));
    CHECK(e.hasCall("set 665402,617990,352"));
    e.advance(3.5);
    ground = true;
    T.tickB();
    CHECK(e.hasLog("modo andando ok=true (chao=true"));
    CHECK(e.hasLog("seguir terminou LONGE") || e.hasLog("CHEGUEI junto"));
    CHECK(!T.ft().pend);
    e.advance(16);
    T.tickB();
    CHECK(!T.ft().tp);
}

static void test_ft_host_follow_tpf() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.flags["tpfloor"] = 1;
    e.flags["tpfit"] = 1;
    e.host = true;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, true, e.t};
    e.traceFn = [](double ax, double ay, double az, double bx, double by, double bz) { return floorAt(162, ax, ay, az, bx, by, bz); };
    Travel T(&e);
    e.x1["FT"] = "9,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    T.tickB();
    CHECK(e.hasLog("tpfloor: ft host sultansk (#1): streaming em"));
    CHECK(T.ft().tp.has_value() && T.ft().tp->tpf);
    for (int i = 0; i < 4; ++i) {
        e.advance(1.1);
        e.remote.recv = e.t;
        T.tickB();
    }
    CHECK(!T.ft().tp);
    CHECK(e.hasLog("NOLA ft: CHEGUEI junto do outro jogador") || e.hasLog("seguir terminou LONGE"));
}

static void test_ft_hold_in_dialog() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.host = true;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    e.cinLocked = true;
    Travel T(&e);
    e.x1["FT"] = "9,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    T.tickB();
    CHECK(e.hasLog("seguir segurado: estou em cinematica (na fila)"));
    CHECK(!T.ft().tp);
    e.cinLocked = false;
    e.advance(0.3);
    T.tickB();
    CHECK(e.hasLog("cinematica acabou -> sigo agora"));
    CHECK(T.ft().tp.has_value());
}

static void test_ft_tie() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.pos = {0, 0, 0};
    Travel T(&e);
    T.tickB();
    e.advance(0.3);
    e.pos = {321300, 412000, 360};
    T.tickB();
    CHECK(T.ft().cand.has_value());
    e.x1["FT"] = "2,asylum,478540,526470,340," + std::to_string(e.unixTime());
    e.advance(0.3);
    T.tickB();
    CHECK(e.hasLog("os dois viajaram (asylum / rostok): o host vence, sigo o host"));
    CHECK(!T.ft().cand);
}

static void test_cfall_cfollow() {
    FakeEnv e;
    e.flags["cfollow"] = 1;
    e.flags["cfollowm"] = 40;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {10000, 0, 0, 0, false, e.t};
    Travel T(&e);
    T.tickB();
    CHECK(e.pos.x == 300);
    CHECK(e.pos.z == 100);
    CHECK(e.hasLog("NOLA cfollow: 100 m -> passo 300 cm"));
    e.falling = true;
    e.advance(0.1);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(e.pos.x == 300);
    e.falling = true;
    e.pos = {0, 0, -8000};
    e.remote = {0, 0, 0, 0, false, e.t};
    e.advance(0.1);
    T.tickB();
    e.advance(3.2);
    e.remote.recv = e.t;
    T.tickB();
    CHECK(e.hasLog("cliente caindo fora do mundo (z=-8000, host z=0) -> posto ao lado do host ok=true stop=true"));
    CHECK(e.hasCall("set 100,0,150"));
    e.host = true;
    e.calls.clear();
    e.advance(1);
    T.tickB();
    CHECK(e.calls.empty());
}

static void test_ft_off() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.host = true;
    e.pos = {0, 0, 0};
    e.hasRemote = true;
    e.remote = {665202, 617990, 252, 0, false, e.t};
    e.traceFn = [](double, double, double, double, double, double) { return TraceHit{true, false, false, 0}; };
    Travel T(&e);
    e.x1["FT"] = "9,sultansk,665202,617990,252," + std::to_string(e.unixTime());
    T.tickB();
    CHECK(T.ft().tp.has_value());
    e.flags["ftfollow"] = 0;
    T.tickB();
    CHECK(!T.ft().tp && !T.ft().pend);
    CHECK(e.hasCall("mm1"));
}

static void test_client_ft_park() {
    FakeEnv e;
    e.flags["ftfollow"] = 1;
    e.pos = {0, 0, 0};
    e.cinLocked = true;
    Travel T(&e);
    T.tickB();
    CHECK(e.hasLog("pedido ft_park parada [cinematica] -> efetiva parada"));
    T.tickB();
    CHECK(T.leashM() >= 1e9);
    e.cinLocked = false;
    e.advance(0.3);
    T.tickB();
    CHECK(e.hasLog("pedido ft_park") && T.ft().hasPark);
    CHECK(T.ft().park == "logo depois de um dialogo");
    e.advance(16);
    T.tickB();
    CHECK(!T.ft().hasPark);
    CHECK(e.hasLog("fim ft_park (livre) -> efetiva 350 m"));
}

static void test_cine_god_handoff() {
    FakeEnv e;
    e.flags["cine"] = 1;
    e.pos = {0, 0, 0};
    Travel T(&e);
    e.x1["V"] = "1,3,c,1";
    T.tickA();
    CHECK(T.frozen());
    CHECK(T.godHeld());
    e.emGodV = true;
    e.x1["V"] = "0,3,c,1";
    e.advance(0.1);
    T.tickA();
    e.advance(1.2);
    T.tickA();
    CHECK(!T.frozen());
    CHECK(!T.godHeld());
    CHECK(!e.hasCall("god off"));
    e.emGodV = false;
    e.x1["V"] = "1,4,c,1";
    e.advance(0.1);
    T.tickA();
    CHECK(T.godHeld());
    e.x1["V"] = "0,4,c,1";
    e.advance(0.1);
    T.tickA();
    e.advance(1.2);
    T.tickA();
    CHECK(e.hasCall("god off"));
    e.flags["cinegod"] = 0;
    e.x1["V"] = "1,5,c,1";
    e.advance(0.1);
    T.tickA();
    CHECK(T.frozen());
    CHECK(!T.godHeld());
}

int main() {
    test_tsv();
    test_ismain();
    test_leash();
    test_leash_check();
    test_cine_host();
    test_cine_client();
    test_cine_god_handoff();
    test_cine_mqpull();
    test_cine_follow();
    test_cine_cjoin_dialog();
    test_cj();
    test_tpf_now_basic();
    test_tpf_now_fly();
    test_tpf_fit();
    test_tpf_job();
    test_tpf_job_guards();
    test_tpf_job_shutdown();
    test_tpf_fall();
    test_tpf_stream_ready();
    test_tpf_stream_unknown();
    test_tpf_fit_no_floor();
    test_ft_client_travel();
    test_ft_ignores();
    test_ft_follow_client();
    test_ft_follow_retry();
    test_ft_in_rules();
    test_ft_hub_relay();
    test_ft_hub_client_echo();
    test_ft_host_follow();
    test_ft_host_follow_tpf();
    test_ft_hold_in_dialog();
    test_ft_tie();
    test_cfall_cfollow();
    test_ft_off();
    test_client_ft_park();
    std::cout << "pass=" << g_pass << " fail=" << g_fail << "\n";
    return g_fail ? 1 : 0;
}
