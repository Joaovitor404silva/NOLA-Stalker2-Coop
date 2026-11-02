#include "s2m_lock.h"
#include "s2m_dlgview.h"
#include "s2m_pop.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace s2m_lock
{
    using x0::Ref;
    using x0::Vec3;
    using namespace detail;

    namespace
    {
        std::string sf(const char* fmt, ...)
        {
            char buf[1024];
            va_list ap;
            va_start(ap, fmt);
            int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
            va_end(ap);
            if (n < 0) return {};
            if (n < (int)sizeof(buf)) return std::string(buf, n);
            std::string big(n + 1, '\0');
            va_start(ap, fmt);
            std::vsnprintf(big.data(), big.size(), fmt, ap);
            va_end(ap);
            big.resize(n);
            return big;
        }

        std::string fstr(const char* key, int64_t def)
        {
            x0::Val v = x0::flag(key);
            if (v.k == x0::Val::nil) return std::to_string(def);
            return v.str();
        }

        bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
        bool digit(char c) { return c >= '0' && c <= '9'; }

        bool has(const Ref& r) { return r.raw != nullptr; }

        bool starts(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }

        bool match_q(std::string_view s, size_t pos, std::string& letters)
        {
            if (pos >= s.size() || s[pos] != 'E') return false;
            size_t i = pos + 1, d0 = i;
            while (i < s.size() && digit(s[i])) ++i;
            if (i == d0 || i >= s.size() || s[i] != '_') return false;
            ++i;
            size_t l0 = i;
            while (i < s.size() && alpha(s[i])) ++i;
            if (i == l0) return false;
            letters = std::string(s.substr(l0, i - l0));
            return true;
        }

        bool is_main_sid(std::string_view sid)
        {
            if (starts(sid, "Arch_Bossfight")) return true;
            if (starts(sid, "Arch_E"))
            {
                size_t i = 6, d0 = i;
                while (i < sid.size() && digit(sid[i])) ++i;
                if (i > d0 && i < sid.size() && sid[i] == '_') return true;
            }
            if (sid.size() > 1 && sid[0] == 'E')
            {
                size_t i = 1, d0 = i;
                while (i < sid.size() && digit(sid[i])) ++i;
                if (i > d0 && starts(sid.substr(i), "_Duga")) return true;
            }
            std::string k;
            bool got = false;
            if (starts(sid, "C_")) got = match_q(sid, 2, k);
            if (!got) got = match_q(sid, 0, k);
            if (!got) return false;
            return k == "MQ" || starts(k, "Boss") || starts(k, "Flashback");
        }

        struct ChoiceTable
        {
            int state = 0;
            std::string version;
            struct Cand
            {
                std::string g;
                double x = 0, y = 0;
                int tier = 1;
            };
            std::vector<Cand> list;
            std::unordered_map<std::string, int> use;
            std::unordered_map<std::string, int> trig;
            int nuse = 0;
        };

        struct Mine
        {
            int seq = 0;
            int64_t ts = 0;
            std::string why;
            double x = 0, y = 0, z = 0;
            double at = 0;
            bool timed = false;
            double until = 0;
        };

        struct LockView
        {
            std::string why;
            int seq = 0;
            double x = 0, y = 0, z = 0;
        };

        struct RawLock
        {
            char side = 0;
            int seq = 0;
            int64_t ts = 0;
            std::string why;
            double x = 0, y = 0, z = 0;
            int pid = -1;
        };

        struct McLead
        {
            int pid = -1;
            RawLock r;
            double since = 0;
        };

        struct PeerKill
        {
            double x = 0, y = 0, z = 0;
            double until = 0;
        };

        struct Dlg
        {
            double t0 = 0;
            std::string mode;
            bool has_q = false;
            Vec3 q;
            bool has_close = false;
            double close_at = 0;
            double held_at = 0;
            double cl = -99;
            int tries = 0;
        };

        struct KillCache
        {
            Ref ref;
            double at = 0;
            std::string g;
        };

        struct VendTrade
        {
            bool on = false;
            double at = -1;
            bool seen = false;
            bool has_t0 = false;
            double t0 = 0;
            bool has_av = false;
            int av = 0;
            std::string st;
            double t1 = 0;
        };

        std::function<bool()> g_beds_probe;
        Engine* g_eng = nullptr;

        struct MqSeen
        {
            bool has = false;
            double at = 0;
            std::string sid;
        };
        MqSeen g_mq;
    }

    struct Engine::Impl
    {
        World& W;
        ChoiceTable tab;

        int seq = 0;
        std::optional<Mine> mine;
        std::optional<PeerKill> pk;
        bool has_tie = false;
        int tie_seq = 0;
        std::optional<McLead> lo;
        std::map<int, double> lo_first;
        std::unordered_map<std::string, double> log_at;
        int deny = 0;
        std::optional<Dlg> dlg;
        bool has_av = false;
        int av = 0;

        std::unordered_map<uint64_t, KillCache> kc;
        int c_shots = 0, c_trig = 0, c_use = 0, c_blk = 0;
        std::unordered_map<std::string, double> ch_log_at;
        double use_at = -1e9;
        std::map<std::string, std::set<std::string>> offk;

        std::unordered_set<uint64_t> off;

        bool led_loaded = false;
        std::map<std::string, int64_t> led_n, led_u;
        bool led_has_addr = false;
        uint64_t led_addr = 0;
        int led_loads = 0;
        std::vector<double> led_passes;

        VendTrade vt;

        explicit Impl(World& w) : W(w) {}

        double fnum(const char* key, double def) { return x0::fd(key, def); }
        int64_t fint(const char* key, int64_t def) { return x0::fi(key, def); }

        void ld_log(const std::string& key, const std::string& msg)
        {
            double now = x0::now();
            auto it = log_at.find(key);
            double prev = it == log_at.end() ? -1e9 : it->second;
            if (now - prev < 10) return;
            log_at[key] = now;
            x0::log(msg);
        }

        void ch_log(const std::string& key, const std::string& msg)
        {
            double now = x0::now();
            auto it = ch_log_at.find(key);
            double prev = it == ch_log_at.end() ? -1e9 : it->second;
            if (now - prev < 30) return;
            ch_log_at[key] = now;
            x0::log(msg);
        }

        bool table(ChoiceTable*& out)
        {
            if (tab.state == 1)
            {
                out = &tab;
                return true;
            }
            if (tab.state == 2) return false;
            tab.state = 2;
            std::vector<x0::TsvRow> rows;
            std::string err;
            if (!x0::data_tsv("s2u_choice.tsv", rows, &err))
            {
                x0::log("NOLA choice: s2u_choice.tsv ausente ou invalido: " + err);
                return false;
            }
            struct NpcRow
            {
                double v[6] = {0, 0, 0, 0, 0, 0};
                bool has[6] = {false, false, false, false, false, false};
            };
            std::map<std::string, NpcRow> npc;
            bool has_npc = false, has_use = false, has_trig = false;
            for (auto& r : rows)
            {
                if (r.key.empty()) continue;
                const std::string& k0 = r.key[0];
                if (k0 == "V") tab.version = r.v;
                else if (k0 == "npc")
                {
                    has_npc = true;
                    if (r.key.size() == 3)
                    {
                        int i = std::atoi(r.key[2].c_str());
                        if (i >= 1 && i <= 5)
                        {
                            NpcRow& n = npc[r.key[1]];
                            n.v[i] = std::atof(r.v.c_str());
                            n.has[i] = true;
                        }
                    }
                }
                else if (k0 == "use")
                {
                    has_use = true;
                    if (r.key.size() == 2) tab.use[r.key[1]] = (int)std::atof(r.v.c_str());
                }
                else if (k0 == "trig")
                {
                    has_trig = true;
                    if (r.key.size() == 2) tab.trig[r.key[1]] = (int)std::atof(r.v.c_str());
                }
            }
            if (!has_npc || !has_use || !has_trig)
            {
                x0::log("NOLA choice: s2u_choice.tsv ausente ou invalido: tabela sem npc/use/trig");
                return false;
            }
            for (auto& kv : npc)
            {
                const NpcRow& n = kv.second;
                double squad = n.has[5] ? n.v[5] : 0;
                if (squad != 0) continue;
                ChoiceTable::Cand c;
                c.g = kv.first;
                c.x = n.has[1] ? n.v[1] : 0;
                c.y = n.has[2] ? n.v[2] : 0;
                c.tier = n.has[4] ? (int)n.v[4] : 1;
                tab.list.push_back(c);
            }
            int nu = 0;
            for (auto& kv : tab.use)
                if (kv.second == 1) ++nu;
            tab.nuse = nu;
            tab.state = 1;
            x0::log(sf("NOLA choice: tabela %s: %d NPCs de escolha (%d resolviveis), %d usaveis, %d gatilhos", tab.version.c_str(), (int)npc.size(),
                       (int)tab.list.size(), nu, (int)tab.trig.size()));
            out = &tab;
            return true;
        }

        bool ld_mc()
        {
            return fint("mclock", 1) == 1 && x0::max_clients() > 1 && (x0::is_host() ? x0::hub() : x0::roster().on);
        }

        bool ld_raw(RawLock& out)
        {
            if (!ld_mc())
            {
                std::string s;
                if (!x0::rpart("LK", s)) return false;
                return ld_parse(s, out);
            }
            if (x0::is_host())
            {
                if (!lo) return false;
                out = lo->r;
                return true;
            }
            std::string s;
            if (x0::rpart("LK", s, 0) && ld_parse(s, out))
            {
                out.pid = 0;
                return true;
            }
            if (!x0::rpart("LO", s, 0)) return false;
            size_t c = s.find(',');
            if (c == std::string::npos || c == 0) return false;
            std::string ps = s.substr(0, c);
            for (char ch : ps)
                if (!digit(ch)) return false;
            int pid = std::atoi(ps.c_str());
            if (pid == x0::my_pid()) return false;
            if (!ld_parse("c," + s.substr(c + 1), out)) return false;
            out.pid = pid;
            return true;
        }

        void ld_mc_tick()
        {
            if (!x0::is_host()) return;
            double now = x0::now();
            std::map<int, RawLock> cl;
            for (int v = 0; v < x0::views(); ++v)
            {
                std::string s;
                if (!x0::rpart("LK", s, v)) continue;
                RawLock r;
                if (!ld_parse(s, r)) continue;
                int pid = x0::view_pid(v);
                if (pid <= 0) continue;
                r.pid = pid;
                cl[pid] = r;
            }
            for (auto it = lo_first.begin(); it != lo_first.end();)
                if (!cl.count(it->first)) it = lo_first.erase(it);
                else ++it;
            for (auto& kv : cl)
                if (!lo_first.count(kv.first)) lo_first[kv.first] = now;
            if (lo && cl.count(lo->pid))
            {
                lo->r = cl[lo->pid];
                return;
            }
            if (lo)
            {
                x0::log(sf("NOLA lead mc: trava #%d (%s) do pid %d solta (%.1f s)", lo->r.seq, lo->r.why.c_str(), lo->pid, now - lo->since));
                lo.reset();
            }
            int best = -1;
            for (auto& kv : lo_first)
                if (best < 0 || kv.second < lo_first[best] || (kv.second == lo_first[best] && kv.first < best)) best = kv.first;
            if (best < 0) return;
            lo = McLead{best, cl[best], now};
            x0::log(sf("NOLA lead mc: trava #%d (%s) dada ao pid %d (primeiro a pedir, %d pedidos): ele lidera, os outros seguem", lo->r.seq, lo->r.why.c_str(), best,
                       (int)cl.size()));
        }

        std::string lo_out()
        {
            if (fint("lead", 0) != 1 || !ld_mc() || !x0::is_host() || !lo || ld_mine()) return {};
            const RawLock& r = lo->r;
            return sf("#LO:%d,%d,%lld,%s,%.0f,%.0f,%.0f", lo->pid, r.seq, (long long)r.ts, r.why.c_str(), r.x, r.y, r.z);
        }

        int ld_owner_pid()
        {
            if (fint("lead", 0) != 1) return -1;
            if (ld_mine()) return ld_mc() ? x0::my_pid() : (x0::is_host() ? 0 : 1);
            if (pk && x0::now() < pk->until) return ld_mc() ? -1 : 1;
            RawLock r;
            if (!ld_raw(r)) return -1;
            if (!ld_mc()) return x0::is_host() ? 1 : 0;
            return r.pid;
        }

        bool ld_parse(const std::string& s, RawLock& out)
        {
            std::vector<std::string> f = x0::split(s, ',', true);
            if (f.size() < 7) return false;
            if (f[0].size() != 1 || (f[0][0] != 'h' && f[0][0] != 'c')) return false;
            auto alldig = [](const std::string& a) {
                if (a.empty()) return false;
                for (char c : a)
                    if (!digit(c)) return false;
                return true;
            };
            if (!alldig(f[1]) || !alldig(f[2])) return false;
            if (f[3].empty()) return false;
            for (char c : f[3])
                if (!alpha(c)) return false;
            auto num = [](const std::string& a, double& v) {
                size_t n = 0;
                while (n < a.size() && (digit(a[n]) || a[n] == '-' || a[n] == '.')) ++n;
                if (n == 0) return false;
                std::string t = a.substr(0, n);
                char* end = nullptr;
                v = std::strtod(t.c_str(), &end);
                return end && *end == '\0';
            };
            RawLock r;
            r.side = f[0][0];
            r.seq = std::atoi(f[1].c_str());
            r.ts = std::atoll(f[2].c_str());
            r.why = f[3];
            if (!num(f[4], r.x) || !num(f[5], r.y) || !num(f[6], r.z)) return false;
            out = r;
            return true;
        }

        void ld_free(const std::string& why)
        {
            if (!mine) return;
            Mine m = *mine;
            mine.reset();
            x0::log(sf("NOLA lead: trava #%d (%s) solta: %s (%.1f s)", m.seq, m.why.c_str(), why.c_str(), x0::now() - m.at));
        }

        bool ld_stale()
        {
            if (!mine || mine->timed || mine->why != "dlg") return false;
            double age = x0::now() - mine->at;
            double lim = fnum("ldwatchs", 600);
            if (!(age > lim)) return false;
            ld_free(sf("watchdog b127: dialogo aberto ha mais de %d s (ldwatchs); o outro lado fica livre", (int)lim));
            return true;
        }

        bool ld_mine()
        {
            if (fint("lead", 0) != 1 || !mine) return false;
            if (mine->timed && x0::now() > mine->until)
            {
                ld_free("tempo");
                return false;
            }
            if (fint("ldwatch", 1) == 1 && ld_stale()) return false;
            return true;
        }

        bool ld_peer(LockView& out)
        {
            if (fint("lead", 0) != 1) return false;
            if (pk && x0::now() < pk->until)
            {
                out = LockView{"kill", 0, pk->x, pk->y, pk->z};
                return true;
            }
            RawLock r;
            if (!ld_raw(r)) return false;
            if (x0::is_host() && ld_mine()) return false;
            out = LockView{r.why, r.seq, r.x, r.y, r.z};
            return true;
        }

        bool ld_take(const std::string& why, double x, double y, double z, bool has_dur, double dur)
        {
            if (fint("lead", 0) != 1) return false;
            double now = x0::now();
            LockView p;
            if (ld_peer(p))
            {
                ld_log("seg" + why, sf("NOLA lead: %s aqui SEGURADO: o outro lado lidera (%s #%d)", why.c_str(), p.why.c_str(), p.seq));
                return false;
            }
            if (ld_mine())
            {
                if (!has_dur)
                {
                    mine->timed = false;
                    mine->why = why;
                    mine->x = x;
                    mine->y = y;
                    mine->z = z;
                }
                else if (mine->timed) mine->until = std::max(mine->until, now + dur);
                return true;
            }
            ++seq;
            Mine m;
            m.seq = seq;
            m.ts = x0::wall_ms() / 1000;
            m.why = why;
            m.x = x;
            m.y = y;
            m.z = z;
            m.at = now;
            m.timed = has_dur;
            m.until = has_dur ? now + dur : 0;
            mine = m;
            x0::log(sf("NOLA lead: trava #%d tomada (%s em %.0f,%.0f,%.0f): este lado lidera, o outro segue", seq, why.c_str(), x, y, z));
            return true;
        }

        bool ld_deny(const std::string& what)
        {
            if (!ld_mine()) return false;
            ++deny;
            ld_log("d" + what, sf("NOLA lead: %s do cliente NAO aplicado: o host lidera (trava #%d %s) [%d]", what.c_str(), mine->seq, mine->why.c_str(), deny));
            return true;
        }

        bool ld_kill(const Ref& best, const std::string& g)
        {
            if (ld_deny("tiro em NPC de historia " + g)) return true;
            double now = x0::now();
            Vec3 q;
            W.npc_pos(best, q);
            if (!(pk && now < pk->until))
                x0::log(sf("NOLA lead: tiro do cliente em NPC de historia %s aplicado: o cliente lidera %s s (o host segue)", g.c_str(), fstr("ldkill", 3).c_str()));
            pk = PeerKill{q.x, q.y, q.z, now + fnum("ldkill", 3)};
            return false;
        }

        void ld_hide(const Ref& vw, bool on, const std::string& why)
        {
            if (on)
            {
                int cur = W.answers_vis(vw);
                if (!has_av && cur >= 0)
                {
                    has_av = true;
                    av = cur;
                    x0::log("NOLA lead: respostas escondidas (" + why + ")");
                }
                if (cur >= 0 && cur != 1) W.set_answers_vis(vw, 1);
            }
            else if (has_av)
            {
                bool ok = W.set_answers_vis(vw, av);
                x0::log("NOLA lead: respostas devolvidas (" + why + ") ok=" + (ok ? "true" : "false"));
                has_av = false;
            }
        }

        void ld_dlg(double now)
        {
            Ref vw = W.dialog_view();
            if (!has(vw))
            {
                if (!dlg) return;
                Dlg& d = *dlg;
                if (d.mode == "lead" && mine && mine->why == "dlg")
                {
                    if (!d.has_close)
                    {
                        d.has_close = true;
                        d.close_at = now;
                    }
                    if (now - d.close_at < fnum("ldhold", 1)) return;
                    ld_free("dialogo fechou");
                }
                if (d.mode != "lead") x0::log("NOLA lead: dialogo fechado (" + (d.mode.empty() ? std::string("nil") : d.mode) + ")");
                dlg.reset();
                return;
            }
            if (!dlg)
            {
                dlg = Dlg{};
                dlg->t0 = now;
                if (has_av) ld_hide(vw, false, "dialogo novo");
            }
            Dlg& d = *dlg;
            d.has_close = false;
            if (d.mode.empty() && fint("qscene", 1) == 1 && W.scene_take())
            {
                d.mode = "scene";
                x0::log("NOLA lead: dialogo de cena do host: sem trava, sem fechar, respostas so do host");
            }
            if (d.mode == "scene")
            {
                if (!W.scene_take())
                {
                    ld_hide(vw, false, "a cena do host acabou");
                    d.mode.clear();
                    return;
                }
                ld_hide(vw, true, "cena do host");
                return;
            }
            if (d.mode.empty())
            {
                Vec3 q;
                bool got = W.interacted_actor(&q);
                if (!got && now - d.t0 < 2) return;
                if (got)
                {
                    d.has_q = true;
                    d.q = q;
                }
                if ((fint("vendtrade", 0) != 0 || fint("ldblk", 0) == 1) && got)
                {
                    s2m_pop::VendHit vh;
                    if (s2m_pop::vendorNear(q.x, q.y, q.z, fnum("vendtr", 1500), vh))
                    {
                        d.mode = "vend";
                        x0::log("NOLA lead: dialogo com vendedor: livre, sem trava (vendtrade/ldblk)");
                        return;
                    }
                }
                LockView p;
                if (ld_peer(p))
                {
                    d.mode = "held";
                    d.held_at = now;
                    x0::log(sf("NOLA lead: dialogo aberto com o outro lado liderando (%s #%d): SEGURADO (respostas escondidas)", p.why.c_str(), p.seq));
                }
                else
                {
                    Vec3 pp = d.has_q ? d.q : Vec3{};
                    if (ld_take("dlg", pp.x, pp.y, pp.z, false, 0)) d.mode = "lead";
                }
            }
            if (d.mode == "held")
            {
                LockView p;
                if (!ld_peer(p))
                {
                    ld_hide(vw, false, "o outro lado soltou a trava");
                    d.mode.clear();
                    return;
                }
                ld_hide(vw, true, "o outro lado lidera");
                if (now - d.held_at >= fnum("ldclose", 4) && now - d.cl >= 0.5)
                {
                    d.cl = now;
                    ++d.tries;
                    bool ok = W.close_dialog(vw);
                    if (d.tries == 1) x0::log(std::string("NOLA lead: dialogo segurado fechado (UIDialogClose) ok=") + (ok ? "true" : "false"));
                }
            }
        }

        std::string ld_out()
        {
            if (fint("lead", 0) != 1) return {};
            if (!ld_mine()) return {};
            const Mine& m = *mine;
            return sf("#LK:%s,%d,%d,%s,%.0f,%.0f,%.0f", x0::is_host() ? "h" : "c", m.seq, (int)m.ts, m.why.c_str(), m.x, m.y, m.z);
        }

        void ld_tick()
        {
            if (fint("lead", 0) != 1)
            {
                lo.reset();
                lo_first.clear();
                if (mine || dlg || has_av)
                {
                    bool had = mine || dlg || pk;
                    mine.reset();
                    dlg.reset();
                    pk.reset();
                    bool restored = false;
                    Ref vw = W.dialog_view();
                    if (has(vw) && has_av)
                    {
                        ld_hide(vw, false, "lead=0");
                        restored = true;
                    }
                    if (had || restored) x0::log("NOLA lead: desligado (lead=0)");
                }
                return;
            }
            double now = x0::now();
            if (ld_mc()) ld_mc_tick();
            else if (lo || !lo_first.empty())
            {
                lo.reset();
                lo_first.clear();
            }
            bool m = ld_mine();
            RawLock p;
            bool hp = ld_raw(p);
            if (m && hp)
            {
                if (!x0::is_host() && p.pid > 0)
                {
                    x0::log(sf("NOLA lead mc: empate: este lado e o pid %d tomaram a trava (#%d %s): o host deu ao pid %d, este lado segue", p.pid, mine->seq,
                               mine->why.c_str(), p.pid));
                    mine.reset();
                    if (dlg && dlg->mode == "lead") dlg->mode.clear();
                }
                else if (!x0::is_host())
                {
                    x0::log(sf("NOLA lead: empate: os dois tomaram a trava (cliente #%d %s, host #%d %s): o HOST vence, o cliente segue", mine->seq, mine->why.c_str(), p.seq,
                               p.why.c_str()));
                    mine.reset();
                    if (dlg && dlg->mode == "lead") dlg->mode.clear();
                }
                else if (!has_tie || tie_seq != p.seq)
                {
                    has_tie = true;
                    tie_seq = p.seq;
                    x0::log(sf("NOLA lead: empate com a trava #%d do cliente (%s): o host vence (trava #%d %s)", p.seq, p.why.c_str(), mine->seq, mine->why.c_str()));
                }
            }
            ld_dlg(now);
        }

        void ld_blk_free(const std::string& why)
        {
            int n = 0;
            for (auto& s : W.humans())
            {
                if (!off.count(s.addr)) continue;
                off.erase(s.addr);
                if (s.comp_ok && !s.active) W.set_active(s.comp, true);
                ++n;
            }
            int gone = (int)off.size();
            off.clear();
            x0::log(sf("NOLA lead: NPCs de historia liberados (%s): %d com interacao de volta, %d sumiram", why.c_str(), n, gone));
        }

        void ld_blk_tick()
        {
            bool want = fint("lead", 0) == 1 && fint("ldblk", 0) == 1;
            LockView p;
            bool hp = want && ld_peer(p);
            if (!hp)
            {
                if (!off.empty())
                    ld_blk_free(fint("ldblk", 0) != 1 ? "ldblk=0" : (fint("lead", 0) != 1 ? "lead=0" : "o outro lado soltou a trava"));
                return;
            }
            uint64_t la = 0;
            Vec3 lp;
            if (!W.local_pawn(la, lp)) return;
            double r = fnum("ldblkr", 3000);
            double r2 = r * r;
            double vr = fnum("vendtr", 1500);
            int n = 0, back = 0;
            for (auto& s : W.humans())
            {
                if (!s.comp_ok) continue;
                if (off.count(s.addr))
                {
                    if (!s.alive)
                    {
                        W.set_active(s.comp, true);
                        off.erase(s.addr);
                        ++back;
                    }
                    else if (s.active) W.set_active(s.comp, false);
                    continue;
                }
                if (!s.alive || s.is_local || s.own) continue;
                double dx = s.pos.x - lp.x, dy = s.pos.y - lp.y;
                if (dx * dx + dy * dy > r2) continue;
                s2m_pop::VendHit vh;
                if (s2m_pop::vendorNear(s.pos.x, s.pos.y, s.pos.z, vr, vh)) continue;
                if (!s.active) continue;
                W.set_active(s.comp, false);
                off.insert(s.addr);
                ++n;
                lg_note('N', s.ref);
            }
            if (n > 0) x0::log(sf("NOLA lead: %d NPCs de historia sem interacao aqui (total %d): o outro lado lidera (%s #%d)", n, (int)off.size(), p.why.c_str(), p.seq));
            if (back > 0) x0::log(sf("NOLA lead: %d NPCs bloqueados morreram: interacao de volta (corpo saqueavel)", back));
        }

        void led_load()
        {
            if (led_loaded) return;
            led_loaded = true;
            std::string txt;
            if (!x0::read_text(x0::bridge_file(L"s2_blk_ledger.txt"), txt)) return;
            for (auto& ln : x0::split(txt, '\n', false))
            {
                std::vector<std::string> f = x0::split(ln, '\t', true);
                if (f.size() < 3 || (f[0] != "N" && f[0] != "U")) continue;
                const std::string& id = f[1];
                if (id.empty()) continue;
                size_t i = id[0] == '-' ? 1 : 0;
                if (i >= id.size()) continue;
                bool okid = true;
                for (size_t k = i; k < id.size(); ++k)
                    if (!(digit(id[k]) || alpha(id[k]))) okid = false;
                if (!okid) continue;
                size_t d = 0;
                while (d < f[2].size() && digit(f[2][d])) ++d;
                if (d == 0) continue;
                int64_t ts = std::atoll(f[2].substr(0, d).c_str());
                (f[0] == "N" ? led_n : led_u)[id] = ts;
            }
        }

        void led_save()
        {
            struct E
            {
                char k;
                std::string id;
                int64_t ts;
            };
            std::vector<E> t;
            int64_t now = x0::wall_ms() / 1000;
            double keep = fnum("ldsavekeep", 604800);
            for (auto* m : {&led_n, &led_u})
            {
                char k = m == &led_n ? 'N' : 'U';
                for (auto it = m->begin(); it != m->end();)
                {
                    if ((double)(now - it->second) > keep) it = m->erase(it);
                    else
                    {
                        t.push_back(E{k, it->first, it->second});
                        ++it;
                    }
                }
            }
            std::sort(t.begin(), t.end(), [](const E& a, const E& b) {
                if (a.ts != b.ts) return a.ts > b.ts;
                return a.id < b.id;
            });
            for (size_t i = 400; i < t.size(); ++i) (t[i].k == 'N' ? led_n : led_u).erase(t[i].id);
            std::string out;
            for (size_t i = 0; i < t.size() && i < 400; ++i) out += std::string(1, t[i].k) + "\t" + t[i].id + "\t" + std::to_string(t[i].ts) + "\n";
            x0::write_text(x0::bridge_file(L"s2_blk_ledger.txt"), out, true);
        }

        void lg_note_id(char k, const std::string& id)
        {
            if (fint("ldsave", 1) < 1) return;
            led_load();
            if (id.empty()) return;
            auto& m = k == 'N' ? led_n : led_u;
            auto it = m.find(id);
            bool had = it != m.end();
            int64_t prev = had ? it->second : 0;
            int64_t now = x0::wall_ms() / 1000;
            m[id] = now;
            if (had && now - prev < 600) return;
            led_save();
        }

        void lg_note(char k, const Ref& npc)
        {
            if (fint("ldsave", 1) < 1) return;
            lg_note_id(k, W.npc_guid(npc));
        }

        void lg_scan(bool fix, const std::string& tag)
        {
            led_load();
            int npc = 0, npcOff = 0, npcLed = 0, npcFix = 0, useOff = 0, useLed = 0, useFix = 0, beds = 0, bedOff = 0;
            for (auto& s : W.humans())
            {
                if (off.count(s.addr) || !s.alive || s.own || !s.comp_ok) continue;
                ++npc;
                if (s.active) continue;
                ++npcOff;
                if (led_n.empty()) continue;
                std::string id = W.npc_guid(s.ref);
                if (!id.empty() && led_n.count(id))
                {
                    ++npcLed;
                    if (fix && W.set_active(s.comp, true))
                    {
                        ++npcFix;
                        led_n.erase(id);
                    }
                }
            }
            for (auto& c : W.inactive_use_comps())
            {
                ++useOff;
                if (led_u.count(c.hex))
                {
                    ++useLed;
                    if (fix && W.set_active(c.comp, true))
                    {
                        ++useFix;
                        led_u.erase(c.hex);
                    }
                }
            }
            bool bed_blocked = g_beds_probe && g_beds_probe();
            if (!bed_blocked) W.beds(beds, bedOff);
            if (npcFix + useFix > 0) led_save();
            std::string bedtxt = bed_blocked ? std::string("bloqueadas agora (nao contadas)") : sf("%d, sem colisao %d", beds, bedOff);
            x0::log(sf("NOLA ldsave: %s: NPCs vivos %d, sem interacao e nao nossos %d (no registro %d%s); usaveis sem interacao %d (no registro %d%s); camas %s; registro N=%d U=%d",
                       tag.c_str(), npc, npcOff, npcLed, fix ? sf(", devolvidos %d", npcFix).c_str() : "", useOff, useLed, fix ? sf(", devolvidos %d", useFix).c_str() : "",
                       bedtxt.c_str(), (int)led_n.size(), (int)led_u.size()));
        }

        void lg_tick()
        {
            if (fint("ldsave", 1) < 1) return;
            uint64_t a = 0;
            Vec3 pos;
            if (!W.local_pawn(a, pos)) return;
            led_load();
            double now = x0::now();
            double w = fnum("ldsavew", 8);
            if (!led_has_addr || a != led_addr)
            {
                led_has_addr = true;
                led_addr = a;
                ++led_loads;
                led_passes = {now + w, now + w + 25};
            }
            if (led_passes.empty() || now < led_passes[0]) return;
            led_passes.erase(led_passes.begin());
            x0::Val wv = x0::flag("ldsavew");
            if (wv.k == x0::Val::nil) wv = x0::Val::I(8);
            std::string first = wv.str();
            std::string second = wv.k == x0::Val::i ? std::to_string(wv.iv + 25) : x0::Val::F(wv.num() + 25).str();
            lg_scan(fint("ldsave", 1) >= 2, sf("pawn #%d +%s s", led_loads, (!led_passes.empty() ? first : second).c_str()));
        }

        bool guid_cand_match(const Ref& best, const std::string& hex)
        {
            Ref o = W.placeholder_actor(hex);
            return has(o) && o.same(best);
        }

        std::string ch_prot(const Ref& best)
        {
            ChoiceTable* T = nullptr;
            if (!table(T)) return {};
            double now = x0::now();
            uint64_t addr = best.address();
            auto it = kc.find(addr);
            if (it != kc.end() && it->second.ref.same(best) && now - it->second.at < fnum("n3cache", 10)) return it->second.g;
            int maxTier = (int)fint("n3kill", 2);
            std::string g;
            Vec3 q;
            if (W.npc_pos(best, q))
            {
                double r = fnum("n3r", 20000);
                double r2 = r * r;
                struct Cand
                {
                    const ChoiceTable::Cand* c;
                    double d;
                };
                std::vector<Cand> cand;
                for (auto& c : T->list)
                {
                    if (c.tier > maxTier) continue;
                    double d2 = (c.x == 0 && c.y == 0) ? 0 : (c.x - q.x) * (c.x - q.x) + (c.y - q.y) * (c.y - q.y);
                    if (d2 <= r2) cand.push_back(Cand{&c, d2});
                }
                std::sort(cand.begin(), cand.end(), [](const Cand& a, const Cand& b) {
                    if (a.d != b.d) return a.d < b.d;
                    return a.c->g < b.c->g;
                });
                size_t lim = std::min<size_t>(cand.size(), (size_t)fint("n3max", 60));
                for (size_t i = 0; i < lim; ++i)
                {
                    if (guid_cand_match(best, cand[i].c->g))
                    {
                        g = cand[i].c->g;
                        break;
                    }
                }
            }
            kc[addr] = KillCache{best, now, g};
            for (auto k = kc.begin(); k != kc.end();)
            {
                if (now - k->second.at > 60) k = kc.erase(k);
                else ++k;
            }
            return g;
        }

        bool shot_deny(const Ref& best)
        {
            if ((fint("choice", 0) != 1 && fint("lead", 0) != 1) || fint("n3kill", 2) < 1 || !x0::is_host()) return false;
            std::string g;
            try
            {
                g = ch_prot(best);
            }
            catch (const std::exception& e)
            {
                ch_log("kerr", std::string("NOLA choice: erro na identidade do NPC: ") + e.what());
                return false;
            }
            if (g.empty()) return false;
            if (fint("lead", 0) == 1) return ld_kill(best, g);
            ++c_shots;
            std::string nm = W.npc_name(best);
            if (nm.empty()) nm = "?";
            ch_log("k" + g, sf("NOLA choice: tiro do cliente em %s (npc %s) NAO aplicado: so o host decide o destino deste NPC (N3) [%d]", nm.c_str(), g.c_str(), c_shots));
            return true;
        }

        bool trig_deny(std::string_view sid, std::string_view seqs)
        {
            if ((fint("choice", 0) != 1 && fint("lead", 0) != 1) || fint("n3trig", 2) < 1) return false;
            ChoiceTable* T = nullptr;
            if (!table(T)) return false;
            auto it = T->trig.find(std::string(sid));
            if (it == T->trig.end() || it->second > fint("n3trig", 2)) return false;
            int tier = it->second;
            std::string s(sid), q(seqs);
            if (fint("lead", 0) == 1) return ld_deny("gatilho " + s + " seq=" + q);
            ++c_trig;
            ch_log("t" + s, sf("NOLA choice: gatilho do cliente %s seq=%s NAO disparado no host: escolha de historia (N3, nivel %d); o host decide ao entrar [%d]", s.c_str(), q.c_str(), tier, c_trig));
            return true;
        }

        void trig_entered(std::string_view sid, double x, double y, double z)
        {
            if (fint("lead", 0) != 1) return;
            ChoiceTable* T = nullptr;
            if (!table(T)) return;
            auto it = T->trig.find(std::string(sid));
            if (it == T->trig.end() || it->second > fint("n3trig", 2)) return;
            if (!ld_take("trig", x, y, z, true, fnum("ldtrig", 3)) && x0::is_host())
                x0::log("NOLA lead: o host entrou no gatilho de historia " + std::string(sid) + " com o cliente liderando (o jogo do host dispara sozinho: nao bloqueavel)");
        }

        bool use_deny(const Ref& obj, std::string_view key)
        {
            if ((fint("choice", 0) != 1 && fint("lead", 0) != 1) || fint("n3use", 1) != 1 || !x0::is_host()) return false;
            ChoiceTable* T = nullptr;
            if (!table(T)) return false;
            std::string hx;
            if (!W.use_hex(obj, hx)) return false;
            auto it = T->use.find(hx);
            if (it == T->use.end() || it->second != 1) return false;
            std::string k(key);
            if (fint("lead", 0) == 1) return ld_deny("uso " + k);
            ++c_use;
            ch_log("u" + k, sf("NOLA choice: uso do cliente em %s (guid %s) NAO aplicado no host: escolha de historia (N3) [%d]", k.c_str(), hx.c_str(), c_use));
            return true;
        }

        bool ld_use_gate()
        {
            LockView p;
            if (ld_peer(p)) return true;
            if (offk.empty()) return false;
            uint64_t la = 0;
            Vec3 lp;
            if (!W.local_pawn(la, lp)) return false;
            double r = fnum("n3ur", 2500) * 4;
            int n = 0;
            auto snaps = W.usables(lp, r * r, [&](const std::string& hex) { return offk.count(hex) > 0; });
            for (auto& u : snaps)
            {
                auto it = offk.find(u.hex);
                if (it == offk.end()) continue;
                std::set<std::string> names = it->second;
                offk.erase(it);
                for (auto& c : u.comps)
                    if (names.count(c.name) && W.set_active(c.ref, true)) ++n;
            }
            int left = (int)offk.size();
            if (n > 0) x0::log(sf("NOLA lead: o outro lado soltou a trava: %d prompts devolvidos (%d objetos longe ficam para depois)", n, left));
            return false;
        }

        void ch_use_tick()
        {
            if (fint("lead", 0) == 1)
            {
                if (fint("n3use", 1) != 1 || !ld_use_gate()) return;
            }
            else if (fint("choice", 0) != 1 || fint("n3use", 1) != 1 || x0::is_host()) return;
            double now = x0::now();
            if (now - use_at < fnum("n3usesec", 2)) return;
            use_at = now;
            ChoiceTable* T = nullptr;
            if (!table(T) || T->nuse == 0) return;
            uint64_t la = 0;
            Vec3 lp;
            if (!W.local_pawn(la, lp)) return;
            double r = fnum("n3ur", 2500);
            auto snaps = W.usables(lp, r * r, [&](const std::string& hex) {
                auto it = T->use.find(hex);
                return it != T->use.end() && it->second == 1;
            });
            for (auto& u : snaps)
            {
                int n = 0;
                for (auto& c : u.comps)
                {
                    if (!c.active) continue;
                    if (W.set_active(c.ref, false))
                    {
                        ++n;
                        if (fint("lead", 0) == 1) offk[u.hex].insert(c.name);
                        lg_note_id('U', u.hex);
                    }
                }
                if (n > 0)
                {
                    ++c_blk;
                    ch_log("b" + u.key, sf("NOLA choice: %s (guid %s) bloqueado no cliente: %d prompts desligados, so o host usa (N3) [%d]", u.key.c_str(), u.hex.c_str(), n, c_blk));
                }
            }
        }

        void vend_show(const Ref& vw, const std::string& why)
        {
            if (!vt.has_av || !has(vw)) return;
            bool ok = W.set_answers_vis(vw, vt.av);
            x0::log("NOLA vendtrade: respostas devolvidas (" + why + ") ok=" + (ok ? "true" : "false"));
            vt.has_av = false;
        }

        void vend_trade_tick()
        {
            if (fint("vendtrade", 0) == 0 || !x0::is_client())
            {
                if (vt.on)
                {
                    vt.on = false;
                    x0::log("NOLA vendtrade: desligado no meio (vendtrade=0)");
                }
                return;
            }
            double now = x0::now();
            if (now - vt.at < 0.2) return;
            vt.at = now;
            Ref vw = W.vend_view();
            if (!vt.on)
            {
                if (!has(vw))
                {
                    vt.seen = false;
                    return;
                }
                if (vt.has_av) vend_show(vw, "dialogo novo");
                if (vt.seen) return;
                if (!vt.has_t0)
                {
                    vt.has_t0 = true;
                    vt.t0 = now;
                }
                Vec3 q;
                bool got = W.interacted_actor(&q);
                s2m_pop::VendHit vh;
                const s2m_pop::VendHit* row = got && s2m_pop::vendorNear(q.x, q.y, q.z, fnum("vendtr", 1500), vh) ? &vh : nullptr;
                if (!got && now - vt.t0 < 2) return;
                vt.seen = true;
                vt.has_t0 = false;
                if (!row) return;
                int hint = W.trade_hint_vis(vw);
                bool tech = row->kind == "technician";
                std::string fn = tech ? "UIDialogOpenUpgrade" : "UIDialogOpenTrade";
                std::string tag = sf("%s %s a %.0f m (dica de troca visibilidade=%s)", row->kind.c_str(), row->obj.c_str(), row->dist / 100, hint >= 0 ? std::to_string(hint).c_str() : "nil");
                if (fint("vendtrade", 0) != 1)
                {
                    x0::log("NOLA vendtrade: TESTE dialogo com " + tag + " -> " + fn + " (vendtrade=2: nada feito)");
                    return;
                }
                int cur = W.answers_vis(vw);
                if (cur >= 0)
                {
                    vt.has_av = true;
                    vt.av = cur;
                    W.set_answers_vis(vw, 1);
                }
                bool ok = W.open_trade(vw, tech);
                vt.on = true;
                vt.st = "open";
                vt.t1 = now;
                x0::log("NOLA vendtrade: dialogo com " + tag + "; respostas escondidas; " + fn + " ok=" + (ok ? "true" : "false"));
                return;
            }
            if (vt.st == "open")
            {
                if (!has(vw))
                {
                    vt.st = "trade";
                    x0::log(sf("NOLA vendtrade: tela de troca aberta (%.1f s)", now - vt.t1));
                }
                else if (now - vt.t1 >= 3)
                {
                    vt.on = false;
                    vend_show(vw, "a vista do dialogo continuou visivel 3 s; sem tela de troca");
                }
            }
            else if (vt.st == "trade")
            {
                if (has(vw))
                {
                    vend_show(vw, "troca fechada");
                    bool ok = W.close_dialog(vw);
                    vt.st = "closing";
                    vt.t1 = now;
                    x0::log(std::string("NOLA vendtrade: troca fechada; UIDialogClose ok=") + (ok ? "true" : "false"));
                }
                else if (!W.interacted_actor(nullptr))
                {
                    vt.on = false;
                    x0::log("NOLA vendtrade: troca fechada e o dialogo ja tinha terminado");
                }
            }
            else if (vt.st == "closing")
            {
                if (!has(vw))
                {
                    vt.on = false;
                    x0::log(sf("NOLA vendtrade: dialogo fechado (%.1f s)", now - vt.t1));
                }
                else if (now - vt.t1 >= 2)
                {
                    vt.on = false;
                    x0::log("NOLA vendtrade: a vista nao fechou em 2 s; dialogo devolvido ao jogador");
                }
            }
        }
    };

    namespace detail
    {
        Engine::Engine(World& w) : p_(new Impl(w)) {}
        Engine::~Engine() = default;
        void Engine::vend_trade_tick() { p_->vend_trade_tick(); }
        void Engine::ld_tick() { p_->ld_tick(); }
        void Engine::ch_use_tick() { p_->ch_use_tick(); }
        void Engine::ld_blk_tick() { p_->ld_blk_tick(); }
        void Engine::lg_tick() { p_->lg_tick(); }
        std::string Engine::ld_out() { return p_->ld_out(); }
        std::string Engine::lo_out() { return p_->lo_out(); }
        int Engine::owner_pid() { return p_->ld_owner_pid(); }
        bool Engine::mine() { return p_->ld_mine(); }
        bool Engine::mine_dlg() { return p_->ld_mine() && p_->mine && p_->mine->why == "dlg"; }
        bool Engine::dlg_open() { return p_->dlg.has_value(); }
        bool Engine::peer()
        {
            LockView v;
            return p_->ld_peer(v);
        }
        std::string Engine::owner()
        {
            if (p_->ld_mine()) return x0::role();
            LockView v;
            if (p_->ld_peer(v))
            {
                RawLock r;
                if (!x0::is_host() && p_->ld_mc() && p_->ld_raw(r) && r.pid > 0) return "client";
                return x0::is_host() ? "client" : "host";
            }
            return {};
        }
        bool Engine::shot_deny(const x0::Ref& npc) { return p_->shot_deny(npc); }
        bool Engine::trig_deny(std::string_view sid, std::string_view seq) { return p_->trig_deny(sid, seq); }
        void Engine::trig_entered(std::string_view sid, double x, double y, double z) { p_->trig_entered(sid, x, y, z); }
        bool Engine::use_deny(const x0::Ref& obj, std::string_view key) { return p_->use_deny(obj, key); }
        std::string Engine::story_guid(const x0::Ref& npc) { return p_->ch_prot(npc); }
        void Engine::set_beds_probe(std::function<bool()> fn) { g_beds_probe = std::move(fn); }
        void bind(Engine* e) { g_eng = e; }
    }

    namespace
    {
        bool live() { return g_eng != nullptr && x0::port(x0::Mod::lock); }
    }

    bool lead_mine() { return live() && g_eng->mine(); }
    bool lead_dlg() { return live() && g_eng->mine_dlg(); }
    bool host_dlg_open() { return live() && g_eng->dlg_open(); }
    bool peer_leads() { return live() && g_eng->peer(); }
    int lead_owner_pid() { return live() ? g_eng->owner_pid() : -1; }

    bool shot_deny(const Ref& npc)
    {
        if (!live()) return false;
        try
        {
            return g_eng->shot_deny(npc);
        }
        catch (...)
        {
            return false;
        }
    }

    bool trig_deny(std::string_view sid, std::string_view seq)
    {
        if (!live()) return false;
        try
        {
            return g_eng->trig_deny(sid, seq);
        }
        catch (...)
        {
            return false;
        }
    }

    void trig_entered(std::string_view sid, double x, double y, double z)
    {
        if (!live()) return;
        try
        {
            g_eng->trig_entered(sid, x, y, z);
        }
        catch (...)
        {
        }
    }

    bool use_deny(const Ref& obj, std::string_view key)
    {
        if (!live()) return false;
        try
        {
            return g_eng->use_deny(obj, key);
        }
        catch (...)
        {
            return false;
        }
    }

    bool is_main(std::string_view sid) { return is_main_sid(sid); }

    void mq_seen(std::string_view sid)
    {
        if (!is_main_sid(sid)) return;
        g_mq.has = true;
        g_mq.at = x0::now();
        g_mq.sid = std::string(sid);
    }

    int mq_flag(bool episode_on, bool sleeping, bool dialog, MqEpisode& ep)
    {
        if (!episode_on)
        {
            ep.since = -1;
            ep.on = false;
            return 0;
        }
        double now = x0::now();
        if (ep.since < 0) ep.since = now;
        if (!ep.on && (sleeping || (g_mq.has && g_mq.at >= ep.since - x0::fd("mqwin", 15))))
        {
            ep.on = true;
            x0::log(sf("NOLA mq: %s do host e de MISSAO PRINCIPAL (%s)", dialog ? "dialogo" : "cinematica",
                       sleeping ? "dormindo" : sf("%s ha %.1f s", g_mq.sid.c_str(), now - g_mq.at).c_str()));
        }
        return ep.on ? 1 : 0;
    }

    bool mq_ok(bool main_episode) { return x0::fi("mqpull", 0) != 1 || main_episode; }
    bool mq_sid(std::string_view sid) { return x0::fi("mqpull", 0) != 1 || is_main_sid(sid); }
    void set_beds_blocked_probe(std::function<bool()> fn) { g_beds_probe = std::move(fn); }

    std::string story_guid(const Ref& npc)
    {
        if (!live()) return {};
        try
        {
            return g_eng->story_guid(npc);
        }
        catch (...)
        {
            return {};
        }
    }
}

#ifndef S2M_LOCK_TEST
extern "C" bool s2m_solo_scene_take();
namespace s2m_lock
{
    x0::Ref dialog_on_screen(double ttl_s)
    {
        x0::Ref on;
        for (const x0::Ref& w : x0::all_of(L"W_DialogueView_C", ttl_s))
            if (s2m_dlgview::shown(x0::call_b(w, L"IsInViewport", false), x0::call_b(w, L"IsVisible", false))) on = w;
        return on;
    }
}

extern "C" bool s2m_lock_dialog_shown()
{
    return static_cast<bool>(s2m_lock::dialog_on_screen(0.5));
}

extern "C" unsigned long long s2m_lock_dialog_view()
{
    const x0::Ref r = s2m_lock::dialog_on_screen(0.5);
    return r ? (r.address() ^ ((unsigned long long)(unsigned)r.serial << 48)) : 0ull;
}

namespace s2m_lock
{

    namespace
    {
        std::string hex_of_guid(const Ref& o)
        {
            int32_t a = 0, b = 0, c = 0, d = 0;
            if (!x0::prop<int32_t>(o, "Guid.A", a) || !x0::prop<int32_t>(o, "Guid.B", b) || !x0::prop<int32_t>(o, "Guid.C", c) || !x0::prop<int32_t>(o, "Guid.D", d)) return {};
            if (!a && !b && !c && !d) return {};
            return sf("%08X%08X%08X%08X", (uint32_t)a, (uint32_t)b, (uint32_t)c, (uint32_t)d);
        }

        std::string door_key(const Vec3& q) { return sf("%.0f,%.0f,%.0f", q.x / 10, q.y / 10, q.z / 10); }

        struct ProdWorld : World
        {
            Ref vw_;
            double vw_at_ = -99;

            bool local_pawn(uint64_t& addr, Vec3& pos) override
            {
                Ref p = x0::first_of(L"BP_Stalker2Character_C");
                if (!p) return false;
                addr = p.address();
                pos = Vec3{};
                x0::call_v(p, L"K2_GetActorLocation", pos);
                return true;
            }

            Ref dialog_view() override
            {
                double t = x0::now();
                if (!vw_.valid() || t - vw_at_ > 2)
                {
                    vw_at_ = t;
                    vw_ = dialog_on_screen();
                }
                if (!vw_.valid()) return {};
                bool on = s2m_dlgview::shown(x0::call_b(vw_, L"IsInViewport", false), x0::call_b(vw_, L"IsVisible", false));
                return on ? vw_ : Ref{};
            }

            Ref vend_view() override
            {
                return dialog_on_screen(0.2);
            }

            bool interacted_actor(Vec3* pos) override
            {
                Ref p = x0::first_of(L"BP_Stalker2Character_C");
                if (!p) return false;
                Ref a = x0::call_o(p, L"GetInteractedActor");
                if (!a) return false;
                if (pos && !x0::call_v(a, L"K2_GetActorLocation", *pos)) return false;
                return true;
            }

            int answers_vis(const Ref& view) override
            {
                Ref ac;
                if (!x0::prop<Ref>(view, "AnswerContainer", ac) || !ac) return -1;
                x0::Call c(ac, L"GetVisibility");
                if (!c.run()) return -1;
                return (int)c.ri();
            }

            bool set_answers_vis(const Ref& view, int vis) override
            {
                Ref ac;
                if (!x0::prop<Ref>(view, "AnswerContainer", ac) || !ac) return false;
                x0::Call c(ac, L"SetVisibility");
                c.i(L"InVisibility", vis);
                return c.run();
            }

            bool close_dialog(const Ref& view) override { return x0::call0(view, L"UIDialogClose"); }
            bool scene_take() override { return s2m_solo_scene_take(); }

            bool open_trade(const Ref& view, bool upgrade) override { return x0::call0(view, upgrade ? L"UIDialogOpenUpgrade" : L"UIDialogOpenTrade"); }

            int trade_hint_vis(const Ref& view) override
            {
                Ref h;
                if (!x0::prop<Ref>(view, "ButtonHintTrade", h) || !h) return -1;
                x0::Call c(h, L"GetVisibility");
                if (!c.run()) return -1;
                return (int)c.ri();
            }

            std::vector<NpcSnap> humans() override
            {
                std::vector<NpcSnap> out;
                Ref lp = x0::first_of(L"BP_Stalker2Character_C");
                for (auto& r : x0::all_of(L"BP_AI_Human_C"))
                {
                    if (!r) continue;
                    NpcSnap s;
                    s.ref = r;
                    s.addr = r.address();
                    s.alive = x0::call_b(r, L"IsAlive", false);
                    x0::call_v(r, L"K2_GetActorLocation", s.pos);
                    s.is_local = lp && lp.same(r);
                    s.own = x0::is_own_body(r);
                    Ref c;
                    if (x0::prop<Ref>(r, "NPCComponent", c) && c)
                    {
                        s.comp = c;
                        s.comp_ok = true;
                        bool a = false;
                        s.active = x0::prop<bool>(c, "bIsInteractionActive", a) && a;
                    }
                    out.push_back(s);
                }
                return out;
            }

            bool set_active(const Ref& comp, bool on) override
            {
                x0::Call c(comp, L"SetInteractionActive");
                c.b(L"bActive", on);
                return c.run();
            }

            std::string npc_guid(const Ref& npc) override
            {
                Ref cm = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
                if (!cm) return {};
                x0::Call c(cm, L"GetGUID");
                c.o(L"Actor", npc);
                if (!c.run()) return {};
                int64_t v = c.ri();
                if (v == 0) return {};
                return std::to_string(v);
            }

            bool npc_pos(const Ref& npc, Vec3& pos) override { return x0::call_v(npc, L"K2_GetActorLocation", pos); }

            std::string npc_name(const Ref& npc) override { return x0::obj_name(npc); }

            Ref placeholder_actor(const std::string& hex) override
            {
                if (hex.size() != 32) return {};
                Ref cm = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
                if (!cm) return {};
                int32_t g[4];
                for (int i = 0; i < 4; ++i) g[i] = (int32_t)(uint32_t)std::strtoul(hex.substr(i * 8, 8).c_str(), nullptr, 16);
                x0::Call c(cm, L"GetActorByGuid");
                c.raw(L"TargetGUID", g, sizeof(g));
                if (!c.run()) return {};
                return c.ro();
            }

            std::vector<UseSnap> usables(const Vec3& center, double r2, const std::function<bool(const std::string&)>& want) override
            {
                std::vector<UseSnap> out;
                std::unordered_map<void*, std::vector<CompSnap>> comps;
                bool built = false;
                for (auto& o : x0::all_of(L"InteractableObject"))
                {
                    if (!o) continue;
                    Vec3 q;
                    if (!x0::call_v(o, L"K2_GetActorLocation", q)) continue;
                    double dx = q.x - center.x, dy = q.y - center.y;
                    if (dx * dx + dy * dy > r2) continue;
                    std::string hex = hex_of_guid(o);
                    if (hex.empty() || !want(hex)) continue;
                    if (!built)
                    {
                        built = true;
                        for (auto& c : x0::all_of(L"InteractionComponent"))
                        {
                            if (!c) continue;
                            Ref ow = x0::call_o(c, L"GetOwner");
                            if (!ow) continue;
                            CompSnap cs;
                            cs.name = x0::obj_name(c);
                            cs.ref = c;
                            bool a = false;
                            cs.active = x0::prop<bool>(c, "bIsInteractionActive", a) && a;
                            comps[ow.raw].push_back(cs);
                        }
                    }
                    UseSnap u;
                    u.obj = o;
                    u.hex = hex;
                    u.key = door_key(q);
                    auto it = comps.find(o.raw);
                    if (it != comps.end()) u.comps = it->second;
                    std::sort(u.comps.begin(), u.comps.end(), [](const CompSnap& a, const CompSnap& b) { return a.name < b.name; });
                    out.push_back(std::move(u));
                }
                return out;
            }

            bool use_hex(const Ref& obj, std::string& hex) override
            {
                hex = hex_of_guid(obj);
                return !hex.empty();
            }

            std::vector<UseComp> inactive_use_comps() override
            {
                std::vector<UseComp> out;
                for (auto& c : x0::all_of(L"InteractionComponent"))
                {
                    if (!c) continue;
                    bool a = false;
                    if (x0::prop<bool>(c, "bIsInteractionActive", a) && a) continue;
                    Ref ow = x0::call_o(c, L"GetOwner");
                    if (!ow) continue;
                    std::string hex = hex_of_guid(ow);
                    if (hex.empty()) continue;
                    out.push_back(UseComp{c, hex});
                }
                return out;
            }

            bool beds(int& total, int& off) override
            {
                total = 0;
                off = 0;
                for (auto& b : x0::all_of(L"BedView"))
                {
                    if (!b) continue;
                    Ref m;
                    if (!x0::prop<Ref>(b, "BedMeshComponent", m) || !m) continue;
                    ++total;
                    x0::Call c(m, L"GetCollisionEnabled");
                    if (c.run() && c.ri() == 0) ++off;
                }
                return true;
            }
        };

        ProdWorld g_world;
        Engine* g_engine = nullptr;
    }
}

X0_MODULE(lock)
{
    using x0::Mod;
    using x0::Rate;
    static s2m_lock::detail::Engine engine(s2m_lock::g_world);
    s2m_lock::g_engine = &engine;
    s2m_lock::detail::bind(&engine);
    x0::every(Mod::lock, "vendTrade", Rate::t100, [] { s2m_lock::g_engine->vend_trade_tick(); });
    x0::every(Mod::lock, "ldTick", Rate::t100, [] { s2m_lock::g_engine->ld_tick(); });
    x0::every(Mod::lock, "chUse", Rate::t1000, [] { s2m_lock::g_engine->ch_use_tick(); });
    x0::every(Mod::lock, "ldBlk", Rate::t1000, [] { s2m_lock::g_engine->ld_blk_tick(); });
    x0::every(Mod::lock, "ldSave", Rate::t1000, [] { s2m_lock::g_engine->lg_tick(); });
    x0::own_tag(Mod::lock, "LK", [] { return s2m_lock::g_engine->ld_out(); });
    x0::own_tag(Mod::lock, "LO", [] { return s2m_lock::g_engine->lo_out(); });
    x0::iface().story_locked = [] { return s2m_lock::peer_leads(); };
    x0::iface().lead_owner = [] { return s2m_lock::g_engine->owner(); };
}

extern "C" bool s2m_story_npc(uint64_t addr, char* guid_out, int cap)
{
    if (!guid_out || cap < 1) return false;
    guid_out[0] = 0;
    try
    {
        x0::Ref r = x0::Ref::addr(addr);
        if (!r.valid()) return false;
        std::string g = s2m_lock::story_guid(r);
        if (g.empty() || (int)g.size() >= cap) return false;
        std::memcpy(guid_out, g.c_str(), g.size() + 1);
        return true;
    }
    catch (...)
    {
        return false;
    }
}
#endif
