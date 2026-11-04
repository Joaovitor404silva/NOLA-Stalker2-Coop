#include "s2m_quest.h"
#include "s2m_dlgview.h"
#include "s2m_text.h"
#include "s2x_fr.h"
#include "s2x_hot_logic.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <set>

namespace s2m_quest
{
    namespace
    {
        Env* E = nullptr;
        Hooks g_hooks;

        std::string F(const char* f, ...)
        {
            char b[2048];
            va_list a;
            va_start(a, f);
            int n = std::vsnprintf(b, sizeof b, f, a);
            va_end(a);
            if (n < 0) return {};
            if (static_cast<size_t>(n) < sizeof b) return std::string(b, static_cast<size_t>(n));
            std::string s(static_cast<size_t>(n) + 1, '\0');
            va_start(a, f);
            std::vsnprintf(s.data(), s.size(), f, a);
            va_end(a);
            s.resize(static_cast<size_t>(n));
            return s;
        }

        std::string num(double v)
        {
            if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) return F("%lld", static_cast<long long>(v));
            return F("%.14g", v);
        }

        const char* tf(bool b) { return b ? "true" : "false"; }

        bool lspace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
        bool ldigit(char c) { return c >= '0' && c <= '9'; }
        bool lalnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

        bool tonum(std::string_view s, double& out)
        {
            size_t a = 0, b = s.size();
            while (a < b && lspace(s[a])) ++a;
            while (b > a && lspace(s[b - 1])) --b;
            if (a == b) return false;
            std::string t(s.substr(a, b - a));
            for (char c : t)
                if (c == 'i' || c == 'I' || c == 'n' || c == 'N') return false;
            char* end = nullptr;
            double v = std::strtod(t.c_str(), &end);
            if (!end || *end != '\0') return false;
            out = v;
            return true;
        }

        bool all_digits(std::string_view s)
        {
            if (s.empty()) return false;
            for (char c : s)
                if (!ldigit(c)) return false;
            return true;
        }

        std::vector<std::string> words(std::string_view s)
        {
            std::vector<std::string> r;
            size_t i = 0;
            while (i < s.size())
            {
                while (i < s.size() && lspace(s[i])) ++i;
                size_t j = i;
                while (j < s.size() && !lspace(s[j])) ++j;
                if (j > i) r.emplace_back(s.substr(i, j - i));
                i = j;
            }
            return r;
        }

        std::vector<std::string> gm(std::string_view s, char sep)
        {
            std::vector<std::string> r;
            size_t i = 0;
            while (i < s.size())
            {
                size_t j = s.find(sep, i);
                if (j == std::string_view::npos) j = s.size();
                if (j > i) r.emplace_back(s.substr(i, j - i));
                i = j + 1;
            }
            return r;
        }

        std::vector<std::string> split_keep(std::string_view s, char sep)
        {
            std::vector<std::string> r;
            size_t i = 0;
            for (;;)
            {
                size_t j = s.find(sep, i);
                if (j == std::string_view::npos)
                {
                    r.emplace_back(s.substr(i));
                    break;
                }
                r.emplace_back(s.substr(i, j - i));
                i = j + 1;
            }
            return r;
        }

        std::vector<std::string> lines(std::string_view s)
        {
            std::vector<std::string> r;
            size_t i = 0;
            while (i < s.size())
            {
                size_t j = s.find('\n', i);
                if (j == std::string_view::npos) j = s.size();
                std::string ln(s.substr(i, j - i));
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                r.push_back(std::move(ln));
                i = j + 1;
            }
            return r;
        }

        std::string join(const std::vector<std::string>& v, const char* sep)
        {
            std::string r;
            for (size_t i = 0; i < v.size(); ++i)
            {
                if (i) r += sep;
                r += v[i];
            }
            return r;
        }

        bool starts(std::string_view s, std::string_view p) { return s.size() >= p.size() && s.substr(0, p.size()) == p; }

        bool parse3(std::string_view e, std::string& seq, std::string& mid, std::string& n)
        {
            size_t c1 = e.find(',');
            if (c1 == std::string_view::npos) return false;
            size_t c2 = e.find(',', c1 + 1);
            if (c2 == std::string_view::npos) return false;
            if (e.find(',', c2 + 1) != std::string_view::npos) return false;
            std::string_view a = e.substr(0, c1), b = e.substr(c1 + 1, c2 - c1 - 1), c = e.substr(c2 + 1);
            if (!all_digits(a) || b.empty() || !all_digits(c)) return false;
            seq = std::string(a);
            mid = std::string(b);
            n = std::string(c);
            return true;
        }

        double fl(const char* k, double d) { return E->fl(k, d); }
        bool on(const char* k, double d) { return E->fl(k, d) == 1; }
        bool host() { return E->host(); }
        double now() { return E->now(); }
        void log(const std::string& s) { E->log(s); }
        std::string part(const char* tag)
        {
            std::string s;
            if (!E->part(tag, s)) return {};
            return s;
        }
        bool has_part(const char* tag, std::string& out) { return E->part(tag, out); }
        int nv()
        {
            if (!host() || !on("mcquest", 1)) return 1;
            return std::max(1, std::min(4, E->nviews()));
        }
        bool has_partv(int v, const char* tag, std::string& out) { return v == 0 ? E->part(tag, out) : E->part_v(v, tag, out); }
        std::string partv(int v, const char* tag)
        {
            std::string s;
            if (!has_partv(v, tag, s)) return {};
            return s;
        }
        std::string vpfx(int v) { return nv() > 1 ? std::to_string(E->view_pid(v)) + ":" : std::string(); }
        std::string vwho(int v) { return nv() > 1 ? " (pid " + std::to_string(E->view_pid(v)) + ")" : std::string(); }

        struct QiTab
        {
            std::map<std::string, int> S;
            std::set<std::string> norm;
            std::map<std::string, double> skip;
            struct Pos
            {
                double x = 0, y = 0, z = 0;
                std::string guid, type, sids;
            };
            std::vector<Pos> pos;
            std::vector<std::string> list;
        };

        struct D1Row
        {
            std::vector<std::string> key;
            char t = 's';
            std::string v;
        };

        std::string d1_unesc(std::string_view s)
        {
            std::string r;
            r.reserve(s.size());
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '\\' && i + 1 < s.size())
                {
                    char c = s[i + 1];
                    if (c == '\\') { r += '\\'; ++i; continue; }
                    if (c == 't') { r += '\t'; ++i; continue; }
                    if (c == 'r') { r += '\r'; ++i; continue; }
                    if (c == 'n') { r += '\n'; ++i; continue; }
                }
                r += s[i];
            }
            return r;
        }

        bool d1_parse(std::string_view text, std::vector<D1Row>& rows, std::string& err)
        {
            auto ls = lines(text);
            if (ls.empty()) { err = "vazio"; return false; }
            auto head = split_keep(ls[0], '\t');
            if (head.size() < 3 || head[head.size() - 2] != "t" || head.back() != "v") { err = "cabecalho"; return false; }
            size_t depth = head.size() - 2;
            for (size_t li = 1; li < ls.size(); ++li)
            {
                if (ls[li].empty()) continue;
                auto c = split_keep(ls[li], '\t');
                if (c.size() != depth + 2) { err = F("linha %zu", li + 1); return false; }
                D1Row r;
                for (size_t k = 0; k < depth; ++k)
                {
                    if (c[k].empty()) break;
                    r.key.push_back(d1_unesc(std::string_view(c[k]).substr(1)));
                }
                r.t = c[depth].empty() ? 's' : c[depth][0];
                r.v = d1_unesc(c[depth + 1]);
                rows.push_back(std::move(r));
            }
            return true;
        }

        int g_qitLoaded = 0;
        QiTab g_qit;

        QiTab* qiTab()
        {
            if (g_qitLoaded != 0) return g_qitLoaded > 0 ? &g_qit : nullptr;
            g_qitLoaded = -1;
            std::string text, err;
            std::vector<D1Row> rows;
            if (!E->data("s2u_qitems.tsv", text)) err = "s2u_qitems.tsv nao encontrado";
            else if (!d1_parse(text, rows, err)) err = "s2u_qitems.tsv " + err;
            if (!err.empty())
            {
                log("NOLA qitem: s2u_qitems.lua ausente ou invalido: " + err);
                return nullptr;
            }
            QiTab T;
            std::map<long long, QiTab::Pos> pos;
            bool hasS = false;
            for (auto& r : rows)
            {
                if (r.key.size() >= 2 && r.key[0] == "S") { double v = 0; tonum(r.v, v); T.S[r.key[1]] = static_cast<int>(v); hasS = true; }
                else if (r.key.size() >= 2 && r.key[0] == "norm") T.norm.insert(r.key[1]);
                else if (r.key.size() >= 2 && r.key[0] == "skip") { double v = 0; tonum(r.v, v); T.skip[r.key[1]] = v; }
                else if (r.key.size() >= 3 && r.key[0] == "pos")
                {
                    long long i = std::atoll(r.key[1].c_str());
                    long long f = std::atoll(r.key[2].c_str());
                    auto& p = pos[i];
                    double v = 0;
                    if (f == 1 && tonum(r.v, v)) p.x = v;
                    else if (f == 2 && tonum(r.v, v)) p.y = v;
                    else if (f == 3 && tonum(r.v, v)) p.z = v;
                    else if (f == 4) p.guid = r.v;
                    else if (f == 5) p.type = r.v;
                    else if (f == 6) p.sids = r.v;
                }
            }
            if (!hasS)
            {
                log("NOLA qitem: s2u_qitems.lua ausente ou invalido: s2u_qitems.tsv sem S");
                return nullptr;
            }
            for (long long i = 1; pos.count(i); ++i) T.pos.push_back(pos[i]);
            for (auto& [s, n] : T.S) T.list.push_back(s);
            g_qit = std::move(T);
            g_qitLoaded = 1;
            log(F("NOLA qitem: %d itens de missao, %d nos ItemAdd pulados no cliente, %d recipientes/corpos com item de missao",
                  static_cast<int>(g_qit.list.size()), static_cast<int>(g_qit.skip.size()), static_cast<int>(g_qit.pos.size())));
            return &g_qit;
        }

        int qiCount(Id obj, const std::string& sid)
        {
            if (!obj) return 0;
            return E->count(obj, sid);
        }

        struct Sent
        {
            double at = 0;
            int n = 0;
        };
        struct QiOut
        {
            std::string e;
            std::optional<double> untilT;
        };
        struct NAt
        {
            int n = 0;
            double at = 0;
        };
        struct Qi
        {
            std::vector<QiOut> out;
            std::map<std::string, Sent> sent;
            std::map<std::string, double> seen;
            long long seq = 0;
            size_t i = 0;
            double at = 0;
            bool based = false;
            int bn = 0;
            std::map<std::string, NAt> pend;
            std::map<std::string, double> hold;
            std::map<std::string, NAt> re;
            std::map<std::string, int> agr;
            std::optional<Id> pAddr;
            std::optional<double> ldWait;
            int ldN = 0;
            std::map<std::string, bool> keep;
            bool hotIn = false;
            std::string hotKept;
            std::string hotKeptX;
        };

        struct TrigRow
        {
            std::string sid, q;
            double x = 0, y = 0, z = 0, yaw = 0, sx = 100, sy = 100, sz = 100;
            std::string shape = "Box";
            std::string guid;
            bool cond = false;
            bool dist = false;
        };
        struct DistIn
        {
            std::set<int> views;
            int rc = -100;
            double last = -1e9;
        };
        struct TrigOut
        {
            std::string sid;
            double at = 0;
            long long seq = 0;
            int n = -1;
            bool leave = false;
        };
        struct CondIn
        {
            std::set<int> views;
            std::string guid;
            int rc = -100;
        };
        struct TrigRt
        {
            std::string sid, q, who, how;
            double t0 = 0;
            size_t n0 = 0;
            int h0 = -1;
        };
        struct Trig
        {
            std::vector<TrigRt> rt;
            bool rowsLoaded = false;
            std::vector<TrigRow> rows;
            std::map<std::string, bool> inside;
            std::vector<TrigOut> out;
            std::map<std::string, double> sent, seen, qseen, fired, selfLog, hfire, condRe;
            std::map<std::string, CondIn> condIn;
            double condAt = -100;
            std::map<std::string, DistIn> distIn;
            double distAt = -100;
            long long qn = 0;
            bool joinWait = false;
            double nearAt = -100, tickAt = 0;
            std::vector<size_t> near;
        };

        struct QdPend
        {
            double first = 0, stepT = 0;
            size_t n0 = 0;
            std::string q;
            std::optional<std::string> why;
            std::set<int> views;
        };
        struct QdHist
        {
            double at = 0;
            size_t n = 0;
        };
        struct QdRoll
        {
            std::string sid;
            long long rn = 0;
            double at = 0;
            bool said = false;
            bool held = false;
        };
        struct Qd
        {
            std::map<std::string, QdPend> pend;
            std::map<std::string, double> done, rep;
            std::vector<QdHist> hist;
            std::set<std::string> hpend;
            long long rn = 0;
            std::string rsid = "-";
            long long rnDone = 0;
            int nrep = 0, nroll = 0, nlast = 0;
            double logAt = 0;
            std::string out;
            double outAt = -1e9, buildAt = -1e9;
            std::optional<double> histAt;
            bool peer = false;
            std::optional<std::string> peerJ, myJ;
            bool qmapSet = false;
            std::map<std::string, std::string> qmap;
            std::optional<long long> hrep;
            std::optional<int> hsess;
            std::optional<QdRoll> roll;
            double rollAt = -1e9;
            bool hold = false;
            std::optional<long long> hnLast;
            std::optional<int> rxLast;
            std::array<long long, 4> vrn{};
            std::array<std::string, 4> vrsid{"-", "-", "-", "-"};
            std::array<std::string, 4> vout;
            std::map<std::string, std::set<int>> wonV;
            std::array<long long, 4> ack{};
            Id pawnAt = 0;
            int ndrop = 0;
        };

        struct LogE
        {
            double t = 0;
            std::string e;
            bool live = false;
        };
        struct ListE
        {
            double t = 0;
            std::string s;
        };
        struct Cu
        {
            size_t n = 0;
            std::vector<std::string> chunks, hashes;
            size_t rr = 0;
        };
        struct CuV
        {
            std::optional<int> session;
            std::optional<double> until;
            std::optional<size_t> n;
            std::optional<std::string> loads;
            std::optional<Cu> cu;
        };
        struct Qs
        {
            long long seq = 0;
            std::vector<ListE> list;
            int got = 0, sent = 0;
            bool logLoaded = false;
            std::vector<LogE> log;
            std::optional<std::vector<std::string>> pend;
            std::optional<Id> dedAddr;
            std::optional<double> dedUntil;
            int dedN = 0;
            std::optional<double> ngAt;
            std::optional<double> rbLast;
            int rbHits = 0;
            std::optional<Cu> cu;
            std::optional<int> cuSession;
            std::optional<double> cuUntil;
            std::optional<size_t> cuN;
            std::optional<std::string> cuLoads;
            std::array<CuV, 4> cuv;
            std::string cu0;
            std::optional<std::string> cid;
            std::optional<long long> gen;
            std::optional<std::string> campTag;
            std::optional<Id> rbAddr;
            std::optional<double> rbCheckAt;
            std::optional<long long> rbSeen;
        };

        struct QpOwn
        {
            int n = 0;
            double at = 0;
        };
        struct QpOut
        {
            std::string e;
            double at = 0;
        };
        struct QpQ
        {
            std::string sid;
            long long k = 0;
            std::string key, seq;
            double at = 0;
        };
        struct Qp
        {
            long long seq = 0;
            std::vector<QpOut> out;
            std::map<std::string, double> seen;
            std::map<std::string, QpOwn> own;
            std::map<std::string, double> ownS;
            std::map<std::string, QpOwn> loc;
            std::vector<QpQ> q;
            double leadUntil = -1e9, pruneAt = 0;
            int nsent = 0, ran = 0, dup = 0, div = 0, skip = 0, nolead = 0;
        };
        struct QpTab
        {
            std::map<std::string, std::map<long long, std::string>> pin;
            std::string dialogs = "nil", pins = "nil";
        };

        struct Qdh
        {
            int n = 0;
            std::optional<std::string> st;
            std::map<std::string, int> held;
            double t0 = 0;
            Id hv = 0;
            std::optional<Id> addr;
            bool needUp = false;
            std::optional<double> used;
            std::optional<double> callAt, chkEnd, relAt;
            double capT = 0;
            std::string via;
            int drops = 0, calls = 0, fb = 0, err = 0;
        };
        struct QdrTab
        {
            std::vector<std::string> list;
        };

        struct RwSnap
        {
            double t = 0;
            std::map<std::string, int> items;
            double money = 0;
        };
        struct RwPend
        {
            std::string q;
            double at = 0;
        };
        struct RwOut
        {
            std::string e;
            double untilT = 0;
        };
        struct Rw
        {
            long long seq = 0;
            std::vector<RwSnap> snaps;
            std::vector<RwPend> pend;
            std::vector<RwOut> out;
            std::map<std::string, double> seen;
            double snapAt = 0;
            bool hotIn = false;
            std::string hotKept;
        };

        struct QcRow
        {
            std::string q;
            std::vector<std::string> nodes;
            bool add = false;
            std::map<std::string, std::pair<std::string, int>> item;
        };
        struct QcEp
        {
            std::string sid;
            int v = 0;
            double tp = 0, first = 0, last = -1e9;
            int rounds = 0, ok = 0, iheld = 0;
            bool flush = false, added = false;
            std::set<std::string> iw;
            std::string err;
        };
        struct Qc
        {
            int loaded = 0;
            std::map<std::string, QcRow> tab;
            std::map<uint32_t, std::string> byH;
            std::map<std::string, double> own;
            Id pawnAt = 0;
            double loadAt = -1e9, goneAt = -1;
            std::array<std::map<std::string, double>, 4> peer;
            std::map<std::string, double> done, mir, s2c;
            std::set<std::string> hostOnly, divMuted;
            std::optional<QcEp> cur;
            std::string busy;
            int nok = 0, nmiss = 0, ndefer = 0, nadd = 0, nitem = 0;
            std::string out;
            double outAt = -1e9, buildAt = -1e9;
        };

        struct QbNode
        {
            char kind = 'I';
            std::string q;
            std::vector<std::string> pins;
        };
        struct QbRec
        {
            std::string key, node;
            long long seq = 0;
            int pin = -1;
            bool fin = true;
            double tp = 0;
        };
        struct QbHeld
        {
            std::string node, pin;
            bool fin = true;
            double at = 0;
            int n = 0, defer = 0;
        };
        struct Qb
        {
            int loaded = 0;
            std::map<std::string, QbNode> tab;
            std::map<uint32_t, std::string> byH;
            std::map<std::string, std::string> ans;
            std::map<uint32_t, std::string> ansH;
            std::map<std::string, std::string> lcN, lcA;
            long long epoch = 0, seq = 0;
            std::vector<QbRec> mine, picks;
            std::map<std::string, QbRec> peer, peerA;
            std::set<std::string> used, done;
            std::map<std::string, std::pair<double, int>> tries;
            std::map<std::string, QbHeld> held;
            std::map<std::string, double> refAt;
            double noteAt = -1e9;
            std::string forcing, mirror, busy;
            Id pawnAt = 0;
            double loadAt = -1e9;
            int nrec = 0, nsub = 0, nok = 0, nhold = 0, nforce = 0, nrel = 0, nref = 0, nmir = 0, nerr = 0, ndefer = 0, ndup = 0;
        };

        struct HaNode
        {
            bool on = false;
            int n = 0;
        };
        struct Ha
        {
            std::map<std::string, HaNode> st;
            size_t folded = 0;
            double sigT = 0;
            std::string sigE;
            std::string out;
            size_t ver = 1, outVer = 0;
            int dropped = 0, cut = 0, nstart = 0, nend = 0, nre = 0, nerr = 0;
            std::map<std::string, int> started;
            std::map<std::string, double> ended;
            std::map<uint32_t, std::string> byHash;
            std::string lastRaw;
            Id pawnAt = 0;
            bool pend = false;
        };

        struct State
        {
            Qi qi;
            Ha ha;
            std::optional<Trig> trig;
            std::optional<Qd> qd;
            Qs qs;
            std::optional<Qp> qp;
            int qpLoaded = 0;
            QpTab qpt;
            std::optional<Qdh> qdh;
            int qdrLoaded = 0;
            QdrTab qdrt;
            std::optional<double> qdrKeyAt;
            std::optional<Rw> rw;
            std::optional<Qc> qc;
            std::optional<Qb> qb;
            std::optional<std::set<std::string>> qskip;
            int qrN = 0, cuVN = 0, cuIRN = 0;
            long long beat = 0;
            int b6err = 0, c22err = 0;
        };
        State S;
        std::optional<double> g_qlogBase;
        std::optional<std::string> g_qdropKey;

        s2m_save::QState& SQ() { return E->sq(); }

        Trig& trigSt()
        {
            if (!S.trig)
            {
                S.trig.emplace();
            }
            return *S.trig;
        }

        bool isDist(const std::string& sid) { return sid.size() > 9 && sid.compare(sid.size() - 9, 9, "_CondDist") == 0; }

        void trigLoad(std::vector<TrigRow>& rows)
        {
            std::string text;
            if (E->data("s2u_triggers.tsv", text))
            {
                for (auto& ln : lines(text))
                {
                    auto c = split_keep(ln, '\t');
                    double x, y, z;
                    if (c.size() >= 5 && tonum(c[2], x) && tonum(c[3], y) && tonum(c[4], z))
                    {
                        TrigRow r;
                        r.sid = c[0];
                        r.q = c[1];
                        r.x = x;
                        r.y = y;
                        r.z = z;
                        double v;
                        r.yaw = c.size() >= 6 && tonum(c[5], v) ? v : 0;
                        r.sx = c.size() >= 7 && tonum(c[6], v) ? v : 100;
                        r.sy = c.size() >= 8 && tonum(c[7], v) ? v : 100;
                        r.sz = c.size() >= 9 && tonum(c[8], v) ? v : 100;
                        r.shape = c.size() >= 10 ? c[9] : std::string("Box");
                        if (c.size() >= 11 && c[10].size() == 32) r.guid = c[10];
                        r.dist = isDist(r.sid);
                        r.cond = r.dist || r.sid.find("_CondTrigger") != std::string::npos;
                        rows.push_back(std::move(r));
                    }
                }
            }
            log("NOLA trig: " + std::to_string(rows.size()) + " gatilhos de missao em s2u_triggers.tsv");
        }

        std::vector<TrigRow>& trigRows(Trig& T)
        {
            if (!T.rowsLoaded)
            {
                T.rowsLoaded = true;
                trigLoad(T.rows);
            }
            return T.rows;
        }

        bool trigInside(const TrigRow& r, const V3& q, double m)
        {
            double dx = q.x - r.x, dy = q.y - r.y, dz = q.z - r.z;
            if (r.dist) return dx * dx + dy * dy + dz * dz <= r.sx * r.sx;
            if (r.shape != "Box") return dx * dx + dy * dy < (r.sx + m) * (r.sx + m) && std::fabs(dz) < r.sz + m + 100;
            double a = -(r.yaw * 3.14159265358979323846 / 180.0);
            double c = std::cos(a), s = std::sin(a);
            double lx = dx * c - dy * s, ly = dx * s + dy * c;
            return std::fabs(lx) <= r.sx + m && std::fabs(ly) <= r.sy + m && std::fabs(dz) <= r.sz + m + 100;
        }

        bool trSelf(Trig& T, const std::string& sid, const std::string& seq, double t)
        {
            if (fl("trigself", 0) != 1 || fl("lead", 0) != 1) return false;
            auto it = T.sent.find(sid);
            if (it == T.sent.end() || t - it->second > fl("trigre", 30)) return false;
            double at = it->second;
            auto sl = T.selfLog.find(sid);
            if (sl == T.selfLog.end() || sl->second != at)
            {
                T.selfLog[sid] = at;
                log(F("NOLA qn: origem c %s seq=%s NAO disparado: o host ja entrou nesse gatilho ha %.1f s", sid.c_str(), seq.c_str(), t - at));
            }
            return true;
        }

        bool trLate(Trig& T, const std::string& sid, double t)
        {
            if (fl("trigself", 0) != 1 || fl("lead", 0) != 1 || !host()) return false;
            auto it = T.fired.find(sid);
            if (it == T.fired.end() || t - it->second > fl("trigre", 30)) return false;
            log(F("NOLA trig: o host entrou em %s que o cliente ja disparou ha %.1f s: o jogo do host dispara sozinho, sem trava", sid.c_str(), t - it->second));
            return true;
        }

        const TrigRow* condRow(Trig& T, const std::string& sid)
        {
            for (auto& r : trigRows(T))
                if (r.cond && r.sid == sid) return &r;
            return nullptr;
        }

        void condHold(const std::string& sid, CondIn& c)
        {
            std::string e;
            int r = E->trig_enter(c.guid, 1, e);
            if (r == c.rc) return;
            c.rc = r;
            log(F("NOLA qcond: %s: o jogador do host fica dentro do gatilho %s enquanto houver cliente dentro r=%d %s", sid.c_str(), c.guid.c_str(), r, e.c_str()));
        }

        void condRelease(const std::string& sid, CondIn& c, const char* why)
        {
            std::string e;
            int r = E->trig_enter(c.guid, 2, e);
            log(F("NOLA qcond: %s: ninguem dentro (%s): o jogador do host sai do gatilho %s r=%d %s", sid.c_str(), why, c.guid.c_str(), r, e.c_str()));
        }

        void condEnter(Trig& T, int v, const std::string& sid)
        {
            const TrigRow* row = condRow(T, sid);
            if (!row || row->guid.empty()) return;
            CondIn& c = T.condIn[sid];
            c.guid = row->guid;
            if (c.views.empty()) c.rc = -100;
            if (!c.views.insert(v).second) return;
            log(F("NOLA qcond: %s: cliente%s entrou, %d dentro", sid.c_str(), vwho(v).c_str(), static_cast<int>(c.views.size())));
            condHold(sid, c);
        }

        void condLeave(Trig& T, int v, const std::string& sid)
        {
            auto it = T.condIn.find(sid);
            if (it == T.condIn.end() || !it->second.views.erase(v)) return;
            log(F("NOLA qcond: %s: cliente%s saiu, %d dentro", sid.c_str(), vwho(v).c_str(), static_cast<int>(it->second.views.size())));
            if (it->second.views.empty())
            {
                condRelease(sid, it->second, "cliente saiu");
                T.condIn.erase(it);
            }
        }

        void condTick(Trig& T, double t, int NV)
        {
            if (T.condIn.empty()) return;
            const bool live = fl("qcondtrig", 1) == 1;
            if (live && t - T.condAt < 0.5) return;
            T.condAt = t;
            for (auto it = T.condIn.begin(); it != T.condIn.end();)
            {
                CondIn& c = it->second;
                for (auto v = c.views.begin(); v != c.views.end();)
                {
                    const bool up = *v < NV && (*v == 0 ? E->peer_state() == 1 : E->peer_state_v(*v) == 1);
                    if (live && up)
                    {
                        ++v;
                        continue;
                    }
                    log(F("NOLA qcond: %s: cliente%s %s, %d dentro", it->first.c_str(), vwho(*v).c_str(), live ? "desconectou" : "recurso desligado", static_cast<int>(c.views.size()) - 1));
                    v = c.views.erase(v);
                }
                if (c.views.empty())
                {
                    condRelease(it->first, c, live ? "cliente desconectou" : "recurso desligado");
                    it = T.condIn.erase(it);
                    continue;
                }
                condHold(it->first, c);
                ++it;
            }
        }

        struct EvRow
        {
            std::string q, type;
            bool rep = false;
        };
        int g_evLoaded = 0;
        std::map<std::string, EvRow> g_ev;
        bool g_cuApply = false;

        bool copyKind(const std::string& k) { return k == "R" || k == "V" || k == "H"; }

        std::map<std::string, EvRow>* evTab()
        {
            if (g_evLoaded != 0) return g_evLoaded > 0 ? &g_ev : nullptr;
            g_evLoaded = -1;
            std::string text, err;
            std::vector<D1Row> rows;
            if (!E->data("s2u_qevent.tsv", text)) err = "s2u_qevent.tsv nao encontrado";
            else if (!d1_parse(text, rows, err)) err = "s2u_qevent.tsv " + err;
            if (!err.empty())
            {
                log("NOLA qev: " + err);
                return nullptr;
            }
            for (auto& r : rows)
            {
                if (r.key.size() < 2 || r.key[0] != "ev") continue;
                auto w = words(r.v);
                if (w.size() < 3) continue;
                g_ev[r.key[1]] = {w[0], w[1], w[2] == "1"};
            }
            g_evLoaded = 1;
            log(F("NOLA qev: tabela carregada (%d nos de evento)", static_cast<int>(g_ev.size())));
            return &g_ev;
        }

        const EvRow* evRow(const std::string& sid)
        {
            if (fl("qevent", 0) != 1) return nullptr;
            auto* tab = evTab();
            if (!tab) return nullptr;
            auto it = tab->find(sid);
            return it == tab->end() ? nullptr : &it->second;
        }

        bool haStale(const std::string& sid, int n);
        bool haKnown(const std::string& sid);
        int haCount(const std::string& sid);
        int haRelayN(const std::string& sid);
        bool haEchoEnd(const std::string& sid, double t);
        void haZ(const std::string& sid);

        bool evEcho(Trig& T, const std::string& sid, const std::string& seq, double t)
        {
            const EvRow* r = evRow(sid);
            if (!r) return false;
            auto h = T.hfire.find(sid);
            if (h == T.hfire.end()) return false;
            if (r->rep && t - h->second > fl("trigre", 30)) return false;
            log(F("NOLA qev: origem c %s seq=%s NAO disparado: o grafo do host ja disparou esse evento ha %.1f s", sid.c_str(), seq.c_str(), t - h->second));
            return true;
        }

        void evCmd(const std::vector<std::string>& w)
        {
            if (fl("qevent", 0) != 1) return;
            if (w.size() < 2)
            {
                log("NOLA qev: linha s2q E sem SID");
                return;
            }
            const std::string& sid = w[1];
            const EvRow* r = evRow(sid);
            if (!r)
            {
                log("NOLA qev: " + sid + " fora de s2u_qevent.tsv: ignorado");
                return;
            }
            Trig& T = trigSt();
            double t = now();
            if (host())
            {
                T.sent[sid] = t;
                T.hfire[sid] = t;
                log(F("NOLA qev: o grafo do host disparou %s (%s, missao %s)", sid.c_str(), r->type.c_str(), r->q.c_str()));
                return;
            }
            if (fl("qdmir", 1) == 1 && S.qc)
            {
                auto m = S.qc->mir.find(sid);
                if (m != S.qc->mir.end() && t - m->second <= fl("qcatchgap", 5))
                {
                    S.qc->mir.erase(m);
                    log(F("NOLA qev: %s fired by the catch-up (the host already ran this step): not a client step, no #QN sent", sid.c_str()));
                    return;
                }
            }
            if (haEchoEnd(sid, t))
            {
                log(F("NOLA qev: %s fechado pelo cliente a pedido do host (conjunto ativo): nao e um passo do cliente, nenhum #QN enviado", sid.c_str()));
                return;
            }
            auto sv = T.sent.find(sid);
            if (t - (sv == T.sent.end() ? -1000.0 : sv->second) <= fl("trigre", 30))
            {
                log(F("NOLA qev: %s de novo em %.1f s: nao reenviado", sid.c_str(), t - sv->second));
                return;
            }
            T.qn += 1;
            T.sent[sid] = t;
            T.out.push_back({sid, t, T.qn, haRelayN(sid)});
            log(F("NOLA qev: o grafo do cliente disparou %s (%s, missao %s) -> #QN c seq=%lld", sid.c_str(), r->type.c_str(), r->q.c_str(), T.qn));
        }

        bool aaCall(const char* fn, const std::string& sid, std::string& e)
        {
            int r = E->aa(fn, sid, e);
            if (r == mgr_missing)
            {
                e = "CustomConsoleManagerAA ausente";
                return false;
            }
            return r == call_ok;
        }

        std::string distNode(const std::string& sid) { return sid.substr(0, sid.size() - 9); }

        void distFire(const std::string& sid, DistIn& d, double t)
        {
            d.last = t;
            if (fl("qdisthold", 1) == 1 && E->world_held())
            {
                if (d.rc == -3) return;
                d.rc = -3;
                log(F("NOLA qdist: %s: XEndQuestNodeBySID adiado: o grafo do host ainda esta parado depois da carga (tick do ScenarioManager 0)", distNode(sid).c_str()));
                return;
            }
            std::string e;
            const bool ok = aaCall("XEndQuestNodeBySID", distNode(sid), e);
            const int r = ok ? 1 : 0;
            if (r == d.rc) return;
            d.rc = r;
            log(F("NOLA qdist: %s: XEndQuestNodeBySID ok=%s %s", distNode(sid).c_str(), tf(ok), ok ? "" : e.c_str()));
        }

        void distEnter(Trig& T, int v, const std::string& sid)
        {
            DistIn& d = T.distIn[sid];
            const bool first = d.views.empty();
            if (!d.views.insert(v).second) return;
            if (first) d.rc = -100;
            double rad = 0;
            for (auto& r : trigRows(T))
                if (r.dist && r.sid == sid) { rad = r.sx; break; }
            log(F("NOLA qdist: %s: cliente%s a menos de %.0f cm do ponto, condicao verdadeira no host (%d dentro)", distNode(sid).c_str(), vwho(v).c_str(), rad,
                  static_cast<int>(d.views.size())));
            distFire(sid, d, now());
        }

        void distLeave(Trig& T, int v, const std::string& sid)
        {
            auto it = T.distIn.find(sid);
            if (it == T.distIn.end() || !it->second.views.erase(v)) return;
            log(F("NOLA qdist: %s: cliente%s saiu do ponto, %d dentro", distNode(sid).c_str(), vwho(v).c_str(), static_cast<int>(it->second.views.size())));
            if (it->second.views.empty()) T.distIn.erase(it);
        }

        void distTick(Trig& T, double t, int NV)
        {
            if (T.distIn.empty()) return;
            const bool live = fl("qdist", 1) == 1;
            if (live && t - T.distAt < 0.5) return;
            T.distAt = t;
            for (auto it = T.distIn.begin(); it != T.distIn.end();)
            {
                DistIn& d = it->second;
                for (auto v = d.views.begin(); v != d.views.end();)
                {
                    const bool up = *v < NV && (*v == 0 ? E->peer_state() == 1 : E->peer_state_v(*v) == 1);
                    if (live && up)
                    {
                        ++v;
                        continue;
                    }
                    log(F("NOLA qdist: %s: cliente%s %s, %d dentro", distNode(it->first).c_str(), vwho(*v).c_str(), live ? "desconectou" : "recurso desligado",
                          static_cast<int>(d.views.size()) - 1));
                    v = d.views.erase(v);
                }
                if (d.views.empty())
                {
                    it = T.distIn.erase(it);
                    continue;
                }
                if (t - d.last >= fl("qdistre", 2)) distFire(it->first, d, t);
                ++it;
            }
        }

        void trigTick()
        {
            if (fl("ctrig", 1) != 1) return;
            Trig& T = trigSt();
            double t = now();
            if (host())
            {
                int NV = nv();
                auto endRt = [&](const TrigRt& r) {
                    if (r.h0 >= 0 && fl("qhact", 1) == 1 && haKnown(r.sid) && !haStale(r.sid, r.h0)) haZ(r.sid);
                };
                for (size_t i = 0; i < T.rt.size();)
                {
                    TrigRt& r = T.rt[i];
                    bool prog = false;
                    for (size_t k = r.n0; k < S.qs.log.size() && !prog; ++k)
                    {
                        const std::string& le = S.qs.log[k].e;
                        if (le.size() > 1 && (le[0] == 'Z' || le[0] == 'H') && le[1] == ',') continue;
                        if (le.find(r.q) != std::string::npos) prog = true;
                    }
                    const bool hOn = r.h0 >= 0 && fl("qhact", 1) == 1 && haKnown(r.sid);
                    if (!prog && hOn && haCount(r.sid) != r.h0) prog = true;
                    if (prog)
                    {
                        log(F("NOLA qn: veredito %s%s via %s: o grafo do host AVANCOU na missao %s %.1f s depois", r.sid.c_str(), r.who.c_str(), r.how.c_str(), r.q.c_str(), t - r.t0));
                        endRt(r);
                        T.rt.erase(T.rt.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }
                    if (t - r.t0 >= fl("qnrwin", 12))
                    {
                        log(F("NOLA qn: veredito %s%s via %s: o grafo do host NAO AVANCOU na missao %s em %.0f s", r.sid.c_str(), r.who.c_str(), r.how.c_str(), r.q.c_str(), t - r.t0));
                        endRt(r);
                        T.rt.erase(T.rt.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }
                    if (hOn && fl("qrearm", 1) == 1 && haStale(r.sid, r.h0))
                    {
                        log(F("NOLA qn: veredito %s%s via %s: o ouvinte do host terminou ou foi rearmado", r.sid.c_str(), r.who.c_str(), r.how.c_str()));
                        T.rt.erase(T.rt.begin() + static_cast<std::ptrdiff_t>(i));
                        continue;
                    }
                    ++i;
                }
                condTick(T, t, NV);
                distTick(T, t, NV);
                for (int v = 0; v < NV; ++v)
                for (auto& ent : gm(partv(v, "QN"), '|'))
                {
                    if (ent.size() < 5 || (ent[0] != 'h' && ent[0] != 'c' && ent[0] != 'x') || ent[1] != ',') continue;
                    size_t c2 = ent.find(',', 2);
                    if (c2 == std::string::npos) continue;
                    std::string seq = ent.substr(2, c2 - 2), sid = ent.substr(c2 + 1);
                    if (!all_digits(seq) || sid.empty()) continue;
                    int rn = -1;
                    {
                        size_t at = sid.rfind('@');
                        if (at != std::string::npos && all_digits(std::string_view(sid).substr(at + 1)))
                        {
                            rn = std::atoi(sid.c_str() + at + 1);
                            sid.resize(at);
                            if (sid.empty()) continue;
                        }
                    }
                    if (ent[0] == 'x')
                    {
                        std::string key = vpfx(v) + sid + ":x" + seq;
                        if (!T.qseen.count(key))
                        {
                            T.qseen[key] = t;
                            if (isDist(sid)) distLeave(T, v, sid);
                            else if (fl("qcondtrig", 1) == 1) condLeave(T, v, sid);
                        }
                    }
                    else if (ent[0] == 'c')
                    {
                        std::string key = vpfx(v) + sid + ":" + seq;
                        if (!T.qseen.count(key))
                        {
                            T.qseen[key] = t;
                            if (isDist(sid))
                            {
                                if (fl("qdist", 1) == 1) distEnter(T, v, sid);
                            }
                            else if (fl("qcondtrig", 1) == 1 && sid.find("_CondTrigger") != std::string::npos) condEnter(T, v, sid);
                            else if (fl("qhstale", 1) == 1 && fl("qhact", 1) == 1 && haStale(sid, rn))
                            {
                                S.ha.dropped += 1;
                                log(F("NOLA qn: origem c %s seq=%s%s ignorado: o ouvinte do host ja terminou ou foi rearmado (contador do cliente %d, total descartado %d)", sid.c_str(), seq.c_str(),
                                      vwho(v).c_str(), rn, S.ha.dropped));
                            }
                            else if (E->trig_deny(sid, seq)) T.seen[sid] = t;
                            else if (evEcho(T, sid, seq, t)) T.seen[sid] = t;
                            else if (trSelf(T, sid, seq, t)) T.seen[sid] = t;
                            else
                            {
                                auto sv = T.seen.find(sid);
                                if (t - (sv == T.seen.end() ? -1000.0 : sv->second) > fl("trigre", 30))
                                {
                                    T.seen[sid] = t;
                                    T.fired[sid] = t;
                                    std::string e;
                                    if (on("cjoin", 0) && E->mq_sid(sid)) E->cj_cause(sid);
                                    const int n0 = fl("qhact", 1) == 1 ? haCount(sid) : -1;
                                    const size_t qn0 = S.qs.log.size();
                                    std::string qid, guid;
                                    for (auto& rw : trigRows(T))
                                        if (rw.sid == sid) { qid = rw.q; guid = rw.guid; break; }
                                    bool ok = false;
                                    std::string how;
                                    int tr = -11;
                                    if (fl("qtrigin", 1) == 1 && !guid.empty())
                                    {
                                        how = "TriggerManager";
                                        tr = E->trig_enter(guid, 0, e);
                                        ok = tr >= 0;
                                        log(F("NOLA qn: origem c %s seq=%s%s -> gatilho %s: o jogador do host entra pelo TriggerManager (sem teleporte, volta ao lugar real) r=%d %s", sid.c_str(), seq.c_str(),
                                              vwho(v).c_str(), guid.c_str(), tr, e.c_str()));
                                        e.clear();
                                    }
                                    if (tr == -11 || tr == -10 || tr == -9 || tr == -2 || tr == -1)
                                    {
                                        how = "XEndQuestNodeBySID";
                                        ok = aaCall("XEndQuestNodeBySID", sid, e);
                                        log(F("NOLA qn: origem c %s seq=%s%s -> XEndQuestNodeBySID ok=%s %s", sid.c_str(), seq.c_str(), vwho(v).c_str(), tf(ok), ok ? "" : e.c_str()));
                                    }
                                    bool queued = false;
                                    if (ok)
                                    {
                                        if (qid.empty())
                                        {
                                            size_t p = sid.find("_On");
                                            if (p == std::string::npos) p = sid.find("_Trigger");
                                            if (p != std::string::npos && p > 0) qid = sid.substr(0, p);
                                        }
                                        if (!qid.empty())
                                        {
                                            queued = true;
                                            bool dup = false;
                                            for (auto& x : T.rt) if (x.sid == sid) dup = true;
                                            if (!dup) T.rt.push_back({sid, qid, vwho(v), how, t, qn0, n0});
                                        }
                                    }
                                    if (!queued && ok && fl("qhact", 1) == 1 && haKnown(sid) && !(fl("qrearm", 1) == 1 && haCount(sid) != n0)) haZ(sid);
                                }
                                else if (NV > 1)
                                    log(F("NOLA qn: origem c %s seq=%s%s: ja disparado ha %.1f s (outro jogador ou repeticao): nada repetido", sid.c_str(), seq.c_str(), vwho(v).c_str(),
                                          t - sv->second));
                            }
                        }
                    }
                    else
                        log(F("NOLA qn: origem h %s seq=%s recebido de volta (ignorado no host)%s", sid.c_str(), seq.c_str(), vwho(v).c_str()));
                }
                if (fl("lead", 0) != 1) return;
            }
            if (fl("qnsrc", 0) != 0) return;
            if (t - T.tickAt < 0.5) return;
            T.tickAt = t;
            Id pw = E->pawn();
            if (!pw) return;
            if (!host() && fl("trigjoin", 1) == 1 && E->join_pending())
            {
                if (!T.joinWait)
                {
                    T.joinWait = true;
                    log("NOLA trig: join teleport to the host pending: trigger detection waits (the pawn still stands where the save left it)");
                }
                return;
            }
            if (T.joinWait)
            {
                T.joinWait = false;
                T.nearAt = -1e9;
                log("NOLA trig: join done: trigger detection back at the client's real place");
            }
            V3 q;
            if (!E->pos(pw, q)) return;
            if (t - T.nearAt > 5)
            {
                T.nearAt = t;
                T.near.clear();
                double tn = fl("trignear", 40000);
                double r2 = tn * tn;
                auto& rows = trigRows(T);
                const bool condOn = fl("qcondtrig", 1) == 1;
                for (size_t i = 0; i < rows.size(); ++i)
                {
                    if (rows[i].dist)
                    {
                        if (!host() && fl("qdist", 1) == 1) T.near.push_back(i);
                        else if (!host())
                        {
                            auto in = T.inside.find(rows[i].sid);
                            if (in != T.inside.end() && in->second)
                            {
                                in->second = false;
                                T.qn += 1;
                                T.out.push_back({rows[i].sid, t, T.qn, -1, true});
                            }
                        }
                    }
                    else if (rows[i].cond)
                    {
                        if (condOn) T.near.push_back(i);
                    }
                    else if ((rows[i].x - q.x) * (rows[i].x - q.x) + (rows[i].y - q.y) * (rows[i].y - q.y) < r2) T.near.push_back(i);
                }
            }
            double margin = fl("trigmargin", 150);
            for (size_t idx : T.near)
            {
                const TrigRow& r = T.rows[idx];
                bool inside = trigInside(r, q, margin);
                auto in = T.inside.find(r.sid);
                const bool was = in != T.inside.end() && in->second;
                if (r.cond)
                {
                    if (inside != was)
                    {
                        T.qn += 1;
                        T.condRe[r.sid] = t;
                        if (!host()) T.out.push_back({r.sid, t, T.qn, -1, !inside});
                        log(F("NOLA trig: %s gatilho de condicao %s (%s)", inside ? "entrei no" : "sai do", r.sid.c_str(), r.q.c_str()));
                    }
                    else if (inside && !host() && t - T.condRe[r.sid] >= fl("qcondre", 5))
                    {
                        T.qn += 1;
                        T.condRe[r.sid] = t;
                        T.out.push_back({r.sid, t, T.qn, -1, false});
                    }
                }
                else if (inside && !was)
                {
                    auto sv = T.sent.find(r.sid);
                    if (t - (sv == T.sent.end() ? -1000.0 : sv->second) > fl("trigre", 30))
                    {
                        T.qn += 1;
                        T.sent[r.sid] = t;
                        if (!host()) T.out.push_back({r.sid, t, T.qn});
                        if (!trLate(T, r.sid, t))
                        {
                            try { E->trig_entered(r.sid, r.x, r.y, r.z); } catch (...) {}
                        }
                        log(F("NOLA trig: entrei no gatilho %s (%s) a %.0f m do centro", r.sid.c_str(), r.q.c_str(),
                              std::sqrt((r.x - q.x) * (r.x - q.x) + (r.y - q.y) * (r.y - q.y)) / 100));
                    }
                }
                T.inside[r.sid] = inside;
            }
        }

        std::string trigOut()
        {
            if (!S.trig || host()) return "";
            Trig& T = *S.trig;
            double t = now();
            double hold = fl("qnkeep", 1) == 1 ? std::max(3.0, fl("peersilent", 10) + 2) : 3.0;
            std::vector<TrigOut> keep;
            for (auto& e : T.out)
                if (t - e.at < hold) keep.push_back(e);
            T.out = keep;
            if (keep.empty()) return "";
            if (fl("qrelay", 1) != 1) return "";
            std::vector<std::string> q;
            for (auto& e : keep)
                q.push_back(e.leave ? F("x,%lld,%s", e.seq, e.sid.c_str()) : e.n >= 0 ? F("c,%lld,%s@%d", e.seq, e.sid.c_str(), e.n) : F("c,%lld,%s", e.seq, e.sid.c_str()));
            return "#QN:" + join(q, "|");
        }

        void qlogLoad();

        Qd& qdSt()
        {
            if (!S.qd)
            {
                S.qd.emplace();
                S.qd->logAt = now();
            }
            return *S.qd;
        }

        std::string qdJ()
        {
            if (fl("qdivj", 0) != 1) return "-";
            int c[4] = {0, 0, 0, 0};
            if (!E->journal(c)) return "-";
            return F("%d.%d.%d.%d", c[0], c[1], c[2], c[3]);
        }

        std::string qdQuest(const std::string& sid)
        {
            Qd& D = qdSt();
            if (!D.qmapSet)
            {
                D.qmapSet = true;
                if (S.trig)
                {
                    for (auto& r : trigRows(*S.trig)) D.qmap[r.sid] = r.q;
                }
                else
                {
                    std::vector<TrigRow> tmp;
                    trigLoad(tmp);
                    for (auto& r : tmp) D.qmap[r.sid] = r.q;
                }
            }
            auto it = D.qmap.find(sid);
            if (it != D.qmap.end()) return it->second;
            if (const EvRow* er = evRow(sid)) return er->q;
            size_t p = sid.find("_OnTrigger");
            if (p != std::string::npos) return sid.substr(0, p);
            size_t u = sid.find('_');
            if (u != 0)
            {
                if (u == std::string::npos) return sid;
                return sid.substr(0, u);
            }
            return sid;
        }

        size_t qdN0(Qd& D, double stepT, size_t n)
        {
            size_t v = D.hist.empty() ? n : D.hist[0].n;
            for (auto& h : D.hist)
            {
                if (h.at <= stepT) v = h.n;
                else break;
            }
            return v;
        }

        bool qdConf(const std::string& q, std::optional<size_t> n0, std::string& ev)
        {
            auto& L = S.qs.log;
            std::string pre = q + "_";
            for (size_t i = (n0 ? *n0 : L.size()) + 1; i <= L.size(); ++i)
            {
                const std::string& e = L[i - 1].e;
                auto f = gm(e, ',');
                std::optional<std::string> sid;
                bool jg = !f.empty() && (f[0] == "J" || f[0] == "G");
                if (jg && f.size() >= 5) sid = f[4];
                else if (f.size() >= 2) sid = f[1];
                if ((!f.empty() && f[0] == "J" && f.size() >= 2 && f[1] == q) || (sid && starts(*sid, pre)))
                {
                    ev = e;
                    return true;
                }
            }
            return false;
        }

        bool qdOpen(const Qd& D, int v)
        {
            if (fl("qdroll1", 1) != 1 || v < 0 || v >= 4) return false;
            return (v == 0 ? D.rn : D.vrn[(size_t)v]) > D.ack[(size_t)v];
        }

        void qdRollV(Qd& D, const std::string& sid, const std::set<int>& views, const std::string& q, const std::string& ev)
        {
            std::vector<std::string> who;
            for (int v : views)
            {
                if (v < 0 || v >= 4 || D.wonV[sid].count(v)) continue;
                D.wonV[sid].insert(v);
                if (qdOpen(D, v)) continue;
                if (v == 0)
                {
                    D.rn += 1;
                    D.rsid = sid;
                }
                else
                {
                    D.vrn[(size_t)v] += 1;
                    D.vrsid[(size_t)v] = sid;
                }
                who.push_back(F("pid %d #%lld", E->view_pid(v), v == 0 ? D.rn : D.vrn[(size_t)v]));
            }
            if (who.empty()) return;
            log(F("NOLA qdiv: o host vence (N14) em %s (missao %s; %s): volta ao estado do host so quem rodou (%s)", sid.c_str(), q.c_str(), ev.c_str(), join(who, ", ").c_str()));
        }

        void qdFix(Qd& D, const std::string& sid, QdPend p, double t)
        {
            bool test = starts(sid, "S2QDIVTEST_");
            bool conf;
            std::string ev;
            if (test)
            {
                conf = starts(sid, "S2QDIVTEST_C");
                ev = "teste S1";
            }
            else
            {
                conf = qdConf(p.q, p.n0, ev);
                if (!conf) ev = "nil";
            }
            D.done[sid] = p.stepT;
            D.pend.erase(sid);
            if (!conf)
            {
                bool ok = true;
                std::string e = "teste S1: XEndQuestNodeBySID NAO chamado";
                if (!test)
                {
                    if (S.trig)
                    {
                        S.trig->seen[sid] = t;
                        S.trig->fired[sid] = t;
                    }
                    if (on("cjoin", 0) && E->mq_sid(sid)) E->cj_cause(sid);
                    e.clear();
                    ok = aaCall("XEndQuestNodeBySID", sid, e);
                }
                if (ok)
                {
                    D.rep[sid] = t;
                    D.nrep += 1;
                    log(F("NOLA qdiv: reparado: o host repetiu o passo do cliente %s (missao %s, %.0f s atras) -> XEndQuestNodeBySID ok %s", sid.c_str(), p.q.c_str(),
                          t - p.stepT, test ? e.c_str() : ""));
                    return;
                }
                ev = "a repeticao falhou: " + e;
            }
            if (fl("qdivroll", 1) != 1)
            {
                log(F("NOLA qdiv: %s (missao %s): o host vence (%s) mas qdivroll=0: so registro", sid.c_str(), p.q.c_str(), ev.c_str()));
                return;
            }
            if (nv() > 1)
            {
                D.nroll += 1;
                qdRollV(D, sid, p.views, p.q, ev);
                return;
            }
            D.rn += 1;
            D.rsid = sid;
            D.nroll += 1;
            log(F("NOLA qdiv: o host vence (N14) em %s (missao %s; %s): o cliente volta ao estado do host (ordem #%lld)", sid.c_str(), p.q.c_str(), ev.c_str(), D.rn));
        }

        void qdHost(Qd& D, double t)
        {
            auto& L = S.qs.log;
            if (t - (D.histAt ? *D.histAt : -1e9) >= 5)
            {
                D.histAt = t;
                D.hist.push_back({t, L.size()});
                while (D.hist.size() >= 2 && t - D.hist[0].at > fl("qdivkeep", 600) + 60) D.hist.erase(D.hist.begin());
            }
            int NV = nv();
            struct Cand
            {
                std::string sid;
                double age = 0;
                int v = 0;
            };
            std::vector<Cand> cand;
            std::map<std::string, std::set<int>> seenV;
            bool any = false;
            for (int v = 0; v < NV; ++v)
            {
                std::string s;
                if (!has_partv(v, "QD", s) || !starts(s, "c,"))
                {
                    if (v == 0) D.peerJ.reset();
                    continue;
                }
                any = true;
                size_t sc = s.find(';');
                std::string head = sc == std::string::npos ? s : s.substr(0, sc);
                std::string list = sc == std::string::npos ? std::string() : s.substr(sc + 1);
                if (v == 0)
                {
                    std::string r = head.substr(2);
                    size_t c = r.find(',');
                    D.peerJ = c == std::string::npos ? r : r.substr(0, c);
                }
                auto hp = split_keep(head, ',');
                if (v < 4 && hp.size() >= 3 && all_digits(hp[2])) D.ack[(size_t)v] = std::atoll(hp[2].c_str());
                for (auto& ent : gm(list, '|'))
                {
                    size_t c = ent.rfind(':');
                    if (c == std::string::npos || c == 0) continue;
                    std::string sid = ent.substr(0, c), ages = ent.substr(c + 1);
                    if (!all_digits(ages)) continue;
                    seenV[sid].insert(v);
                    cand.push_back({sid, std::atof(ages.c_str()), v});
                }
            }
            if (!any)
            {
                D.peerJ.reset();
                D.peer = false;
                return;
            }
            D.peer = true;
            bool lead = E->lead_mine();
            std::vector<std::string> fix;
            for (auto& cd : cand)
            {
                const std::string& sid = cd.sid;
                double age = cd.age;
                double stepT = t - age;
                double cov = -1e9;
                if (S.trig)
                {
                    auto f = S.trig->fired.find(sid);
                    if (f != S.trig->fired.end()) cov = std::max(cov, f->second);
                    auto se = S.trig->sent.find(sid);
                    if (se != S.trig->sent.end()) cov = std::max(cov, se->second);
                }
                auto rp = D.rep.find(sid);
                if (rp != D.rep.end()) cov = std::max(cov, rp->second);
                auto dn = D.done.find(sid);
                bool own = false;
                if (S.trig && fl("qdmir", 1) == 1)
                {
                    auto h = S.trig->hfire.find(sid);
                    const EvRow* er = h != S.trig->hfire.end() ? evRow(sid) : nullptr;
                    own = er && (!er->rep || stepT - h->second <= fl("trigre", 30));
                }
                if (qdOpen(D, cd.v))
                {
                    auto pi = D.pend.find(sid);
                    if (pi != D.pend.end())
                    {
                        pi->second.views.erase(cd.v);
                        if (pi->second.views.empty()) D.pend.erase(pi);
                    }
                    continue;
                }
                if (own || cov >= stepT - fl("trigre", 30) || (dn != D.done.end() && dn->second >= stepT - 15))
                {
                    auto wv = D.wonV.find(sid);
                    if (NV > 1 && wv != D.wonV.end() && !wv->second.count(cd.v) && age >= fl("qdivm", 20)) qdRollV(D, sid, {cd.v}, qdQuest(sid), "o host ja venceu nesse passo");
                    D.pend.erase(sid);
                }
                else if (age >= fl("qdivm", 20))
                {
                    auto pi = D.pend.find(sid);
                    if (pi == D.pend.end())
                    {
                        QdPend p;
                        p.first = t;
                        p.stepT = stepT;
                        p.n0 = qdN0(D, stepT, L.size());
                        p.q = qdQuest(sid);
                        pi = D.pend.emplace(sid, p).first;
                        log(F("NOLA qdiv: divergencia: o grafo do cliente rodou %s (missao %s) ha %d s e o host nao%s", sid.c_str(), pi->second.q.c_str(), static_cast<int>(age),
                              vwho(cd.v).c_str()));
                    }
                    pi->second.views.insert(cd.v);
                    if (fl("lead", 0) != 1) pi->second.why = "lead=0: so registro";
                    else if (lead) pi->second.why = "o host lidera agora: espera";
                    else if (NV == 1) qdFix(D, sid, pi->second, t);
                    else if (std::find(fix.begin(), fix.end(), sid) == fix.end()) fix.push_back(sid);
                }
            }
            for (auto& sid : fix)
            {
                auto pi = D.pend.find(sid);
                if (pi != D.pend.end()) qdFix(D, sid, pi->second, t);
            }
            for (auto it = D.pend.begin(); it != D.pend.end();)
            {
                auto sv = seenV.find(it->first);
                if (sv == seenV.end()) it = D.pend.erase(it);
                else
                {
                    if (NV > 1)
                    {
                        std::set<int> keep;
                        for (int v : it->second.views)
                            if (sv->second.count(v)) keep.insert(v);
                        it->second.views = keep;
                    }
                    ++it;
                }
            }
        }

        void qdRoll(Qd& D, double t)
        {
            if (!D.roll) return;
            QdRoll& R = *D.roll;
            std::string sid = R.sid;
            if (starts(sid, "S2QDIVTEST_"))
            {
                D.roll.reset();
                log("NOLA qdiv: teste S1: o host venceu em " + sid + ": faria S2L.n7Follow (nada recarregado)");
                return;
            }
            if (fl("qdivroll", 1) != 1 || fl("n7", 0) != 1 || fl("csave", 0) != 1 || fl("qrollback", 0) != 1)
            {
                D.roll.reset();
                log("NOLA qdiv: o host venceu em " + sid + " mas o cliente nao pode voltar (precisa qdivroll=1 n7=1 csave=1 qrollback=1): fica adiantado");
                return;
            }
            double gap = fl("qdivgap", 90);
            if (t < D.rollAt + gap)
            {
                if (!R.held)
                {
                    R.held = true;
                    log(F("NOLA qdiv: ordem #%lld (%s) chegou %.0f s depois do ultimo retorno: espero %.0f s e volto uma vez so", R.rn, sid.c_str(), t - D.rollAt, gap));
                }
                R.at = t;
                return;
            }
            if (!E->n7_idle() || SQ().rbWait)
            {
                if (t - R.at > 120)
                {
                    D.roll.reset();
                    log("NOLA qdiv: retorno por " + sid + " desistido: n7 ocupado ha 120 s");
                }
                return;
            }
            if (!R.said)
            {
                R.said = true;
                log(F("NOLA qdiv: o host venceu em %s (ordem #%lld): volto ao estado do host (save pareado + catch-up + meus itens)", sid.c_str(), R.rn));
            }
            double at = R.at;
            E->n7_follow();
            if (!E->n7_idle()) { D.roll.reset(); D.rollAt = t; }
            else if (t - at > 120)
            {
                D.roll.reset();
                log("NOLA qdiv: retorno por " + sid + " desistido (120 s)");
            }
        }

        void qdClient(Qd& D, double t)
        {
            std::string s;
            if (has_part("QD", s) && starts(s, "h,"))
            {
                D.peer = true;
                size_t sc = s.find(';');
                std::string head = sc == std::string::npos ? s : s.substr(0, sc);
                std::string list = sc == std::string::npos ? std::string() : s.substr(sc + 1);
                auto p = split_keep(head, ',');
                std::optional<long long> rn;
                std::optional<std::string> rsid;
                std::string hr;
                bool okp = (p.size() == 4 || p.size() == 5) && p[0] == "h" && all_digits(p[2]);
                if (okp && p.size() == 5 && !p[4].empty() && !all_digits(p[4])) okp = false;
                if (okp)
                {
                    D.peerJ = p[1];
                    rn = std::atoll(p[2].c_str());
                    rsid = p[3];
                    hr = p.size() == 5 ? p[4] : std::string();
                }
                else D.peerJ.reset();
                if (!hr.empty()) D.hrep = std::atoll(hr.c_str());
                else D.hrep.reset();
                D.hpend.clear();
                for (auto& sid : gm(list, '|')) D.hpend.insert(sid);
                int sess = E->peer_sessions();
                if (rn && (!D.hsess || *D.hsess != sess))
                {
                    D.hsess = sess;
                    D.rnDone = *rn;
                }
                else if (rn && *rn < D.rnDone) D.rnDone = *rn;
                else if (rn && *rn > D.rnDone)
                {
                    D.rnDone = *rn;
                    D.nroll += 1;
                    if (S.trig && rsid && *rsid != "-")
                    {
                        S.trig->sent.erase(*rsid);
                        S.trig->inside.erase(*rsid);
                    }
                    if (S.trig && fl("qdroll1", 1) == 1 && !S.trig->sent.empty())
                    {
                        log(F("NOLA qdiv: rollback order #%lld: the reload undoes every client step before it; %d step(s) leave the divergence list", *rn,
                              static_cast<int>(S.trig->sent.size())));
                        D.ndrop += static_cast<int>(S.trig->sent.size());
                        S.trig->sent.clear();
                    }
                    QdRoll R;
                    R.sid = rsid ? *rsid : std::string("-");
                    R.rn = *rn;
                    R.at = t;
                    D.roll = R;
                }
            }
            else
            {
                D.peer = false;
                D.peerJ.reset();
                D.hpend.clear();
            }
            if (D.roll) qdRoll(D, t);
            Id pw = E->pawn();
            if (pw && pw != D.pawnAt)
            {
                if (D.pawnAt && S.trig && fl("qdroll1", 1) == 1)
                {
                    int n = 0;
                    for (auto it = S.trig->sent.begin(); it != S.trig->sent.end();)
                    {
                        if (it->second < t) { it = S.trig->sent.erase(it); ++n; }
                        else ++it;
                    }
                    if (n > 0)
                    {
                        D.ndrop += n;
                        log(F("NOLA qdiv: new client pawn (load): %d client step(s) from before the load leave the divergence list", n));
                    }
                }
                D.pawnAt = pw;
            }
            bool hold = false;
            double young = fl("qdivm", 20) + 15;
            if (S.trig)
            {
                for (auto& [sid, at] : S.trig->sent)
                {
                    if (t - at < young || D.hpend.count(sid))
                    {
                        hold = true;
                        break;
                    }
                }
            }
            if (hold || D.roll)
            {
                auto& Q = SQ();
                Q.csNext = std::max(Q.csNextSet ? Q.csNext : 0.0, t + 2);
                Q.csNextSet = true;
            }
            D.hold = hold;
        }

        void qdBuild(Qd& D, double t)
        {
            std::string j = qdJ();
            D.myJ = j;
            if (host())
            {
                int NV = nv();
                if (NV > 1)
                {
                    for (int v = 0; v < NV; ++v)
                    {
                        std::vector<std::string> p;
                        for (auto& [sid, x] : D.pend)
                            if (x.views.count(v)) p.push_back(sid);
                        std::sort(p.begin(), p.end());
                        std::string o = F("#QD:h,%s,%lld,%s,%d;%s", j.c_str(), v == 0 ? D.rn : D.vrn[(size_t)v], v == 0 ? D.rsid.c_str() : D.vrsid[(size_t)v].c_str(), D.nrep,
                                          join(p, "|").c_str());
                        if (v == 0) D.out = o;
                        else D.vout[(size_t)v] = o;
                    }
                }
                else
                {
                    std::vector<std::string> p;
                    for (auto& [sid, x] : D.pend) p.push_back(sid);
                    std::sort(p.begin(), p.end());
                    D.out = F("#QD:h,%s,%lld,%s,%d;%s", j.c_str(), D.rn, D.rsid.c_str(), D.nrep, join(p, "|").c_str());
                }
            }
            else
            {
                struct A
                {
                    std::string sid;
                    double age;
                };
                std::vector<A> arr;
                if (S.trig)
                {
                    for (auto& [sid, at] : S.trig->sent)
                    {
                        double age = t - at;
                        if (age >= 0 && age <= fl("qdivkeep", 600)) arr.push_back({sid, age});
                    }
                }
                std::stable_sort(arr.begin(), arr.end(), [](const A& a, const A& b) { return a.age < b.age; });
                std::vector<std::string> v;
                size_t lim = static_cast<size_t>(std::max(0.0, fl("qdivn", 12)));
                for (size_t i = 0; i < arr.size() && i < lim; ++i) v.push_back(F("%s:%d", arr[i].sid.c_str(), static_cast<int>(std::floor(arr[i].age))));
                D.out = F("#QD:c,%s,%lld;%s", j.c_str(), D.rnDone, join(v, "|").c_str());
            }
            D.outAt = t;
        }

        uint32_t qcHash(std::string_view s)
        {
            uint32_t h = 5381;
            for (unsigned char c : s) h = h * 33u + c;
            return h;
        }

        Qc& qcSt()
        {
            if (!S.qc) S.qc.emplace();
            return *S.qc;
        }

        bool qcTab(Qc& C)
        {
            if (C.loaded != 0) return C.loaded > 0;
            C.loaded = -1;
            std::string text;
            if (!E->data("s2u_qcatch.tsv", text))
            {
                log("NOLA qcatch: s2u_qcatch.tsv ausente: o catch-up de nos de missao fica desligado");
                return false;
            }
            for (auto& ln : lines(text))
            {
                auto c = split_keep(ln, '\t');
                if (c.size() < 3 || c[0].empty()) continue;
                QcRow r;
                r.q = c[1];
                if (c[2] != "-") r.nodes = words(c[2]);
                r.add = c.size() > 3 && c[3] == "A";
                if (c.size() > 4 && c[4] != "-")
                    for (auto& w : words(c[4]))
                    {
                        size_t e = w.find('='), k = w.rfind(':');
                        if (e == std::string::npos || k == std::string::npos || k < e) continue;
                        r.item[w.substr(0, e)] = {w.substr(e + 1, k - e - 1), std::max(1, std::atoi(w.c_str() + k + 1))};
                    }
                if (r.nodes.empty() && !r.add) continue;
                C.byH[qcHash(c[0])] = c[0];
                C.tab[c[0]] = std::move(r);
            }
            if (C.tab.empty())
            {
                log("NOLA qcatch: s2u_qcatch.tsv vazio: o catch-up de nos de missao fica desligado");
                return false;
            }
            C.loaded = 1;
            return true;
        }

        void qcReach(const std::string& sid)
        {
            if (fl("qcatch", 1) != 1) return;
            qcSt().own[sid] = now();
        }

        bool qcPeerUp(int v) { return v == 0 ? E->peer_state() == 1 : E->peer_state_v(v) == 1; }

        void qcPeer(Qc& C, int v, const std::string& s, double t)
        {
            std::map<std::string, double> nw;
            auto& old = C.peer[static_cast<size_t>(v)];
            size_t sc = s.find(';');
            std::string list = sc == std::string::npos ? std::string() : s.substr(sc + 1);
            for (auto& ent : gm(list, '|'))
            {
                size_t c = ent.find(':');
                if (c != 8) continue;
                std::string h = ent.substr(0, c), a = ent.substr(c + 1);
                bool hex = all_digits(a);
                for (char ch : h)
                    if (!std::isxdigit(static_cast<unsigned char>(ch))) hex = false;
                if (!hex) continue;
                auto it = C.byH.find(static_cast<uint32_t>(std::strtoul(h.c_str(), nullptr, 16)));
                if (it == C.byH.end()) continue;
                double tp = t - std::atof(a.c_str());
                auto o = old.find(it->second);
                if (o != old.end() && std::fabs(o->second - tp) <= 3) tp = o->second;
                nw[it->second] = tp;
            }
            old = std::move(nw);
        }

        void qcBuild(Qc& C, double t)
        {
            struct A
            {
                const std::string* sid;
                double age;
            };
            std::vector<A> arr;
            double keep = fl("qcatchkeep", 600);
            for (auto& [sid, at] : C.own)
            {
                double age = t - at;
                if (age >= 0 && age <= keep && C.tab.count(sid)) arr.push_back({&sid, age});
            }
            std::stable_sort(arr.begin(), arr.end(), [](const A& a, const A& b) { return a.age < b.age; });
            std::vector<std::string> v;
            size_t lim = static_cast<size_t>(std::max(0.0, fl("qcatchn", 32)));
            for (size_t i = 0; i < arr.size() && i < lim; ++i) v.push_back(F("%08x:%d", qcHash(*arr[i].sid), static_cast<int>(std::floor(arr[i].age))));
            C.out = F("#QF:%d;%s", static_cast<int>(v.size()), join(v, "|").c_str());
            C.outAt = t;
        }

        bool qcHeld(const std::string& sid)
        {
            if (!S.qd) return false;
            if (host()) return S.qd->pend.count(sid) > 0 || S.qd->wonV.count(sid) > 0;
            return S.qd->roll.has_value();
        }

        bool qcSynced(const Qc& C, const std::string& sid, double tp)
        {
            double keep = fl("qcatchkeep", 600);
            auto o = C.own.find(sid);
            if (o != C.own.end() && o->second >= tp - keep) return true;
            auto d = C.done.find(sid);
            return d != C.done.end() && tp <= d->second + keep;
        }

        void qcEnd(Qc& C, bool ok, double t, const std::string& why)
        {
            QcEp P = *C.cur;
            C.cur.reset();
            C.done[P.sid] = P.tp;
            auto it = C.tab.find(P.sid);
            std::string q = it != C.tab.end() ? it->second.q : std::string("?");
            std::string ns = it != C.tab.end() ? join(it->second.nodes, " ") : std::string("-");
            if (ok) C.nok += 1;
            else C.nmiss += 1;
            std::string ex;
            if (P.iheld) ex += F("; %d no(s) de item esperaram o item chegar", P.iheld);
            if (P.added) ex += "; ItemAdd iniciado com XStartQuestNodeBySID";
            log(F("NOLA qcatch: o grafo do %s estava atras em %s (missao %s): o outro lado%s chegou ha %.0f s; XEndQuestNodeBySID %s em %d rodada(s)%s -> %s", host() ? "host" : "cliente",
                  P.sid.c_str(), q.c_str(), vwho(P.v).c_str(), t - P.tp, ns.empty() ? "-" : ns.c_str(), P.rounds, ex.c_str(), ok ? "alcancou" : why.c_str()));
        }

        bool qcItemWait(const QcRow& R, const std::string& n, QcEp& P, double t)
        {
            auto f = R.item.find(n);
            if (f == R.item.end() || fl("qcatchitem", 30) <= 0) return false;
            Id pw = E->pawn();
            if (!pw || E->count(pw, f->second.first) >= f->second.second) return false;
            if (t - P.first >= fl("qcatchitem", 30)) return false;
            if (!P.iw.count(n))
            {
                P.iw.insert(n);
                P.iheld += 1;
                qcSt().nitem += 1;
            }
            return true;
        }

        bool qcBusy(Qc& C)
        {
            std::string why;
            if (!E->busy(why))
            {
                C.busy.clear();
                return false;
            }
            if (C.busy.empty()) C.ndefer += 1;
            C.busy = why.empty() ? std::string("ocupado") : why;
            return true;
        }

        void qcRound(Qc& C, double t)
        {
            QcEp& P = *C.cur;
            auto o = C.own.find(P.sid);
            if (o != C.own.end() && o->second >= P.first - 1)
            {
                qcEnd(C, true, t, "");
                return;
            }
            if (t - P.last < fl("qcatchgap", 5)) return;
            auto it = C.tab.find(P.sid);
            if (it == C.tab.end())
            {
                qcEnd(C, false, t, "fora da tabela");
                return;
            }
            const QcRow& R = it->second;
            int tries = static_cast<int>(fl("qcatchtry", 3));
            bool iw = false;
            for (auto& n : R.nodes)
                if (qcItemWait(R, n, P, t)) iw = true;
            bool last = false;
            if (P.rounds >= tries || R.nodes.empty())
            {
                if (iw) return;
                if (P.iheld > 0 && !P.flush) last = true;
                else if (R.add && !P.added && fl("qcatchs2c", 1) == 1 && C.s2c.count(P.sid) && C.s2c[P.sid] >= P.tp - 30)
                {
                    qcEnd(C, false, t, "o ItemAdd ja rodou aqui pela copia _S2C: nao repetido (qcatchs2c)");
                    return;
                }
                else if (R.add && !P.added && fl("qcatchadd", 1) == 1)
                {
                    if (qcBusy(C)) return;
                    std::string e;
                    P.added = true;
                    P.last = t;
                    if (aaCall("XStartQuestNodeBySID", P.sid, e)) C.nadd += 1;
                    else P.err = e;
                    return;
                }
                else
                {
                    qcEnd(C, false, t, P.added ? std::string("nao alcancou: o ItemAdd iniciado nao rodou aqui") + (P.err.empty() ? "" : ": " + P.err)
                                       : P.ok == 0 ? "a chamada falhou: " + P.err : std::string("nao alcancou: nenhum desses nos estava ativo aqui ou falta outra condicao"));
                    return;
                }
            }
            if (qcBusy(C)) return;
            if (last) P.flush = true;
            else P.rounds += 1;
            P.last = t;
            for (auto& n : R.nodes)
            {
                if (!last && qcItemWait(R, n, P, t)) continue;
                std::string e;
                if (P.rounds == 1 && host() && on("cjoin", 0) && E->mq_sid(n)) E->cj_cause(n);
                if (!host() && fl("qdmir", 1) == 1) C.mir[n] = t;
                if (aaCall("XEndQuestNodeBySID", n, e))
                {
                    P.ok += 1;
                    if (host() && S.trig) S.trig->fired[n] = t;
                }
                else P.err = e;
            }
        }

        void qcTick()
        {
            if (fl("qcatch", 1) != 1)
            {
                if (S.qc)
                {
                    S.qc->cur.reset();
                    S.qc->out.clear();
                }
                return;
            }
            Qc& C = qcSt();
            double t = now();
            Id pw = E->pawn();
            if (!pw && C.goneAt < 0) C.goneAt = t;
            if (pw && pw != C.pawnAt)
            {
                if (C.cur) qcEnd(C, false, t, "este jogo recarregou");
                double cut = C.goneAt >= 0 ? C.goneAt : t;
                for (auto it = C.own.begin(); it != C.own.end();)
                    it = it->second < cut ? C.own.erase(it) : std::next(it);
                for (auto it = C.s2c.begin(); it != C.s2c.end();)
                    it = it->second < cut ? C.s2c.erase(it) : std::next(it);
                C.done.clear();
                C.hostOnly.clear();
                C.divMuted.clear();
                C.pawnAt = pw;
                C.loadAt = t;
            }
            if (pw) C.goneAt = -1;
            int NV = host() ? nv() : 1;
            bool up = false;
            for (int v = 0; v < NV; ++v)
            {
                if (!qcPeerUp(v))
                {
                    C.peer[static_cast<size_t>(v)].clear();
                    if (C.cur && C.cur->v == v) qcEnd(C, false, t, "o outro lado saiu");
                    continue;
                }
                up = true;
                std::string s;
                if (has_partv(v, "QF", s) && qcTab(C)) qcPeer(C, v, s, t);
            }
            if (!up)
            {
                C.out.clear();
                C.busy.clear();
                return;
            }
            if (!qcTab(C)) return;
            if (t - C.buildAt >= 2)
            {
                C.buildAt = t;
                qcBuild(C, t);
            }
            if (C.cur)
            {
                qcRound(C, t);
                return;
            }
            if (!pw || !E->ql_settled() || t - C.loadAt < fl("qcatchsettle", 30) || SQ().rbWait || !E->n7_idle())
            {
                C.busy.clear();
                return;
            }
            double wait = fl("qcatchwait", 25);
            std::optional<QcEp> best;
            for (int v = 0; v < NV; ++v)
                for (auto& [sid, tp] : C.peer[static_cast<size_t>(v)])
                {
                    if (tp < C.loadAt || t - tp < wait || qcSynced(C, sid, tp) || qcHeld(sid)) continue;
                    if (!host())
                    {
                        auto r = C.tab.find(sid);
                        if (r != C.tab.end() && E->quest_muted(r->second.q))
                        {
                            if (C.hostOnly.insert(sid).second)
                                log(F("NOLA qcatch: %s (missao %s) nao e alcancado no cliente: a missao so roda no host (qmuteq), o efeito chega pela copia _S2C", sid.c_str(),
                                      r->second.q.c_str()));
                            continue;
                        }
                    }
                    if (!best || tp < best->tp)
                    {
                        QcEp e;
                        e.sid = sid;
                        e.v = v;
                        e.tp = tp;
                        best = e;
                    }
                }
            if (!best)
            {
                C.busy.clear();
                return;
            }
            if (qcBusy(C)) return;
            best->first = t;
            C.cur = best;
            qcRound(C, t);
        }

        std::string qcOut()
        {
            if (fl("qcatch", 1) != 1 || !S.qc || now() - S.qc->outAt > 5) return "";
            return S.qc->out;
        }

        void qcDiv(std::vector<std::string>& div, double t)
        {
            if (!S.qc || fl("qcatch", 1) != 1) return;
            Qc& C = *S.qc;
            std::string b = C.busy.empty() ? std::string() : "; adiado: " + C.busy;
            if (C.cur) div.push_back(F("%s [no do grafo atras: catch-up, rodada %d%s]", C.cur->sid.c_str(), C.cur->rounds, b.c_str()));
            std::set<std::string> seen;
            int more = 0;
            for (auto& pm : C.peer)
                for (auto& [sid, tp] : pm)
                {
                    if ((C.cur && C.cur->sid == sid) || seen.count(sid) || tp < C.loadAt || t - tp < fl("qdivm", 20) || qcSynced(C, sid, tp)) continue;
                    if (!host() && fl("qdivmute", 1) == 1)
                    {
                        auto r = C.tab.find(sid);
                        if (r != C.tab.end() && E->quest_muted(r->second.q))
                        {
                            if (C.divMuted.insert(sid).second)
                                log(F("NOLA qdiv: %s (missao %s) e so do host (qmuteq): nao e divergencia", sid.c_str(), r->second.q.c_str()));
                            continue;
                        }
                    }
                    seen.insert(sid);
                    if (seen.size() > 4)
                    {
                        more += 1;
                        continue;
                    }
                    div.push_back(F("%s [no do grafo atras: o outro lado chegou ha %.0f s%s]", sid.c_str(), t - tp, b.c_str()));
                }
            if (more > 0) div.push_back(F("mais %d nos do grafo atras", more));
        }

        Qb& qbSt()
        {
            if (!S.qb)
            {
                S.qb.emplace();
                S.qb->epoch = E->unix_time() % 1000000;
            }
            return *S.qb;
        }

        std::string qbLc(const std::string& s)
        {
            std::string o = s;
            for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return o;
        }

        bool qbTab(Qb& B)
        {
            if (B.loaded != 0) return B.loaded > 0;
            B.loaded = -1;
            std::string text;
            if (!E->data("s2u_qbranch.tsv", text))
            {
                log("NOLA qbranch: s2u_qbranch.tsv ausente: escolhas e ramos de missao nao sao forcados");
                return false;
            }
            for (auto& ln : lines(text))
            {
                auto c = split_keep(ln, '\t');
                if (c.size() >= 5 && c[0] == "N" && !c[1].empty() && c[2].size() == 1)
                {
                    QbNode n;
                    n.kind = c[2][0];
                    n.q = c[3];
                    if (c[4] != "-") n.pins = words(c[4]);
                    B.byH[qcHash(c[1])] = c[1];
                    B.lcN[qbLc(c[1])] = c[1];
                    B.tab[c[1]] = std::move(n);
                }
                else if (c.size() >= 3 && c[0] == "A" && !c[1].empty())
                {
                    B.ans[c[1]] = c[2];
                    B.ansH[qcHash(c[1])] = c[1];
                    B.lcA[qbLc(c[1])] = c[1];
                }
            }
            if (B.tab.empty())
            {
                log("NOLA qbranch: s2u_qbranch.tsv vazio: escolhas e ramos de missao nao sao forcados");
                return false;
            }
            B.loaded = 1;
            return true;
        }

        bool qbOn() { return E && fl("qbranch", 1) == 1 && qbTab(qbSt()); }

        const char* qbKind(char k)
        {
            switch (k)
            {
            case 'D': return "dialogo";
            case 'I': return "If";
            case 'R': return "Random";
            case 'B': return "comportamento";
            case 'C': return "container";
            case 'E': return "BridgeEvent";
            case 'W': return "tutorial";
            case 'T': return "infotopico";
            default: return "no";
            }
        }

        const std::string* qbName(const std::map<std::string, std::string>& lc, const std::string& s)
        {
            auto it = lc.find(qbLc(s));
            return it == lc.end() ? nullptr : &it->second;
        }

        int qbPin(const QbNode& N, const std::string& pin)
        {
            if (pin.empty()) return -1;
            std::string p = qbLc(pin);
            for (size_t i = 0; i < N.pins.size(); ++i)
                if (qbLc(N.pins[i]) == p) return static_cast<int>(i);
            return -2;
        }

        std::string qbPinS(const QbNode& N, int k) { return k >= 0 && k < static_cast<int>(N.pins.size()) ? N.pins[static_cast<size_t>(k)] : std::string(); }

        std::string qbShow(const std::string& p) { return p.empty() ? std::string("(sem pino)") : p; }

        void qbTrim(std::vector<QbRec>& v, double t)
        {
            double keep = fl("qbkeep", 900);
            size_t lim = static_cast<size_t>(std::max(1.0, fl("qbn", 48)));
            size_t per = static_cast<size_t>(std::max(1.0, fl("qbper", 4)));
            std::map<std::string, size_t> n;
            std::vector<QbRec> k;
            for (auto it = v.rbegin(); it != v.rend(); ++it)
            {
                if (t - it->tp > keep || k.size() >= lim || n[it->node] >= per) continue;
                n[it->node] += 1;
                k.push_back(*it);
            }
            v.assign(k.rbegin(), k.rend());
        }

        const QbRec* qbFind(Qb& B, const std::string& node, bool fin)
        {
            const QbRec* best = nullptr;
            for (auto& [k, r] : B.peer)
                if (r.node == node && r.fin == fin && !B.used.count(k) && (!best || r.seq < best->seq)) best = &r;
            return best;
        }

        bool qbBusy(Qb& B)
        {
            std::string why;
            if (!E->busy(why))
            {
                B.busy.clear();
                return false;
            }
            if (B.busy.empty()) B.ndefer += 1;
            B.busy = why.empty() ? std::string("ocupado") : why;
            return true;
        }

        std::string qbHeldKey(const std::string& node, const std::string& pin, bool fin) { return fin ? node + "#" : node + "#" + qbLc(pin); }

        int qbForce(Qb& B, const std::string& node, const std::string& pin, bool fin, std::string& err)
        {
            B.forcing = node;
            int r = E->qb_force(node, B.tab[node].q, pin, fin, err);
            B.forcing.clear();
            if (r > 0)
            {
                B.nforce += 1;
                B.done.insert(qbHeldKey(node, pin, fin));
            }
            else if (r < 0) B.nerr += 1;
            return r;
        }

        int qbFire(const std::string& node0, const std::string& pin, bool fin, std::string& sub)
        {
            if (!qbOn()) return 0;
            Qb& B = qbSt();
            const std::string* nm = qbName(B.lcN, node0);
            if (!nm || qbLc(B.forcing) == qbLc(*nm)) return 0;
            const std::string node = *nm;
            const QbNode& N = B.tab[node];
            if (pin.empty() && N.kind != 'T') return 0;
            int k = qbPin(N, pin);
            if (k == -2) return 0;
            double t = now();
            if (host())
            {
                B.seq += 1;
                QbRec r;
                r.key = std::to_string(B.seq);
                r.seq = B.seq;
                r.node = node;
                r.pin = k;
                r.fin = fin;
                r.tp = t;
                B.mine.push_back(r);
                qbTrim(B.mine, t);
                B.nrec += 1;
                return 0;
            }
            if (!qcPeerUp(0)) return 0;
            std::string hk = qbHeldKey(node, pin, fin);
            if (const QbRec* r = qbFind(B, node, fin))
            {
                B.used.insert(r->key);
                B.held.erase(hk);
                if (r->pin == k)
                {
                    B.nok += 1;
                    return 0;
                }
                sub = qbPinS(N, r->pin);
                B.nsub += 1;
                B.done.insert(qbHeldKey(node, sub, fin));
                log(F("NOLA qbranch: %s %s (missao %s): o cliente ia sair por %s; o host saiu por %s: forcado o caminho do host", qbKind(N.kind), node.c_str(), N.q.c_str(),
                      qbShow(pin).c_str(), qbShow(sub).c_str()));
                return 1;
            }
            if (!fin && B.done.count(hk))
            {
                B.ndup += 1;
                return 2;
            }
            QbHeld& h = B.held[hk];
            if (h.n == 0)
            {
                h.node = node;
                h.at = t;
                B.nhold += 1;
                log(F("NOLA qbranch: %s %s (missao %s): saida %s do cliente segurada: so o host decide este ramo", qbKind(N.kind), node.c_str(), N.q.c_str(), qbShow(pin).c_str()));
            }
            h.n += 1;
            h.pin = qbPinS(N, k);
            h.fin = fin;
            return 2;
        }

        bool qbAnswer(const std::string& phrase0)
        {
            if (!qbOn() || fl("qbgate", 1) != 1) return true;
            Qb& B = qbSt();
            const std::string* nm = qbName(B.lcA, phrase0);
            if (!nm) return true;
            const std::string phrase = *nm;
            const std::string& dlg = B.ans[phrase];
            double t = now();
            if (host())
            {
                B.seq += 1;
                QbRec r;
                r.key = std::to_string(B.seq);
                r.seq = B.seq;
                r.node = phrase;
                r.tp = t;
                B.picks.push_back(r);
                qbTrim(B.picks, t);
                return true;
            }
            if (!qcPeerUp(0) || B.mirror == phrase) return true;
            B.nref += 1;
            if (t - B.noteAt >= 4)
            {
                B.noteAt = t;
                E->screen("s2m_session_qb", nola_text::tx(nola_text::T::qb_host_only), 6);
            }
            auto f = B.refAt.find(phrase);
            if (f == B.refAt.end() || t - f->second >= 10)
            {
                log(F("NOLA qbranch: resposta %s (dialogo %s) recusada no cliente: so o host escolhe o caminho da missao", phrase.c_str(), dlg.c_str()));
                B.refAt[phrase] = t;
            }
            return false;
        }

        void qbPeer(Qb& B, const std::string& s, double t)
        {
            std::string_view body(s);
            if (body.rfind("#QB:", 0) == 0) body.remove_prefix(4);
            auto sec = split_keep(body, ';');
            if (sec.size() < 3) return;
            S2FR(x0::fr::handled("QB", body));
            const std::string& ep = sec[0];
            std::map<std::string, QbRec> nw, na;
            for (auto& ent : gm(sec[1], '|'))
            {
                auto p = split_keep(ent, ':');
                if (p.size() != 5 || !all_digits(p[0]) || p[1].size() != 8 || !all_digits(p[4])) continue;
                auto h = B.byH.find(static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 16)));
                if (h == B.byH.end()) continue;
                QbRec r;
                r.key = ep + ":" + p[0];
                r.seq = std::atoll(p[0].c_str());
                r.node = h->second;
                r.pin = std::atoi(p[2].c_str());
                r.fin = p[3] == "1";
                auto o = B.peer.find(r.key);
                r.tp = o != B.peer.end() ? o->second.tp : t - std::atof(p[4].c_str());
                nw[r.key] = r;
            }
            for (auto& ent : gm(sec[2], '|'))
            {
                auto p = split_keep(ent, ':');
                if (p.size() != 3 || !all_digits(p[0]) || p[1].size() != 8 || !all_digits(p[2])) continue;
                auto h = B.ansH.find(static_cast<uint32_t>(std::strtoul(p[1].c_str(), nullptr, 16)));
                if (h == B.ansH.end()) continue;
                QbRec r;
                r.key = ep + ":a" + p[0];
                r.seq = std::atoll(p[0].c_str());
                r.node = h->second;
                auto o = B.peerA.find(r.key);
                r.tp = o != B.peerA.end() ? o->second.tp : t - std::atof(p[2].c_str());
                na[r.key] = r;
            }
            std::set<std::string> u;
            for (auto& k : B.used)
                if (nw.count(k) || na.count(k)) u.insert(k);
            B.used = std::move(u);
            std::map<std::string, std::pair<double, int>> tr;
            for (auto& [k, v] : B.tries)
                if (nw.count(k)) tr[k] = v;
            B.tries = std::move(tr);
            B.peer = std::move(nw);
            B.peerA = std::move(na);
        }

        void qbRelease(Qb& B, const std::string& why)
        {
            for (auto& [k, h] : B.held)
            {
                std::string e;
                int r = qbForce(B, h.node, h.pin, h.fin, e);
                B.nrel += 1;
                log(F("NOLA qbranch: %s: %s: saida %s do cliente liberada%s", h.node.c_str(), why.c_str(), qbShow(h.pin).c_str(),
                      r > 0 ? "" : r == 0 ? ": o no ja nao estava ativo" : (": falhou: " + e).c_str()));
            }
            B.held.clear();
        }

        void qbMids(Qb& B, const QbRec& fin, const QbNode& N)
        {
            std::vector<const QbRec*> m;
            for (auto& [k, r] : B.peer)
                if (!r.fin && r.node == fin.node && r.seq < fin.seq && !B.used.count(k)) m.push_back(&r);
            std::sort(m.begin(), m.end(), [](const QbRec* a, const QbRec* b) { return a->seq < b->seq; });
            for (const QbRec* r : m)
            {
                std::string pin = qbPinS(N, r->pin), e;
                B.used.insert(r->key);
                int ok = qbForce(B, r->node, pin, false, e);
                log(F("NOLA qbranch: %s %s (missao %s): saida intermediaria %s do host disparada neste cliente%s", qbKind(N.kind), r->node.c_str(), N.q.c_str(), qbShow(pin).c_str(),
                      ok > 0 ? "" : (": falhou: " + e).c_str()));
            }
        }

        void qbTick()
        {
            if (fl("qbranch", 1) != 1)
            {
                if (S.qb && !S.qb->held.empty()) qbRelease(*S.qb, "qbranch desligado");
                return;
            }
            Qb& B = qbSt();
            if (!qbTab(B) || host()) return;
            double t = now();
            Id pw = E->pawn();
            if (pw && pw != B.pawnAt)
            {
                if (B.pawnAt) log(F("NOLA qbranch: este jogo recarregou: %d saida(s) segurada(s) descartada(s); as escolhas do host valem de novo", static_cast<int>(B.held.size())));
                B.pawnAt = pw;
                B.loadAt = t;
                B.held.clear();
                B.used.clear();
                B.done.clear();
                B.tries.clear();
            }
            if (!qcPeerUp(0))
            {
                if (!B.held.empty()) qbRelease(B, "o host saiu");
                return;
            }
            std::string s;
            if (E->part("QB", s)) qbPeer(B, s, t);
            if (fl("qbmirror", 1) == 1)
                for (auto& [k, r] : B.peerA)
                {
                    if (B.used.count(k) || t - r.tp > fl("qbkeep", 900) || !E->dlg_has(r.node)) continue;
                    std::string e;
                    B.used.insert(k);
                    B.mirror = r.node;
                    bool ok = E->dlg_pick(r.node, e);
                    B.mirror.clear();
                    if (ok) B.nmir += 1;
                    else B.nerr += 1;
                    log(F("NOLA qbranch: o host escolheu %s (dialogo %s): a mesma resposta no dialogo deste cliente%s", r.node.c_str(), B.ans[r.node].c_str(),
                          ok ? "" : (": falhou: " + e).c_str()));
                }
            std::vector<std::string> gone;
            for (auto& [k, h] : B.held)
            {
                auto it = B.tab.find(h.node);
                if (it == B.tab.end())
                {
                    gone.push_back(k);
                    continue;
                }
                const QbNode& N = it->second;
                const QbRec* r = qbFind(B, h.node, h.fin);
                if (r)
                {
                    if (qbBusy(B))
                    {
                        h.defer += 1;
                        continue;
                    }
                    std::string pin = qbPinS(N, r->pin), e;
                    B.used.insert(r->key);
                    QbRec rc = *r;
                    int ok = qbForce(B, h.node, pin, h.fin, e);
                    log(F("NOLA qbranch: %s %s (missao %s): o host saiu por %s; a saida %s do cliente virou a do host%s", qbKind(N.kind), h.node.c_str(), N.q.c_str(),
                          qbShow(pin).c_str(), qbShow(h.pin).c_str(), ok > 0 ? "" : ok == 0 ? ": o no ja nao estava ativo" : (": falhou: " + e).c_str()));
                    if (ok > 0 && h.fin) qbMids(B, rc, N);
                    gone.push_back(k);
                    continue;
                }
                if (N.kind == 'D' || N.kind == 'T' || t - h.at < fl("qbhold", 120)) continue;
                std::string e;
                int ok = qbForce(B, h.node, h.pin, h.fin, e);
                B.nrel += 1;
                log(F("NOLA qbranch: %s %s (missao %s): o host nao passou por este ramo em %.0f s: saida %s do cliente liberada%s", qbKind(N.kind), h.node.c_str(), N.q.c_str(),
                      t - h.at, qbShow(h.pin).c_str(), ok > 0 ? "" : ok == 0 ? ": o no ja nao estava ativo" : (": falhou: " + e).c_str()));
                gone.push_back(k);
            }
            for (auto& k : gone) B.held.erase(k);
            if (t - B.loadAt < fl("qbsettle", 10)) return;
            std::vector<QbRec> todo;
            for (auto& [k, r] : B.peer)
            {
                if (!r.fin || B.used.count(k) || r.tp < B.loadAt || t - r.tp < fl("qbwait", 10) || t - r.tp > fl("qbkeep", 900)) continue;
                auto it = B.tab.find(r.node);
                if (it == B.tab.end() || it->second.kind == 'C' || B.held.count(qbHeldKey(r.node, "", true))) continue;
                auto tr = B.tries.find(k);
                if (tr != B.tries.end() && t - tr->second.first < fl("qbretry", 5)) continue;
                todo.push_back(r);
            }
            std::sort(todo.begin(), todo.end(), [](const QbRec& a, const QbRec& b) { return a.seq < b.seq; });
            for (auto& r : todo)
            {
                if (B.used.count(r.key)) continue;
                if (qbBusy(B)) return;
                const QbNode& N = B.tab[r.node];
                std::string pin = qbPinS(N, r.pin), e;
                auto& tr = B.tries[r.key];
                tr.first = t;
                int ok = qbForce(B, r.node, pin, true, e);
                if (ok == 0) continue;
                if (ok < 0 && ++tr.second < static_cast<int>(fl("qbtries", 3))) continue;
                B.used.insert(r.key);
                if (ok > 0)
                    log(F("NOLA qbranch: %s %s (missao %s) ativo aqui; o host saiu por %s ha %.0f s: forcado neste cliente", qbKind(N.kind), r.node.c_str(), N.q.c_str(), qbShow(pin).c_str(),
                          t - r.tp));
                else
                    log(F("NOLA qbranch: %s %s (missao %s): a saida %s do host nao foi forcada neste cliente em %d tentativa(s): %s", qbKind(N.kind), r.node.c_str(), N.q.c_str(),
                          qbShow(pin).c_str(), tr.second, e.c_str()));
                if (ok > 0) qbMids(B, r, N);
            }
        }

        std::string qbOut()
        {
            if (!E || !host() || fl("qbranch", 1) != 1 || !S.qb || S.qb->loaded <= 0) return "";
            Qb& B = *S.qb;
            double t = now();
            qbTrim(B.mine, t);
            qbTrim(B.picks, t);
            if (B.mine.empty() && B.picks.empty()) return "";
            std::vector<std::string> a, b;
            for (auto& r : B.mine)
                a.push_back(F("%lld:%08x:%d:%d:%d", r.seq, qcHash(r.node), r.pin, r.fin ? 1 : 0, static_cast<int>(std::floor(t - r.tp))));
            for (auto& r : B.picks) b.push_back(F("%lld:%08x:%d", r.seq, qcHash(r.node), static_cast<int>(std::floor(t - r.tp))));
            return F("#QB:%lld;%s;%s", B.epoch, join(a, "|").c_str(), join(b, "|").c_str());
        }

        void qdLog(Qd& D, double t)
        {
            if (t - D.logAt < fl("qdivlog", 30)) return;
            D.logAt = t;
            int fixed = D.nrep + D.nroll - D.nlast;
            D.nlast = D.nrep + D.nroll;
            std::vector<std::string> div;
            if (host())
            {
                for (auto& [sid, p] : D.pend) div.push_back(sid + " [" + (p.why ? *p.why : std::string("aguardando")) + "]");
            }
            else
            {
                for (auto& sid : D.hpend) div.push_back(sid + " [o host ainda nao tem]");
                if (D.roll) div.push_back("retorno ao host pendente (" + D.roll->sid + ")");
                if (fl("qnohold", 0) != 1)
                {
                    auto& Q = SQ();
                    size_t held = (Q.rbQueue ? Q.rbQueue->size() : 0) + (Q.csQueue ? Q.csQueue->size() : 0);
                    bool hasW = Q.rbWait.has_value() || Q.csWait.has_value();
                    double wat = Q.rbWait ? Q.rbWait->at : (Q.csWait ? Q.csWait->at : 0);
                    if (held > 0) div.push_back(F("%d passos do host retidos (%s ha %.0f s)", static_cast<int>(held), Q.rbWait ? "rbWait" : "csWait", hasW ? (t - wat) : 0.0));
                    std::optional<long long> hn;
                    {
                        std::string b = part("B");
                        size_t c = b.find(',');
                        if (c != std::string::npos && c > 0 && all_digits(b.substr(0, c)))
                        {
                            size_t e = c + 1;
                            while (e < b.size() && ldigit(b[e])) ++e;
                            if (e > c + 1) hn = std::atoll(b.substr(c + 1, e - c - 1).c_str());
                        }
                    }
                    int rx = Q.rx;
                    if (hn && D.hnLast && held == 0 && *hn - *D.hnLast >= 2 && D.rxLast && rx == *D.rxLast)
                    {
                        div.push_back(F("o host avancou %lld passos e nenhum chegou aqui", *hn - *D.hnLast));
                        if (fl("qdivfix", 0) == 1 && t >= Q.gapAt && !Q.rbWait)
                        {
                            Q.gapAt = t + 60;
                            Q.csLoads += 1;
                            Q.cuDone = false;
                            log("NOLA qdiv: b187 qdivfix: catch-up do host pedido de novo (#N " + std::to_string(Q.csLoads) + ")");
                        }
                    }
                    D.hnLast = hn;
                    D.rxLast = rx;
                }
            }
            qcDiv(div, t);
            std::sort(div.begin(), div.end());
            std::string jd;
            if (D.myJ && D.peerJ && *D.myJ != "-" && *D.peerJ != "-" && *D.myJ != *D.peerJ) jd = F("; diario difere (aqui %s, outro %s)", D.myJ->c_str(), D.peerJ->c_str());
            const char* st = !div.empty() ? "diverged" : (fixed > 0 ? "repaired" : "in-sync");
            log(F("NOLA qdiv %ds: %s%s (host repetiu %d, cliente voltou %d no total)%s%s", static_cast<int>(fl("qdivlog", 30)), st,
                  div.empty() ? "" : (": " + join(div, ", ")).c_str(), D.nrep, D.nroll, jd.c_str(), D.peer ? "" : "; sem digest do outro lado"));
        }

        void qdTick()
        {
            if (fl("qdiv", 0) != 1) return;
            Qd& D = qdSt();
            double t = now();
            bool up = E->peer_state() == 1;
            for (int v = 1, NV = nv(); v < NV && !up; ++v) up = E->peer_state_v(v) == 1;
            if (!up) return;
            if (host()) qdHost(D, t);
            else qdClient(D, t);
            if (t - D.buildAt >= fl("qdivs", 2))
            {
                D.buildAt = t;
                qdBuild(D, t);
            }
            qdLog(D, t);
        }

        std::string qdOut()
        {
            if (fl("qdiv", 0) != 1) return "";
            if (!S.qd || now() - S.qd->outAt > 5) return "";
            return S.qd->out;
        }

        std::string qdOutV(int v)
        {
            if (v == 0 || nv() == 1) return qdOut();
            if (fl("qdiv", 0) != 1 || v < 0 || v >= 4 || v >= nv()) return "";
            if (!S.qd || now() - S.qd->outAt > 5) return "";
            return S.qd->vout[(size_t)v];
        }

        const char* QLOG = "quest_log.txt";

        void qlogLoad()
        {
            Qs& Q = S.qs;
            if (Q.logLoaded) return;
            Q.logLoaded = true;
            Q.log.clear();
            std::string text;
            if (!E->read(QLOG, text)) return;
            for (auto& ln : lines(text))
            {
                size_t tab = ln.find('\t');
                if (tab == std::string::npos || tab == 0 || tab + 1 >= ln.size()) continue;
                bool okc = true;
                for (size_t i = 0; i < tab; ++i)
                    if (!ldigit(ln[i]) && ln[i] != '.') okc = false;
                if (!okc) continue;
                double tv;
                if (!tonum(ln.substr(0, tab), tv)) continue;
                Q.log.push_back({tv, ln.substr(tab + 1), false});
            }
            log("NOLA quest: log do host carregado, " + std::to_string(Q.log.size()) + " eventos");
        }

        void qlogRewrite()
        {
            std::string s;
            for (auto& x : S.qs.log) s += F("%.1f\t%s\n", x.t, x.e.c_str());
            E->write(QLOG, s);
        }

        std::string firstLine(const std::string& text, bool& had)
        {
            had = !text.empty();
            size_t n = text.find('\n');
            std::string l = n == std::string::npos ? text : text.substr(0, n);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            return l;
        }

        std::string campId()
        {
            Qs& Q = S.qs;
            if (Q.cid) return *Q.cid;
            std::string text, v;
            bool had = false;
            if (E->read("quest_log.id", text)) v = firstLine(text, had);
            bool good = !v.empty();
            for (char c : v)
                if (!lalnum(c)) good = false;
            if (!good)
            {
                v = F("c%llx%04x", static_cast<long long>(E->unix_time()), E->rnd(0, 65535));
                E->write("quest_log.id", v + "\n");
                log("NOLA quest: id da campanha criado: " + v + " (quest_log.id)");
            }
            Q.cid = v;
            return v;
        }

        long long rbGen()
        {
            Qs& Q = S.qs;
            if (!Q.gen)
            {
                std::string text;
                double v = 0;
                bool had = false;
                if (E->read("quest_log.gen", text) && tonum(firstLine(text, had), v)) Q.gen = static_cast<long long>(v);
                else Q.gen = 0;
            }
            return *Q.gen;
        }

        void rbBumpI(const std::string& why)
        {
            if (fl("qrollback", 0) != 1 || !host()) return;
            Qs& Q = S.qs;
            Q.gen = rbGen() + 1;
            E->write("quest_log.gen", std::to_string(*Q.gen) + "\n");
            Q.cu.reset();
            Q.cuN = Q.log.size();
            Q.cuUntil = now() + fl("qrbwindow", 300);
            for (size_t v = 1; v < Q.cuv.size(); ++v)
            {
                Q.cuv[v].cu.reset();
                Q.cuv[v].n = Q.log.size();
                Q.cuv[v].until = now() + fl("qrbwindow", 300);
            }
            log(F("NOLA rollback: host voltou no tempo (%s): geracao %lld, log com %d eventos, catch-up oferecido por %d s", why.c_str(), *Q.gen,
                  static_cast<int>(Q.log.size()), static_cast<int>(fl("qrbwindow", 300))));
        }

        void campRotateI(const std::string& why)
        {
            if (!host()) return;
            Qs& Q = S.qs;
            qlogLoad();
            std::string old = campId();
            size_t n = Q.log.size();
            for (const char* p : {"quest_log.txt", "taken.txt", "art_taken.txt", "taken_mine.txt"})
            {
                if (E->exists(p))
                {
                    E->remove(std::string(p) + "." + old);
                    E->rename(p, std::string(p) + "." + old);
                }
            }
            E->remove("quest_log.id");
            Q.cid.reset();
            Q.log.clear();
            Q.logLoaded = true;
            Q.pend.reset();
            Q.cu.reset();
            for (auto& c : Q.cuv) c.cu.reset();
            std::string nw = campId();
            try { E->campaign_rotated(); } catch (...) {}
            log(F("NOLA campanha: nova campanha cooperativa (%s): %s -> %s; %d eventos e taken/art_taken antigos guardados como *.%s", why.c_str(), old.c_str(), nw.c_str(),
                  static_cast<int>(n), old.c_str()));
            rbBumpI("campanha nova");
        }

        void campFlag()
        {
            std::string want = E->fls("campaign");
            if (want.empty() || !host()) return;
            Qs& Q = S.qs;
            if (!Q.campTag)
            {
                std::string text;
                bool had = false;
                Q.campTag = E->read("quest_log.camp", text) ? firstLine(text, had) : std::string();
            }
            if (want == *Q.campTag) return;
            campRotateI("flag campaign=" + want);
            Q.campTag = want;
            E->write("quest_log.camp", want + "\n");
        }

        void qpCmd(const std::vector<std::string>& t);
        void questFinishedI(const std::string& quest);

        void questCmd(std::string_view full)
        {
            Qs& Q = S.qs;
            auto t = words(full);
            if (!t.empty() && t[0] == "s2q") t.erase(t.begin());
            if (!host() && !t.empty() && (t[0] == "G" || t[0] == "J" || t[0] == "N" || t[0] == "W" || t[0] == "X" || t[0] == "Z" || copyKind(t[0]))) return;
            try
            {
                std::optional<std::string> ms;
                if (!t.empty() && t[0] == "G") { if (t.size() >= 5) ms = t[4]; }
                else if (t.size() >= 2) ms = t[1];
                if (ms) E->mq_seen(*ms);
            }
            catch (...) {}
            if (t.size() >= 2 && t[0] == "L")
            {
                try { qcReach(t[1]); } catch (...) {}
            }
            if (!t.empty() && (t[0] == "D" || t[0] == "L"))
            {
                try { qpCmd(t); } catch (...) {}
                return;
            }
            if (!t.empty() && t[0] == "E")
            {
                try { evCmd(t); } catch (...) {}
                return;
            }
            std::string kind = t.empty() ? std::string() : t[0];
            std::string e;
            bool has = false;
            if ((kind == "J" || kind == "G") && t.size() >= 4)
            {
                e = kind + "," + t[1] + "," + t[2] + "," + t[3] + "," + (t.size() >= 5 ? t[4] : std::string("-"));
                has = true;
            }
            else if ((kind == "N" || kind == "W" || kind == "X" || kind == "Z" || copyKind(kind)) && t.size() >= 2)
            {
                e = kind + "," + t[1];
                has = true;
            }
            if (!has)
            {
                log("NOLA quest: linha s2q invalida: " + std::string(full));
                return;
            }
            Q.got += 1;
            if (fl("qsync", 0) != 1) return;
            if (host() && kind == "H" && t.size() >= 3 && t[2] == "r" && fl("qrearm", 1) != 1) return;
            if (host() && (kind == "H" || kind == "Z") && fl("qhzrate", 1) == 1)
            {
                static std::map<std::string, std::pair<double, int>> burst;
                double bt = now();
                auto& b = burst[kind + t[1]];
                if (bt - b.first > 1.0) b = {bt, 0};
                b.second += 1;
                if (b.second > 8)
                {
                    if (b.second == 9) log("NOLA quest: " + kind + " " + t[1] + " mais de 8 vezes em 1 s: repeticoes descartadas");
                    return;
                }
            }
            const bool zk = kind == "Z";
            if (zk && (fl("qhact", 1) != 1 || fl("qlisten", 1) != 1)) return;
            if (host() && copyKind(kind) && fl("qhostcopy", 1) == 1)
            {
                const bool on = kind == "R" ? fl("qcopyr", 1) == 1 : kind == "V" ? fl("qcopyv", 1) == 1 : fl("qlisten", 1) == 1;
                if (!on) return;
            }
            double tnow = now();
            if (host() && fl("newgame", 1) == 1 && kind == "G" && t.size() >= 5 && t[4] == "EQ101_P_SetGlobalVariable_CA_Disable_EQ101_False_NewStart")
            {
                double gt = E->game_secs().value_or(0);
                if (gt < fl("newgamesecs", 600) && tnow - (Q.ngAt ? *Q.ngAt : -1e9) > 120)
                {
                    Q.ngAt = tnow;
                    campRotateI(F("jogo novo (relogio %.0f s)", gt));
                }
            }
            if (host() && fl("qbootdedupe", 1) == 1 && kind == "J")
            {
                Id addr = E->pawn_pc();
                if (addr && (!Q.dedAddr || addr != *Q.dedAddr))
                {
                    Q.dedAddr = addr;
                    Q.dedUntil = tnow + fl("qbootwin", 30);
                }
                if (!addr || tnow < (Q.dedUntil ? *Q.dedUntil : 0))
                {
                    qlogLoad();
                    bool dup = false;
                    for (auto& x : Q.log)
                        if (x.e == e) { dup = true; break; }
                    if (!dup && Q.pend)
                        for (auto& x : *Q.pend)
                            if (x == e) { dup = true; break; }
                    if (dup)
                    {
                        Q.dedN += 1;
                        log("NOLA quest: host " + e + " repetido logo depois de carregar o mundo: ignorado (" + std::to_string(Q.dedN) + ")");
                        return;
                    }
                }
            }
            if (!zk) Q.seq += 1;
            std::string str = zk ? e : std::to_string(Q.seq) + "," + e;
            if (!zk) Q.list.push_back({tnow, str});
            if (host())
            {
                qlogLoad();
                double gt = E->game_secs().value_or(0);
                if (gt <= 0 && fl("qbootdedupe", 1) == 1)
                {
                    if (!Q.pend) Q.pend.emplace();
                    Q.pend->push_back(e);
                }
                else
                {
                    Q.log.push_back({gt, e, true});
                    E->append(QLOG, F("%.1f\t%s\n", gt, e.c_str()));
                }
                if (kind == "J" && t[2] == "-" && t[3] == "Finish")
                {
                    try { questFinishedI(t[1]); } catch (...) {}
                }
            }
            log("NOLA quest: host " + str);
        }

        void qlogFlush()
        {
            Qs& Q = S.qs;
            if (!host() || !Q.pend || Q.pend->empty()) return;
            auto gs = E->game_secs();
            if (!gs || *gs <= 0) return;
            double gt = *gs;
            qlogLoad();
            std::string s;
            for (auto& x : *Q.pend)
            {
                Q.log.push_back({gt, x, true});
                s += F("%.1f\t%s\n", gt, x.c_str());
            }
            E->append(QLOG, s);
            log(F("NOLA quest: %d eventos de antes do relogio gravados no log com %.1f s", static_cast<int>(Q.pend->size()), gt));
            Q.pend.reset();
        }

        void questRollback()
        {
            if (!host() || fl("qsync", 0) != 1) return;
            Qs& Q = S.qs;
            qlogLoad();
            auto gs = E->game_secs();
            if (!gs || *gs <= 0 || (Q.rbLast && *gs <= *Q.rbLast))
            {
                Q.rbLast = gs;
                Q.rbHits = 0;
                return;
            }
            double gt = *gs;
            Q.rbLast = gt;
            if (!g_qlogBase) g_qlogBase = gt;
            double base = *g_qlogBase;
            double newest = 0;
            for (auto& x : Q.log)
                if (!x.live && x.t > newest) newest = x.t;
            if (Q.log.empty() || newest <= base + 60)
            {
                Q.rbHits = 0;
                SQ().qlOk = true;
                return;
            }
            Q.rbHits += 1;
            if (Q.rbHits < 3) return;
            Q.rbHits = 0;
            std::vector<LogE> keep;
            for (auto& x : Q.log)
                if (x.live || x.t <= base) keep.push_back(x);
            log(F("NOLA quest: save mais antigo carregado (relogio %.0f s < %.0f s): %d eventos do log descartados", gt, newest, static_cast<int>(Q.log.size() - keep.size())));
            Q.log = keep;
            qlogRewrite();
            Q.cu.reset();
            for (auto& c : Q.cuv) c.cu.reset();
            rbBumpI("relogio do jogo voltou");
        }

        std::string questOutJ()
        {
            Qs& Q = S.qs;
            double t = now();
            std::vector<ListE> keep;
            std::vector<std::string> parts;
            for (auto& e : Q.list)
                if (t - e.t < 2)
                {
                    keep.push_back(e);
                    parts.push_back(e.s);
                }
            Q.list = keep;
            if (!parts.empty())
            {
                Q.sent += 1;
                return "#J:" + join(parts, "|");
            }
            return "";
        }

        bool qskip(const std::optional<std::string>& sid)
        {
            if (!S.qskip)
            {
                S.qskip.emplace();
                int n = 0;
                std::string text;
                if (E->data("s2c_skip.txt", text))
                {
                    for (auto& ln : lines(text))
                    {
                        auto w = words(ln);
                        if (!w.empty())
                        {
                            S.qskip->insert(w[0]);
                            ++n;
                        }
                    }
                }
                log("NOLA quest: " + std::to_string(n) + " nos em s2c_skip.txt (ficam com o grafo local do cliente)");
            }
            return sid && S.qskip->count(*sid) > 0;
        }

        uint32_t haFnv(const std::string& s)
        {
            uint32_t h = 2166136261u;
            for (unsigned char c : s)
            {
                h ^= c;
                h *= 16777619u;
            }
            return h;
        }

        void haFold()
        {
            Ha& H = S.ha;
            Qs& Q = S.qs;
            qlogLoad();
            bool ok = H.folded <= Q.log.size();
            if (ok && H.folded > 0)
            {
                const LogE& l = Q.log[H.folded - 1];
                ok = l.t == H.sigT && l.e == H.sigE;
            }
            if (!ok)
            {
                H.st.clear();
                H.folded = 0;
                H.ver += 1;
            }
            std::map<std::string, EvRow>* tab = nullptr;
            for (; H.folded < Q.log.size(); ++H.folded)
            {
                const std::string& e = Q.log[H.folded].e;
                if (e.size() > 2 && e[1] == ',' && (e[0] == 'H' || e[0] == 'Z'))
                {
                    HaNode& n = H.st[e.substr(2)];
                    if (e[0] == 'H')
                    {
                        n.on = true;
                        n.n += 1;
                    }
                    else n.on = false;
                    H.ver += 1;
                }
                else if (e.size() > 4 && e[0] == 'J' && e[1] == ',' && e.find(",-,") != std::string::npos)
                {
                    auto f = gm(e, ',');
                    if (f.size() >= 4 && f[2] == "-" && (f[3] == "Finish" || f[3] == "Fail" || f[3] == "Cancel"))
                    {
                        if (!tab) tab = evTab();
                        if (tab)
                            for (auto& [sid, nd] : H.st)
                            {
                                if (!nd.on) continue;
                                auto it = tab->find(sid);
                                if (it != tab->end() && it->second.q == f[1])
                                {
                                    nd.on = false;
                                    H.ver += 1;
                                }
                            }
                    }
                }
            }
            if (H.folded > 0)
            {
                H.sigT = Q.log[H.folded - 1].t;
                H.sigE = Q.log[H.folded - 1].e;
            }
        }

        std::string haOut()
        {
            if (!host() || fl("qhact", 1) != 1 || fl("qlisten", 1) != 1 || fl("qsync", 0) != 1 || !E->ql_settled()) return "";
            Ha& H = S.ha;
            haFold();
            if (H.outVer != H.ver)
            {
                H.outVer = H.ver;
                std::vector<std::string> v;
                const size_t cap = static_cast<size_t>(std::max(1.0, fl("qhmax", 100)));
                size_t on = 0;
                for (auto& [sid, nd] : H.st)
                {
                    if (!nd.on) continue;
                    on += 1;
                    if (v.size() < cap) v.push_back(F("%08x.%d", haFnv(sid), nd.n));
                }
                H.out = v.empty() ? std::string("-") : join(v, "|");
                H.cut = on > v.size() ? static_cast<int>(on - v.size()) : 0;
                log(F("NOLA qh: conjunto ativo de ouvintes H do host: %d, enviados %d%s", static_cast<int>(on), static_cast<int>(v.size()),
                      H.cut > 0 ? F(" (%d acima de qhmax=%d ficam de fora)", H.cut, static_cast<int>(cap)).c_str() : ""));
            }
            return H.out;
        }

        bool haKnown(const std::string& sid)
        {
            haFold();
            return S.ha.st.count(sid) > 0;
        }

        int haCount(const std::string& sid)
        {
            haFold();
            auto it = S.ha.st.find(sid);
            return it == S.ha.st.end() ? -1 : it->second.n;
        }

        bool haStale(const std::string& sid, int n)
        {
            haFold();
            auto it = S.ha.st.find(sid);
            if (it == S.ha.st.end()) return false;
            if (!it->second.on) return true;
            return n >= 0 && n != it->second.n;
        }

        void haZ(const std::string& sid) { questCmd("s2q Z " + sid); }

        int haRelayN(const std::string& sid)
        {
            if (fl("qhact", 1) != 1 || fl("qhstale", 1) != 1) return -1;
            auto it = S.ha.started.find(sid);
            return it == S.ha.started.end() ? -1 : it->second;
        }

        bool haEchoEnd(const std::string& sid, double t)
        {
            if (fl("qhact", 1) != 1) return false;
            auto it = S.ha.ended.find(sid);
            if (it == S.ha.ended.end()) return false;
            bool hit = t - it->second <= fl("qcatchgap", 5);
            S.ha.ended.erase(it);
            return hit;
        }

        void haTick()
        {
            if (host() || fl("qhact", 1) != 1 || fl("qlisten", 1) != 1 || fl("qsync", 0) != 1) return;
            Ha& H = S.ha;
            Id pw = E->pawn_pc();
            if (!pw) return;
            if (pw != H.pawnAt)
            {
                H.pawnAt = pw;
                H.started.clear();
                H.ended.clear();
                H.lastRaw.clear();
                H.pend = true;
                log("NOLA qh: mundo novo no cliente: os ouvintes H recomecam do conjunto ativo do host");
            }
            if (E->join_pending() || !E->aa_present()) return;
            std::string raw;
            if (!E->part("QH", raw)) return;
            if (raw == H.lastRaw && !H.pend) return;
            std::map<std::string, EvRow>* tab = evTab();
            if (!tab) return;
            if (H.byHash.empty())
                for (auto& kv : *tab)
                {
                    auto ins = H.byHash.emplace(haFnv(kv.first), kv.first);
                    if (!ins.second) ins.first->second.clear();
                }
            std::map<std::string, int> want;
            if (raw != "-")
                for (auto& ent : gm(raw, '|'))
                {
                    if (ent.size() < 10 || ent[8] != '.') continue;
                    char* end = nullptr;
                    unsigned long hv = std::strtoul(ent.substr(0, 8).c_str(), &end, 16);
                    if (!end || *end != '\0') continue;
                    auto it = H.byHash.find(static_cast<uint32_t>(hv));
                    if (it == H.byHash.end() || it->second.empty()) continue;
                    want[it->second] = std::atoi(ent.c_str() + 9);
                }
            int budget = std::max(1, static_cast<int>(fl("qhbudget", 20)));
            double t = now();
            for (auto it = H.ended.begin(); it != H.ended.end();)
                it = t - it->second > 60 ? H.ended.erase(it) : std::next(it);
            for (auto it = H.started.begin(); it != H.started.end() && budget > 0;)
            {
                auto w = want.find(it->first);
                if (w != want.end() && w->second == it->second)
                {
                    ++it;
                    continue;
                }
                std::string e;
                H.ended[it->first] = t;
                if (w != want.end() && fl("qhrestart", 1) == 1)
                {
                    H.nre += 1;
                    log(F("NOLA qh: reiniciado %s n=%d", it->first.c_str(), w->second));
                }
                E->aa("XEndQuestNodeBySID", it->first + "_S2C", e);
                H.nend += 1;
                budget -= 1;
                it = H.started.erase(it);
            }
            for (auto& [sid, n] : want)
            {
                if (budget <= 0) break;
                if (H.started.count(sid)) continue;
                std::string e;
                int r = E->aa("XStartQuestNodeBySID", sid + "_S2C", e);
                H.started[sid] = n;
                budget -= 1;
                if (r == call_ok)
                {
                    H.nstart += 1;
                    qcSt().s2c[sid] = t;
                }
                else
                {
                    H.nerr += 1;
                    if (H.nerr <= 5) log("NOLA qh: XStartQuestNodeBySID " + sid + "_S2C falhou: " + e);
                }
            }
            bool more = false;
            for (auto& [sid, n] : want)
                if (!H.started.count(sid)) more = true;
            for (auto& kv : H.started)
                if (!want.count(kv.first)) more = true;
            if (!more || H.pend)
                log(F("NOLA qh: ouvintes H do cliente: %d no conjunto do host, %d iniciados, %d encerrados, %d reiniciados%s", static_cast<int>(want.size()), H.nstart, H.nend, H.nre, more ? " (lote, continua)" : ""));
            H.pend = more;
            H.lastRaw = raw;
        }

        bool qiSkip(const std::optional<std::string>& sid)
        {
            if (sid && E->qi_npcadd(*sid)) return false;
            if ((fl("qitem", 0) != 1 && fl("qimirror", 0) != 1) || host() || !sid) return false;
            QiTab* T = qiTab();
            if (!T) return false;
            auto it = T->skip.find(*sid);
            return it != T->skip.end() && it->second == 1;
        }

        bool qpSkipI(const std::optional<std::string>& sid);

        Applied questApplyI(const std::string& kind, const std::optional<std::string>& a, const std::optional<std::string>& b,
                            const std::optional<std::string>& c, const std::optional<std::string>& sid, bool live)
        {
            Applied R;
            auto ret = [&](int r, std::optional<std::string> why) {
                R.r = r;
                R.why = std::move(why);
                return R;
            };
            auto thrown = [&](const std::string& e) {
                R.ok = false;
                R.r = -1;
                R.why.reset();
                R.err = e;
                return R;
            };
            if (kind == "Z") return ret(-1, std::string("fim de ouvinte: so o log do host usa (qhact)"));
            if (kind == "H" && fl("qhact", 1) == 1 && fl("qlisten", 1) == 1) return ret(-1, std::string("ouvinte H iniciado pelo conjunto ativo do host (qhact)"));
            if (qiSkip(sid)) return ret(-1, std::string("item de missao fica so no host (qitem)"));
            if (fl("natnpc", 0) == 1 && sid)
            {
                auto g = E->qr_npc(*sid);
                if (g)
                {
                    bool hit = false;
                    try { hit = E->nat_guid_listed(*g); } catch (...) { hit = false; }
                    if (hit)
                    {
                        S.qrN += 1;
                        return ret(-1, "no de NPC que o natnpc dirige: pulado (b164, total " + std::to_string(S.qrN) + ")");
                    }
                }
            }
            if (qpSkipI(sid)) return ret(-1, std::string("ja rodou no grafo do cliente (qpick): pulado"));
            if (qskip(sid) && !(live && fl("qskipcatch", 1) == 1)) return ret(-1, std::string("no vivo no cliente (s2c_skip)"));
            bool cp = copyKind(kind);
            if (cp)
            {
                const char* f = kind == "R" ? "qcopyr" : kind == "V" ? "qcopyv" : "qlisten";
                if (fl(f, 1) != 1) return ret(-1, std::string("copia de efeito desligada (") + f + "=0)");
                if (kind != "R" && g_cuApply)
                {
                    S.cuVN += 1;
                    if (S.cuVN <= 3 || S.cuVN % 25 == 0)
                        log(F("NOLA quest: efeito %s de momento %s nao repetido no catch-up (total %d)", kind.c_str(), sid ? sid->c_str() : "-", S.cuVN));
                    if (kind == "V" && sid && sid->find("_ItemRemove_") != std::string::npos)
                    {
                        S.cuIRN += 1;
                        if (S.cuIRN <= 3 || S.cuIRN % 25 == 0)
                            log(F("NOLA quest: ItemRemove %s nao repetido no catch-up (total %d)", sid->c_str(), S.cuIRN));
                    }
                    return ret(-1, std::string("efeito de momento: fora do catch-up (o save do host ja traz o estado)"));
                }
            }
            if (!cp && fl("qnodlg", 1) == 1 && sid && sid->find("Dialog") != std::string::npos) return ret(-1, std::string("dialogo fica so no host (qnodlg)"));
            if (kind == "W" && fl("qworld", 1) != 1) return ret(-1, std::string("evento de mundo ignorado (qworld=0)"));
            if (kind == "X" && fl("qextra", 0) != 1) return ret(-1, std::string("evento extra ignorado (qextra=0)"));
            if (fl("qnode", 0) == 1 && sid && *sid != "-")
            {
                if (!E->aa_present()) return ret(0, std::string("CustomConsoleManagerAA ausente"));
                if (on("cjoin", 0) && E->mq_sid(*sid))
                {
                    try { E->cj_before(*sid); } catch (...) {}
                }
                std::string e;
                int r = E->aa("XStartQuestNodeBySID", *sid + "_S2C", e);
                if (r != call_ok) return thrown(r == mgr_missing ? std::string("CustomConsoleManagerAA ausente") : e);
                qcSt().s2c[*sid] = now();
                return ret(1, "no " + *sid + "_S2C");
            }
            if (kind == "J")
            {
                std::string e;
                const std::string A = a ? *a : std::string();
                const std::string* B = (b && *b != "-") ? &*b : nullptr;
                if (c && *c == "Start")
                {
                    int r = E->dk(B ? "XDKStartQuestStage" : "XDKStartQuest", A, B, -1, e);
                    if (r == mgr_missing) return ret(0, std::string("CustomConsoleManagerDK ausente"));
                    if (r != call_ok) return thrown(e);
                    return ret(1, std::nullopt);
                }
                if (c && (*c == "Finish" || *c == "Fail" || *c == "Cancel"))
                {
                    int r = E->dk(B ? "XDKFinishQuestStage" : "XDKFinishQuest", A, B, *c == "Finish" ? 1 : 0, e);
                    if (r == mgr_missing) return ret(0, std::string("CustomConsoleManagerDK ausente"));
                    if (r != call_ok) return thrown(e);
                    return ret(1, std::nullopt);
                }
                return ret(0, "acao desconhecida " + (c ? *c : std::string("nil")));
            }
            if (kind == "G" && b && *b == "Set" && c && (*c == "true" || *c == "false" || *c == "True" || *c == "False"))
            {
                std::string e;
                int r = E->rk_bool(a ? *a : std::string(), *c == "true" || *c == "True", e);
                if (r == mgr_missing) return ret(0, std::string("CustomConsoleManagerRK ausente"));
                if (r != call_ok) return thrown(e);
                return ret(1, std::nullopt);
            }
            return ret(-1, std::string("nao aplicavel sem o pak do cliente (qnode=1)"));
        }

        Applied questApplyEv(const std::string& e, bool live)
        {
            auto f = gm(e, ',');
            auto at = [&](size_t i) -> std::optional<std::string> {
                if (i < f.size()) return f[i];
                return std::nullopt;
            };
            std::string k = f.empty() ? std::string() : f[0];
            try
            {
                if (k == "N" || k == "W" || k == "X" || copyKind(k)) return questApplyI(k, std::nullopt, std::nullopt, std::nullopt, at(1), live);
                return questApplyI(k, at(1), at(2), at(3), at(4), live);
            }
            catch (const std::exception& x)
            {
                Applied R;
                R.ok = false;
                R.err = x.what();
                return R;
            }
        }

        std::string cuServe(std::optional<Cu>& cu, size_t cuN, int v);

        std::string catchupOut()
        {
            if (!host() || fl("qsync", 0) != 1 || !E->look_ok()) return "";
            Qs& Q = S.qs;
            double t = now();
            qlogLoad();
            if (!E->ql_settled()) return "";
            int ps = E->peer_state();
            int sess = E->peer_sessions();
            if ((!Q.cuSession || sess != *Q.cuSession) && ps == 1 && !E->peer_loopback())
            {
                Q.cuSession = sess;
                Q.cuUntil = t + fl("qcatchup", 60);
                Q.cu.reset();
                Q.cuN = Q.log.size();
                std::string nl;
                if (has_part("N", nl)) Q.cuLoads = nl;
                else Q.cuLoads.reset();
            }
            std::string nl;
            bool hasN = has_part("N", nl);
            if (hasN && Q.cuLoads && nl != *Q.cuLoads && ps == 1)
            {
                Q.cuLoads = nl;
                Q.cuUntil = t + fl("qcatchup", 60);
                Q.cu.reset();
                Q.cuN = Q.log.size();
                log("NOLA quest: o cliente carregou um save (#" + nl + "); catch-up oferecido de novo");
            }
            else if (hasN && !Q.cuLoads) Q.cuLoads = nl;
            if (!Q.cuUntil || t > *Q.cuUntil || (Q.cuN ? *Q.cuN : 0) == 0 || Q.log.empty()) return "";
            return cuServe(Q.cu, *Q.cuN, -1);
        }

        bool cuDrop(const std::string& e)
        {
            if (e.size() < 2 || e[1] != ',' || fl("qcuflags", 1) != 1) return false;
            if (e[0] == 'R') return fl("qcopyr", 1) != 1;
            if (e[0] == 'V') return fl("qcopyv", 1) != 1;
            if (e[0] == 'H') return fl("qlisten", 1) != 1;
            return false;
        }

        size_t cuLogN()
        {
            Qs& Q = S.qs;
            if (fl("qcuflags", 1) != 1 || (fl("qcopyr", 1) == 1 && fl("qcopyv", 1) == 1 && fl("qlisten", 1) == 1)) return Q.log.size();
            size_t n = 0;
            for (auto& x : Q.log)
                if (!cuDrop(x.e)) ++n;
            return n;
        }

        std::string cuServe(std::optional<Cu>& cu, size_t cuN, int v)
        {
            Qs& Q = S.qs;
            if (!cu)
            {
                std::vector<std::string> chunks, cur;
                size_t size = 0, kept = 0;
                for (size_t i = 1; i <= Q.log.size(); ++i)
                {
                    if (i > cuN) break;
                    const std::string& e = Q.log[i - 1].e;
                    if (cuDrop(e)) continue;
                    ++kept;
                    if (size + e.size() + 1 > 1200 && !cur.empty())
                    {
                        chunks.push_back(join(cur, ";"));
                        cur.clear();
                        size = 0;
                    }
                    cur.push_back(e);
                    size += e.size() + 1;
                }
                if (!cur.empty()) chunks.push_back(join(cur, ";"));
                Cu C;
                C.n = kept;
                C.chunks = chunks;
                for (size_t i = 0; i < chunks.size(); ++i) C.hashes.push_back(E->look_put("QL" + std::to_string(i + 1) + "~" + chunks[i]));
                cu = C;
                const size_t cut = std::min(cuN, Q.log.size()) - kept;
                log(F("NOLA quest: catch-up oferecido, %d eventos em %d pedacos%s", static_cast<int>(C.n), static_cast<int>(chunks.size()), v < 0 ? "" : vwho(v).c_str()) +
                    (cut > 0 ? F(" (copias R/V/H omitidas pelas flags do host: %d)", static_cast<int>(cut)) : std::string()));
            }
            Cu& C = *cu;
            size_t nc = C.chunks.size();
            if (nc > 0)
            {
                size_t reps = std::min(nc, std::max<size_t>(2, (nc + 14) / 15));
                for (size_t k = 0; k < reps; ++k)
                {
                    C.rr = C.rr % nc + 1;
                    E->look_put("QL" + std::to_string(C.rr) + "~" + C.chunks[C.rr - 1]);
                }
            }
            const size_t cutN = std::min(cuN, Q.log.size()) - C.n;
            return "#S:" + std::to_string(C.n) + "," + join(C.hashes, ".") + (cutN > 0 ? "," + std::to_string(cutN) : std::string());
        }

        std::string catchupOutV(int v)
        {
            if (v == 0) return S.qs.cu0 = catchupOut();
            if (nv() == 1) return v > 0 && v < 4 ? S.qs.cu0 : std::string();
            if (!host() || fl("qsync", 0) != 1 || !E->look_ok() || v < 0 || v >= 4 || v >= nv()) return "";
            Qs& Q = S.qs;
            CuV& C = Q.cuv[(size_t)v];
            double t = now();
            qlogLoad();
            if (!E->ql_settled()) return "";
            int ps = E->peer_state_v(v);
            int sess = E->peer_sessions_v(v);
            if ((!C.session || sess != *C.session) && ps == 1 && !E->peer_loopback_v(v))
            {
                C.session = sess;
                C.until = t + fl("qcatchup", 60);
                C.cu.reset();
                C.n = Q.log.size();
                std::string nl;
                if (has_partv(v, "N", nl)) C.loads = nl;
                else C.loads.reset();
                log(F("NOLA quest: sessao nova do cliente%s: catch-up de %d eventos aberto por %.0f s", vwho(v).c_str(), static_cast<int>(Q.log.size()), fl("qcatchup", 60)));
            }
            std::string nl;
            bool hasN = has_partv(v, "N", nl);
            if (hasN && C.loads && nl != *C.loads && ps == 1)
            {
                C.loads = nl;
                C.until = t + fl("qcatchup", 60);
                C.cu.reset();
                C.n = Q.log.size();
                log("NOLA quest: o cliente" + vwho(v) + " carregou um save (#" + nl + "); catch-up oferecido de novo");
            }
            else if (hasN && !C.loads) C.loads = nl;
            if (!C.until || t > *C.until || (C.n ? *C.n : 0) == 0 || Q.log.empty()) return "";
            return cuServe(C.cu, *C.n, v);
        }

        std::string rbOut()
        {
            if (fl("qrollback", 0) != 1 || !host() || fl("qsync", 0) != 1) return "";
            qlogLoad();
            if (!E->ql_settled()) return "";
            return F("#B:%lld,%d,%s", rbGen(), static_cast<int>(cuLogN()), campId().c_str()) + E->n7_b();
        }

        bool rbReadyI()
        {
            auto& Q = SQ();
            if (!Q.rbWait) return true;
            auto& W = *Q.rbWait;
            double t = now();
            Id pw = E->pawn_pc();
            if (pw && pw != W.addr && !W.readyAt) W.readyAt = t + fl("qrbdelay", 12);
            return W.readyAt.has_value() && t >= *W.readyAt;
        }

        void rbDoneI(const std::string& why)
        {
            auto& Q = SQ();
            if (!Q.rbWait) return;
            size_t n = Q.rbQueue ? Q.rbQueue->size() : 0;
            int okN = 0;
            auto& ap = E->applied();
            std::vector<std::string> queue = Q.rbQueue ? *Q.rbQueue : std::vector<std::string>();
            for (auto& rest : queue)
            {
                Applied r = questApplyEv(rest, false);
                if (r.ok && r.r == 1) ++okN;
                ap[rest] += 1;
                Q.csDirty = true;
                Q.csAt = now();
            }
            log(F("NOLA rollback: cliente pronto (%s) depois de %.0f s; %d eventos ao vivo guardados, %d aplicados", why.c_str(), now() - Q.rbWait->at, static_cast<int>(n), okN));
            Q.rbWait.reset();
            Q.rbQueue.reset();
        }

        void rbTick()
        {
            if (fl("qsync", 0) != 1) return;
            Qs& Q = S.qs;
            double t = now();
            if (host())
            {
                try { campFlag(); } catch (...) {}
                if (fl("qrollback", 0) != 1) return;
                Id addr = E->pawn_pc();
                if (!addr) return;
                if (!Q.rbAddr) Q.rbAddr = addr;
                else if (addr != *Q.rbAddr)
                {
                    Q.rbAddr = addr;
                    Q.rbCheckAt = t + 12;
                }
                SQ().rbAddrKnown = true;
                SQ().rbCheckPending = Q.rbCheckAt.has_value();
                if (Q.rbCheckAt && t >= *Q.rbCheckAt)
                {
                    Q.rbCheckAt.reset();
                    SQ().rbCheckPending = false;
                    qlogLoad();
                    auto gs = E->game_secs();
                    double newest = 0;
                    for (auto& x : Q.log)
                        if (x.t > newest) newest = x.t;
                    if (gs && *gs > 0 && newest > *gs + 1)
                    {
                        double gt = *gs;
                        std::vector<LogE> keep;
                        for (auto& x : Q.log)
                            if (x.t <= gt) keep.push_back(x);
                        log(F("NOLA quest: pawn novo do host e relogio %.0f s atras do log (%.0f s): %d eventos descartados", gt, newest, static_cast<int>(Q.log.size() - keep.size())));
                        Q.log = keep;
                        qlogRewrite();
                        rbBumpI("pawn novo do host");
                    }
                }
                return;
            }
            auto& M = SQ();
            if (fl("qrollback", 0) != 1)
            {
                if (M.rbWait) rbDoneI("qrollback desligado");
                return;
            }
            std::string b = part("B");
            std::optional<long long> g, n;
            {
                size_t c = b.find(',');
                if (c != std::string::npos && c > 0 && all_digits(b.substr(0, c)))
                {
                    size_t e = c + 1;
                    while (e < b.size() && ldigit(b[e])) ++e;
                    if (e > c + 1)
                    {
                        g = std::atoll(b.substr(0, c).c_str());
                        n = std::atoll(b.substr(c + 1, e - c - 1).c_str());
                    }
                }
            }
            if (g)
            {
                if (!Q.rbSeen) Q.rbSeen = *g;
                else if (*g != *Q.rbSeen)
                {
                    Q.rbSeen = *g;
                    if (fl("qrbmode", 1) != 1 || fl("csave", 0) == 1)
                    {
                        if (fl("csave", 0) == 1)
                        {
                            M.csRb = true;
                            M.cuDone = false;
                        }
                        log(F("NOLA rollback: o host voltou no tempo (geracao %lld, %lld eventos); qrbmode=0: o cliente NAO recarrega e fica com o diario adiantado", *g, *n));
                    }
                    else if (!M.rbWait)
                    {
                        Id pw = E->pawn_pc();
                        try { E->cp_flush("antes de recarregar"); } catch (...) {}
                        M.cuDone = false;
                        M.seen.clear();
                        s2m_save::RbWait W;
                        W.at = t;
                        W.addr = pw;
                        W.n = static_cast<int>(*n);
                        M.rbWait = W;
                        M.rbQueue.emplace();
                        std::string e;
                        bool ok = E->load_last_save(e);
                        log(F("NOLA rollback: o host voltou no tempo (geracao %lld, %lld eventos) -> XLoadLastSave ok=%s %s", *g, *n, tf(ok), ok ? "" : e.c_str()));
                        if (!ok) rbDoneI("XLoadLastSave falhou");
                    }
                }
            }
            {
                std::optional<std::string> bcid;
                auto p = split_keep(b, ',');
                if (p.size() >= 3 && all_digits(p[0]) && all_digits(p[1]))
                {
                    std::string w;
                    for (char ch : p[2])
                    {
                        if (!lalnum(ch)) break;
                        w += ch;
                    }
                    if (!w.empty()) bcid = w;
                }
                if (bcid && fl("csave", 0) == 1 && M.apCid && *bcid != *M.apCid)
                {
                    auto& ap = E->applied();
                    int had = static_cast<int>(ap.size());
                    ap.clear();
                    log(F("NOLA csave: campanha do host mudou (%s -> %s): registro descartado (%d eventos), adiantados tambem; o proximo catch-up e aplicado inteiro", M.apCid->c_str(), bcid->c_str(), had));
                    M.apCid = *bcid;
                    M.csAhead.reset();
                    M.csRb = false;
                    M.cuDone = false;
                    M.csDirty = true;
                    M.csAt = now();
                }
            }
            if (M.rbWait)
            {
                auto& W = *M.rbWait;
                if (n) W.n = static_cast<int>(*n);
                if (W.n == 0 && rbReadyI()) rbDoneI("log do host vazio");
                else if (M.rbWait && fl("qnohold", 0) != 1 && W.addr && t - W.at > fl("qrbnl", 30) && E->pawn_pc() == W.addr && E->pawn_pc() != 0)
                    rbDoneI("nenhum save carregado em " + num(fl("qrbnl", 30)) + " s (mesmo pawn)");
                else if (M.rbWait && t - W.at > fl("qrbmax", 240)) rbDoneI("tempo esgotado");
            }
        }

        Qp& qpSt()
        {
            if (!S.qp) S.qp.emplace();
            return *S.qp;
        }

        QpTab* qpTab()
        {
            if (S.qpLoaded != 0) return S.qpLoaded > 0 ? &S.qpt : nullptr;
            S.qpLoaded = -1;
            std::string text, err;
            if (!E->data("s2u_qpick.tsv", text))
            {
                log("NOLA qpick: falta s2u_qpick.lua na ponte");
                return nullptr;
            }
            std::vector<D1Row> rows;
            if (!d1_parse(text, rows, err))
            {
                log("NOLA qpick: s2u_qpick.lua erro " + err);
                return nullptr;
            }
            QpTab T;
            bool hasPin = false;
            for (auto& r : rows)
            {
                if (r.key.size() >= 3 && r.key[0] == "pin")
                {
                    T.pin[r.key[1]][std::atoll(r.key[2].c_str())] = r.v;
                    hasPin = true;
                }
                else if (r.key.size() >= 2 && r.key[0] == "stats" && r.key[1] == "dialogs") T.dialogs = r.v;
                else if (r.key.size() >= 2 && r.key[0] == "stats" && r.key[1] == "pins") T.pins = r.v;
            }
            if (!hasPin)
            {
                log("NOLA qpick: s2u_qpick.lua erro sem pin");
                return nullptr;
            }
            S.qpt = std::move(T);
            S.qpLoaded = 1;
            log(F("NOLA qpick: tabela carregada (%s dialogos, %s pinos)", S.qpt.dialogs.c_str(), S.qpt.pins.c_str()));
            return &S.qpt;
        }

        void qpCmd(const std::vector<std::string>& t)
        {
            if (fl("qpick", 0) != 1) return;
            Qp& P = qpSt();
            double tn = now();
            if (t[0] == "L" && t.size() >= 2)
            {
                if (host()) return;
                auto& e = P.loc[t[1]];
                e.n += 1;
                e.at = tn;
                log(F("NOLA qpick: no %s rodou no grafo do cliente (x%d): o relato do host desse no sera pulado", t[1].c_str(), e.n));
                return;
            }
            double kd = 0;
            if (t[0] != "D" || t.size() < 3 || !tonum(t[2], kd)) return;
            std::string ks = num(kd);
            std::string key = t[1] + "#" + ks;
            if (host())
            {
                auto& o = P.own[key];
                o.n += 1;
                o.at = tn;
                P.ownS[t[1]] = tn;
                log(F("NOLA qpick: pino %s #%s rodou no grafo do host", t[1].c_str(), ks.c_str()));
                return;
            }
            if (qbOn())
            {
                log(F("NOLA qpick: pino %s #%s rodou no cliente: nao enviado (qbranch: so o host escolhe e o cliente segue o host)", t[1].c_str(), ks.c_str()));
                return;
            }
            if (!(E->lead_dlg() || tn <= P.leadUntil))
            {
                P.nolead += 1;
                log(F("NOLA qpick: pino %s #%s rodou no cliente sem o cliente liderar o dialogo: nao enviado (o host roda o seu)", t[1].c_str(), ks.c_str()));
                return;
            }
            P.seq += 1;
            P.nsent += 1;
            P.out.push_back({std::to_string(P.seq) + "," + t[1] + "," + ks, tn});
            log(F("NOLA qpick: pino %s #%s escolhido aqui (o cliente lidera): enviado ao host (seq %lld)", t[1].c_str(), ks.c_str(), P.seq));
        }

        bool qpSkipI(const std::optional<std::string>& sid)
        {
            if (fl("qpick", 0) != 1 || host() || !sid) return false;
            if (!S.qp) return false;
            Qp& P = *S.qp;
            auto it = P.loc.find(*sid);
            if (it == P.loc.end() || it->second.n <= 0) return false;
            if (now() - it->second.at > fl("qplkeep", 900))
            {
                P.loc.erase(it);
                return false;
            }
            it->second.n -= 1;
            P.skip += 1;
            return true;
        }

        std::string qpOut()
        {
            if (fl("qpick", 0) != 1 || host() || !S.qp) return "";
            Qp& P = *S.qp;
            double t = now();
            std::vector<QpOut> keep;
            std::vector<std::string> r;
            for (auto& o : P.out)
                if (t - o.at < 3)
                {
                    keep.push_back(o);
                    r.push_back(o.e);
                }
            P.out = keep;
            return r.empty() ? std::string() : "#QP:" + join(r, "|");
        }

        bool qpRun(Qp& P, const QpQ& it, double t)
        {
            QpTab* T = qpTab();
            const std::string* row = nullptr;
            if (T)
            {
                auto a = T->pin.find(it.sid);
                if (a != T->pin.end())
                {
                    auto b = a->second.find(it.k);
                    if (b != a->second.end()) row = &b->second;
                }
            }
            if (!row)
            {
                log(F("NOLA qpick: origem c %s pino %lld seq=%s fora da tabela: nada feito", it.sid.c_str(), it.k, it.seq.c_str()));
                return true;
            }
            if (!E->aa_present())
            {
                if (t - it.at < fl("qpmax", 120)) return false;
                log("NOLA qpick: CustomConsoleManagerAA ausente: pino " + it.key + " perdido");
                return true;
            }
            int ns = 0, nx = 0, bad = 0;
            std::vector<std::string> names;
            const std::string& s = *row;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] != '!') continue;
                size_t j = i + 1;
                while (j < s.size() && !lspace(s[j])) ++j;
                if (j > i + 1)
                {
                    std::string e;
                    if (E->aa("XExcludeQuestNodeBySID", s.substr(i + 1, j - i - 1), e) == call_ok) ++nx;
                    else ++bad;
                    i = j - 1;
                }
            }
            std::string sp = " " + s;
            for (size_t i = 0; i + 1 < sp.size();)
            {
                if (sp[i] == ' ' && sp[i + 1] != '!' && !lspace(sp[i + 1]))
                {
                    size_t j = i + 1;
                    while (j < sp.size() && !lspace(sp[j])) ++j;
                    std::string tok = sp.substr(i + 1, j - i - 1), e;
                    if (E->aa("XStartQuestNodeBySID", tok, e) == call_ok)
                    {
                        ++ns;
                        names.push_back(tok);
                    }
                    else ++bad;
                    i = j;
                }
                else ++i;
            }
            P.ran += 1;
            log(F("NOLA qpick: origem c %s pino %lld seq=%s -> host XStartQuestNodeBySID %d [%s], XExclude %d, erros %d", it.sid.c_str(), it.k, it.seq.c_str(), ns,
                  join(names, " ").c_str(), nx, bad));
            return true;
        }

        void qpTick()
        {
            if (fl("qpick", 0) != 1) return;
            Qp& P = qpSt();
            double t = now();
            if (!host())
            {
                if (E->lead_dlg()) P.leadUntil = t + fl("qplead", 5);
            }
            else
            {
                for (int v = 0, NV = nv(); v < NV; ++v)
                for (auto& ent : gm(partv(v, "QP"), '|'))
                {
                    std::string seq, sid, k;
                    if (!parse3(ent, seq, sid, k)) continue;
                    std::string sk = vpfx(v) + seq + ":" + sid + "#" + k;
                    if (P.seen.count(sk)) continue;
                    P.seen[sk] = t;
                    double kd = 0;
                    tonum(k, kd);
                    QpQ q;
                    q.sid = sid;
                    q.k = static_cast<long long>(kd);
                    q.key = sid + "#" + k;
                    q.seq = seq;
                    q.at = t;
                    P.q.push_back(q);
                    log(F("NOLA qpick: origem c %s pino %s seq=%s%s recebido: espera %s s pelo grafo do host", sid.c_str(), k.c_str(), seq.c_str(), vwho(v).c_str(),
                          num(fl("qpwait", 8)).c_str()));
                }
                std::vector<QpQ> keep;
                for (auto& it : P.q)
                {
                    bool done = false;
                    auto o = P.own.find(it.key);
                    auto os = P.ownS.find(it.sid);
                    if (o != P.own.end() && o->second.n > 0 && t - o->second.at <= fl("qpkeep", 600))
                    {
                        o->second.n -= 1;
                        P.dup += 1;
                        done = true;
                        log(F("NOLA qpick: origem c %s pino %lld seq=%s: o grafo do host ja rodou esse pino (dialogo seguido): nada repetido", it.sid.c_str(), it.k, it.seq.c_str()));
                    }
                    else if (os != P.ownS.end() && t - os->second <= fl("qpwait", 8) + (t - it.at))
                    {
                        P.div += 1;
                        done = true;
                        log(F("NOLA qpick: DIVERGENCIA origem c %s pino %lld seq=%s: o host rodou outro pino desse dialogo: nada repetido", it.sid.c_str(), it.k, it.seq.c_str()));
                    }
                    else if (t - it.at >= fl("qpwait", 8) && !(E->host_dlg_open() && t - it.at < fl("qpmax", 120)))
                        done = qpRun(P, it, t);
                    if (!done) keep.push_back(it);
                }
                P.q = keep;
            }
            if (t - P.pruneAt >= 5)
            {
                P.pruneAt = t;
                for (auto it = P.seen.begin(); it != P.seen.end();) it = t - it->second > 60 ? P.seen.erase(it) : std::next(it);
                for (auto it = P.own.begin(); it != P.own.end();) it = t - it->second.at > fl("qpkeep", 600) ? P.own.erase(it) : std::next(it);
                for (auto it = P.ownS.begin(); it != P.ownS.end();) it = t - it->second > fl("qpkeep", 600) ? P.ownS.erase(it) : std::next(it);
                for (auto it = P.loc.begin(); it != P.loc.end();) it = (it->second.n <= 0 || t - it->second.at > fl("qplkeep", 900)) ? P.loc.erase(it) : std::next(it);
            }
        }

        std::string qiGive(const std::string& sid, int n, const std::string& why, bool match)
        {
            QiTab* T = qiTab();
            if (!T || fl("qigive", 1) != 1) return "qigive=0";
            Id pw = E->pawn();
            if (!pw) return "sem pawn";
            auto it = T->S.find(sid);
            int need = it != T->S.end() ? it->second : 1;
            int have = qiCount(pw, sid);
            int give = match ? (std::min(n, need) - have) : std::min(n, need - have);
            if (give <= 0) return F("host ja tem %d/%d: nada", have, need);
            std::string e;
            bool ok = E->give(sid, give, e);
            int after = qiCount(pw, sid);
            return F("host %d -> %d (+%d pedido, need %d) ok=%s%s [%s]", have, after, give, need, tf(ok), ok ? "" : (" " + e).c_str(), why.c_str());
        }

        template <class T> void hotIn(T& X, const char* name, double t)
        {
            if (X.hotIn) return;
            X.hotIn = true;
            auto k = x0::hot::logic::unpack(x0::hot::logic::take(name));
            if (!k.ok) return;
            X.seq = std::max(X.seq, k.seq);
            for (auto& s : k.keys) X.seen[s] = t;
            log(F("NOLA hot: %s seq %lld and %d relay entries kept across the reload (not applied again)", name, X.seq, static_cast<int>(k.keys.size())));
        }

        template <class T> void hotOut(T& X, const char* name)
        {
#ifdef S2X_RELEASE
            (void)X;
            (void)name;
#else
            std::string p = x0::hot::logic::pack(X.seq, x0::hot::logic::keys_of(X.seen));
            if (p == X.hotKept) return;
            X.hotKept = p;
            if (!x0::hot::logic::keep(name, p)) log(F("NOLA hot: WARNING %s stash (%d bytes) over the environment limit, tail keys cut, some relay entries may apply again after the reload", name, static_cast<int>(p.size())));
#endif
        }

        void qixIn(Qi& Q)
        {
            bool ok = false;
            auto x = x0::hot::logic::qix_unpack(x0::hot::logic::take("QIX"), ok);
            if (!ok) return;
            Q.based = x.based;
            for (auto& [sid, at, n] : x.sent) Q.sent[sid] = {at, n};
            for (auto& [e, has, t] : x.out) Q.out.push_back({e, has ? std::optional<double>(t) : std::nullopt});
            log(F("NOLA hot: QI baseline kept across the reload (based=%d, %d sent, %d queued)", Q.based ? 1 : 0, static_cast<int>(x.sent.size()), static_cast<int>(x.out.size())));
        }

        void qixOut(Qi& Q)
        {
#ifdef S2X_RELEASE
            (void)Q;
#else
            x0::hot::logic::QiX x;
            x.based = Q.based;
            for (auto& [sid, s] : Q.sent) x.sent.emplace_back(sid, s.at, s.n);
            for (auto& o : Q.out) x.out.emplace_back(o.e, o.untilT.has_value(), o.untilT.value_or(0.0));
            std::string p = x0::hot::logic::qix_pack(x);
            if (p == Q.hotKeptX) return;
            Q.hotKeptX = p;
            if (!x0::hot::logic::keep("QIX", p)) log(F("NOLA hot: WARNING QIX stash (%d bytes) over the environment limit, tail cut, a quest item baseline may be re-taken after the reload", static_cast<int>(p.size())));
#endif
        }

        DeadRes qgDead(Id pw, bool rebase)
        {
            DeadRes r;
            try { r = E->qg_dead(pw, rebase); } catch (...) { return DeadRes{}; }
            Qi& Q = S.qi;
            if (r.drop_pending) Q.pend.clear();
            if (r.rebase)
            {
                Q.based = false;
                Q.bn = 0;
                Q.sent.clear();
                Q.pend.clear();
            }
            return r;
        }

        void qiFwd(Qi& Q, const std::string& sid, int n, int v, const char* what)
        {
            Q.seq += 1;
            Q.out.push_back({std::to_string(Q.seq) + "," + sid + "," + std::to_string(n), std::nullopt});
            log(F("NOLA %s: %s x%d do pid %d repassado aos outros clientes (seq %lld)", what, sid.c_str(), n, E->view_pid(v), Q.seq));
        }

        void qmApply(QiTab& T, Qi& Q, const std::string& sid, int tc, const std::string& seq, double t)
        {
            Id pw = E->pawn();
            if (!pw) return;
            int have = qiCount(pw, sid);
            Q.pend.erase(sid);
            Q.re.erase(sid);
            Q.sent[sid] = {t, tc};
            Q.hold[sid] = t + 2 * fl("qmset", 3);
            std::string r;
            if (tc == have) r = "ja igual";
            else if (tc > have)
            {
                std::string e;
                bool ok = E->give(sid, tc - have, e);
                r = F("+%d ok=%s%s", tc - have, tf(ok), ok ? "" : (" " + e).c_str());
            }
            else if (T.norm.count(sid) || (host() && fl("qmrmhost", 0) != 1))
            {
                Q.sent[sid].n = have;
                Q.hold.erase(sid);
                r = T.norm.count(sid) ? "sem no de remocao (item fora do kit): fica" : "host sem o pak S2QI (qmrmhost=0): fica";
            }
            else
            {
                std::string e;
                bool ok = aaCall("XStartQuestNodeBySID", "S2QI_" + sid, e);
                if (ok && tc > 0) Q.re[sid] = {tc, t + fl("qmre", 1)};
                r = F("S2QI_%s ok=%s%s%s", sid.c_str(), tf(ok), ok ? "" : (" " + e).c_str(), (ok && tc > 0) ? F(", depois +%d", tc).c_str() : "");
            }
            log(F("NOLA qimirror: o outro jogador tem %s x%d (seq %s); eu tinha %d -> %s", sid.c_str(), tc, seq.c_str(), have, r.c_str()));
        }

        bool qmLoad(Qi& Q, Id pw, double t)
        {
            Id a = pw;
            if (!Q.pAddr)
            {
                Q.pAddr = a;
                return false;
            }
            if (a != *Q.pAddr)
            {
                Q.pAddr = a;
                std::map<std::string, int> agr;
                int n = 0;
                if (!host() && E->n7_idle())
                {
                    for (auto& [sid, s] : Q.sent)
                        if (s.n > 0)
                        {
                            agr[sid] = s.n;
                            ++n;
                        }
                }
                Q.agr = agr;
                Q.based = false;
                Q.bn = 0;
                Q.sent.clear();
                Q.pend.clear();
                Q.hold.clear();
                Q.re.clear();
                Q.ldWait = t + fl("qmloadw", 8);
                Q.ldN += 1;
                log(F("NOLA qmload: pawn novo #%d (save carregado / morte / respawn): contagens de itens de missao NAO enviadas; base relida em %.0f s%s", Q.ldN, fl("qmloadw", 8),
                      n > 0 ? F("; %d itens voltam ao combinado se faltarem", n).c_str() : ""));
            }
            if (Q.ldWait)
            {
                if (t < *Q.ldWait) return true;
                Q.ldWait.reset();
            }
            return false;
        }

        int qmTop(Qi& Q, const std::string& sid, int n, double t)
        {
            auto it = Q.agr.find(sid);
            if (it == Q.agr.end()) return n;
            int want = it->second;
            Q.agr.erase(it);
            if (n >= want) return n;
            std::string e;
            bool ok = E->give(sid, want - n, e);
            Q.hold[sid] = t + 2 * fl("qmset", 3);
            log(F("NOLA qmload: %s %d -> %d (o combinado antes do load) ok=%s%s; nada enviado", sid.c_str(), n, want, tf(ok), ok ? "" : (" " + e).c_str()));
            return ok ? want : n;
        }

        void qmTick(QiTab& T, Qi& Q, double t)
        {
            double set = fl("qmset", 3);
            int NV = nv();
            for (int v = 0; v < NV; ++v)
            for (auto& ent : gm(partv(v, "QI"), '|'))
            {
                std::string sk = vpfx(v) + ent;
                if (Q.seen.count(sk)) continue;
                Q.seen[sk] = t;
                std::string seq, sid, n;
                if (parse3(ent, seq, sid, n) && T.S.count(sid))
                {
                    int had = NV > 1 && E->pawn() ? qiCount(E->pawn(), sid) : 0;
                    qmApply(T, Q, sid, std::atoi(n.c_str()), seq, t);
                    if (NV > 1 && E->pawn() && had != std::atoi(n.c_str())) qiFwd(Q, sid, std::atoi(n.c_str()), v, "qimirror");
                }
                else if (parse3(ent, seq, sid, n)) log("NOLA qimirror: relay ignorado (fora da tabela): " + ent);
            }
            for (auto it = Q.seen.begin(); it != Q.seen.end();) it = t - it->second > 60 ? Q.seen.erase(it) : std::next(it);
            for (auto it = Q.re.begin(); it != Q.re.end();)
            {
                if (t >= it->second.at)
                {
                    std::string sid = it->first, e;
                    int n = it->second.n;
                    it = Q.re.erase(it);
                    bool ok = E->give(sid, n, e);
                    log(F("NOLA qimirror: %s devolvido x%d depois da remocao ok=%s", sid.c_str(), n, tf(ok)));
                }
                else ++it;
            }
            if (t < Q.at) return;
            Q.at = t + fl("qisec", 0.5);
            Id pw = E->pawn();
            if (!pw) return;
            if (fl("qmload", 1) == 1 && qmLoad(Q, pw, t)) return;
            if (qgDead(pw, true).skip) return;
            size_t nL = T.list.size();
            if (nL == 0) return;
            std::set<std::string> look;
            std::vector<std::string> order;
            auto add = [&](const std::string& sid) {
                if (look.insert(sid).second) order.push_back(sid);
            };
            size_t lim = std::min(static_cast<size_t>(std::max(0.0, fl("qiper", 80))), nL);
            for (size_t k = 0; k < lim; ++k)
            {
                Q.i = Q.i % nL + 1;
                const std::string& sid = T.list[Q.i - 1];
                if (!Q.based)
                {
                    int n = qiCount(pw, sid);
                    if (Q.agr.count(sid)) n = qmTop(Q, sid, n, t);
                    if (n > 0) Q.sent[sid] = {t, n};
                    Q.bn += 1;
                    if (static_cast<size_t>(Q.bn) >= nL)
                    {
                        Q.based = true;
                        log("NOLA qimirror: inventario base lido (" + std::to_string(nL) + " itens de missao); a partir de agora a contagem de cada um e espelhada");
                    }
                }
                else add(sid);
            }
            if (!Q.based) return;
            for (auto& [sid, s] : Q.sent)
                if (s.n > 0) add(sid);
            for (auto& [sid, p] : Q.pend) add(sid);
            for (auto& sid : order)
            {
                int n = qiCount(pw, sid);
                auto si = Q.sent.find(sid);
                int base = si != Q.sent.end() ? si->second.n : 0;
                auto h = Q.hold.find(sid);
                if ((h != Q.hold.end() && h->second > t) || Q.re.count(sid)) {}
                else if (n != base)
                {
                    auto p = Q.pend.find(sid);
                    if (p == Q.pend.end() || p->second.n != n) Q.pend[sid] = {n, t};
                    else if (t - p->second.at >= set)
                    {
                        Q.pend.erase(p);
                        Q.sent[sid] = {t, n};
                        Q.seq += 1;
                        Q.out.push_back({std::to_string(Q.seq) + "," + sid + "," + std::to_string(n), std::nullopt});
                        log(F("NOLA qimirror: %s %d -> %d aqui (estavel %.0f s); contagem enviada ao outro (seq %lld)", sid.c_str(), base, n, set, Q.seq));
                    }
                }
                else if (Q.pend.count(sid))
                {
                    Q.pend.erase(sid);
                    log(F("NOLA qimirror: %s voltou a %d em menos de %.0f s: nada enviado", sid.c_str(), n, set));
                }
            }
        }

        void ldQi(QiTab& T, Qi& Q, double t)
        {
            if (fl("qimirror", 0) == 1)
            {
                qmTick(T, Q, t);
                return;
            }
            int NV = nv();
            for (int v = 0; v < NV; ++v)
            for (auto& ent : gm(partv(v, "QI"), '|'))
            {
                std::string sk = vpfx(v) + ent;
                if (Q.seen.count(sk)) continue;
                Q.seen[sk] = t;
                std::string seq, sid, ns;
                if (parse3(ent, seq, sid, ns) && T.S.count(sid))
                {
                    int n = std::atoi(ns.c_str());
                    int had = NV > 1 && E->pawn() ? qiCount(E->pawn(), sid) : 0;
                    std::string r = qiGive(sid, n, "copia do outro jogador", true);
                    int c = qiCount(E->pawn(), sid);
                    Q.sent[sid] = {t, std::max(c, std::min(n, T.S[sid]))};
                    log("NOLA lead: o outro jogador" + vwho(v) + " pegou " + sid + " x" + std::to_string(n) + " (seq " + seq + ") -> copia: " + r);
                    if (NV > 1 && n > had) qiFwd(Q, sid, n, v, "lead");
                }
                else if (parse3(ent, seq, sid, ns)) log("NOLA lead: relay de item ignorado (fora da tabela): " + ent);
            }
            for (auto it = Q.seen.begin(); it != Q.seen.end();) it = t - it->second > 60 ? Q.seen.erase(it) : std::next(it);
            if (t < Q.at) return;
            Q.at = t + fl("qisec", 0.5);
            Id pw = E->pawn();
            if (!pw) return;
            if (qgDead(pw, true).skip) return;
            size_t nL = T.list.size();
            if (nL == 0) return;
            size_t lim = std::min(static_cast<size_t>(std::max(0.0, fl("qiper", 80))), nL);
            for (size_t k = 0; k < lim; ++k)
            {
                Q.i = Q.i % nL + 1;
                const std::string& sid = T.list[Q.i - 1];
                int n = qiCount(pw, sid);
                auto si = Q.sent.find(sid);
                if (!Q.based)
                {
                    if (n > 0) Q.sent[sid] = {t, n};
                    Q.bn += 1;
                    if (static_cast<size_t>(Q.bn) >= nL)
                    {
                        Q.based = true;
                        log("NOLA lead: inventario base lido (" + std::to_string(nL) + " itens de missao); a partir de agora cada item pego vai em copia ao outro");
                    }
                }
                else if (n > (si != Q.sent.end() ? si->second.n : 0))
                {
                    Q.sent[sid] = {t, n};
                    Q.seq += 1;
                    Q.out.push_back({std::to_string(Q.seq) + "," + sid + "," + std::to_string(n), std::nullopt});
                    log(F("NOLA lead: item de missao pego aqui: %s x%d (fica com este jogador); copia enviada ao outro (seq %lld)", sid.c_str(), n, Q.seq));
                }
                else if (si != Q.sent.end() && n < si->second.n) si->second.n = n;
            }
        }

        void qiTickRun()
        {
            if (fl("qitem", 0) != 1 && fl("lead", 0) != 1) return;
            QiTab* T = qiTab();
            if (!T) return;
            Qi& Q = S.qi;
            double t = now();
            if (fl("lead", 0) == 1)
            {
                ldQi(*T, Q, t);
                return;
            }
            if (host())
            {
                for (int v = 0, NV = nv(); v < NV; ++v)
                for (auto& ent : gm(partv(v, "QI"), '|'))
                {
                    std::string sk = vpfx(v) + ent;
                    if (Q.seen.count(sk)) continue;
                    Q.seen[sk] = t;
                    std::string seq, sid, n;
                    if (parse3(ent, seq, sid, n) && T->S.count(sid))
                        log("NOLA qitem: cliente" + vwho(v) + " pegou " + sid + " x" + n + " (seq " + seq + ") -> " + qiGive(sid, std::atoi(n.c_str()), "relay do cliente", false));
                    else if (parse3(ent, seq, sid, n)) log("NOLA qitem: relay ignorado (fora da tabela): " + ent);
                }
                for (auto it = Q.seen.begin(); it != Q.seen.end();) it = t - it->second > 60 ? Q.seen.erase(it) : std::next(it);
                return;
            }
            if (t < Q.at) return;
            Q.at = t + fl("qisec", 0.5);
            Id pw = E->pawn();
            if (!pw) return;
            if (qgDead(pw, false).skip) return;
            size_t nL = T->list.size();
            if (nL == 0) return;
            size_t lim = std::min(static_cast<size_t>(std::max(0.0, fl("qiper", 80))), nL);
            for (size_t k = 0; k < lim; ++k)
            {
                Q.i = Q.i % nL + 1;
                const std::string& sid = T->list[Q.i - 1];
                int n = qiCount(pw, sid);
                auto si = Q.sent.find(sid);
                if (n > 0)
                {
                    bool had = si != Q.sent.end();
                    if (!had || t - si->second.at > fl("qire", 20) || n > si->second.n)
                    {
                        Q.sent[sid] = {t, n};
                        Q.seq += 1;
                        Q.out.push_back({std::to_string(Q.seq) + "," + sid + "," + std::to_string(n), std::nullopt});
                        std::string rm = "remocao desligada (qirm=0)";
                        if (fl("qirm", 1) == 1 && !T->norm.count(sid))
                        {
                            std::string e;
                            bool ok = aaCall("XStartQuestNodeBySID", "S2QI_" + sid, e);
                            rm = "S2QI_" + sid + " ok=" + tf(ok) + (ok ? "" : " " + e);
                        }
                        else if (T->norm.count(sid)) rm = "sem no de remocao (item fora do kit)";
                        log(F("NOLA qitem: item de missao no cliente: %s x%d%s -> %s; enviado ao host (seq %lld)", sid.c_str(), n, had ? " (de novo)" : "", rm.c_str(), Q.seq));
                    }
                }
                else if (si != Q.sent.end() && si->second.n > 0)
                {
                    log("NOLA qitem: " + sid + " saiu do inventario do cliente (" + F("%.1f", t - si->second.at) + " s)");
                    si->second.n = 0;
                }
            }
        }

        void qiTick()
        {
            bool first = !S.qi.hotIn;
            hotIn(S.qi, "QI", now());
            if (first) qixIn(S.qi);
            qiTickRun();
            hotOut(S.qi, "QI");
            qixOut(S.qi);
        }

        std::string qiOut()
        {
            if (fl("lead", 0) != 1 && (fl("qitem", 0) != 1 || host())) return "";
            Qi& Q = S.qi;
            if (Q.out.empty()) return "";
            double t = now();
            std::vector<QiOut> keep;
            std::vector<std::string> r;
            for (auto& o : Q.out)
            {
                if (r.size() < 8 && !o.untilT) o.untilT = t + fl("resend", 4);
                if (!o.untilT) keep.push_back(o);
                else if (t < *o.untilT)
                {
                    keep.push_back(o);
                    r.push_back(o.e);
                }
            }
            Q.out = keep;
            return r.empty() ? std::string() : "#QI:" + join(r, "|");
        }

        QdrTab* qdrTab()
        {
            if (S.qdrLoaded != 0) return S.qdrLoaded > 0 ? &S.qdrt : nullptr;
            S.qdrLoaded = -1;
            std::string text, err;
            std::vector<D1Row> rows;
            if (!E->data("s2u_qdrop.tsv", text)) err = "s2u_qdrop.tsv nao encontrado";
            else if (!d1_parse(text, rows, err)) err = "s2u_qdrop.tsv " + err;
            QdrTab T;
            std::set<std::string> ids;
            bool hasD = false;
            if (err.empty())
                for (auto& r : rows)
                    if (r.key.size() >= 2 && r.key[0] == "D")
                    {
                        ids.insert(r.key[1]);
                        hasD = true;
                    }
            if (err.empty() && !hasD) err = "s2u_qdrop.tsv sem D";
            if (!err.empty())
            {
                log("NOLA qdrop: s2u_qdrop.lua ausente ou invalido: " + err);
                return nullptr;
            }
            T.list.assign(ids.begin(), ids.end());
            S.qdrt = std::move(T);
            S.qdrLoaded = 1;
            log(F("NOLA qdrop: %d itens de missao podem ser largados segurando D (pak S2QD)", static_cast<int>(S.qdrt.list.size())));
            return &S.qdrt;
        }

        std::pair<int, int> qdrSet(Qdh& Q, bool onv)
        {
            int n = 0, okN = 0;
            bool aa = E->aa_present();
            for (auto& [sid, c] : Q.held)
            {
                ++n;
                std::string e;
                if (aa && E->aa("XStartQuestNodeBySID", (onv ? "S2QD0_" : "S2QD1_") + sid, e) == call_ok) ++okN;
            }
            return {okN, n};
        }

        void qdrEnd(Qdh& Q, const std::string& why)
        {
            if (Q.st && (*Q.st == "drop" || *Q.st == "win"))
            {
                auto [okN, n] = qdrSet(Q, false);
                log(F("NOLA qdrop: %s: %d/%d itens voltaram a ser item de missao (S2QD1)", why.c_str(), okN, n));
            }
            else log("NOLA qdrop: " + why);
            Q.st.reset();
            Q.held.clear();
            Q.needUp = E->key_d();
        }

        void qdrRun(Qdh& Q)
        {
            QdrTab* T = qdrTab();
            if (!T) return;
            if (!g_qdropKey)
            {
                g_qdropKey = E->bind_d();
                log("NOLA qdrop: RegisterKeyBind D -> " + *g_qdropKey);
            }
            double t = now();
            Id addr = E->pawn();
            if (Q.st && (!Q.addr || addr != *Q.addr))
            {
                if (!addr) return;
                qdrEnd(Q, "pawn novo: load/morte/respawn");
                return;
            }
            if (!addr) return;
            std::optional<double> kAt = S.qdrKeyAt;
            if (!Q.st)
            {
                if (Q.needUp)
                {
                    if (E->key_d())
                    {
                        Q.used = kAt;
                        return;
                    }
                    Q.needUp = false;
                }
                if (!kAt || (Q.used && *kAt == *Q.used)) return;
                Q.used = kAt;
                if (t - *kAt > 0.5) return;
                if (!E->inv_open()) return;
                Id hv = E->hovered();
                if (!hv) return;
                std::map<std::string, int> held;
                int nh = 0;
                for (auto& sid : T->list)
                {
                    int n = qiCount(addr, sid);
                    if (n > 0)
                    {
                        held[sid] = n;
                        ++nh;
                    }
                }
                if (nh == 0) return;
                Q.st = "hold";
                Q.t0 = *kAt;
                Q.hv = hv;
                Q.held = held;
                Q.addr = addr;
                Q.n += 1;
                try { E->qf_arm("qdrop"); } catch (...) {}
                return;
            }
            Q.used = kAt;
            bool w = E->inv_open();
            if (*Q.st == "hold")
            {
                if (!E->key_d())
                {
                    qdrEnd(Q, F("D solto antes de %d ms: toque = recusa normal do jogo", static_cast<int>(fl("qdrophold", 1000))));
                    return;
                }
                if (!w || E->hovered() != Q.hv)
                {
                    qdrEnd(Q, "inventario fechado ou outro item sob o cursor: nada liberado");
                    return;
                }
                if ((t - Q.t0) * 1000 < fl("qdrophold", 1000)) return;
                auto [okN, n] = qdrSet(Q, true);
                Q.st = "drop";
                Q.callAt = t + 0.1;
                Q.chkEnd.reset();
                Q.capT = t + fl("qdropwin", 4) + 10;
                Q.via = "?";
                log(F("NOLA qdrop: D segurado #%d: %d/%d itens de missao liberados (S2QD0)", Q.n, okN, n));
                return;
            }
            for (auto& [sid, n0] : Q.held)
            {
                int n = qiCount(addr, sid);
                if (n < n0)
                {
                    Q.drops += 1;
                    std::string why = F("%s largado segurando D (%d -> %d) #%d via %s", sid.c_str(), n0, n, Q.drops, Q.via.c_str());
                    qdrEnd(Q, why);
                    return;
                }
            }
            if (!w)
            {
                qdrEnd(Q, "inventario fechado, nada largado");
                return;
            }
            if (t >= Q.capT)
            {
                qdrEnd(Q, "limite de tempo, nada largado");
                return;
            }
            if (*Q.st == "drop")
            {
                if (Q.callAt && t >= *Q.callAt)
                {
                    Q.callAt.reset();
                    Q.chkEnd = t + 0.6;
                    double fnI = fl("qdropfn", 1);
                    const char* fn = fnI == 1 ? "UIInventoryDropItem" : (fnI == 2 ? "OnDropItemHoldTriggered" : nullptr);
                    if (!fn)
                    {
                        Q.chkEnd = t;
                        return;
                    }
                    if (E->hovered() != Q.hv)
                    {
                        log("NOLA qdrop: outro item sob o cursor: sem chamada de largar");
                        Q.chkEnd = t;
                        return;
                    }
                    std::string e;
                    bool ok = E->inv_call(fn, e);
                    Q.via = fn;
                    Q.calls += 1;
                    if (!ok) log(std::string("NOLA qdrop: ") + fn + " erro: " + e);
                }
                else if (Q.chkEnd && t >= *Q.chkEnd)
                {
                    Q.st = "win";
                    Q.relAt.reset();
                    Q.via = "toque na janela";
                    Q.fb += 1;
                    log(F("NOLA qdrop: a chamada nao largou: itens liberados ate %d s depois de soltar D (o proximo toque larga)", static_cast<int>(fl("qdropwin", 4))));
                }
                return;
            }
            if (E->key_d()) Q.relAt.reset();
            else if (!Q.relAt) Q.relAt = t;
            if (Q.relAt && t - *Q.relAt >= fl("qdropwin", 4)) qdrEnd(Q, "janela acabou, nada largado");
        }

        void qdrTick()
        {
            if (fl("qdrop", 1) != 1) return;
            if (!S.qdh) S.qdh.emplace();
            Qdh& Q = *S.qdh;
            try
            {
                qdrRun(Q);
            }
            catch (const std::exception& e)
            {
                Q.err += 1;
                if (Q.err <= 5) log(std::string("NOLA qdrop erro: ") + e.what());
            }
        }

        Rw& rwInit()
        {
            if (!S.rw) S.rw.emplace();
            return *S.rw;
        }

        void questFinishedI(const std::string& quest)
        {
            Rw& R = rwInit();
            R.pend.push_back({quest, now()});
        }

        std::string invDelta(const std::map<std::string, int>& a, const std::map<std::string, int>& b)
        {
            std::vector<std::string> r;
            for (auto& [s, n] : b)
            {
                auto it = a.find(s);
                int d = n - (it != a.end() ? it->second : 0);
                if (d > 0) r.push_back(s + "x" + std::to_string(d));
            }
            std::sort(r.begin(), r.end());
            return join(r, ",");
        }

        void rewardTickRun();

        void rewardTick()
        {
            if (fl("qreward", 1) != 1) return;
            hotIn(rwInit(), "RW", now());
            rewardTickRun();
            hotOut(rwInit(), "RW");
        }

        void rewardTickRun()
        {
            Rw& R = rwInit();
            double t = now();
            Id pw = E->pawn();
            if (!pw) return;
            if (host())
            {
                if (t >= R.snapAt)
                {
                    R.snapAt = t + 3;
                    Inv iv = E->inv_snap(pw);
                    R.snaps.push_back({t, iv.items, iv.money});
                    while (R.snaps.size() > 8) R.snaps.erase(R.snaps.begin());
                }
                for (size_t i = R.pend.size(); i-- > 0;)
                {
                    RwPend p = R.pend[i];
                    if (t - p.at >= 3)
                    {
                        R.pend.erase(R.pend.begin() + static_cast<long>(i));
                        const RwSnap* base = nullptr;
                        for (auto& s : R.snaps)
                            if (s.t <= p.at - 6) base = &s;
                        if (!base && !R.snaps.empty()) base = &R.snaps[0];
                        if (base)
                        {
                            Inv iv = E->inv_snap(pw);
                            std::string items = invDelta(base->items, iv.items);
                            long long dm = static_cast<long long>(std::floor((iv.money - base->money) + 0.5));
                            if (dm < 0) dm = 0;
                            if (dm == 0 && items.empty()) log("NOLA reward: missao " + p.q + " terminou sem ganho no host; nada enviado");
                            else
                            {
                                R.seq += 1;
                                std::string e = F("%lld;%s;%lld;%s", R.seq, p.q.c_str(), dm, items.c_str());
                                R.out.push_back({e, t + fl("resend", 6)});
                                log(F("NOLA reward: missao %s terminou; ganho do host desde %.0f s antes: dinheiro +%lld, itens %s -> enviado ao cliente", p.q.c_str(), p.at - base->t, dm,
                                      items.empty() ? "-" : items.c_str()));
                            }
                        }
                    }
                }
            }
            if (!host() || fl("rwtest", 0) == 1)
            {
                for (auto& ent : gm(part("W"), '|'))
                {
                    if (R.seen.count(ent)) continue;
                    R.seen[ent] = t;
                    auto p = split_keep(ent, ';');
                    if (p.size() < 4) continue;
                    std::string items = ent.substr(p[0].size() + p[1].size() + p[2].size() + 3);
                    if (!all_digits(p[0]) || p[1].empty() || !all_digits(p[2])) continue;
                    const std::string& q = p[1];
                    if (E->n7_rw(q)) continue;
                    double m0 = E->money(pw);
                    long long dm = std::atoll(p[2].c_str());
                    if (dm > 0) E->add_money(pw, dm);
                    double m1 = E->money(pw);
                    std::vector<std::string> got;
                    size_t i = 0;
                    auto w = [](char c) { return lalnum(c) || c == '_'; };
                    while (i < items.size())
                    {
                        if (!w(items[i]))
                        {
                            ++i;
                            continue;
                        }
                        size_t j = i;
                        while (j < items.size() && w(items[j])) ++j;
                        size_t k = j;
                        while (k > i + 1 && !(items[k - 1] == 'x' && k < j && ldigit(items[k]))) --k;
                        if (k <= i + 1)
                        {
                            i = j;
                            continue;
                        }
                        size_t xk = k - 1, d = k;
                        while (d < items.size() && ldigit(items[d])) ++d;
                        std::string sid = items.substr(i, xk - i), nn = items.substr(k, d - k), e;
                        bool ok = E->give(sid, std::atoi(nn.c_str()), e);
                        got.push_back(sid + "+" + nn + (ok ? "" : "(erro)"));
                        i = d;
                    }
                    log(F("NOLA reward: recompensa da missao %s recebida: dinheiro %.0f -> %.0f, itens %s", q.c_str(), m0, m1, got.empty() ? "-" : join(got, " ").c_str()));
                }
                for (auto it = R.seen.begin(); it != R.seen.end();) it = t - it->second > 30 ? R.seen.erase(it) : std::next(it);
            }
        }

        std::string rewardOut()
        {
            if (!S.rw || fl("qreward", 1) != 1 || !host()) return "";
            double t = now();
            std::vector<std::string> r;
            for (auto& o : S.rw->out)
                if (t < o.untilT) r.push_back(o.e);
            return r.empty() ? std::string() : "#W:" + join(r, "|");
        }

        template <class Fn> void guarded(const char* name, int* counter, const char* sep, Fn&& fn)
        {
            try
            {
                fn();
            }
            catch (const std::exception& e)
            {
                if (!counter) return;
                *counter += 1;
                if (*counter <= 5) log(std::string("NOLA ") + name + sep + e.what());
            }
            catch (...)
            {
                if (!counter) return;
                *counter += 1;
                if (*counter <= 5) log(std::string("NOLA ") + name + sep + "?");
            }
        }
    }

    Hooks& hooks() { return g_hooks; }
    bool busy_of(const BusyIn& in, std::string& why)
    {
        bool view = false;
        for (const auto& v : in.views)
            if (s2m_dlgview::shown(v.first, v.second)) view = true;
        if (in.lead_dlg || in.host_dlg || view)
        {
            why = "dialogo";
            return true;
        }
        if (in.cine || in.pc_cine)
        {
            why = "cinematica";
            return true;
        }
        return false;
    }
    void bind(Env* env) { E = env; }
    Env* env() { return E; }

    void reset()
    {
        S = State();
        g_qitLoaded = 0;
        g_qit = QiTab();
        g_qlogBase.reset();
        g_qdropKey.reset();
    }

    void tick100()
    {
        tick100_rb();
        tick100_trig();
        tick100_qp();
        tick100_qi();
        tick100_qb();
    }

    void tick100_rb()
    {
        if (!E) return;
        guarded("rbTick", &S.b6err, " erro: ", [] { rbTick(); });
        guarded("rewardTick", &S.c22err, " erro ", [] { rewardTick(); });
    }

    void tick100_trig()
    {
        if (!E) return;
        guarded("trigTick", nullptr, "", [] { trigTick(); });
        guarded("haTick", nullptr, "", [] { haTick(); });
    }

    void tick100_qp()
    {
        if (!E) return;
        guarded("qpTick", nullptr, "", [] { qpTick(); });
    }

    void tick100_qb()
    {
        if (!E) return;
        guarded("qbTick", nullptr, "", [] { qbTick(); });
    }

    int qb_fire(const std::string& node, const std::string& pin, bool fin, std::string& sub)
    {
        if (!E) return 0;
        int r = 0;
        guarded("qbFire", nullptr, "", [&] { r = qbFire(node, pin, fin, sub); });
        return r;
    }

    bool qb_answer(const std::string& phrase)
    {
        if (!E) return true;
        bool r = true;
        guarded("qbAnswer", nullptr, "", [&] { r = qbAnswer(phrase); });
        return r;
    }

    bool qb_on() { return E && qbOn(); }

    void tick100_qi()
    {
        if (!E) return;
        guarded("qiTick", nullptr, "", [] { qiTick(); });
        guarded("qdrTick", nullptr, "", [] { qdrTick(); });
    }

    void tick1000()
    {
        if (!E) return;
        S.beat += 1;
        if (S.beat % 5 == 0) guarded("questRollback", nullptr, "", [] { questRollback(); });
        guarded("qlogFlush", nullptr, "", [] { qlogFlush(); });
        guarded("qdTick", nullptr, "", [] { qdTick(); });
        guarded("qcTick", nullptr, "", [] { qcTick(); });
    }

    void console(std::string_view full)
    {
        if (!E) return;
        guarded("questCmd", nullptr, "", [&] { questCmd(full); });
    }

    void key_d_pressed()
    {
        if (!E) return;
        S.qdrKeyAt = now();
    }

    std::string out_qn() { return E ? trigOut() : std::string(); }
    std::string out_w() { return E ? rewardOut() : std::string(); }
    std::string out_j() { return E ? questOutJ() : std::string(); }
    std::string out_qd() { return E ? qdOut() : std::string(); }
    std::string out_qh() { return E ? haOut() : std::string(); }
    std::string out_s() { return E ? catchupOut() : std::string(); }
    std::string out_qd_v(int v) { return E ? qdOutV(v) : std::string(); }
    std::string out_s_v(int v) { return E ? catchupOutV(v) : std::string(); }
    std::string out_b() { return E ? rbOut() : std::string(); }
    std::string out_qi() { return E ? qiOut() : std::string(); }
    std::string out_qp() { return E ? qpOut() : std::string(); }
    std::string out_qf() { return E ? qcOut() : std::string(); }
    std::string out_qb() { return E ? qbOut() : std::string(); }

    bool qi_table_on() { return E && qiTab() != nullptr; }
    bool is_quest_item(std::string_view sid)
    {
        if (!E) return false;
        QiTab* T = qiTab();
        return T && T->S.count(std::string(sid)) > 0;
    }
    int qi_need(std::string_view sid)
    {
        if (!E) return 0;
        QiTab* T = qiTab();
        if (!T) return 0;
        auto it = T->S.find(std::string(sid));
        return it == T->S.end() ? 0 : it->second;
    }
    const std::vector<std::string>& qi_list()
    {
        static const std::vector<std::string> none;
        if (!E) return none;
        QiTab* T = qiTab();
        return T ? T->list : none;
    }
    const std::map<std::string, int>& qi_needs()
    {
        static const std::map<std::string, int> none;
        if (!E) return none;
        QiTab* T = qiTab();
        return T ? T->S : none;
    }

    bool qi_corpse(Id o, const std::string& id)
    {
        if (!E || fl("qitem", 0) != 1) return false;
        QiTab* T = qiTab();
        if (!T) return false;
        std::vector<std::pair<std::string, int>> got;
        for (auto& s : T->list)
        {
            int n = qiCount(o, s);
            if (n > 0) got.push_back({s, n});
        }
        if (got.empty()) return false;
        for (auto& g : got)
            log("NOLA qitem: corpo " + id + " tem " + g.first + " x" + std::to_string(g.second) + " (cliente pediu o saque) -> " + qiGive(g.first, g.second, "saque do cliente", false));
        log("NOLA qitem: corpo " + id + " NAO marcado como entregue; o host continua podendo abrir");
        return true;
    }

    bool qi_host_keep(const std::string& id, Id pw)
    {
        if (!E || fl("qitem", 0) != 1 || !host() || id.empty()) return false;
        QiTab* T = qiTab();
        if (!T) return false;
        Qi& Q = S.qi;
        auto k = Q.keep.find(id);
        if (k != Q.keep.end()) return k->second;
        std::optional<std::string> keep;
        for (size_t at = id.find('@'); at != std::string::npos && !keep; at = id.find('@', at + 1))
        {
            auto p = split_keep(std::string_view(id).substr(at + 1), ',');
            if (p.size() != 3) continue;
            bool good = true;
            for (auto& s : p)
            {
                if (s.empty()) good = false;
                for (char c : s)
                    if (!ldigit(c) && c != '-' && c != '.') good = false;
            }
            if (!good) continue;
            double a, b, c;
            if (!tonum(p[0], a) || !tonum(p[1], b) || !tonum(p[2], c)) break;
            double x = a * 10, y = b * 10, z = c * 10, r = fl("qir", 400), r2 = r * r;
            for (auto& ps : T->pos)
                if ((ps.x - x) * (ps.x - x) + (ps.y - y) * (ps.y - y) + (ps.z - z) * (ps.z - z) <= r2)
                {
                    keep = ps.guid + " " + ps.sids;
                    break;
                }
            break;
        }
        if (!keep && starts(id, "BP_AI_Human_C"))
        {
            Id o = pw ? E->interacted(pw) : 0;
            if (o)
                for (auto& s : T->list)
                    if (qiCount(o, s) > 0)
                    {
                        keep = "corpo com " + s;
                        break;
                    }
        }
        Q.keep[id] = keep.has_value();
        if (keep) log("NOLA qitem: " + id + " pode ter item de missao (" + *keep + "): o host nunca e negado (TAKEN_DENY pulado)");
        return Q.keep[id];
    }

    void qi_reset()
    {
        Qi& Q = S.qi;
        Q.based = false;
        Q.bn = 0;
        Q.sent.clear();
        Q.pend.clear();
        Q.hold.clear();
        Q.re.clear();
    }

    Applied quest_apply(const std::string& kind, const std::optional<std::string>& a, const std::optional<std::string>& b, const std::optional<std::string>& c,
                        const std::optional<std::string>& sid, bool live)
    {
        if (!E) return Applied{false, -1, std::nullopt, "sem env"};
        try
        {
            return questApplyI(kind, a, b, c, sid, live);
        }
        catch (const std::exception& x)
        {
            return Applied{false, -1, std::nullopt, x.what()};
        }
    }
    Applied quest_apply_ev(const std::string& ev, bool live)
    {
        if (!E) return Applied{false, -1, std::nullopt, "sem env"};
        g_cuApply = true;
        Applied r;
        try { r = questApplyEv(ev, live); }
        catch (...)
        {
            g_cuApply = false;
            throw;
        }
        g_cuApply = false;
        return r;
    }
    void rb_done(const std::string& why)
    {
        if (E) rbDoneI(why);
    }
    bool rb_ready() { return E ? rbReadyI() : true; }
    void rb_bump(const std::string& why)
    {
        if (E) rbBumpI(why);
    }
    void camp_rotate(const std::string& why)
    {
        if (E) campRotateI(why);
    }
    std::string camp_id()
    {
        if (!E || !host()) return std::string();
        return campId();
    }
    std::size_t host_log_size()
    {
        if (!E) return 0;
        qlogLoad();
        return S.qs.log.size();
    }
    std::optional<int64_t> host_gen()
    {
        if (!S.qs.gen) return std::nullopt;
        return static_cast<int64_t>(*S.qs.gen);
    }
    void quest_finished(const std::string& quest)
    {
        if (E) questFinishedI(quest);
    }
    bool qp_skip(const std::string& sid) { return E ? qpSkipI(sid) : false; }

    std::string status()
    {
        std::string s = F("quest: seq=%lld got=%d sent=%d log=%d qi.seq=%lld qi.out=%d", S.qs.seq, S.qs.got, S.qs.sent, static_cast<int>(S.qs.log.size()), S.qi.seq,
                          static_cast<int>(S.qi.out.size()));
        if (S.trig) s += F(" trig.qn=%lld out=%d", S.trig->qn, static_cast<int>(S.trig->out.size()));
        if (S.qd) s += F(" qdiv rn=%lld rep=%d roll=%d", S.qd->rn, S.qd->nrep, S.qd->nroll);
        if (S.qp) s += F(" qpick sent=%d ran=%d dup=%d div=%d skip=%d", S.qp->nsent, S.qp->ran, S.qp->dup, S.qp->div, S.qp->skip);
        if (S.qc) s += F(" qcatch ok=%d miss=%d defer=%d own=%d%s%s", S.qc->nok, S.qc->nmiss, S.qc->ndefer, static_cast<int>(S.qc->own.size()),
                         S.qc->nadd || S.qc->nitem ? F(" add=%d item=%d", S.qc->nadd, S.qc->nitem).c_str() : "", S.qc->cur ? (" cur=" + S.qc->cur->sid).c_str() : "");
        if (S.qb && S.qb->loaded > 0)
            s += F(" qbranch nos=%d respostas=%d rec=%d ok=%d sub=%d hold=%d force=%d rel=%d ref=%d mir=%d err=%d defer=%d dup=%d held=%d", static_cast<int>(S.qb->tab.size()), static_cast<int>(S.qb->ans.size()), S.qb->nrec, S.qb->nok, S.qb->nsub, S.qb->nhold, S.qb->nforce,
                   S.qb->nrel, S.qb->nref, S.qb->nmir, S.qb->nerr, S.qb->ndefer, S.qb->ndup, static_cast<int>(S.qb->held.size()));
        return s;
    }
}

#ifndef S2M_QUEST_STANDALONE
#include <ctime>
#include <random>
#include <windows.h>
#include "s2x.h"
#include "s2m_bodies.h"
#include "s2m_lock.h"
#include "s2m_pop.h"
#include "s2m_qbranch.h"
#include "s2m_travel.h"

extern "C" bool s2m_solo_quest_muted(const char* quest);
extern "C" void s2m_lockbehind_tick();

extern "C" bool s2m_ls_before_load();

namespace s2m_quest
{
    namespace
    {
        std::wstring W(const std::string& s)
        {
            if (s.empty()) return {};
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
            std::wstring w(static_cast<size_t>(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
            return w;
        }

        x0::Ref R(Id a) { return a ? x0::Ref::addr(a) : x0::Ref{}; }

        x0::Ref mediator() { return x0::find_path(L"/Script/Stalker2.Default__CppMediator"); }

        struct ProdEnv final : Env
        {
            std::mt19937 rng{static_cast<unsigned>(std::time(nullptr))};

            double now() override { return x0::now(); }
            bool host() override { return x0::is_host(); }
            double fl(const char* key, double def) override
            {
                x0::Val v = x0::flag(key);
                if (v.k == x0::Val::nil) return def;
                return v.num();
            }
            std::string fls(const char* key) override
            {
                x0::Val v = x0::flag(key);
                if (v.k == x0::Val::nil) return {};
                return v.str();
            }
            void fset(const char* key, double v, const char* why) override { x0::fset(key, x0::Val::I(static_cast<int64_t>(v)), why); }
            void log(const std::string& line) override { x0::log(line); }
            void screen(const char* id, const std::string& text, double secs) override { x0::ui_msg(id, text, secs); }
            bool part(const char* tag, std::string& out) override { return x0::rpart(tag, out); }
            int peer_state() override { return static_cast<int>(x0::peer().state); }
            int peer_sessions() override { return x0::peer().sessions; }
            bool peer_loopback() override { return x0::peer().loopback; }
            int nviews() override { return x0::hub() ? x0::views() : 1; }
            bool part_v(int v, const char* tag, std::string& out) override { return x0::rpart(tag, out, v); }
            int peer_state_v(int v) override { return static_cast<int>(x0::peer(v).state); }
            int peer_sessions_v(int v) override { return x0::peer(v).sessions; }
            bool peer_loopback_v(int v) override { return x0::peer(v).loopback; }
            int view_pid(int v) override { return x0::view_pid(v); }
            bool world_held() override { return m01::quest_graph_held(); }
            bool look_ok() override { return true; }
            std::string look_put(const std::string& text) override { return x0::look_put(text); }

            bool read(const std::string& name, std::string& out) override { return x0::read_text(x0::bridge_file(W(name)), out); }
            bool write(const std::string& name, const std::string& text) override { return x0::write_text(x0::bridge_file(W(name)), text, true); }
            bool append(const std::string& name, const std::string& text) override { return x0::append_text(x0::bridge_file(W(name)), text); }
            bool exists(const std::string& name) override { return x0::file_exists(x0::bridge_file(W(name))); }
            bool remove(const std::string& name) override { return x0::remove_file(x0::bridge_file(W(name))); }
            bool rename(const std::string& from, const std::string& to) override
            {
                return MoveFileExW(x0::bridge_file(W(from)).c_str(), x0::bridge_file(W(to)).c_str(), MOVEFILE_COPY_ALLOWED) != 0;
            }
            bool data(const char* name, std::string& out) override
            {
                if (x0::data(name, out)) return true;
                return x0::read_text(x0::bridge_file(W(name)), out);
            }
            int64_t unix_time() override { return static_cast<int64_t>(std::time(nullptr)); }
            int rnd(int lo, int hi) override { return std::uniform_int_distribution<int>(lo, hi)(rng); }
            std::optional<double> game_secs() override
            {
                x0::Ref tm = x0::first_of(L"TimeManager");
                double v = 0;
                if (!tm || !x0::prop<double>(tm, "GameTimeSeconds", v)) return std::nullopt;
                return v;
            }

            Id pawn() override
            {
                x0::Ref p = x0::first_of(L"BP_Stalker2Character_C");
                return p ? p.address() : 0;
            }
            Id pawn_pc() override
            {
                Id a = pawn();
                if (a) return a;
                x0::Ref pc = x0::first_of(L"PlayerController");
                x0::Ref p;
                if (pc && x0::prop<x0::Ref>(pc, "Pawn", p) && p) return p.address();
                return 0;
            }
            bool pos(Id a, V3& out) override
            {
                x0::Vec3 v;
                x0::Ref r = R(a);
                if (!r || !x0::call_v(r, L"K2_GetActorLocation", v)) return false;
                out = {v.x, v.y, v.z};
                return true;
            }
            Id interacted(Id pw) override
            {
                x0::Ref r = R(pw);
                if (!r) return 0;
                x0::Ref o = x0::call_o(r, L"GetInteractedActor");
                return o ? o.address() : 0;
            }
            int count(Id obj, const std::string& sid) override
            {
                x0::Ref o = R(obj), med = mediator();
                if (!o || !med) return 0;
                x0::Call c(med, L"GetItemsCountInInventory");
                c.o(L"Obj", o).s(L"ItemName", W(sid));
                if (!c.run()) return 0;
                return static_cast<int>(c.ri());
            }
            bool give(const std::string& sid, int n, std::string& err) override
            {
                x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
                if (!rk)
                {
                    err = "CustomConsoleManagerRK ausente";
                    return false;
                }
                x0::Call c(rk, L"XCreateItemInInventoryByID");
                c.s(L"PrototypeID", W(sid)).i(L"ObjUID", 0).i(L"Count", n).f(L"Durability", 1.0f);
                if (!c.run())
                {
                    err = "XCreateItemInInventoryByID falhou";
                    return false;
                }
                return true;
            }
            bool aa_present() override { return x0::first_of(L"CustomConsoleManagerAA").valid(); }
            int aa(const char* fn, const std::string& sid, std::string& err) override
            {
                x0::Ref aa = x0::first_of(L"CustomConsoleManagerAA");
                if (!aa) return mgr_missing;
                x0::Call c(aa, W(fn).c_str());
                c.s(L"SID", W(sid));
                if (!c.run())
                {
                    err = std::string(fn) + " falhou";
                    return call_failed;
                }
                return call_ok;
            }
            int dk(const char* fn, const std::string& a, const std::string* b, int win, std::string& err) override
            {
                x0::Ref dk = x0::first_of(L"CustomConsoleManagerDK");
                if (!dk) return mgr_missing;
                std::wstring f = W(fn);
                x0::Call c(dk, f.c_str());
                if (b) c.s(L"QSID", W(a)).s(L"SSID", W(*b));
                else c.s(L"SID", W(a));
                if (win >= 0) c.b(L"bWin", win == 1);
                if (!c.run())
                {
                    err = std::string(fn) + " falhou";
                    return call_failed;
                }
                return call_ok;
            }
            int rk_bool(const std::string& var, bool v, std::string& err) override
            {
                x0::Ref rk = x0::first_of(L"CustomConsoleManagerRK");
                if (!rk) return mgr_missing;
                x0::Call c(rk, L"XSetGlobalBool");
                c.s(L"SID", W(var)).b(L"InValue", v);
                if (!c.run())
                {
                    err = "XSetGlobalBool falhou";
                    return call_failed;
                }
                return call_ok;
            }
            bool load_last_save(std::string& err) override
            {
                x0::Ref mp = x0::first_of(L"CustomConsoleManagerMP");
                if (!mp)
                {
                    err = "CustomConsoleManagerMP ausente";
                    return false;
                }
                s2m_ls_before_load();
                if (!x0::call0(mp, L"XLoadLastSave"))
                {
                    err = "XLoadLastSave falhou";
                    return false;
                }
                return true;
            }
            bool journal(int out[4]) override
            {
                x0::Ref med = mediator();
                if (!med) return false;
                const wchar_t* fns[4] = {L"GetActiveMainQuests", L"GetActiveSecondaryQuests", L"GetFinishedQuests", L"GetFailedQuests"};
                for (int i = 0; i < 4; ++i)
                {
                    x0::Call c(med, fns[i]);
                    if (!c.run()) return false;
                    out[i] = static_cast<int>(c.rarr().size());
                }
                return true;
            }
            Inv inv_snap(Id pw) override
            {
                Inv r;
                x0::Ref p = R(pw);
                if (!p) return r;
                for (auto& s : s2m_save::lootSids())
                {
                    int n = count(pw, s);
                    if (n > 0) r.items[s] = n;
                }
                r.money = money(pw);
                return r;
            }
            double money(Id pw) override
            {
                x0::Ref p = R(pw);
                if (!p) return 0;
                x0::Call c(p, L"GetMoney");
                if (!c.run()) return 0;
                return c.rf();
            }
            bool add_money(Id pw, int64_t d) override
            {
                x0::Ref p = R(pw);
                if (!p) return false;
                x0::Call c(p, L"ModifyMoney");
                c.i(L"Delta", d);
                return c.run();
            }

            bool qi_npcadd(const std::string& sid) override { return g_hooks.qi_npcadd && g_hooks.qi_npcadd(sid); }
            bool quest_muted(const std::string& q) override { return s2m_solo_quest_muted(q.c_str()); }
            std::optional<std::string> qr_npc(const std::string& sid) override
            {
                if (!g_hooks.qr_npc || !g_hooks.nat_guid_listed) return std::nullopt;
                return g_hooks.qr_npc(sid);
            }
            bool nat_guid_listed(const std::string& guid) override { return g_hooks.nat_guid_listed && g_hooks.nat_guid_listed(guid); }

            bool inv_open() override
            {
                x0::Ref w = x0::first_of(L"InventoryNew");
                return w && x0::call_b(w, L"IsVisible", false);
            }
            Id hovered() override
            {
                x0::Ref w = x0::first_of(L"InventoryNew");
                if (!w || !x0::call_b(w, L"IsVisible", false)) return 0;
                x0::Ref h;
                if (!x0::prop<x0::Ref>(w, "HoveredItem", h) || !h) return 0;
                return h.address();
            }
            bool inv_call(const char* fn, std::string& err) override
            {
                x0::Ref w = x0::first_of(L"InventoryNew");
                if (!w)
                {
                    err = "InventoryNew ausente";
                    return false;
                }
                if (!x0::call0(w, W(fn).c_str()))
                {
                    err = std::string(fn) + " falhou";
                    return false;
                }
                return true;
            }
            bool key_d() override { return x0::key_down('D'); }
            std::string bind_d() override
            {
                int id = x0::key(x0::Mod::quest, 'D', [] { key_d_pressed(); });
                return id >= 0 ? std::string("ok") : std::string("erro x0::key");
            }

            bool lead_mine() override { return s2m_lock::lead_mine(); }
            bool lead_dlg() override { return s2m_lock::lead_dlg(); }
            bool host_dlg_open() override { return s2m_lock::host_dlg_open(); }
            bool busy(std::string& why) override
            {
                BusyIn in;
                in.lead_dlg = s2m_lock::lead_dlg();
                in.host_dlg = s2m_lock::host_dlg_open();
                in.cine = x0::is_host() ? s2m_travel_cine_on() : s2m_travel_remote_cine();
                x0::Ref pc = x0::first_of(L"PC");
                bool cin = false;
                in.pc_cine = pc && x0::prop(pc, "bInCinematicSequence", cin) && cin;
                for (const x0::Ref& w : x0::all_of(L"W_DialogueView_C", 2))
                    in.views.emplace_back(x0::call_b(w, L"IsInViewport", false), x0::call_b(w, L"IsVisible", false));
                return busy_of(in, why);
            }
            bool trig_deny(const std::string& sid, const std::string& seq) override { return s2m_lock::trig_deny(sid, seq); }
            void trig_entered(const std::string& sid, double x, double y, double z) override { s2m_lock::trig_entered(sid, x, y, z); }
            void mq_seen(const std::string& sid) override { s2m_lock::mq_seen(sid); }
            bool mq_sid(const std::string& sid) override { return s2m_lock::mq_sid(sid); }
            void cj_cause(const std::string& sid) override { s2m_travel_cj_on_remote_cause(sid); }
            void cj_before(const std::string& sid) override { s2m_travel_cj_before_start(sid); }
            DeadRes qg_dead(Id pw, bool rebase) override
            {
                static s2m_pop::QGuard g;
                s2m_pop::QSense s;
                x0::Ref p = R(pw);
                s.pawn = pw;
                s.alive = x0::call_b(p, L"IsAlive", true);
                {
                    x0::Call c(p, L"GetHP");
                    if (c.run())
                    {
                        s.hasHp = true;
                        s.hp = c.rf();
                    }
                }
                s.ndDead = x0::iface().nd_dead && x0::iface().nd_dead();
                s.lifeDown = x0::iface().local_downed && x0::iface().local_downed();
                s2m_pop::QRes q = s2m_pop::qgDead(g, s, rebase);
                return DeadRes{q.skip, q.dropPending, q.rebase};
            }
            void qf_arm(const char* why) override { s2m_pop::qfArm(why); }
            bool n7_idle() override { return s2m_save::n7Idle(); }
            void n7_follow() override { s2m_save::n7Follow(); }
            bool n7_rw(const std::string& quest) override { return s2m_save::n7Rw(quest); }
            std::string n7_b() override { return s2m_save::n7B(); }
            bool ql_settled() override { return s2m_save::qlSettled(); }
            bool join_pending() override { return s2m_save::joinPending(); }
            void cp_flush(const char* why) override
            {
                if (g_hooks.cp_flush) g_hooks.cp_flush(why);
            }
            void campaign_rotated() override
            {
                if (g_hooks.campaign_rotated) g_hooks.campaign_rotated();
            }
            s2m_save::QState& sq() override { return s2m_save::qs(); }
            s2m_save::Counts& applied() override { return s2m_save::applied(); }
            int qb_force(const std::string& node, const std::string& q, const std::string& pin, bool fin, std::string& err) override { return s2m_qbranch::force(node, q, pin, fin, err); }
            bool dlg_has(const std::string& phrase) override { return s2m_qbranch::has(phrase); }
            bool dlg_pick(const std::string& phrase, std::string& err) override { return s2m_qbranch::pick(phrase, err); }
            int trig_enter(const std::string& guid, int mode, std::string& info) override
            {
                Id p = pawn_pc();
                int r = s2m_qbranch::trig_enter(guid, p, info, mode);
                Id q = pawn();
                if (r == -4 && q && q != p) r = s2m_qbranch::trig_enter(guid, q, info, mode);
                return r;
            }
        };
    }
}

X0_MODULE(quest)
{
    static s2m_quest::ProdEnv env;
    s2m_quest::bind(&env);
    x0::iface().is_quest_item = [](std::string_view sid) { return s2m_quest::is_quest_item(sid); };
    x0::every(x0::Mod::quest, "questRb", x0::Rate::t100, [] { s2m_quest::tick100_rb(); }, x0::ord::t100_rbTick);
    x0::every(x0::Mod::quest, "questTrig", x0::Rate::t100, [] { s2m_quest::tick100_trig(); }, x0::ord::t100_trigTick);
    x0::every(x0::Mod::quest, "questQp", x0::Rate::t100, [] { s2m_quest::tick100_qp(); }, x0::ord::t100_qpTick);
    x0::every(x0::Mod::quest, "questQi", x0::Rate::t100, [] { s2m_quest::tick100_qi(); }, x0::ord::t100_qiTick);
    x0::every(x0::Mod::quest, "questQb", x0::Rate::t100, [] { s2m_quest::tick100_qb(); });
    x0::every(x0::Mod::quest, "lockbehind", x0::Rate::t100, [] { s2m_lockbehind_tick(); });
    x0::every(x0::Mod::quest, "quest1000", x0::Rate::t1000, [] { s2m_quest::tick1000(); });
    S2FR(x0::fr::track("QB"));
    x0::own_tag(x0::Mod::quest, "QN", [] { return s2m_quest::out_qn(); });
    x0::own_tag(x0::Mod::quest, "W", [] { return s2m_quest::out_w(); });
    x0::own_tag(x0::Mod::quest, "J", [] { return s2m_quest::out_j(); });
    x0::own_tag(x0::Mod::quest, "QD", [] { return s2m_quest::out_qd(); });
    x0::own_tag(x0::Mod::quest, "QH", [] { return s2m_quest::out_qh(); });
    x0::own_tag(x0::Mod::quest, "S", [] { return s2m_quest::out_s(); });
    x0::own_tag_hub(x0::Mod::quest, "QD", [](int v) { return s2m_quest::out_qd_v(v); });
    x0::own_tag_hub(x0::Mod::quest, "S", [](int v) { return s2m_quest::out_s_v(v); });
    x0::own_tag(x0::Mod::quest, "B", [] { return s2m_quest::out_b(); });
    x0::own_tag(x0::Mod::quest, "QI", [] { return s2m_quest::out_qi(); });
    x0::own_tag(x0::Mod::quest, "QP", [] { return s2m_quest::out_qp(); });
    x0::own_tag(x0::Mod::quest, "QF", [] { return s2m_quest::out_qf(); });
    x0::own_tag(x0::Mod::quest, "QB", [] { return s2m_quest::out_qb(); });
    x0::cmd(x0::Mod::quest, "s2q", [](const std::vector<std::string>& args) {
        std::string line;
        for (auto& a : args)
        {
            if (!line.empty()) line += ' ';
            line += a;
        }
        s2m_quest::console(line);
        return std::string("ok");
    });
    x0::status(x0::Mod::quest, [] { return s2m_quest::status(); });
}
#endif
