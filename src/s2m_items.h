#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "s2m_save.h"
#include "s2x_istate.h"

#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
namespace cen
{
    struct Row;
    void ev(const char* fam, const char* why, const std::string& key);
}
#define NOLA_CEN_EV(f, w, k) ::cen::ev(f, w, k)
#else
#define NOLA_CEN_EV(f, w, k) ((void)0)
#endif

namespace s2m_items
{
    using Obj = uint64_t;
    using Counts = std::map<std::string, long long>;

    struct V3
    {
        double x = 0, y = 0, z = 0;
    };

    class Env
    {
      public:
        virtual ~Env() {}
        virtual double now() = 0;
        virtual bool host() = 0;
        virtual double fnum(const char* key, double def) = 0;
        virtual bool fhas(const char* key) = 0;
        virtual std::string fstr(const char* key, const std::string& def) = 0;
        virtual void log(const std::string& line) = 0;

        virtual bool part(const char* tag, std::string& out) = 0;
        virtual bool peerLoopback() = 0;
        virtual int peerSessions() = 0;
        virtual bool peerSilent() = 0;
        virtual bool remote(double& x, double& y, double& recv) = 0;
        virtual bool lookOn() = 0;
        virtual std::string lookPut(const std::string& text) = 0;
        virtual bool lookGet(const std::string& hash, std::string& text) = 0;
        virtual std::optional<double> gameSecs() = 0;
        virtual double wallSecs() = 0;
        virtual int random(int lo, int hi) = 0;
        virtual std::optional<double> keyLatch() = 0;
        virtual bool keyDownG() = 0;

        virtual bool readText(const std::string& name, std::string& out) = 0;
        virtual bool appendText(const std::string& name, const std::string& text) = 0;
        virtual bool writeText(const std::string& name, const std::string& text) = 0;
        virtual bool dataText(const std::string& name, std::string& out) = 0;

        virtual bool valid(Obj o) = 0;
        virtual Obj pawn() = 0;
        virtual Obj controller() = 0;
        virtual Obj first(const char* cls) = 0;
        virtual std::vector<Obj> all(const char* cls) = 0;
        virtual uint64_t addr(Obj o) = 0;
        virtual std::string name(Obj o) = 0;
        virtual std::string cls(Obj o) = 0;
        virtual std::string full(Obj o) = 0;
        virtual bool loc(Obj o, V3& out) = 0;
        virtual bool setLoc(Obj o, const V3& v) = 0;
        virtual bool alive(Obj o, bool& out) = 0;
        virtual bool maxHp(Obj o, double& out) = 0;
        virtual bool hidden(Obj o, bool& out) = 0;
        virtual bool setHidden(Obj o, bool on) = 0;
        virtual bool setCollision(Obj o, bool on) = 0;
        virtual bool setActorTick(Obj o, bool on) = 0;
        virtual Obj interacted(Obj pawnOrPc) = 0;
        virtual bool closeLoot(Obj pawnOrPc) = 0;
        virtual bool visible(Obj widget, bool& out) = 0;
        virtual bool natItems(Obj o, int where, bool all, Counts& out) = 0;
        virtual bool natPut(Obj widget, const std::string& sid, long long d, long long& done) = 0;
        virtual bool natRefresh(Obj widget) = 0;
        virtual bool natInst(Obj o, int where, bool state, std::vector<s2x_is::Inst>& out) = 0;
        virtual bool natPutI(Obj o, int where, const s2x_is::IState& st, long long n, uint32_t& uid) = 0;
        virtual bool natRmI(Obj o, int where, uint32_t uid, long long n, long long& done) = 0;
        virtual bool natApplyI(uint32_t uid, const s2x_is::IState& st) = 0;
        virtual bool natSpawnI(const s2x_is::IState& st, double x, double y, double z) = 0;
        virtual bool invReadable(Obj o) = 0;
        virtual bool count(Obj pawn, const std::string& sid, long long& out) = 0;
        virtual std::optional<long long> guid(Obj pawn) = 0;
        virtual bool money(Obj pawn, long long& out) = 0;
        virtual bool guidHex(Obj o, std::string& out) = 0;
        virtual std::optional<std::string> meshName(Obj o, const char* comp) = 0;
        virtual bool meshVisible(Obj o, bool& out) = 0;
        virtual bool setMeshVisible(Obj o, bool on) = 0;
        virtual int setInteraction(Obj o, bool on) = 0;
        virtual std::vector<Obj> comps(Obj o, const char* clsPath) = 0;
        virtual bool compActive(Obj c, bool& out) = 0;
        virtual bool compQuiet(Obj c, bool stopAudio) = 0;
        virtual bool compWake(Obj c, bool activate) = 0;

        virtual bool startNode(const std::string& sid, std::string& err) = 0;
        virtual bool createItem(const std::string& sid, long long uid, long long n) = 0;
        virtual bool spawnObj(const std::string& sid, double x, double y, double z, std::string& err) = 0;
        virtual bool spawnItem(const std::string& sid, double x, double y, double z) = 0;
        virtual bool uiDrop(uint32_t uid, long long n) = 0;
        virtual bool killUid(long long uid) = 0;
        virtual bool setUnkillable(long long uid, bool on) = 0;
        virtual bool destroyModel(long long uid) = 0;

        virtual bool isOwnBody(Obj o) = 0;
        virtual Obj targetPawn() = 0;
        virtual Obj puppetPawn() = 0;
        virtual bool bodySpawning() = 0;
        virtual std::string worldKey() = 0;
        virtual bool worldOpen() = 0;
        virtual bool storyNpc(Obj o) = 0;
        virtual std::optional<long long> npcUid(uint64_t addr, const std::string& name) = 0;
        virtual bool natOn() = 0;
        virtual bool natDead(const std::string& hid) = 0;
        virtual bool natCorpse(const std::string& hid, V3& out) = 0;
        virtual std::string lkHold(const std::string& slot, const std::string& key, double age, double lease, double hmax) = 0;
        virtual int lkOff(uint64_t addr, int on, double lease) = 0;
        virtual std::optional<double> clSwap(const std::string& hid, uint64_t loot, uint64_t src) = 0;
        virtual void clSwapTick() = 0;
        virtual bool removeItem(Obj o, const std::string& sid, long long n, long long& removed) = 0;
        virtual uint64_t natBody(const std::string& hid, int& st) = 0;
        virtual void natClOwn(uint64_t a, bool on) = 0;
        virtual void gcFeed(int mode, const std::vector<uint64_t>& keep) = 0;
        virtual void natGcKeep(std::vector<uint64_t>& keep) = 0;
        virtual std::string gcStat() = 0;
        virtual bool qiCorpse(Obj corpse, const std::string& id) = 0;
        virtual bool qiHostKeep(const std::string& id, Obj pawn) = 0;

        virtual std::string peerId() = 0;
        virtual std::string campaign() = 0;
        virtual bool cuOn() = 0;
        virtual s2m_save::QState* cuQ() = 0;
        virtual std::map<std::string, int>* cuApplied() = 0;
        virtual int cuApply(const std::string& ev) = 0;
        virtual bool cuRbReady() = 0;
        virtual void cuRbDone(const std::string& why) = 0;

        virtual bool mc() = 0;
        virtual int views() = 0;
        virtual int viewPid(int v) = 0;
        virtual int myPid() = 0;
        virtual bool partV(const char* tag, std::string& out, int v) = 0;
        virtual bool remoteV(int v, double& x, double& y, double& recv) = 0;
        virtual int viewSessions(int v) = 0;
    };

    void bind(Env* env);
    Env* env();
    void reset();
    void* swapState(void* st);

    void tick100();
    void catchupIn();
    void onPeer();
    void campaignRotated();
    void roleChanged();

    std::string outK();
    std::string outLoot();
    std::string outGround();
    std::string outU();
    std::string outStash();
    std::string outArt();
    std::string outCl();
    std::string outKH();
    std::string outGH();
    std::string outGC();
    std::string outGR();
    std::string outIDR();
    std::string outIDS();
    std::string outIDU();
    std::string outUR();
    std::string outYR();
    std::string outCLH();

    const std::vector<std::string>& lootSids();
    bool isQuestItem(const std::string& sid);
    bool qshareOn();
    Counts invSnap(Obj pawn, long long* money = nullptr);
    bool itemRemove(const std::string& kind, const std::string& a, const std::string& b, const std::string& c);
    bool itemSend(const std::string& kind, const std::string& a, const std::string& b, const std::string& c);
    std::optional<std::string> contSidId(const std::string& id);
    std::optional<std::string> contSidPos(double x, double y, double z);
    bool artPlaceTaken(int place, const std::string& who);
    std::string stashNote(const std::string& ops);
    bool runBusy();
    std::optional<std::string> lockMine();
    bool stashOpen();
    bool lootPending();
    bool isLootBody(Obj o);
    bool lootBodySeen(Obj o);
    void lootReshow(Obj o);
    std::string status();
    std::string idropNow(const std::string& sid, long long n);
#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
    void census(std::vector<cen::Row>& out);
#endif

    namespace pure
    {
        struct XItem
        {
            std::string sid;
            long long n = 0;
        };
        std::vector<XItem> xItems(const std::string& s);
        struct SignItem
        {
            char sign = '+';
            std::string sid;
            long long n = 0;
        };
        std::vector<SignItem> signItems(const std::string& s);
        std::vector<XItem> starItems(const std::string& s);
        std::string fmt0(double v);
        std::string numStr(double v);
        std::string lockId(const std::string& cls, double x, double y, double z);
        std::string invDelta(const Counts& a, const Counts& b);
        std::string lsEnc(const Counts& t);
        Counts lsDec(const std::string& s);
        std::string clEnc(const Counts& t);
        Counts clDec(const std::string& s);
        Counts clAdd(const Counts& a, const Counts& b);
        void ssOps(Counts& led, const std::string& ops);
        std::string ssText(const Counts& led);
        std::vector<std::string> ylChunks(std::string txt);
        std::vector<std::string> nodeBits(const std::string& prefix, const std::string& sid, long long n);
        bool keyParts(const std::string& k, std::string& p, long long& x, long long& y, long long& z);
        bool keyClose(const std::string& a, const std::string& b, bool keyfix, long long tol);
        std::optional<std::string> keyGet(const std::set<std::string>& map, const std::string& k, bool keyfix, long long tol);
        bool tokOk(const std::string& v);
        bool idCoords(const std::string& id, double& x, double& y, double& z);
        std::string stripWs(const std::string& s);
        std::string stripGuid(const std::string& op);
        struct EqRow
        {
            std::string kind;
            std::optional<std::string> ammo;
            std::optional<double> max;
        };
        Counts clHold(const Counts& want, const std::map<std::string, EqRow>& eq, bool clammo1);
        std::pair<Counts, Counts> clRest(Counts& gv, const Counts& want, const Counts& t, const Counts& q, const std::set<std::string>* Q);
        long long qgGive(long long n, long long tot, long long have);
        std::string qgKey(const char* kind, const std::string& src, const std::string& sid, long long n, long long tot, const std::string& who);
        bool qgParse(const std::string& k, std::string& kind, std::string& src, std::string& sid, long long& n, long long& tot, std::string& who);
    }
}
