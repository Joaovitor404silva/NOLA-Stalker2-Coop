#include "s2m_items.h"
#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
#include "s2m_census_logic.h"
#endif
#include "s2x_hot_logic.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <exception>
#include <stdexcept>
#include <functional>
#include <unordered_map>

namespace s2m_items
{
    namespace
    {
        Env* g_env = nullptr;

        std::string sf(const char* fmt, ...)
        {
            char b[4096];
            va_list ap;
            va_start(ap, fmt);
            int n = vsnprintf(b, sizeof b, fmt, ap);
            va_end(ap);
            if (n < 0) return std::string();
            if (n < (int)sizeof b) return std::string(b, (size_t)n);
            std::string r((size_t)n + 1, '\0');
            va_start(ap, fmt);
            vsnprintf(&r[0], r.size(), fmt, ap);
            va_end(ap);
            r.resize((size_t)n);
            return r;
        }

        bool isW(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
        bool isD(char c) { return c >= '0' && c <= '9'; }
        bool isX(char c) { return isD(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
        bool isSp(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }
        bool allOf(const std::string& s, bool (*f)(char))
        {
            if (s.empty()) return false;
            for (char c : s) if (!f(c)) return false;
            return true;
        }
        bool numChars(const std::string& s)
        {
            if (s.empty()) return false;
            for (char c : s) if (!(isD(c) || c == '-' || c == '.')) return false;
            return true;
        }
        bool intChars(const std::string& s)
        {
            if (s.empty()) return false;
            for (char c : s) if (!(isD(c) || c == '-')) return false;
            return true;
        }
        std::optional<double> tonum(const std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && isSp(s[a])) ++a;
            while (b > a && isSp(s[b - 1])) --b;
            if (a == b) return std::nullopt;
            std::string t = s.substr(a, b - a);
            const char* p = t.c_str();
            char* e = nullptr;
            bool neg = false;
            const char* q = p;
            if (*q == '-' || *q == '+') { neg = *q == '-'; ++q; }
            if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X'))
            {
                unsigned long long v = strtoull(q + 2, &e, 16);
                if (e == q + 2 || *e) return std::nullopt;
                return neg ? -(double)v : (double)v;
            }
            double v = strtod(p, &e);
            if (e == p || *e) return std::nullopt;
            return v;
        }
        std::vector<std::string> splitAll(const std::string& s, char sep)
        {
            std::vector<std::string> r;
            size_t i = 0;
            while (true)
            {
                size_t j = s.find(sep, i);
                if (j == std::string::npos) { r.push_back(s.substr(i)); break; }
                r.push_back(s.substr(i, j - i));
                i = j + 1;
            }
            return r;
        }
        std::vector<std::string> splitNE(const std::string& s, char sep)
        {
            std::vector<std::string> r;
            for (auto& p : splitAll(s, sep)) if (!p.empty()) r.push_back(p);
            return r;
        }
        std::vector<std::string> lines(const std::string& text)
        {
            std::vector<std::string> r;
            if (text.empty()) return r;
            size_t i = 0;
            while (i < text.size())
            {
                size_t j = text.find('\n', i);
                std::string l = text.substr(i, j == std::string::npos ? std::string::npos : j - i);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                r.push_back(l);
                if (j == std::string::npos) break;
                i = j + 1;
            }
            return r;
        }
        bool startsWith(const std::string& s, const char* p)
        {
            size_t n = strlen(p);
            return s.size() >= n && s.compare(0, n, p) == 0;
        }
        bool has(const std::string& s, const char* p) { return s.find(p) != std::string::npos; }
        std::string firstTok(const std::string& s)
        {
            size_t i = 0;
            while (i < s.size() && !isSp(s[i])) ++i;
            return s.substr(0, i);
        }
        std::string bstr(bool b) { return b ? "true" : "false"; }
        std::string luaF(double v)
        {
            if (std::floor(v) == v && std::fabs(v) < 1e15) return sf("%.1f", v);
            return sf("%.14g", v);
        }
        std::string lld(long long v) { return sf("%lld", v); }
        std::string uidStr(const std::optional<long long>& u) { return u ? lld(*u) : std::string("nil"); }
        double sq(double v) { return v * v; }
        template <class M> int cnt(const M& m) { return (int)m.size(); }

        Env& E() { return *g_env; }
        double F(const char* k, double d) { return E().fnum(k, d); }
        long long FI(const char* k, double d) { return (long long)std::floor(E().fnum(k, d)); }
        bool FON(const char* k) { return E().fnum(k, 0) == 1; }
        bool HOST() { return E().host(); }
        const char* ROLE() { return HOST() ? "host" : "client"; }
        void LOG(const std::string& s) { E().log(s); }
        double NOW() { return E().now(); }
        bool VAL(Obj o) { return o != 0 && E().valid(o); }
        bool PART(const char* tag, std::string& out)
        {
            out.clear();
            return E().part(tag, out);
        }
        bool b164on() { return F("b164off", 0) == 0; }
        bool MC() { return E().fnum("mcitems", 1) == 1 && E().mc(); }
        bool PARTV(const char* tag, std::string& out, int v)
        {
            out.clear();
            return E().partV(tag, out, v);
        }
        std::string vTok(int v) { return sf("%d.%d", E().viewPid(v), E().viewSessions(v)); }
        int tokPid(const std::string& t)
        {
            size_t i = 0;
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
            if (i == 0 || (i < t.size() && t[i] != '.')) return -1;
            return std::atoi(t.substr(0, i).c_str());
        }

        struct Taken
        {
            bool loaded = false;
            std::set<std::string> set;
            std::vector<std::string> list;
            bool mineLoaded = false;
            std::set<std::string> mine;
            std::optional<std::string> cid;
        };
        struct LootReply
        {
            long long seq = 0;
            std::string id, str;
            std::optional<double> at;
        };
        struct LootReq
        {
            long long seq = 0;
            std::string id;
            double untilT = 0;
        };
        struct Loot
        {
            bool init = false;
            long long seq = 0;
            std::map<double, double> done, applied;
            std::set<std::string> given, qkeep;
            std::string corpses;
            std::optional<LootReq> req;
            bool keyDown = false;
            double lastScan = 0;
            std::deque<LootReply> replyQ;
            long long xi = 0;
            double xAt = 0;
            double reqSeen = 0;
            std::optional<double> latchUsed;
            bool hotIn = false;
            std::string hotKept;
        };
        struct TOut
        {
            std::string id;
            double untilT = 0;
        };
        struct GCand
        {
            Obj o = 0;
            std::string cls, local;
            double x = 0, y = 0, z = 0, t = 0;
            std::optional<std::string> mesh;
        };
        struct GOut
        {
            std::string e;
            double untilT = 0;
        };
        struct GPend
        {
            double x = 0, y = 0, z = 0;
            std::string cls, mesh, e;
            double untilT = 0, nextT = 0;
        };
        struct Ground
        {
            struct Copy
            {
                std::string local, cls, mesh;
                V3 pos;
            };
            long long seq = 0;
            std::map<std::string, GCand> cand;
            std::vector<GOut> out;
            std::map<std::string, double> seen;
            std::vector<GPend> pend;
            std::map<std::string, Copy> used;
            std::map<std::string, Copy> gone;
            std::string wk;
            double sweepAt = 0;
        };
        struct ContRow
        {
            std::string sid;
            double x = 0, y = 0, z = 0;
        };
        struct ItOutE
        {
            double t = 0;
            std::string s;
        };
        struct It
        {
            long long seq = 0;
            std::vector<ItOutE> list;
            std::map<double, double> seen;
            std::optional<double> pruneAt;
            std::deque<std::string> run;
            long long sent = 0, applied = 0;
            bool contLoaded = false;
            std::vector<ContRow> cont;
        };
        struct Pt
        {
            std::set<std::string> applied, base;
            double checkAt = 0;
            bool init = false;
        };
        struct LsRow
        {
            double x = 0, y = 0, z = 0;
        };
        struct CkRow
        {
            double x = 0, y = 0, z = 0;
            std::set<std::string> set;
        };
        struct LsRead
        {
            std::string sid, id;
            double ta = 0;
            bool body = false;
        };
        struct Ls
        {
            std::map<std::string, std::string> canon, mine, cb;
            std::set<std::string> done, rc, emp;
            size_t ix = 0;
            std::map<std::string, bool> nog;
            double at = 0;
            int err = 0;
            std::optional<LsRead> crd;
            bool rowsLoaded = false;
            std::map<std::string, LsRow> rows;
            bool ckLoaded = false;
            double ckAt = 0;
            std::map<std::string, CkRow> ck;
        };
        struct Lock
        {
            Obj widget = 0;
            std::optional<std::string> mine, denied, psid;
            std::optional<Counts> psnap;
            bool psnapInf = false;
            long long denies = 0, grants = 0;
            std::optional<Counts> cfirst, clast, plast;
            Counts ceq, cpre;
            size_t cix = 0;
            std::optional<std::vector<s2x_is::Inst>> pinst;
            std::vector<s2x_is::Inst> winst;
        };
        struct SsHist
        {
            double t = 0;
            std::string op;
        };
        struct SsYl
        {
            std::vector<std::string> chunks, hashes;
            size_t rr = 0;
        };
        struct SsOpen
        {
            std::string id;
            Counts snap;
            std::optional<std::string> guid;
        };
        struct Ss
        {
            bool init = false;
            Counts led;
            long long seq = 0;
            std::vector<ItOutE> out;
            std::map<double, double> seen;
            std::optional<double> pruneAt;
            std::vector<SsHist> hist;
            std::optional<SsYl> yl;
            std::optional<uint64_t> addr;
            double addrAt = 0;
            long long addrN = 0;
            std::optional<SsOpen> open;
            double rbAt = 0;
            std::optional<uint64_t> rbAddr, fillAddr;
            std::optional<std::string> ylSeen;
            bool ylOk = false;
            bool boxLoaded = false;
            Counts box;
        };
        struct ApPlace
        {
            std::string sid;
            double x = 0, y = 0, z = 0;
        };
        struct ApTrack
        {
            std::optional<int> place;
            bool done = false;
            std::optional<double> pd;
        };
        struct Ap
        {
            bool init = false;
            std::vector<ApPlace> places;
            std::map<int, double> taken;
            std::map<std::string, ApTrack> track;
            std::map<std::string, Obj> hid;
            std::map<std::string, std::map<std::string, std::vector<bool>>> was;
            double at = 0, quiet = 0;
            std::optional<V3> pp;
            std::optional<uint64_t> paddr;
        };

        struct ClE
        {
            uint64_t ad = 0;
            long long uid = 0;
            long long ver = 1;
            Counts canon, q;
            std::string str;
            double rc = 0;
            double x = 0, y = 0, z = 0, d2 = 0;
            bool hasD2 = false;
            bool sw = false, off = false;
        };
        struct ClZ
        {
            uint64_t ad = 0;
            std::string nm;
            std::optional<long long> uid;
            double at = 0;
        };
        struct ClR
        {
            double x = 0, y = 0, z = 0;
            long long uid = 0, ver = 0;
            bool o = false;
            std::string str;
            std::optional<double> gone;
            int twTry = 0, cpTry = 0;
            double first = 0;
        };
        struct ClB
        {
            uint64_t ad = 0;
            std::string nm, st, wstr, hstr;
            long long ver = 0;
            std::optional<long long> uid;
            bool mh = false, open = false, sw = false;
            bool hasGv = false;
            Counts gv, held;
            bool nat = false;
            double at = 0;
        };
        struct ClM
        {
            uint64_t ad = 0;
            std::string nm;
            double at = 0;
        };
        struct ClTk
        {
            std::string id, hid, str;
            long long ver = 0;
            double at = 0;
        };
        struct ClPend
        {
            std::string hid, st, str;
            double at = 0, x = 0, y = 0, z = 0, nx = 0;
            long long ver = 0;
            std::set<std::string> before;
            int kills = 0;
            Counts want;
            std::set<std::string> am;
        };
        struct ClLate
        {
            std::string hid;
            std::set<std::string> before;
            double x = 0, y = 0, to = 0;
        };
        struct ClCnt
        {
            std::string hid, kind;
            double at = 0;
        };
        struct ClW
        {
            double x = 0, y = 0, z = 0;
            std::string str;
            long long ver = 0;
            bool block = false;
        };
        struct ClStat
        {
            double at = 0;
            long long spawn = 0, live = 0, drop = 0, fail = 0, take = 0, tin = 0, trej = 0, rc = 0, deny = 0, copy = 0, inert = 0, hold = 0;
            long long nat = 0, rmv = 0, gone = 0, hrm = 0;
            double ms = 0;
        };
        struct Cl
        {
            std::string wk;
            std::map<std::string, ClE> K;
            std::map<std::string, ClR> R;
            std::map<std::string, ClB> B;
            std::map<std::string, ClZ> Z;
            std::map<std::string, ClM> M;
            std::map<std::string, uint64_t> L;
            std::map<std::string, double> X, xdone;
            std::string gcLast;
            std::set<std::string> had;
            std::set<std::string> left;
            std::map<uint64_t, std::string> own;
            std::vector<ClTk> T;
            std::map<std::string, double> done, fail;
            std::map<std::string, int> tries;
            std::map<std::string, std::string> cp;
            std::map<std::string, bool> lsb;
            long long seq = 0;
            double lt = 0, ht = 0, gap = 0;
            std::string nonce;
            ClStat s;
            std::optional<ClPend> pend;
            std::optional<std::string> co, raw, open, openK;
            bool silent = false;
            std::optional<ClCnt> cnt;
            std::optional<std::vector<ClLate>> late;
            std::optional<std::map<std::string, uint64_t>> tw;
            int swN = 0;
            std::optional<double> swAt;
            std::set<std::string> natNo;
            double denyLog = -1e9;
            int err = 0;
            bool mapOk = false;
            double mapAt = -1;
            std::unordered_map<uint64_t, Obj> map;
        };

        struct McRel
        {
            double t = 0;
            std::string s;
        };
        struct McHold
        {
            int pid = 0;
            std::string key;
            double since = 0;
        };
        struct McGOwn
        {
            int pid = 0;
            double x = 0, y = 0, z = 0, t = 0;
            std::string cls, mesh, local;
        };
        struct McOff
        {
            Obj o = 0;
            std::string key;
            int n = 0;
            int pid = -1;
        };
        struct Mc
        {
            std::vector<McHold> kh, gh;
            std::vector<McRel> ur, yr, gr;
            std::map<std::string, double> useen, yseen;
            std::vector<McGOwn> gown;
            std::vector<McHold> co;
            std::map<std::string, int> clh;
            std::vector<McOff> goff;
            std::vector<McOff> koff;
            std::optional<std::string> kclaim;
            std::map<std::string, int> kcls;
            std::set<std::string> kexp;
            std::string kbRaw;
            double kbAt = -1e9;
            std::string ghRaw;
            double ghAt = -1e9;
            std::optional<std::string> gc;
            std::optional<double> uPrune, yPrune;
            long long gdup = 0, kGive = 0, kKeep = 0, gGive = 0, uIn = 0, yIn = 0, grIn = 0, coGive = 0;
            std::map<std::string, std::vector<std::string>> hq;
            std::map<std::string, std::map<std::string, double>> hqSeen;
            bool hqHeld = false;
            long long hqParked = 0;
        };

        struct DEnt
        {
            std::string id, sid, mesh, st;
            int owner = -1;
            long long seq = 0, n = 1;
            double x = 0, y = 0, z = 0, t = 0;
        };
        struct DTake
        {
            std::string id;
            int pid = 0;
            double t = 0;
        };
        struct DKept
        {
            DEnt e;
            double gs = 0;
            bool restored = false;
        };
        struct DPre
        {
            Obj o = 0;
            std::string sid, mesh, st;
            long long n = 1;
            V3 p;
            double since = 0, born = 0;
        };
        struct DLoc
        {
            std::string id;
            long long seq = 0;
            Obj o = 0;
            bool mine = false, wait = false, gone = false, iset = false;
            std::string sid, mesh, st;
            long long n = 1;
            V3 p, last;
            double born = 0, nextT = 0, spawnT = 0;
            int tries = 0;
            std::set<std::string> before;
        };
        struct DClaim
        {
            std::string id, sid, st;
            long long n = 1, base = 0, cseq = 0, peak = 0;
            double t = 0;
            int verdict = 0;
            bool done = false;
            std::set<uint32_t> before;
            bool hasBefore = false;
        };
        struct Drop
        {
            bool loaded = false, armed = false, wasOpen = false, ever = false, snap = false;
            std::map<std::string, std::vector<std::string>> m2s;
            std::map<std::string, std::string> s2m;
            std::string wk;
            double lastOpen = -1e9, scanAt = 0, baseAt = -1e9;
            int wState = -1;
            Counts base, tb;
            std::map<uint32_t, s2x_is::Inst> binst;
            std::map<std::string, std::set<uint32_t>> tbi;

            std::map<std::string, bool> known;
            std::map<std::string, int> rej;
            std::vector<DPre> pre;
            std::vector<DLoc> loc;
            std::vector<DEnt> ents;
            std::vector<DTake> takes;
            std::vector<DKept> kept;
            double rbAt = 0;
            double rbGs = 0;
            bool rbGsOk = false;
            std::vector<DClaim> cl;
            std::map<std::string, double> rseen, useen, fail;
            std::map<Obj, double> done;
            long long seq = 0, cseq = 0, nReg = 0, nSpawn = 0, nTake = 0, nLose = 0, nFix = 0;
            int err = 0;
        };

        struct QgWin
        {
            std::string src;
            Counts c;
            double at = 0;
            bool hasC = false;
        };
        struct Qg
        {
            bool loaded = false, based = false, nat = false, fresh = false;
            std::set<std::string> mine, seenC;
            Counts base, put;
            std::optional<uint64_t> pAddr;
            double at = 0, waitT = 0;
            size_t ix = 0;
            std::optional<QgWin> win;
            std::optional<std::pair<std::string, double>> g;
            std::map<std::string, std::vector<std::string>> led;
            std::map<std::string, double> hold;
            std::vector<std::string> todo;
            long long claims = 0, grants = 0, dups = 0, rms = 0, nosrc = 0;
            int err = 0;
        };

        struct State
        {
            Mc mc;
            Taken tk;
            Loot loot;
            std::vector<TOut> tList;
            std::optional<TOut> tOut;
            Ground ground;
            bool groundOn = false;
            Drop drop;
            It it;
            Pt pt;
            Ls ls;
            Lock lock;
            bool lockOn = false;
            Ss ss;
            Ap ap;
            std::optional<Cl> cl;
            int clOutErr = 0;
            int lootErr = 0, gErr = 0, c22err = 0, b6err = 0;
            int peerSessions = -1;
            bool peerWasSilent = false;
            bool sidsLoaded = false;
            std::vector<std::string> sids;
            bool qtLoaded = false;
            std::map<std::string, long long> qS;
            std::vector<std::string> qList;
            std::set<std::string> sidS;
            std::set<std::string> qSh;
            Qg qg;
            bool eqLoaded = false;
            std::map<std::string, pure::EqRow> eq;
            long long invGuard = 0;
            int lsErrLogs = 0;
        };
        State* g_st = nullptr;
        State& S()
        {
            if (!g_st) g_st = new State();
            return *g_st;
        }

        const std::vector<std::string>& sids()
        {
            State& st = S();
            if (!st.sidsLoaded)
            {
                st.sidsLoaded = true;
                std::string text;
                if (E().dataText("loot_sids.tsv", text))
                {
                    std::vector<std::pair<long long, std::string>> rows;
                    for (auto& l : lines(text))
                    {
                        auto f = splitAll(l, '\t');
                        if (f.size() < 3 || f[0].size() < 2 || f[0][0] != 'i') continue;
                        auto n = tonum(f[0].substr(1));
                        if (!n) continue;
                        rows.push_back({(long long)*n, f.back()});
                    }
                    std::sort(rows.begin(), rows.end());
                    for (auto& r : rows) st.sids.push_back(r.second);
                }
                if (st.sids.empty()) LOG("NOLA items: loot_sids.tsv ausente ou vazio (LOOT_SIDS vazio)");
            }
            return st.sids;
        }

        void qtLoad()
        {
            State& st = S();
            if (st.qtLoaded) return;
            st.qtLoaded = true;
            std::string text;
            if (!E().dataText("s2u_qitems.tsv", text)) return;
            for (auto& l : lines(text))
            {
                auto f = splitAll(l, '\t');
                if (f.size() < 5 || f[0] != "sS" || f[1].size() < 2 || f[1][0] != 's') continue;
                auto v = tonum(f[4]);
                st.qS[f[1].substr(1)] = v ? (long long)*v : 1;
            }
            for (auto& kv : st.qS) st.qList.push_back(kv.first);
            std::string sh;
            if (E().dataText("s2u_qshare.tsv", sh))
                for (auto& l : lines(sh))
                {
                    auto f = splitAll(l, '\t');
                    std::string s = f.empty() ? std::string() : pure::stripWs(f[0]);
                    if (s.empty() || s == "sid" || st.qS.count(s) || !st.qSh.insert(s).second) continue;
                    st.qList.push_back(s);
                }
        }
        bool qs(const std::string& sid)
        {
            qtLoad();
            return S().qS.count(sid) != 0;
        }
        bool qtOk()
        {
            qtLoad();
            return !S().qS.empty();
        }
        bool qsh(const std::string& sid)
        {
            qtLoad();
            return S().qS.count(sid) != 0 || S().qSh.count(sid) != 0;
        }

        Counts countAll(Obj o)
        {
            Counts t;
            if (!VAL(o)) return t;
            if (!E().invReadable(o)) { ++S().invGuard; return t; }
            for (auto& s : sids())
            {
                long long n = 0;
                if (E().count(o, s, n) && n > 0) t[s] = n;
            }
            return t;
        }
    }

    namespace pure
    {
        std::vector<XItem> xItems(const std::string& s)
        {
            std::vector<XItem> r;
            size_t i = 0, N = s.size();
            while (i < N)
            {
                if (!isW(s[i])) { ++i; continue; }
                size_t j = i;
                while (j < N && isW(s[j])) ++j;
                size_t p = i;
                while (p < j)
                {
                    size_t k = std::string::npos;
                    for (size_t q = j; q-- > p + 1;)
                        if (s[q] == 'x' && q + 1 < j && isD(s[q + 1])) { k = q; break; }
                    if (k == std::string::npos) break;
                    size_t d = k + 1;
                    while (d < j && isD(s[d])) ++d;
                    XItem it;
                    it.sid = s.substr(p, k - p);
                    it.n = std::strtoll(s.substr(k + 1, d - k - 1).c_str(), nullptr, 10);
                    r.push_back(it);
                    p = d;
                }
                i = j;
            }
            return r;
        }
        std::vector<SignItem> signItems(const std::string& s)
        {
            std::vector<SignItem> r;
            size_t i = 0, N = s.size();
            while (i < N)
            {
                if ((s[i] == '+' || s[i] == '-') && i + 1 < N && isW(s[i + 1]))
                {
                    size_t a = i + 1, j = a;
                    while (j < N && isW(s[j])) ++j;
                    size_t k = std::string::npos;
                    for (size_t q = j; q-- > a + 1;)
                        if (s[q] == 'x' && q + 1 < j && isD(s[q + 1])) { k = q; break; }
                    if (k != std::string::npos)
                    {
                        size_t d = k + 1;
                        while (d < j && isD(s[d])) ++d;
                        SignItem it;
                        it.sign = s[i];
                        it.sid = s.substr(a, k - a);
                        it.n = std::strtoll(s.substr(k + 1, d - k - 1).c_str(), nullptr, 10);
                        r.push_back(it);
                        i = d;
                        continue;
                    }
                }
                ++i;
            }
            return r;
        }
        std::vector<XItem> starItems(const std::string& s)
        {
            std::vector<XItem> r;
            size_t i = 0, N = s.size();
            while (i < N)
            {
                if (!isW(s[i])) { ++i; continue; }
                size_t j = i;
                while (j < N && isW(s[j])) ++j;
                if (j < N && s[j] == '*' && j + 1 < N && isD(s[j + 1]))
                {
                    size_t d = j + 1;
                    while (d < N && isD(s[d])) ++d;
                    XItem it;
                    it.sid = s.substr(i, j - i);
                    it.n = std::strtoll(s.substr(j + 1, d - j - 1).c_str(), nullptr, 10);
                    r.push_back(it);
                    i = d;
                    continue;
                }
                i = j;
            }
            return r;
        }
        std::string fmt0(double v) { return sf("%.0f", v); }
        std::string numStr(double v)
        {
            if (std::floor(v) == v && std::fabs(v) < 9.2e18) return sf("%lld", (long long)v);
            return sf("%.14g", v);
        }
        std::string lockId(const std::string& c, double x, double y, double z) { return sf("%s@%.0f,%.0f,%.0f", c.c_str(), x / 10, y / 10, z / 10); }
        std::string invDelta(const Counts& a, const Counts& b)
        {
            std::vector<std::string> r;
            for (auto& kv : b)
            {
                auto it = a.find(kv.first);
                long long d = kv.second - (it == a.end() ? 0 : it->second);
                if (d > 0) r.push_back(kv.first + "x" + lld(d));
            }
            std::sort(r.begin(), r.end());
            std::string s;
            for (size_t i = 0; i < r.size(); ++i) s += (i ? "," : "") + r[i];
            return s;
        }
        std::string lsEnc(const Counts& t)
        {
            std::vector<std::string> r;
            for (auto& kv : t) if (kv.second > 0) r.push_back(kv.first + "x" + lld(kv.second));
            std::sort(r.begin(), r.end());
            if (r.empty()) return "-";
            std::string s;
            for (size_t i = 0; i < r.size(); ++i) s += (i ? "," : "") + r[i];
            return s;
        }
        Counts lsDec(const std::string& s)
        {
            Counts t;
            for (auto& it : xItems(s)) t[it.sid] += it.n;
            return t;
        }
        std::string clEnc(const Counts& t)
        {
            std::vector<std::string> r;
            for (auto& kv : t) if (kv.second > 0) r.push_back(kv.first + "*" + lld(kv.second));
            std::sort(r.begin(), r.end());
            if (r.empty()) return "-";
            std::string s;
            for (size_t i = 0; i < r.size(); ++i) s += (i ? "." : "") + r[i];
            return s;
        }
        Counts clDec(const std::string& s)
        {
            Counts t;
            for (auto& it : starItems(s)) t[it.sid] += it.n;
            return t;
        }
        Counts clAdd(const Counts& a, const Counts& b)
        {
            Counts t = a;
            for (auto& kv : b) t[kv.first] += kv.second;
            return t;
        }
        void ssOps(Counts& led, const std::string& ops)
        {
            for (auto& it : signItems(ops))
            {
                auto f = led.find(it.sid);
                long long v = (f == led.end() ? 0 : f->second) + (it.sign == '+' ? it.n : -it.n);
                if (v > 0) led[it.sid] = v; else led.erase(it.sid);
            }
        }
        std::string ssText(const Counts& led)
        {
            std::vector<std::string> t;
            for (auto& kv : led) t.push_back(kv.first + "x" + lld(kv.second));
            std::sort(t.begin(), t.end());
            std::string s;
            for (size_t i = 0; i < t.size(); ++i) s += (i ? "," : "") + t[i];
            return s;
        }
        std::vector<std::string> ylChunks(std::string txt)
        {
            std::vector<std::string> chunks;
            while (txt.size() > 1000)
            {
                std::string head = txt.substr(0, 1000);
                size_t c = head.rfind(',');
                size_t cut = c == std::string::npos ? 1000 : c + 1;
                chunks.push_back(txt.substr(0, cut - 1));
                txt = cut < txt.size() ? txt.substr(cut) : std::string();
            }
            chunks.push_back(txt);
            return chunks;
        }
        std::vector<std::string> nodeBits(const std::string& prefix, const std::string& sid, long long n)
        {
            std::vector<std::string> r;
            while (n > 0)
            {
                long long part = std::min<long long>(n, 1023);
                n -= part;
                for (int k = 9; k >= 0; --k)
                    if (part >= (1LL << k)) { part -= (1LL << k); r.push_back(prefix + sid + "_" + lld(1LL << k)); }
            }
            return r;
        }
        bool keyParts(const std::string& k, std::string& p, long long& x, long long& y, long long& z)
        {
            size_t c2 = k.rfind(',');
            if (c2 == std::string::npos) return false;
            size_t c1 = k.rfind(',', c2 == 0 ? 0 : c2 - 1);
            if (c1 == std::string::npos || c1 == c2) return false;
            std::string zs = k.substr(c2 + 1), ys = k.substr(c1 + 1, c2 - c1 - 1);
            size_t xe = c1, xs = c1;
            while (xs > 0 && isD(k[xs - 1])) --xs;
            if (xs == xe) return false;
            if (xs > 0 && k[xs - 1] == '-') --xs;
            auto okInt = [](const std::string& s) {
                size_t i = 0;
                if (i < s.size() && s[i] == '-') ++i;
                if (i >= s.size()) return false;
                for (; i < s.size(); ++i) if (!isD(s[i])) return false;
                return true;
            };
            if (!okInt(zs) || !okInt(ys)) return false;
            p = k.substr(0, xs);
            x = std::strtoll(k.substr(xs, xe - xs).c_str(), nullptr, 10);
            y = std::strtoll(ys.c_str(), nullptr, 10);
            z = std::strtoll(zs.c_str(), nullptr, 10);
            return true;
        }
        bool keyClose(const std::string& a, const std::string& b, bool keyfix, long long t)
        {
            if (a == b) return true;
            if (!keyfix) return false;
            std::string pa, pb;
            long long ax, ay, az, bx, by, bz;
            if (!keyParts(a, pa, ax, ay, az) || !keyParts(b, pb, bx, by, bz) || pa != pb) return false;
            return std::llabs(ax - bx) <= t && std::llabs(ay - by) <= t && std::llabs(az - bz) <= t;
        }
        std::optional<std::string> keyGet(const std::set<std::string>& map, const std::string& k, bool keyfix, long long t)
        {
            if (map.count(k)) return k;
            if (!keyfix) return std::nullopt;
            std::string p;
            long long x, y, z;
            if (!keyParts(k, p, x, y, z)) return std::nullopt;
            auto alts = [&](long long n) {
                std::vector<std::string> r;
                for (long long d = -t; d <= t; ++d)
                {
                    long long m = n + d;
                    r.push_back(lld(m));
                    if (m == 0) r.push_back("-0");
                }
                return r;
            };
            for (auto& a : alts(x))
                for (auto& b : alts(y))
                    for (auto& c : alts(z))
                    {
                        std::string k2 = p + a + "," + b + "," + c;
                        if (map.count(k2)) return k2;
                    }
            return std::nullopt;
        }
        bool tokOk(const std::string& v)
        {
            if (v.empty()) return false;
            for (char c : v) if (!(isW(c) || c == '-' || c == '.')) return false;
            return true;
        }
        bool idCoords(const std::string& id, double& x, double& y, double& z)
        {
            size_t at = id.rfind('@');
            if (at == std::string::npos) return false;
            auto f = splitAll(id.substr(at + 1), ',');
            if (f.size() != 3 || !numChars(f[0]) || !numChars(f[1]) || !numChars(f[2])) return false;
            auto a = tonum(f[0]), b = tonum(f[1]), c = tonum(f[2]);
            if (!a || !b || !c) return false;
            x = *a;
            y = *b;
            z = *c;
            return true;
        }
        std::string stripWs(const std::string& s)
        {
            std::string r;
            for (char c : s) if (!isSp(c)) r += c;
            return r;
        }
        std::string stripGuid(const std::string& op)
        {
            std::string r;
            size_t i = 0;
            while (i < op.size())
            {
                size_t a = i;
                if (op[i] == '.' && i + 1 < op.size() && op[i + 1] == '@') a = i + 1;
                if (op[a] == '@' && a + 1 < op.size() && isX(op[a + 1]))
                {
                    size_t j = a + 1;
                    while (j < op.size() && isX(op[j])) ++j;
                    i = j;
                    continue;
                }
                r += op[i];
                ++i;
            }
            return r;
        }
        Counts clHold(const Counts& want, const std::map<std::string, EqRow>& eq, bool clammo1)
        {
            Counts held;
            if (!clammo1) return held;
            std::vector<std::string> ws;
            for (auto& kv : want)
            {
                auto e = eq.find(kv.first);
                if (e != eq.end() && e->second.ammo && e->second.max.value_or(0) > 0 && kv.second > 0) ws.push_back(kv.first);
            }
            std::sort(ws.begin(), ws.end(), [&](const std::string& a, const std::string& b) {
                double ma = *eq.at(a).max, mb = *eq.at(b).max;
                if (ma != mb) return ma < mb;
                return a < b;
            });
            std::map<std::string, double> bud;
            for (auto& sid : ws)
            {
                const EqRow& e = eq.at(sid);
                if (!bud.count(*e.ammo)) { auto w = want.find(*e.ammo); bud[*e.ammo] = w == want.end() ? 0 : (double)w->second; }
                long long k = std::min<long long>(want.at(sid), (long long)std::floor(bud[*e.ammo] / *e.max));
                bud[*e.ammo] -= k * *e.max;
                if (k < want.at(sid)) held[sid] = want.at(sid) - k;
            }
            return held;
        }
        std::pair<Counts, Counts> clRest(Counts& gv, const Counts& want, const Counts& t, const Counts& q, const std::set<std::string>* Q)
        {
            Counts now = clAdd(t, q), t2, q2;
            for (auto& kv : clAdd(want, now))
            {
                const std::string& sid = kv.first;
                auto g = [](const Counts& c, const std::string& k) { auto i = c.find(k); return i == c.end() ? 0LL : i->second; };
                long long n = std::max<long long>(0, g(want, sid) + g(now, sid) - g(gv, sid));
                if (n > 0)
                {
                    if ((Q && Q->count(sid)) || q.count(sid)) q2[sid] = n; else t2[sid] = n;
                }
            }
            gv = now;
            return {t2, q2};
        }
        long long qgGive(long long n, long long tot, long long have) { return std::max<long long>(0, std::min(n, tot - have)); }
        std::string qgKey(const char* kind, const std::string& src, const std::string& sid, long long n, long long tot, const std::string& who)
        {
            return std::string(kind) + "@" + src + ";" + sid + ";" + std::to_string(n) + ";" + std::to_string(tot) + ";" + who;
        }
        bool qgParse(const std::string& k, std::string& kind, std::string& src, std::string& sid, long long& n, long long& tot, std::string& who)
        {
            if (k.size() < 4 || k[2] != '@' || (k.compare(0, 2, "qg") != 0 && k.compare(0, 2, "qc") != 0)) return false;
            std::vector<std::string> f;
            size_t a = 3;
            while (true)
            {
                size_t b = k.find(';', a);
                f.push_back(k.substr(a, b == std::string::npos ? std::string::npos : b - a));
                if (b == std::string::npos) break;
                a = b + 1;
            }
            if (f.size() != 5 || f[0].empty() || !tokOk(f[1]) || f[2].empty() || f[3].empty() || f[4].empty()) return false;
            for (char c : f[0] + f[4])
                if (c == '|' || c == '#' || c == ' ' || c == '\t') return false;
            for (int i = 2; i < 4; ++i)
                for (char c : f[(size_t)i])
                    if (c < '0' || c > '9') return false;
            if (f[2].size() > 9 || f[3].size() > 9) return false;
            kind = k.substr(0, 2);
            src = f[0];
            sid = f[1];
            n = std::stoll(f[2]);
            tot = std::stoll(f[3]);
            who = f[4];
            return n > 0;
        }
    }

    using namespace pure;

    namespace
    {
        double numOr(const std::string& s, double d)
        {
            auto v = tonum(s);
            return v ? *v : d;
        }
        bool exactXN(const std::string& ent, std::string& sid, long long& n)
        {
            if (!allOf(ent, isW)) return false;
            for (size_t k = ent.size(); k-- > 1;)
            {
                if (ent[k] != 'x') continue;
                std::string d = ent.substr(k + 1);
                if (!allOf(d, isD)) continue;
                sid = ent.substr(0, k);
                n = std::strtoll(d.c_str(), nullptr, 10);
                return true;
            }
            return false;
        }
        size_t humanAt(const std::string& s, size_t from, size_t& end)
        {
            static const std::string p = "BP_AI_Human_C_";
            size_t i = from;
            while ((i = s.find(p, i)) != std::string::npos)
            {
                size_t j = i + p.size();
                size_t k = j;
                while (k < s.size() && isD(s[k])) ++k;
                if (k > j) { end = k; return i; }
                ++i;
            }
            return std::string::npos;
        }
        std::optional<std::string> humanName(const std::string& full)
        {
            size_t e = 0, a = humanAt(full, 0, e);
            if (a == std::string::npos) return std::nullopt;
            return full.substr(a, e - a);
        }
        bool isHumanId(const std::string& s)
        {
            size_t e = 0, a = humanAt(s, 0, e);
            return a == 0 && e == s.size();
        }
        bool hexRun(const std::string& k, size_t at, size_t& end)
        {
            size_t j = at;
            while (j < k.size() && isX(k[j])) ++j;
            end = j;
            return j > at;
        }
        bool keyLsc(const std::string& k, std::string& sid, std::string& rest)
        {
            if (!startsWith(k, "lsc@")) return false;
            size_t e;
            if (!hexRun(k, 4, e) || e >= k.size() || k[e] != ';') return false;
            sid = k.substr(4, e - 4);
            rest = k.substr(e + 1);
            return true;
        }
        bool keyLsl(const std::string& k, std::string& sid, std::string& rest)
        {
            if (!startsWith(k, "lsl@")) return false;
            size_t e;
            if (!hexRun(k, 4, e) || k.compare(e, 3, ";c;") != 0) return false;
            sid = k.substr(4, e - 4);
            rest = k.substr(e + 3);
            return true;
        }
        bool keyCtake(const std::string& k, std::string& sid, std::string& items)
        {
            if (!startsWith(k, "ctake@")) return false;
            size_t e;
            if (!hexRun(k, 6, e) || e >= k.size() || k[e] != ';') return false;
            size_t f = k.find(';', e + 1);
            if (f == std::string::npos || f == e + 1) return false;
            sid = k.substr(6, e - 6);
            size_t g = k.find(';', f + 1);
            items = k.substr(f + 1, g == std::string::npos ? std::string::npos : g - f - 1);
            return true;
        }
        bool keyCput(const std::string& k, std::string& sid, std::string& items)
        {
            if (!startsWith(k, "cput@")) return false;
            size_t e;
            if (!hexRun(k, 5, e) || e >= k.size() || k[e] != ';') return false;
            size_t f = k.find(';', e + 1);
            if (f == std::string::npos || f == e + 1) return false;
            sid = k.substr(5, e - 5);
            size_t g = k.find(';', f + 1);
            items = k.substr(f + 1, g == std::string::npos ? std::string::npos : g - f - 1);
            return true;
        }
        std::vector<s2x_is::IState> keyStates(const std::string& k)
        {
            std::vector<s2x_is::IState> r;
            if (!startsWith(k, "cput@") && !startsWith(k, "ctake@")) return r;
            size_t a = k.find(';');
            size_t b = a == std::string::npos ? a : k.find(';', a + 1);
            size_t c = b == std::string::npos ? b : k.find(';', b + 1);
            if (c == std::string::npos || k.compare(c + 1, 3, "is=") != 0) return r;
            if (!s2x_is::decList(k.substr(c + 4), r)) r.clear();
            return r;
        }
        int keyCdelta(const std::string& k, std::string& sid, std::string& items)
        {
            if (keyCtake(k, sid, items)) return -1;
            if (keyCput(k, sid, items)) return 1;
            return 0;
        }
        bool keyCtakeSid(const std::string& k, std::string& sid)
        {
            if (!startsWith(k, "ctake@")) return false;
            size_t e;
            if (!hexRun(k, 6, e) || e >= k.size() || k[e] != ';') return false;
            sid = k.substr(6, e - 6);
            return true;
        }
        bool numPrefix(const std::string& f, double& v)
        {
            size_t i = 0;
            while (i < f.size() && (isD(f[i]) || f[i] == '-' || f[i] == '.')) ++i;
            if (i == 0) return false;
            auto n = tonum(f.substr(0, i));
            if (!n) return false;
            v = *n;
            return true;
        }
        bool fullNum(const std::string& f, double& v)
        {
            if (!numChars(f)) return false;
            auto n = tonum(f);
            if (!n) return false;
            v = *n;
            return true;
        }
        bool fullInt(const std::string& f, double& v)
        {
            if (!intChars(f)) return false;
            auto n = tonum(f);
            if (!n) return false;
            v = *n;
            return true;
        }
        bool wq(const std::string& s)
        {
            if (s.empty()) return false;
            for (char c : s) if (!(isW(c) || c == '?')) return false;
            return true;
        }
        bool clList(const std::string& s)
        {
            for (char c : s) if (!(isW(c) || c == '*' || c == '.' || c == '-')) return false;
            return true;
        }
        std::string join(const std::vector<std::string>& v, const char* sep)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) { if (i) s += sep; s += v[i]; }
            return s;
        }
        long long g0(const Counts& c, const std::string& k)
        {
            auto i = c.find(k);
            return i == c.end() ? 0 : i->second;
        }
        Obj interacted(Obj pw)
        {
            if (!VAL(pw)) return 0;
            Obj a = E().interacted(pw);
            return VAL(a) ? a : 0;
        }
        bool aliveOr(Obj o, bool def)
        {
            bool a = def;
            if (!VAL(o) || !E().alive(o, a)) return def;
            return a;
        }
        Counts invSnapI(Obj pw, long long& m)
        {
            Counts t = countAll(pw);
            m = 0;
            long long mm = 0;
            if (VAL(pw) && E().money(pw, mm)) m = mm;
            return t;
        }

        void takenLoad()
        {
            Taken& T = S().tk;
            T.loaded = true;
            T.set.clear();
            T.list.clear();
            S().pt.base.clear();
            if (!HOST()) return;
            std::string text;
            if (E().readText("taken.txt", text))
                for (auto& l : lines(text))
                {
                    std::string k = stripWs(l);
                    if (!k.empty() && !T.set.count(k)) { T.set.insert(k); T.list.push_back(k); }
                }
            S().pt.base = T.set;
            LOG("NOLA TAKEN: " + std::to_string(T.list.size()) + " chaves lidas de taken.txt");
        }
        std::set<std::string>& mineSet()
        {
            Taken& T = S().tk;
            if (T.mineLoaded) return T.mine;
            T.mineLoaded = true;
            std::string text;
            if (E().readText("taken_mine.txt", text))
            {
                for (auto& l : lines(text)) { std::string k = stripWs(l); if (!k.empty()) T.mine.insert(k); }
            }
            else if (HOST())
            {
                std::string g, out;
                int n = 0;
                if (E().readText("taken.txt", g))
                    for (auto& l : lines(g))
                    {
                        std::string k = stripWs(l);
                        if (k.empty()) continue;
                        T.mine.insert(k);
                        ++n;
                        out += k + "\n";
                    }
                E().writeText("taken_mine.txt", out);
                LOG("NOLA TAKEN: taken_mine.txt criado de taken.txt (" + std::to_string(n) + " chaves contam como minhas: migracao)");
            }
            return T.mine;
        }
        void takenMineAdd(const std::string& key)
        {
            auto& M2 = mineSet();
            if (M2.count(key)) return;
            M2.insert(key);
            E().appendText("taken_mine.txt", key + "\n");
        }
        bool takenAdd(const std::string& key)
        {
            Taken& T = S().tk;
            if (!T.loaded) takenLoad();
            if (key.empty() || T.set.count(key)) return false;
            T.set.insert(key);
            T.list.push_back(key);
            if (HOST()) E().appendText("taken.txt", key + "\n");
            LOG("NOLA TAKEN +" + key + " (total " + std::to_string(T.list.size()) + ")");
            return true;
        }
        bool takenHas(const std::string& k) { return S().tk.loaded && S().tk.set.count(k) != 0; }
        bool dropHas(Obj o);
        bool dropSeen(Obj o);
        void dropClear(const char* why);
        void qgReset(const char* why);
        void gClear();
        void tkReset(const std::string& why)
        {
            State& st = S();
            size_t n = st.tk.list.size();
            st.tk.set.clear();
            st.tk.list.clear();
            st.tk.loaded = true;
            st.tk.mine.clear();
            st.tk.mineLoaded = true;
            if (HOST()) takenLoad();
            else E().writeText("taken_mine.txt", "");
            st.loot.xi = 0;
            st.loot.given.clear();
            st.loot.qkeep.clear();
            qgReset(why.c_str());
            st.qg.mine.clear();
            st.qg.loaded = true;
            st.qg.fresh = false;
            E().writeText("qshare_mine.txt", "");
            st.tList.clear();
            st.tOut.reset();
            gClear();
            dropClear(why.c_str());
            st.pt.applied.clear();
            st.pt.base.clear();
            st.pt.init = false;
            Ls& L = st.ls;
            L.canon.clear();
            L.mine.clear();
            L.cb.clear();
            L.done.clear();
            L.rc.clear();
            L.emp.clear();
            L.ix = 0;
            L.crd.reset();
            st.ap.taken.clear();
            LOG(sf("NOLA TAKEN: %s: %d chaves da campanha anterior descartadas (chao, recipientes, lootshare, ptake, artefatos)", why.c_str(), (int)n));
        }
        std::string tkTag(const std::string& s, std::vector<std::string>& keys)
        {
            keys = splitNE(s, '|');
            std::string tag;
            if (!keys.empty() && keys[0].size() > 1 && keys[0][0] == '=')
            {
                tag = keys[0].substr(1);
                keys.erase(keys.begin());
            }
            return tag;
        }
        std::string corpseKey(Obj o)
        {
            V3 q;
            E().loc(o, q);
            return sf("corpse@%.0f,%.0f", q.x / 100, q.y / 100);
        }
        Obj findPawnByName(const std::string& id)
        {
            for (Obj o : E().all("Pawn"))
                if (VAL(o) && E().full(o).find(id) != std::string::npos) return o;
            return 0;
        }

        void qgClaim(const std::string& k, int pid);
        void tIn(const std::string& k, int pid)
        {
            if (k.compare(0, 3, "qc@") == 0) qgClaim(k, pid);
            else if (!takenHas(k)) takenAdd(k);
        }
        bool lsNoG(Obj o);

        void lootHotIn(Loot& L)
        {
            if (L.hotIn) return;
            L.hotIn = true;
            auto k = x0::hot::logic::unpack(x0::hot::logic::take("LOOT"));
            if (!k.ok) return;
            double t = NOW();
            L.seq = std::max(L.seq, k.seq);
            for (auto& s : k.keys)
            {
                std::string v = s.substr(1);
                if (s[0] == 'd') L.done[numOr(v, 0)] = t;
                else if (s[0] == 'a') L.applied[numOr(v, 0)] = t;
                else if (s[0] == 'g') L.given.insert(v);
                else if (s[0] == 'k') L.qkeep.insert(v);
            }
            LOG(sf("NOLA hot: loot seq %lld and %d done/applied/given keys kept across the reload (not given again)", L.seq, (int)k.keys.size()));
        }

        void lootHotOut(Loot& L)
        {
#ifdef S2X_RELEASE
            (void)L;
#else
            std::vector<std::string> keys;
            for (auto& kv : L.done) keys.push_back("d" + lld((long long)kv.first));
            for (auto& kv : L.applied) keys.push_back("a" + lld((long long)kv.first));
            for (auto& g : L.given) keys.push_back("g" + g);
            for (auto& q : L.qkeep) keys.push_back("k" + q);
            std::string p = x0::hot::logic::pack(L.seq, keys);
            if (p == L.hotKept) return;
            L.hotKept = p;
            if (!x0::hot::logic::keep("LOOT", p)) LOG(sf("NOLA hot: WARNING loot stash (%d bytes) over the environment limit, tail keys cut, some loot may be given again after the reload", (int)p.size()));
#endif
        }

        void lootTickRun();

        void lootTick()
        {
            lootHotIn(S().loot);
            lootTickRun();
            lootHotOut(S().loot);
        }

        void lootTickRun()
        {
            if (!FON("loot")) return;
            State& st = S();
            Loot& L = st.loot;
            L.init = true;
            double now = NOW();
            if (!st.tk.loaded) takenLoad();
            Obj lp = E().pawn();
            if (!VAL(lp)) return;
            if (HOST())
            {
                if (now - L.lastScan >= 1)
                {
                    L.lastScan = now;
                    V3 lpos;
                    E().loc(lp, lpos);
                    std::vector<std::string> r;
                    for (Obj o : E().all("Pawn"))
                    {
                        if (!VAL(o)) continue;
                        if (r.size() < 4 && o != lp && !E().isOwnBody(o) && !aliveOr(o, true))
                        {
                            auto nm = humanName(E().full(o));
                            if (nm && !takenHas(corpseKey(o)) && !lsNoG(o))
                            {
                                V3 q;
                                E().loc(o, q);
                                if (sq(q.x - lpos.x) + sq(q.y - lpos.y) < sq(3000)) r.push_back(sf("%s@%.0f,%.0f,%.0f", nm->c_str(), q.x, q.y, q.z));
                            }
                        }
                    }
                    L.corpses = join(r, "|");
                }
                std::string tk;
                if (MC())
                {
                    for (int v = 0; v < E().views(); ++v)
                        if (PARTV("T", tk, v) && !tk.empty() && FON("taken"))
                            for (auto& k : splitNE(tk, '|')) tIn(k, E().viewPid(v));
                }
                else if (PART("T", tk) && !tk.empty() && FON("taken"))
                    for (auto& k : splitNE(tk, '|')) tIn(k, -1);
                std::string q;
                if (PART("Q", q))
                {
                    size_t sc = q.find(';');
                    std::string ss = sc == std::string::npos ? "" : q.substr(0, sc);
                    std::string id = sc == std::string::npos ? "" : q.substr(sc + 1);
                    if (allOf(ss, isD) && isHumanId(id))
                    {
                        double seq = numOr(ss, 0);
                        if (!L.done.count(seq))
                        {
                            L.done[seq] = now;
                            long long sqn = (long long)seq;
                            Obj o = findPawnByName(id);
                            if (!o) LOG("NOLA LOOT_REQ " + id + " seq=" + lld(sqn) + ": pawn nao encontrado");
                            else if (aliveOr(o, false)) LOG("NOLA LOOT_REQ " + id + " seq=" + lld(sqn) + ": ainda vivo, negado");
                            else if (L.given.count(id) || takenHas(corpseKey(o))) LOG("NOLA LOOT_REQ " + id + " seq=" + lld(sqn) + ": ja entregue, negado");
                            else
                            {
                                double t0 = NOW();
                                std::vector<std::string> items;
                                Counts c = countAll(o);
                                for (auto& s : sids()) { auto f = c.find(s); if (f != c.end()) items.push_back(s + "x" + lld(f->second)); }
                                L.given.insert(id);
                                if (FI("qitem", 0) == 1 && E().qiCorpse(o, id)) L.qkeep.insert(id);
                                else takenAdd(corpseKey(o));
                                L.replyQ.push_back(LootReply{sqn, id, join(items, "|"), std::nullopt});
                                LOG(sf("NOLA LOOT_REQ %s seq=%lld: %d itens em %.3f s: %s (corpo marcado como entregue)", id.c_str(), sqn, (int)items.size(), NOW() - t0, join(items, " ").c_str()));
                            }
                        }
                    }
                }
                if (!L.given.empty())
                {
                    Obj ia = interacted(E().controller());
                    if (ia)
                    {
                        auto nm = humanName(E().full(ia));
                        if (nm && L.given.count(*nm) && !L.qkeep.count(*nm))
                        {
                            bool vis = false;
                            Obj w = E().first("InventoryNew");
                            if (w) E().visible(w, vis);
                            if (vis)
                            {
                                E().closeLoot(E().controller());
                                NOLA_CEN_EV("lootclose", "body_given", *nm);
                                LOG("NOLA LOOT: janela de saque do corpo entregue " + *nm + " fechada no host");
                            }
                        }
                    }
                }
            }
            else
            {
                std::optional<std::pair<std::string, double>> near;
                V3 lpos;
                E().loc(lp, lpos);
                std::string cs;
                bool hasC = PART("C", cs);
                for (auto& ent : splitNE(cs, '|'))
                {
                    size_t at = ent.find('@');
                    if (at == std::string::npos) continue;
                    std::string id = ent.substr(0, at);
                    auto f = splitAll(ent.substr(at + 1), ',');
                    double x, y, z;
                    if (!isHumanId(id) || f.size() != 3 || !fullNum(f[0], x) || !fullNum(f[1], y) || !fullNum(f[2], z)) continue;
                    double d = std::sqrt(sq(x - lpos.x) + sq(y - lpos.y));
                    if (d < F("lootr", 700) && (!near || d < near->second)) near = std::make_pair(id, d);
                }
                std::string xs;
                PART("X", xs);
                std::vector<std::string> xk;
                std::string xc = tkTag(xs, xk);
                if (!xc.empty())
                {
                    if (st.tk.cid && *st.tk.cid != xc) tkReset("campanha do host mudou (" + *st.tk.cid + " -> " + xc + ")");
                    st.tk.cid = xc;
                }
                for (auto& k : xk) if (!takenHas(k)) takenAdd(k);
                bool down = E().keyDownG();
                auto latch = E().keyLatch();
                bool latched = latch && (now - *latch) < 1.0 && (!L.latchUsed || *L.latchUsed != *latch);
                if (latched) L.latchUsed = latch;
                double lr = F("lootreq", 0);
                bool want = latched || (down && !L.keyDown) || (lr != 0 && lr != L.reqSeen);
                L.keyDown = down;
                L.reqSeen = lr;
                if (want)
                {
                    if (near)
                    {
                        L.seq += 1;
                        L.req = LootReq{L.seq, near->first, now + 3};
                        LOG(sf("NOLA LOOT pedido seq=%lld %s a %.0f cm", L.seq, near->first.c_str(), near->second));
                    }
                    else LOG("NOLA LOOT: nenhum corpo a menos de " + luaF(F("lootr", 700) / 100) + " m (lista do host: '" + (hasC ? cs : std::string("nil")) + "')");
                }
                std::string r;
                if (PART("R", r))
                {
                    size_t a = r.find(';');
                    size_t b = a == std::string::npos ? std::string::npos : r.find(';', a + 1);
                    if (b != std::string::npos)
                    {
                        std::string ss = r.substr(0, a), id = r.substr(a + 1, b - a - 1), items = r.substr(b + 1);
                        if (allOf(ss, isD) && isHumanId(id))
                        {
                            double seq = numOr(ss, 0);
                            if (!L.applied.count(seq))
                            {
                                L.applied[seq] = now;
                                std::vector<std::string> got;
                                for (auto& ent : splitNE(items, '|'))
                                {
                                    std::string sid;
                                    long long n;
                                    if (!exactXN(ent, sid, n)) continue;
                                    long long b4 = 0, af = 0;
                                    if (E().invReadable(lp)) E().count(lp, sid, b4); else ++S().invGuard;
                                    bool ok = E().createItem(sid, 0, n);
                                    if (E().invReadable(lp)) E().count(lp, sid, af); else ++S().invGuard;
                                    got.push_back(sf("%s +%lld (%lld->%lld ok=%s)", sid.c_str(), n, b4, af, bstr(ok).c_str()));
                                }
                                LOG("NOLA LOOT_GOT seq=" + lld((long long)seq) + " de " + id + ": " + join(got, "; "));
                            }
                        }
                    }
                }
            }
        }

        std::string lootOutI()
        {
            State& st = S();
            Loot& L = st.loot;
            if (!L.init || !FON("loot")) return "";
            double now = NOW();
            std::string s;
            if (HOST())
            {
                auto& TL = st.tk.list;
                std::vector<std::string> part;
                std::string cid = E().campaign();
                if (!cid.empty()) part.push_back("=" + cid);
                if (!TL.empty())
                {
                    long long n = (long long)TL.size();
                    L.xi = (L.xi % n) + 1;
                    if (now - L.xAt >= 0.3) { L.xAt = now; L.xi = L.xi % n + 1; }
                    for (long long i = 0; i <= std::min<long long>(3, n - 1); ++i) part.push_back(TL[(size_t)((L.xi + i - 1) % n)]);
                }
                if (!part.empty()) s += "#X:" + join(part, "|");
                if (!L.corpses.empty()) s += "#C:" + L.corpses;
                auto& Q = L.replyQ;
                if (!Q.empty())
                {
                    LootReply* h = &Q.front();
                    if (!h->at) h->at = now;
                    if (now >= *h->at + F("resend", 6))
                    {
                        Q.pop_front();
                        h = Q.empty() ? nullptr : &Q.front();
                        if (h) h->at = now;
                    }
                    if (h) s += sf("#R:%lld;%s;%s", h->seq, h->id.c_str(), h->str.c_str());
                }
            }
            else
            {
                if (L.req && now < L.req->untilT) s += sf("#Q:%lld;%s", L.req->seq, L.req->id.c_str());
                if (st.tOut)
                {
                    st.tList.push_back(TOut{st.tOut->id, now + F("resend", 6)});
                    st.tOut.reset();
                }
                if (!st.tList.empty())
                {
                    std::vector<TOut> keep;
                    std::vector<std::string> ids;
                    for (auto& e : st.tList) if (now < e.untilT) { keep.push_back(e); ids.push_back(e.id); }
                    st.tList = keep;
                    if (!ids.empty()) s += "#T:" + join(ids, "|");
                }
            }
            return s;
        }

        bool gHide(Obj o)
        {
            if (!E().setHidden(o, true)) return false;
            E().setCollision(o, false);
            bool hidden = false;
            return E().hidden(o, hidden) && hidden;
        }
        std::string meshOf(Obj o)
        {
            auto m = E().meshName(o, "MeshComponent");
            return m ? *m : std::string("?");
        }
        std::string gPos(double x, double y, double z)
        {
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return {};
            return sf("%.0f,%.0f,%.0f", x, y, z);
        }
        constexpr double G_RADIUS = 15;
        constexpr double G_MARGIN = 3;
        double gDist(const V3& a, const V3& b)
        {
            return std::sqrt(sq(a.x - b.x) + sq(a.y - b.y) + sq(a.z - b.z));
        }
        std::string gKey(double x, double y, double z, const std::string& cls, const std::string& mesh)
        {
            std::string p = gPos(x, y, z);
            return p.empty() ? p : p + ";" + cls + ";" + mesh;
        }
        std::string gLocal(Obj o) { return std::to_string(E().addr(o)) + "|" + E().full(o); }
        struct GMatch
        {
            Obj o = 0;
            std::string local;
            V3 pos;
            double d = 0;
            bool ambiguous = false;
        };
        void gRemember(const std::string& key, const std::string& cls, const std::string& mesh, const GMatch& m)
        {
            if (key.empty() || m.local.empty() || m.ambiguous) return;
            Ground& G = S().ground;
            Ground::Copy copy{m.local, cls, mesh, m.pos};
            auto old = G.used.find(key);
            if (m.o && old != G.used.end() && old->second.local != m.local)
            {
                std::string previous = old->second.local;
                for (auto& kv : G.used) if (kv.second.local == previous) kv.second = copy;
                G.gone.erase(previous);
            }
            G.used[key] = copy;
            G.gone[m.local] = copy;
        }
        GMatch gFind(double x, double y, double z, const std::string& cls, const std::string& mesh)
        {
            std::string key = gKey(x, y, z, cls, mesh);
            if (key.empty()) return {};
            Ground& G = S().ground;
            const V3 at{x, y, z};
            std::vector<GMatch> copies;
            std::set<std::string> present;
            for (Obj o : E().all(cls.c_str()))
            {
                if (!VAL(o) || dropHas(o) || meshOf(o) != mesh) continue;
                bool hid = false;
                V3 q;
                if (!E().hidden(o, hid) || !E().loc(o, q) || gPos(q.x, q.y, q.z).empty()) continue;
                std::string local = gLocal(o);
                auto u = G.gone.find(local);
                if (u != G.gone.end())
                {
                    if (gDist(q, u->second.pos) > G_RADIUS) hid = true;
                    q = u->second.pos;
                }
                if (!(gDist(q, at) <= G_RADIUS + G_MARGIN)) continue;
                if (hid) G.gone[local] = Ground::Copy{local, cls, mesh, q};
                copies.push_back(GMatch{hid ? 0 : o, local, q});
                present.insert(local);
            }
            auto bound = G.used.find(key);
            if (bound != G.used.end())
            {
                const auto& c = bound->second;
                GMatch done{0, c.local, c.pos};
                for (auto m : copies) if (m.local == c.local)
                {
                    m.d = gDist(m.pos, at);
                    return m;
                }
                GMatch replacement;
                int nearby = 0;
                for (const auto& m : copies)
                {
                    if (gDist(m.pos, c.pos) <= G_MARGIN) ++nearby;
                    if (m.o && gPos(m.pos.x, m.pos.y, m.pos.z) == gPos(c.pos.x, c.pos.y, c.pos.z)) replacement = m;
                }
                if (replacement.o && nearby == 1)
                {
                    replacement.d = gDist(replacement.pos, at);
                    if (replacement.d <= G_RADIUS) return replacement;
                }
                return done;
            }
            for (const auto& kv : G.gone)
            {
                if (kv.second.cls == cls && kv.second.mesh == mesh && !present.count(kv.first)
                    && gDist(kv.second.pos, at) <= G_RADIUS + G_MARGIN)
                    copies.push_back(GMatch{0, kv.first, kv.second.pos});
            }
            GMatch best;
            double first = G_RADIUS + G_MARGIN + 1, second = first;
            for (auto m : copies)
            {
                m.d = gDist(m.pos, at);
                if (!(m.d <= G_RADIUS + G_MARGIN)) continue;
                if (m.d < first) { second = first; first = m.d; best = m; }
                else if (m.d < second) second = m.d;
            }
            if (first > G_RADIUS) return {};
            if (!best.local.empty() && second - first <= G_MARGIN)
            {
                GMatch unsure;
                unsure.ambiguous = true;
                return unsure;
            }
            return best;
        }
        void gClear()
        {
            Ground& G = S().ground;
            G.cand.clear();
            G.out.clear();
            G.seen.clear();
            G.pend.clear();
            G.used.clear();
            G.gone.clear();
            G.wk.clear();
            G.sweepAt = 0;
            S().mc.gown.clear();
        }
        bool gParse(const std::string& s, std::string xyz[3], std::string& cls, std::string& mesh)
        {
            auto f = splitAll(s, ';');
            if (f.size() != 3) return false;
            auto c = splitAll(f[0], ',');
            if (c.size() != 3) return false;
            double v;
            for (int i = 0; i < 3; ++i) { if (!fullInt(c[i], v)) return false; xyz[i] = c[i]; }
            if (!allOf(f[1], isW) || !wq(f[2])) return false;
            cls = f[1];
            mesh = f[2];
            return true;
        }

        void mcArb(std::vector<McHold>& H, std::vector<std::pair<int, std::string>> claims, bool hostWins, const char* what)
        {
            double now = NOW();
            bool keyfix = FON("keyfix");
            long long tol = FI("keytol", 1);
            std::stable_sort(claims.begin(), claims.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            std::vector<McHold> keep;
            for (auto& h : H)
            {
                bool still = false;
                for (auto& c : claims)
                    if (c.first == h.pid && keyClose(c.second, h.key, keyfix, tol)) { still = true; break; }
                if (still) keep.push_back(h);
                else LOG(sf("NOLA mc %s: %s solto pelo pid %d (%.1f s)", what, h.key.c_str(), h.pid, now - h.since));
            }
            H = keep;
            for (auto& c : claims)
            {
                bool has = false;
                for (auto& h : H) if (h.pid == c.first) { has = true; break; }
                if (has) continue;
                McHold* other = nullptr;
                for (auto& h : H) if (keyClose(c.second, h.key, keyfix, tol)) { other = &h; break; }
                if (other)
                {
                    if (!(hostWins && c.first == 0)) continue;
                    LOG(sf("NOLA mc %s: %s tirado do pid %d: o host fica (lockfirst=0)", what, c.second.c_str(), other->pid));
                    other->pid = 0;
                    other->key = c.second;
                    other->since = now;
                    continue;
                }
                H.push_back(McHold{c.first, c.second, now});
                LOG(sf("NOLA mc %s: %s dado ao pid %d (primeiro a pedir)", what, c.second.c_str(), c.first));
            }
        }
        std::string mcHoldOut(const std::vector<McHold>& H)
        {
            std::vector<std::string> t;
            for (auto& h : H) t.push_back(sf("%d:%s", h.pid, h.key.c_str()));
            return join(t, "|");
        }
        std::vector<McHold> mcHoldIn(const std::string& s)
        {
            std::vector<McHold> r;
            for (auto& e : splitNE(s, '|'))
            {
                size_t c = e.find(':');
                if (c == std::string::npos || c == 0 || c + 1 >= e.size() || !allOf(e.substr(0, c), isD)) continue;
                r.push_back(McHold{std::atoi(e.substr(0, c).c_str()), e.substr(c + 1), 0});
            }
            return r;
        }
        std::optional<int> mcHolder(const std::vector<McHold>& H, const std::string& id)
        {
            bool keyfix = FON("keyfix");
            long long tol = FI("keytol", 1);
            for (auto& h : H) if (keyClose(h.key, id, keyfix, tol)) return h.pid;
            return std::nullopt;
        }
        std::vector<McHold> mcHolds(const char* tag, std::vector<McHold>& hostSide)
        {
            if (HOST()) return hostSide;
            std::string s;
            PART(tag, s);
            return mcHoldIn(s);
        }
        std::string mcRelIn(const char* tag)
        {
            Mc& M = S().mc;
            double now = NOW();
            std::string raw;
            PART(tag, raw);
            auto& seen = M.hqSeen[tag];
            for (auto it = seen.begin(); it != seen.end();)
                if (now - it->second > 30) it = seen.erase(it); else ++it;
            auto& q = M.hq[tag];
            for (auto& e : splitNE(raw, '|'))
            {
                bool was = seen.count(e) != 0;
                seen[e] = now;
                if (!was && q.size() < 500) q.push_back(e);
            }
            bool held = FON("bodysafe") && FI("mchold", 1) == 1 && !E().worldOpen();
            if (held)
            {
                if (!q.empty() && !M.hqHeld)
                {
                    M.hqHeld = true;
                    LOG(sf("NOLA mc: eventos de itens de outros jogadores esperando o mundo abrir (H1): %s", tag));
                }
                return {};
            }
            if (q.empty()) return {};
            if (M.hqHeld)
            {
                M.hqHeld = false;
                long long n = 0;
                for (auto& kv : M.hq) n += (long long)kv.second.size();
                M.hqParked += n;
                LOG(sf("NOLA mc: mundo aberto, %lld eventos de itens de outros jogadores aplicados agora", n));
            }
            std::string out = join(q, "|");
            q.clear();
            return out;
        }
        bool mcGOwnTake(int pid, double x, double y, double z, const std::string& cls, const std::string& mesh, const std::string& ent)
        {
            if (gPos(x, y, z).empty()) return false;
            Mc& M = S().mc;
            double now = NOW();
            M.gown.erase(std::remove_if(M.gown.begin(), M.gown.end(), [&](const McGOwn& o) { return now - o.t > 60; }), M.gown.end());
            GMatch current = gFind(x, y, z, cls, mesh);
            if (current.ambiguous)
            {
                LOG("NOLA GROUND_AMBIGUOUS " + ent + ": nearby copies; take not relayed");
                return false;
            }
            McGOwn* owner = nullptr;
            double first = G_RADIUS + G_MARGIN + 1, second = first;
            for (auto& o : M.gown)
            {
                if (o.cls != cls || o.mesh != mesh) continue;
                auto bound = S().ground.used.find(gKey(o.x, o.y, o.z, cls, mesh));
                const std::string& prior = bound == S().ground.used.end() ? o.local : bound->second.local;
                double d = gDist(V3{x, y, z}, V3{o.x, o.y, o.z});
                if (!current.local.empty() && !prior.empty())
                {
                    if (current.local != prior) continue;
                    d = 0;
                }
                if (!(d <= G_RADIUS + G_MARGIN)) continue;
                if (d < first) { second = first; first = d; owner = &o; }
                else if (d < second) second = d;
            }
            if (owner && first <= G_RADIUS)
            {
                if (second - first <= G_MARGIN)
                {
                    LOG("NOLA GROUND_AMBIGUOUS " + ent + ": nearby owners; take not relayed");
                    return false;
                }
                if (owner->pid == pid) return true;
                M.gdup += 1;
                LOG(sf("NOLA GROUND_DUP %s: o pid %d pegou o item que o pid %d ja tinha pegado ha %.1f s (primeiro vence; a copia do pid %d nao tem como ser desfeita: o item no chao nao tem SID)",
                       ent.c_str(), pid, owner->pid, now - owner->t, pid));
                return false;
            }
            M.gown.push_back(McGOwn{pid, x, y, z, now, cls, mesh, current.local});
            return true;
        }
        void groundTick()
        {
            if (!FON("ground")) return;
            State& st = S();
            Ground& G = st.ground;
            st.groundOn = true;
            double now = NOW();
            Obj pw = E().pawn();
            if (!VAL(pw)) return;
            std::string wk = E().worldKey();
            if (wk.empty()) return;
            if (!G.wk.empty() && G.wk != wk) gClear();
            G.wk = wk;
            Obj a = interacted(pw);
            if (a)
            {
                std::string cls = firstTok(E().full(a));
                if (!cls.empty() && has(cls, "ItemContainer"))
                {
                    std::string nm = E().name(a);
                    V3 q;
                    if (E().loc(a, q) && std::fabs(q.x) + std::fabs(q.y) > 1000)
                    {
                        GCand& c = G.cand[nm];
                        c.o = a;
                        c.cls = cls;
                        c.local = gLocal(a);
                        c.x = q.x;
                        c.y = q.y;
                        c.z = q.z;
                        c.t = now;
                        if (!c.mesh) c.mesh = meshOf(a);
                    }
                }
            }
            for (auto it = G.cand.begin(); it != G.cand.end();)
            {
                GCand& c = it->second;
                bool gone = false;
                V3 q;
                if (!VAL(c.o)) gone = true;
                else if (E().loc(c.o, q) && std::fabs(q.x) + std::fabs(q.y) < 1000) gone = true;
                if (gone)
                {
                    GCand cc = c;
                    it = G.cand.erase(it);
                    if (dropSeen(cc.o)) continue;
                    G.seq += 1;
                    std::string mesh = cc.mesh.value_or("?");
                    gRemember(gKey(cc.x, cc.y, cc.z, cc.cls, mesh), cc.cls, mesh,
                              GMatch{0, cc.local, V3{cc.x, cc.y, cc.z}});
                    std::string e = sf("%lld;%.0f,%.0f,%.0f;%s;%s", G.seq, cc.x, cc.y, cc.z, cc.cls.c_str(), mesh.c_str());
                    G.out.insert(G.out.begin(), GOut{e, now + 4});
                    while (G.out.size() > 4) G.out.pop_back();
                    LOG("NOLA GROUND_TAKE " + e);
                    S().qg.g = std::make_pair(sf("g%.0f,%.0f,%.0f", cc.x / 100, cc.y / 100, cc.z / 100), now);
                    if (MC() && HOST()) mcGOwnTake(0, cc.x, cc.y, cc.z, cc.cls, mesh, e);
                    if (FON("taken") && HOST()) takenAdd(sf("item@%.0f,%.0f,%.0f;%s;%s", cc.x, cc.y, cc.z, cc.cls.c_str(), mesh.c_str()));
                }
                else if (now - c.t > 3) it = G.cand.erase(it);
                else ++it;
            }
            struct GIn
            {
                std::string ent, seen, tok;
                int pid = -1;
            };
            std::vector<GIn> gins;
            std::string gs;
            if (MC() && HOST())
            {
                for (int v = 0; v < E().views(); ++v)
                {
                    if (!PARTV("G", gs, v)) continue;
                    std::string tok = vTok(v);
                    for (auto& ent : splitNE(gs, '|')) gins.push_back(GIn{ent, tok + "|" + ent, tok, E().viewPid(v)});
                }
            }
            else
            {
                if (MC()) gs = mcRelIn("G");
                else PART("G", gs);
                for (auto& ent : splitNE(gs, '|')) gins.push_back(GIn{ent, ent, std::string(), -1});
                if (MC())
                {
                    std::string gr = mcRelIn("GR");
                    int me = E().myPid();
                    for (auto& ent : splitNE(gr, '|'))
                    {
                        size_t sc = ent.find(';');
                        if (sc == std::string::npos) continue;
                        int pid = tokPid(ent.substr(0, sc));
                        if (pid < 0 || pid == me) continue;
                        gins.push_back(GIn{ent.substr(sc + 1), ent, ent.substr(0, sc), pid});
                    }
                }
            }
            for (auto& gi : gins)
            {
                const std::string& ent = gi.ent;
                if (G.seen.count(gi.seen)) continue;
                G.seen[gi.seen] = now;
                size_t sc = ent.find(';');
                if (sc == std::string::npos || !allOf(ent.substr(0, sc), isD)) continue;
                std::string xyz[3], cls, mesh;
                if (!gParse(ent.substr(sc + 1), xyz, cls, mesh)) continue;
                if (MC() && HOST())
                {
                    if (!mcGOwnTake(gi.pid, numOr(xyz[0], 0), numOr(xyz[1], 0), numOr(xyz[2], 0), cls, mesh, ent)) continue;
                    S().mc.gr.push_back(McRel{now, gi.tok + ";" + ent});
                }
                else if (gi.pid > 0)
                {
                    S().mc.grIn += 1;
                    LOG(sf("NOLA GROUND_TAKE do jogador pid %d (via host): %s", gi.pid, ent.c_str()));
                }
                GPend p;
                p.x = numOr(xyz[0], 0);
                p.y = numOr(xyz[1], 0);
                p.z = numOr(xyz[2], 0);
                p.cls = cls;
                p.mesh = mesh;
                p.untilT = now + 60;
                p.e = ent;
                p.nextT = 0;
                G.pend.push_back(p);
                if (FON("taken") && HOST()) takenAdd("item@" + xyz[0] + "," + xyz[1] + "," + xyz[2] + ";" + cls + ";" + mesh);
            }
            for (auto it = G.seen.begin(); it != G.seen.end();)
                if (now - it->second > 60) it = G.seen.erase(it); else ++it;
            for (size_t i = G.pend.size(); i-- > 0;)
            {
                GPend& p = G.pend[i];
                if (now < p.nextT) continue;
                p.nextT = now + 2;
                GMatch m = gFind(p.x, p.y, p.z, p.cls, p.mesh);
                if (!m.local.empty())
                {
                    if (m.o && !gHide(m.o)) continue;
                    gRemember(gKey(p.x, p.y, p.z, p.cls, p.mesh), p.cls, p.mesh, m);
                    std::string e = p.e;
                    G.pend.erase(G.pend.begin() + (long)i);
                    if (m.o) LOG(sf("NOLA GROUND_GOT %s: copia local %s a %.0f cm escondida", e.c_str(), E().name(m.o).c_str(), m.d));
                }
                else if (now > p.untilT)
                {
                    std::string e = p.e;
                    G.pend.erase(G.pend.begin() + (long)i);
                    LOG("NOLA GROUND_GOT " + e + ": nenhuma copia local em 60 s (desisti)");
                }
            }
            if (FON("taken") && st.tk.loaded && now >= G.sweepAt)
            {
                G.sweepAt = now + 5;
                std::vector<std::string> TL = st.tk.list;
                for (auto& k : TL)
                {
                    if (!startsWith(k, "item@")) continue;
                    std::string xyz[3], cls, mesh;
                    if (!gParse(k.substr(5), xyz, cls, mesh)) continue;
                    GMatch m = gFind(numOr(xyz[0], 0), numOr(xyz[1], 0), numOr(xyz[2], 0), cls, mesh);
                    if (!m.local.empty())
                    {
                        if (m.o && !gHide(m.o)) continue;
                        gRemember(k.substr(5), cls, mesh, m);
                        if (m.o) LOG(sf("NOLA GROUND_SWEEP %s escondido (%.0f cm de %s)", E().name(m.o).c_str(), m.d, k.c_str()));
                    }
                }
            }
        }
        std::string groundOutI()
        {
            State& st = S();
            if (!st.groundOn || !FON("ground")) return "";
            double now = NOW();
            std::vector<std::string> r;
            for (auto& o : st.ground.out) if (now < o.untilT) r.push_back(o.e);
            return r.empty() ? std::string() : "#G:" + join(r, "|");
        }
        void mcGoffClear(const char* why)
        {
            Mc& M = S().mc;
            for (auto& f : M.goff)
            {
                if (VAL(f.o)) E().setInteraction(f.o, true);
                LOG(sf("NOLA mc chao: %s com interacao de volta (%s)", f.key.c_str(), why));
            }
            M.goff.clear();
            M.ghRaw.clear();
        }
        bool busyOn();
        bool busyIs(Obj a, const std::string& cls, const std::string& id);
        bool cownOn() { return FON("cown") && busyOn(); }
        void mcGroundTick()
        {
            if (!FON("ground") || FI("mcgclaim", 1) != 1)
            {
                if (!S().mc.goff.empty()) mcGoffClear("mcgclaim=0");
                return;
            }
            State& st = S();
            Mc& M = st.mc;
            Ground& G = st.ground;
            double now = NOW();
            const GCand* best = nullptr;
            bool cown = cownOn();
            for (auto& kv : G.cand)
            {
                bool hid = false;
                if (!VAL(kv.second.o) || (E().hidden(kv.second.o, hid) && hid)) continue;
                if (cown && busyIs(kv.second.o, kv.second.cls, lockId(kv.second.cls, kv.second.x, kv.second.y, kv.second.z))) continue;
                if (now - kv.second.t < 1.0 && (!best || kv.second.t > best->t)) best = &kv.second;
            }
            if (best) M.gc = lockId(best->cls, best->x, best->y, best->z);
            else M.gc.reset();
            if (M.gc && M.kclaim && *M.gc == *M.kclaim) M.gc.reset();
            std::vector<McHold> H;
            if (HOST())
            {
                std::vector<std::pair<int, std::string>> claims;
                if (M.gc) claims.push_back({0, *M.gc});
                for (int v = 0; v < E().views(); ++v)
                {
                    std::string s;
                    if (PARTV("GC", s, v) && !s.empty()) claims.push_back({E().viewPid(v), s});
                }
                mcArb(M.gh, claims, false, "chao");
                H = M.gh;
            }
            else
            {
                std::string s;
                PART("GH", s);
                H = mcHoldIn(s);
            }
            int me = E().myPid();
            std::string raw;
            for (auto& h : H) if (h.pid != me) raw += h.key + "|";
            if (raw == M.ghRaw && now - M.ghAt < 2) return;
            M.ghRaw = raw;
            M.ghAt = now;
            for (auto it = M.goff.begin(); it != M.goff.end();)
            {
                bool still = false;
                for (auto& h : H) if (h.pid != me && h.key == it->key) still = true;
                if (still) { ++it; continue; }
                if (VAL(it->o)) E().setInteraction(it->o, true);
                LOG("NOLA mc chao: " + it->key + " livre de novo: interacao de volta");
                it = M.goff.erase(it);
            }
            for (auto& h : H)
            {
                if (h.pid == me) continue;
                bool have = false;
                for (auto& f : M.goff) if (f.key == h.key) have = true;
                if (have) continue;
                size_t at = h.key.find('@');
                if (at == std::string::npos || at == 0) continue;
                std::string cls = h.key.substr(0, at);
                if (!allOf(cls, isW)) continue;
                for (Obj o : E().all(cls.c_str()))
                {
                    if (!VAL(o)) continue;
                    V3 q;
                    if (!E().loc(o, q) || lockId(cls, q.x, q.y, q.z) != h.key) continue;
                    if (cown && busyIs(o, cls, h.key))
                    {
                        LOG(sf("NOLA cown: %s claimed on the ground by pid %d: a container, its interaction belongs to cbusy (left alone here)", h.key.c_str(), h.pid));
                        break;
                    }
                    int n = E().setInteraction(o, false);
                    M.goff.push_back(McOff{o, h.key});
                    LOG(sf("NOLA mc chao: %s na mao do pid %d: %s sem interacao aqui (%d componentes)", h.key.c_str(), h.pid, E().name(o).c_str(), n));
                    break;
                }
            }
        }

        bool tabText(const char* name, std::string& out)
        {
            if (E().readText(name, out)) return true;
            if (!FON("contall") || !E().dataText(name, out)) return false;
            out.erase(std::remove(out.begin(), out.end(), '\r'), out.end());
            return true;
        }
        const std::vector<ContRow>& itCont()
        {
            It& I = S().it;
            if (I.contLoaded) return I.cont;
            I.contLoaded = true;
            std::string text;
            if (tabText("s2u_cont.tsv", text))
                for (auto& l : lines(text))
                {
                    auto f = splitAll(l, '\t');
                    if (f.size() < 6 || !allOf(f[0], isX)) continue;
                    ContRow r;
                    if (!fullNum(f[3], r.x) || !fullNum(f[4], r.y) || !numPrefix(f[5], r.z)) continue;
                    r.sid = f[0];
                    I.cont.push_back(r);
                }
            LOG("NOLA itake: " + std::to_string(I.cont.size()) + " recipientes em s2u_cont.tsv");
            return I.cont;
        }
        std::optional<std::string> contSidXYZ(double x, double y, double z)
        {
            std::optional<std::string> best;
            double bd = sq(F("icontr", 300));
            for (auto& c : itCont())
            {
                double d = sq(c.x - x) + sq(c.y - y) + sq(c.z - z);
                if (d <= bd) { best = c.sid; bd = d; }
            }
            return best;
        }
        std::optional<std::string> contSidOfId(const std::string& id)
        {
            double a, b, c;
            if (!idCoords(id, a, b, c)) return std::nullopt;
            return contSidXYZ(a * 10, b * 10, c * 10);
        }
        bool artTaken(int i, const std::string& who);
        bool itQueue(const std::string& kind, const std::string& a, const std::string& b, const std::string& c, bool hasC)
        {
            It& I = S().it;
            if (kind == "P")
            {
                for (auto& s : nodeBits("S2U_P_", a, (long long)std::floor(numOr(b, 0)))) I.run.push_back(s);
                return true;
            }
            if (kind == "C")
            {
                if (hasC && c == "A")
                {
                    if (FON("ionly1")) { LOG("NOLA itake: pedido 'A' recusado (paks so com nos de 1 unidade; mande a quantidade)"); return false; }
                    I.run.push_back("S2U_C_" + a + "_" + b + "_A");
                    return true;
                }
                long long n = hasC ? (long long)std::floor(numOr(c, 0)) : 0;
                if (n < 1) return false;
                long long mx = FI("icmax", 1000);
                if (n > mx)
                {
                    LOG(sf("NOLA itake: %s x%lld cortado em %lld", b.c_str(), n, mx));
                    n = mx;
                }
                for (long long k = 0; k < n; ++k) I.run.push_back("S2U_C_" + a + "_" + b + "_1");
                return true;
            }
            if (kind == "A")
            {
                auto i = tonum(a);
                if (!i) return false;
                return artTaken((int)*i, "cliente");
            }
            return false;
        }
        bool itemRemoveI(const std::string& kind, const std::string& a, const std::string& b, const std::string& c, bool hasC)
        {
            bool ok = itQueue(kind, a, b, c, hasC);
            LOG(sf("NOLA itake: local %s %s %s %s -> %s (%d nos na fila)", kind.c_str(), a.c_str(), b.c_str(), hasC ? c.c_str() : "nil", bstr(ok).c_str(), (int)S().it.run.size()));
            return ok;
        }
        void rmC(const std::string& sid, const std::string& it, long long n) { itemRemoveI("C", sid, it, lld(n), true); }
        void rmP(const std::string& sid, long long n) { itemRemoveI("P", sid, lld(n), "", false); }
        bool isOn() { return FON("istate"); }
        std::vector<s2x_is::Inst> isRead(Obj o, int where, bool& ok)
        {
            std::vector<s2x_is::Inst> v;
            ok = VAL(o) && E().natInst(o, where, true, v);
            if (!ok) v.clear();
            return v;
        }
        std::vector<s2x_is::IState> isTake(const std::vector<s2x_is::IState>& pool, const std::string& items, const std::vector<s2x_is::IState>& prefer)
        {
            std::vector<s2x_is::IState> r;
            for (auto& it : xItems(items))
            {
                auto c = s2x_is::ofSid(pool, it.sid);
                auto f = s2x_is::common(c, s2x_is::ofSid(prefer, it.sid));
                auto rest = s2x_is::minus(c, f);
                f.insert(f.end(), rest.begin(), rest.end());
                for (long long k = 0; k < it.n && k < (long long)f.size(); ++k) r.push_back(f[(size_t)k]);
            }
            return r;
        }
        std::string isSeg(const std::vector<s2x_is::IState>& v)
        {
            std::string e = s2x_is::encList(v);
            return e.empty() ? std::string() : ";is=" + e;
        }
        std::string isShow(const std::vector<s2x_is::IState>& v)
        {
            std::vector<std::string> r;
            for (auto& s : v) r.push_back("{" + s2x_is::show(s) + "}");
            return join(r, " ");
        }
        void isNet(const std::string& sid, std::vector<s2x_is::IState>& want, std::vector<s2x_is::IState>& gone)
        {
            want.clear();
            gone.clear();
            State& st = S();
            if (!st.tk.loaded) return;
            for (auto& k : st.tk.list)
            {
                std::string s2, items;
                int sg = keyCdelta(k, s2, items);
                if (sg == 0 || s2 != sid) continue;
                for (auto& s : keyStates(k))
                {
                    if (sg > 0) { want.push_back(s); continue; }
                    bool hit = false;
                    for (size_t i = 0; i < want.size() && !hit; ++i)
                        if (s2x_is::same(want[i], s))
                        {
                            want.erase(want.begin() + (long)i);
                            hit = true;
                        }
                    if (!hit) gone.push_back(s);
                }
            }
        }
        long long isPutN(Obj o, int where, const std::string& sid, long long n, std::vector<s2x_is::IState>& sts, std::vector<std::string>& lg)
        {
            long long k = 0;
            for (size_t i = 0; i < sts.size() && k < n;)
            {
                if (sts[i].sid != sid) { ++i; continue; }
                uint32_t uid = 0;
                bool r = E().natPutI(o, where, sts[i], 1, uid);
                lg.push_back("+{" + s2x_is::show(sts[i]) + "}" + (r ? "" : "(erro)"));
                sts.erase(sts.begin() + (long)i);
                if (r) ++k;
            }
            return k;
        }
        long long isRmN(Obj o, int where, const std::string& sid, long long n, const std::vector<s2x_is::IState>& takes, std::vector<s2x_is::Inst>& hv, std::vector<long long>& left, std::vector<std::string>& lg)
        {
            long long k = 0;
            for (auto& t : takes)
            {
                if (k >= n) break;
                if (t.sid != sid) continue;
                int i = s2x_is::pickRm(t, hv, left);
                if (i < 0) break;
                long long dn = 0;
                bool r = E().natRmI(o, where, hv[(size_t)i].uid, 1, dn);
                lg.push_back("-{" + s2x_is::show(hv[(size_t)i].st) + "}" + (r ? "" : "(erro)"));
                if (r) k += dn;
            }
            while (k < n)
            {
                int i = s2x_is::pickSpare(sid, hv, left);
                if (i < 0) break;
                long long m = std::min<long long>(n - k - 1, left[(size_t)i]);
                left[(size_t)i] -= m;
                long long dn = 0;
                bool r = E().natRmI(o, where, hv[(size_t)i].uid, 1 + m, dn);
                lg.push_back("-{" + s2x_is::show(hv[(size_t)i].st) + "}x" + lld(1 + m) + (r ? "" : "(erro)"));
                if (!r || dn <= 0) break;
                k += dn;
            }
            return k;
        }
        void isFix(const std::string& name, Obj o, int where, const std::vector<s2x_is::IState>& want, std::vector<std::string>& lg)
        {
            if (want.empty()) return;
            bool ok = false;
            auto hv = isRead(o, where, ok);
            if (!ok) return;
            s2x_is::Plan p = s2x_is::plan(want, hv);
            for (auto& a : p.apply)
            {
                bool r = E().natApplyI(hv[(size_t)a.first].uid, a.second);
                lg.push_back("={" + s2x_is::show(hv[(size_t)a.first].st) + "}->{" + s2x_is::show(a.second) + "}" + (r ? "" : "(erro)"));
            }
            for (auto& c : p.create) lg.push_back("?{" + s2x_is::show(c) + "}");
            if (!p.create.empty()) LOG(sf("NOLA istate: %s: %d instances with state have no item of that SID here (count path decides)", name.c_str(), (int)p.create.size()));
        }
#include "s2m_items_drop.inc"
        bool itemSendI(const std::string& kind, const std::string& a, const std::string& b, const std::string& c, bool hasC)
        {
            It& I = S().it;
            if (!FON("itake")) return false;
            bool okC = hasC && tokOk(c);
            if (!(tokOk(a) && tokOk(b) && (kind == "P" || ((kind == "C" || kind == "T" || kind == "A") && okC))))
            {
                LOG("NOLA itake: pedido invalido");
                return false;
            }
            I.seq += 1;
            std::string s = lld(I.seq) + "," + kind + "," + a + "," + b + (kind != "P" ? ("," + c) : std::string());
            I.list.push_back(ItOutE{NOW(), s});
            LOG("NOLA itake: envio " + s);
            return true;
        }
        std::string itOutI()
        {
            It& I = S().it;
            double now = NOW();
            std::vector<ItOutE> keep;
            std::vector<std::string> parts;
            for (auto& e : I.list) if (now - e.t < F("resend", 6)) { keep.push_back(e); parts.push_back(e.s); }
            I.list = keep;
            if (!parts.empty()) { I.sent += 1; return "#U:" + join(parts, "|"); }
            return "";
        }
        void itIn()
        {
            if (!FON("itake")) return;
            It& I = S().it;
            double now = NOW();
            if (E().peerLoopback() && !FON("itest")) return;
            if (now - I.pruneAt.value_or(-10) >= 1)
            {
                I.pruneAt = now;
                for (auto it = I.seen.begin(); it != I.seen.end();)
                    if (now - it->second > 5) it = I.seen.erase(it); else ++it;
            }
            if (MC())
            {
                Mc& M = S().mc;
                if (now - M.uPrune.value_or(-10) >= 1)
                {
                    M.uPrune = now;
                    for (auto it = M.useen.begin(); it != M.useen.end();)
                        if (now - it->second > 5) it = M.useen.erase(it); else ++it;
                }
                auto apply = [&](const std::string& key, const std::vector<std::string>& f, size_t o, int pid, const std::string& ev) {
                    bool was = M.useen.count(key) != 0;
                    M.useen[key] = now;
                    if (was) return false;
                    auto at = [&](size_t i) { return o + i < f.size() ? f[o + i] : std::string(); };
                    bool ok = f.size() >= o + 2 && itQueue(at(1), at(2), at(3), at(4), f.size() >= o + 5);
                    if (ok) I.applied += 1;
                    M.uIn += 1;
                    LOG(sf("NOLA itake: recebido do pid %d seq=%s %s -> %s (%d nos na fila)", pid, at(0).c_str(), ev.c_str(), bstr(ok).c_str(), (int)I.run.size()));
                    return true;
                };
                if (HOST())
                {
                    for (int v = 0; v < E().views(); ++v)
                    {
                        std::string us;
                        if (!PARTV("U", us, v)) continue;
                        std::string tok = vTok(v);
                        int pid = E().viewPid(v);
                        for (auto& ev : splitNE(us, '|'))
                        {
                            auto f = splitNE(ev, ',');
                            if (f.empty() || !tonum(f[0])) continue;
                            if (apply(tok + ":" + f[0], f, 0, pid, ev) && !(f.size() >= 2 && f[1] == "A")) M.ur.push_back(McRel{now, tok + "," + ev});
                        }
                    }
                    return;
                }
                std::string ur = mcRelIn("UR");
                int me = E().myPid();
                for (auto& ev : splitNE(ur, '|'))
                {
                    auto f = splitNE(ev, ',');
                    if (f.size() < 2 || !tonum(f[1])) continue;
                    int pid = tokPid(f[0]);
                    if (pid < 0 || pid == me) continue;
                    apply(f[0] + ":" + f[1], f, 1, pid, ev);
                }
            }
            std::string us;
            if (MC()) us = mcRelIn("U");
            else PART("U", us);
            for (auto& ev : splitNE(us, '|'))
            {
                auto f = splitNE(ev, ',');
                auto seq = f.empty() ? std::nullopt : tonum(f[0]);
                if (seq && !I.seen.count(*seq))
                {
                    I.seen[*seq] = now;
                    auto at = [&](size_t i) { return i < f.size() ? f[i] : std::string(); };
                    bool ok = f.size() >= 2 && itQueue(at(1), at(2), at(3), at(4), f.size() >= 5);
                    if (ok) I.applied += 1;
                    LOG(sf("NOLA itake: recebido seq=%lld %s -> %s (%d nos na fila)", (long long)*seq, ev.c_str(), bstr(ok).c_str(), (int)I.run.size()));
                }
                else if (seq) I.seen[*seq] = now;
            }
        }
        void itRun()
        {
            It& I = S().it;
            if (I.run.empty()) return;
            if (!VAL(E().first("CustomConsoleManagerAA"))) return;
            std::set<std::string> used;
            long long n = 0, mx = FI("inodes", 4);
            while (!I.run.empty() && n < mx)
            {
                std::string sid = I.run.front();
                if (used.count(sid) && !FON("isame")) break;
                I.run.pop_front();
                used.insert(sid);
                ++n;
                std::string err;
                if (!E().startNode(sid, err)) LOG("NOLA itake: XStartQuestNodeBySID " + sid + " erro " + err);
            }
            if (I.run.empty()) LOG("NOLA itake: fila vazia");
        }

        std::optional<std::string> partialSid(const std::string& id)
        {
            if (!FON("ptake") || id.empty()) return std::nullopt;
            return contSidOfId(id);
        }
        bool genSid(const std::string& sid) { return sid.size() == 16 && allOf(sid, isX); }
        bool contGenOn() { return FON("contall") && FON("ptake") && FON("lootshare") && FON("lscont"); }
        bool depOn() { return FON("cdeposit") && FON("ptake") && FON("lootshare") && FON("lscont") && FON("taken"); }
        std::optional<std::string> contKey(const std::string& id)
        {
            if (auto t = partialSid(id)) return t;
            if (!contGenOn() || id.empty() || has(id, "PlayerStash") || startsWith(id, "BP_AI_Human_C")) return std::nullopt;
            double x, y, z;
            if (!idCoords(id, x, y, z)) return std::nullopt;
            uint64_t h = 1469598103934665603ULL;
            for (unsigned char c : id) { h ^= c; h *= 1099511628211ULL; }
            return sf("%016llX", (unsigned long long)h);
        }
        bool lsHold(const std::string& k);
        void ptakeTick()
        {
            if (!FON("ptake")) return;
            State& st = S();
            Pt& P = st.pt;
            double now = NOW();
            if (now < P.checkAt) return;
            P.checkAt = now + 1;
            if (!st.tk.loaded) return;
            if (!P.init)
            {
                P.init = true;
                if (HOST()) for (auto& k : st.tk.list) if (startsWith(k, "ctake@")) P.applied.insert(k);
            }
            std::vector<std::string> TL = st.tk.list;
            for (auto& k : TL)
            {
                if (!startsWith(k, "ctake@") || P.applied.count(k) || lsHold(k)) continue;
                P.applied.insert(k);
                std::string sid, items;
                if (keyCtake(k, sid, items))
                {
                    for (auto& it : xItems(items)) rmC(sid, it.sid, it.n);
                    LOG("NOLA ptake: aplicado na minha copia: " + k);
                }
            }
        }

        const std::map<std::string, LsRow>& lsRows()
        {
            Ls& S_ = S().ls;
            if (S_.rowsLoaded) return S_.rows;
            S_.rowsLoaded = true;
            int n = 0;
            std::string text;
            if (tabText("s2u_cont.tsv", text))
                for (auto& l : lines(text))
                {
                    auto f = splitAll(l, '\t');
                    if (f.size() < 6 || !allOf(f[0], isX) || f[1] != "DeadBody") continue;
                    LsRow r;
                    if (!fullNum(f[3], r.x) || !fullNum(f[4], r.y) || !numPrefix(f[5], r.z)) continue;
                    S_.rows[f[0]] = r;
                    ++n;
                }
            LOG("NOLA lootshare: " + std::to_string(n) + " corpos (DeadBody) em s2u_cont.tsv");
            return S_.rows;
        }
        std::optional<std::string> lsBody(const std::string& id)
        {
            if (!startsWith(id, "BP_AI_Human_C@")) return std::nullopt;
            auto sid = contSidOfId(id);
            if (sid && lsRows().count(*sid)) return sid;
            return std::nullopt;
        }
        Counts lsCount(Obj o) { return countAll(o); }
        Ls& lsScan()
        {
            State& st = S();
            Ls& L = st.ls;
            if (!st.tk.loaded) return L;
            if (L.ix > st.tk.list.size()) L.ix = 0;
            while (L.ix < st.tk.list.size())
            {
                L.ix += 1;
                std::string k = st.tk.list[L.ix - 1];
                std::string sid, lst;
                if (keyLsc(k, sid, lst))
                {
                    if (!L.canon.count(sid))
                    {
                        L.canon[sid] = lst;
                        auto m = L.mine.find(sid);
                        if (m != L.mine.end() && m->second == lst) L.done.insert(sid);
                        else
                        {
                            L.done.erase(sid);
                            if (m != L.mine.end()) LOG("NOLA lootshare: corpo " + sid + ": a lista comum veio do outro jogador e difere da minha copia: " + lst);
                        }
                        LOG("NOLA lootshare: lista comum do corpo " + sid + ": " + lst + (L.done.count(sid) ? " (minha copia ja e igual)" : " (minha copia sera igualada)"));
                    }
                }
                else if (HOST())
                {
                    std::string s2, l2;
                    if (keyLsl(k, s2, l2))
                    {
                        if (!L.canon.count(s2))
                        {
                            LOG("NOLA lootshare: o cliente abriu primeiro o corpo " + s2 + ": a lista dele vale para os dois");
                            takenAdd("lsc@" + s2 + ";" + l2);
                        }
                        else LOG("NOLA lootshare: pedido do cliente para o corpo " + s2 + " ignorado (ja tem lista comum)");
                    }
                }
            }
            return L;
        }
        Counts lsTarget(const std::string& sid, std::vector<std::string>& keys)
        {
            State& st = S();
            auto c = st.ls.canon.find(sid);
            Counts t = lsDec(c == st.ls.canon.end() ? std::string() : c->second);
            if (!st.tk.loaded) return t;
            bool dep = depOn();
            for (auto& k : st.tk.list)
            {
                std::string s2, items;
                int sg = keyCdelta(k, s2, items);
                if (sg == 0 || s2 != sid || (sg > 0 && !dep)) continue;
                keys.push_back(k);
                for (auto& it : xItems(items)) t[it.sid] = g0(t, it.sid) + sg * it.n;
            }
            return t;
        }
        bool lsFix(const std::string& sid, Obj o)
        {
            State& st = S();
            Ls& L = st.ls;
            if (!VAL(o)) return false;
            if (!st.it.run.empty()) return false;
            Counts cur = lsCount(o);
            std::vector<std::string> keys;
            Counts tgt = lsTarget(sid, keys);
            std::optional<long long> uid = E().guid(o);
            Obj rk = E().first("CustomConsoleManagerRK");
            std::vector<std::string> add, rm;
            std::set<std::string> all;
            for (auto& kv : tgt) all.insert(kv.first);
            for (auto& kv : cur) all.insert(kv.first);
            for (auto& it : all)
            {
                long long d = std::max<long long>(0, g0(tgt, it)) - g0(cur, it);
                if (qs(it)) {}
                else if (d > 0)
                {
                    if (uid && *uid != 0 && VAL(rk))
                    {
                        bool ok = E().createItem(it, *uid, d);
                        add.push_back(it + "x" + lld(d) + (ok ? "" : "(erro)"));
                    }
                    else add.push_back(it + "x" + lld(d) + "(sem uid, nao posto)");
                }
                else if (d < 0)
                {
                    rmC(sid, it, -d);
                    rm.push_back(it + "x" + lld(-d));
                }
            }
            for (auto& k : keys) st.pt.applied.insert(k);
            L.done.insert(sid);
            std::sort(add.begin(), add.end());
            std::sort(rm.begin(), rm.end());
            LOG(sf("NOLA lootshare: corpo %s igualado a lista comum (%d tiradas contadas): +[%s] -[%s] uid=%s", sid.c_str(), (int)keys.size(), join(add, " ").c_str(), join(rm, " ").c_str(), uidStr(uid).c_str()));
            return true;
        }
        const std::map<std::string, CkRow>& lsCrows();
        bool lsHold(const std::string& k)
        {
            if (!FON("lootshare")) return false;
            std::string sid;
            if (!keyCtakeSid(k, sid)) return false;
            Ls& L = S().ls;
            if (depOn() && !lsRows().count(sid)) return true;
            if (FON("lscont") && lsCrows().count(sid)) return !L.cb.count(sid);
            if (contGenOn() && genSid(sid)) return true;
            return L.canon.count(sid) && !L.done.count(sid);
        }
        bool lsSame(const std::string& sid, Obj o)
        {
            Counts cur = lsCount(o);
            std::vector<std::string> keys;
            Counts tgt = lsTarget(sid, keys);
            std::set<std::string> all;
            for (auto& kv : tgt) all.insert(kv.first);
            for (auto& kv : cur) all.insert(kv.first);
            for (auto& it : all)
                if (!qs(it) && std::max<long long>(0, g0(tgt, it)) != g0(cur, it)) return false;
            return true;
        }
        void lsCopen(const std::string& id, Obj pw);
        void lsOpen(const std::string& id, Obj pw)
        {
            if (!FON("lootshare")) return;
            auto sid = lsBody(id);
            if (!sid) { lsCopen(id, pw); return; }
            Ls& L = lsScan();
            Obj o = interacted(pw);
            bool dep = depOn();
            if (L.canon.count(*sid) || L.mine.count(*sid))
            {
                if (dep)
                {
                    if (!L.crd) L.crd = LsRead{*sid, id, NOW() + F("lsctake", 0.3), true};
                    return;
                }
                if (!L.canon.count(*sid)) return;
                if (L.done.count(*sid) && (!o || lsSame(*sid, o))) return;
                if (L.done.count(*sid))
                {
                    if (L.rc.count(*sid)) return;
                    L.rc.insert(*sid);
                }
                if (!L.crd) L.crd = LsRead{*sid, id, NOW() + F("lsctake", 0.3), true};
                return;
            }
            if (!o) return;
            std::string Ls_ = lsEnc(lsCount(o));
            L.mine[*sid] = Ls_;
            L.done.insert(*sid);
            if (HOST()) takenAdd("lsc@" + *sid + ";" + Ls_);
            else S().tList.push_back(TOut{"lsl@" + *sid + ";c;" + Ls_, NOW() + F("resend", 6)});
            LOG("NOLA lootshare: primeiro a abrir o corpo " + *sid + " (" + ROLE() + "): lista gerada aqui: " + Ls_);
            if (dep && !L.crd) L.crd = LsRead{*sid, id, NOW() + F("lsctake", 0.3), true};
        }
        bool lsNoG(Obj o)
        {
            if (F("cloot", 0) == 1) return true;
            if (!FON("lsnog")) return false;
            V3 q;
            E().loc(o, q);
            Ls& L = S().ls;
            std::string key = sf("%.0f,%.0f,%.0f", q.x / 100, q.y / 100, q.z / 100);
            auto f = L.nog.find(key);
            if (f == L.nog.end())
            {
                auto sid = contSidXYZ(q.x, q.y, q.z);
                bool v = sid && lsRows().count(*sid);
                L.nog[key] = v;
                if (v) LOG("NOLA lootshare: corpo " + *sid + " fora da lista do G (lsnog): o cliente abre a propria copia");
                return v;
            }
            return f->second;
        }
        void lsCread();
        void lsCsync();
        void lsTick()
        {
            if (!FON("lootshare")) return;
            State& st = S();
            Ls& L = st.ls;
            double now = NOW();
            if (L.crd)
            {
                try { lsCread(); }
                catch (const std::exception& e) { L.crd.reset(); LOG(std::string("NOLA lscont erro: ") + e.what()); }
            }
            if (now < L.at) return;
            L.at = now + F("lsper", 1);
            try
            {
                lsScan();
                if (FON("lscont") && !depOn()) lsCsync();
                Obj pw = E().pawn();
                if (!VAL(pw)) return;
                const auto& R = lsRows();
                V3 q;
                E().loc(pw, q);
                std::optional<std::string> bsid;
                if (st.lock.mine) bsid = lsBody(*st.lock.mine);
                std::vector<std::string> want;
                double r2 = sq(F("lsr", 3000));
                for (auto& kv : L.canon)
                {
                    auto r = R.find(kv.first);
                    if (!L.done.count(kv.first) && r != R.end() && (!bsid || kv.first != *bsid) && sq(r->second.x - q.x) + sq(r->second.y - q.y) <= r2) want.push_back(kv.first);
                }
                if (want.empty() || !st.it.run.empty()) return;
                double rc = sq(F("icontr", 300));
                for (Obj o : E().all("BP_AI_Human_C"))
                {
                    if (want.empty()) break;
                    if (!VAL(o) || aliveOr(o, true)) continue;
                    V3 p;
                    if (!E().loc(o, p)) continue;
                    for (size_t i = 0; i < want.size(); ++i)
                    {
                        const LsRow& r = R.at(want[i]);
                        if (sq(r.x - p.x) + sq(r.y - p.y) + sq(r.z - p.z) <= rc)
                        {
                            std::string sid = want[i];
                            want.erase(want.begin() + (long)i);
                            lsFix(sid, o);
                            break;
                        }
                    }
                }
            }
            catch (const std::exception& e)
            {
                L.err += 1;
                if (L.err <= 5) LOG(std::string("NOLA lootshare erro: ") + e.what());
            }
        }
        const std::map<std::string, CkRow>& lsCrows()
        {
            static const std::map<std::string, CkRow> empty;
            Ls& L = S().ls;
            if (L.ckLoaded) return L.ck;
            double rt = F("crowsretry", 30);
            if (rt > 0 && NOW() < L.ckAt) return empty;
            std::map<std::string, CkRow> R;
            int n = 0;
            std::string text;
            if (tabText("s2u_contk.tsv", text))
                for (auto& l : lines(text))
                {
                    auto f = splitAll(l, '\t');
                    if (f.size() < 6 || !allOf(f[0], isX)) continue;
                    CkRow r;
                    if (!fullNum(f[1], r.x) || !fullNum(f[2], r.y) || !fullNum(f[3], r.z)) continue;
                    std::string its = firstTok(f[5]);
                    if (its.empty()) continue;
                    for (auto& it : splitNE(its, ',')) r.set.insert(it);
                    R[f[0]] = r;
                    ++n;
                }
            L.ck = R;
            if (rt <= 0) L.ckLoaded = true;
            else if (n > 0) L.ckLoaded = true;
            else L.ckAt = NOW() + rt;
            LOG("NOLA lscont: " + std::to_string(n) + " recipientes em s2u_contk.tsv" + (n == 0 ? (" (arquivo ausente ou vazio: tento de novo em " + numStr(rt) + " s)") : std::string()));
            return L.ck;
        }
        std::optional<std::string> lsCid(const std::string& id)
        {
            if (startsWith(id, "BP_AI_Human_C") || has(id, "PlayerStash")) return std::nullopt;
            double a, b, c;
            if (!idCoords(id, a, b, c)) return std::nullopt;
            double x = a * 10, y = b * 10, z = c * 10;
            std::optional<std::string> best;
            double bd = sq(F("lscr", 150));
            for (auto& kv : lsCrows())
            {
                double d = sq(kv.second.x - x) + sq(kv.second.y - y) + sq(kv.second.z - z);
                if (d <= bd) { best = kv.first; bd = d; }
            }
            if (!best)
                if (auto k = contKey(id); k && (genSid(*k) || (depOn() && !lsRows().count(*k)))) return k;
            return best;
        }
        void lsCopen(const std::string& id, Obj)
        {
            if (!FON("lscont")) return;
            auto sid = lsCid(id);
            if (!sid) return;
            State& st = S();
            Ls& L = st.ls;
            if (L.crd || (!depOn() && L.cb.count(*sid) && !genSid(*sid))) return;
            if (!st.lock.psid || *st.lock.psid != *sid)
            {
                LOG("NOLA lscont: recipiente " + *sid + " sem ptake (psid=" + (st.lock.psid ? *st.lock.psid : std::string("nil")) + "): fica com a rolagem desta PC");
                return;
            }
            L.crd = LsRead{*sid, id, NOW() + F("lsctake", 0.3), false};
        }
        bool isSid(const std::string& s)
        {
            State& st = S();
            if (st.sidS.empty())
                for (auto& x : sids()) st.sidS.insert(x);
            return st.sidS.count(s) != 0;
        }
        Counts lsLoot(const Counts& c)
        {
            Counts r;
            for (auto& kv : c)
                if (kv.second > 0 && isSid(kv.first) && !qs(kv.first)) r[kv.first] = kv.second;
            return r;
        }
        Counts lsTook(const std::string& sid)
        {
            State& st = S();
            Lock& K = st.lock;
            Counts r;
            if (!K.psid || *K.psid != sid || !K.psnap || K.psnapInf) return r;
            Obj pw = E().pawn();
            if (!VAL(pw)) return r;
            long long m = 0;
            Counts now = invSnapI(pw, m);
            for (auto& kv : now)
            {
                long long d = kv.second - g0(*K.psnap, kv.first);
                if (d > 0) r[kv.first] = d;
            }
            return r;
        }
        Counts lsLost(const std::string& sid)
        {
            State& st = S();
            Lock& K = st.lock;
            Counts r;
            if (!K.psid || *K.psid != sid || !K.psnap || K.psnapInf) return r;
            Obj pw = E().pawn();
            if (!VAL(pw)) return r;
            long long m = 0;
            Counts now = invSnapI(pw, m);
            for (auto& kv : *K.psnap)
            {
                long long d = kv.second - g0(now, kv.first);
                if (d > 0) r[kv.first] = d;
            }
            return r;
        }
        Obj lootWin()
        {
            Obj w = S().lock.widget;
            if (!VAL(w)) w = E().first("InventoryNew");
            return w;
        }
        bool lsEqWin(const char* what, const std::string& name, Obj w, const Counts& cur, Counts t, const std::vector<std::string>& keys)
        {
            for (auto& kv : lsTook(name)) t[kv.first] = g0(t, kv.first) - kv.second;
            bool dep = depOn();
            Lock& K = S().lock;
            if (dep)
            {
                K.ceq.clear();
                K.cpre.clear();
                for (auto& kv : lsLost(name))
                {
                    if (qs(kv.first) || !isSid(kv.first)) continue;
                    long long tt = std::max<long long>(0, g0(t, kv.first));
                    long long a = std::min<long long>(kv.second, std::max<long long>(0, g0(cur, kv.first) - tt));
                    if (a <= 0) continue;
                    t[kv.first] = tt + a;
                    K.cpre[kv.first] = a;
                }
            }
            std::set<std::string> all;
            for (auto& kv : t) all.insert(kv.first);
            for (auto& kv : cur) all.insert(kv.first);
            std::vector<std::string> add, rm, ilg;
            bool ok = true;
            bool iso = dep && isOn();
            std::vector<s2x_is::IState> iwant, igone;
            std::vector<s2x_is::Inst> hv;
            s2x_is::Plan ip;
            bool hok = false;
            if (iso)
            {
                isNet(name, iwant, igone);
                if (!iwant.empty() || !igone.empty()) hv = isRead(w, 1, hok);
                if (hok) ip = s2x_is::plan(iwant, hv);
            }
            for (auto& it : all)
            {
                if (qs(it) || !isSid(it)) continue;
                long long d = std::max<long long>(0, g0(t, it)) - g0(cur, it);
                if (d == 0) continue;
                long long done = 0, k = 0;
                if (hok && d > 0) k = isPutN(w, 1, it, d, ip.create, ilg);
                if (hok && d < 0) k = -isRmN(w, 1, it, -d, igone, hv, ip.left, ilg);
                bool r = true;
                if (d - k != 0) r = E().natPut(w, it, d - k, done);
                done += k;
                if (!r) ok = false;
                else if (dep) K.ceq[it] += d > 0 ? std::llabs(done) : -std::llabs(done);
                (d > 0 ? add : rm).push_back(it + "x" + lld(d > 0 ? d : -d) + (r ? "" : "(erro)"));
            }
            if (hok) isFix(name, w, 1, iwant, ilg);
            if (!ilg.empty())
            {
                LOG(sf("NOLA istate: %s: items with state set in the open window: [%s]", name.c_str(), join(ilg, " ").c_str()));
                if (add.empty() && rm.empty()) E().natRefresh(w);
            }
            if (dep && !K.cpre.empty())
            {
                std::vector<std::string> pre;
                for (auto& kv : K.cpre) pre.push_back(kv.first + "x" + lld(kv.second));
                LOG(sf("NOLA cdeposit: %s: put in before the read, kept in the window: [%s]", name.c_str(), join(pre, " ").c_str()));
            }
            if (!add.empty() || !rm.empty()) E().natRefresh(w);
            for (auto& k : keys) S().pt.applied.insert(k);
            std::sort(add.begin(), add.end());
            std::sort(rm.begin(), rm.end());
            LOG(sf("NOLA %s: %s igualado a lista comum na janela aberta (%d tiradas contadas; nada tirado do jogador, janela aberta): +[%s] -[%s]", what, name.c_str(), (int)keys.size(), join(add, " ").c_str(),
                   join(rm, " ").c_str()));
            return ok;
        }
        Counts lsCtgt(const std::string& sid, const std::string& list, std::vector<std::string>& keys)
        {
            State& st = S();
            Counts t = lsDec(list);
            if (!st.tk.loaded) return t;
            bool dep = depOn();
            for (auto& k : st.tk.list)
            {
                std::string s2, items;
                int sg = keyCdelta(k, s2, items);
                if (sg == 0 || s2 != sid || (sg > 0 && !dep)) continue;
                keys.push_back(k);
                for (auto& it : xItems(items)) t[it.sid] = g0(t, it.sid) + sg * it.n;
            }
            return t;
        }
        bool lsFix(const std::string& sid, Obj o);
        void lsGenNew(const std::string& sid, Obj w, const Counts& cur)
        {
            State& st = S();
            Counts want;
            std::vector<std::string> keys;
            std::vector<s2x_is::IState> its;
            if (st.tk.loaded)
                for (auto& k : st.tk.list)
                {
                    std::string s2, items;
                    if (!keyCtake(k, s2, items) || s2 != sid || st.pt.applied.count(k)) continue;
                    keys.push_back(k);
                    for (auto& it : xItems(items)) want[it.sid] += it.n;
                    for (auto& s : keyStates(k)) its.push_back(s);
                }
            std::vector<std::string> rm, ilg;
            bool hok = false;
            std::vector<s2x_is::Inst> hv;
            std::vector<long long> left;
            if (isOn() && !its.empty())
            {
                hv = isRead(w, 1, hok);
                for (auto& h : hv) left.push_back(h.n);
            }
            for (auto& kv : want)
            {
                if (qs(kv.first) || !isSid(kv.first)) continue;
                long long n = std::min<long long>(kv.second, g0(cur, kv.first));
                if (n <= 0) continue;
                long long done = 0, k = 0;
                if (hok) k = isRmN(w, 1, kv.first, n, its, hv, left, ilg);
                bool r = true;
                if (n - k > 0) r = E().natPut(w, kv.first, -(n - k), done);
                rm.push_back(kv.first + "x" + lld(n) + (r ? "" : "(erro)"));
            }
            if (!ilg.empty()) LOG(sf("NOLA istate: %s: taken by the other player, exact instances removed: [%s]", sid.c_str(), join(ilg, " ").c_str()));
            if (!rm.empty()) E().natRefresh(w);
            for (auto& k : keys) st.pt.applied.insert(k);
            std::sort(rm.begin(), rm.end());
            LOG(sf("NOLA lscont: recipiente %s aberto de novo: %d tiradas novas do outro jogador tiradas da minha copia na janela aberta (janela aberta): -[%s]", sid.c_str(), (int)keys.size(), join(rm, " ").c_str()));
        }
        bool lsPend(const std::string& sid)
        {
            std::string a = "S2U_C_" + sid + "_", b = "S2U_K_" + sid + "_";
            for (auto& n : S().it.run)
                if (startsWith(n, a.c_str()) || startsWith(n, b.c_str())) return true;
            return false;
        }
        void lsMark(const std::string& sid, const Counts& cur)
        {
            State& st = S();
            Lock& K = st.lock;
            if (!K.psid || *K.psid != sid) return;
            K.cfirst = cur;
            Counts c = cur;
            for (auto& kv : K.ceq) c[kv.first] = g0(c, kv.first) + kv.second;
            K.clast = c;
            K.cix = st.tk.loaded ? st.tk.list.size() : 0;
            Obj pw = E().pawn();
            Counts p;
            if (VAL(pw) && E().natItems(pw, 0, true, p)) K.plast = p;
            else K.plast.reset();
        }
        void lsCreadDep(const LsRead& R, Obj w, const Counts& cur)
        {
            State& st = S();
            Ls& L = st.ls;
            if (!L.canon.count(R.sid) && !L.mine.count(R.sid))
            {
                Counts loot = lsLoot(cur);
                Counts full = loot;
                for (auto& kv : lsTook(R.sid))
                    if (isSid(kv.first) && !qs(kv.first)) full[kv.first] += kv.second;
                for (auto& kv : lsLost(R.sid))
                {
                    long long a = std::min<long long>(kv.second, g0(loot, kv.first));
                    if (a > 0) full[kv.first] -= a;
                }
                int mk = 0;
                if (st.tk.loaded)
                    for (auto& k : st.tk.list)
                    {
                        std::string s2, items;
                        int sg = keyCdelta(k, s2, items);
                        if (sg == 0 || s2 != R.sid || (!st.pt.applied.count(k) && !st.pt.base.count(k))) continue;
                        ++mk;
                        for (auto& it : xItems(items))
                            if (isSid(it.sid) && !qs(it.sid)) full[it.sid] = g0(full, it.sid) - sg * it.n;
                    }
                std::string Lst = lsEnc(full);
                if (Lst == "-")
                {
                    L.emp.insert(R.sid);
                    LOG("NOLA lscont: recipiente " + R.sid + ": vazio na leitura: sem lista (cdeposit: so as chaves contam)");
                }
                else
                {
                    L.mine[R.sid] = Lst;
                    if (HOST()) { takenAdd("lsc@" + R.sid + ";" + Lst); lsScan(); }
                    else st.tList.push_back(TOut{"lsl@" + R.sid + ";c;" + Lst, NOW() + F("resend", 6)});
                    LOG("NOLA lscont: primeiro a abrir o recipiente " + R.sid + " (" + ROLE() + "): lista lida sem tirar nada: " + Lst + (mk ? sf(" (cdeposit: %d chaves antigas somadas de volta)", mk) : std::string()));
                }
            }
            auto c = L.canon.find(R.sid);
            auto m = L.mine.find(R.sid);
            bool has = c != L.canon.end() || m != L.mine.end();
            std::string tl = c != L.canon.end() ? c->second : m != L.mine.end() ? m->second : std::string("-");
            std::vector<std::string> keys;
            Counts t = lsCtgt(R.sid, tl, keys);
            lsEqWin("lscont", R.sid, w, cur, t, keys);
            if (has) L.cb[R.sid] = tl;
            lsMark(R.sid, cur);
        }
        void lsCread()
        {
            State& st = S();
            Ls& L = st.ls;
            if (!L.crd) return;
            LsRead R = *L.crd;
            Lock& K = st.lock;
            if (!K.mine || *K.mine != R.id || !VAL(E().pawn()))
            {
                L.crd.reset();
                LOG("NOLA lscont: janela de " + R.sid + " fechada antes da leitura (abra de novo)");
                return;
            }
            if (NOW() < R.ta) return;
            bool dep = depOn();
            if (dep && lsPend(R.sid))
            {
                L.crd->ta = NOW() + F("lsctake", 0.3);
                return;
            }
            L.crd.reset();
            Obj w = lootWin();
            Counts cur;
            bool nat = VAL(w) && E().natItems(w, 1, false, cur);
            if (R.body)
            {
                std::vector<std::string> keys;
                Counts tgt;
                if (dep)
                {
                    auto c = L.canon.find(R.sid);
                    auto m = L.mine.find(R.sid);
                    tgt = lsCtgt(R.sid, c != L.canon.end() ? c->second : m != L.mine.end() ? m->second : std::string("-"), keys);
                }
                else tgt = lsTarget(R.sid, keys);
                if (nat)
                {
                    lsEqWin("lootshare", R.sid, w, cur, tgt, keys);
                    if (dep) lsMark(R.sid, cur);
                }
                else
                {
                    bool r = lsFix(R.sid, interacted(E().pawn()));
                    LOG("NOLA lootshare: corpo " + R.sid + " diferente da lista comum e leitura nativa indisponivel: igualado por nos com a janela aberta (" + bstr(r) + ")");
                }
                L.done.insert(R.sid);
                return;
            }
            if (!nat)
            {
                LOG("NOLA lscont: leitura nativa do recipiente " + R.sid + " indisponivel: fica com a rolagem desta PC (nada tirado, janela aberta)");
                return;
            }
            lsScan();
            if (dep)
            {
                lsCreadDep(R, w, cur);
                return;
            }
            if (L.cb.count(R.sid))
            {
                auto c = L.canon.find(R.sid);
                if (c != L.canon.end() && c->second != L.cb[R.sid])
                {
                    std::vector<std::string> keys;
                    Counts t = lsCtgt(R.sid, c->second, keys);
                    lsEqWin("lscont", R.sid, w, cur, t, keys);
                    L.cb[R.sid] = c->second;
                }
                else lsGenNew(R.sid, w, cur);
                return;
            }
            if (!L.canon.count(R.sid) && !L.mine.count(R.sid))
            {
                Counts full = lsLoot(cur);
                for (auto& kv : lsTook(R.sid))
                    if (isSid(kv.first) && !qs(kv.first)) full[kv.first] += kv.second;
                std::string Lst = lsEnc(full);
                if (Lst == "-")
                {
                    LOG("NOLA lscont: recipiente " + R.sid + ": vazio na leitura: sem lista");
                    return;
                }
                L.mine[R.sid] = Lst;
                if (HOST()) { takenAdd("lsc@" + R.sid + ";" + Lst); lsScan(); }
                else st.tList.push_back(TOut{"lsl@" + R.sid + ";c;" + Lst, NOW() + F("resend", 6)});
                LOG("NOLA lscont: primeiro a abrir o recipiente " + R.sid + " (" + ROLE() + "): lista lida sem tirar nada: " + Lst);
            }
            auto c = L.canon.find(R.sid);
            std::string tl = c != L.canon.end() ? c->second : L.mine[R.sid];
            std::vector<std::string> keys;
            Counts t = lsCtgt(R.sid, tl, keys);
            lsEqWin("lscont", R.sid, w, cur, t, keys);
            L.cb[R.sid] = tl;
        }
        bool lsCfill(const std::string& sid, const std::string& newL, const std::optional<std::string>& oldL)
        {
            State& st = S();
            Ls& L = st.ls;
            const auto& rows = lsCrows();
            auto row = rows.find(sid);
            if (row == rows.end()) return false;
            Counts t = lsDec(newL);
            std::vector<std::string> keys;
            if (oldL) { for (auto& kv : lsDec(*oldL)) t[kv.first] = g0(t, kv.first) - kv.second; }
            else if (st.tk.loaded)
            {
                for (auto& k : st.tk.list)
                {
                    std::string s2, items;
                    if (keyCtake(k, s2, items) && s2 == sid)
                    {
                        keys.push_back(k);
                        for (auto& it : xItems(items)) t[it.sid] = g0(t, it.sid) - it.n;
                    }
                }
            }
            std::vector<std::string> add, rm;
            for (auto& kv : t)
            {
                const std::string& it = kv.first;
                long long d = kv.second;
                if (qs(it)) {}
                else if (d > 0)
                {
                    if (row->second.set.count(it))
                    {
                        long long k = std::min<long long>(d, FI("icmax", 1000));
                        for (long long j = 0; j < k; ++j) st.it.run.push_back("S2U_K_" + sid + "_" + it + "_1");
                        add.push_back(it + "x" + lld(d));
                    }
                    else add.push_back(it + "x" + lld(d) + "(sem no K)");
                }
                else if (d < 0 && oldL)
                {
                    rmC(sid, it, -d);
                    rm.push_back(it + "x" + lld(-d));
                }
            }
            for (auto& k : keys) st.pt.applied.insert(k);
            L.cb[sid] = newL;
            std::sort(add.begin(), add.end());
            std::sort(rm.begin(), rm.end());
            LOG(sf("NOLA lscont: recipiente %s %s (%d tiradas contadas): +[%s] -[%s]", sid.c_str(), oldL ? "corrigido para a lista do host" : "enchido com a lista comum", (int)keys.size(), join(add, " ").c_str(), join(rm, " ").c_str()));
            return true;
        }
        void lsCsync()
        {
            Ls& L = S().ls;
            auto cb = L.cb;
            for (auto& kv : cb)
            {
                auto c = L.canon.find(kv.first);
                if (c != L.canon.end() && c->second != kv.second) lsCfill(kv.first, c->second, kv.second);
            }
        }

        bool clIs(Obj pw);
        std::string invDeltaK(const Lock& K, const Counts& after)
        {
            if (K.psnapInf) return "";
            return invDelta(*K.psnap, after);
        }
        bool busyOn() { return FON("cbusy") && FON("locks"); }
        int busyKind(Obj a, const std::string& cls)
        {
            Mc& M = S().mc;
            auto f = M.kcls.find(cls);
            if (f != M.kcls.end()) return f->second;
            int k = 0;
            if (!E().comps(a, "/Script/Stalker2.ItemContainerInteractionComponent").empty() || !E().comps(a, "/Script/Stalker2.ItemContainerHoldComponent").empty()) k = 1;
            else if (!E().comps(a, "/Script/Stalker2.DeadBody").empty()) k = 2;
            if (M.kcls.size() > 512) M.kcls.clear();
            M.kcls[cls] = k;
            LOG(sf("NOLA cbusy: class %s is %s", cls.c_str(), k == 1 ? "a container" : k == 2 ? "a body" : "not a container"));
            return k;
        }
        bool busyIs(Obj a, const std::string& cls, const std::string& id)
        {
            if (!VAL(a) || cls.empty() || !allOf(cls, isW) || has(cls, "PlayerStash") || E().isOwnBody(a)) return false;
            int k = busyKind(a, cls);
            if (k == 1) return true;
            if (k != 2) return false;
            bool al = true;
            if (!E().alive(a, al) || al) return false;
            return !(F("cloot", 0) == 1 && b164on() && !lsBody(id));
        }
        void busyClaimTick()
        {
            Mc& M = S().mc;
            M.kclaim.reset();
            if (!busyOn()) return;
            Obj pw = E().pawn();
            if (!VAL(pw)) return;
            if (S().lock.mine)
            {
                M.kclaim = S().lock.mine;
                return;
            }
            Obj a = interacted(pw);
            V3 q;
            if (!a || !E().loc(a, q) || std::fabs(q.x) + std::fabs(q.y) < 1000) return;
            std::string cls = firstTok(E().full(a));
            std::string id = lockId(cls, q.x, q.y, q.z);
            if (busyIs(a, cls, id)) M.kclaim = id;
        }
        void busyRestore(McOff& f, const char* why)
        {
            if (f.n > 0 && VAL(f.o)) E().setInteraction(f.o, true);
            LOG(sf("NOLA cbusy: %s free again (%s): interaction back on %d components", f.key.c_str(), why, VAL(f.o) ? f.n : 0));
        }
        void busyClear(const char* why)
        {
            Mc& M = S().mc;
            for (auto& f : M.koff) busyRestore(f, why);
            M.koff.clear();
            M.kbRaw.clear();
            M.kexp.clear();
        }
        void busyApplyTick()
        {
            Mc& M = S().mc;
            if (!busyOn())
            {
                if (!M.koff.empty() || !M.kexp.empty()) busyClear("cbusy=0");
                return;
            }
            std::vector<McHold> H;
            if (MC()) H = mcHolds("KH", M.kh);
            else
            {
                std::string r;
                if (PART("K", r) && !r.empty()) H.push_back(McHold{-1, r, 0});
            }
            int me = MC() ? E().myPid() : -2;
            double now = NOW();
            bool keyfix = FON("keyfix");
            long long tol = FI("keytol", 1);
            for (auto it = M.koff.begin(); it != M.koff.end();)
            {
                if (!VAL(it->o))
                {
                    LOG("NOLA cbusy: " + it->key + " actor gone here (load, travel or streaming): forgotten");
                    it = M.koff.erase(it);
                    continue;
                }
                bool still = false;
                for (auto& h : H) if (h.pid != me && keyClose(h.key, it->key, keyfix, tol)) still = true;
                if (still) { ++it; continue; }
                busyRestore(*it, "released by the host");
                it = M.koff.erase(it);
            }
            std::string raw;
            for (auto& h : H) if (h.pid != me) raw += sf("%d:", h.pid) + h.key + "|";
            if (raw == M.kbRaw && now - M.kbAt < 2) return;
            M.kbRaw = raw;
            M.kbAt = now;
            for (auto& h : H)
            {
                if (h.pid == me) continue;
                bool have = false;
                for (auto& f : M.koff) if (keyClose(f.key, h.key, keyfix, tol)) have = true;
                if (have) continue;
                size_t at = h.key.find('@');
                if (at == std::string::npos || at == 0) continue;
                std::string cls = h.key.substr(0, at);
                if (!allOf(cls, isW)) continue;
                for (Obj o : E().all(cls.c_str()))
                {
                    if (!VAL(o)) continue;
                    V3 q;
                    if (!E().loc(o, q)) continue;
                    std::string id = lockId(cls, q.x, q.y, q.z);
                    if (!keyClose(id, h.key, keyfix, tol) || !busyIs(o, cls, id)) continue;
                    int n = E().setInteraction(o, false);
                    M.koff.push_back(McOff{o, h.key, n, h.pid});
                    LOG(sf("NOLA cbusy: %s in use by pid %d: %s interaction off here (%d components)", h.key.c_str(), h.pid, E().name(o).c_str(), n));
                    break;
                }
            }
        }
        void mcKTick()
        {
            if (!FON("locks") || !HOST()) return;
            Mc& M = S().mc;
            bool busy = busyOn();
            std::vector<std::pair<int, std::string>> claims;
            std::optional<std::string> own = busy ? M.kclaim : S().lock.mine;
            if (own) claims.push_back({0, *own});
            std::string k;
            for (int v = 0; v < E().views(); ++v)
                if (PARTV("K", k, v) && !k.empty()) claims.push_back({E().viewPid(v), k});
            if (busy)
            {
                for (auto it = M.kexp.begin(); it != M.kexp.end();)
                {
                    bool live = false;
                    for (auto& c : claims) if (sf("%d|", c.first) + c.second == *it) live = true;
                    if (live) ++it;
                    else it = M.kexp.erase(it);
                }
                claims.erase(std::remove_if(claims.begin(), claims.end(), [&](const auto& c) { return M.kexp.count(sf("%d|", c.first) + c.second) != 0; }), claims.end());
            }
            mcArb(M.kh, claims, !FON("lockfirst") && !busy, "bau");
            if (!busy) return;
            double now = NOW(), mx = F("cbusymax", 600);
            for (auto it = M.kh.begin(); it != M.kh.end();)
            {
                if (now - it->since <= mx) { ++it; continue; }
                for (auto& c : claims)
                    if (c.first == it->pid && keyClose(c.second, it->key, FON("keyfix"), FI("keytol", 1))) M.kexp.insert(sf("%d|", c.first) + c.second);
                LOG(sf("NOLA cbusy: %s held by pid %d for %.0f s (over cbusymax): the host frees it", it->key.c_str(), it->pid, now - it->since));
                it = M.kh.erase(it);
            }
        }
        void depReset(Lock& K)
        {
            K.cfirst.reset();
            K.clast.reset();
            K.plast.reset();
            K.ceq.clear();
            K.cpre.clear();
            K.cix = 0;
            K.pinst.reset();
            K.winst.clear();
        }
        void depSample()
        {
            State& st = S();
            Lock& K = st.lock;
            if (!depOn() || !K.psid || !K.cfirst) return;
            Obj w = lootWin();
            if (!VAL(w)) return;
            if (st.tk.loaded && K.cix < st.tk.list.size())
            {
                std::vector<std::string> ch;
                bool any = false;
                for (size_t i = K.cix; i < st.tk.list.size(); ++i)
                {
                    std::string k = st.tk.list[i];
                    std::string s2, items;
                    int sg = keyCdelta(k, s2, items);
                    if (sg == 0 || s2 != *K.psid || st.pt.applied.count(k)) continue;
                    st.pt.applied.insert(k);
                    std::vector<s2x_is::IState> its = isOn() ? keyStates(k) : std::vector<s2x_is::IState>();
                    std::vector<std::string> ilg;
                    for (auto& it : xItems(items))
                    {
                        if (qs(it.sid) || !isSid(it.sid)) continue;
                        long long done = 0, kk = 0;
                        if (!s2x_is::ofSid(its, it.sid).empty())
                        {
                            if (sg > 0) kk = isPutN(w, 1, it.sid, it.n, its, ilg);
                            else
                            {
                                bool hok = false;
                                auto hv = isRead(w, 1, hok);
                                std::vector<long long> left;
                                for (auto& h : hv) left.push_back(h.n);
                                if (hok) kk = isRmN(w, 1, it.sid, it.n, its, hv, left, ilg);
                            }
                        }
                        bool r = true;
                        if (it.n - kk > 0) r = E().natPut(w, it.sid, sg * (it.n - kk), done);
                        done = done + sg * kk;
                        if (!ilg.empty())
                        {
                            LOG(sf("NOLA istate: %s: key %s applied with state: [%s]", K.psid->c_str(), k.substr(0, 24).c_str(), join(ilg, " ").c_str()));
                            ilg.clear();
                        }
                        if (r && done != 0)
                        {
                            K.ceq[it.sid] += sg > 0 ? std::llabs(done) : -std::llabs(done);
                            any = true;
                        }
                        ch.push_back((sg > 0 ? "+" : "-") + it.sid + "x" + lld(it.n) + (r ? "" : "(erro)"));
                    }
                }
                K.cix = st.tk.list.size();
                if (any) E().natRefresh(w);
                if (!ch.empty()) LOG(sf("NOLA cdeposit: %s: chaves novas do outro jogador aplicadas na janela aberta: [%s]", K.psid->c_str(), join(ch, " ").c_str()));
            }
            Counts c;
            if (E().natItems(w, 1, false, c)) K.clast = c;
            Obj pw = E().pawn();
            Counts p;
            if (VAL(pw) && E().natItems(pw, 0, true, p)) K.plast = p;
            if (isOn() && K.pinst)
            {
                bool ok = false;
                auto v = isRead(w, 1, ok);
                if (ok) K.winst = v;
            }
        }
        std::string depItems(const Lock& K, const Counts& after)
        {
            if (K.psnapInf || !K.psnap || !K.cfirst) return "";
            Counts pc;
            bool hasPc = false;
            Obj pw = E().pawn();
            if (K.plast && VAL(pw)) hasPc = E().natItems(pw, 0, true, pc);
            std::vector<std::string> r;
            for (auto& kv : *K.psnap)
            {
                const std::string& it = kv.first;
                long long lost = kv.second - g0(after, it);
                if (lost <= 0 || qs(it) || !isSid(it)) continue;
                long long base = g0(*K.cfirst, it) + g0(K.ceq, it) - g0(K.cpre, it);
                long long wd = K.clast ? std::max<long long>(0, g0(*K.clast, it) - base) : 0;
                long long pt = hasPc ? std::max<long long>(0, g0(*K.plast, it) - g0(pc, it)) : 0;
                long long d = std::min<long long>(lost, wd + pt);
                if (d > 0) r.push_back(it + "x" + lld(d));
            }
            std::sort(r.begin(), r.end());
            return join(r, ",");
        }
        void lockTick()
        {
            if (!FON("locks")) return;
            State& st = S();
            Lock& K = st.lock;
            st.lockOn = true;
            Obj pw = E().pawn();
            if (!VAL(pw)) return;
            Obj w = K.widget;
            if (!VAL(w)) { w = E().first("InventoryNew"); K.widget = w; }
            bool open = false;
            if (VAL(w)) { bool v = false; if (E().visible(w, v)) open = v; }
            std::optional<std::string> id;
            Obj a = 0;
            if (open)
            {
                a = interacted(pw);
                V3 q;
                if (a && E().loc(a, q))
                {
                    std::string c = firstTok(E().full(a));
                    id = lockId(c.empty() ? std::string("?") : c, q.x, q.y, q.z);
                }
            }
            if (id && clIs(pw)) return;
            if (id && F("cloot", 0) == 1 && b164on() && !lsBody(*id))
            {
                bool al = true;
                Obj ia = interacted(pw);
                if (ia && E().alive(ia, al) && !al) return;
            }
            std::string remote;
            bool keyfix = FON("keyfix");
            long long tol = FI("keytol", 1);
            bool held = false;
            if (MC())
            {
                Mc& M = st.mc;
                std::vector<McHold> H;
                if (HOST()) H = M.kh;
                else
                {
                    PART("KH", remote);
                    H = mcHoldIn(remote);
                }
                auto who = id ? mcHolder(H, *id) : std::nullopt;
                held = (who && *who != E().myPid()) || (FON("locktest") && id);
            }
            else
            {
                PART("K", remote);
                held = (!remote.empty() && id && keyClose(remote, *id, keyfix, tol)) || (FON("locktest") && id);
            }
            bool tkn = FON("taken") && id && st.tk.loaded && keyGet(st.tk.set, *id, keyfix, tol)
                       && !(FON("takenown") && mineSet().count(*id)) && !contKey(*id) && !has(*id, "PlayerStash")
                       && !(FI("qitem", 0) == 1 && E().qiHostKeep(*id, pw));
            if (tkn && *id != K.mine.value_or("\x01") && !K.denied)
            {
                K.denied = id;
                bool ok = E().closeLoot(pw);
                NOLA_CEN_EV("lootclose", "taken_deny", *id);
                LOG("NOLA TAKEN_DENY " + *id + " (ja levado) janela fechada ok=" + bstr(ok));
            }
            else if (id && (!K.mine || *id != *K.mine) && !K.denied)
            {
                if (held && (FON("locktest") || !HOST() || FON("lockfirst")))
                {
                    K.denied = id;
                    K.denies += 1;
                    bool ok = E().closeLoot(pw);
                    NOLA_CEN_EV("lootclose", "lock_deny", *id);
                    LOG("NOLA LOCK_DENY " + *id + " (em uso pelo outro jogador) janela fechada ok=" + bstr(ok) + " " + (ok ? "" : "erro"));
                }
                else
                {
                    K.mine = id;
                    K.grants += 1;
                    LOG("NOLA LOCK_REQ " + *id);
                    LOG("NOLA LOCK_GRANT " + *id + (held ? " (conflito: host fica)" : ""));
                    K.psid = contKey(*id);
                    K.psnap.reset();
                    K.psnapInf = false;
                    depReset(K);
                    if (K.psid)
                    {
                        long long m;
                        K.psnap = invSnapI(pw, m);
                        if (isOn())
                        {
                            bool ok = false;
                            auto v = isRead(pw, 0, ok);
                            if (ok) K.pinst = v;
                        }
                        LOG("NOLA ptake: recipiente " + *K.psid + " aberto; inventario anotado");
                    }
                    try { lsOpen(*id, pw); }
                    catch (const std::exception&) {}
                }
            }
            else if (K.mine && id && *id == *K.mine && held && !HOST() && !FON("locktest"))
            {
                E().closeLoot(pw);
                NOLA_CEN_EV("lootclose", "lock_conflict", *K.mine);
                if (MC())
                {
                    LOG("NOLA LOCK_DENY " + *K.mine + " (aberto ao mesmo tempo; o host deu a outro jogador) janela fechada");
                    K.denied = K.mine;
                    K.denies += 1;
                }
                else LOG("NOLA LOCK_DENY " + *K.mine + " (conflito: o host fica) janela fechada");
                K.mine.reset();
            }
            if (open && id && K.mine && *id == *K.mine) depSample();
            if (!open)
            {
                if (K.mine)
                {
                    std::string mine = *K.mine;
                    LOG("NOLA LOCK_REL " + mine);
                    if (K.psid && K.psnap)
                    {
                        long long m;
                        Counts after = invSnapI(pw, m);
                        std::string items = invDeltaK(K, after);
                        std::vector<s2x_is::IState> igain, ilost;
                        if (isOn() && K.pinst)
                        {
                            bool ok = false;
                            auto pn = isRead(pw, 0, ok);
                            if (ok)
                            {
                                auto a = s2x_is::expand(*K.pinst, true), b = s2x_is::expand(pn, true);
                                igain = s2x_is::minus(b, a);
                                ilost = s2x_is::minus(a, b);
                            }
                        }
                        if (!items.empty())
                        {
                            std::vector<s2x_is::IState> ts = isTake(igain, items, {});
                            std::string key = sf("ctake@%s;%c%d;%s", K.psid->c_str(), ROLE()[0], E().random(1, 999999), items.c_str()) + isSeg(ts);
                            if (!ts.empty()) LOG("NOLA istate: taken from " + *K.psid + " with state: " + isShow(ts));
                            st.pt.applied.insert(key);
                            if (FON("taken"))
                            {
                                if (HOST()) takenAdd(key);
                                else { st.tOut = TOut{key, NOW() + 3}; takenAdd(key); }
                            }
                            LOG("NOLA ptake: tirado do recipiente " + *K.psid + ": " + items);
                        }
                        else LOG("NOLA ptake: recipiente " + *K.psid + " fechado sem tirar nada");
                        std::string put = depOn() ? depItems(K, after) : std::string();
                        if (!put.empty())
                        {
                            Ls& L = st.ls;
                            if (L.emp.count(*K.psid) && !L.canon.count(*K.psid) && !L.mine.count(*K.psid))
                            {
                                L.mine[*K.psid] = "-";
                                if (HOST()) { takenAdd("lsc@" + *K.psid + ";-"); lsScan(); }
                                else st.tList.push_back(TOut{"lsl@" + *K.psid + ";c;-", NOW() + F("resend", 6)});
                                LOG("NOLA cdeposit: recipiente " + *K.psid + " vazio na leitura: lista comum vazia registrada antes do primeiro deposito");
                            }
                            std::vector<s2x_is::IState> ps = isTake(ilost, put, s2x_is::expand(K.winst, true));
                            std::string key = sf("cput@%s;%c%d;%s", K.psid->c_str(), ROLE()[0], E().random(1, 999999), put.c_str()) + isSeg(ps);
                            if (!ps.empty()) LOG("NOLA istate: put in " + *K.psid + " with state: " + isShow(ps));
                            st.pt.applied.insert(key);
                            if (HOST()) takenAdd(key);
                            else { st.tList.push_back(TOut{key, NOW() + F("resend", 6)}); takenAdd(key); }
                            LOG("NOLA cdeposit: posto no recipiente " + *K.psid + ": " + put);
                        }
                        K.psid.reset();
                        K.psnap.reset();
                        K.psnapInf = false;
                        depReset(K);
                    }
                    else if (FON("taken") && !has(mine, "PlayerStash"))
                    {
                        if (FON("takenown")) takenMineAdd(mine);
                        if (HOST()) takenAdd(mine);
                        else { st.tOut = TOut{mine, NOW() + 3}; takenAdd(mine); }
                    }
                    K.mine.reset();
                }
                K.denied.reset();
            }
        }

        const Counts& boxLoad()
        {
            Ss& Sx = S().ss;
            if (Sx.boxLoaded) return Sx.box;
            Sx.boxLoaded = true;
            std::string text;
            if (E().readText("ss_box.txt", text))
                for (auto& l : lines(text))
                {
                    auto f = splitAll(l, '\t');
                    if (f.size() != 2 || !allOf(f[0], isW) || !allOf(f[1], isD)) continue;
                    Sx.box[f[0]] = (long long)numOr(f[1], 0);
                }
            return Sx.box;
        }
        void boxAdd(const std::string& sid, long long n)
        {
            if (!FON("ssbox") || HOST()) return;
            boxLoad();
            Counts& bx = S().ss.box;
            long long v = g0(bx, sid) + n;
            if (v > 0) bx[sid] = v; else bx.erase(sid);
            std::string out;
            for (auto& kv : bx) out += kv.first + "\t" + lld(kv.second) + "\n";
            E().writeText("ss_box.txt", out);
        }
        void ssRewrite()
        {
            std::string out;
            for (auto& h : S().ss.hist) out += sf("%.1f\t%s\n", h.t, h.op.c_str());
            E().writeText("stash_ledger.txt", out);
        }
        Ss& ssInit()
        {
            Ss& Sx = S().ss;
            if (Sx.init) return Sx;
            Sx.init = true;
            if (HOST())
            {
                std::string text;
                if (E().readText("stash_ledger.txt", text))
                    for (auto& l : lines(text))
                    {
                        size_t tb = l.find('\t');
                        if (tb == std::string::npos || tb == 0 || tb + 1 >= l.size()) continue;
                        std::string ts = l.substr(0, tb);
                        bool ok = true;
                        for (char c : ts) if (!(isD(c) || c == '.')) ok = false;
                        auto t = tonum(ts);
                        if (!ok || !t) continue;
                        std::string op = l.substr(tb + 1);
                        Sx.hist.push_back(SsHist{*t, op});
                        ssOps(Sx.led, op);
                    }
                LOG("NOLA stash: livro do host carregado, " + std::to_string(Sx.hist.size()) + " operacoes");
            }
            return Sx;
        }
        void ssRecord(Ss& Sx, const std::string& op)
        {
            if (!HOST()) return;
            double gt = E().gameSecs().value_or(0);
            Sx.hist.push_back(SsHist{gt, op});
            E().appendText("stash_ledger.txt", sf("%.1f\t%s\n", gt, op.c_str()));
            Sx.yl.reset();
        }
        void ssQueue(const std::string& prefix, const std::string& sid, long long n)
        {
            for (auto& s : nodeBits(prefix, sid, n)) S().it.run.push_back(s);
        }
        std::string stashNoteI(const std::string& ops)
        {
            Ss& Sx = ssInit();
            ssOps(Sx.led, ops);
            ssRecord(Sx, ops);
            Sx.seq += 1;
            Sx.out.push_back(ItOutE{NOW(), lld(Sx.seq) + "," + ops});
            LOG("NOLA stash: anotado a mao: " + ops);
            return ssText(Sx.led);
        }
        std::string ssOutI()
        {
            if (!FON("sstash")) return "";
            Ss& Sx = ssInit();
            double now = NOW();
            std::vector<ItOutE> keep;
            std::vector<std::string> parts;
            for (auto& e : Sx.out) if (now - e.t < 2) { keep.push_back(e); parts.push_back(e.s); }
            Sx.out = keep;
            std::string r = parts.empty() ? std::string() : "#Y:" + join(parts, "|");
            if (HOST() && E().lookOn())
            {
                if (!Sx.yl)
                {
                    SsYl y;
                    y.chunks = ylChunks(ssText(Sx.led));
                    for (size_t i = 0; i < y.chunks.size(); ++i) y.hashes.push_back(E().lookPut("YL" + std::to_string(i + 1) + "~" + y.chunks[i]));
                    Sx.yl = y;
                }
                SsYl& Y = *Sx.yl;
                Y.rr = Y.rr % Y.chunks.size() + 1;
                E().lookPut("YL" + std::to_string(Y.rr) + "~" + Y.chunks[Y.rr - 1]);
                r += "#YL:" + std::to_string(Y.chunks.size()) + "," + join(Y.hashes, ".");
            }
            return r;
        }
        long long ylIndex(const std::string& t)
        {
            if (!startsWith(t, "YL")) return 0;
            size_t i = 2;
            while (i < t.size() && isD(t[i])) ++i;
            return std::strtoll(t.substr(2, i - 2).c_str(), nullptr, 10);
        }
        std::string ylBody(const std::string& t)
        {
            if (!startsWith(t, "YL")) return t;
            size_t i = 2;
            while (i < t.size() && isD(t[i])) ++i;
            if (i == 2 || i >= t.size() || t[i] != '~') return t;
            return t.substr(i + 1);
        }
        void ssTick()
        {
            if (!FON("sstash")) return;
            State& st = S();
            Ss& Sx = ssInit();
            double now = NOW();
            Obj pw = E().pawn();
            if (!VAL(pw)) return;
            uint64_t addr = E().addr(pw);
            if (!Sx.addr || *Sx.addr != addr) { Sx.addr = addr; Sx.addrAt = now; Sx.addrN += 1; Sx.open.reset(); }
            if (HOST() && !Sx.hist.empty() && now >= Sx.rbAt)
            {
                Sx.rbAt = now + 5;
                auto gt = E().gameSecs();
                double last = Sx.hist.back().t;
                bool newPawn = Sx.addrN > 1 && (!Sx.rbAddr || *Sx.rbAddr != addr) && now - Sx.addrAt >= 12;
                if (gt && (last > *gt + 60 || (newPawn && last > *gt + 1)))
                {
                    std::vector<SsHist> keep;
                    Sx.led.clear();
                    for (auto& h : Sx.hist) if (h.t <= *gt) { keep.push_back(h); ssOps(Sx.led, h.op); }
                    LOG(sf("NOLA stash: save mais antigo no host: %d operacoes do livro descartadas", (int)(Sx.hist.size() - keep.size())));
                    Sx.hist = keep;
                    ssRewrite();
                    Sx.yl.reset();
                }
                if (now - Sx.addrAt >= 12) Sx.rbAddr = addr;
            }
            const std::optional<std::string>& mine = st.lock.mine;
            bool isBox = mine && has(*mine, "PlayerStash");
            if (isBox && !Sx.open)
            {
                long long m;
                SsOpen o;
                o.id = *mine;
                o.snap = invSnapI(pw, m);
                Obj ia = interacted(pw);
                std::string g;
                if (ia && E().guidHex(ia, g)) o.guid = g;
                Sx.open = o;
                LOG("NOLA stash: caixa aberta; inventario anotado");
            }
            else if (Sx.open && (!mine || *mine != Sx.open->id))
            {
                long long m;
                Counts after = invSnapI(pw, m);
                std::vector<std::string> ops;
                long long ghost = 0;
                for (auto& sid : sids())
                {
                    long long d = g0(after, sid) - g0(Sx.open->snap, sid);
                    if (d != 0 && FON("ssbox")) boxAdd(sid, -d);
                    if (d > 0)
                    {
                        long long have = g0(Sx.led, sid);
                        if (d > have)
                        {
                            if (!HOST() || FON("ssstrict")) { rmP(sid, d - have); ghost += d - have; }
                            d = have;
                        }
                        if (d > 0) ops.push_back("-" + sid + "x" + lld(d));
                    }
                    else if (d < 0) ops.push_back("+" + sid + "x" + lld(-d));
                }
                SsOpen opened = *Sx.open;
                Sx.open.reset();
                if (!ops.empty())
                {
                    std::string op = join(ops, ".");
                    ssOps(Sx.led, op);
                    ssRecord(Sx, op);
                    Sx.seq += 1;
                    Sx.out.push_back(ItOutE{now, lld(Sx.seq) + "," + op + (opened.guid ? (".@" + *opened.guid) : std::string())});
                }
                LOG(sf("NOLA stash: caixa fechada: %s%s", ops.empty() ? "nada mudou" : join(ops, " ").c_str(), ghost > 0 ? (" | " + lld(ghost) + " itens fantasma tirados de mim").c_str() : ""));
            }
            if (now - Sx.pruneAt.value_or(-10) >= 1)
            {
                Sx.pruneAt = now;
                for (auto it = Sx.seen.begin(); it != Sx.seen.end();)
                    if (now - it->second > 5) it = Sx.seen.erase(it); else ++it;
            }
            bool echo = E().peerLoopback();
            struct YIn
            {
                std::string ev, tok;
                int pid = -1;
            };
            std::vector<YIn> yins;
            bool mc = MC();
            Mc& M = st.mc;
            if (mc && now - M.yPrune.value_or(-10) >= 1)
            {
                M.yPrune = now;
                for (auto it = M.yseen.begin(); it != M.yseen.end();)
                    if (now - it->second > 5) it = M.yseen.erase(it); else ++it;
            }
            std::string ys;
            if (mc && HOST())
            {
                for (int v = 0; !echo && v < E().views(); ++v)
                {
                    if (!PARTV("Y", ys, v)) continue;
                    std::string tok = vTok(v);
                    for (auto& ev : splitNE(ys, '|')) yins.push_back(YIn{ev, tok, E().viewPid(v)});
                }
            }
            else
            {
                if (!echo)
                {
                    if (mc) ys = mcRelIn("Y");
                    else PART("Y", ys);
                }
                for (auto& ev : splitNE(ys, '|')) yins.push_back(YIn{ev, std::string(), -1});
                if (mc && !echo)
                {
                    std::string yr = mcRelIn("YR");
                    int me = E().myPid();
                    for (auto& ev : splitNE(yr, '|'))
                    {
                        size_t c0 = ev.find(',');
                        if (c0 == std::string::npos) continue;
                        int pid = tokPid(ev.substr(0, c0));
                        if (pid < 0 || pid == me) continue;
                        yins.push_back(YIn{ev.substr(c0 + 1), ev.substr(0, c0), pid});
                    }
                }
            }
            for (auto& yi : yins)
            {
                const std::string& ev = yi.ev;
                size_t cm = ev.find(',');
                if (cm == std::string::npos || cm == 0 || cm + 1 >= ev.size() || !allOf(ev.substr(0, cm), isD)) continue;
                double seq = numOr(ev.substr(0, cm), 0);
                std::string op = ev.substr(cm + 1);
                if (!yi.tok.empty())
                {
                    std::string key = yi.tok + ":" + ev.substr(0, cm);
                    bool was = M.yseen.count(key) != 0;
                    M.yseen[key] = now;
                    if (was) continue;
                    M.yIn += 1;
                    if (HOST()) M.yr.push_back(McRel{now, yi.tok + "," + ev});
                    LOG(sf("NOLA stash: mexida do pid %d na caixa (seq %s)", yi.pid, ev.substr(0, cm).c_str()));
                }
                else
                {
                    bool was = Sx.seen.count(seq) != 0;
                    Sx.seen[seq] = now;
                    if (was) continue;
                }
                ssOps(Sx.led, op);
                ssRecord(Sx, stripGuid(op));
                std::optional<std::string> g;
                {
                    size_t p = 0;
                    while ((p = op.find('@', p)) != std::string::npos)
                    {
                        size_t e;
                        if (hexRun(op, p + 1, e)) { g = op.substr(p + 1, e - p - 1); break; }
                        ++p;
                    }
                }
                for (auto& it : signItems(op))
                {
                    if (it.sign == '+')
                    {
                        ssQueue("S2U_SA_", it.sid, it.n);
                        if (FON("ssbox")) boxAdd(it.sid, it.n);
                    }
                    else if (FON("ssremove") && g)
                    {
                        long long k = std::min<long long>(it.n, FI("icmax", 1000));
                        for (long long j = 0; j < k; ++j) st.it.run.push_back("S2U_SR_" + *g + "_" + it.sid + "_1");
                    }
                }
                LOG("NOLA stash: o outro jogador mexeu na caixa: " + op + (FON("ssremove") ? "" : " (retiradas so no livro)"));
            }
            if (!HOST() && E().lookOn())
            {
                std::string sg;
                if (PART("YL", sg) && !Sx.open)
                {
                    if (!Sx.ylSeen || sg != *Sx.ylSeen)
                    {
                        std::vector<std::string> parts;
                        int miss = 0;
                        size_t cm = sg.find(',');
                        std::string hs;
                        if (cm != std::string::npos && cm > 0 && allOf(sg.substr(0, cm), isD) && cm + 1 < sg.size()) hs = sg.substr(cm + 1);
                        for (auto& h : splitNE(hs, '.'))
                        {
                            std::string t;
                            if (E().lookGet(h, t)) parts.push_back(t); else ++miss;
                        }
                        if (miss == 0 && !parts.empty())
                        {
                            std::stable_sort(parts.begin(), parts.end(), [](const std::string& a, const std::string& b) { return ylIndex(a) < ylIndex(b); });
                            Counts led;
                            for (auto& t : parts) for (auto& it : xItems(ylBody(t))) led[it.sid] = it.n;
                            Sx.led = led;
                            Sx.ylSeen = sg;
                            Sx.ylOk = true;
                        }
                    }
                    if (Sx.ylOk && (!Sx.fillAddr || *Sx.fillAddr != addr) && now - Sx.addrAt >= F("ssdelay", 10))
                    {
                        Sx.fillAddr = addr;
                        long long n = 0, kinds = 0;
                        bool useBx = FON("ssbox") && FON("csave");
                        Counts bx;
                        if (useBx) bx = boxLoad();
                        Counts led = Sx.led;
                        for (auto& kv : led)
                        {
                            long long d = useBx ? kv.second - g0(bx, kv.first) : kv.second;
                            if (d > 0)
                            {
                                ssQueue("S2U_SA_", kv.first, d);
                                n += d;
                                kinds += 1;
                                if (useBx) boxAdd(kv.first, d);
                            }
                        }
                        LOG(sf("NOLA stash: copia da caixa preenchida pelo livro do host: %lld itens de %lld tipos", n, kinds));
                    }
                }
            }
        }

        const char* ART_QUIET[] = {"/Script/AkAudio.AkComponent", "/Script/Niagara.NiagaraComponent", "/Script/Stalker2.PhysicsInteractionComponent", "/Script/Stalker2.ArtifactInteractableComponent"};
        Ap& apInit()
        {
            Ap& P = S().ap;
            if (P.init) return P;
            P.init = true;
            std::string text;
            if (E().readText("s2u_artspawn.tsv", text))
                for (auto& l : lines(text))
                {
                    auto f = splitAll(l, '\t');
                    if (f.size() < 5 || !allOf(f[0], isX)) continue;
                    ApPlace p;
                    if (!fullNum(f[2], p.x) || !fullNum(f[3], p.y) || !numPrefix(f[4], p.z)) continue;
                    p.sid = f[0];
                    P.places.push_back(p);
                }
            if (HOST())
            {
                std::string t2;
                if (E().readText("art_taken.txt", t2))
                    for (auto& l : lines(t2))
                    {
                        auto f = splitAll(l, '\t');
                        double u;
                        if (f.size() < 2 || !allOf(f[0], isD) || !numPrefix(f[1], u)) continue;
                        P.taken[(int)numOr(f[0], 0)] = u;
                    }
            }
            LOG("NOLA art2: " + std::to_string(P.places.size()) + " lugares de artefato em s2u_artspawn.tsv");
            return P;
        }
        double apClock()
        {
            auto g = E().gameSecs();
            return g ? *g : E().wallSecs();
        }
        void apSave(Ap& P)
        {
            std::string out;
            for (auto& kv : P.taken) out += sf("%d\t%.0f\n", kv.first, kv.second);
            E().writeText("art_taken.txt", out);
        }
        std::optional<int> apPlace(Ap& P, double x, double y)
        {
            std::optional<int> best;
            double bd = sq(F("artrad", 2500));
            for (size_t i = 0; i < P.places.size(); ++i)
            {
                double d = sq(P.places[i].x - x) + sq(P.places[i].y - y);
                if (d <= bd) { best = (int)i + 1; bd = d; }
            }
            return best;
        }
        bool artTaken(int i, const std::string& who)
        {
            if (F("artsync", 0) != 2 || !HOST()) return false;
            Ap& P = apInit();
            if (i < 1 || i > (int)P.places.size()) return false;
            P.taken[i] = apClock() + F("artcool", 5400);
            apSave(P);
            LOG(sf("NOLA art2: lugar %d (%s) esvaziado pelo %s; fechado para o outro jogador por %lld s de jogo", i, P.places[i - 1].sid.substr(0, 8).c_str(), who.c_str(), FI("artcool", 5400)));
            return true;
        }
        std::string apOut()
        {
            Ap& P = apInit();
            if (!HOST()) return "";
            std::vector<std::string> t;
            for (auto& kv : P.taken) t.push_back(std::to_string(kv.first));
            return "#AL:" + join(t, ".");
        }
        void apSilence(Ap& P, const std::string& id, Obj o, bool on)
        {
            if (!FON("artmute")) return;
            int n = 0;
            if (on)
            {
                std::map<std::string, std::vector<bool>> was;
                for (const char* cp : ART_QUIET)
                {
                    auto& w = was[cp];
                    for (Obj c : E().comps(o, cp))
                    {
                        bool act = true;
                        bool a2;
                        if (E().compActive(c, a2)) act = a2;
                        w.push_back(act);
                        if (E().compQuiet(c, has(cp, "AkComponent"))) ++n;
                    }
                }
                P.was[id] = was;
                E().setActorTick(o, false);
            }
            else
            {
                E().setActorTick(o, true);
                std::map<std::string, std::vector<bool>> was;
                auto f = P.was.find(id);
                if (f != P.was.end()) { was = f->second; P.was.erase(f); }
                for (const char* cp : ART_QUIET)
                {
                    auto list = E().comps(o, cp);
                    auto wc = was.find(cp);
                    for (size_t i = 0; i < list.size(); ++i)
                    {
                        bool act = (wc == was.end() && !has(cp, "ArtifactInteractable")) || (wc != was.end() && i < wc->second.size() && wc->second[i]);
                        if (E().compWake(list[i], act)) ++n;
                    }
                }
            }
            LOG(sf("NOLA art2: artefato %s %s (%d componentes%s)", id.c_str(), on ? "silenciado" : "reativado", n, ""));
        }
        void apTick()
        {
            Ap& P = apInit();
            double now = NOW();
            if (now < P.at) return;
            P.at = now + 1;
            Obj pw = E().pawn();
            if (!VAL(pw)) return;
            bool host = HOST();
            if (host)
            {
                double gs = apClock();
                bool ch = false;
                double cool = F("artcool", 5400);
                for (auto it = P.taken.begin(); it != P.taken.end();)
                {
                    if (gs >= it->second || it->second - gs > cool + 60)
                    {
                        int i = it->first;
                        it = P.taken.erase(it);
                        ch = true;
                        LOG("NOLA art2: lugar " + std::to_string(i) + " aberto de novo");
                    }
                    else ++it;
                }
                if (ch) apSave(P);
            }
            else
            {
                std::string sg;
                if (PART("AL", sg))
                {
                    std::map<int, double> t;
                    size_t i = 0;
                    while (i < sg.size())
                    {
                        if (!isD(sg[i])) { ++i; continue; }
                        size_t j = i;
                        while (j < sg.size() && isD(sg[j])) ++j;
                        t[(int)std::strtoll(sg.substr(i, j - i).c_str(), nullptr, 10)] = 1;
                        i = j;
                    }
                    P.taken = t;
                }
            }
            std::set<std::string> live;
            V3 ppv;
            bool hasPp = E().loc(pw, ppv);
            uint64_t paddr = E().addr(pw);
            if (hasPp)
            {
                if (!P.paddr || paddr != *P.paddr || (P.pp && sq(ppv.x - P.pp->x) + sq(ppv.y - P.pp->y) > sq(5000))) P.quiet = now + F("artquiet", 20);
                P.pp = ppv;
                P.paddr = paddr;
            }
            for (Obj o : E().all("Artifact"))
            {
                if (!VAL(o)) continue;
                std::string id = E().name(o);
                live.insert(id);
                V3 q;
                if (!E().loc(o, q)) continue;
                auto Tf = P.track.find(id);
                if (std::fabs(q.x) + std::fabs(q.y) < 1000)
                {
                    if (Tf != P.track.end() && Tf->second.place && !Tf->second.done)
                    {
                        ApTrack& T = Tf->second;
                        T.done = true;
                        std::optional<std::string> why;
                        if (now < P.quiet) why = std::string("teleporte ou save carregado ha pouco");
                        else if (T.pd.value_or(1e9) > F("artreach", 600)) why = sf("eu estava a %.0f m dele", T.pd.value_or(0) / 100);
                        if (why) LOG("NOLA art2: artefato do lugar " + std::to_string(*T.place) + " sumiu, mas nao conta como pego (" + *why + ")");
                        if (!why && !P.hid.count(id))
                        {
                            if (host) artTaken(*T.place, "host");
                            else itemSendI("A", std::to_string(*T.place), "x", "1", true);
                            LOG("NOLA art2: peguei um artefato no lugar " + std::to_string(*T.place));
                        }
                    }
                    continue;
                }
                ApTrack& T = P.track[id];
                T.place = apPlace(P, q.x, q.y);
                if (hasPp) T.pd = std::sqrt(sq(q.x - ppv.x) + sq(q.y - ppv.y) + sq(q.z - ppv.z)); else T.pd.reset();
                bool closed = T.place && P.taken.count(*T.place);
                if (closed && !P.hid.count(id))
                {
                    P.hid[id] = o;
                    gHide(o);
                    apSilence(P, id, o, true);
                }
                else if (!closed && P.hid.count(id))
                {
                    P.hid.erase(id);
                    apSilence(P, id, o, false);
                    E().setHidden(o, false);
                    E().setCollision(o, true);
                }
            }
            for (auto it = P.track.begin(); it != P.track.end();)
            {
                if (!live.count(it->first))
                {
                    P.hid.erase(it->first);
                    P.was.erase(it->first);
                    it = P.track.erase(it);
                }
                else ++it;
            }
        }

        const std::map<std::string, EqRow>& eqTab()
        {
            State& st = S();
            if (st.eqLoaded) return st.eq;
            st.eqLoaded = true;
            int nList = 0;
            std::string text;
            if (E().readText("s2u_equip.tsv", text))
                for (auto& l : lines(text))
                {
                    auto f = splitAll(l, '\t');
                    if (f.size() < 4 || f[0].empty()) continue;
                    bool ok = true;
                    for (int i = 0; i < 3; ++i) for (char c : f[i]) if (isSp(c)) ok = false;
                    if (!ok) continue;
                    EqRow r;
                    r.kind = f[1];
                    if (!f[2].empty()) r.ammo = f[2];
                    std::string mx = firstTok(f[3]);
                    auto v = tonum(mx);
                    if (v) r.max = *v;
                    st.eq[f[0]] = r;
                    ++nList;
                }
            int nBase = 0;
            std::string bk;
            if (E().readText("s2u_basekit.txt", bk))
            {
                std::set<std::string> base;
                for (auto& l : lines(bk))
                {
                    size_t i = 0;
                    while (i < l.size() && isSp(l[i])) ++i;
                    size_t j = i;
                    while (j < l.size() && isW(l[j])) ++j;
                    if (j > i) base.insert(l.substr(i, j - i));
                }
                nBase = (int)base.size();
            }
            LOG(sf("NOLA persist: %d itens de slot em s2u_equip.tsv, kit base vestido: %d", nList, nBase));
            if (st.eq.empty()) LOG("NOLA cloot: s2u_equip.tsv ausente ou vazio: pentes das armas nao previstos (clammo so acerta a municao solta)");
            return st.eq;
        }
        Counts clHoldI(const Counts& want)
        {
            if (FI("clammo", 0) != 1) return Counts();
            return clHold(want, eqTab(), true);
        }
        bool clVis(const std::string& str)
        {
            if (str == "-") return false;
            if (FI("clammo", 0) != 1) return true;
            Counts want = clDec(str);
            Counts held = clHoldI(want);
            for (auto& kv : want) if (kv.second > g0(held, kv.first)) return true;
            return false;
        }

        Cl& clSt()
        {
            State& st = S();
            std::string wk = E().worldKey();
            if (st.cl && st.cl->wk != wk)
            {
                if (st.cl->B.size() + st.cl->K.size() > 0) LOG(sf("NOLA cloot: mundo novo -> %d corpos de saque e %d corpos do host esquecidos sem tocar", (int)st.cl->B.size(), (int)st.cl->K.size()));
                if (!st.cl->gcLast.empty() && st.cl->gcLast != "0") E().gcFeed(0, {});
                st.cl.reset();
            }
            if (!st.cl)
            {
                st.cl.emplace();
                Cl& C = *st.cl;
                C.wk = wk;
                C.nonce = lld((long long)std::floor(NOW() * 1000) % 1000000);
                C.s = ClStat();
                C.s.at = NOW();
            }
            return *st.cl;
        }
        void clCount(Obj o, bool quest, Counts& t, Counts& q)
        {
            t.clear();
            q.clear();
            if (!VAL(o)) return;
            if (!E().invReadable(o)) { ++S().invGuard; return; }
            bool useQ = quest && qtOk();
            for (auto& s : sids())
            {
                long long n = 0;
                if (E().count(o, s, n) && n > 0) { if (useQ && qs(s)) q[s] = n; else t[s] = n; }
            }
            if (useQ)
                for (auto& s : S().qList)
                {
                    if (t.count(s) || q.count(s)) continue;
                    long long n = 0;
                    if (E().count(o, s, n) && n > 0) q[s] = n;
                }
        }
        const std::unordered_map<uint64_t, Obj>& clMap(Cl& C, double now)
        {
            if (C.mapOk && C.mapAt == now) return C.map;
            C.map.clear();
            for (Obj o : E().all("Pawn")) if (VAL(o)) C.map[E().addr(o)] = o;
            C.mapOk = true;
            C.mapAt = now;
            return C.map;
        }
        Obj clGet(Cl& C, double now, uint64_t ad, const std::string& nm)
        {
            const auto& m = clMap(C, now);
            auto f = m.find(ad);
            if (f == m.end() || !VAL(f->second)) return 0;
            return E().name(f->second) == nm ? f->second : 0;
        }
        void clInert(Obj o)
        {
            E().setHidden(o, true);
            E().setCollision(o, false);
            E().setInteraction(o, false);
        }
        bool clOwn(Obj o)
        {
            State& st = S();
            if (!st.cl || st.cl->own.empty()) return false;
            if (!VAL(o)) return false;
            auto f = st.cl->own.find(E().addr(o));
            return f != st.cl->own.end() && f->second == E().name(o);
        }
        bool clIs(Obj pw)
        {
            if (F("cloot", 0) != 1 || !S().cl) return false;
            Obj a = interacted(pw);
            if (!a) return false;
            if (clOwn(a)) return true;
            uint64_t ad = E().addr(a);
            for (auto& kv : S().cl->B) if (kv.second.nat && kv.second.ad == ad) return true;
            return false;
        }
        bool clMeshHide(const std::string& hid)
        {
            if (F("clvis", 0) != 0) return false;
            if (HOST()) return true;
            if (E().natOn() && E().natDead(hid)) return true;
            return F("clmir", 1) != 1;
        }
        void clClose(Obj lp, Cl& C, const std::string& hid, const char* why)
        {
            if (!E().closeLoot(lp)) E().closeLoot(E().controller());
            NOLA_CEN_EV("lootclose", why, hid);
            C.s.deny += 1;
            double now = NOW();
            if (now - C.denyLog > 2)
            {
                C.denyLog = now;
                LOG("NOLA cloot: janela de saque de " + hid + " fechada aqui (" + why + ")");
            }
        }
        std::optional<uint64_t> clOpenAddr(Obj lp)
        {
            Obj w = E().first("InventoryNew");
            bool vis = false;
            if (!VAL(w) || !E().visible(w, vis) || !vis) return std::nullopt;
            Obj a = interacted(lp);
            if (!a) return std::nullopt;
            return E().addr(a);
        }
        void clZFree(Cl& C, std::map<std::string, ClZ>::iterator it, double now)
        {
            Obj o = clGet(C, now, it->second.ad, it->second.nm);
            if (o)
            {
                clInert(o);
                if (it->second.uid) E().destroyModel(*it->second.uid);
            }
            C.Z.erase(it);
        }
        void clZTick(Cl& C, double now)
        {
            if (C.Z.empty()) return;
            for (auto it = C.Z.begin(); it != C.Z.end();)
            {
                Obj o = clGet(C, now, it->second.ad, it->second.nm);
                if (!o) { it = C.Z.erase(it); continue; }
                bool vis = false;
                if (E().meshVisible(o, vis) && !vis) E().setMeshVisible(o, true);
                E().setInteraction(o, false);
                ++it;
            }
            long long cap = std::max(1LL, (long long)F("clkeepmax", 12));
            while ((long long)C.Z.size() > cap)
            {
                auto old = C.Z.begin();
                for (auto it = C.Z.begin(); it != C.Z.end(); ++it) if (it->second.at < old->second.at) old = it;
                clZFree(C, old, now);
            }
        }
        bool clKeep(Cl& C, const std::string& hid, ClB& B, Obj o, double now)
        {
            if (HOST() || !B.uid || !o || F("clkeep", 1) != 1) return false;
            auto z = C.Z.find(hid);
            if (z != C.Z.end()) clZFree(C, z, now);
            ClZ Z;
            Z.ad = B.ad;
            Z.nm = B.nm;
            Z.uid = B.uid;
            Z.at = now;
            C.Z[hid] = Z;
            E().setHidden(o, false);
            E().setCollision(o, false);
            E().setInteraction(o, false);
            E().setMeshVisible(o, true);
            return true;
        }
        void clDrop(Cl& C, const std::string& hid, double now, const char* why)
        {
            auto f = C.B.find(hid);
            if (f == C.B.end()) return;
            ClB& B = f->second;
            Obj o = clGet(C, now, B.ad, B.nm);
            if (B.nat)
            {
                if (o) E().setInteraction(o, false);
                E().natClOwn(B.ad, false);
                C.B.erase(f);
                C.s.drop += 1;
                if (FON("cllog")) LOG("NOLA cloot: corpo nativo de " + hid + " solto (" + why + "): fica no mundo sem interacao");
                return;
            }
            if (o && std::string(why) == "vazio" && clKeep(C, hid, B, o, now))
            {
                C.B.erase(f);
                C.s.drop += 1;
                if (FON("cllog")) LOG("NOLA cloot: corpo de saque de " + hid + " fica como corpo vazio (sem interacao)");
                return;
            }
            if (o)
            {
                clInert(o);
                if (B.uid) E().destroyModel(*B.uid);
            }
            C.B.erase(f);
            C.s.drop += 1;
            if (FON("cllog")) LOG("NOLA cloot: corpo de saque de " + hid + " largado (" + why + ")");
        }
        std::string addrKey(uint64_t ad, const std::string& nm, const char* sep) { return sf("%llu", (unsigned long long)ad) + sep + nm; }
        bool clNatOn() { return F("clnat", 1) == 1; }
        bool clGcOn() { return F("clhostgc", 1) == 1; }
        Obj clAt(Cl& C, double now, uint64_t ad)
        {
            if (!ad) return 0;
            const auto& m = clMap(C, now);
            auto f = m.find(ad);
            return f == m.end() || !VAL(f->second) ? 0 : f->second;
        }
        void clMark(Cl& C, const std::string& hid, Obj o, double now)
        {
            if (!clGcOn() || !o) return;
            uint64_t ad = E().addr(o);
            auto f = C.M.find(hid);
            if (f != C.M.end() && f->second.ad == ad) return;
            ClM m;
            m.ad = ad;
            m.nm = E().name(o);
            m.at = now;
            C.M[hid] = m;
            long long cap = std::max(1LL, (long long)F("clgcmax", 32));
            while ((long long)C.M.size() > cap)
            {
                auto old = C.M.begin();
                for (auto it = C.M.begin(); it != C.M.end(); ++it) if (it->second.at < old->second.at) old = it;
                C.M.erase(old);
            }
        }
        Obj clNative(Cl& C, const std::string& hid, double now, bool& wait)
        {
            wait = false;
            if (E().natOn())
            {
                int st = 0;
                Obj o = clAt(C, now, E().natBody(hid, st));
                if (o && st == 3 && !aliveOr(o, true)) return o;
                if (o) wait = true;
            }
            if (C.tw)
            {
                auto f = C.tw->find(hid);
                if (f != C.tw->end())
                {
                    Obj o = clAt(C, now, f->second);
                    if (o && !aliveOr(o, true)) return o;
                }
            }
            if (FON("clcopy") && !C.cp.count(hid)) wait = true;
            auto R = C.R.find(hid);
            if (R == C.R.end() || now - R->second.first > F("clnatwait", 5)) wait = false;
            return 0;
        }
        std::pair<long long, long long> clNatApply(Obj o, ClB& B, const std::string& str, bool all, bool& bad)
        {
            Counts t, q;
            clCount(o, true, t, q);
            Counts have = clAdd(t, q), want = clDec(str);
            std::set<std::string> am;
            if (!all)
            {
                const auto& eq = eqTab();
                for (auto& kv : want)
                {
                    auto e = eq.find(kv.first);
                    if (e != eq.end() && e->second.ammo && e->second.max.value_or(0) > 0 && g0(have, kv.first) < kv.second) am.insert(*e->second.ammo);
                }
            }
            long long rm = 0, add = 0;
            for (auto& kv : have)
            {
                long long w = g0(want, kv.first);
                if (kv.second <= w) continue;
                long long r = 0;
                if (E().removeItem(o, kv.first, kv.second - w, r)) rm += r;
                else bad = true;
            }
            if (bad) return {rm, add};
            for (auto& kv : want)
            {
                long long h = g0(have, kv.first);
                if (kv.second > h && !am.count(kv.first))
                {
                    E().createItem(kv.first, *B.uid, kv.second - h);
                    add += kv.second - h;
                }
            }
            return {rm, add};
        }
        bool clAdopt(Cl& C, const std::string& hid, const ClW& W, Obj o, double now)
        {
            auto u = E().guid(o);
            if (!u) return false;
            ClB B;
            B.ad = E().addr(o);
            B.nm = E().name(o);
            B.uid = u;
            B.nat = true;
            B.st = "n1";
            B.wstr = W.str;
            B.ver = W.ver;
            B.at = now;
            E().setInteraction(o, false);
            E().natClOwn(B.ad, true);
            bool bad = false;
            auto r = clNatApply(o, B, W.str, false, bad);
            if (bad)
            {
                E().natClOwn(B.ad, false);
                C.natNo.insert(hid);
                LOG("NOLA cloot: " + hid + ": o corpo nativo " + B.nm + " nao deixa tirar itens (remocao nativa indisponivel): usa o corpo de saque antigo");
                return false;
            }
            C.B[hid] = B;
            C.tries.erase(hid);
            clMark(C, hid, o, now);
            C.s.nat += 1;
            C.s.rmv += r.first;
            if (FON("cllog")) LOG(sf("NOLA cloot: %s: o corpo nativo %s (uid=%s) vira o corpo de saque, sem corpo extra (clnat): tirados=%lld criados=%lld lista=%s", hid.c_str(), B.nm.c_str(), uidStr(B.uid).c_str(), r.first, r.second, W.str.c_str()));
            return true;
        }
        void clNatStep(Cl& C, const std::string& hid, ClB& B, double now)
        {
            Obj o = clGet(C, now, B.ad, B.nm);
            if (!o)
            {
                E().natClOwn(B.ad, false);
                C.B.erase(hid);
                return;
            }
            E().setInteraction(o, false);
            if (now - B.at < 0.5) return;
            if (B.st == "n1")
            {
                bool bad = false;
                auto r = clNatApply(o, B, B.wstr, true, bad);
                C.s.rmv += r.first;
                B.st = "n2";
                B.at = now;
                return;
            }
            Counts t, q;
            clCount(o, true, t, q);
            B.gv = clAdd(t, q);
            B.hasGv = true;
            B.hstr = clEnc(B.gv);
            B.st = "live";
            C.s.live += 1;
            if (FON("cllog")) LOG(sf("NOLA cloot: corpo nativo de %s pronto (%s) lista=%s%s", hid.c_str(), B.nm.c_str(), B.hstr.c_str(), B.hstr != B.wstr ? (" PARCIAL (pedido " + B.wstr + ")").c_str() : ""));
        }
        void clGone(Cl& C, const std::string& hid, double now)
        {
            if (C.xdone.count(hid)) return;
            C.xdone[hid] = now;
            std::set<uint64_t> done;
            int n = 0;
            auto kill = [&](Obj o) {
                if (!o) return;
                uint64_t ad = E().addr(o);
                if (done.count(ad)) return;
                done.insert(ad);
                clInert(o);
                E().natClOwn(ad, false);
                auto u = E().guid(o);
                if (u) E().destroyModel(*u);
                ++n;
            };
            auto bf = C.B.find(hid);
            if (bf != C.B.end())
            {
                Obj o = clGet(C, now, bf->second.ad, bf->second.nm);
                if (bf->second.nat)
                {
                    kill(o);
                    C.B.erase(bf);
                }
                else
                {
                    if (o)
                    {
                        done.insert(E().addr(o));
                        ++n;
                    }
                    clDrop(C, hid, now, "o host tirou o corpo");
                }
            }
            auto z = C.Z.find(hid);
            if (z != C.Z.end())
            {
                Obj o = clGet(C, now, z->second.ad, z->second.nm);
                if (o && !done.count(E().addr(o)))
                {
                    done.insert(E().addr(o));
                    ++n;
                }
                clZFree(C, z, now);
            }
            auto m = C.M.find(hid);
            if (m != C.M.end())
            {
                kill(clGet(C, now, m->second.ad, m->second.nm));
                C.M.erase(m);
            }
            if (E().natOn())
            {
                int st = 0;
                Obj o = clAt(C, now, E().natBody(hid, st));
                if (o && st == 3) kill(o);
            }
            if (C.tw)
            {
                auto f = C.tw->find(hid);
                if (f != C.tw->end())
                {
                    Obj o = clAt(C, now, f->second);
                    if (o && !aliveOr(o, true)) kill(o);
                    C.tw->erase(f);
                }
            }
            C.R.erase(hid);
            C.fail[hid] = now + 1e9;
            C.s.gone += 1;
            LOG(sf("NOLA cloot: o host tirou o corpo %s do mundo dele: %d corpo(s) tirado(s) aqui tambem", hid.c_str(), n));
        }
        void clGcFeed(Cl& C, double now)
        {
            if (HOST() || !clGcOn())
            {
                if (C.gcLast != "0")
                {
                    if (!C.gcLast.empty()) E().gcFeed(0, {});
                    C.gcLast = "0";
                }
                return;
            }
            for (auto& kv : C.R)
            {
                if (kv.second.gone) continue;
                bool wait = false;
                Obj o = clNative(C, kv.first, now, wait);
                if (o) clMark(C, kv.first, o, now);
            }
            for (auto& kv : C.B)
                if (kv.second.st == "live")
                {
                    Obj o = clGet(C, now, kv.second.ad, kv.second.nm);
                    if (o) clMark(C, kv.first, o, now);
                }
            std::vector<uint64_t> keep;
            for (auto it = C.M.begin(); it != C.M.end();)
            {
                if (!clGet(C, now, it->second.ad, it->second.nm))
                {
                    it = C.M.erase(it);
                    continue;
                }
                keep.push_back(it->second.ad);
                ++it;
            }
            if (E().natOn()) E().natGcKeep(keep);
            std::sort(keep.begin(), keep.end());
            std::string k = "2";
            for (uint64_t a : keep) k += sf(",%llx", (unsigned long long)a);
            if (k == C.gcLast) return;
            C.gcLast = k;
            E().gcFeed(2, keep);
        }
        void clSpawn(Cl& C, const std::string& hid, ClW W, double now)
        {
            V3 nc;
            if (E().natOn() && E().natCorpse(hid, nc)) { W.x = nc.x; W.y = nc.y; W.z = nc.z; }
            auto zk = C.Z.find(hid);
            if (zk != C.Z.end()) clZFree(C, zk, now);
            std::set<std::string> before;
            for (Obj o : E().all("BP_AI_Human_C")) if (VAL(o)) before.insert(addrKey(E().addr(o), E().name(o), "|"));
            C.gap = now + 1.5;
            C.s.spawn += 1;
            std::string clsid = E().fstr("clsid", "Nola_RemoteTarget");
            std::string err;
            if (!E().spawnObj(clsid, W.x, W.y, W.z + 50, err))
            {
                C.fail[hid] = now + 10;
                C.s.fail += 1;
                LOG("NOLA cloot: XSpawnObjBySID " + clsid + " falhou: " + err);
                return;
            }
            ClPend p;
            p.hid = hid;
            p.st = "ask";
            p.at = now;
            p.x = W.x;
            p.y = W.y;
            p.z = W.z;
            p.str = W.str;
            p.ver = W.ver;
            p.before = before;
            C.pend = p;
        }
        void clFillLog(Cl& C, const std::string& hid, ClB& B, ClPend& P)
        {
            Counts want = clDec(P.str), over;
            for (auto& kv : B.gv) if (kv.second > g0(want, kv.first)) over[kv.first] = kv.second - g0(want, kv.first);
            if (B.held.empty() && over.empty()) return;
            C.s.hold += 1;
            if (FON("cllog")) LOG(sf("NOLA cloot: corpo de saque de %s: armas retidas (pente cheio > municao da lista)=%s sobra sobre a lista=%s", hid.c_str(), clEnc(B.held).c_str(), clEnc(over).c_str()));
        }
        void clFill1(Cl& C, ClPend& P, ClB& B, double now)
        {
            (void)C;
            Counts want = clDec(P.str);
            Counts held = clHoldI(want);
            const auto& eq = eqTab();
            std::set<std::string> am;
            for (auto& kv : want)
            {
                auto e = eq.find(kv.first);
                if (e != eq.end() && e->second.ammo && e->second.max.value_or(0) > 0 && kv.second > g0(held, kv.first)) am.insert(*e->second.ammo);
            }
            for (auto& kv : want)
            {
                long long k = kv.second - g0(held, kv.first);
                if (k > 0 && !am.count(kv.first)) E().createItem(kv.first, *B.uid, k);
            }
            P.want = want;
            P.am = am;
            B.held = held;
            P.st = am.empty() ? "fill" : "fill1";
            P.at = now;
        }
        void clFill2(Cl& C, ClPend& P, ClB& B, Obj o, double now)
        {
            (void)C;
            if (now - P.at < 0.5) return;
            Counts t, q;
            clCount(o, HOST() || b164on(), t, q);
            Counts have = clAdd(t, q);
            for (auto& sid : P.am)
            {
                long long d = g0(P.want, sid) - g0(have, sid);
                if (d > 0) E().createItem(sid, *B.uid, d);
            }
            P.st = "fill";
            P.at = now;
        }
        void clStep(Cl& C, Obj lp, double now)
        {
            (void)lp;
            ClPend& P = *C.pend;
            std::string hid = P.hid;
            if (P.st == "ask")
            {
                if (now - P.at > 6)
                {
                    ClPend Pc = P;
                    C.pend.reset();
                    int nt = ++C.tries[hid];
                    C.fail[hid] = nt >= 3 ? now + 1e9 : now + 10;
                    C.s.fail += 1;
                    LOG("NOLA cloot: nenhum corpo novo em 6 s para " + hid + (nt >= 3 ? sf(" (%d pedidos sem corpo: desisti deste corpo; clsid=%s)", nt, E().fstr("clsid", "Nola_RemoteTarget").c_str()) : std::string()));
                    if (F("cllate", 1) == 1)
                    {
                        if (!C.late) C.late.emplace();
                        C.late->push_back(ClLate{hid, Pc.before, Pc.x, Pc.y, now + 60});
                        if (C.late->size() > 4) C.late->erase(C.late->begin());
                    }
                    return;
                }
                if (now < P.nx) return;
                P.nx = now + 0.5;
                Obj best = 0;
                std::optional<double> bd;
                Obj tp = E().targetPawn(), pp = E().puppetPawn();
                for (Obj o : E().all("BP_AI_Human_C"))
                {
                    if (!VAL(o) || o == tp || o == pp) continue;
                    if (P.before.count(addrKey(E().addr(o), E().name(o), "|"))) continue;
                    double mh = 0;
                    if (!E().maxHp(o, mh) || mh < 4000 || mh > 6000) continue;
                    V3 q;
                    if (!E().loc(o, q)) continue;
                    double d = sq(q.x - P.x) + sq(q.y - P.y);
                    if (d < sq(800) && (!bd || d < *bd)) { best = o; bd = d; }
                }
                if (!best) return;
                ClB B;
                B.ad = E().addr(best);
                B.nm = E().name(best);
                B.st = "kill";
                B.wstr = P.str;
                B.ver = P.ver;
                B.uid = E().guid(best);
                C.own[B.ad] = B.nm;
                E().setHidden(best, false);
                E().setCollision(best, true);
                E().setInteraction(best, false);
                B.mh = clMeshHide(hid) || (b164on() && C.tw && C.tw->count(hid));
                if (B.mh) E().setMeshVisible(best, false);
                E().setLoc(best, V3{P.x, P.y, P.z});
                bool noUid = !B.uid;
                C.B[hid] = B;
                C.tries.erase(hid);
                if (noUid)
                {
                    clDrop(C, hid, now, "sem uid");
                    C.pend.reset();
                    C.fail[hid] = now + 30;
                    C.s.fail += 1;
                    return;
                }
                E().killUid(*C.B[hid].uid);
                P.st = "kill";
                P.at = now;
                P.kills = 1;
                return;
            }
            auto bf = C.B.find(hid);
            Obj o = bf != C.B.end() ? clGet(C, now, bf->second.ad, bf->second.nm) : 0;
            if (!o)
            {
                C.pend.reset();
                if (bf != C.B.end()) C.B.erase(bf);
                C.fail[hid] = now + 10;
                C.s.fail += 1;
                LOG("NOLA cloot: corpo de saque de " + hid + " sumiu enquanto era feito");
                return;
            }
            ClB& B = bf->second;
            if (P.st == "kill")
            {
                bool alive = aliveOr(o, true);
                if (!alive)
                {
                    if (FI("clammo", 0) >= 1) { clFill1(C, P, B, now); return; }
                    for (auto& kv : clDec(P.str)) E().createItem(kv.first, *B.uid, kv.second);
                    P.st = "fill";
                    P.at = now;
                }
                else if (now - P.at > 5)
                {
                    clDrop(C, hid, now, "nao morreu em 5 s");
                    C.pend.reset();
                    C.fail[hid] = now + 30;
                    C.s.fail += 1;
                }
                else if (now - P.at > 1.5 && P.kills == 1)
                {
                    P.kills = 2;
                    E().setUnkillable(*B.uid, false);
                    E().killUid(*B.uid);
                }
            }
            else if (P.st == "fill1") clFill2(C, P, B, o, now);
            else if (P.st == "fill")
            {
                if (now - P.at < 0.5) return;
                Counts t, q;
                clCount(o, HOST() || b164on(), t, q);
                ClPend Pc = P;
                B.hstr = clEnc(clAdd(t, q));
                B.wstr = Pc.str;
                B.st = "live";
                C.pend.reset();
                B.gv = clAdd(t, q);
                B.hasGv = true;
                if (FI("clammo", 0) >= 1) clFillLog(C, hid, B, Pc);
                C.s.live += 1;
                if (!HOST() && b164on() && E().natOn())
                {
                    uint64_t tw = 0;
                    if (C.tw) { auto f = C.tw->find(hid); if (f != C.tw->end()) tw = f->second; }
                    if (E().clSwap(hid, B.ad, tw)) { B.sw = true; B.mh = false; }
                }
                if (FON("cllog")) LOG(sf("NOLA cloot: corpo de saque de %s pronto uid=%s (%s) lista=%s%s", hid.c_str(), uidStr(B.uid).c_str(), B.nm.c_str(), B.hstr.c_str(), B.hstr != Pc.str ? (" PARCIAL (pedido " + Pc.str + ")").c_str() : ""));
            }
        }
        void clRecount(Cl& C, const std::string& hid, double now, const char* why)
        {
            auto f = C.K.find(hid);
            if (f == C.K.end()) return;
            ClE& E_ = f->second;
            Obj o = clGet(C, now, E_.ad, hid);
            if (!o) return;
            E_.rc = now;
            C.s.rc += 1;
            Counts t, q;
            clCount(o, true, t, q);
            std::string s = clEnc(t);
            if (s != E_.str || clEnc(q) != clEnc(E_.q))
            {
                E_.canon = t;
                E_.q = q;
                E_.str = s;
                E_.ver += 1;
                if (FON("cllog")) LOG(sf("NOLA cloot: corpo %s recontado no host (%s): ver=%lld lista=%s", hid.c_str(), why, E_.ver, s.c_str()));
            }
        }
        bool clTakeNat(Cl& C, const std::string& hid, ClE& E_, const std::string& list)
        {
            double now = NOW();
            Obj o = clGet(C, now, E_.ad, hid);
            if (!o) return false;
            Counts t, q;
            clCount(o, true, t, q);
            Counts have = clAdd(t, q), want = clDec(list);
            long long rm = 0, add = 0;
            bool bad = false;
            for (auto& kv : have)
            {
                long long w = g0(want, kv.first);
                if (kv.second <= w) continue;
                long long r = 0;
                if (E().removeItem(o, kv.first, kv.second - w, r)) rm += r;
                else bad = true;
            }
            if (bad)
            {
                LOG("NOLA cloot: retirada do cliente em " + hid + ": remocao nativa indisponivel no corpo real; usa o corpo de saque do host");
                return false;
            }
            auto u = E().guid(o);
            for (auto& kv : want)
            {
                long long h = g0(have, kv.first);
                if (kv.second > h && u)
                {
                    E().createItem(kv.first, *u, kv.second - h);
                    add += kv.second - h;
                }
            }
            clCount(o, true, t, q);
            E_.canon = t;
            E_.q = q;
            E_.str = clEnc(t);
            E_.ver += 1;
            E_.rc = now;
            C.s.tin += 1;
            C.s.hrm += rm;
            if (FON("cllog")) LOG(sf("NOLA cloot: retirada do cliente em %s feita no corpo real (clnat): tirados=%lld criados=%lld ver=%lld lista=%s", hid.c_str(), rm, add, E_.ver, clEnc(clAdd(t, q)).c_str()));
            return true;
        }
        void clTake(Cl& C, const std::string& hid, long long ver, const std::string& list)
        {
            auto f = C.K.find(hid);
            if (f == C.K.end() || ver != f->second.ver)
            {
                C.s.trej += 1;
                LOG(sf("NOLA cloot: retirada do cliente em %s RECUSADA (ver %lld, host %s): vale a lista do host", hid.c_str(), ver, f != C.K.end() ? lld(f->second.ver).c_str() : "sem corpo"));
                return;
            }
            ClE& E_ = f->second;
            if (clNatOn() && !E_.sw && clTakeNat(C, hid, E_, list)) return;
            E_.canon = clDec(list);
            E_.str = clEnc(E_.canon);
            E_.ver += 1;
            if (b164on())
            {
                E_.q.clear();
                for (auto it = E_.canon.begin(); it != E_.canon.end();)
                {
                    if (qs(it->first)) { E_.q[it->first] = it->second; it = E_.canon.erase(it); }
                    else ++it;
                }
                E_.str = clEnc(E_.canon);
            }
            C.s.tin += 1;
            if (!E_.sw)
            {
                E_.sw = true;
                LOG("NOLA cloot: corpo " + hid + " trocado: o real fica sem interacao, o host usa um corpo de saque com a lista comum");
            }
            if (FON("cllog")) LOG(sf("NOLA cloot: retirada do cliente em %s aceita: ver=%lld lista=%s", hid.c_str(), E_.ver, E_.str.c_str()));
        }
        void clClosed(Cl& C, double now, const std::string& hid, const std::string& kind)
        {
            if (kind == "c") { clRecount(C, hid, now, "janela fechada"); return; }
            auto bf = C.B.find(hid);
            if (bf == C.B.end()) return;
            ClB& B = bf->second;
            Obj o = clGet(C, now, B.ad, B.nm);
            if (!o) return;
            bool host = HOST();
            Counts t, q;
            clCount(o, host || b164on(), t, q);
            std::string s = clEnc(clAdd(t, q));
            if (s == B.hstr) return;
            B.hstr = s;
            if (host)
            {
                auto ef = C.K.find(hid);
                if (ef != C.K.end() && FI("clammo", 0) >= 1 && B.hasGv)
                {
                    std::set<std::string> Q;
                    for (auto& kv : ef->second.q) Q.insert(kv.first);
                    auto r = clRest(B.gv, clAdd(ef->second.canon, ef->second.q), t, q, &Q);
                    t = r.first;
                    q = r.second;
                    s = clEnc(clAdd(t, q));
                }
                if (ef != C.K.end())
                {
                    ClE& E_ = ef->second;
                    E_.canon = t;
                    E_.q = q;
                    E_.str = clEnc(t);
                    E_.ver += 1;
                    B.wstr = s;
                    B.ver = E_.ver;
                }
                if (FON("cllog")) LOG(sf("NOLA cloot: host tirou do corpo de saque %s: ver=%s lista=%s", hid.c_str(), ef != C.K.end() ? lld(ef->second.ver).c_str() : "?", s.c_str()));
            }
            else
            {
                if (FI("clammo", 0) >= 1 && B.hasGv)
                {
                    auto r = clRest(B.gv, clDec(B.wstr), t, q, nullptr);
                    s = clEnc(clAdd(r.first, r.second));
                    B.wstr = s;
                }
                C.seq += 1;
                ClTk tk{C.nonce + "." + lld(C.seq), hid, s, B.ver, now};
                C.T.push_back(tk);
                B.ver += 1;
                C.s.take += 1;
                if (FON("cllog")) LOG(sf("NOLA cloot: cliente tirou de %s: #CLT %s ver=%lld lista=%s", hid.c_str(), tk.id.c_str(), tk.ver, s.c_str()));
            }
        }
        bool clByClient(const Cl& C, const std::string& hid)
        {
            if (!MC()) return C.co && *C.co == hid;
            auto w = mcHolder(S().mc.co, hid);
            return w && *w != 0;
        }
        bool clByOther(const std::string& hid)
        {
            if (!MC()) return false;
            auto& m = S().mc.clh;
            auto f = m.find(hid);
            return f != m.end() && f->second != E().myPid();
        }
        void clLight(Cl& C, Obj lp, double now)
        {
            bool host = HOST();
            bool mc = MC();
            if (host && mc)
            {
                std::vector<std::pair<int, std::string>> claims;
                if (C.open) claims.push_back({0, *C.open});
                for (int v = 0; v < E().views(); ++v)
                {
                    int pid = E().viewPid(v);
                    std::string co;
                    PARTV("CO", co, v);
                    if (b164on() && E().natOn())
                    {
                        double rx, ry, rr;
                        bool hasR = E().remoteV(v, rx, ry, rr);
                        co = E().lkHold(sf("rco%d", pid), co, hasR ? now - rr : 1e9, F("cllease", 3), F("clholdmax", 120));
                    }
                    if (!co.empty()) claims.push_back({pid, co});
                }
                mcArb(S().mc.co, claims, false, "corpo");
                C.co.reset();
            }
            else if (host)
            {
                std::string co;
                if (PART("CO", co)) C.co = co; else C.co.reset();
                if (b164on() && E().natOn())
                {
                    double rx, ry, rr;
                    bool hasR = E().remote(rx, ry, rr);
                    std::string h = E().lkHold("rco", C.co.value_or(""), hasR ? now - rr : 1e9, F("cllease", 3), F("clholdmax", 120));
                    if (!h.empty()) C.co = h; else C.co.reset();
                }
            }
            if (host)
            {
                std::vector<std::string> raws;
                std::string raw;
                if (mc)
                {
                    for (int v = 0; v < E().views(); ++v)
                        if (PARTV("CLT", raw, v) && !raw.empty()) raws.push_back(raw);
                }
                else if (PART("CLT", raw) && !raw.empty()) raws.push_back(raw);
                for (auto& rw : raws)
                    for (auto& ent : splitNE(rw, ';'))
                    {
                        auto f = splitAll(ent, ',');
                        if (f.size() != 4) continue;
                        bool okId = !f[0].empty();
                        for (char c : f[0]) if (!(isD(c) || c == '.')) okId = false;
                        if (!okId || !allOf(f[1], isW) || !allOf(f[2], isD) || !clList(f[3])) continue;
                        if (C.done.count(f[0])) continue;
                        C.done[f[0]] = now;
                        clTake(C, f[1], (long long)numOr(f[2], 0), f[3]);
                    }
            }
            else
            {
                std::string raw;
                if (mc)
                {
                    std::map<std::string, int>& clh = S().mc.clh;
                    clh.clear();
                    if (PART("CLH", raw))
                        for (auto& h : mcHoldIn(raw)) clh[h.key] = h.pid;
                }
                bool present = PART("CL", raw);
                C.silent = !present;
                if (present && (!C.raw || raw != *C.raw))
                {
                    C.raw = raw;
                    std::set<std::string> seen;
                    for (auto& ent : splitNE(raw, ';'))
                    {
                        auto f = splitAll(ent, ',');
                        if (f.size() != 8) continue;
                        double x, y, z;
                        bool uidOk = !f[4].empty();
                        {
                            size_t i = (f[4].size() && f[4][0] == '-') ? 1 : 0;
                            if (i >= f[4].size()) uidOk = false;
                            for (; i < f[4].size(); ++i) if (!isD(f[4][i])) uidOk = false;
                        }
                        if (!allOf(f[0], isW) || !fullNum(f[1], x) || !fullNum(f[2], y) || !fullNum(f[3], z) || !uidOk || !allOf(f[5], isD) || (f[6] != "0" && f[6] != "1") || !clList(f[7])) continue;
                        bool fresh = !C.R.count(f[0]);
                        ClR& R = C.R[f[0]];
                        if (fresh) R.first = now;
                        R.x = x;
                        R.y = y;
                        R.z = z;
                        R.uid = (long long)numOr(f[4], 0);
                        R.ver = (long long)numOr(f[5], 0);
                        R.o = f[6] == "1";
                        R.str = f[7].empty() ? std::string("-") : f[7];
                        R.gone.reset();
                        seen.insert(f[0]);
                    }
                    for (auto& kv : C.R) if (!kv.second.gone && !seen.count(kv.first)) kv.second.gone = now;
                }
                std::string xr;
                if (clGcOn() && PART("CLX", xr) && !xr.empty())
                    for (auto& h : splitNE(xr, ';'))
                        if (allOf(h, isW)) clGone(C, h, now);
                for (auto it = C.xdone.begin(); it != C.xdone.end();)
                    if (now - it->second > 120) it = C.xdone.erase(it); else ++it;
                double cg = F("clgone", 4);
                for (auto it = C.R.begin(); it != C.R.end();)
                    if (it->second.gone && now - *it->second.gone > cg) it = C.R.erase(it); else ++it;
                std::vector<ClTk> keep;
                for (auto& tk : C.T)
                {
                    auto R = C.R.find(tk.hid);
                    if (R != C.R.end() && R->second.ver > tk.ver) {}
                    else if (R == C.R.end() || now - tk.at > 20) LOG("NOLA cloot: retirada " + tk.id + " em " + tk.hid + " sem confirmacao do host (largada)");
                    else keep.push_back(tk);
                }
                C.T = keep;
            }
            auto ad = clOpenAddr(lp);
            std::optional<std::string> hid, kind;
            if (ad)
            {
                for (auto& kv : C.B) if (kv.second.ad == *ad && kv.second.st == "live") { hid = kv.first; kind = std::string("b"); }
                if (!hid && host) for (auto& kv : C.K) if (kv.second.ad == *ad) { hid = kv.first; kind = std::string("c"); }
            }
            if (C.open && (!hid || *C.open != *hid))
            {
                auto bf = C.B.find(*C.open);
                if (bf != C.B.end()) bf->second.open = false;
                C.cnt = ClCnt{*C.open, C.openK.value_or(""), now + 0.3};
                C.open.reset();
                C.openK.reset();
            }
            if (hid && (!C.open || *C.open != *hid))
            {
                bool other = (host && clByClient(C, *hid));
                if (!host) { auto R = C.R.find(*hid); other = (R != C.R.end() && R->second.o) || clByOther(*hid); }
                if (other) clClose(lp, C, *hid, "o outro jogador ja esta com ele aberto");
                else
                {
                    C.open = hid;
                    C.openK = kind;
                    if (*kind == "b") C.B[*hid].open = true;
                }
            }
            else if (hid && !host && C.R.count(*hid) && (C.R[*hid].o || clByOther(*hid)))
            {
                clClose(lp, C, *hid, C.R[*hid].o ? "aberto ao mesmo tempo no host; o cliente cede" : "aberto ao mesmo tempo por outro jogador; o host deu a ele");
                auto bf = C.B.find(*hid);
                if (bf != C.B.end()) bf->second.open = false;
                C.cnt = ClCnt{*hid, kind.value_or(""), now + 0.3};
                C.open.reset();
                C.openK.reset();
            }
            if (C.cnt && now >= C.cnt->at)
            {
                ClCnt c = *C.cnt;
                C.cnt.reset();
                clClosed(C, now, c.hid, c.kind);
            }
            if (C.pend) clStep(C, lp, now);
        }
        bool clNatEmpty(Cl& C, const std::string& hk, Obj o, const std::string& hid)
        {
            Counts c;
            bool ok = E().natItems(o, 0, false, c);
            if (ok && c.empty()) return true;
            if (C.left.size() > 512) C.left.erase(C.left.begin());
            if (C.left.insert(hk).second)
                LOG("NOLA cloot: corpo " + hid + " sem itens da lista mas " + (ok ? "ainda com " + clEnc(c) + " (leitura nativa)" : std::string("sem leitura nativa")) + ": nao conta como vazio, a interacao fica");
            return false;
        }
        void clHostScan(Cl& C, Obj lp, double now)
        {
            double rx = 0, ry = 0, rr = -1e9;
            bool hasR = E().remote(rx, ry, rr);
            bool rok = hasR && (now - rr) < 15;
            std::vector<std::pair<double, double>> rps;
            bool mc = MC();
            if (mc)
            {
                for (int v = 0; v < E().views(); ++v)
                {
                    double vx, vy, vr;
                    if (E().remoteV(v, vx, vy, vr) && now - vr < 15) rps.push_back({vx, vy});
                }
                rok = !rps.empty();
            }
            double send = F("clsend", 4000), send2 = sq(send);
            std::set<std::string> seen, dseen;
            int nNew = 0;
            bool gc = clGcOn();
            if (clNatOn() && b164on() && E().natOn()) E().lkOff(0, 0, F("cllease", 3));
            for (Obj o : E().all("Pawn"))
            {
                if (!VAL(o) || o == lp) continue;
                bool alive = aliveOr(o, true);
                if (alive || E().isOwnBody(o)) continue;
                std::string full = E().full(o);
                if (has(full, "Stalker2Character")) continue;
                V3 q;
                if (!E().loc(o, q))
                {
                    dseen.insert(E().name(o));
                    continue;
                }
                if (std::fabs(q.x) < 100 && std::fabs(q.y) < 100) continue;
                std::string hid = E().name(o);
                dseen.insert(hid);
                uint64_t ad = E().addr(o);
                auto f = C.K.find(hid);
                if (f != C.K.end() && f->second.ad != ad) { C.K.erase(f); f = C.K.end(); }
                double d2 = rok ? sq(q.x - rx) + sq(q.y - ry) : 1e18;
                if (mc)
                {
                    d2 = 1e18;
                    for (auto& p : rps) d2 = std::min(d2, sq(q.x - p.first) + sq(q.y - p.second));
                }
                if (f == C.K.end())
                {
                    if (d2 > send2 || nNew >= 2) continue;
                    std::string lk = addrKey(ad, hid, ":");
                    auto lb = C.lsb.find(lk);
                    if (lb == C.lsb.end())
                        lb = C.lsb.emplace(lk, has(full, "BP_AI_Human_C") && lsBody(sf("BP_AI_Human_C@%.0f,%.0f,%.0f", q.x / 10, q.y / 10, q.z / 10)).has_value()).first;
                    if (lb->second) continue;
                    ClE e;
                    e.ad = ad;
                    e.uid = E().npcUid(ad, hid).value_or(0);
                    e.ver = 1;
                    clCount(o, true, e.canon, e.q);
                    e.str = clEnc(e.canon);
                    e.rc = now;
                    f = C.K.emplace(hid, e).first;
                    nNew += 1;
                    C.s.rc += 1;
                    if (FON("cllog") && e.str != "-") LOG(sf("NOLA cloot: corpo %s listado para o cliente (uid=%lld) lista=%s", hid.c_str(), e.uid, e.str.c_str()));
                }
                ClE& E_ = f->second;
                E_.x = q.x;
                E_.y = q.y;
                E_.z = q.z;
                E_.d2 = d2;
                E_.hasD2 = true;
                seen.insert(hid);
                if (gc) C.L[hid] = ad;
                std::string hk = addrKey(ad, hid, ":");
                bool nonEmpty = clEnc(clAdd(E_.canon, E_.q)) != "-";
                if (nonEmpty) C.had.insert(hk);
                bool emptied = !nonEmpty && F("clnoint", 1) == 1 && C.had.count(hk) > 0 && clNatEmpty(C, hk, o, hid);
                if (C.had.size() > 512) C.had.erase(C.had.begin());
                if (E_.sw || clByClient(C, hid))
                {
                    E().setInteraction(o, false);
                    E_.off = true;
                    if (b164on() && E().natOn()) E().lkOff(ad, 1, F("cllease", 3));
                }
                else if (emptied)
                {
                    E().setInteraction(o, false);
                    E_.off = true;
                }
                else if (E_.off)
                {
                    E().setInteraction(o, true);
                    E_.off = false;
                }
            }
            for (auto it = C.K.begin(); it != C.K.end();)
            {
                if (!seen.count(it->first) || (!it->second.sw && it->second.d2 > sq(2 * send))) it = C.K.erase(it);
                else ++it;
            }
            if (gc)
            {
                for (auto it = C.L.begin(); it != C.L.end();)
                {
                    if (dseen.count(it->first))
                    {
                        ++it;
                        continue;
                    }
                    Obj o = clGet(C, now, it->second, it->first);
                    if (!o || !aliveOr(o, false))
                    {
                        C.X[it->first] = now;
                        if (FON("cllog")) LOG("NOLA cloot: corpo " + it->first + " saiu do mundo do host: os clientes tiram o deles (#CLX)");
                    }
                    it = C.L.erase(it);
                }
                while (C.L.size() > 256) C.L.erase(C.L.begin());
                for (auto it = C.X.begin(); it != C.X.end();)
                    if (now - it->second > 20) it = C.X.erase(it); else ++it;
            }
            std::optional<std::string> best;
            double bt = 0;
            double clrc = F("clrc", 5);
            for (auto& kv : C.K)
            {
                const ClE& e = kv.second;
                if (!e.sw && e.d2 <= send2 && (!C.open || *C.open != kv.first) && now - e.rc >= clrc && (!best || e.rc < bt)) { best = kv.first; bt = e.rc; }
            }
            if (!clNatOn() && b164on() && E().natOn()) E().lkOff(0, 0, F("cllease", 3));
            if (best) clRecount(C, *best, now, "periodico");
        }
        std::map<std::string, ClW> clWant(Cl& C)
        {
            std::map<std::string, ClW> w;
            if (HOST())
            {
                for (auto& kv : C.K)
                    if (kv.second.sw) w[kv.first] = ClW{kv.second.x, kv.second.y, kv.second.z, clEnc(clAdd(kv.second.canon, kv.second.q)), kv.second.ver, clByClient(C, kv.first)};
            }
            else
            {
                std::set<std::string> tk;
                for (auto& t : C.T) tk.insert(t.hid);
                for (auto& kv : C.R)
                {
                    const ClR& R = kv.second;
                    w[kv.first] = ClW{R.x, R.y, R.z, R.str, R.ver, R.o || C.silent || R.gone.has_value() || tk.count(kv.first) != 0 || clByOther(kv.first)};
                }
            }
            return w;
        }
        void clBodies(Cl& C, Obj lp, double now)
        {
            auto w = clWant(C);
            V3 me;
            E().loc(lp, me);
            auto d2 = [&](const ClW& W) { return sq(W.x - me.x) + sq(W.y - me.y); };
            std::vector<std::string> keys;
            for (auto& kv : C.B) keys.push_back(kv.first);
            for (auto& hid : keys)
            {
                auto bf = C.B.find(hid);
                if (bf != C.B.end() && bf->second.nat && bf->second.st != "live")
                {
                    clNatStep(C, hid, bf->second, now);
                    continue;
                }
                if (bf == C.B.end() || bf->second.st != "live") continue;
                ClB& B = bf->second;
                auto wf = w.find(hid);
                const ClW* W = wf == w.end() ? nullptr : &wf->second;
                const char* why = nullptr;
                if (!W) why = "fora da lista";
                else if (W->str == "-" || !clVis(W->str)) why = "vazio";
                else if (d2(*W) > sq(F("clr", 2500) + 1500)) why = "longe";
                else if (W->str != B.wstr && W->str != B.hstr && !W->block) why = "lista mudou; refeito";
                if (why && B.nat && (!C.open || *C.open != hid) && (std::string(why) == "vazio" || std::string(why) == "lista mudou; refeito"))
                {
                    Obj o = clGet(C, now, B.ad, B.nm);
                    if (!o)
                    {
                        E().natClOwn(B.ad, false);
                        C.B.erase(hid);
                        continue;
                    }
                    E().setInteraction(o, false);
                    if (std::string(why) == "vazio")
                    {
                        if (W->str == "-" && B.hstr != "-")
                        {
                            bool bad = false;
                            auto r = clNatApply(o, B, "-", true, bad);
                            C.s.rmv += r.first;
                            Counts t, q;
                            clCount(o, true, t, q);
                            B.hstr = clEnc(clAdd(t, q));
                            B.wstr = "-";
                            B.ver = std::max(B.ver, W->ver);
                            if (FON("cllog")) LOG("NOLA cloot: corpo nativo de " + hid + " esvaziado como no host: fica no mundo sem interacao");
                        }
                        continue;
                    }
                    bool bad = false;
                    B.wstr = W->str;
                    B.ver = W->ver;
                    B.st = "n1";
                    B.at = now;
                    auto r = clNatApply(o, B, W->str, false, bad);
                    C.s.rmv += r.first;
                    if (FON("cllog")) LOG(sf("NOLA cloot: corpo nativo de %s acompanha a lista nova do host: tirados=%lld criados=%lld lista=%s", hid.c_str(), r.first, r.second, W->str.c_str()));
                    continue;
                }
                if (why && (!C.open || *C.open != hid)) clDrop(C, hid, now, why);
                else
                {
                    if (W && W->str == B.hstr) { B.wstr = W->str; B.ver = std::max(B.ver, W->ver); }
                    Obj o = clGet(C, now, B.ad, B.nm);
                    if (!o)
                        C.B.erase(hid);
                    else
                    {
                        bool on = W && !W->block && W->str != "-" && (W->str == B.wstr || W->str == B.hstr);
                        E().setInteraction(o, on);
                        if (!B.nat)
                        {
                            bool hide = clMeshHide(hid);
                            bool vis = false;
                            bool vok = E().meshVisible(o, vis);
                            B.mh = hide;
                            if (vok && vis == hide) E().setMeshVisible(o, !hide);
                        }
                    }
                }
            }
            bool gate = (!FON("bodysafe") || E().worldOpen()) && !E().bodySpawning();
            if (!gate) return;
            int nb = 0;
            for (auto& kv : C.B) if (!kv.second.nat) ++nb;
            bool canSpawn = !C.pend && now >= C.gap && (double)nb < F("clmax", 6);
            bool nat = !HOST() && clNatOn();
            if (!canSpawn && !nat) return;
            std::vector<std::pair<double, std::string>> cand;
            double clr2 = sq(F("clr", 2500));
            for (auto& kv : w)
            {
                const ClW& W = kv.second;
                auto fl = C.fail.find(kv.first);
                if (!C.B.count(kv.first) && W.str != "-" && !W.block && clVis(W.str) && now >= (fl == C.fail.end() ? 0 : fl->second))
                {
                    double d = d2(W);
                    if (d < clr2) cand.push_back({d, kv.first});
                }
            }
            std::stable_sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            int adopted = 0;
            for (auto& c : cand)
            {
                const std::string& hid = c.second;
                if (nat && !C.natNo.count(hid))
                {
                    if (adopted >= 2) continue;
                    bool wait = false;
                    Obj o = clNative(C, hid, now, wait);
                    if (o && clAdopt(C, hid, w[hid], o, now))
                    {
                        ++adopted;
                        continue;
                    }
                    if (wait) continue;
                }
                if (canSpawn)
                {
                    clSpawn(C, hid, w[hid], now);
                    return;
                }
            }
        }
        void clCopy(Cl& C, Obj lp, double now)
        {
            std::vector<std::string> todo, tw0;
            for (auto& kv : C.R)
            {
                const ClR& R = kv.second;
                if (R.uid > 0 && !C.cp.count(kv.first) && !R.gone && todo.size() < 2) todo.push_back(kv.first);
                if (R.uid == 0 && !C.cp.count(kv.first) && !R.gone && tw0.size() < 2 && b164on()) tw0.push_back(kv.first);
            }
            if (todo.empty() && tw0.empty()) return;
            int calls = 0;
            std::vector<Obj> live, dead;
            bool nat = clNatOn();
            for (Obj o : E().all("Pawn"))
            {
                if (!VAL(o) || o == lp || E().isOwnBody(o)) continue;
                bool al;
                if (!E().alive(o, al)) continue;
                if (!al)
                {
                    if ((!tw0.empty() || (nat && !todo.empty())) && !has(E().full(o), "Stalker2Character")) dead.push_back(o);
                    continue;
                }
                live.push_back(o);
            }
            if (!C.tw) C.tw.emplace();
            for (auto& hid : tw0)
            {
                ClR& R = C.R[hid];
                Obj hit = 0;
                for (Obj o : dead)
                {
                    if (hit) break;
                    V3 q;
                    if (E().loc(o, q) && sq(q.x - R.x) + sq(q.y - R.y) <= sq(150) && std::fabs(q.z - R.z) <= 250) hit = o;
                }
                R.twTry += 1;
                if (hit)
                {
                    (*C.tw)[hid] = E().addr(hit);
                    E().setInteraction(hit, false);
                    C.cp[hid] = "twin";
                    C.s.copy += 1;
                    LOG("NOLA cloot: corpo gemeo do cliente em " + hid + " (" + E().name(hit) + "): sem interacao; o corpo de saque veste ele (b164)");
                }
                else if (R.twTry >= 3) C.cp[hid] = "none";
            }
            for (auto& hid : todo)
            {
                ClR& R = C.R[hid];
                Obj hit = 0;
                if (nat)
                {
                    for (Obj o : dead)
                    {
                        if (hit) break;
                        V3 q;
                        if (!E().loc(o, q) || sq(q.x - R.x) + sq(q.y - R.y) > sq(5000)) continue;
                        uint64_t ad = E().addr(o);
                        std::optional<long long> u = E().npcUid(ad, E().name(o));
                        if ((!u || *u == 0) && calls < 12)
                        {
                            calls += 1;
                            auto g = E().guid(o);
                            if (g) u = g;
                        }
                        if (u && *u == R.uid) hit = o;
                    }
                    if (hit)
                    {
                        (*C.tw)[hid] = E().addr(hit);
                        E().setInteraction(hit, false);
                        C.cp[hid] = "twin";
                        C.s.copy += 1;
                        LOG(sf("NOLA cloot: corpo morto local de %s (uid %lld, %s) e o corpo deste cliente (clnat)", hid.c_str(), R.uid, E().name(hit).c_str()));
                        continue;
                    }
                }
                for (Obj o : live)
                {
                    if (hit) break;
                    V3 q;
                    if (!E().loc(o, q)) continue;
                    if (sq(q.x - R.x) + sq(q.y - R.y) > sq(5000)) continue;
                    uint64_t ad = E().addr(o);
                    std::optional<long long> u = E().npcUid(ad, E().name(o));
                    if ((!u || *u == 0) && calls < 12)
                    {
                        calls += 1;
                        auto g = E().guid(o);
                        if (g) u = g;
                    }
                    if (u && *u == R.uid) hit = o;
                }
                R.cpTry += 1;
                if (hit)
                {
                    bool story = E().storyNpc(hit);
                    if (!story) E().killUid(R.uid);
                    int nst = 0;
                    uint64_t nb = nat && E().natOn() ? E().natBody(hid, nst) : 0;
                    if (story || !b164on() || (nb && nb != E().addr(hit))) clInert(hit);
                    else
                    {
                        (*C.tw)[hid] = E().addr(hit);
                        E().setInteraction(hit, false);
                    }
                    C.cp[hid] = story ? "story" : "killed";
                    C.s.copy += 1;
                    LOG(sf("NOLA cloot: copia local de %s (uid %lld) %s e inerte", hid.c_str(), R.uid, story ? "da historia: viva" : "morta"));
                }
                else if (R.cpTry >= 3) C.cp[hid] = "none";
            }
        }
        bool clGuard(Cl& C, uint64_t ad)
        {
            for (auto& kv : C.B) if (kv.second.nat && kv.second.ad == ad) return true;
            for (auto& kv : C.M) if (kv.second.ad == ad && !C.own.count(ad)) return true;
            if (C.tw) for (auto& kv : *C.tw) if (kv.second == ad) return true;
            return false;
        }
        void clSweep(Cl& C)
        {
            long long n = 0;
            long long k = 0;
            Obj tp = E().targetPawn();
            for (Obj o : E().all("BP_AI_Human_C"))
            {
                if (!VAL(o) || o == tp) continue;
                bool al;
                if (!E().alive(o, al) || al) continue;
                double mh = 0;
                if (!E().maxHp(o, mh) || std::fabs(mh - 5000) > 0.5) continue;
                uint64_t ad = E().addr(o);
                std::string nm = E().name(o);
                auto f = C.own.find(ad);
                if (f != C.own.end() && f->second == nm) continue;
                if (clGuard(C, ad)) continue;
                clInert(o);
                C.own[ad] = nm;
                ++n;
                if (F("clsweepdel", 1) == 1)
                {
                    auto u = E().guid(o);
                    if (u && E().destroyModel(*u)) ++k;
                }
            }
            C.s.inert += n;
            if (n > 0 && F("clsweepdel", 1) == 1) LOG("NOLA cloot: " + lld(n) + " corpo(s) de saque de antes (save) deixados inertes; " + lld(k) + " destruidos (XDestroyModel)");
            else if (n > 0) LOG("NOLA cloot: " + lld(n) + " corpo(s) de saque de antes (save) deixados inertes");
        }
        void clLate(Cl& C, double now)
        {
            auto& L = *C.late;
            for (size_t i = L.size(); i-- > 0;) if (now > L[i].to) L.erase(L.begin() + (long)i);
            if (L.empty()) { C.late.reset(); return; }
            if (E().bodySpawning()) return;
            long long n = 0;
            Obj tp = E().targetPawn(), pp = E().puppetPawn();
            for (Obj o : E().all("BP_AI_Human_C"))
            {
                if (!VAL(o) || o == tp || o == pp) continue;
                uint64_t ad = E().addr(o);
                std::string nm = E().name(o);
                auto f = C.own.find(ad);
                if (f != C.own.end() && f->second == nm) continue;
                if (clGuard(C, ad)) continue;
                double mh = 0;
                if (!E().maxHp(o, mh) || mh < 4000 || mh > 6000) continue;
                std::string k = addrKey(ad, nm, "|");
                V3 q;
                if (!E().loc(o, q)) continue;
                for (auto& e : L)
                {
                    if (!e.before.count(k) && sq(q.x - e.x) + sq(q.y - e.y) < sq(800))
                    {
                        clInert(o);
                        C.own[ad] = nm;
                        ++n;
                        auto u = E().guid(o);
                        if (u) E().destroyModel(*u);
                        LOG("NOLA cloot: corpo de saque tardio de " + e.hid + " (" + nm + " maxhp=" + luaF(mh) + ") deixado inerte e destruido");
                        break;
                    }
                }
            }
            C.s.inert += n;
        }
        void clHeavy(Cl& C, Obj lp, double now)
        {
            if (HOST()) clHostScan(C, lp, now);
            else if (FON("clcopy")) clCopy(C, lp, now);
            clBodies(C, lp, now);
            clZTick(C, now);
            clGcFeed(C, now);
            if (!HOST() && b164on() && E().natOn()) E().clSwapTick();
            for (auto it = C.done.begin(); it != C.done.end();)
                if (now - it->second > 120) it = C.done.erase(it); else ++it;
            if (C.late && !C.pend) clLate(C, now);
            if (!C.pend && (!FON("bodysafe") || E().worldOpen()) && C.swN < 2)
            {
                if (!C.swAt) C.swAt = now + 5;
                if (now >= *C.swAt)
                {
                    clSweep(C);
                    C.swN += 1;
                    C.swAt = now + 30;
                }
            }
        }
        void clOff(Cl& C)
        {
            double now = NOW();
            std::vector<std::string> keys;
            for (auto& kv : C.B) keys.push_back(kv.first);
            for (auto& h : keys) clDrop(C, h, now, "cloot=0");
            while (!C.Z.empty()) clZFree(C, C.Z.begin(), now);
            for (auto& kv : C.K)
                if (kv.second.off)
                {
                    Obj o = clGet(C, now, kv.second.ad, kv.first);
                    if (o) E().setInteraction(o, true);
                }
            if (!C.gcLast.empty() && C.gcLast != "0") E().gcFeed(0, {});
            C.map.clear();
            C.mapOk = false;
            S().cl.reset();
            LOG("NOLA cloot: desligado; corpos de saque largados");
        }
        void clStats(Cl& C, double now)
        {
            ClStat& s = C.s;
            if (FON("cllog") && (s.spawn + s.drop + s.take + s.tin + s.trej + s.deny + s.copy + s.inert + s.fail > 0 || !C.B.empty()))
                LOG(sf("NOLA cloot 10 s (%s): entradas=%d corpos=%d pedidos=%lld prontos=%lld largados=%lld falhas=%lld retiradas=%lld aceitas=%lld recusadas=%lld recontagens=%lld negados=%lld copias=%lld inertes=%lld ms=%.1f",
                       ROLE(), (int)(HOST() ? C.K.size() : C.R.size()), (int)C.B.size(), s.spawn, s.live, s.drop, s.fail, s.take, s.tin, s.trej, s.rc, s.deny, s.copy, s.inert, s.ms) +
                    (s.nat + s.rmv + s.gone + s.hrm > 0 || !C.M.empty() || !C.X.empty() ? sf(" nativos=%lld tirados=%lld tirados_host=%lld sumidos=%lld guardados=%d removidos_host=%d %s", s.nat, s.rmv, s.hrm, s.gone, (int)C.M.size(), (int)C.X.size(), E().gcStat().c_str()) : std::string()));
            C.s = ClStat();
            C.s.at = now;
        }
        void clTick()
        {
            State& st = S();
            if (F("cloot", 0) != 1)
            {
                if (st.cl)
                {
                    try { clOff(*st.cl); }
                    catch (const std::exception&) {}
                    st.cl.reset();
                }
                return;
            }
            Obj lp = E().pawn();
            if (!VAL(lp)) return;
            Cl& C = clSt();
            double now = NOW();
            if (now < C.lt) return;
            C.lt = now + 0.2;
            bool ok = true;
            std::string err;
            try { clLight(C, lp, now); }
            catch (const std::exception& e) { ok = false; err = e.what(); }
            if (ok && now >= C.ht)
            {
                C.ht = now + 1;
                try { clHeavy(C, lp, now); }
                catch (const std::exception& e) { ok = false; err = e.what(); }
            }
            C.map.clear();
            C.mapOk = false;
            if (!ok)
            {
                C.err += 1;
                if (C.err <= 5) LOG("NOLA cloot erro: " + err);
            }
            C.s.ms += (NOW() - now) * 1000;
            if (now - C.s.at >= 10) clStats(C, now);
        }
        std::string clOutI()
        {
            State& st = S();
            if (F("cloot", 0) != 1 || !st.cl) return "";
            Cl& C = *st.cl;
            double send2 = sq(F("clsend", 4000));
            if (HOST())
            {
                std::vector<std::pair<std::string, const ClE*>> l;
                for (auto& kv : C.K) if (kv.second.hasD2 && kv.second.d2 <= send2) l.push_back({kv.first, &kv.second});
                std::stable_sort(l.begin(), l.end(), [](const auto& a, const auto& b) { return a.second->d2 < b.second->d2; });
                bool hk = !b164on() || !E().natOn() || !E().lkHold("lopen", C.open.value_or(""), 0, F("cllease", 3), F("clholdmax", 120)).empty();
                std::vector<std::string> t;
                for (size_t i = 0; i < std::min<size_t>(l.size(), 12); ++i)
                {
                    const std::string& hid = l[i].first;
                    const ClE& e = *l[i].second;
                    int o = (C.open && *C.open == hid && hk) ? 1 : 0;
                    std::string list = b164on() ? clEnc(clAdd(e.canon, e.q)) : e.str;
                    t.push_back(sf("%s,%.0f,%.0f,%.0f,%lld,%lld,%d,%s", hid.c_str(), e.x, e.y, e.z, e.uid, e.ver, o, list.c_str()));
                }
                std::string x;
                if (clGcOn() && !C.X.empty())
                {
                    std::vector<std::string> xs;
                    for (auto& kv : C.X) xs.push_back(kv.first);
                    x = "#CLX:" + join(xs, ";");
                }
                return "#CL:" + join(t, ";") + x;
            }
            std::string s;
            if (C.open && C.openK && *C.openK == "b") s = "#CO:" + *C.open;
            if (!C.T.empty())
            {
                std::vector<std::string> t;
                for (auto& tk : C.T) t.push_back(sf("%s,%s,%lld,%s", tk.id.c_str(), tk.hid.c_str(), tk.ver, tk.str.c_str()));
                s += "#CLT:" + join(t, ";");
            }
            return s;
        }

        template <class Fn> void guarded(const char* name, int& errs, const char* sep, Fn fn)
        {
            try { fn(); }
            catch (const std::exception& e)
            {
                errs += 1;
                if (errs <= 5) LOG(std::string("NOLA ") + name + sep + e.what());
            }
        }
        std::string cuKey;

        bool qlHead(const std::string& t, long long& num, size_t& at)
        {
            if (t.size() < 3 || t.compare(0, 2, "QL") != 0) return false;
            size_t i = 2;
            while (i < t.size() && t[i] >= '0' && t[i] <= '9') ++i;
            if (i == 2 || i >= t.size() || t[i] != '~') return false;
            num = std::stoll(t.substr(2, i - 2));
            at = i + 1;
            return true;
        }
        long long qlNum(const std::string& t)
        {
            long long v = 0;
            size_t at = 0;
            if (!qlHead(t, v, at)) throw std::runtime_error("attempt to compare nil with number");
            return v;
        }
        std::string qlBody(const std::string& t)
        {
            long long v = 0;
            size_t at = 0;
            return qlHead(t, v, at) ? t.substr(at) : t;
        }
        bool digitsAt(const std::string& s, size_t& j)
        {
            size_t s0 = j;
            while (j < s.size() && s[j] >= '0' && s[j] <= '9') ++j;
            return j > s0;
        }

        void catchupInI()
        {
            if (F("qsync", 0) != 1 || !(!HOST() || F("qtest", 0) == 1) || !E().lookOn()) return;
            s2m_save::QState* Qp = E().cuQ();
            if (!Qp) return;
            s2m_save::QState& Q = *Qp;
            std::string sg;
            if (!PART("S", sg)) return;
            if (Q.cuStale && sg == *Q.cuStale && NOW() < Q.cuStaleAt + 3) return;
            size_t j0 = 0;
            if (!digitsAt(sg, j0) || j0 >= sg.size() || sg[j0] != ',' || j0 + 1 >= sg.size()) return;
            std::string n = sg.substr(0, j0), hs = sg.substr(j0 + 1);
            long long cutRows = 0;
            {
                size_t lc = hs.rfind(',');
                if (lc != std::string::npos)
                {
                    size_t jc = lc + 1;
                    if (digitsAt(hs, jc) && jc == hs.size() && jc > lc + 1) cutRows = std::stoll(hs.substr(lc + 1));
                    hs.resize(lc);
                }
            }
            std::string pid = E().peerId();
            std::string key = (pid.empty() ? std::string("?") : pid) + "/" + n;
            if (Q.cuDone && cuKey == key) return;
            if (Q.rbWait && !E().cuRbReady()) return;
            if (Q.csWait) return;
            std::vector<std::string> texts;
            int missing = 0;
            for (auto& hsh : splitNE(hs, '.'))
            {
                std::string t;
                if (E().lookGet(hsh, t)) texts.push_back(t);
                else missing += 1;
            }
            if (missing > 0) return;
            if (texts.size() > 1) for (auto& t : texts) (void)qlNum(t);
            std::stable_sort(texts.begin(), texts.end(), [](const std::string& x, const std::string& y) { return qlNum(x) < qlNum(y); });
            int okN = 0, skip = 0, had = 0;
            std::map<std::string, int>* app = E().cuApplied();
            if (!app) return;
            std::map<std::string, int>& ap = *app;
            std::map<std::string, int> seen;
            bool rolled = false, stale = false;
            if (F("csave", 0) == 1)
            {
                long long have = 0, nlog = 0;
                for (auto& kv : ap) have += kv.second;
                for (auto& t : texts) nlog += (long long)splitNE(qlBody(t), ';').size();
                std::string B;
                std::optional<std::string> cid;
                std::optional<long long> bn;
                if (PART("B", B))
                {
                    size_t j = 0;
                    if (digitsAt(B, j) && j < B.size() && B[j] == ',')
                    {
                        size_t s1 = ++j;
                        if (digitsAt(B, j))
                        {
                            bn = std::stoll(B.substr(s1, j - s1));
                            if (j < B.size() && B[j] == ',')
                            {
                                size_t s2 = ++j;
                                while (j < B.size() && (std::isalnum((unsigned char)B[j]) != 0)) ++j;
                                if (j > s2) cid = B.substr(s2, j - s2);
                            }
                        }
                    }
                }
                if (bn && nlog < *bn)
                {
                    stale = true;
                    LOG(sf("NOLA quest: catch-up antigo (%lld eventos, o host ja tem %lld): so o que falta, sem decidir rollback", nlog, *bn));
                }
                else if (cid && Q.apCid && *cid != *Q.apCid && have > 0)
                {
                    LOG(sf("NOLA csave: client_applied.txt e da campanha %s e o host esta na %s: registro descartado; tudo sera aplicado", Q.apCid->c_str(), cid->c_str()));
                    ap.clear();
                    Q.csAhead.reset();
                }
                else if (have > nlog + cutRows && (Q.csRb || (cid && Q.apCid && *cid == *Q.apCid))) rolled = true;
                else if (have > nlog + cutRows)
                {
                    LOG(sf("NOLA csave: client_applied.txt tem %lld eventos e o log do host so %lld: e de outra campanha; tudo sera aplicado", have, nlog));
                    ap.clear();
                    Q.csAhead.reset();
                }
                if (cid) Q.apCid = cid;
            }
            for (auto& t : texts)
            {
                for (auto& e : splitNE(qlBody(t), ';'))
                {
                    int sc = ++seen[e];
                    auto f = ap.find(e);
                    int have = f == ap.end() ? 0 : f->second;
                    if (F("qonce", 0) == 1 && sc <= have) had += 1;
                    else
                    {
                        int r = E().cuApply(e);
                        if (r == 1) okN += 1;
                        else skip += 1;
                        ap[e] = std::max(have, sc);
                        Q.csDirty = true;
                        Q.csAt = NOW();
                    }
                }
            }
            if (rolled)
            {
                int na = 0;
                for (auto it = ap.begin(); it != ap.end();)
                {
                    auto sv = seen.find(it->first);
                    int sn = sv == seen.end() ? 0 : sv->second;
                    int d = it->second - sn;
                    if (d > 0)
                    {
                        if (!Q.csAhead) Q.csAhead.emplace();
                        (*Q.csAhead)[it->first] = d;
                        na += d;
                        if (sv == seen.end())
                        {
                            it = ap.erase(it);
                            continue;
                        }
                        it->second = sn;
                    }
                    ++it;
                }
                Q.csDirty = true;
                LOG("NOLA csave: o host voltou no tempo e o cliente esta " + std::to_string(na) + " eventos adiante; nada reaplicado, esses eventos serao pulados quando o host refizer");
            }
            if (!stale)
            {
                Q.csRb = false;
                Q.apFresh = false;
            }
            Q.cuDone = true;
            cuKey = key;
            Q.rx += okN + skip + had;
            std::string hadS = had > 0 ? (", " + std::to_string(had) + " ja aplicados antes") : std::string();
            LOG(sf("NOLA quest: catch-up aplicado: %d eventos aplicados, %d sem rota%s (log do host com %s eventos)", okN, skip, hadS.c_str(), n.c_str()));
            if (Q.rbWait) E().cuRbDone("catch-up refeito");
        }
#include "s2m_items_qg.inc"
        template <class Fn> std::string outGuard(Fn fn)
        {
            try { return fn(); }
            catch (const std::exception&) { return ""; }
        }
    }

    void bind(Env* env) { g_env = env; }
    Env* env() { return g_env; }
    void reset()
    {
        delete g_st;
        g_st = nullptr;
    }
    void* swapState(void* st)
    {
        State* o = g_st;
        g_st = static_cast<State*>(st);
        return o;
    }

    void roleChanged()
    {
        if (!g_env || !g_st) return;
        Taken& T = S().tk;
        T.loaded = false;
        T.mineLoaded = false;
        T.mine.clear();
        gClear();
        dropClear("role changed");
        S().qg.loaded = false;
        qgReset("papel mudou");
        LOG(std::string("NOLA TAKEN: role is now ") + ROLE() + ": the taken list reloads for this role");
        if (F("rolereset", 1) != 1) return;
        struct RoleState
        {
            const char* name;
            void (*reset)(State&);
        };
        static const RoleState kRoleState[] = {
            {"stash", [](State& s) { s.ss = Ss{}; }},
            {"art2", [](State& s) { s.ap = Ap{}; }},
            {"ptake", [](State& s) { s.pt = Pt{}; }},
        };
        std::string names;
        for (const RoleState& r : kRoleState)
        {
            r.reset(S());
            names += std::string(names.empty() ? "" : ", ") + r.name;
        }
        LOG(std::string("NOLA items: role is now ") + ROLE() + ": " + names + " state reloads from this role's files (rolereset=1)");
    }

    void campaignRotated()
    {
        if (!g_env || !HOST()) return;
        tkReset("campanha nova do host");
    }

    void onPeer()
    {
        if (!g_env) return;
        State& st = S();
        bool silent = E().peerSilent();
        if (silent && !st.peerWasSilent) st.lock.denied.reset();
        st.peerWasSilent = silent;
        int n = E().peerSessions();
        if (st.peerSessions < 0) st.peerSessions = n;
        else if (n != st.peerSessions)
        {
            st.peerSessions = n;
            st.it.seen.clear();
            st.it.pruneAt.reset();
            st.ss.seen.clear();
            st.ss.pruneAt.reset();
            st.lock.denied.reset();
        }
    }

    void tick100()
    {
        if (!g_env) return;
        State& st = S();
        onPeer();
        if (E().cuOn()) guarded("catchupIn", st.b6err, " erro: ", [] { catchupInI(); });
        guarded("itIn", st.b6err, " erro: ", [] { itIn(); });
        guarded("ssTick", st.b6err, " erro: ", [] { ssTick(); });
        guarded("artTick", st.b6err, " erro: ", [] { if (F("artsync", 0) == 2) apTick(); });
        guarded("itRun", st.b6err, " erro: ", [] { itRun(); });
        int dummy = 99;
        bool mc = MC();
        guarded("busyClaimTick", st.b6err, " erro ", [] { busyClaimTick(); });
        if (mc && HOST()) guarded("mcKTick", st.b6err, " erro ", [] { mcKTick(); });
        guarded("lockTick", dummy, " erro ", [] { lockTick(); });
        guarded("busyApplyTick", st.b6err, " erro ", [] { busyApplyTick(); });
        guarded("lootTick", st.lootErr, " erro ", [] { lootTick(); });
        guarded("dropTick", st.drop.err, " erro ", [] { dropTick(); });
        guarded("groundTick", st.gErr, " erro ", [] { groundTick(); });
        if (mc) guarded("mcGroundTick", st.gErr, " erro ", [] { mcGroundTick(); });
        else if (!st.mc.goff.empty() || !st.mc.kh.empty() || !st.mc.gh.empty() || !st.mc.co.empty())
        {
            mcGoffClear("sem hub multi");
            st.mc.kh.clear();
            st.mc.gh.clear();
            st.mc.co.clear();
            st.mc.clh.clear();
            st.mc.gc.reset();
        }
        guarded("ptakeTick", st.c22err, " erro ", [] { ptakeTick(); });
        try { lsTick(); }
        catch (const std::exception&) {}
        try { clTick(); }
        catch (const std::exception&) {}
        guarded("qgTick", st.qg.err, " erro ", [] { qgTick(); });
    }

    void catchupIn()
    {
        if (!g_env || !E().cuOn()) return;
        guarded("catchupIn", S().b6err, " erro: ", [] { catchupInI(); });
    }

    std::string outK()
    {
        if (!g_env) return "#K:";
        if (busyOn() && MC()) return "#K:" + S().mc.kclaim.value_or("");
        return "#K:" + S().lock.mine.value_or("");
    }
    std::string outLoot() { return g_env ? outGuard([] { return lootOutI(); }) : ""; }
    std::string outGround() { return g_env ? outGuard([] { return groundOutI(); }) : ""; }
    std::string outU() { return g_env ? outGuard([] { return itOutI(); }) : ""; }
    std::string outStash() { return g_env ? outGuard([] { return ssOutI(); }) : ""; }
    std::string outArt() { return g_env && F("artsync", 0) == 2 ? outGuard([] { return apOut(); }) : ""; }
    std::string outCl()
    {
        if (!g_env) return "";
        try { return clOutI(); }
        catch (const std::exception& e)
        {
            State& st = S();
            st.clOutErr += 1;
            if (st.clOutErr <= 5) LOG(std::string("NOLA cloot erro (x1): ") + e.what());
            return "";
        }
    }
    namespace
    {
        std::string relOut(const char* tag, std::vector<McRel>& L, double win)
        {
            double now = NOW();
            L.erase(std::remove_if(L.begin(), L.end(), [&](const McRel& r) { return now - r.t >= win; }), L.end());
            if (L.empty()) return "";
            std::vector<std::string> t;
            for (auto& r : L) t.push_back(r.s);
            return std::string("#") + tag + ":" + join(t, "|");
        }
        bool mcHostOut() { return g_env && HOST() && MC(); }
    }
    std::string outKH()
    {
        if (!mcHostOut() || !FON("locks") || S().mc.kh.empty()) return "";
        return "#KH:" + mcHoldOut(S().mc.kh);
    }
    std::string outGH()
    {
        if (!mcHostOut() || !FON("ground") || FI("mcgclaim", 1) != 1 || S().mc.gh.empty()) return "";
        return "#GH:" + mcHoldOut(S().mc.gh);
    }
    std::string outGC()
    {
        if (!g_env || HOST() || !MC() || !FON("ground") || FI("mcgclaim", 1) != 1 || !S().mc.gc) return "";
        return "#GC:" + *S().mc.gc;
    }
    std::string outIDR() { return g_env ? outGuard([] { return dropOutR(); }) : ""; }
    std::string outIDS() { return g_env ? outGuard([] { return dropOutS(); }) : ""; }
    std::string outIDU() { return g_env ? outGuard([] { return dropOutU(); }) : ""; }
    std::string outGR() { return mcHostOut() && FON("ground") ? relOut("GR", S().mc.gr, 4) : ""; }
    std::string outUR() { return mcHostOut() && FON("itake") ? relOut("UR", S().mc.ur, F("resend", 6)) : ""; }
    std::string outYR() { return mcHostOut() && FON("sstash") ? relOut("YR", S().mc.yr, 2) : ""; }
    std::string outCLH()
    {
        if (!mcHostOut() || F("cloot", 0) != 1 || S().mc.co.empty()) return "";
        return "#CLH:" + mcHoldOut(S().mc.co);
    }

    const std::vector<std::string>& lootSids()
    {
        static const std::vector<std::string> none;
        return g_env ? sids() : none;
    }
    bool isQuestItem(const std::string& sid) { return g_env && qs(sid); }
    bool qshareOn() { return g_env && FON("qshare") && qtOk(); }
    Counts invSnap(Obj pawn, long long* money)
    {
        long long m = 0;
        Counts t = invSnapI(pawn, m);
        if (money) *money = m;
        return t;
    }
    bool itemRemove(const std::string& kind, const std::string& a, const std::string& b, const std::string& c)
    {
        return itemRemoveI(kind, a, b, c, !c.empty());
    }
    bool itemSend(const std::string& kind, const std::string& a, const std::string& b, const std::string& c)
    {
        return itemSendI(kind, a, b, c, !c.empty());
    }
    std::optional<std::string> contSidId(const std::string& id) { return contSidOfId(id); }
    std::optional<std::string> contSidPos(double x, double y, double z) { return contSidXYZ(x, y, z); }
    bool artPlaceTaken(int place, const std::string& who) { return artTaken(place, who); }
    std::string stashNote(const std::string& ops) { return stashNoteI(ops); }
    bool runBusy() { return g_env && !S().it.run.empty(); }
    std::optional<std::string> lockMine() { return g_env ? S().lock.mine : std::nullopt; }
    bool stashOpen() { return g_env && S().ss.open.has_value(); }
    bool lootPending() { return g_env && S().cl && S().cl->pend.has_value(); }
    bool isLootBody(Obj o) { return g_env && clOwn(o); }
    bool lootBodySeen(Obj o)
    {
        if (!g_env || !clOwn(o)) return false;
        lootReshow(o);
        return true;
    }
    void lootReshow(Obj o)
    {
        if (!g_env) return;
        bool h = false;
        if (E().hidden(o, h) && h)
        {
            bool covered = false;
            if (F("natnpc", 0) == 1)
            {
                uint64_t ad = E().addr(o);
                bool mine = false;
                if (S().cl) for (auto& kv : S().cl->B) if (kv.second.ad == ad) mine = true;
                covered = !mine;
            }
            if (!covered)
            {
                E().setHidden(o, false);
                LOG("NOLA AVATAR dup: corpo de saque " + E().name(o) + " estava escondido; mostrado de novo (b141)");
            }
        }
    }
    std::string idropNow(const std::string& sid, long long n)
    {
        if (!g_env) return "idropnow: no env";
        Obj pw = E().pawn();
        V3 pp;
        if (!VAL(pw) || !E().loc(pw, pp)) return "idropnow: no pawn";
        std::vector<s2x_is::Inst> v;
        if (!E().natInst(pw, 0, false, v)) return "idropnow: inventory unreadable";
        long long left = n, done = 0;
        for (auto& in : v)
        {
            if (left <= 0) break;
            if (in.st.sid != sid || in.n <= 0) continue;
            long long take = std::min(left, in.n);
            if (!E().uiDrop(in.uid, take)) break;
            done += take;
            left -= take;
        }
        LOG(sf("NOLA idrop: devcmd dropped %lld of %s via OnSplitConfirmed", done, sid.c_str()));
        return sf("idropnow: dropped %lld of %lld %s", done, n, sid.c_str());
    }
    std::string status()
    {
        if (!g_env) return "items: sem env";
        State& st = S();
        std::string s = sf("items role=%s taken=%d mine=%d run=%d itSent=%lld itApplied=%lld lock=%s denies=%lld grants=%lld ledger=%d canon=%d cb=%d art2=%d invGuard=%lld qitem_eff=%lld clammo_eff=%lld",
                           ROLE(), (int)st.tk.list.size(), (int)st.tk.mine.size(), (int)st.it.run.size(), st.it.sent, st.it.applied, st.lock.mine.value_or("-").c_str(), st.lock.denies, st.lock.grants, (int)st.ss.led.size(), (int)st.ls.canon.size(), (int)st.ls.cb.size(), (int)st.ap.taken.size(), st.invGuard, FI("qitem", 0), FI("clammo", 0));
        s += " tkcid=" + (HOST() ? E().campaign() : st.tk.cid.value_or("-"));
        if (st.cl) s += sf(" cl: K=%d R=%d B=%d T=%d pend=%s", (int)st.cl->K.size(), (int)st.cl->R.size(), (int)st.cl->B.size(), (int)st.cl->T.size(), st.cl->pend ? st.cl->pend->hid.c_str() : "-");
        if (FON("idrop"))
        {
            const Drop& D = st.drop;
            s += sf(" idrop: reg=%d loc=%d claims=%d pre=%d registered=%lld spawned=%lld took=%lld lost=%lld fixed=%lld", (int)D.ents.size(), (int)D.loc.size(), (int)D.cl.size(), (int)D.pre.size(), D.nReg, D.nSpawn, D.nTake, D.nLose, D.nFix);
        }
        if (MC())
        {
            const Mc& M = st.mc;
            s += sf(" mc: pid=%d views=%d kh=%d gh=%d co=%d clh=%d goff=%d gdup=%lld uIn=%lld yIn=%lld grIn=%lld ur=%d yr=%d gr=%d", E().myPid(), E().views(), (int)M.kh.size(), (int)M.gh.size(),
                    (int)M.co.size(), (int)M.clh.size(), (int)M.goff.size(), M.gdup, M.uIn, M.yIn, M.grIn, (int)M.ur.size(), (int)M.yr.size(), (int)M.gr.size());
            if (M.hqHeld || M.hqParked)
            {
                long long n = 0;
                for (auto& kv : M.hq) n += (long long)kv.second.size();
                s += sf(" h1held=%d h1wait=%lld h1done=%lld", M.hqHeld ? 1 : 0, n, M.hqParked);
            }
        }
        return s;
    }
#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
    void census(std::vector<cen::Row>& out)
    {
        if (!g_env) return;
        State& st = S();
        std::map<std::string, std::pair<int, std::string>> per;
        for (auto& k : st.tk.list)
        {
            cen::Row r("taken", k);
            size_t at = k.find('@');
            r.s("kind", at == std::string::npos ? std::string("plain") : k.substr(0, at));
            std::string sid, items;
            if (keyCtake(k, sid, items))
            {
                r.s("cont", sid).s("items", items);
                auto& p = per[sid];
                p.first += 1;
                p.second += (p.second.empty() ? "" : ",") + items;
            }
            out.push_back(std::move(r));
        }
        for (auto& k : st.tk.mine) out.push_back(std::move(cen::Row("taken_mine", k)));
        for (auto& [sid, p] : per)
        {
            auto parts = cen::split(p.second, ',');
            std::sort(parts.begin(), parts.end());
            std::string j;
            for (auto& x : parts) j += (j.empty() ? "" : ",") + x;
            out.push_back(std::move(cen::Row("ctake", sid).n("n", p.first).s("items", j)));
        }
        for (auto& [k, v] : st.qg.led)
        {
            std::string j;
            for (auto& x : v) j += (j.empty() ? "" : ",") + x;
            out.push_back(std::move(cen::Row("qg", k).n("n", (double)v.size()).s("v", j)));
        }
        for (auto& [k, n] : st.ss.led) out.push_back(std::move(cen::Row("stash", k).n("n", (double)n)));
        Obj w = lootWin();
        bool vis = false;
        if (VAL(w) && E().visible(w, vis) && vis)
        {
            Counts c;
            bool nat = E().natItems(w, 1, true, c);
            Obj pc = E().controller();
            Obj ia = VAL(pc) ? E().interacted(pc) : 0;
            std::string key = "-", cls = "-";
            V3 q;
            if (VAL(ia))
            {
                cls = E().cls(ia);
                if (E().loc(ia, q)) key = cen::pos_key(cls, q.x, q.y, q.z);
            }
            std::string items;
            for (auto& [sid, n] : c) items += (items.empty() ? "" : ",") + sid + "x" + std::to_string(n);
            cen::Row r("window", key);
            r.s("cls", cls).b("nat", nat).n("n", (double)c.size()).s("items", items);
            if (st.lock.mine) r.s("lock", *st.lock.mine);
            out.push_back(std::move(r));
        }
    }
#endif
}
