#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "s2m_save.h"
#include "s2m_text.h"
#include "s2m_worn.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace s2m_save
{
    namespace
    {
        struct Row
        {
            int seq = 0;
            std::string guid;
            int tot = 0;
            std::string sig;
            std::string cid = "-";
            std::optional<std::string> wc;
            std::optional<double> clk;
            std::string wp = "-";
        };
        struct Sv
        {
            bool made = false;
            std::vector<Row> rows;
            int seq = 0;
            bool dirty = false;
            std::optional<std::map<std::string, std::string>> base;
            std::vector<std::string> names;
            std::optional<double> tickAt;
        };
        struct Snapshot
        {
            Counts items;
            double money = 0;
            Id addr = 0;
            double at = 0;
            bool skip = false;
            std::optional<std::set<std::string>> qt;
            std::optional<std::string> sxId;
            bool fresh = false;
            std::optional<s2m_worn::Worn> worn;
            bool wornDone = false;
            std::vector<std::pair<int, std::string>> reEq;
        };
        struct SvPend
        {
            double at = 0;
            Id addr = 0;
        };
        struct Sx
        {
            double at = 0;
            std::string cid;
            int kh = 0;
            int tries = 0;
            std::string s = "snap";
            std::optional<std::string> id;
            bool inst = false;
            std::optional<double> t;
            double logAt = 0;
            std::optional<std::string> s0;
            bool fresh = false;
        };
        struct Res
        {
            int give = 0, take = 0;
            long long money = 0;
        };
        struct Give
        {
            std::string sid;
            int n = 0;
        };
        struct EqDiff
        {
            std::string sid;
            int d = 0;
            bool mag = false;
        };
        struct Wp
        {
            std::string id, cid;
            int hs = 0, kh = -1;
            double at = 0;
            bool asked = false;
        };
        struct Early
        {
            int n = 0, kh = -1;
            std::string guid;
            double at = 0;
        };
        struct Wsp
        {
            std::string key, file, guid, cid;
            int kh = -1, hs = -1;
            int seq = 0;
            double at = 0;
            bool run = false;
        };
        struct N7
        {
            bool made = false;
            std::string stage = "idle";
            int hl = 0, hs = 0;
            std::map<std::string, double> rw;
            std::set<std::string> keep;
            std::optional<std::string> tag;
            bool pathInit = false;
            std::optional<std::string> path;
            std::optional<Id> addr;
            std::optional<double> pend;
            bool first = false;
            std::optional<SvPend> sv;
            std::optional<int> kh;
            bool sxLd = false;
            std::optional<std::string> sig;
            double pollAt = 0;
            std::optional<long long> g;
            std::optional<std::string> cid;
            std::optional<int> sess;
            bool shared = false;
            bool warned = false;
            std::optional<Snapshot> snap;
            bool qiReset = false;
            std::optional<double> heldAt;
            double t = 0;
            Res res;
            std::vector<Give> give;
            std::string next;
            int done = 0;
            std::optional<double> svPend;
            bool svGo = false;
            bool bootGo = false;
            bool dieGo = false;
            std::optional<std::string> goSeq;
            std::optional<int> hldSeen;
            std::optional<Early> early;
            bool again = false;
            int hldN = 0, hldKh = -1;
            std::string hldGuid = "none";
            std::optional<Sx> sx;
            std::optional<Wp> wp;
            std::optional<Wsp> wsp;
            std::string wspLast;
            std::string wspSeenFile;
            double wspSeenAt = 0;
            bool wspWarned = false;
            double hsvAt = -1;
            int hsvN = 0;
        };
        struct Join
        {
            std::optional<Id> addr;
            std::optional<std::string> want;
            std::optional<int> sess;
            std::optional<double> waitT;
            std::optional<int> tries;
            double untilT = 0;
            bool oldLeashWarned = false;
        };
        struct Kit
        {
            std::optional<Id> addr;
            double at = 0;
            bool done = false;
        };
        struct EqEntry
        {
            std::string kind;
            std::optional<std::string> ammo;
            std::optional<int> max;
        };
        struct EqTables
        {
            bool loaded = false;
            bool none = false;
            std::map<std::string, EqEntry> tab;
            std::vector<std::string> list;
            std::set<std::string> base;
        };

        Env* E = nullptr;
        QState Q;
        N7 N;
        Sv V;
        Join J;
        Kit KT;
        EqTables EQ;
        std::vector<std::string> LOOT;
        uint64_t pairSerial = 0;
        int errs = 0;
        const char* CSFMT = "#S2CA 1";

        std::string F(const char* fmt, ...)
        {
            char buf[2048];
            va_list ap;
            va_start(ap, fmt);
            int n = std::vsnprintf(buf, sizeof buf, fmt, ap);
            va_end(ap);
            if (n < 0) return {};
            if (n < (int)sizeof buf) return std::string(buf, (size_t)n);
            std::string big((size_t)n + 1, '\0');
            va_start(ap, fmt);
            std::vsnprintf(big.data(), big.size(), fmt, ap);
            va_end(ap);
            big.resize((size_t)n);
            return big;
        }
        void L(const std::string& s) { E->log(s); }
        double fl(const char* k, double def = 0) { return E->flag(k, def); }
        bool on(const char* k, double def = 0) { return fl(k, def) == 1; }
        bool client() { return !E->isHost(); }
        double now() { return E->now(); }
        std::string tb(bool b) { return b ? "true" : "false"; }
        std::string ts(const std::optional<std::string>& s) { return s ? *s : std::string("nil"); }
        std::string ti(const std::optional<int>& v) { return v ? std::to_string(*v) : std::string("nil"); }
        std::string sub8(const std::string& s) { return s.substr(0, 8); }
        bool isw(char c) { return std::isalnum((unsigned char)c) != 0; }
        bool isd(char c) { return c >= '0' && c <= '9'; }
        bool isx(char c) { return std::isxdigit((unsigned char)c) != 0; }

        std::string trimLine(std::string s)
        {
            while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
            return s;
        }
        std::vector<std::string> lines(const std::string& d)
        {
            std::vector<std::string> out;
            size_t p = 0;
            while (p < d.size())
            {
                size_t e = d.find('\n', p);
                std::string ln = d.substr(p, e == std::string::npos ? std::string::npos : e - p);
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                out.push_back(ln);
                if (e == std::string::npos) break;
                p = e + 1;
            }
            return out;
        }
        std::string join(const std::vector<std::string>& v, const char* sep, size_t first = 0, size_t last = (size_t)-1)
        {
            std::string o;
            for (size_t i = first; i < v.size() && i < last; ++i)
            {
                if (i > first) o += sep;
                o += v[i];
            }
            return o;
        }

        struct Cur
        {
            const std::string& s;
            size_t p = 0;
            bool digits(std::string* out = nullptr, bool neg = false)
            {
                size_t b = p;
                if (neg && p < s.size() && s[p] == '-') ++p;
                size_t d0 = p;
                while (p < s.size() && isd(s[p])) ++p;
                if (p == d0) { p = b; return false; }
                if (out) *out = s.substr(b, p - b);
                return true;
            }
            bool word(std::string* out = nullptr)
            {
                size_t b = p;
                while (p < s.size() && isw(s[p])) ++p;
                if (p == b) return false;
                if (out) *out = s.substr(b, p - b);
                return true;
            }
            bool ch(char c)
            {
                if (p < s.size() && s[p] == c) { ++p; return true; }
                return false;
            }
        };

        std::optional<std::string> bCid(const std::string& b)
        {
            Cur c{b};
            std::string w;
            if (c.digits() && c.ch(',') && c.digits() && c.ch(',') && c.word(&w)) return w;
            return std::nullopt;
        }
        std::optional<long long> bHn(const std::string& b)
        {
            Cur c{b};
            std::string w;
            if (c.digits() && c.ch(',') && c.digits(&w)) return std::atoll(w.c_str());
            return std::nullopt;
        }
        std::optional<std::string> part(const char* tag)
        {
            std::string v;
            if (E->part(tag, v)) return v;
            return std::nullopt;
        }

        Id pawn() { return E->localPawn(); }
        bool valid(Id p) { return p != 0; }

        struct Ev
        {
            std::string kind;
            std::optional<std::string> a, b, c, sid;
        };
        Ev parseEv(const std::string& e)
        {
            std::vector<std::string> f;
            size_t p = 0;
            while (p < e.size())
            {
                size_t q = e.find(',', p);
                if (q == std::string::npos) q = e.size();
                if (q > p) f.push_back(e.substr(p, q - p));
                p = q + 1;
            }
            auto at = [&](size_t i) -> std::optional<std::string> { return i < f.size() ? std::optional<std::string>(f[i]) : std::nullopt; };
            Ev v;
            v.kind = f.empty() ? std::string() : f[0];
            if (v.kind == "N" || v.kind == "W" || v.kind == "X" || v.kind == "R" || v.kind == "V" || v.kind == "H") { v.sid = at(1); return v; }
            v.a = at(1); v.b = at(2); v.c = at(3); v.sid = at(4);
            return v;
        }
        QuestResult apply(const Ev& v, bool live)
        {
            if (v.kind.empty()) { QuestResult r; r.ok = false; r.why = "evento vazio"; return r; }
            return E->questApply(v.kind, v.a, v.b, v.c, v.sid, live);
        }
        std::string trv(const QuestResult& r) { return r.r == 1 ? "true" : (r.r == 0 ? "false" : "nil"); }

        bool svInfo(std::string& dir, std::string& stamp, std::string& plat, std::string& newest)
        {
            if (!E->saveInfo(dir, stamp, plat, newest)) return false;
            return !dir.empty();
        }
        std::optional<std::string> stampNow()
        {
            std::string d, st, pl, nw;
            if (!svInfo(d, st, pl, nw)) return std::nullopt;
            return st;
        }

        std::pair<Counts, double> invSnap(Id pw)
        {
            Counts t;
            for (auto& s : LOOT)
            {
                int n = 0;
                if (E->itemCount(pw, s, n) && n > 0) t[s] = n;
            }
            double m = 0;
            if (!E->money(pw, m)) m = 0;
            return {t, m};
        }
        int sum(const Counts& c)
        {
            int n = 0;
            for (auto& kv : c) n += kv.second;
            return n;
        }

        N7& n7St()
        {
            N.made = true;
            return N;
        }

        std::optional<std::string> n7Path()
        {
            std::string d, st, pl, nw;
            if (svInfo(d, st, pl, nw)) return d + (pl.empty() ? std::string("STEAM") : pl) + "\\" + E->saveFolder() + "\\CampaignsSave.sav";
            return std::nullopt;
        }
        std::string n7Tag(const std::optional<std::string>& p)
        {
            if (!p) return "0";
            std::string s = *p;
            for (auto& c : s)
            {
                c = (char)std::tolower((unsigned char)c);
                if (c == '/') c = '\\';
            }
            long long h = 5381;
            for (unsigned char c : s) h = (h * 33 + c) % 2147483647LL;
            return F("%llx", (unsigned long long)h);
        }
        std::optional<std::string> n7Sig() { return stampNow(); }

        bool qtOn() { return (on("qitem") || fl("lead") == 1) && E->questTableOn(); }

        void cpTables()
        {
            if (EQ.loaded) return;
            EQ.loaded = true;
            std::string d;
            if (E->readText("s2u_equip.tsv", d))
            {
                for (auto& ln : lines(d))
                {
                    std::vector<std::string> c;
                    size_t p = 0;
                    bool bad = false;
                    for (int i = 0; i < 4 && !bad; ++i)
                    {
                        size_t b = p;
                        while (p < ln.size() && !std::isspace((unsigned char)ln[p])) ++p;
                        c.push_back(ln.substr(b, p - b));
                        if (i < 3)
                        {
                            if (p >= ln.size() || ln[p] != '\t') bad = true;
                            else ++p;
                        }
                    }
                    if (bad || c.size() < 4 || c[0].empty()) continue;
                    EqEntry e;
                    e.kind = c[1];
                    if (!c[2].empty()) e.ammo = c[2];
                    if (!c[3].empty()) { char* end = nullptr; long v = std::strtol(c[3].c_str(), &end, 10); if (end && *end == 0) e.max = (int)v; }
                    EQ.tab[c[0]] = e;
                    EQ.list.push_back(c[0]);
                }
            }
            std::string b;
            if (E->readText("s2u_basekit.txt", b))
            {
                for (auto& ln : lines(b))
                {
                    size_t i = 0;
                    while (i < ln.size() && std::isspace((unsigned char)ln[i])) ++i;
                    size_t j = i;
                    while (j < ln.size() && (isw(ln[j]) || ln[j] == '_')) ++j;
                    if (j > i) EQ.base.insert(ln.substr(i, j - i));
                }
            }
            L(F("NOLA persist: %d itens de slot em s2u_equip.tsv, kit base vestido: %d", (int)EQ.list.size(), (int)EQ.base.size()));
        }
        const std::map<std::string, EqEntry>* n7EqTab()
        {
            if (fl("n7eq") != 1) return nullptr;
            if (!EQ.loaded)
            {
                cpTables();
                if (EQ.list.empty())
                {
                    EQ.none = true;
                    L("NOLA n7: n7eq=1 mas s2u_equip.tsv ausente ou vazio: itens de slot julgados como antes");
                }
            }
            if (EQ.none) return nullptr;
            return &EQ.tab;
        }
        void n7EqLog(const std::vector<EqDiff>& eq, const char* why)
        {
            std::vector<std::string> r;
            for (auto& x : eq) r.push_back(x.sid + (x.d > 0 ? "+" : "") + std::to_string(x.d) + (x.mag ? "(pente)" : ""));
            L(F("NOLA n7: itens de slot %s (n7eq=1; contagem 0 = vestido ou ausente): [%s]", why, join(r, " ").c_str()));
        }

        using QT = std::function<bool(const std::string&)>;
        QT globalQT()
        {
            if (!qtOn()) return {};
            return [](const std::string& s) { return E->isQuestItem(s); };
        }
        QT snapQT(const Snapshot& S)
        {
            if (S.qt)
            {
                auto set = *S.qt;
                return [set](const std::string& s) { return set.count(s) > 0; };
            }
            return globalQT();
        }

        void n7Diff(const Counts& want, const Counts& cur, const QT& T, std::vector<Give>& give, std::vector<Give>& take, std::vector<EqDiff>& eq)
        {
            auto get = [](const Counts& c, const std::string& s) { auto it = c.find(s); return it == c.end() ? 0 : it->second; };
            const auto* Et = n7EqTab();
            std::map<std::string, long long> mag;
            if (Et)
            {
                for (auto& s : LOOT)
                {
                    auto it = Et->find(s);
                    if (it != Et->end() && it->second.ammo && it->second.max && !(T && T(s)))
                    {
                        int d = get(want, s) - get(cur, s);
                        if (d != 0) mag[*it->second.ammo] += (long long)d * *it->second.max;
                    }
                }
            }
            for (auto& s : LOOT)
            {
                if (T && T(s)) continue;
                long long d = get(want, s) - get(cur, s);
                if (d != 0 && Et && Et->count(s)) { eq.push_back({s, (int)d, false}); continue; }
                long long m = mag.count(s) ? mag[s] : 0;
                if ((m > 0 && d > 0) || (m < 0 && d < 0))
                {
                    long long k = d > 0 ? std::min(d, m) : std::max(d, m);
                    d -= k;
                    eq.push_back({s, (int)k, true});
                }
                if (d > 0) give.push_back({s, (int)d});
                else if (d < 0) take.push_back({s, (int)-d});
            }
        }

        Counts n7Mag(Id pw, const std::vector<Give>& take)
        {
            Counts m;
            if (fl("n7mag", 1) != 1) return m;
            for (auto& x : take)
            {
                int n = 0;
                if (E->ammoLoaded(pw, x.sid, n) && n > 0) m[x.sid] = n;
            }
            return m;
        }
        void n7Cap(std::vector<Give>& take, const Counts& cur, const Counts& mag, std::vector<Give>& stuck)
        {
            for (auto it = take.begin(); it != take.end();)
            {
                auto f = mag.find(it->sid);
                if (f == mag.end()) { ++it; continue; }
                auto c = cur.find(it->sid);
                int loose = std::max(0, (c == cur.end() ? 0 : c->second) - f->second);
                if (it->n > loose) { stuck.push_back({it->sid, it->n - loose}); it->n = loose; }
                if (it->n <= 0) it = take.erase(it);
                else ++it;
            }
        }
        std::string n7StuckTxt(const std::vector<Give>& stuck)
        {
            std::vector<std::string> v;
            for (auto& x : stuck) v.push_back(x.sid + " " + std::to_string(x.n));
            return join(v, ", ");
        }

        Sv& svSt();
        std::optional<std::string> svDir()
        {
            auto p = n7Path();
            if (!p) return std::nullopt;
            size_t e = p->find_last_of("\\/");
            if (e == std::string::npos || e + 1 >= p->size()) return std::nullopt;
            return p->substr(0, e) + "\\Data\\";
        }
        std::optional<std::string> svSig(const std::string& path)
        {
            long long size = 0;
            std::string tail;
            if (!E->statTail(path, size, tail)) return std::nullopt;
            long long h = 0;
            for (unsigned char c : tail) h = (h * 31 + c) % 2147483647LL;
            return std::to_string(size) + ":" + std::to_string(h);
        }
        void svLoad(Sv& v)
        {
            std::string d;
            if (!E->readText("n7sv_pares.txt", d) || d.compare(0, 7, "#N7SV 1") != 0) return;
            for (auto& ln : lines(d))
            {
                if (ln.empty()) continue;
                std::vector<std::string> t;
                std::string s = ln + "\t";
                size_t p = 0;
                while (true)
                {
                    size_t q = s.find('\t', p);
                    if (q == std::string::npos) break;
                    t.push_back(s.substr(p, q - p));
                    p = q + 1;
                }
                auto num = [](const std::string& x, long long& out) {
                    if (x.empty()) return false;
                    char* e = nullptr;
                    out = std::strtoll(x.c_str(), &e, 10);
                    return e && *e == 0;
                };
                long long sq = 0, tot = 0;
                if (!t.empty() && t[0] == "R" && t.size() >= 6 && num(t[1], sq) && num(t[3], tot))
                {
                    Row r;
                    r.seq = (int)sq; r.guid = t[2]; r.tot = (int)tot; r.sig = t[4]; r.cid = t[5];
                    if (t.size() > 6 && !t[6].empty() && t[6] != "-") r.wc = t[6];
                    v.rows.push_back(r);
                    v.seq = std::max(v.seq, (int)sq);
                }
                else if (!t.empty() && t[0] == "B" && t.size() >= 3)
                {
                    if (!v.base) v.base.emplace();
                    (*v.base)[t[1]] = t[2];
                }
                else if (!t.empty() && t[0] == "D") v.dirty = t.size() > 1 && t[1] == "1";
            }
        }
        Sv& svSt()
        {
            if (!V.made) { V = Sv{}; V.made = true; svLoad(V); }
            return V;
        }
        void svSave(Sv& v)
        {
            std::string o = "#N7SV 1";
            for (auto& r : v.rows) o += "\n" + F("R\t%d\t%s\t%d\t%s\t%s\t%s", r.seq, r.guid.c_str(), r.tot, r.sig.c_str(), r.cid.empty() ? "-" : r.cid.c_str(), r.wc ? r.wc->c_str() : "-");
            if (v.base) for (auto& kv : *v.base) o += "\nB\t" + kv.first + "\t" + kv.second;
            o += std::string("\nD\t") + (v.dirty ? "1" : "0");
            E->writeBin("n7sv_pares.txt", o + "\n");
        }
        template <class P> int svDrop(Sv& v, P pred)
        {
            std::vector<Row> keep;
            int n = 0;
            for (auto& r : v.rows) { if (pred(r)) ++n; else keep.push_back(r); }
            v.rows = keep;
            return n;
        }
        std::vector<std::string>& svNames(Sv& v)
        {
            std::string l = E->saveList();
            std::vector<std::string> t;
            size_t p = 0;
            while (p < l.size())
            {
                while (p < l.size() && !isx(l[p])) ++p;
                size_t b = p;
                while (p < l.size() && isx(l[p])) ++p;
                if (p - b == 32)
                {
                    std::string g = l.substr(b, 32);
                    for (auto& c : g) c = (char)std::toupper((unsigned char)c);
                    t.push_back(g);
                }
            }
            v.names = t;
            return v.names;
        }
        std::map<std::string, std::string> svSigs(Sv& v)
        {
            std::map<std::string, std::string> t;
            auto dir = svDir();
            if (!dir) return t;
            for (auto& g : svNames(v))
            {
                auto s = svSig(*dir + g + ".sav");
                t[g] = s ? *s : std::string("-");
            }
            return t;
        }
        std::string lastPos()
        {
            Id pw = pawn();
            V3 l;
            if (valid(pw) && E->location(pw, l)) return F(" em %.0f,%.0f,%.0f", l.x, l.y, l.z);
            return "";
        }
        void svRec(int n, const std::string& wp = "-")
        {
            if (fl("n7sv") != 1 || !client()) return;
            Sv& v = svSt();
            std::map<std::string, std::string> base = v.base ? *v.base : std::map<std::string, std::string>{};
            auto cur = svSigs(v);
            std::vector<std::string> ch;
            for (auto& kv : cur)
            {
                auto it = base.find(kv.first);
                if (kv.second != "-" && (it == base.end() || kv.second != it->second)) ch.push_back(kv.first);
            }
            if (ch.empty())
            {
                std::set<std::string> old;
                for (auto& kv : cur) old.insert(kv.first);
                svNames(v);
                cur = svSigs(v);
                for (auto& kv : cur) if (!old.count(kv.first) && kv.second != "-") ch.push_back(kv.first);
            }
            for (auto& g : ch) svDrop(v, [&](const Row& r) { return r.guid == g; });
            if (ch.size() == 1)
            {
                const std::string g = ch[0];
                v.seq += 1;
                Row r;
                r.seq = v.seq; r.guid = g; r.tot = n; r.sig = cur[g]; r.cid = Q.apCid ? *Q.apCid : "-"; r.clk = now(); r.wc = Q.apW; r.wp = wp.empty() ? "-" : wp;
                v.rows.push_back(r);
                std::string ap;
                if (!E->readText("client_applied.txt", ap)) ap.clear();
                E->writeBin("n7ap_" + g + ".txt", ap);
                v.dirty = false;
                std::string pos = lastPos();
                L(F("NOLA n7sv: save do cliente %s guarda %d passos do host (par #%d, %d guardados)%s", sub8(g).c_str(), n, v.seq, (int)v.rows.size(), pos.c_str()));
            }
            else
            {
                v.dirty = true;
                L(F("NOLA n7sv: save do cliente nao identificado (%d arquivos mudaram): o mais novo fica desconhecido ate o proximo save", (int)ch.size()));
            }
            v.base = cur;
            svSave(v);
        }

        void n7FollowImpl();

        void svTick()
        {
            if (fl("n7sv") != 1 || !client() || fl("csave") != 1) return;
            Sv& v = svSt();
            double t = now();
            if (N.svPend)
            {
                if (Q.csWait && t < *N.svPend + 15) return;
                N.svPend.reset();
                N.svGo = true;
                L("NOLA n7sv: o save em confirmacao acabou (ou 15 s): sigo o host agora");
                n7FollowImpl();
                N.svGo = false;
                return;
            }
            if (Q.csWait || N.stage != "idle" || (v.tickAt && t < *v.tickAt)) return;
            if (N.wsp && N.wsp->run) return;
            v.tickAt = t + 10;
            auto cur = svSigs(v);
            std::vector<std::string> ch;
            if (v.base)
                for (auto& kv : cur)
                {
                    auto it = v.base->find(kv.first);
                    if (it != v.base->end() && kv.second != it->second) ch.push_back(kv.first);
                }
            for (auto& g : ch) svDrop(v, [&](const Row& r) { return r.guid == g; });
            if (!ch.empty())
            {
                v.dirty = true;
                L(F("NOLA n7sv: %d save(s) do cliente mudaram fora do csave (%s): pares descartados; o mais novo fica desconhecido ate o proximo save do csave", (int)ch.size(), sub8(ch[0]).c_str()));
            }
            bool first = !v.base.has_value();
            v.base = cur;
            if (first || !ch.empty()) svSave(v);
        }

        const Row* newestRow(const Sv& v)
        {
            const Row* nw = nullptr;
            for (auto& r : v.rows) if (!nw || r.seq > nw->seq) nw = &r;
            return nw;
        }

        std::optional<Row> svPair(const std::optional<int>& K, const std::optional<std::string>& cid)
        {
            Sv& v = svSt();
            auto dir = svDir();
            if (!dir || !K || *K < 0) { L("NOLA n7sv: sem kh do host ou sem pasta de saves: XLoadLastSave normal"); return std::nullopt; }
            int k = *K;
            const Row* nwp = newestRow(v);
            std::optional<Row> newest;
            if (nwp) newest = *nwp;
            std::optional<Row> pick;
            for (auto& r : v.rows)
            {
                bool cidOk = r.cid == "-" || !cid || r.cid == *cid;
                bool wcOk = fl("cswc", 1) != 1 || (r.wc && (!cid || *r.wc == *cid));
                if (r.tot <= k && cidOk && wcOk && svSig(*dir + r.guid + ".sav") == std::optional<std::string>(r.sig))
                {
                    if (!pick || r.tot > pick->tot || (r.tot == pick->tot && r.seq > pick->seq)) pick = r;
                }
            }
            int gone = svDrop(v, [&](const Row& r) { return r.tot > k; });
            if (v.dirty || !newest || svSig(*dir + newest->guid + ".sav") != std::optional<std::string>(newest->sig))
            {
                L(F("NOLA n7sv: host voltou ao save com %d passos, mas o save mais novo do cliente e desconhecido (%s): XLoadLastSave normal (antigo)", k,
                    v.dirty ? "mudou fora do csave" : "sem registro"));
                svSave(v);
                return std::nullopt;
            }
            if (!pick)
            {
                L(F("NOLA n7sv: host voltou ao save com %d passos e nenhum save do cliente tem <= %d (o mais novo tem %d): XLoadLastSave normal (antigo)", k, k, newest->tot));
                svSave(v);
                return std::nullopt;
            }
            if (pick->guid == newest->guid)
            {
                L(F("NOLA n7sv: host voltou ao save com %d passos: o save mais novo do cliente %s (%d passos) ja e o pareado", k, sub8(pick->guid).c_str(), pick->tot));
                svSave(v);
                return pick;
            }
            std::string ap, data, old;
            bool okAp = E->readText("n7ap_" + pick->guid + ".txt", ap);
            bool okD = E->readAbs(*dir + pick->guid + ".sav", data, true);
            bool okO = E->readAbs(*dir + newest->guid + ".sav", old, true);
            if (!(okAp && okD && okO && !ap.empty()))
            {
                L("NOLA n7sv: copia do save pareado " + sub8(pick->guid) + " ou do seu registro ilegivel: XLoadLastSave normal (antigo)");
                svSave(v);
                return std::nullopt;
            }
            E->writeBin("n7sv_backup.sav", old);
            E->writeBin("n7sv_backup.txt", F("%s.sav, %d passos do host, copiado por cima em %s pelo save %s (%d passos)\n", newest->guid.c_str(), newest->tot, E->dateText().c_str(), pick->guid.c_str(), pick->tot));
            if (!E->writeAbs(*dir + newest->guid + ".sav", data) || svSig(*dir + newest->guid + ".sav") != std::optional<std::string>(pick->sig))
            {
                E->writeAbs(*dir + newest->guid + ".sav", old);
                L("NOLA n7sv: a copia por cima de " + sub8(newest->guid) + " nao conferiu: original devolvido; XLoadLastSave normal (antigo)");
                svSave(v);
                return std::nullopt;
            }
            E->writeBin("n7ap_" + newest->guid + ".txt", ap);
            E->writeBin("client_applied.txt", ap);
            int was = newest->tot;
            for (auto& r : v.rows)
                if (r.seq == newest->seq && r.guid == newest->guid)
                {
                    r.tot = pick->tot; r.sig = pick->sig; r.cid = pick->cid; r.clk = pick->clk;
                }
            if (!v.base) v.base.emplace();
            (*v.base)[newest->guid] = pick->sig;
            svSave(v);
            L(F("NOLA n7sv: host voltou ao save com %d passos -> save pareado do cliente %s (%d passos, par #%d) copiado por cima do mais novo %s (%d passos; copia em n7sv_backup.sav; %d pares do ramo abandonado largados); XLoadLastSave carrega o pareado",
                k, sub8(pick->guid).c_str(), pick->tot, pick->seq, sub8(newest->guid).c_str(), was, gone));
            return pick;
        }

        bool svPre()
        {
            if (fl("n7sv") != 1 || !client()) return false;
            if (N.svGo) N.svGo = false;
            else if (Q.csWait)
            {
                if (!N.svPend) { N.svPend = now(); L("NOLA n7sv: host voltou no tempo com um save do cliente em confirmacao: espero ate 15 s para saber qual arquivo e o mais novo"); }
                return true;
            }
            N.svPend.reset();
            auto b = part("B");
            auto r = svPair(N.kh, bCid(b ? *b : std::string()));
            if (r) Q.n7SavedAt = r->clk ? *r->clk : -1e9;
            return false;
        }

        std::optional<Row> svFind(int k, const std::string& cid)
        {
            Sv& v = svSt();
            auto dir = svDir();
            if (!dir || v.dirty) return std::nullopt;
            const Row* nw = newestRow(v);
            if (!nw || svSig(*dir + nw->guid + ".sav") != std::optional<std::string>(nw->sig)) return std::nullopt;
            std::optional<Row> pick;
            for (auto& r : v.rows)
            {
                bool wcOk = fl("cswc", 1) != 1 || (r.wc && *r.wc == cid);
                if (r.tot <= k && r.cid == cid && wcOk && svSig(*dir + r.guid + ".sav") == std::optional<std::string>(r.sig) &&
                    (!pick || r.tot > pick->tot || (r.tot == pick->tot && r.seq > pick->seq)))
                    pick = r;
            }
            if (pick && pick->guid == nw->guid) return std::nullopt;
            return pick;
        }

        void beginRb(Id addr)
        {
            RbWait w;
            w.at = now();
            w.addr = addr;
            w.n = -1;
            Q.rbWait = w;
            Q.rbQueue = std::vector<std::string>{};
        }

        void n7FollowImpl()
        {
            if (svPre()) return;
            Id pw = pawn();
            if (!valid(pw)) { L("NOLA n7: sem pawn: nao sigo o host"); return; }
            auto inv = invSnap(pw);
            Counts items = inv.first;
            int nq = 0;
            QT T = globalQT();
            if (T)
                for (auto it = items.begin(); it != items.end();)
                {
                    if (T(it->first)) { it = items.erase(it); ++nq; }
                    else ++it;
                }
            Snapshot S;
            S.items = items; S.money = inv.second; S.addr = pw; S.at = now();
            N.snap = S;
            N.keep.clear();
            double since = Q.n7SavedAt ? *Q.n7SavedAt : -1e9;
            for (auto& kv : N.rw) if (kv.second > since) N.keep.insert(kv.first);
            int ni = sum(items);
            Q.cuDone = false;
            if (fl("qnohold") == 1) Q.seen.clear();
            Q.csRb = true;
            Q.n7Pair = Pair{};
            beginRb(S.addr);
            std::string e;
            bool ok = E->loadLastSave(e);
            L(F("NOLA n7: host voltou no tempo -> retrato do cliente: %d itens, dinheiro %d%s; XLoadLastSave ok=%s %s", ni, (int)N.snap->money,
                nq > 0 ? F(" (%d itens de missao ficam com as regras qitem/lead)", nq).c_str() : "", tb(ok).c_str(), ok ? "" : e.c_str()));
            if (!ok) { E->rbDone("n7: XLoadLastSave falhou"); N.snap.reset(); return; }
            N.stage = "load";
            N.qiReset = false;
        }

        void sxAbort(const std::string& why)
        {
            E->screenClear("s2m_save_join");
            if (N.sx)
            {
                E->sxWant("");
                if (N.sx->id && !N.sx->inst) E->sxMark(*N.sx->id, "dropped");
            }
            else E->sxWant("");
            L("NOLA sxjoin: entrada cancelada (" + why + "): o cliente fica no proprio mundo; o catch-up segue como antes");
            N.sx.reset();
            N.stage = "idle";
        }

        std::set<std::string> tokSet(const std::string& s)
        {
            std::set<std::string> r;
            size_t p = 0;
            auto ok = [](char c) { return isw(c) || c == '_' || c == '-' || c == '.'; };
            while (p < s.size())
            {
                while (p < s.size() && !ok(s[p])) ++p;
                size_t b = p;
                while (p < s.size() && ok(s[p])) ++p;
                if (p > b) r.insert(s.substr(b, p - b));
            }
            return r;
        }
        Counts itemPairs(const std::string& s)
        {
            Counts t;
            auto ok = [](char c) { return isw(c) || c == '_' || c == '-' || c == '.'; };
            size_t p = 0;
            while (p < s.size())
            {
                size_t b = p;
                while (p < s.size() && ok(s[p])) ++p;
                if (p > b && p < s.size() && s[p] == '=')
                {
                    std::string k = s.substr(b, p - b);
                    size_t q = p + 1, d0 = q;
                    while (q < s.size() && isd(s[q])) ++q;
                    if (q > d0) { t[k] = std::atoi(s.substr(d0, q - d0).c_str()); p = q; continue; }
                }
                if (p == b) ++p;
            }
            return t;
        }

        std::string wallId()
        {
            return std::to_string((long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
        }

        bool sxFresh(const std::string& id)
        {
            Id pw = pawn();
            if (!valid(pw)) { L("NOLA sxjoin: entrada " + id + " sem pawn: nada carregado"); return false; }
            Snapshot S;
            S.addr = pw; S.at = now(); S.fresh = true;
            N.snap = S;
            Q.cuDone = false;
            Q.n7Pair = Pair{};
            beginRb(S.addr);
            N.bootGo = true;
            std::string e;
            bool ok = E->loadLastSave(e);
            L("NOLA sxjoin: XLoadLastSave do save do host instalado (entrada " + id + ", sem retrato) ok=" + tb(ok) + (ok ? "" : " " + e));
            if (!ok) { E->rbDone("sxjoin: XLoadLastSave falhou"); N.bootGo = false; N.snap.reset(); return false; }
            N.stage = "load";
            N.qiReset = false;
            return true;
        }

        bool sxRestore(const std::string& id, bool load)
        {
            Id pw = pawn();
            std::string sid, items, qs, hand, w;
            double money = 0;
            E->sxGet(sid, money, items, qs, hand, w);
            if (!valid(pw) || sid != id) { L("NOLA sxjoin: retrato " + id + " ilegivel ou sem pawn: nada restaurado"); return false; }
            Snapshot S;
            S.items = itemPairs(items);
            S.qt = tokSet(qs);
            S.money = money; S.addr = pw; S.at = now(); S.sxId = id;
            std::string ws;
            if (fl("sxworn", 1) == 1 && E->sxWorn(ws)) S.worn = s2m_worn::parse(ws);
            N.snap = S;
            if (!load)
            {
                N.stage = "settle";
                N.t = now() + fl("n7wait", 5);
                L("NOLA sxjoin: o save do host ja esta carregado e o retrato " + id + " nao foi devolvido: devolvo agora");
                return true;
            }
            Q.cuDone = false;
            Q.n7Pair = Pair{};
            beginRb(S.addr);
            N.bootGo = true;
            std::string e;
            bool ok = E->loadLastSave(e);
            L("NOLA sxjoin: XLoadLastSave do save do host instalado ok=" + tb(ok) + (ok ? "" : " " + e));
            if (!ok) { E->rbDone("sxjoin: XLoadLastSave falhou"); N.bootGo = false; N.snap.reset(); return false; }
            N.stage = "load";
            N.qiReset = false;
            return true;
        }

        bool isOther(const BootRec& R, const std::string& cid)
        {
            return (R.cid && *R.cid != cid) || (!R.cid && R.n > 0) || (fl("cswc", 1) == 1 && R.wc != std::optional<std::string>(cid));
        }

        bool sxBoot(const std::string& cid, int kh, const BootRec& R)
        {
            if (fl("sxjoin") != 1 || !client()) return false;
            bool other = isOther(R, cid);
            bool mism = other || R.n > kh || !R.cid;
            std::string id, items, qs, hand, w;
            double money = 0;
            std::string st = E->sxGet(id, money, items, qs, hand, w);
            if (st == "installed" && !mism)
            {
                if (w == cid || fl("sxscope", 1) != 1) return sxRestore(id, false);
                E->sxMark(id, "dropped");
                L("NOLA sxjoin: retrato " + id + " instalado e do mundo " + (w.empty() ? std::string("desconhecido") : w) + " e o host esta na campanha " + cid + ": largado sem restaurar");
                return false;
            }
            if (!mism) return false;
            if (N.shared && fl("n7shared") != 1) { L("NOLA sxjoin: mesma pasta de saves do host (D13): sem entrada"); return false; }
            if (fl("n7sv") == 1 && fl("qrollback") == 1 && svFind(kh, cid)) return false;
            Sx x;
            x.at = now(); x.cid = cid; x.kh = kh; x.fresh = other && fl("sxscope", 1) == 1;
            N.sx = x;
            N.stage = "sx";
            std::string how = x.fresh ? "o mundo carregado e de outra campanha (" + ts(R.wc) + "): os itens dele NAO vao junto, o cliente comeca com o jogador do save do host"
                                    : std::string("os itens do cliente vao junto");
            L(F("NOLA sxjoin: o save carregado do cliente nao e o mundo do host (campanha %s, %d passos; host %s, %d) e nao ha save pareado: pego o save do host%s", ts(R.cid).c_str(), R.n, cid.c_str(), kh, fl("sxscope", 1) == 1 ? ("; " + how).c_str() : ""));
            return true;
        }

        void sxTick()
        {
            if (!N.sx) { N.stage = "idle"; return; }
            Sx& X = *N.sx;
            double t = now();
            if (X.s == "snap")
            {
                Id pw = pawn();
                if (!valid(pw)) return;
                if (X.fresh)
                {
                    std::string oid, items, qs, hand, w;
                    double money = 0;
                    std::string st = E->sxGet(oid, money, items, qs, hand, w);
                    if ((st == "taken" || st == "installed") && !oid.empty())
                    {
                        E->sxMark(oid, "dropped");
                        L("NOLA sxjoin: retrato " + oid + " (" + st + ", mundo " + (w.empty() ? std::string("desconhecido") : w) + ") largado: nao e da campanha " + X.cid);
                    }
                    X.id = wallId();
                    X.inst = false;
                    L("NOLA sxjoin: entrada " + *X.id + " na campanha " + X.cid + " sem retrato");
                    E->sxWant(*X.id);
                    X.s = "want";
                    X.t = t;
                    return;
                }
                std::string sids = join(LOOT, ",");
                std::string id, hand, err;
                int n = 0;
                double money = 0;
                bool reused = false;
                if (!E->sxSnap(pw, sids, fl("sxscope", 1) == 1 ? X.cid : std::string(), id, n, money, hand, reused, err)) { sxAbort("retrato: " + err); return; }
                X.id = id;
                X.inst = reused;
                L(F("NOLA sxjoin: retrato %s%s: %d itens, dinheiro %d, na mao %s", id.c_str(), reused ? " (o mesmo da entrada interrompida)" : "", n, (int)money, hand.c_str()));
                E->sxWant(id);
                X.s = "want";
                X.t = t;
                return;
            }
            if (X.s == "want")
            {
                std::string d;
                std::string st = E->sxIn(*X.id, d);
                if (st == "ready") { L("NOLA sxjoin: save do host recebido (" + d + ")"); E->screenClear("s2m_save_join"); X.s = "save"; X.t.reset(); return; }
                double xt = X.t ? *X.t : t;
                E->screen("s2m_save_join", F(nola_text::tx(nola_text::T::save_following).c_str(), t - xt), 0);
                if (t >= X.logAt) { X.logAt = t + 10; L(F("NOLA sxjoin: esperando o save do host (%s %s, %.0f s)", st.c_str(), d.c_str(), t - xt)); }
                double w = fl("sxwait", 180);
                if (t - xt > w) { sxAbort(F("o save do host nao chegou em %s s", E->flagText("sxwait", "180").c_str())); return; }
                return;
            }
            if (X.s == "save")
            {
                auto stamp = stampNow();
                if (!X.t)
                {
                    if (fl("sxinworld", 1) == 1 && !valid(pawn())) { sxAbort("o cliente saiu do mundo antes do save automatico"); return; }
                    X.tries += 1;
                    if (X.tries > (int)fl("sxtries", 3)) { sxAbort(F("o jogo recusou o save automatico %s vezes", E->flagText("sxtries", "3").c_str())); return; }
                    X.s0 = stamp;
                    X.t = t;
                    std::string e;
                    bool ok = E->forcedAutoSave(e);
                    L(F("NOLA sxjoin: XTriggerForcedAutoSave (entrada nova para o mundo do host, tentativa %d) ok=%s%s", X.tries, tb(ok).c_str(), ok ? "" : (" " + e).c_str()));
                    return;
                }
                if (stamp && stamp != X.s0) { X.s = "wait"; X.t = t; return; }
                if (t - *X.t > fl("sxsave", 25)) X.t.reset();
                return;
            }
            if (X.s == "wait")
            {
                if (t - *X.t >= 3) X.s = "install";
                return;
            }
            if (X.s == "install")
            {
                std::string g, cid, err;
                int kh = -1;
                if (!E->sxInstall(*X.id, g, kh, cid, err)) { sxAbort("instalacao: " + err); return; }
                X.inst = true;
                {
                    Sv& v = svSt();
                    auto dir = svDir();
                    svDrop(v, [&](const Row& r) { return r.guid == g; });
                    v.seq += 1;
                    Row r;
                    r.seq = v.seq; r.guid = g; r.tot = kh;
                    std::optional<std::string> sg;
                    if (dir) sg = svSig(*dir + g + ".sav");
                    r.sig = sg ? *sg : "-";
                    r.cid = cid.empty() ? "-" : cid;
                    r.clk = t;
                    if (!cid.empty()) r.wc = cid;
                    v.rows.push_back(r);
                    v.base = svSigs(v);
                    v.dirty = false;
                    svSave(v);
                }
                bool fresh = X.fresh;
                if (!fresh) E->sxMark(*X.id, "installed");
                E->sxWant("");
                L(F("NOLA sxjoin: save do host instalado no save novo %s (kh=%d, campanha %s); carrego", sub8(g).c_str(), kh, cid.c_str()));
                std::string id = *X.id;
                N.sx.reset();
                if (!(fresh ? sxFresh(id) : sxRestore(id, true))) { N.stage = "idle"; E->sxUndo(g); }
            }
        }

        void n7Stage()
        {
            if (N.stage == "sx") { sxTick(); return; }
            if (!N.snap) { N.stage = "idle"; return; }
            Snapshot& S = *N.snap;
            Id pw = pawn();
            double t = now();
            if (N.stage == "load")
            {
                if (t - S.at > fl("qrbmax", 240) + 30) { L("NOLA n7: o recarregamento nao terminou: nada restaurado"); N.stage = "idle"; N.snap.reset(); return; }
                if (valid(pw) && pw != S.addr && !N.qiReset) { N.qiReset = true; E->qiReset(); }
                if (Q.rbWait || !valid(pw)) return;
                if (pw == S.addr) { L("NOLA n7: o pawn nao mudou (nenhum save carregado): nada restaurado"); N.stage = "idle"; N.snap.reset(); return; }
                if (S.fresh)
                {
                    L("NOLA sxjoin: mundo do host carregado: o cliente comeca com o jogador do save do host; nenhum item de outra campanha restaurado");
                    N.stage = "idle";
                    N.snap.reset();
                    N.qiReset = false;
                    return;
                }
                if (S.skip) { L("NOLA n7boot: save pareado carregado; os itens do save da outra campanha NAO sao restaurados"); N.stage = "idle"; N.snap.reset(); N.qiReset = false; return; }
                N.qiReset = false;
                N.heldAt.reset();
                N.stage = "settle";
                N.t = t + fl("n7wait", 5);
                return;
            }
            if (!valid(pw)) return;
            bool busy = E->itemsBusy();
            if (N.stage != "give")
            {
                int held = E->questsHeld();
                if (held > 0 && t - S.at < fl("qrbmax", 240) + 30)
                {
                    if (!N.heldAt)
                    {
                        N.heldAt = t;
                        L(F("NOLA n7: o jogo ainda segura os nos de missao depois da carga (%d missoes na fila nativa): o retrato espera", held));
                    }
                    return;
                }
                if (N.heldAt)
                {
                    L(F("NOLA n7: nos de missao liberados depois de %.1f s (fila nativa %d): o retrato segue", t - *N.heldAt, held));
                    N.heldAt.reset();
                }
            }
            if (N.stage == "settle" || N.stage == "trim")
            {
                if (t < N.t || busy) return;
                if (N.stage == "settle" && S.worn && !S.wornDone)
                {
                    S.wornDone = true;
                    std::string cs, e;
                    if (!E->worn(pw, cs)) { L("NOLA sxworn: slots do pawn ilegiveis: slots do save do host ficam"); N.t = t + 1; return; }
                    auto P = s2m_worn::plan(s2m_worn::parse(cs), *S.worn);
                    std::vector<std::string> off;
                    for (int s : P.off)
                    {
                        bool ok = E->unequip(pw, s, e);
                        off.push_back(std::to_string(s) + (ok ? "" : "(falhou " + e + ")"));
                    }
                    for (auto& sid : P.kept)
                    {
                        auto it = S.items.find(sid);
                        if (it != S.items.end() && --it->second <= 0) S.items.erase(it);
                    }
                    S.reEq = P.on;
                    L(F("NOLA sxworn: slots agora [%s], do cliente [%s]: tirados dos slots [%s], ficam %d, a vestir depois da restauracao %d", cs.c_str(), s2m_worn::str(*S.worn).c_str(),
                        join(off, " ").c_str(), (int)P.kept.size(), (int)P.on.size()));
                    N.t = t + 1.5;
                    return;
                }
                auto inv = invSnap(pw);
                std::vector<Give> give, take;
                std::vector<EqDiff> eq;
                n7Diff(S.items, inv.first, snapQT(S), give, take, eq);
                if (!eq.empty() && N.stage == "settle") n7EqLog(eq, "nao restaurados (podem estar num slot)");
                std::vector<Give> stuck;
                n7Cap(take, inv.first, n7Mag(pw, take), stuck);
                if (!stuck.empty())
                    L("NOLA n7: municao carregada nas armas do save fica (os nos de remocao so tiram municao solta): " + n7StuckTxt(stuck));
                for (auto& x : take) E->itemRemove(x.sid, x.n);
                long long dm = 0;
                if (N.stage == "settle")
                {
                    dm = (long long)std::floor(S.money - inv.second + 0.5);
                    if (dm != 0) E->modifyMoney(pw, dm);
                    N.res = Res{0, 0, dm};
                }
                std::vector<std::string> g, tk;
                for (auto& x : give) { g.push_back(x.sid + "+" + std::to_string(x.n)); N.res.give += x.n; }
                for (auto& x : take) { tk.push_back(x.sid + "-" + std::to_string(x.n)); N.res.take += x.n; }
                bool settle = N.stage == "settle";
                L(F("NOLA n7: %s: +[%s] -[%s]%s", settle ? "restaurando o retrato" : "ajuste depois das criacoes", join(g, " ").c_str(), join(tk, " ").c_str(),
                    settle ? F(" dinheiro %+lld", dm).c_str() : ""));
                N.give = give;
                N.next = settle ? "trim" : "verify";
                N.stage = "give";
                return;
            }
            if (N.stage == "give")
            {
                int per = (int)fl("n7per", 4);
                for (int i = 0; i < per && !N.give.empty(); ++i)
                {
                    Give x = N.give.front();
                    N.give.erase(N.give.begin());
                    std::string e;
                    if (!E->createItem(x.sid, x.n, e)) L("NOLA n7: XCreateItemInInventoryByID " + x.sid + " erro " + e);
                }
                if (N.give.empty()) { N.stage = N.next; N.t = t + (N.next == "trim" ? 1.5 : 3); }
                return;
            }
            if (N.stage == "verify")
            {
                if (t < N.t || busy) return;
                if (!S.reEq.empty())
                {
                    std::vector<std::string> r;
                    for (auto& [s, sid] : S.reEq)
                    {
                        std::string e;
                        bool ok = E->equip(pw, sid, s, e);
                        if (ok)
                        {
                            auto it = S.items.find(sid);
                            if (it != S.items.end() && --it->second <= 0) S.items.erase(it);
                        }
                        r.push_back(std::to_string(s) + "=" + sid + (ok ? "" : "(falhou " + e + ")"));
                    }
                    S.reEq.clear();
                    std::string cs;
                    E->worn(pw, cs);
                    L("NOLA sxworn: vestido de volta [" + join(r, " ") + "]; slots agora [" + cs + "]");
                    N.t = t + 1.5;
                    return;
                }
                auto inv = invSnap(pw);
                std::vector<Give> give, take;
                std::vector<EqDiff> eq;
                n7Diff(S.items, inv.first, snapQT(S), give, take, eq);
                std::vector<Give> stuck;
                n7Cap(take, inv.first, n7Mag(pw, take), stuck);
                std::vector<std::string> bad;
                for (auto& x : give) bad.push_back(x.sid + " falta " + std::to_string(x.n));
                for (auto& x : take) bad.push_back(x.sid + " sobra " + std::to_string(x.n));
                L(F("NOLA n7: RESTAURADO: +%d -%d itens, dinheiro %d (retrato %d), %d tipos diferentes do retrato%s", N.res.give, N.res.take, (int)inv.second, (int)S.money,
                    (int)bad.size(), bad.empty() ? "" : (" " + join(bad, ", ", 0, 6)).c_str()));
                if (!stuck.empty()) L("NOLA n7: alem do retrato, carregada nas armas do save: " + n7StuckTxt(stuck));
                if (!eq.empty()) n7EqLog(eq, "fora da conta do retrato");
                std::optional<std::string> sxId = S.sxId;
                N.stage = "idle";
                N.snap.reset();
                N.done += 1;
                if (sxId) E->sxMark(*sxId, "applied");
                Pair p;
                p.kind = Pair::force;
                p.serial = ++pairSerial;
                Q.n7Pair = p;
            }
        }

        int csCount()
        {
            int n = 0;
            for (auto& kv : Q.ap) n += kv.second;
            return n;
        }

        void n7Boot(const std::string& cid, int kh)
        {
            if (fl("csave") != 1) return;
            if (!Q.bootRec || Q.bootRec->done || kh < 0 || cid.empty() || N.stage != "idle" || Q.rbWait) return;
            BootRec& R = *Q.bootRec;
            Id pw = pawn();
            if (!(valid(pw) && pw == R.addr)) return;
            R.done = true;
            if (N.bootGo) { N.bootGo = false; L(F("NOLA n7boot: save pareado carregado (%d passos do host no registro, host %d)", R.n, kh)); return; }
            BootRec Rc = R;
            if (sxBoot(cid, kh, Rc)) return;
            bool other = isOther(Rc, cid);
            if (!other && Rc.n <= kh)
            {
                L(F("NOLA n7boot: o save carregado do cliente casa com o do host (campanha %s, %d passos, host %d)%s", cid.c_str(), Rc.n, kh, Rc.n < kh ? ": o catch-up aplica o resto" : ""));
                return;
            }
            std::string why = other ? F("e da campanha %s e o host esta na %s", ts(Rc.cid).c_str(), cid.c_str()) : F("tem %d passos do host e o save do host so %d (adiantado)", Rc.n, kh);
            std::string rest = other ? "catch-up limpo: registro descartado, o log do host inteiro e aplicado (o mundo do cliente pode ter restos da outra campanha)"
                                     : "o cliente fica adiantado: os passos a mais sao pulados quando o host os refizer";
            if (N.shared && fl("n7shared") != 1)
            {
                L("NOLA n7boot: o save carregado do cliente " + why + "; a pasta de saves e a MESMA do host (D13), nao ha save pareado para carregar; " + rest);
                return;
            }
            std::optional<Row> pick;
            if (fl("n7sv") == 1 && fl("qrollback") == 1) pick = svFind(kh, cid);
            if (!pick)
            {
                L("NOLA n7boot: o save carregado do cliente " + why + " e nenhum outro save do cliente da campanha " + cid + " tem <= " + std::to_string(kh) + " passos" +
                  (fl("n7sv") == 1 ? "" : " (n7sv=0)") + ": " + rest);
                return;
            }
            L(F("NOLA n7boot: o save carregado do cliente %s -> carrego o save pareado %s (%d passos do host, campanha %s)", why.c_str(), sub8(pick->guid).c_str(), pick->tot, cid.c_str()));
            N.bootGo = true;
            n7FollowImpl();
            if (N.snap && other) N.snap->skip = true;
            if (N.stage == "idle") N.bootGo = false;
        }

        bool parseB(const std::string& b, long long& g, std::string& cid, int& hl, int& hs, int& kh, std::string& tag)
        {
            Cur c{b};
            std::string sg, shl, shs, skh;
            if (!(c.digits(&sg) && c.ch(',') && c.digits() && c.ch(',') && c.word(&cid) && c.ch(',') && c.digits(&shl) && c.ch(',') && c.digits(&shs) && c.ch(',') &&
                  c.digits(&skh, true) && c.ch(',') && c.word(&tag)))
                return false;
            g = std::atoll(sg.c_str());
            hl = std::atoi(shl.c_str());
            hs = std::atoi(shs.c_str());
            kh = std::atoi(skh.c_str());
            return true;
        }

        void n7Early(double t, bool ok)
        {
            auto hp = part("HLD");
            if (!hp) return;
            Cur c{*hp};
            std::string sn, skh, guid;
            if (!(c.digits(&sn) && c.ch(',') && c.digits(&skh, true) && c.ch(',') && c.word(&guid))) return;
            int n = std::atoi(sn.c_str());
            int kh = std::atoi(skh.c_str());
            if (!N.hldSeen || n < *N.hldSeen) { N.hldSeen = n; return; }
            if (n == *N.hldSeen)
            {
                if (N.early && t - N.early->at > fl("qrbmax", 240) + 30)
                {
                    L(F("NOLA n7: o host comecou a carregar ha %.0f s e nao terminou (carga cancelada ou falhou): o cliente fica no save pareado que ja carregou; a proxima carga do host e seguida como sempre", t - N.early->at));
                    N.early.reset();
                }
                return;
            }
            N.hldSeen = n;
            if (fl("n7early") != 1) return;
            std::string g8 = guid.substr(0, 8);
            bool always = fl("n7always") == 1;
            if (!N.dieGo && !always) { L(F("NOLA n7: host comecou a carregar um save (inicio #%d, %s, kh=%d): sem a morte dos dois, o cliente espera o host entrar no mundo para saber se ele voltou no tempo", n, g8.c_str(), kh)); return; }
            if (N.stage != "idle") { L(F("NOLA n7: host comecou a carregar um save (inicio #%d) durante um retorno do cliente: o retorno em curso segue o host", n)); return; }
            if (!ok || fl("csave") != 1 || fl("qrollback") != 1) { L(F("NOLA n7: host comecou a carregar um save (inicio #%d) mas o cliente nao segue cedo (pasta de saves D13 ou csave/qrollback desligado): espera o host entrar no mundo", n)); return; }
            bool die = N.dieGo;
            N.dieGo = false;
            if (kh >= 0) N.kh = kh;
            if (die) L(F("NOLA n7: host COMECOU a carregar o save %s (inicio #%d, kh=%d) depois da morte dos dois: o cliente carrega o save pareado agora, sem esperar o host entrar no mundo", g8.c_str(), n, kh));
            else L(F("NOLA n7: host COMECOU a carregar o save %s (inicio #%d, kh=%d): o cliente sai do mundo e carrega o save pareado junto (n7always=1); entra quando o mundo do host estiver de pe", g8.c_str(), n, kh));
            Early e;
            e.n = n; e.kh = kh; e.guid = guid; e.at = t;
            N.early = e;
            n7FollowImpl();
            if (N.stage == "idle" && !N.svPend)
            {
                N.early.reset();
                N.dieGo = die;
                L("NOLA n7: o retorno cedo nao comecou: o cliente segue o host quando ele terminar de carregar");
            }
        }

        void wpFreeze()
        {
            if (fl("wpkg", 1) != 1 || fl("sxjoin") != 1 || !N.kh) return;
            std::string g, e;
            if (E->wpFreeze(N.hs, *N.kh, g, e))
                L(F("NOLA wpkg: host save #%d (kh=%d) frozen as the world package source: %s", N.hs, *N.kh, sub8(g).c_str()));
            else
                L(F("NOLA wpkg: host save #%d (kh=%d) NOT frozen: %s; clients get no world package for it", N.hs, *N.kh, e.c_str()));
        }

        std::string wpAsk(int hs, int kh, const std::string& cid, bool ok, double t)
        {
            if (fl("wpkg", 1) != 1 || fl("sxjoin") != 1) return "";
            if (!ok) { L(F("NOLA wpkg: host save #%d: no world package (same save folder as the host, D13)", hs)); return ""; }
            if (N.wp) L(F("NOLA wpkg: host save #%d supersedes the pending world package of save #%d", hs, N.wp->hs));
            Wp w;
            w.id = "9" + wallId();
            w.hs = hs;
            w.kh = kh;
            w.cid = cid;
            w.at = t;
            N.wp = w;
            return w.id;
        }

        void wpTick(double t)
        {
            if (!N.wp || N.sx) return;
            Wp& W = *N.wp;
            if (!W.asked)
            {
                if (!E->sxWant(W.id)) { L("NOLA wpkg: sx_want.txt not written: world package of save #" + std::to_string(W.hs) + " dropped"); N.wp.reset(); return; }
                W.asked = true;
                W.at = t;
                L(F("NOLA wpkg: asking the host for the world package of its save #%d (kh=%d, campaign %s, request %s)", W.hs, W.kh, W.cid.c_str(), W.id.c_str()));
                return;
            }
            std::string d;
            std::string st = E->sxIn(W.id, d);
            if (st == "ready")
            {
                int kh = -1, hs = -1;
                long long sz = 0;
                std::string cid, e;
                bool ok = E->wpTake(W.id, kh, hs, cid, sz, e);
                if (ok)
                    L(F("NOLA wpkg: world package received in %.1f s: host save #%d, kh=%d, campaign %s, %lld B%s", t - W.at, hs, kh, cid.c_str(), sz,
                        hs == W.hs ? "" : F(" (asked for #%d, the host had saved again)", W.hs).c_str()));
                else
                    L(F("NOLA wpkg: world package of save #%d refused: %s", W.hs, e.c_str()));
                E->sxWant("");
                N.wp.reset();
                return;
            }
            if (t - W.at > fl("wpkgwait", 180))
            {
                L(F("NOLA wpkg: no world package for host save #%d after %.0f s (state %s %s): dropped", W.hs, t - W.at, st.c_str(), d.c_str()));
                E->sxWant("");
                N.wp.reset();
            }
        }

        void wspRow(const Wsp& W)
        {
            Sv& v = svSt();
            auto dir = svDir();
            if (!dir) return;
            auto s = svSig(*dir + W.guid + ".sav");
            if (!s) return;
            for (auto& r : v.rows)
                if (r.guid == W.guid) r.sig = *s;
            if (v.base) (*v.base)[W.guid] = *s;
            svSave(v);
        }

        static std::string wpReq(const std::string& file)
        {
            size_t a = file.rfind("__");
            size_t b = file.rfind(".pkg");
            if (a == std::string::npos || b == std::string::npos || b <= a + 2) return "?";
            return file.substr(a + 2, b - a - 2);
        }

        void wspTick(double t)
        {
            if (fl("wsplice", 1) != 1 || fl("wpkg", 1) != 1 || fl("n7sv") != 1 || fl("csave") != 1 || !client()) return;
            if (N.wsp && N.wsp->run)
            {
                Wsp& W = *N.wsp;
                std::string d;
                std::string st = E->wspPoll(d);
                if (st == "run")
                {
                    if (t - W.at > fl("wsplicewait", 120))
                    {
                        L(F("NOLA wsplice: splice of client save %s still running after %.0f s: given up, the save keeps the client's own world", sub8(W.guid).c_str(), t - W.at));
                        N.wspLast = W.key;
                        N.wsp.reset();
                    }
                    return;
                }
                N.wspLast = W.key;
                if (st == "done")
                {
                    wspRow(W);
                    L(F("NOLA wsplice: client save %s (pair #%d) now holds the host world of save #%d (kh=%d) and this client's own character, %.1f s: %s", sub8(W.guid).c_str(), W.seq, W.hs, W.kh,
                        t - W.at, d.c_str()));
                }
                else
                    L(F("NOLA wsplice: client save %s NOT spliced with the host world of save #%d (%s): %s; the save keeps the client's own world", sub8(W.guid).c_str(), W.hs, st.c_str(), d.c_str()));
                N.wsp.reset();
                return;
            }
            if (Q.csWait || N.stage != "idle" || N.sx) return;
            std::string file, cid;
            int kh = -1, hs = -1;
            if (!E->wpLast(file, cid, kh, hs)) return;
            auto dir = svDir();
            if (!dir) return;
            Sv& v = svSt();
            const Row* pick = nullptr;
            const bool bound = fl("wspair", 1) == 1;
            const std::string req = wpReq(file);
            for (auto& r : v.rows)
                if (r.tot == kh && (r.cid == "-" || cid.empty() || r.cid == cid) && (!bound || r.wp == req) && (!pick || r.seq > pick->seq)) pick = &r;
            if (N.wspSeenFile != file)
            {
                N.wspSeenFile = file;
                N.wspSeenAt = t;
                N.wspWarned = false;
            }
            if (!pick)
            {
                if (!N.wspWarned && t - N.wspSeenAt > fl("wpkgwait", 180))
                {
                    N.wspWarned = true;
                    L(bound ? F("NOLA wsplice: host world of save #%d (kh=%d, request %s) has no client save made for that host save after %.0f s: nothing spliced", hs, kh, req.c_str(), t - N.wspSeenAt)
                            : F("NOLA wsplice: host world of save #%d (kh=%d) has no client save with the same %d host steps after %.0f s: nothing spliced", hs, kh, kh, t - N.wspSeenAt));
                }
                return;
            }
            std::string key = file + "|" + pick->guid;
            if (key == N.wspLast) return;
            if (svSig(*dir + pick->guid + ".sav") != std::optional<std::string>(pick->sig)) return;
            std::string e;
            if (!E->wspStart(*dir, pick->guid, file, fl("wspos", 1) == 1, fl("wstrip", 1) == 1, e))
            {
                N.wspLast = key;
                L(F("NOLA wsplice: client save %s NOT spliced with the host world of save #%d: %s", sub8(pick->guid).c_str(), hs, e.c_str()));
                return;
            }
            Wsp w;
            w.key = key; w.file = file; w.guid = pick->guid; w.cid = cid; w.kh = kh; w.hs = hs; w.seq = pick->seq; w.at = t; w.run = true;
            N.wsp = w;
            L(F("NOLA wsplice: splicing the host world of save #%d (kh=%d, wp_in\\%s) into client save %s (pair #%d)", hs, kh, file.c_str(), sub8(pick->guid).c_str(), pick->seq));
        }

        void n7Client()
        {
            auto bp = part("B");
            std::string b = bp ? *bp : std::string();
            long long g = 0;
            std::string cid, tag;
            int hl = 0, hs = 0, kh = 0;
            double t = now();
            if (!parseB(b, g, cid, hl, hs, kh, tag))
            {
                if (!b.empty() && !N.warned) { N.warned = true; L("NOLA n7: o host nao manda os campos n7 em #B (n7=0 ou core antigo no host): nada a seguir"); }
                if (N.stage != "idle") n7Stage();
                return;
            }
            int sess = E->peerSessions();
            if (N.sess != std::optional<int>(sess))
            {
                N.sess = sess;
                N.hldSeen.reset();
                N.early.reset();
                N.again = false;
                N.g = g; N.hl = hl; N.hs = hs;
                if (kh >= 0) N.kh = kh; else N.kh.reset();
                N.cid = cid;
                N.shared = tag == n7Tag(n7Path());
                L(F("NOLA n7: host na sessao %d: cargas=%d saves=%d kh=%s geracao=%lld pasta_de_saves=%s", sess, hl, hs, ti(N.kh).c_str(), g, N.shared ? "A MESMA do host (D13)" : "propria"));
                if (fl("n7reboot", 1) == 1 && Q.bootRec && Q.bootRec->done)
                {
                    Id pw = pawn();
                    if (valid(pw) && pw == Q.bootRec->addr)
                    {
                        Q.bootRec->done = false;
                        Q.bootRec->n = csCount();
                        L(F("NOLA n7: host voltou em nova sessao %d com o cliente no mundo: decisao de boot rearmada (%d passos aplicados aqui)", sess, Q.bootRec->n));
                    }
                }
                n7Boot(cid, kh);
                return;
            }
            if (N.cid != std::optional<std::string>(cid) && fl("qnohold") != 1)
            {
                if (N.cid) L(F("NOLA n7: host comecou outra campanha (%s -> %s, geracao %lld): jogo novo, nao e volta no tempo; o cliente NAO recarrega", N.cid->c_str(), cid.c_str(), g));
                N.cid = cid;
                N.g = g;
            }
            if (kh >= 0) N.kh = kh;
            n7Boot(cid, kh);
            bool ok = !N.shared || fl("n7shared") == 1;
            if (hs > N.hs)
            {
                N.hs = hs;
                Pair p;
                p.kind = Pair::table; p.serial = ++pairSerial; p.hs = hs; p.kh = kh; p.cid = cid; p.at = t;
                Q.n7Pair = p;
                L(F("NOLA csave1: host salvou (#%d, kh=%d, campanha %s): o cliente salva agora%s", hs, kh, cid.c_str(), ok ? "" : " (NAO: mesma pasta de saves, D13)"));
                Q.n7Pair.wp = wpAsk(hs, kh, cid, ok, t);
            }
            wpTick(t);
            wspTick(t);
            n7Early(t, ok);
            if (N.again && N.stage == "idle" && hl <= N.hl)
            {
                N.again = false;
                L("NOLA n7: o retorno cedo acabou num save diferente do que o host carregou: o cliente segue o host de novo agora");
                n7FollowImpl();
            }
            if (hl > N.hl && N.early)
            {
                Early e = *N.early;
                N.early.reset();
                N.hl = hl;
                N.g = g;
                if (kh == e.kh)
                    L(F("NOLA n7: host terminou de carregar (#%d, kh=%d, %.1f s depois de comecar): e o save que o cliente ja seguiu cedo; nada mais a fazer", hl, kh, t - e.at));
                else if (e.kh >= 0 && kh > e.kh && fl("n7samepair", 1) == 1)
                    L(F("NOLA n7: host terminou de carregar (#%d) com kh=%d, o cliente seguiu cedo kh=%d: os %d passo(s) a mais vieram depois do inicio da carga e os pares acima de kh=%d ja foram largados, o save pareado e o mesmo; nada mais a fazer", hl, kh, e.kh, kh - e.kh, e.kh));
                else if (N.stage != "idle")
                {
                    N.again = true;
                    L(F("NOLA n7: host terminou de carregar (#%d) com kh=%d, mas o cliente seguiu cedo kh=%d: quando o retorno em curso acabar o cliente segue de novo", hl, kh, e.kh));
                }
                else
                {
                    L(F("NOLA n7: host terminou de carregar (#%d) com kh=%d, mas o cliente seguiu cedo kh=%d: o cliente segue de novo o save certo", hl, kh, e.kh));
                    n7FollowImpl();
                }
            }
            else if (hl > N.hl)
            {
                bool die = N.dieGo;
                N.dieGo = false;
                bool always = fl("n7always") == 1;
                bool cut = N.g != std::optional<long long>(g) || die;
                if (die) L(F("NOLA n7: host carregou um save (#%d) depois da morte dos dois jogadores: o cliente segue o host", hl));
                else if (!cut && always) L(F("NOLA n7: host carregou um save (#%d, geracao %lld): o cliente recarrega junto (n7always=1)", hl, g));
                N.hl = hl;
                N.g = g;
                if (!cut && !always) L(F("NOLA n7: host carregou um save (#%d) sem perder passos (geracao %lld): nada a seguir", hl, g));
                else if (N.stage != "idle") L(F("NOLA n7: host carregou de novo (#%d) durante o retorno: o catch-up em curso segue o host", hl));
                else if (!ok) L(F("NOLA n7: host voltou no tempo (carga #%d, geracao %lld) mas a pasta de saves e a MESMA dos dois jogos (D13): o cliente NAO recarrega (n7shared=0)", hl, g));
                else if (fl("csave") != 1 || fl("qrollback") != 1) L("NOLA n7: host voltou no tempo mas o cliente precisa de csave=1 e qrollback=1: nada feito");
                else n7FollowImpl();
            }
            else if (hl < N.hl) N.hl = hl;
            if (N.stage != "idle") n7Stage();
        }

        void n7Host()
        {
            double t = now();
            std::string hst = fl("sxjoin") == 1 ? E->sxHost(N.sxLd ? -1 : (N.kh ? *N.kh : -1)) : std::string();
            Id pw = pawn();
            if (!valid(pw)) return;
            if (!N.path) N.path = n7Path();
            if (!N.addr) { N.addr = pw; N.pend = t; N.first = true; }
            else if (pw != *N.addr) { N.addr = pw; N.pend = t; N.first = false; N.sv.reset(); }
            if (N.pend && t >= *N.pend + 11 && !Q.rbCheckPending && qlSettled())
            {
                N.pend.reset();
                N.kh = E->hostLogSize();
                N.sxLd = true;
                N.sig = n7Sig();
                if (N.first) { N.first = false; L(F("NOLA n7: host: pawn no mundo; passos no save carregado kh=%d", *N.kh)); }
                else
                {
                    N.hl += 1;
                    auto gen = E->hostGen();
                    L(F("NOLA n7: host carregou um save (#%d): geracao %s, kh=%d", N.hl, gen ? std::to_string(*gen).c_str() : "nil", *N.kh));
                }
                return;
            }
            if (N.sxLd && !N.pend && !N.sv && hst.rfind("recusado", 0) == 0 && fl("sxhsave", 1) == 1 && (N.hsvAt < 0 || t >= N.hsvAt + fl("sxhsavegap", 20)))
            {
                N.hsvAt = t;
                N.hsvN += 1;
                if (E->cineFrozen() || E->downed() || E->emNoSave())
                    L(F("NOLA sxjoin: host: a client waits for the host save, but the host loaded a save and has not saved since; no autosave now (cinematic, downed or no-save zone), try #%d again in %.0f s", N.hsvN, fl("sxhsavegap", 20)));
                else
                {
                    std::string e;
                    bool ok = E->joinAutoSave(e);
                    L(F("NOLA sxjoin: host: a client waits for the host save, but the host loaded a save and has not saved since: host join autosave #%d (XTriggerForcedAutoSave through the co-op save mask, ok=%s%s%s)", N.hsvN, ok ? "true" : "false", e.empty() ? "" : ", ", e.c_str()));
                }
            }
            if (t >= N.pollAt)
            {
                N.pollAt = t + fl("n7poll", 2);
                auto s = n7Sig();
                if (!N.sig) N.sig = s;
                else if (s != N.sig) { N.sig = s; N.sv = SvPend{t, pw}; }
            }
            if (N.sv && t >= N.sv->at + fl("n7conf", 5))
            {
                SvPend sv = *N.sv;
                N.sv.reset();
                if (sv.addr == pw && !N.pend)
                {
                    N.hs += 1;
                    N.kh = E->hostLogSize();
                    N.sxLd = false;
                    L(F("NOLA n7: host salvou (#%d): kh=%d", N.hs, *N.kh));
                    wpFreeze();
                }
            }
        }

        bool cs1Due(bool frozen, double t)
        {
            const Pair T = Q.n7Pair;
            if (T.kind != Pair::table || frozen || Q.rbWait || t < Q.csNext) return false;
            if (fl("cswc", 1) == 1 && !T.cid.empty() && Q.apW != std::optional<std::string>(T.cid))
            {
                Q.n7Pair = Pair{};
                L(F("NOLA csave1: host salvou (#%d) mas o mundo carregado do cliente e da campanha %s e o host esta na %s: o cliente NAO salva (seria um par falso; entrada sxjoin=1 ou um save pareado de verdade)",
                    T.hs, Q.apW ? Q.apW->c_str() : "desconhecida", T.cid.c_str()));
                return false;
            }
            if (N.made && N.stage != "idle") return false;
            if (N.made && N.shared && fl("n7shared") != 1)
            {
                Q.n7Pair = Pair{};
                if (!Q.cs1Warn) { Q.cs1Warn = true; L("NOLA csave1: o cliente e o host gravam na MESMA pasta de saves (D13): o cliente NAO salva (lance o cliente com -saveddirsuffix=client e com o Steam aberto)"); }
                return false;
            }
            return true;
        }

        int cs1Rec(int n, std::string& wp)
        {
            if (!Q.csWait || Q.csWait->tag.kind != Pair::table) return n;
            const Pair T = Q.csWait->tag;
            wp = T.wp.empty() ? "-" : T.wp;
            if (Q.n7Pair.kind == Pair::table && Q.n7Pair.serial == T.serial) Q.n7Pair = Pair{};
            L(F("NOLA csave1: save do cliente = par do save #%d do host (kh=%d, campanha %s); o cliente tinha %d passos aplicados%s%s", T.hs, T.kh, T.cid.c_str(), n,
                n != T.kh ? " (diferente do host: o catch-up da proxima carga acerta)" : "",
                Q.n7Pair.kind == Pair::table ? F("; o host ja salvou de novo (#%d): outro save a seguir", Q.n7Pair.hs).c_str() : ""));
            return T.kh;
        }

        void record(const std::string& why)
        {
            if (!Q.apCid)
            {
                auto b = part("B");
                Q.apCid = bCid(b ? *b : std::string());
            }
            std::string o = std::string(CSFMT) + (Q.apCid ? " " + *Q.apCid : std::string()) + "\n";
            if (Q.apW) o += "W\t" + *Q.apW + "\n";
            int n = 0;
            if (Q.csSnap)
                for (auto& kv : *Q.csSnap) { o += std::to_string(kv.second) + "\t" + kv.first + "\n"; n += kv.second; }
            if (E->writeText("client_applied.txt", o))
            {
                L("NOLA csave: jogo do cliente salvo (" + why + "); " + std::to_string(n) + " eventos do host registrados como contidos nele");
                Q.csBase = n;
                Q.n7SavedAt = now();
                std::string wp = "-";
                int k = cs1Rec(n, wp);
                svRec(k, wp);
            }
            Q.csSnap.reset();
            Q.csWait.reset();
            Q.csRefused.reset();
        }
    }

    void bind(Env* env) { E = env; }
    Env* env() { return E; }
    QState& qs() { return Q; }
    void reset()
    {
        Q = QState{};
        N = N7{};
        V = Sv{};
        J = Join{};
        KT = Kit{};
        EQ = EqTables{};
        pairSerial = 0;
    }
    void setLootSids(std::vector<std::string> sids) { LOOT = std::move(sids); }
    const std::vector<std::string>& lootSids() { return LOOT; }

    Counts& applied()
    {
        Id pw = pawn();
        if (valid(pw) && Q.apAddr != std::optional<Id>(pw))
        {
            if (Q.apAddr && !Q.ap.empty()) L("NOLA quest: pawn novo do cliente: contagem de eventos aplicados zerada (mundo voltou ao save base)");
            Q.apAddr = pw;
            Q.ap.clear();
            Q.csDirty = false; Q.csSnap.reset(); Q.csWait.reset(); Q.csQueue.reset(); Q.csAhead.reset(); Q.csRefused.reset(); Q.apFresh = false; Q.apCid.reset(); Q.apW.reset();
            Q.csLoads += 1;
            Q.cuDone = false;
            Q.cuStale = part("S");
            Q.cuStaleAt = now();
            if (fl("csave") == 1 && client())
            {
                int n = 0;
                std::string d;
                if (E->readText("client_applied.txt", d))
                {
                    auto ls = lines(d);
                    if (!d.empty() && d.back() == '\n' && !ls.empty() && ls.back().empty()) ls.pop_back();
                    std::string head = ls.empty() ? std::string() : ls[0];
                    std::string pre = std::string(CSFMT) + " ";
                    if (head.size() > pre.size() && head.compare(0, pre.size(), pre) == 0)
                    {
                        std::string w = head.substr(pre.size());
                        bool allw = !w.empty();
                        for (char c : w) if (!isw(c)) allw = false;
                        if (allw) Q.apCid = w;
                    }
                    if (head != CSFMT && !Q.apCid) L(std::string("NOLA csave: client_applied.txt sem o carimbo '") + CSFMT + "' (arquivo antigo ou de outro mod): ignorado");
                    else
                        for (size_t i = 1; i < ls.size(); ++i)
                        {
                            const std::string& ln = ls[i];
                            size_t p = 0;
                            while (p < ln.size() && isd(ln[p])) ++p;
                            if (p > 0 && p + 1 < ln.size() && ln[p] == '\t')
                            {
                                int c = std::atoi(ln.substr(0, p).c_str());
                                Q.ap[ln.substr(p + 1)] = c;
                                n += c;
                            }
                            else if (!Q.apW && ln.size() > 2 && ln[0] == 'W' && ln[1] == '\t')
                            {
                                std::string w = ln.substr(2);
                                bool allw = !w.empty();
                                for (char c : w) if (!isw(c)) allw = false;
                                if (allw) Q.apW = w;
                            }
                        }
                }
                BootRec r;
                r.cid = Q.apCid; r.n = n; r.addr = pw; r.wc = Q.apW;
                Q.bootRec = r;
                Q.apFresh = n > 0;
                L("NOLA csave: save do cliente carregado; " + std::to_string(n) + " eventos do host ja estao nele (client_applied.txt)");
            }
        }
        return Q.ap;
    }

    bool qlSettled()
    {
        if (!E->isHost()) return true;
        if (Q.rbCheckPending || (fl("qrollback") == 1 && !Q.rbAddrKnown)) return false;
        if (Q.qlOk) return true;
        if (!Q.qlT0) Q.qlT0 = now();
        if (now() - *Q.qlT0 >= fl("qlsettle", 60))
        {
            Q.qlOk = true;
            L(F("NOLA quest: o relogio do jogo nao confirmou o log do host em %d s: #B e catch-up liberados assim mesmo", (int)fl("qlsettle", 60)));
        }
        return Q.qlOk;
    }

    void joinTick()
    {
        if (!client()) return;
        bool up = E->peerState() == peerUp && !E->peerLiveFalse();
        bool frozen = E->cineFrozen();
        const char* why = !up ? "parceiro ausente" : (frozen ? "congelado na cinematica do host" : nullptr);
        if (fl("leashown") == 1)
        {
            if (why) E->leashSet("join_park", 1e9, -1, why, true);
            else E->leashDrop("join_park", "parceiro de volta / descongelado");
        }
        else if (why && !J.oldLeashWarned)
        {
            J.oldLeashWarned = true;
            L("NOLA join: leashown=0: a coleira antiga (FL.leashm) nao existe no C++; join sem parada nem puxao");
        }
        if (fl("joinport") != 1) { J.want.reset(); return; }
        Id pw = pawn();
        if (!valid(pw)) return;
        if (J.addr != std::optional<Id>(pw))
        {
            if (J.addr) J.want = "save carregado";
            J.addr = pw;
        }
        if (E->peerState() != peerUp || E->peerLiveFalse()) return;
        int sess = E->peerSessions();
        if (J.sess != std::optional<int>(sess)) { J.sess = sess; J.want = "sessao do host #" + std::to_string(sess); }
        V3 r, lp;
        if (!E->remotePos(r) || !E->localPos(lp)) return;
        double d = std::sqrt((r.x - lp.x) * (r.x - lp.x) + (r.y - lp.y) * (r.y - lp.y)) / 100;
        double t = now();
        double joinm = fl("joinm", 10);
        if (J.waitT)
        {
            if (d <= joinm + 5) { L(F("NOLA join: ao lado do host (%.0f m)", d)); J.want.reset(); J.waitT.reset(); J.tries.reset(); }
            else if (t > *J.waitT)
            {
                J.waitT.reset();
                J.tries = (J.tries ? *J.tries : 0) + 1;
                if (*J.tries >= 3) { J.want.reset(); J.tries.reset(); L("NOLA join: teleporte ao host nao funcionou em 3 tentativas; desisto ate o proximo join"); }
            }
            return;
        }
        if (!J.want) return;
        if (E->cineJoinBusy()) return;
        if (d <= joinm) { L(F("NOLA join: %s; host a %.0f m: sem teleporte", J.want->c_str(), d)); J.want.reset(); J.tries.reset(); return; }
        if (fl("leashown") == 1)
        {
            if (!E->leashPull("join", joinm, 1, *J.want)) return;
        }
        else
        {
            if (!J.oldLeashWarned) { J.oldLeashWarned = true; L("NOLA join: leashown=0: a coleira antiga (FL.leashm) nao existe no C++; join sem parada nem puxao"); }
            return;
        }
        J.untilT = t + 1;
        J.waitT = t + 60;
        L(F("NOLA join: %s; host a %.0f m -> teleporte para junto do host (coleira em %s m por 1 s)", J.want->c_str(), d, E->flagText("joinm", "10").c_str()));
    }

    void kitTick()
    {
        if (fl("kit") != 1 || !client() || !E->coop()) return;
        Id pw = pawn();
        if (!valid(pw)) { KT.addr.reset(); return; }
        double t = now();
        if (KT.addr != std::optional<Id>(pw)) { KT.addr = pw; KT.at = t; KT.done = false; return; }
        if (KT.done || t - KT.at < fl("kitdelay", 12)) return;
        std::string cid;
        if (Q.apCid) cid = *Q.apCid;
        else
        {
            if (t - KT.at < fl("kitwait", 90)) return;
            cid = "-";
        }
        if (fl("sxscope", 1) == 1 && N.made && N.stage != "idle") return;
        if (fl("sxscope", 1) == 1 && cid != "-" && fl("cswc", 1) == 1 && Q.apW != std::optional<std::string>(cid)) return;
        std::string mk;
        bool hasMark = false;
        if (E->readText("kit_done.txt", mk))
        {
            auto ls = lines(mk);
            if (!ls.empty() && !(mk.empty())) { mk = ls[0]; hasMark = true; }
        }
        if (hasMark && (mk == cid || cid == "-")) { KT.done = true; return; }
        KT.done = true;
        std::string d;
        if (!E->readText("s2u_kit.tsv", d)) { L("NOLA kit: s2u_kit.tsv ausente; nada dado"); return; }
        std::vector<std::pair<std::string, int>> rows;
        for (auto& ln : lines(d))
        {
            size_t i = 0;
            while (i < ln.size() && std::isspace((unsigned char)ln[i])) ++i;
            size_t j = i;
            while (j < ln.size() && (isw(ln[j]) || ln[j] == '_')) ++j;
            if (j == i) continue;
            size_t k = j;
            while (k < ln.size() && std::isspace((unsigned char)ln[k])) ++k;
            if (k == j) continue;
            size_t m = k;
            while (m < ln.size() && isd(ln[m])) ++m;
            if (m == k) continue;
            rows.push_back({ln.substr(i, j - i), std::atoi(ln.substr(k, m - k).c_str())});
        }
        std::vector<std::string> given;
        int had = 0;
        for (auto& r : rows)
        {
            int have = 0;
            if (!E->itemCount(pw, r.first, have)) have = 0;
            if (have >= r.second) ++had;
            else
            {
                std::string e;
                if (E->createItem(r.first, r.second - have, e)) given.push_back(r.first + "x" + std::to_string(r.second - have));
                else L("NOLA kit: " + r.first + " erro " + e);
            }
        }
        E->writeText("kit_done.txt", cid + "\n");
        L(F("NOLA kit: campanha %s: %d itens dados (%s), %d ja tinha", cid.c_str(), (int)given.size(), join(given, ",").c_str(), had));
    }

    void questIn()
    {
        if (fl("qsync") != 1 || !(client() || fl("qtest") == 1)) return;
        double t = now();
        if (t - Q.seenPruneAt >= 1)
        {
            Q.seenPruneAt = t;
            for (auto it = Q.seen.begin(); it != Q.seen.end();)
            {
                if (t - it->second > 5) it = Q.seen.erase(it);
                else ++it;
            }
        }
        auto jp = part("J");
        std::string j = jp ? *jp : std::string();
        size_t p = 0;
        while (p < j.size())
        {
            size_t e = j.find('|', p);
            if (e == std::string::npos) e = j.size();
            std::string ev = j.substr(p, e - p);
            p = e + 1;
            if (ev.empty()) continue;
            size_t q = 0;
            while (q < ev.size() && isd(ev[q])) ++q;
            if (q == 0 || q >= ev.size() || ev[q] != ',' || q + 1 >= ev.size()) continue;
            long long seq = std::atoll(ev.substr(0, q).c_str());
            std::string rest = ev.substr(q + 1);
            Ev v = parseEv(rest);
            auto it = Q.seen.find(seq);
            bool seenBefore = it != Q.seen.end();
            Q.seen[seq] = t;
            if (!seenBefore) Q.rx += 1;
            if (seenBefore) continue;
            if (Q.rbWait)
            {
                if (!Q.rbQueue) Q.rbQueue.emplace();
                Q.rbQueue->push_back(rest);
                size_t n = Q.rbQueue->size();
                if (n == 1 || n % 25 == 0) L(F("NOLA quest: cliente seq=%lld %s -> retido (rbWait ha %.0f s; %d na fila)", seq, rest.c_str(), t - Q.rbWait->at, (int)n));
            }
            else if (Q.csWait)
            {
                if (!Q.csQueue) Q.csQueue.emplace();
                Q.csQueue->push_back(rest);
                size_t n = Q.csQueue->size();
                if (n == 1 || n % 25 == 0) L(F("NOLA quest: cliente seq=%lld %s -> retido (csWait; %d na fila)", seq, rest.c_str(), (int)n));
            }
            else if (Q.csAhead && Q.csAhead->count(rest) && (*Q.csAhead)[rest] > 0)
            {
                (*Q.csAhead)[rest] -= 1;
                Counts& ap = applied();
                ap[rest] += 1;
                Q.csDirty = true;
                L("NOLA quest: cliente seq=" + std::to_string(seq) + " " + rest + " -> pulado: ja estava aplicado antes de o host voltar no tempo");
            }
            else
            {
                Counts& ap = applied();
                ap[rest] += 1;
                Q.csDirty = true;
                Q.csAt = now();
                QuestResult r = apply(v, true);
                if (r.ok && r.r == 1) Q.applied += 1;
                else Q.skipped += 1;
                L(F("NOLA quest: cliente seq=%lld %s %s %s %s %s -> %s %s", seq, v.kind.c_str(), ts(v.a).c_str(), ts(v.b).c_str(), ts(v.c).c_str(), ts(v.sid).c_str(),
                    r.ok ? trv(r).c_str() : "erro", r.ok ? (r.hasWhy ? r.why.c_str() : "") : r.why.c_str()));
            }
        }
    }

    void csTick()
    {
        if (fl("csave") != 1 || !client()) return;
        if (!E->coop() && !Q.csWait) return;
        double t = now();
        Id pw = pawn();
        if (!valid(pw)) return;
        bool frozen = E->cineFrozen() || E->downed() || E->emNoSave();
        applied();
        if (Q.csAddr != std::optional<Id>(pw))
        {
            Q.csAddr = pw;
            Q.csNext = t + 20;
            Q.csPeerWas.clear();
            Q.csLeave = false;
            if (!frozen)
            {
                int mm = -1;
                if (E->movementMode(pw, mm) && mm == 0 && E->setMovementMode(pw, 1)) L("NOLA csave: movimento devolvido ao pawn carregado");
            }
        }
        if (fl("qgap") == 1 && fl("qonce") == 1 && t >= Q.gapChk)
        {
            Q.gapChk = t + 5;
            auto b = part("B");
            auto hn = bHn(b ? *b : std::string());
            int have = (Q.csQueue ? (int)Q.csQueue->size() : 0) + (Q.rbQueue ? (int)Q.rbQueue->size() : 0) + sum(applied());
            if (hn && *hn > have && !part("S") && !Q.rbWait)
            {
                Q.gapN += 1;
                if (Q.gapN >= 2 && t >= Q.gapAt)
                {
                    Q.gapAt = t + 60;
                    Q.gapN = 0;
                    Q.csLoads += 1;
                    Q.cuDone = false;
                    L(F("NOLA quest: o host tem %d eventos e este mundo %d: catch-up pedido de novo (#N %d)", (int)*hn, have, Q.csLoads));
                }
            }
            else Q.gapN = 0;
        }
        if (!Q.csWait && Q.csQueue)
        {
            Counts& ap = applied();
            int okN = 0;
            for (auto& rest : *Q.csQueue)
            {
                ap[rest] += 1;
                if (Q.csAhead && Q.csAhead->count(rest) && (*Q.csAhead)[rest] > 0)
                {
                    (*Q.csAhead)[rest] -= 1;
                    L("NOLA quest: cliente " + rest + " -> pulado: ja estava aplicado antes de o host voltar no tempo");
                }
                else
                {
                    QuestResult r = apply(parseEv(rest), false);
                    if (r.ok && r.r == 1) { ++okN; Q.applied += 1; }
                    else Q.skipped += 1;
                }
            }
            L("NOLA csave: " + std::to_string(Q.csQueue->size()) + " eventos do host guardados durante o save; " + std::to_string(okN) + " aplicados agora");
            Q.csQueue.reset();
            Q.csDirty = true;
            Q.csAt = t;
        }
        if (Q.csWait)
        {
            CsWait& W = *Q.csWait;
            if (t >= W.at + fl("csdelay", 0))
            {
                auto s1 = stampNow();
                if (!W.sig && !s1)
                {
                    if (!Q.csBlind) { Q.csBlind = true; L("NOLA csave: nao consigo ler o indice de saves (nil); registro sem verificacao"); }
                    record("sem verificacao");
                }
                else if (s1 != W.sig)
                {
                    if (!W.okAt) W.okAt = t;
                    if (t >= *W.okAt + (Q.lvOn ? fl("clqconf", 2) : fl("csconfirm", 5))) record(Q.lvOn ? "saida: arquivo de saves mudou" : "arquivo de saves mudou");
                }
                else if (t >= W.at + fl("cswait", 10))
                {
                    Q.csRefused = (Q.csRefused ? *Q.csRefused : 0) + 1;
                    int rf = *Q.csRefused;
                    if (rf <= 3 || rf % 10 == 0)
                        L(F("NOLA csave: o jogo RECUSOU salvar (tentativa %d; em combate?); nada registrado, nova tentativa em %d s ou mais, depois de %d s sem perder vida", rf, (int)fl("csretry", 20), (int)fl("cscalm", 15)));
                    Q.csWait.reset();
                    Q.csSnap.reset();
                    Q.csDirty = true;
                    Q.csNext = t + fl("csretry", 20);
                }
            }
            return;
        }
        int st = E->peerState();
        std::string sts = st == peerUp ? "up" : (st == peerSilent ? "silent" : "");
        bool leaving = sts == "silent" && Q.csPeerWas == "up";
        Q.csPeerWas = sts;
        if (leaving) Q.csLeave = true;
        bool go = cs1Due(frozen, t);
        if (go)
        {
            auto s0 = stampNow();
            std::string e;
            bool ok = E->forcedAutoSave(e);
            if (!Q.csRefused) L("NOLA csave: XTriggerForcedAutoSave ok=" + tb(ok) + (Q.csLeave ? " (o outro lado saiu)" : "") + " " + (ok ? "" : e));
            Q.csNext = t;
            if (ok)
            {
                Q.csDirty = false;
                Q.csLeave = false;
                CsWait w;
                w.at = t; w.sig = s0; w.tag = Q.n7Pair;
                Q.csWait = w;
                Q.csSnap = applied();
            }
        }
    }

    int leaveAsk(std::string& why)
    {
        if (!E || fl("clqsave", 1) != 1) { why = "clqsave=0"; return 0; }
        if (fl("csave") != 1 || fl("n7") != 1 || !client()) { why = "not a co-op client (csave/n7 off or host)"; return 0; }
        double t = now();
        if (Q.lvOn) { why = "leave save already running"; return 1; }
        Id pw = pawn();
        if (!valid(pw)) { why = "no pawn"; return 0; }
        if (!N.cid) { why = "no host campaign seen in this game"; return 0; }
        applied();
        if (fl("cswc", 1) == 1 && Q.apW != N.cid)
        {
            why = F("the loaded world is campaign %s and the host is on %s: no save (it would be a false pair)", Q.apW ? Q.apW->c_str() : "unknown", N.cid->c_str());
            return 0;
        }
        if (N.shared && fl("n7shared") != 1) { why = "client and host write the same save folder (D13): no save"; return 0; }
        if (!E->leaveStoreOk()) { why = "the save store switch is not finished: no save"; return 0; }
        if (Q.csWait)
        {
            Q.lvOn = true;
            Q.lvAt = t;
            why = "a client save is already being written: wait for it";
            return 1;
        }
        if (E->cineFrozen() || E->downed() || E->emNoSave()) { why = "cinematic, downed or no-save zone: no save"; return 0; }
        auto s0 = stampNow();
        std::string e;
        if (!E->forcedAutoSave(e)) { why = "XTriggerForcedAutoSave failed: " + e; return 0; }
        CsWait w;
        w.at = t;
        w.sig = s0;
        Q.csWait = w;
        Q.csSnap = applied();
        Q.csDirty = false;
        Q.csNext = t;
        Q.lvOn = true;
        Q.lvAt = t;
        why = F("client save started (campaign %s, %d host steps applied)", N.cid->c_str(), sum(*Q.csSnap));
        return 1;
    }

    bool leaveWait()
    {
        if (!Q.lvOn) return false;
        double t = now();
        if (!Q.csWait)
        {
            Q.lvOn = false;
            L(F("NOLA clq: leave save finished after %.1f s (%s)", t - Q.lvAt, Q.csRefused ? "the game refused the save" : "save written"));
            return false;
        }
        if (t >= Q.lvAt + fl("clqwait", 12))
        {
            Q.lvOn = false;
            L(F("NOLA clq: leave save not confirmed after %.0f s: the leave goes on", t - Q.lvAt));
            return false;
        }
        return true;
    }

    void n7Tick()
    {
        if (fl("n7") != 1 || fl("qsync") != 1) return;
        n7St();
        if (E->isHost()) { n7Host(); return; }
        svTick();
        n7Client();
    }

    std::string csOut()
    {
        if (fl("csave") != 1 || !client()) return "";
        return "#N:" + std::to_string(Q.csLoads);
    }

    std::string n7HL()
    {
        if (fl("n7") != 1 || fl("n7early") != 1 || !E->isHost()) return "";
        n7St();
        std::string guid;
        int n = E->loadBegins(guid);
        if (n != N.hldN)
        {
            N.hldN = n;
            N.hldKh = N.kh ? *N.kh : -1;
            N.hldGuid = guid.empty() ? std::string("none") : guid;
            L(F("NOLA n7: host comecou a carregar o save %s (inicio #%d, kh=%d): avisado ao cliente agora (#HLD)", N.hldGuid.substr(0, 8).c_str(), n, N.hldKh));
        }
        return F("%d,%d,%s", N.hldN, N.hldKh, N.hldGuid.c_str());
    }

    std::string n7B()
    {
        if (fl("n7") != 1 || !E->isHost()) return "";
        n7St();
        if (!N.tag) N.tag = n7Tag(n7Path());
        return F(",%d,%d,%d,%s", N.hl, N.hs, N.kh ? *N.kh : -1, N.tag->c_str());
    }

    bool n7Rw(const std::string& q)
    {
        if (fl("n7") != 1) return false;
        n7St();
        if (N.keep.count(q))
        {
            N.keep.erase(q);
            L("NOLA n7: recompensa da missao " + q + " ja ficou com o cliente (de antes de o host voltar no tempo): NAO recebida de novo");
            return true;
        }
        N.rw[q] = now();
        return false;
    }

    void n7Follow()
    {
        n7St();
        n7FollowImpl();
    }

    bool n7Idle() { return !N.made || N.stage == "idle"; }

    bool joinBusy() { return E && client() && fl("joinport") == 1 && (J.want || J.waitT); }

    bool joinPending()
    {
        if (!E || !client() || fl("joinport") != 1 || E->peerState() != peerUp || E->peerLiveFalse()) return false;
        Id pw = pawn();
        if (!valid(pw)) return false;
        return (J.addr && *J.addr != pw) || J.want || J.waitT;
    }

    void n7DieFollow()
    {
        if (fl("n7") != 1 || !client()) return;
        n7St();
        if (!N.dieGo) L("NOLA n7: morte dos dois jogadores: a proxima carga do host leva o cliente ao save pareado do host");
        N.dieGo = true;
    }

    void n7Go(const std::string& v)
    {
        if (fl("n7go", 1) != 1 || fl("n7") != 1 || !client()) return;
        size_t c = v.find(',');
        if (c == std::string::npos || c == 0) return;
        std::string seq = v.substr(0, c);
        for (char ch : seq)
            if (ch < '0' || ch > '9') return;
        std::string rest = v.substr(c + 1);
        size_t c2 = rest.find(',');
        std::string kind = c2 == std::string::npos ? rest : rest.substr(0, c2);
        std::string who = c2 == std::string::npos ? std::string() : rest.substr(c2 + 1);
        if (kind != "alldown" && kind != "bleed") return;
        n7St();
        if (N.goSeq && *N.goSeq == seq) return;
        N.goSeq = seq;
        L("NOLA n7: #GO " + seq + " (" + kind + (who.empty() ? std::string() : ", pid " + who) + "): o grupo morreu; a proxima carga do host leva este cliente ao save pareado do host");
        n7DieFollow();
    }

    void tick100()
    {
        tick100_join();
        tick100_mid();
        tick100_kit();
    }

    void tick100_join() { joinTick(); }

    void tick100_mid()
    {
        questIn();
        csTick();
        n7Tick();
    }

    void tick100_kit() { kitTick(); }

    std::string status()
    {
        return F("save: n7 %s hl=%d hs=%d kh=%s pares=%d csLoads=%d csWait=%s rbWait=%s par=%d", N.stage.c_str(), N.hl, N.hs, ti(N.kh).c_str(), (int)V.rows.size(), Q.csLoads,
                 tb(Q.csWait.has_value()).c_str(), tb(Q.rbWait.has_value()).c_str(), (int)Q.n7Pair.kind);
    }
}
