#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace s2t {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};

struct Remote {
    double x = 0, y = 0, z = 0;
    double yaw = 0;
    bool hasYaw = false;
    double recv = 0;
};

struct TraceHit {
    bool ok = false;
    bool hit = false;
    bool hasZ = false;
    double z = 0;
};

using ActorRef = std::uint64_t;

struct Env {
    virtual ~Env() = default;
    virtual bool isHost() = 0;
    virtual double now() = 0;
    virtual long long unixTime() = 0;
    virtual void log(const std::string& line) = 0;
    virtual bool flag(const char* name, double& out) = 0;
    virtual bool peerUp() = 0;
    virtual bool x1part(const char* tag, std::string& out) = 0;
    virtual bool remoteIn(const char* what, Remote& out) = 0;
    virtual int views() { return 1; }
    virtual bool hub() { return false; }
    virtual bool x1partV(int v, const char* tag, std::string& out) { return v == 0 && x1part(tag, out); }
    virtual bool remoteInV(int v, const char* what, Remote& out) { return v == 0 && remoteIn(what, out); }
    virtual int viewPid(int v) { return v == 0 ? (isHost() ? 1 : 0) : -1; }
    virtual int myPid() { return isHost() ? 0 : 1; }
    virtual bool inWorld(double x, double y, double z) = 0;
    virtual bool pawnValid() = 0;
    virtual bool pawnPos(Vec3& out) = 0;
    virtual bool pawnYaw(double& out) = 0;
    virtual std::uint64_t pawnAddr() = 0;
    virtual bool pawnSetLocation(double x, double y, double z) = 0;
    virtual bool pawnTeleportTo(double x, double y, double z, double yaw) = 0;
    virtual bool setMovementMode(int mode) = 0;
    virtual int movementMode() = 0;
    virtual bool disableMovement() = 0;
    virtual bool stopMovement() = 0;
    virtual bool isFalling(bool& out) = 0;
    virtual bool getHP(double& out) = 0;
    virtual bool forceSetHP(double hp) = 0;
    virtual bool getBleeding(double& out) = 0;
    virtual bool forceSetBleeding(double v) = 0;
    virtual bool enableInput() = 0;
    virtual bool disableInput() = 0;
    virtual std::string unkillableOn() = 0;
    virtual bool unkillableOff(const std::string& uid) = 0;
    virtual bool pcCinematicLocked() = 0;
    virtual bool pawnSleeping() = 0;
    virtual void dialogRescan() = 0;
    virtual bool dialogValid() = 0;
    virtual bool dialogVisible() = 0;
    virtual TraceHit trace(double ax, double ay, double az, double bx, double by, double bz) = 0;
    virtual ActorRef spawnStreamingSource(double x, double y, double z) = 0;
    virtual int streamReady(double x, double y, double z, double radius) = 0;
    virtual void destroyActor(ActorRef ref) = 0;
    virtual bool legacySafeTeleport(double x, double y, double z) = 0;
    virtual bool lifeDown() = 0;
    virtual bool lifeDownFrozen() = 0;
    virtual bool emGodHeld() = 0;
    virtual bool ndDead() = 0;
    virtual bool leadLockMine() = 0;
    virtual void dmgProtectUntil(double t) = 0;
};

struct SceneRow {
    std::string kind;
    bool tpFirst = false;
    bool s2c = false;
};

struct LeadRow {
    std::string scene;
    int steps = 0;
    bool tpOnPath = false;
};

struct CineTable {
    std::unordered_map<std::string, SceneRow> scene;
    std::unordered_map<std::string, LeadRow> lead;
};

bool parseCineTsv(const std::string& text, CineTable& out, std::string& err);

struct LeashReq {
    double m = 0;
    bool hasUntil = false;
    double untilT = 0;
    bool hard = false;
    std::string why;
};

struct Leash {
    bool on = false;
    std::map<std::string, LeashReq> req;
    std::map<std::string, std::string> refused;
    bool hasPull = false;
    std::string pullName;
    double pullUntil = 0;
    double base = 350;
    bool hasLast = false;
    double last = 0;
    double flagSeen = 0;
    bool hasFlagSeen = false;
};

struct Freeze {
    double at = 0;
    bool hasAddr = false;
    std::uint64_t addr = 0;
    std::optional<double> hp;
    bool move = false;
    std::optional<bool> input;
    std::string god;
    int held = 0;
};

struct Cine {
    bool on = false;
    int tseq = 0;
    std::string kind;
    bool sleep = false;
    double dwAt = 0;
    bool hasDw = false;
    bool hasPos = false;
    Vec3 pos;
    double posT = 0;
    std::string rk;
    bool rm = false;
    bool mEp = false;
    bool rc = false;
    double hideUntil = 0;
    bool cjWant = false;
    bool hasFollow = false;
    double followUntil = 0;
    bool hasRts = false;
    long rts = 0;
    std::optional<Freeze> fz;
    bool fzBlock = false;
    bool hasMq0 = false;
    double mq0 = 0;
    bool mqOn = false;
};

struct MqStamp {
    bool has = false;
    double at = 0;
    std::string sid;
};

struct TpfJob {
    std::string why;
    double x = 0, y = 0, z = 0;
    bool hasYaw = false;
    double yaw = 0;
    double t0 = 0;
    double next = 0;
    std::string stage;
    int w = 0;
    int m = 0;
    ActorRef src = 0;
    double tx = 0, ty = 0, tz = 0;
    int falls = 0;
    bool aside = false;
};

struct Tpf {
    int n = 0;
    std::optional<TpfJob> job;
};

struct FtCand {
    std::string sid;
    double at = 0;
    double d = 0;
};

struct FtMine {
    double at = 0;
    std::string sid;
    int seq = 0;
};

struct FtPend {
    std::string seq;
    std::string sid;
    double x = 0, y = 0, z = 0;
    double at = 0;
    int tries = 0;
    std::string held;
    int view = 0;
};

struct FtFol {
    double lowUntil = 0;
    bool hasLow = false;
    double waitUntil = 0;
    double t0 = 0;
    int tries = 0;
};

struct FtTp {
    bool tpf = false;
    double t0 = 0;
    std::string sid;
    double x = 0, y = 0, z = 0;
    std::string stage;
    double t1 = 0;
    double t2 = 0;
    int falls = 0;
    bool told = false;
    double next = -1000;
};

struct FtLp {
    Vec3 p;
    std::uint64_t addr = 0;
};

struct Ft {
    double nextAt = -1000;
    std::string lk;
    bool hasLp = false;
    FtLp lp;
    double selfUntil = -1000;
    std::optional<FtCand> cand;
    std::optional<FtMine> mine;
    int seq = 0;
    std::string ev;
    double evUntil = 0;
    std::string rkey;
    std::string rkeyV[4];
    std::optional<FtPend> pend;
    std::optional<FtFol> fol;
    std::optional<FtTp> tp;
    bool hasPark = false;
    std::string park;
    double dlgAt = -1000;
    double dwAt = -1000;
};

class Travel {
public:
    explicit Travel(Env* env) : env_(env) {}
    bool loadCine(const std::string& tsvText);

    void tickA();
    void tickB();
    std::string outV();
    std::string outFt();
    bool remoteCine();
    bool cineOn() const { return cv_.on; }
    bool frozen() const { return cv_.fz.has_value(); }
    bool godHeld() const { return cv_.fz && !cv_.fz->god.empty(); }
    std::string ftLockNow();
    void shutdown();

    double leashM();
    void leashCheck(const Remote& r, const Vec3& p);
    void lrSet(const std::string& name, double m, double secs, const std::string& why, bool hard);
    void lrDrop(const std::string& name, const std::string& why);
    bool lrPull(const std::string& name, double m, double secs, const std::string& why);

    bool tpfNow(double x, double y, double z, std::optional<double> yaw, const std::string& why);

    void mqSeen(const std::string& sid);
    bool mqSid(const std::string& sid);
    static bool isMain(const std::string& sid);

    double cjBeforeStart(const std::string& sid);
    bool cjOnRemoteCause(const std::string& sid);

    const Cine& cine() const { return cv_; }
    const Leash& leash() const { return lr_; }
    const Tpf& tpf() const { return tpf_; }
    const Ft& ft() const { return ft_; }
    const CineTable& table() const { return cj_; }

private:
    double fl(const char* name, double def);
    bool role_client() { return !env_->isHost(); }
    void log(const std::string& s) { env_->log(s); }

    static std::string lrFmt(double m);
    double lrEff(double now);
    void lrOn();
    void lrApply();

    bool mqOk();
    int mqFlag();
    void cineTick0();
    void cineFreeze();
    bool cineRestore(const std::string& why);

    void cjTables();
    bool cjBeside(double x, double y, double z);
    bool cjFirstSight(const std::string& sid);

    std::optional<double> tpfFloor(double x, double y, double z);
    struct Spot {
        double x, y, z;
        std::string how, tr;
    };
    Spot tpfSpot(double x, double y, double z, std::optional<double> yaw);
    struct Cand {
        double x, y;
        std::string label;
    };
    std::vector<Cand> tpfCands(double x, double y, std::optional<double> yaw);
    struct FitResult {
        bool ok;
        double x, y, z;
        std::string label;
    };
    FitResult tpfFit(double x, double y, double z, std::optional<double> yaw, bool floor, const std::string& why);
    void tpfEnd(const char* why);
    bool tpfStart(double x, double y, double z, std::optional<double> yaw, const std::string& why);
    bool tpfLeash(double x, double y, double z);
    void tpfStep(TpfJob& J, double now);
    void tpfTick();

    void cfollowTick();

    static const char* ftNear(double x, double y, double z, double& dist);
    bool ftMe(Vec3& out);
    bool ftRemote(double now, double maxAge, Vec3& out);
    std::string ftLock(double now);
    void ftPark(bool on, const std::string& why);
    void ftSee(double now);
    void ftSend(double now);
    void ftWaitOther(double now);
    void ftIn(double now);
    void ftInV(double now, int v, const std::string& sg);
    int ftNv();
    bool ftArrived(const Vec3* q, double now, double t0);
    bool ftGround(double x, double y, double z);
    void ftTpStep(double now);
    void ftFollow(double now);
    void ftTick();

    Env* env_;
    CineTable cj_;
    bool cjLoaded_ = false;
    bool cjAnnounced_ = false;
    Cine cv_;
    MqStamp mq_;
    Leash lr_;
    std::string leashState_;
    Tpf tpf_;
    Ft ft_;
    bool hasCfFall_ = false;
    double cfFallT_ = 0;
    double cfLog_ = 0;
};

std::string luaNum(double v);
std::string luaFloat(double v);

}

void s2m_travel_init(s2t::Env* env, const std::string& cineTsvText);
bool s2m_travel_ready();
void s2m_travel_tick_a();
void s2m_travel_tick_b();
std::string s2m_travel_out_v();
std::string s2m_travel_out_ft();
bool s2m_travel_remote_cine();
bool s2m_travel_cine_on();
bool s2m_travel_frozen();
bool s2m_travel_god_held();
bool s2m_travel_tpf_busy();
std::string s2m_travel_ft_lock();
void s2m_travel_shutdown();
double s2m_travel_leash_m();
void s2m_travel_leash_check(const s2t::Remote& r, const s2t::Vec3& p);
void s2m_travel_lr_set(const std::string& name, double m, double secs, const std::string& why, bool hard);
void s2m_travel_lr_drop(const std::string& name, const std::string& why);
bool s2m_travel_lr_pull(const std::string& name, double m, double secs, const std::string& why);
bool s2m_travel_pull_now(double x, double y, double z, std::optional<double> yaw, const std::string& why);
void s2m_travel_mq_seen(const std::string& sid);
bool s2m_travel_mq_sid(const std::string& sid);
bool s2m_travel_is_main(const std::string& sid);
double s2m_travel_cj_before_start(const std::string& sid);
bool s2m_travel_cj_on_remote_cause(const std::string& sid);
