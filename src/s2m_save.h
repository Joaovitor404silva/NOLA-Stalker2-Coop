#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace s2m_save
{
    using Id = uint64_t;
    using Counts = std::map<std::string, int>;

    struct V3
    {
        double x = 0, y = 0, z = 0;
    };

    struct QuestResult
    {
        bool ok = true;
        int r = -1;
        std::string why;
        bool hasWhy = false;
    };

    struct Pair
    {
        enum Kind : uint8_t { none = 0, force, yes, table } kind = none;
        uint64_t serial = 0;
        int hs = 0;
        int kh = 0;
        std::string cid;
        std::string wp;
        double at = 0;
    };

    struct CsWait
    {
        double at = 0;
        std::optional<std::string> sig;
        Pair tag;
        std::optional<double> okAt;
    };

    struct RbWait
    {
        double at = 0;
        Id addr = 0;
        int n = -1;
        std::optional<double> readyAt;
    };

    struct BootRec
    {
        std::optional<std::string> cid;
        int n = 0;
        Id addr = 0;
        std::optional<std::string> wc;
        bool done = false;
    };

    struct QState
    {
        Counts ap;
        bool apInit = false;
        std::optional<Id> apAddr;
        std::optional<std::string> apCid;
        std::optional<std::string> apW;
        bool apFresh = false;
        bool csDirty = false;
        std::optional<Counts> csSnap;
        std::optional<CsWait> csWait;
        std::optional<std::vector<std::string>> csQueue;
        std::optional<Counts> csAhead;
        std::optional<int> csRefused;
        bool csBlind = false;
        int csLoads = 0;
        bool cuDone = false;
        std::optional<std::string> cuStale;
        double cuStaleAt = 0;
        std::optional<BootRec> bootRec;
        std::optional<Id> csAddr;
        double csNext = 0;
        bool csNextSet = false;
        std::string csPeerWas;
        bool csLeave = false;
        std::optional<double> csAt;
        std::optional<int> csBase;
        std::optional<double> n7SavedAt;
        Pair n7Pair;
        double gapChk = 0;
        int gapN = 0;
        double gapAt = 0;
        std::optional<RbWait> rbWait;
        std::optional<std::vector<std::string>> rbQueue;
        int applied = 0;
        int skipped = 0;
        std::map<long long, double> seen;
        double seenPruneAt = -10;
        int rx = 0;
        bool csRb = false;
        bool cs1Warn = false;
        bool qlOk = false;
        std::optional<double> qlT0;
        bool rbCheckPending = false;
        bool rbAddrKnown = false;
        bool lvOn = false;
        double lvAt = 0;
    };

    class Env
    {
      public:
        virtual ~Env() = default;
        virtual double now() = 0;
        virtual double flag(const char* key, double def) = 0;
        virtual std::string flagText(const char* key, const char* def) = 0;
        virtual bool isHost() = 0;
        virtual bool coop() { return true; }
        virtual void log(const std::string& line) = 0;
        virtual void screen(const char*, const std::string&, double) {}
        virtual void screenClear(const char*) {}
        virtual bool readText(const std::string& name, std::string& out) = 0;
        virtual bool writeText(const std::string& name, const std::string& text) = 0;
        virtual bool writeBin(const std::string& name, const std::string& bytes) = 0;
        virtual bool readAbs(const std::string& path, std::string& out, bool bin) = 0;
        virtual bool writeAbs(const std::string& path, const std::string& bytes) = 0;
        virtual bool statTail(const std::string& path, long long& size, std::string& tail16) = 0;
        virtual bool saveInfo(std::string& dir, std::string& stamp, std::string& plat, std::string& newest) = 0;
        virtual std::string saveList() = 0;
        virtual std::string saveFolder() { return "SaveGames"; }
        virtual std::string dateText() = 0;

        virtual std::string sxHost(int kh) = 0;
        virtual bool sxSnap(Id pawn, const std::string& sids, const std::string& wc, std::string& id, int& n, double& money, std::string& hand, bool& reused, std::string& err) = 0;
        virtual std::string sxGet(std::string& id, double& money, std::string& items, std::string& qs, std::string& hand, std::string& w) = 0;
        virtual bool sxWorn(std::string& out) { out.clear(); return false; }
        virtual bool worn(Id, std::string& out) { out.clear(); return false; }
        virtual bool unequip(Id, int, std::string& err) { err = "sem api"; return false; }
        virtual bool equip(Id, const std::string&, int, std::string& err) { err = "sem api"; return false; }
        virtual bool sxMark(const std::string& id, const std::string& state) = 0;
        virtual bool sxWant(const std::string& id) = 0;
        virtual std::string sxIn(const std::string& id, std::string& detail) = 0;
        virtual bool sxInstall(const std::string& id, std::string& guid, int& kh, std::string& cid, std::string& err) = 0;
        virtual bool sxUndo(const std::string& guid) = 0;
        virtual bool wpFreeze(int, int, std::string&, std::string& err) { err = "unsupported"; return false; }
        virtual bool wpTake(const std::string&, int&, int&, std::string&, long long&, std::string& err) { err = "unsupported"; return false; }
        virtual bool wpLast(std::string&, std::string&, int&, int&) { return false; }
        virtual bool wspStart(const std::string&, const std::string&, const std::string&, bool, bool, std::string& err) { err = "unsupported"; return false; }
        virtual std::string wspPoll(std::string& detail) { detail.clear(); return "none"; }

        virtual Id localPawn() = 0;
        virtual bool hp(Id pawn, double& out) = 0;
        virtual bool movementMode(Id pawn, int& out) = 0;
        virtual bool setMovementMode(Id pawn, int mode) = 0;
        virtual bool location(Id pawn, V3& out) = 0;
        virtual bool localPos(V3& out) = 0;
        virtual bool money(Id pawn, double& out) = 0;
        virtual bool modifyMoney(Id pawn, long long delta) = 0;
        virtual bool itemCount(Id pawn, const std::string& sid, int& out) = 0;
        virtual bool ammoLoaded(Id, const std::string&, int&) { return false; }
        virtual bool createItem(const std::string& sid, int n, std::string& err) = 0;
        virtual bool forcedAutoSave(std::string& err) = 0;
        virtual bool joinAutoSave(std::string& err) = 0;
        virtual bool loadLastSave(std::string& err) = 0;
        virtual int loadBegins(std::string& guid) = 0;

        virtual bool part(const char* tag, std::string& out) = 0;
        virtual int peerState() = 0;
        virtual bool peerLiveFalse() = 0;
        virtual int peerSessions() = 0;
        virtual bool remotePos(V3& out) = 0;

        virtual QuestResult questApply(const std::string& kind, const std::optional<std::string>& a, const std::optional<std::string>& b,
                                       const std::optional<std::string>& c, const std::optional<std::string>& sid, bool live) = 0;
        virtual void rbDone(const std::string& why) = 0;
        virtual int hostLogSize() = 0;
        virtual std::optional<long long> hostGen() = 0;
        virtual void qiReset() = 0;
        virtual bool questTableOn() = 0;
        virtual bool isQuestItem(const std::string& sid) = 0;
        virtual void itemRemove(const std::string& sid, int n) = 0;
        virtual bool itemsBusy() = 0;
        virtual int questsHeld() = 0;
        virtual bool cineFrozen() = 0;
        virtual bool cineJoinBusy() = 0;
        virtual bool downed() = 0;
        virtual bool emNoSave() = 0;
        virtual bool leaveStoreOk() { return true; }
        virtual void leashSet(const std::string& name, double m, double secs, const std::string& why, bool hard) = 0;
        virtual void leashDrop(const std::string& name, const std::string& why) = 0;
        virtual bool leashPull(const std::string& name, double m, double secs, const std::string& why) = 0;
    };

    enum PeerSt : int { peerNone = 0, peerUp = 1, peerSilent = 2 };

    void bind(Env* env);
    Env* env();
    QState& qs();
    void reset();
    void setLootSids(std::vector<std::string> sids);
    const std::vector<std::string>& lootSids();

    void joinTick();
    void kitTick();
    void questIn();
    void csTick();
    void n7Tick();
    std::string csOut();
    std::string n7B();
    std::string n7HL();
    Counts& applied();
    bool qlSettled();
    bool n7Rw(const std::string& quest);
    void n7Follow();
    bool n7Idle();
    bool joinBusy();
    int leaveAsk(std::string& why);
    bool leaveWait();
    bool joinPending();
    void n7DieFollow();
    void n7Go(const std::string& v);
    void tick100();
    void tick100_join();
    void tick100_mid();
    void tick100_kit();
    std::string status();
}
