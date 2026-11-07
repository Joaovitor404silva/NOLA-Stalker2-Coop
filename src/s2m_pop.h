#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace s2m_pop {

using Id = uint64_t;

struct V3 { double x = 0, y = 0, z = 0; };

enum class PawnClass { All, Humans };

struct Body {
    bool ok = false;
    Id pawn = 0;
    bool hasUid = false;
    long long uid = 0;
    bool spawnPending = false;
};

struct Cont { Id id = 0; double x = 0, y = 0, z = 0; };

class Env {
public:
    virtual ~Env() {}
    virtual double now() = 0;
    virtual double flag(const char* name, double def) = 0;
    virtual bool isHost() = 0;
    virtual bool coop() { return true; }
    virtual void log(const std::string& line) = 0;
    virtual bool readFile(const char* name, std::string& out) = 0;

    virtual bool localPawn(Id& id, V3& at) = 0;
    virtual void pawns(PawnClass c, std::vector<Id>& out) = 0;
    virtual bool valid(Id p) = 0;
    virtual bool alive(Id p) = 0;
    virtual bool loc(Id p, V3& at) = 0;
    virtual bool hiddenFlag(Id p) = 0;
    virtual bool maxHp(Id p, double& hp) = 0;
    virtual std::string className(Id p) = 0;
    virtual std::string shortName(Id p) = 0;
    virtual std::string meshName(Id p, const char* comp) = 0;

    virtual bool ownBodyBase(Id p) = 0;
    virtual bool natOwn(Id p) = 0;
    virtual bool natListed(Id p) = 0;
    virtual bool natListedUid(long long uid) = 0;
    virtual bool natPark(Id, long long) { return false; }
    virtual bool inert(Id, long long, bool) { return false; }
    virtual bool storyNpc(Id p, std::string& guid) = 0;

    virtual bool setHidden(Id p, bool hidden) = 0;
    virtual bool setCollision(Id p, bool on) = 0;
    virtual bool setInteractionOff(Id p) = 0;
    virtual bool destroyActor(Id p) = 0;

    virtual bool aiFeatures(bool off, long long mask) = 0;
    virtual bool aLife(bool off) = 0;
    virtual bool inThreat(Id p) = 0;
    virtual bool focusIsLocal(Id p) = 0;
    virtual bool guid(Id p, long long& uid) = 0;
    virtual bool protoId(Id p, long long& id) = 0;
    virtual bool killByUid(long long uid) = 0;
    virtual bool destroyModel(long long uid) = 0;
    virtual bool despawnDeadAtRadius(double r) = 0;

    virtual Body target() = 0;
    virtual Body puppet() = 0;
    virtual int bodyViews() { return 1; }
    virtual bool bodyViewLive(int view) { return view == 0; }
    virtual Body target(int view) { return view == 0 ? target() : Body{}; }
    virtual Body puppet(int view) { return view == 0 ? puppet() : Body{}; }
    virtual bool worldKey(std::string& key) = 0;
    virtual bool worldOpen() = 0;
    virtual bool clPending() = 0;

    virtual bool inventoryOpen() = 0;
    virtual void containers(std::vector<Cont>& out) = 0;
    virtual bool trace(double ax, double ay, double az, double bx, double by, double bz, bool& hit, double& iz) = 0;
    virtual bool simPhysicsOff(Id p) = 0;
    virtual bool setLoc(Id p, double x, double y, double z) = 0;
};

struct QGuard {
    bool active = false;
    Id pawn = 0;
    double at = 0;
    bool dead = false;
    bool hasOk = false;
    double ok = 0;
};

struct QSense {
    Id pawn = 0;
    bool alive = true;
    bool hasHp = false;
    double hp = 0;
    bool ndDead = false;
    bool lifeDown = false;
};

struct QRes {
    bool skip = false;
    bool dropPending = false;
    bool rebase = false;
};

void init(Env* env);
void shutdown();
void tick1000();

bool nearHub(double x, double y);
bool nearTrade(double x, double y);
bool isHiddenLocal(Id p);
bool isVendor(Id p, double* distCm = nullptr);
bool vendSkip(Id p);
bool ownBody(Id p);

QRes qgDead(QGuard& g, const QSense& s, bool rebase);
void qfArm(const char* why);

size_t loadVendorRows(const std::string& tsv);
size_t vendorRowCount();

struct VendHit { std::string kind, obj; double dist = 0; };
bool vendorInfo(Id p, VendHit& out);
bool vendorNear(double x, double y, double z, double r, VendHit& out);

}
