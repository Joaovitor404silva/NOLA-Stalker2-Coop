#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace s2m {

struct Val {
    int t = 0;
    double n = 0;
    std::string s;
    std::string str() const;
    bool operator==(const Val& o) const { return t == o.t && n == o.n && s == o.s; }
};

struct Clock {
    double gs = 0;
    double gt = 0;
    std::optional<double> wt;
};

struct Bed {
    std::string key;
    int cur = -1;
};

struct RemoteInfo {
    double recv = 0;
    std::optional<double> t;
    std::string sid;
    std::string hello;
    bool left = false;
    bool bye = false;
    bool local = false;
};

struct RemotePos {
    double x = 0, y = 0, z = 0, yaw = 0, recv = 0;
};

enum class SetTime { NoMgr, Fail, Ok };

class Env {
public:
    virtual ~Env() {}
    virtual double clock() = 0;
    virtual long long unixTime() = 0;
    virtual void log(const std::string& line) = 0;
    virtual void screen(const char*, const std::string&, double) {}
    virtual void screenClear(const char*) {}
    virtual bool host() = 0;
    virtual bool coop() { return true; }

    virtual double fnum(const char* key, double def) = 0;
    virtual std::string fstr(const char* key, const std::string& def) = 0;
    virtual int ftype(const char* key) = 0;
    virtual Val fget(const char* key) = 0;
    virtual void fset(const char* key, const Val& v) = 0;

    virtual bool x1part(const char* tag, std::string& out) = 0;
    virtual int remoteWx() = 0;
    virtual bool remoteTod(const char* key, double& out) = 0;
    virtual bool remote(RemoteInfo& out) = 0;
    virtual bool remotePos(RemotePos& out) = 0;
    virtual int views() { return 1; }
    virtual int maxClients() { return 1; }
    virtual int viewPid(int v) { return v == 0 ? (host() ? 1 : 0) : -1; }
    virtual bool remoteView(int v, RemoteInfo& out) { return v == 0 && remote(out); }
    virtual bool x1partView(int v, const char* tag, std::string& out) { return v == 0 && x1part(tag, out); }
    virtual bool remotePosView(int v, RemotePos& out) { return v == 0 && remotePos(out); }
    virtual int viewState(int v) { return v == 0 ? 1 : 0; }
    virtual int myPid() { return host() ? 0 : 1; }
    virtual void onPeerSilent() = 0;
    virtual void onNewSession() = 0;
    virtual std::string myNonce() = 0;
    virtual std::string modVer() = 0;
    virtual std::string coreVer() = 0;
    virtual std::string buildMd5() = 0;

    virtual bool readClock(Clock& c) = 0;
    virtual int targetWeather() = 0;
    virtual bool getReqDur(double& d) = 0;
    virtual bool setReqDur(double d) = 0;
    virtual SetTime cmdSetTime(int h, int m, int s, bool weather) = 0;
    virtual bool cmdWeather(bool force, int w) = 0;
    virtual bool cmdEm(const char* fn) = 0;
    virtual bool cmdStartSleep() = 0;
    virtual bool cmdSkipHours(int h) = 0;
    virtual bool cmdUnkillable(const std::string& uid, bool on) = 0;
    virtual int emissionActive() = 0;

    virtual bool ioPawn(uintptr_t& addr) = 0;
    virtual bool anyPawn(uintptr_t& addr) = 0;
    virtual bool pawnSleeping() = 0;
    virtual bool pawnAlive(bool& v) = 0;
    virtual bool pawnHp(double& v) = 0;
    virtual bool pawnGuid(std::string& uid) = 0;
    virtual bool getSleepiness(double& v) = 0;
    virtual bool setSleepiness(double v) = 0;
    virtual bool getHunger(double& v) = 0;
    virtual bool forceSetHp(double v) = 0;
    virtual bool localPos(double& x, double& y, double& z) = 0;
    virtual bool tpfNow(double x, double y, double z, double yaw, const char* why) = 0;

    virtual void beds(std::vector<Bed>& out) = 0;
    virtual bool bedSet(const std::string& key, int v) = 0;

    virtual bool nodeathDead() = 0;
    virtual bool lifeDown() = 0;
    virtual std::string ftLock() = 0;
    virtual bool cineGodHeld() = 0;

    virtual bool gamePaused(bool& paused) = 0;
    virtual bool setGamePaused(bool paused, bool& ret) = 0;
    virtual bool inCinematic() = 0;
};

struct Peer {
    std::string state = "none";
    int live = 0;
    bool loopback = false;
    bool warned = false;
    bool firstSeen = false;
    bool helloOk = false;
    std::optional<double> firstT;
    bool hasId = false;
    std::string id, sid, nonce, role, mod, core, diff;
    int ups = 0;
    int sessions = 0;
    int lefts = 0;
    double silentAt = 0;
    double leftAt = 0;
};

struct Stats {
    std::optional<double> sl, hp, hu;
};

struct Pre {
    bool valid = false;
    Clock c;
    Stats st;
    double at = 0;
};

struct Mine {
    int seq = 0;
    long long ts = 0;
    char st = 'S';
    double h = 0;
    double t0 = 0;
    Pre pre;
    double lim = 0;
    double untilT = 0;
    std::optional<double> endAt, ok, done, woke;
    std::string no;
    char ra = 0;
    int nv = 1;
    std::map<int, char> va;
};

struct Fol {
    std::string key;
    int seq = 0;
    long long ts = 0;
    double t0 = 0;
    Pre pre;
    std::optional<double> h;
    std::string phase;
    std::optional<double> virt, nat, cancel, closed, undone, doneAt;
    bool skip = false;
    int view = 0;
    int pid = -1;
};

struct Ans {
    int seq = 0;
    long long ts = 0;
    char a = '-';
    double untilT = 0;
    int pid = -1;
};

class Session {
public:
    explicit Session(Env& env);
    void tick100();
    void tick1000();
    void onSnapshot();
    void onFlagChanged(const std::string& key, const Val& after);
    std::string outHello();
    std::string outEm();
    std::string outTail();
    std::string headTimeFields();
    bool emNoSave() const { return em_.noSave; }
    bool emGod() const { return !em_.god.empty(); }
    const Peer& peer() const { return peer_; }
    int errors() const { return errs_; }
    bool setGameTime(int h, int m, int s, const std::string& why, bool weather);

    static const char* kHcKeys;

private:
    struct Wx {
        std::optional<uintptr_t> paddr;
        int sess = -1;
        bool instant = false;
        bool done = false;
        double lockAt = 0;
        std::optional<double> checkAt;
        int checkWant = 0;
        std::string checkHow;
        bool retried = false;
        int host = -2;
        int mine = -2;
        int setTo = -2;
        double next = 0;
        double retryAt = 0;
    };
    struct Em {
        int n = 0;
        std::optional<bool> on;
        double at = 0;
        bool paused = false;
        std::optional<uintptr_t> addr;
        double stopAt = 0;
        int stops = 0;
        std::optional<double> offAt;
        std::optional<double> newAt;
        std::optional<double> gs0;
        bool ran = false;
        std::string god;
        double godAt = 0;
        bool godLogged = false;
        std::optional<double> hp;
        double calmAt = 0;
        bool noSave = false;
        std::optional<double> hostOnAt, mineOn;
        bool armed = false;
        bool ron = false;
        int hk = 0;
        std::optional<double> hostSince;
        int seenK = -1;
        int chkK = -1;
        int startedN = -1;
    };
    struct Sl {
        int seq = 0;
        bool active = false;
        std::optional<uintptr_t> addr;
        std::optional<bool> wasSlp;
        double nextAt = -1000;
        double hurtAt = -1000;
        std::optional<double> lastHp;
        double jumpAt = -1000;
        Pre pre, prePrev;
        std::optional<Mine> mine;
        std::optional<Fol> fol;
        std::optional<Ans> ans;
        std::string rkey;
        std::string rkeyV[4];
        double coolUntil = -1000;
        std::optional<double> park;
        std::string busyTx;
        char obusy = 0;
        char bedOff = 0;
        double bedAt = 0;
        std::map<std::string, int> bedSv;
    };
    struct Hc {
        std::map<std::string, Val> own, ad;
        std::string sumKey, mdKey, noLog;
        std::optional<double> noAt;
        std::string outS;
        double outAt = 0;
        bool hasOut = false;
    };
    struct Np {
        int n = 0, sup = 0;
        double logAt = -1e9;
        std::vector<double> hits;
        std::optional<uintptr_t> addr;
        double at = 0;
        double holdUntil = 0;
    };

    double F(const char* k, double d) { return env_.fnum(k, d); }
    void log(const std::string& s) { env_.log(s); }

    void refreshClock();
    void fastTime();
    void peerTick();
    void viewsTick();
    int playersUp() const;
    std::string leftMsg(int pid) const;
    void wxTick();
    void emTick();
    void noPause();
    void slTick();
    void sleepStep();
    void hcTick();

    void emRelease(const std::string& why);
    bool emReset(const std::string& why);
    void wxLock(double now, const std::string& why);

    std::optional<Clock> slClock();
    Stats slStats();
    bool slSetTod(std::optional<double> gt, std::optional<double> wt, const std::string& why);
    bool slSetChk(std::optional<double> gt, std::optional<double> wt, const std::string& why, std::optional<Clock> c0, std::string& msg, std::optional<Clock>& r0, std::optional<Clock>& r1);
    bool slSkip(double h, const std::string& why, const Pre& pre, std::string& msg);
    void slUndo(const Pre& pre, double now, const std::string& why);
    void slBed(double now);
    void slBeds(char ob, double now);
    void slPark(std::optional<double> untilT);
    char slBusy(double now);
    void slAnswer(const Fol& Fo, char a, double now);
    void slAsk(double now, const std::string& key, int seq, long long ts, std::optional<double> h, int view = 0, int pid = -1);
    bool slPull(int seq, int view = 0);
    void slIn(double now);
    void slInV(double now, int v, const std::string& sg);
    int slNv();
    void slMineAns(int v, char ans, double now);
    void hcGiveBack(const std::string& why, const std::string* only);
    void hcAdopt(const std::string& rest);

    Env& env_;
    Peer peer_;
    static constexpr int kViews = 4;
    Peer vpeer_[kViews];
    Wx wx_;
    Em em_;
    Sl sl_;
    Hc hc_;
    Np np_;
    std::optional<double> gameSecs_, gtod_, wtod_;
    std::optional<int> wxCache_;
    double clkAt_ = -1e9;
    double wxAt_ = 0;
    double syncAtGt_ = 0, syncAtWt_ = 0;
    int settimeDone_ = 0;
    bool cslp_ = false;
    int errs_ = 0;
    int errSleep_ = 0, errHc_ = 0, errNp_ = 0, errTick_ = 0;
};

std::unique_ptr<Session> session_create(Env& env);

}
