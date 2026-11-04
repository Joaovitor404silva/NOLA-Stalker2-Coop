#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "s2m_damage.h"
#include "s2m_bodies.h"
#include "s2m_lock.h"
#include "s2m_text.h"
#include "s2x_ui.h"
#include "s2n_rel_core.inc"
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace s2m::damage
{
    namespace
    {
        using x0::Call;
        using x0::Ref;
        using x0::Rot;
        using x0::Vec3;

        const double PI = 3.14159265358979323846;

        x0::Flag F_dmgin("dmgin"), F_dmgtest("dmgtest"), F_dmgmode("dmgmode"), F_dmgap10("dmgap10"), F_dmgmax("dmgmax"), F_dmgcap("dmgcap"),
            F_dmgspawn("dmgspawn"), F_dmgfloor("dmgfloor"), F_dmgdown("dmgdown"), F_dmgdeadhold("dmgdeadhold"), F_cine("cine"), F_lifeguard("lifeguard"), F_downhp("downhp"),
            F_downsecs("downsecs"), F_downfreeze("downfreeze"), F_revivehp("revivehp"), F_reviveradius("reviveradius"),
            F_revivesecs("revivesecs"), F_downgod("downgod"), F_downdie("downdie"), F_reviveitem("reviveitem"), F_reviveheal("reviveheal"), F_bothdie("bothdie"), F_lifegod("lifegod"), F_bothdown("bothdown"), F_dieload("dieload"), F_tgtsafe("tgtsafe"), F_tgtdiag("tgtdiag"), F_shotdetect("shotdetect"), F_shotdmg("shotdmg"),
            F_shotlag("shotlag"), F_shotpad("shotpad"), F_shotattr("shotattr"), F_repsync("repsync"), F_repfoe("repfoe"), F_repecho("repecho"),
            F_repechocd("repechocd"), F_relfoe("relfoe"), F_relmir("relmir"), F_relmirs("relmirs"), F_relmirr("relmirr"), F_relmirtol("relmirtol"), F_downnoweap("downnoweap"), F_revnobag("revnobag"), F_downcam("downcam"), F_downhands("downhands"), F_downhold("downhold"), F_downfx("downfx"),
            F_friendlyfire("friendlyfire"), F_revwait("revwait"), F_ffscale("ffscale"), F_ffswallow("ffswallow"), F_meleesync("meleesync"), F_deathneutral("deathneutral"), F_testgod("testgod");

        std::string fmt(const char* f, ...)
        {
            char b[1024];
            va_list a;
            va_start(a, f);
            int n = vsnprintf(b, sizeof b, f, a);
            va_end(a);
            if (n < 0) return {};
            if (n < (int)sizeof b) return std::string(b, (size_t)n);
            std::string s((size_t)n + 1, '\0');
            va_start(a, f);
            vsnprintf(&s[0], s.size(), f, a);
            va_end(a);
            s.resize((size_t)n);
            return s;
        }

        std::string lnum(double v)
        {
            if (std::isnan(v)) return std::signbit(v) ? "-nan" : "nan";
            if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
            char b[64];
            snprintf(b, sizeof b, "%.14g", v);
            std::string s = b;
            if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
            return s;
        }

        const char* tf(bool b) { return b ? "true" : "false"; }

        bool multi() { return x0::max_clients() > 1; }
        std::string pname(int pid) { return pid == 0 ? std::string("HOST") : pid > 0 ? "PLAYER " + std::to_string(pid) : std::string("PLAYER ?"); }

        std::string after_last(const std::string& s, const char* seps)
        {
            size_t p = s.find_last_of(seps);
            return p == std::string::npos ? s : s.substr(p + 1);
        }

        bool contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

        double d2xy(const Vec3& a, double x, double y) { return (a.x - x) * (a.x - x) + (a.y - y) * (a.y - y); }

        std::vector<std::string> pieces(const std::string& s, char sep)
        {
            std::vector<std::string> out;
            size_t i = 0;
            while (i <= s.size())
            {
                size_t j = s.find(sep, i);
                if (j == std::string::npos) j = s.size();
                if (j > i) out.push_back(s.substr(i, j - i));
                i = j + 1;
            }
            return out;
        }

        Ref g_pc, g_med;

        Ref local_pc()
        {
            if (g_pc.valid()) return g_pc;
            for (const Ref& r : x0::all_of(L"PlayerController"))
                if (x0::call_b(r, L"IsLocalPlayerController", false)) { g_pc = r; return r; }
            return {};
        }

        Ref local_pawn()
        {
            Ref pc = local_pc();
            Ref p;
            if (!pc || !x0::prop<Ref>(pc, "Pawn", p) || !p.valid()) return {};
            return p;
        }

        bool player_fx(bool on, const wchar_t* sid)
        {
            if (!local_pawn()) return false;
            Ref m = x0::first_of(on ? L"CustomConsoleManagerRK" : L"CustomConsoleManagerMH");
            if (!m) return false;
            Call c(m, on ? L"XApplyEffectOnPlayer" : L"XRemoveEffectFromPlayer");
            c.s(L"EffectPrototypeSID", sid);
            return c.run();
        }

        Ref mediator()
        {
            if (!g_med.valid()) g_med = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
            return g_med;
        }

        bool guid_of(const Ref& a, int64_t& out)
        {
            Ref m = mediator();
            if (!m || !a) return false;
            Call c(m, L"GetGUID");
            c.o(L"Actor", a);
            if (!c.run()) return false;
            out = c.ri();
            return true;
        }

        double hp_of(const Ref& r, double def) { return x0::call_d(r, L"GetHP", def); }
        bool alive_of(const Ref& r) { return x0::call_b(r, L"IsAlive", false); }
        bool loc_of(const Ref& r, Vec3& v) { return x0::call_v(r, L"K2_GetActorLocation", v); }

        bool force_hp(const Ref& r, double hp)
        {
            Call c(r, L"ForceSetHP");
            c.i(L"HP", (int64_t)hp);
            return c.run();
        }

        double bleed_clear(const Ref& r)
        {
            double b = x0::call_d(r, L"GetBleeding", -1);
            if (b > 0)
            {
                Call c(r, L"ForceSetBleeding");
                c.f(L"Bleeding", 0.0f);
                c.run();
            }
            return b;
        }

        Ref movement_of(const Ref& pw)
        {
            Ref cm;
            if (!x0::prop<Ref>(pw, "CharacterMovement", cm)) return {};
            return cm;
        }

        bool move_off(const Ref& pw)
        {
            Ref cm = movement_of(pw);
            return cm && x0::call0(cm, L"DisableMovement");
        }

        bool move_on(const Ref& pw)
        {
            Ref cm = movement_of(pw);
            if (!cm) return false;
            Call c(cm, L"SetMovementMode");
            c.i(L"NewMovementMode", 1).i(L"NewCustomMode", 0);
            return c.run();
        }

        bool brief_damage(const Ref& o, double dmg, double ap, const wchar_t* bone, bool withUid, int64_t uid, int ty = 6, int src = 2)
        {
            Call c(o, L"ReceiveDamageStruct");
            c.f(L"BriefDamageData.Damage", (float)dmg)
                .f(L"BriefDamageData.ArmorDamage", 0.0f)
                .f(L"BriefDamageData.ArmorPiercing", (float)ap)
                .f(L"BriefDamageData.Bleeding", 0.0f)
                .f(L"BriefDamageData.BleedingChance", 0.0f)
                .n(L"BriefDamageData.BoneHit", bone)
                .i(L"BriefDamageData.DamageType", ty)
                .i(L"BriefDamageData.DamageSource", src);
            if (withUid) c.i(L"BriefDamageData.AttackerUID.UID", uid);
            return c.run();
        }

        bool plain_damage(const Ref& o, double dmg, const wchar_t* bone)
        {
            Call c(o, L"ReceiveDamage");
            c.f(L"InDamage", (float)dmg).i(L"InDamageType", 6).f(L"ArmorDamage", 0.0f).f(L"ArmorPiercing", 0.0f).f(L"Bleeding", 0.0f)
                .f(L"BleedingChance", 0.0f).n(L"BoneHit", bone);
            return c.run();
        }

        int g_sv = 0;

        bool tgt_uid(int64_t& out)
        {
            auto u = m01::target_uid(g_sv);
            if (!u) return false;
            out = *u;
            return true;
        }

        double g_anyShotAt = -1e9;
        int g_anyShots = 0;

        struct NCount { int ok = 0, cap = 0, dead = 0, prot = 0, down = 0; };
        struct DIn
        {
            std::unordered_map<int64_t, double> seen;
            double pruneAt = -10;
            NCount n;
            Ref pawn;
            double protectUntil = 0;
            double downUntil = 0;
            Ref deadPawn;
            double deadOk = -1;
            bool hasBudget = false;
            double budget = 0, budgetAt = 0;
            double logAt = 0;
            int total = 0;
        } DI;

        struct Chain
        {
            std::unordered_map<int64_t, double> seen;
            double pruneAt = -10;
            int64_t maxSeq = 0;
            int rx = 0, novos = 0, perdidos = 0, off = 0, nd = 0, ndAp = 0;
            double hpLost = 0;
            int64_t lastSeq = 0, lastDrop = 0;
            bool hasLast = false;
            int events = 0, ignoredN = 0, sent = 0, rotaNd = 0, ndSent = 0;
            double ndSum = 0;
            int64_t sentMax = 0, seenMax = 0;
            int foco = 0, focoMax = 0, focoRuns = 0;
            double logAt = 0, focoAt = -10;
        } CH;

        struct Life
        {
            bool down = false;
            double at = 0;
            bool hasNear = false;
            double nearAt = 0;
            int downs = 0;
            bool frozen = false;
            std::string lastBleed = "nil";
            std::string god;
            int lethal = 0;
            bool dieWait = false;
            double killAt = 0;
            int kills = 0;
            int cause = 0;
            Ref godPawn;
            bool godFail = false;
            bool gmKeep = false;
            bool dying = false;
            bool both = false;
            bool loadBlock = false;
            int loadTries = 0;
            int loadBuilds = 0;
        } LF;

        struct TestGod
        {
            Ref pawn;
            int64_t uid = 0;
            bool god = false;
            double seenMax = 100;
            int refills = 0, skipped = 0;
        } TG;

        double g_rvStart = -1;
        struct Revive
        {
            int64_t rev = 0, die = 0;
            bool seen = false;
            int64_t rRev = 0, rDie = 0;
            int64_t req = 0, reqWaitLog = 0;
            double reqAt = 0;
            bool press = false;
            bool downTxt = false;
            int downLeft = -1;
            std::string done;
            double sentUntil = -1, noUntil = -1;
            bool pd = false, pdWait = false;
            double pdNext = 0;
            int pdTries = 0;
            bool itemsLoaded = false;
            std::vector<std::string> items;
            Ref dwPawn;
            bool dwAlive = false;
            int64_t tpid = 0;
            int reqView = 0;
            bool ctx = false;
            int item = 0;
            int reqItem = 0;
            bool healLoaded = false;
            std::map<std::string, std::map<int64_t, std::string>> healFx;
            std::unordered_map<std::string, double> healHp;
            struct Trace
            {
                bool on = false;
                std::string sid;
                double t0 = 0, base = 0, expect = 0;
                size_t next = 0;
            } tr;
        } RV;

        struct VLife
        {
            std::string id;
            int ses = -1;
            bool seen = false;
            int64_t rRev = 0, rDie = 0;
            std::unordered_map<int64_t, double> shotSeen;
            double shotPruneAt = -10;
            int64_t seenMax = 0, sentMax = 0;
            std::unordered_map<int64_t, double> ffSeen;
            double ffPruneAt = -10;
            std::unordered_map<int64_t, double> mdSeen;
            double mdPruneAt = -10;
        } VL[x0::kMaxClients];

        struct MdSt
        {
            int64_t seq = 0;
            struct E { double t; std::string s; };
            std::vector<E> q;
            int sent = 0, sentDrop = 0, rx = 0, ok = 0, drop = 0, bad = 0, kill = 0;
            double sum = 0, hpLost = 0;
            std::map<std::string, int> why;
            double logAt = 0;
            int logN = 0, inLogN = 0;
            bool cfg = false;
        } MD;

        struct FfSt
        {
            int64_t seq = 0;
            struct E { double t; std::string s; };
            std::vector<E> q;
            int sent = 0, sentDrop = 0, dupPup = 0, rx = 0, ok = 0, drop = 0, bad = 0;
            int sentT = 0, okT = 0, dropT = 0;
            double sum = 0, hpLost = 0;
            std::map<std::string, int> why;
            double logAt = 0;
            int logN = 0, inLogN = 0;
            std::string bodies;
        } FF;

        struct GoSt
        {
            int64_t seq = 0;
            std::string txt;
            double at = 0, until = -1;
            bool latched = false;
            bool armed = false;
            int64_t last = 0;
            std::string hostId;
            int hostSes = -1;
            bool dying = false;
        } GO;

        struct SOut
        {
            int pending = 0;
            int64_t seq = 0;
            struct E { double t; std::string s; };
            std::vector<E> q;
        } SO;

        struct WStat { double v[5]; };
        std::map<std::string, WStat> g_weapons;
        std::map<std::string, double> g_diff;
        bool g_tabLoaded = false;

        std::unordered_map<int64_t, double> g_shotSeen;
        double g_shotPruneAt = -10;
        int g_cineShots = 0;

        struct Rep
        {
            int n = 0, kill = 0, echo = 0, fail = 0, nat = 0;
            std::map<std::string, double> at;
            Ref pa;
            bool uidOk = false;
            int64_t uid = 0;
            bool pend = false;
            int64_t pUid = 0;
            Ref pBest;
            double pAt = 0;
            int pAf = 0;
        } RP;
        struct Attr { int ok = 0, fail = 0; } AT;
        struct Foe { int n = 0, calm = 0, kill = 0, mut = 0, hum = 0, skip = 0; } FO;
        struct RelK { int n = 0, foe = 0; } RK;
        struct RelM { int n = 0, set = 0; double at = -1e9; } RM;

        Links g_links;

        std::string g_peerId;
        int g_peerSes = -1;

        void session_check()
        {
            const x0::Peer& p = x0::peer();
            if (p.id == g_peerId && p.sessions == g_peerSes) return;
            g_peerId = p.id;
            g_peerSes = p.sessions;
            DI.seen.clear();
            CH.seen.clear();
            CH.maxSeq = 0;
            g_shotSeen.clear();
        }

        bool host_cine_now() { return F_cine.i() == 1 && g_links.host_cine && g_links.host_cine(); }
        bool cine_frozen_now() { return g_links.cine_frozen && g_links.cine_frozen(); }

        bool move_back(const Ref& pw)
        {
            if (cine_frozen_now())
            {
                x0::log("NOLA LIFE: movimento fica com o congelamento da cinematica (cineRestore libera)");
                return false;
            }
            return move_on(pw);
        }

        bool num_run(const std::string& s, size_t& i, double& out)
        {
            size_t b = i;
            while (i < s.size() && ((s[i] >= '0' && s[i] <= '9') || s[i] == '-' || s[i] == '.')) ++i;
            if (i == b) return false;
            std::string t = s.substr(b, i - b);
            char* e = nullptr;
            out = std::strtod(t.c_str(), &e);
            if (e == t.c_str() || *e) out = std::nan("");
            return true;
        }

        bool parse_life(char& st, double& hp, double& mx, int view = 0)
        {
            std::string sg;
            if (!x0::rpart("L", sg, view) || sg.size() < 2 || sg[1] != ',') return false;
            char c = sg[0];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return false;
            size_t i = 2;
            if (!num_run(sg, i, hp) || i >= sg.size() || sg[i] != ',') return false;
            ++i;
            if (!num_run(sg, i, mx)) return false;
            st = c;
            return true;
        }

        void load_tables()
        {
            if (g_tabLoaded) return;
            g_tabLoaded = true;
            std::vector<x0::TsvRow> rows;
            std::string err;
            if (x0::data_tsv("weapons_main.tsv", rows, &err))
            {
                for (auto& r : rows)
                {
                    if (r.key.size() < 2) continue;
                    int i = std::atoi(r.key[1].c_str());
                    if (i < 1 || i > 5) continue;
                    auto it = g_weapons.find(r.key[0]);
                    if (it == g_weapons.end()) it = g_weapons.emplace(r.key[0], WStat{{0, 0, 0, 0, 0}}).first;
                    it->second.v[i - 1] = std::atof(r.v.c_str());
                }
            }
            else x0::log("NOLA dmg: weapons_main.tsv ilegivel (" + err + "); tiros com dano fixo");
            rows.clear();
            if (x0::data_tsv("diff_weapon_mult_main.tsv", rows, &err))
            {
                for (auto& r : rows)
                    if (!r.key.empty()) g_diff[r.key[0]] = std::atof(r.v.c_str());
            }
            else x0::log("NOLA dmg: diff_weapon_mult_main.tsv ilegivel (" + err + ")");
            x0::logf("NOLA dmg: tabelas armas=%d dificuldades=%d", (int)g_weapons.size(), (int)g_diff.size());
        }

        std::string d_out(int view = 0)
        {
            m01::Dmg& D = m01::dmg(view);
            int64_t& seenMax = view == 0 ? CH.seenMax : VL[view].seenMax;
            int64_t& sentMax = view == 0 ? CH.sentMax : VL[view].sentMax;
            double now = x0::now();
            for (auto& e : D.list)
                if (e.seq > seenMax)
                {
                    seenMax = e.seq;
                    if (route_nd()) CH.rotaNd++;
                }
            if (host_cine_now()) { D.list.clear(); return "#D:"; }
            std::vector<m01::DmgEvent> keep;
            std::string t;
            for (auto& e : D.list)
                if (now - e.at < 2)
                {
                    keep.push_back(e);
                    if (!t.empty()) t += "|";
                    t += fmt("%lld,%.1f", (long long)e.seq, e.amount);
                    if (e.seq > sentMax) { sentMax = e.seq; CH.sent++; }
                }
            D.list.swap(keep);
            return "#D:" + t;
        }

        std::string l_out()
        {
            Ref pw = local_pawn();
            double hp = -1, mx = -1;
            bool alive = true;
            if (pw)
            {
                hp = x0::call_d(pw, L"GetHP", -1);
                mx = x0::call_d(pw, L"GetMaxHP", -1);
                alive = x0::call_b(pw, L"IsAlive", true);
            }
            const char* st = "a";
            if (!alive || hp == 0) st = "d";
            else if (x0::now() < DI.downUntil) st = "w";
            return fmt("#L:%s,%.0f,%.0f", st, hp, mx);
        }

        bool parse_dev(const std::string& ev, int64_t& seq, double& amt)
        {
            size_t c = ev.find(',');
            if (c == std::string::npos || c == 0 || c + 1 >= ev.size()) return false;
            for (size_t i = 0; i < c; ++i)
                if (ev[i] < '0' || ev[i] > '9') return false;
            for (size_t i = c + 1; i < ev.size(); ++i)
                if (!((ev[i] >= '0' && ev[i] <= '9') || ev[i] == '.')) return false;
            std::string a = ev.substr(c + 1);
            char* e = nullptr;
            amt = std::strtod(a.c_str(), &e);
            if (e == a.c_str() || *e) return false;
            seq = std::strtoll(ev.substr(0, c).c_str(), nullptr, 10);
            return true;
        }

        void chain_rx(const std::string& part, double now)
        {
            if (now - CH.pruneAt >= 1)
            {
                CH.pruneAt = now;
                for (auto it = CH.seen.begin(); it != CH.seen.end();)
                {
                    if (now - it->second > 5) it = CH.seen.erase(it);
                    else ++it;
                }
            }
            for (auto& ev : pieces(part, '|'))
            {
                int64_t seq;
                double amt;
                if (!parse_dev(ev, seq, amt)) continue;
                CH.rx++;
                bool fresh = CH.seen.find(seq) == CH.seen.end();
                CH.seen[seq] = now;
                if (!fresh) continue;
                CH.novos++;
                if (CH.maxSeq && seq > CH.maxSeq + 1 && seq - CH.maxSeq < 1000) CH.perdidos += (int)(seq - CH.maxSeq - 1);
                if (seq > CH.maxSeq || CH.maxSeq - seq > 1000) CH.maxSeq = seq;
                if (F_dmgin.i() != 1) CH.off++;
                if (route_nd()) CH.nd++;
            }
        }

        bool dmg_apply(const Ref& pawn, double a)
        {
            if (F_dmgmode.i() == 2) return plain_damage(pawn, a, L"None");
            return brief_damage(pawn, a, F_dmgap10.d() / 10.0, L"None", false, 0);
        }

        void dead_track(const Ref& pawn, double now)
        {
            double hp = hp_of(pawn, 0);
            if (!alive_of(pawn) || hp <= 0) { DI.deadPawn = pawn; DI.deadOk = -1; return; }
            if (DI.deadPawn && DI.deadPawn.same(pawn))
            {
                if (DI.deadOk < 0) DI.deadOk = now;
                if (now - DI.deadOk >= F_dmgdeadhold.d()) DI.deadPawn = Ref();
            }
        }

        bool dead_held(const Ref& pawn) { return F_dmgdeadhold.d() > 0 && DI.deadPawn && DI.deadPawn.same(pawn); }

        void dmg_in()
        {
            session_check();
            double now = x0::now();
            bool runs = x0::is_client() || F_dmgtest.i() == 1;
            std::string part;
            bool has = runs && x0::rpart("D", part);
            if (has) chain_rx(part, now);
            if (F_dmgin.i() != 1 || !runs) return;
            Ref pawn = local_pawn();
            if (!pawn) return;
            if (!pawn.same(DI.pawn))
            {
                DI.pawn = pawn;
                DI.protectUntil = now + F_dmgspawn.d();
                DI.downUntil = 0;
            }
            double cap = F_dmgcap.d();
            if (!DI.hasBudget) { DI.hasBudget = true; DI.budget = cap; DI.budgetAt = now; }
            DI.budget = std::min(cap, DI.budget + cap * (now - DI.budgetAt));
            DI.budgetAt = now;
            if (now - DI.pruneAt >= 1)
            {
                DI.pruneAt = now;
                for (auto it = DI.seen.begin(); it != DI.seen.end();)
                {
                    if (now - it->second > 5) it = DI.seen.erase(it);
                    else ++it;
                }
            }
            dead_track(pawn, now);
            for (auto& ev : pieces(has ? part : std::string(), '|'))
            {
                int64_t seq;
                double amt;
                if (!parse_dev(ev, seq, amt)) continue;
                bool first = DI.seen.find(seq) == DI.seen.end();
                DI.seen[seq] = now;
                if (!first) continue;
                if (F_testgod.i() == 1)
                {
                    TG.skipped++;
                    if (TG.skipped <= 5 || TG.skipped % 50 == 0) x0::logf("NOLA testgod: dano roteado seq=%lld (%.1f) ignorado (#%d)", (long long)seq, amt, TG.skipped);
                    continue;
                }
                double hp = hp_of(pawn, 0);
                bool alive = alive_of(pawn);
                if (!alive || hp <= 0 || dead_held(pawn)) DI.n.dead++;
                else if (now < DI.protectUntil) DI.n.prot++;
                else if (now < DI.downUntil) DI.n.down++;
                else
                {
                    double a = std::min({amt, F_dmgmax.d(), DI.budget});
                    if (a < amt) DI.n.cap++;
                    if (a > 0)
                    {
                        DI.budget -= a;
                        bool ok = dmg_apply(pawn, a);
                        double hp1 = hp_of(pawn, std::nan(""));
                        double fl = F_dmgfloor.d();
                        if (fl > 0 && !std::isnan(hp1) && hp1 < fl)
                        {
                            force_hp(pawn, fl);
                            DI.downUntil = now + F_dmgdown.d();
                            x0::logf("NOLA dmgin: CAIDO (hp %.1f -> piso %lld) por %lld s", hp1, (long long)F_dmgfloor.i(), (long long)F_dmgdown.i());
                            hp1 = hp_of(pawn, std::nan(""));
                        }
                        if (!std::isnan(hp1) && hp1 <= 0) dead_track(pawn, now);
                        DI.n.ok++;
                        DI.total++;
                        if (!std::isnan(hp1)) CH.hpLost += hp - hp1;
                        x0::logf("NOLA dmgin: seq=%lld pedido=%.1f aplicado=%.1f hp %.1f -> %s ok=%s %s", (long long)seq, amt, a, hp,
                                 std::isnan(hp1) ? "nil" : lnum(hp1).c_str(), tf(ok), ok ? "" : "chamada recusada");
                    }
                }
            }
            if (now - DI.logAt >= 10)
            {
                DI.logAt = now;
                NCount& n = DI.n;
                if (n.ok + n.cap + n.dead + n.prot + n.down > 0)
                    x0::logf("NOLA dmgin 10s: aplicados=%d cortados_pelo_teto=%d ignorados morto=%d protecao=%d caido=%d", n.ok, n.cap, n.dead, n.prot, n.down);
                DI.n = NCount{};
            }
        }

        bool shot_ray_hit(const Ref& o, double ox, double oy, double oz, double dx, double dy, double dz, double& bt, double& bh)
        {
            Vec3 q;
            if (!loc_of(o, q)) return false;
            double t0 = (q.x - ox) * dx + (q.y - oy) * dy + (q.z - oz) * dz;
            if (t0 <= -1000 || t0 > 31000) return false;
            double ex = ox + dx * t0 - q.x, ey = oy + dy * t0 - q.y, ez = oz + dz * t0 - q.z;
            if (ex * ex + ey * ey + ez * ez > 800.0 * 800.0) return false;
            double rr = 0, hh = 0;
            Ref c;
            if (x0::prop<Ref>(o, "CapsuleComponent", c) && c.valid())
            {
                rr = x0::call_d(c, L"GetScaledCapsuleRadius", 0);
                hh = x0::call_d(c, L"GetScaledCapsuleHalfHeight", 0);
            }
            double pad = F_shotpad.d();
            rr = std::max(45.0, rr + pad);
            hh = std::max(95.0, hh + pad);
            Vec3 v;
            if (!x0::call_v(o, L"GetVelocity", v)) v = Vec3{};
            double L = F_shotlag.d() / 1000.0;
            bool found = false;
            double bd = 0;
            for (int k = 0; k <= 3; ++k)
            {
                double px = q.x - v.x * L * k / 3, py = q.y - v.y * L * k / 3, pz = q.z - v.z * L * k / 3;
                double t = (px - ox) * dx + (py - oy) * dy + (pz - oz) * dz;
                if (t > 0 && t <= 30000)
                {
                    double hx = ox + dx * t, hy = oy + dy * t, hz = oz + dz * t;
                    double d = (hx - px) * (hx - px) + (hy - py) * (hy - py);
                    if (d <= rr * rr && std::fabs(hz - pz) <= hh && (!found || d < bd))
                    {
                        found = true;
                        bt = t;
                        bh = hz - pz;
                        bd = d;
                    }
                }
            }
            if (found && !contains(x0::full_name(o), "BP_AI_Human")) bh = 0;
            return found;
        }

        bool rep_uid(const Ref& pawn, const Ref& best, int64_t& out)
        {
            if (F_repsync.i() != 1 || !pawn || !best) return false;
            RP.pend = false;
            if (!RP.pa.same(pawn))
            {
                RP.pa = pawn;
                RP.uidOk = false;
                int64_t u;
                if (guid_of(pawn, u))
                {
                    RP.uidOk = true;
                    RP.uid = u;
                    x0::logf("NOLA rep: tiros do cliente em NPCs contam como do host (uid do host=%lld)", (long long)u);
                }
                else x0::log("NOLA rep: uid do host ilegivel (chamada recusada); tiros do cliente seguem o caminho antigo");
            }
            if (!RP.uidOk) return false;
            RP.pend = true;
            RP.pUid = RP.uid;
            RP.pBest = best;
            RP.pAt = x0::now();
            RP.pAf = AT.fail;
            out = RP.uid;
            return true;
        }

        std::string dead_txt(const Ref& o)
        {
            bool w = x0::call_b(o, L"IsWounded", false);
            bool fw = x0::call_b(o, L"IsFallingWounded", false);
            if (w || fw) return " FERIDO (estado ferido do jogo; morre se levar outro tiro)";
            return " MORTO";
        }

        void rep_after(const Ref& best, const std::string& sid, double h0, double h1, bool okD)
        {
            if (!RP.pend) return;
            RP.pend = false;
            if (!best) return;
            if (!RP.pBest.same(best) || x0::now() - RP.pAt > 1) return;
            if (!okD || AT.fail > RP.pAf)
            {
                RP.fail++;
                if (RP.fail <= 3) x0::log("NOLA rep: acerto em nome do host recusado; o tiro do cliente nao contou para a reputacao do host");
                return;
            }
            RP.n++;
            bool alive = h1 > 0;
            bool dead = h0 > 0 && h1 >= 0 && h1 <= 0.001;
            if (dead) RP.kill++;
            if (RP.n <= 5 || RP.n % 50 == 0 || dead)
                x0::logf("NOLA rep: tiro do cliente em %s contado como do host (uid=%lld): hp %.1f -> %.1f%s (%d acertos, %d mortes)", sid.c_str(),
                         (long long)RP.pUid, h0, h1, dead ? dead_txt(best).c_str() : "", RP.n, RP.kill);
            if (F_shotattr.i() != 1 || !alive || F_repecho.d() <= 0) return;
            int64_t tu;
            if (!tgt_uid(tu) || tu == RP.pUid) return;
            double now = x0::now();
            auto it = RP.at.find(sid);
            if (it != RP.at.end() && now - it->second < F_repechocd.d()) return;
            RP.nat++;
            if (RP.nat > 200) { RP.at.clear(); RP.nat = 1; }
            RP.at[sid] = now;
            if (brief_damage(best, F_repecho.d(), 0.0, L"None", true, tu))
            {
                RP.echo++;
                if (RP.echo <= 3 || RP.echo % 50 == 0)
                    x0::logf("NOLA rep: eco em nome do alvo do cliente (uid=%lld) em %s: %.1f dano (%d ecos)", (long long)tu, sid.c_str(), F_repecho.d(), RP.echo);
            }
            else
            {
                RP.fail++;
                if (RP.fail <= 3) x0::log("NOLA rep: eco recusado: chamada recusada");
            }
        }

        bool rel_host(const Ref& pawn, int64_t& hu)
        {
            if (RP.uidOk) { hu = RP.uid; return true; }
            return guid_of(pawn, hu);
        }

        int rel_kind(const Ref& pawn, const Ref& best, std::string& txt)
        {
            if (F_relfoe.i() != 1 || !best) return -1;
            int64_t hu, bu;
            if (!rel_host(pawn, hu) || !guid_of(best, bu)) return -1;
            int32_t v = 0;
            int lev = -1;
            if (s2rel::get((uint32_t)bu, (uint32_t)hu, v, lev) != 0) return -1;
            RK.n++;
            if (lev == 0) RK.foe++;
            if (RK.n <= 5 || RK.n % 50 == 0)
                x0::logf("NOLA relfoe: vitima uid=%lld -> host uid=%lld relacao %d nivel %d -> %s (%d lidos, %d inimigos)", (long long)bu, (long long)hu, (int)v,
                         lev, lev == 0 ? "nome do alvo do cliente" : "nome do host", RK.n, RK.foe);
            if (lev == 0) { txt = "inimigo do host rel=" + std::to_string(v); return 1; }
            return 0;
        }

        bool foe_uid(const Ref& pawn, const Ref& best, double dmg, double h0, int64_t& out)
        {
            int64_t m = F_repfoe.i();
            if (m <= 0 || !best) return false;
            int64_t tu;
            if (!m01::target(g_sv).valid() || m01::target_parked(g_sv) || !tgt_uid(tu)) { FO.skip++; return false; }
            std::string full = x0::full_name(best);
            std::string kind;
            int k = rel_kind(pawn, best, kind);
            if (k == 0) kind.clear();
            else if (k < 0)
            {
                if (contains(full, "BP_AI_") && !contains(full, "BP_AI_Human")) kind = "mutante";
                else
                {
                    Ref m2 = mediator();
                    Ref f;
                    if (m2)
                    {
                        Call c(m2, L"GetFocusedEnemy");
                        c.o(L"Obj", best);
                        if (c.run()) f = c.ro();
                    }
                    if (f.valid())
                    {
                        for (const Ref& o : {pawn, m01::target(g_sv), m01::puppet(g_sv)})
                            if (o.valid() && o.same(f)) { kind = "humano hostil"; break; }
                        if (kind.empty() && multi() && m01::is_m1_body(f)) kind = "humano hostil";
                    }
                }
            }
            std::string shortName = after_last(full, ".:");
            if (kind.empty()) { FO.calm++; return false; }
            if (!(h0 > 0) || dmg >= h0)
            {
                FO.kill++;
                if (FO.kill <= 3 || FO.kill % 50 == 0)
                    x0::logf("NOLA repfoe: golpe final/hp ilegivel em %s fica em nome do host (dano %.1f hp %.1f, %d)", shortName.c_str(), dmg, h0, FO.kill);
                return false;
            }
            FO.n++;
            if (kind == "mutante") FO.mut++;
            else FO.hum++;
            if (FO.n <= 5 || FO.n % 50 == 0)
                x0::logf("NOLA repfoe: acerto do cliente em %s (%s) em nome do alvo uid=%lld%s (%d: mutantes=%d humanos=%d calmos=%d finais=%d)", shortName.c_str(),
                         kind.c_str(), (long long)tu, m == 2 ? " [so log]" : "", FO.n, FO.mut, FO.hum, FO.calm, FO.kill);
            if (m != 1) return false;
            RP.pend = false;
            out = tu;
            return true;
        }

        const char* bone_txt(const wchar_t* b)
        {
            if (std::wstring(b) == L"jnt_head") return "jnt_head";
            if (std::wstring(b) == L"jnt_l_leg") return "jnt_l_leg";
            return "None";
        }

        bool pick_attacker(const Ref& pawn, const Ref& best, double dmg, double h0, int64_t& uid)
        {
            bool has = false;
            if (F_shotattr.i() == 1 && tgt_uid(uid)) has = true;
            int64_t rU;
            if (rep_uid(pawn, best, rU)) { uid = rU; has = true; }
            int64_t fU;
            if (foe_uid(pawn, best, dmg, h0, fU)) { uid = fU; has = true; }
            return has;
        }

        void shot_resolve(const std::string& str, const Ref& pawn)
        {
            std::vector<double> v;
            for (auto& p : pieces(str, ';'))
            {
                char* e = nullptr;
                double x = std::strtod(p.c_str(), &e);
                if (e == p.c_str()) break;
                v.push_back(x);
            }
            if (v.size() < 8) return;
            double nowS = x0::now();
            if (nowS - g_shotPruneAt >= 1)
            {
                g_shotPruneAt = nowS;
                for (auto it = g_shotSeen.begin(); it != g_shotSeen.end();)
                {
                    if (nowS - it->second > 5) it = g_shotSeen.erase(it);
                    else ++it;
                }
            }
            auto& shotSeen = g_sv == 0 ? g_shotSeen : VL[g_sv].shotSeen;
            double& shotPruneAt = g_sv == 0 ? g_shotPruneAt : VL[g_sv].shotPruneAt;
            if (g_sv != 0 && nowS - shotPruneAt >= 1)
            {
                shotPruneAt = nowS;
                for (auto it = shotSeen.begin(); it != shotSeen.end();)
                {
                    if (nowS - it->second > 5) it = shotSeen.erase(it);
                    else ++it;
                }
            }
            int64_t seq = (int64_t)v[0];
            bool seen = shotSeen.find(seq) != shotSeen.end();
            shotSeen[seq] = nowS;
            if (seen) return;
            if (host_cine_now()) { g_cineShots++; return; }
            double ox = v[1], oy = v[2], oz = v[3], dx = v[4], dy = v[5], dz = v[6], dmg = v[7];
            Ref body = m01::puppet(), body2 = m01::target();
            Ref best;
            double bt = 1e18, bh = 0;
            int n = 0;
            bool lag = F_shotlag.d() > 0;
            for (const Ref& o : x0::all_of(L"Pawn"))
            {
                if (!o.valid() || o.same(pawn) || (body.valid() && o.same(body)) || (body2.valid() && o.same(body2)) || (multi() && m01::is_m1_body(o))) continue;
                if (!contains(x0::full_name(o), "BP_AI_") || !alive_of(o)) continue;
                n++;
                if (lag)
                {
                    double tt, th;
                    if (shot_ray_hit(o, ox, oy, oz, dx, dy, dz, tt, th) && tt < bt) { best = o; bt = tt; bh = th; }
                    continue;
                }
                Vec3 q;
                if (!loc_of(o, q)) continue;
                double t = (q.x - ox) * dx + (q.y - oy) * dy + (q.z - oz) * dz;
                if (t <= 0 || t > 30000) continue;
                double hx = ox + dx * t, hy = oy + dy * t, hz = oz + dz * t;
                double dh = std::sqrt((hx - q.x) * (hx - q.x) + (hy - q.y) * (hy - q.y));
                if (dh <= 45 && std::fabs(hz - q.z) <= 95 && t < bt) { best = o; bt = t; bh = hz - q.z; }
            }
            if (!best) { x0::logf("SHOT seq=%lld hit=nenhum (testados=%d)%s", (long long)seq, n, multi() ? (" de " + pname(x0::view_pid(g_sv))).c_str() : ""); return; }
            if (g_links.shot_deny ? g_links.shot_deny(best) : s2m_lock::shot_deny(best)) return;
            std::string sid = after_last(x0::full_name(best), ".");
            double h0 = hp_of(best, -1), h1 = -1;
            const wchar_t* bone = bh > 60 ? L"jnt_head" : L"None";
            bool okD = false;
            if (v.size() >= 12)
            {
                double ap = v[8], eff = v[9], drop = v[10], minmod = v[11];
                double fall = 1.0;
                if (bt >= eff + drop) fall = minmod;
                else if (bt > eff && drop > 0) fall = 1.0 - (1.0 - minmod) * (bt - eff) / drop;
                double coef = 1.0;
                if (bh > 60) coef = 6.0;
                else if (bh < -10) { coef = 0.7; bone = L"jnt_l_leg"; }
                dmg = dmg * fall * coef * (v.size() >= 13 ? v[12] : 1.0);
                int64_t uid = 0;
                bool hasUid = pick_attacker(pawn, best, dmg, h0, uid);
                if (hasUid)
                {
                    okD = brief_damage(best, dmg, ap, bone, true, uid);
                    if (okD)
                    {
                        AT.ok++;
                        if (AT.ok <= 5 || AT.ok % 50 == 0) x0::logf("NOLA shotattr: acerto do cliente em nome do alvo uid=%lld (%d)", (long long)uid, AT.ok);
                    }
                    else
                    {
                        AT.fail++;
                        if (AT.fail <= 3) x0::log("NOLA shotattr: AttackerUID recusado, chamada antiga: chamada recusada");
                    }
                }
                if (!hasUid || !okD) okD = brief_damage(best, dmg, ap, bone, false, 0);
            }
            else okD = plain_damage(best, dmg, bone);
            h1 = hp_of(best, -1);
            rep_after(best, sid, h0, h1, okD);
            x0::logf("SHOT(core) seq=%lld hit=%s dist=%.1fm altura=%.0f osso=%s dano=%.1f hp %.1f -> %.1f ok=%s %s%s", (long long)seq, sid.c_str(), bt / 100, bh,
                     bone_txt(bone), dmg, h0, h1, tf(okD), okD ? "" : "chamada recusada", multi() ? (" de " + pname(x0::view_pid(g_sv))).c_str() : "");
        }

        void vl_session(int v);

        void shots_in()
        {
            if (!x0::is_host()) return;
            session_check();
            if (multi())
            {
                Ref pawn;
                bool got = false;
                x0::each_view([&](int v, const x0::Snap& s, const x0::Peer&) {
                    vl_session(v);
                    if (!s.ok || s.shots.empty()) return;
                    if (!got) { pawn = local_pawn(); got = true; }
                    if (!pawn) return;
                    g_sv = v;
                    for (auto& one : pieces(s.shots, '|')) shot_resolve(one, pawn);
                    g_sv = 0;
                });
                return;
            }
            const x0::Snap& s = x0::remote();
            if (!s.ok || s.shots.empty()) return;
            Ref pawn = local_pawn();
            if (!pawn) return;
            for (auto& one : pieces(s.shots, '|')) shot_resolve(one, pawn);
        }

        bool camera_ray(Vec3& o, Vec3& d)
        {
            Ref pc = local_pc();
            Ref cm;
            if (!pc || !x0::prop<Ref>(pc, "PlayerCameraManager", cm) || !cm.valid()) return false;
            Rot r;
            Call cl(cm, L"GetCameraLocation");
            if (!cl.run() || !cl.rv(o)) return false;
            Call cr(cm, L"GetCameraRotation");
            if (!cr.run() || !cr.rr(r)) return false;
            double cp = std::cos(r.pitch * PI / 180), yw = r.yaw * PI / 180;
            d.x = cp * std::cos(yw);
            d.y = cp * std::sin(yw);
            d.z = std::sin(r.pitch * PI / 180);
            return true;
        }

        void on_shot_sound(const Ref& self)
        {
            if (F_tgtsafe.i() == 1 || F_tgtdiag.i() == 1) { g_anyShotAt = x0::now(); g_anyShots++; }
            Ref lp = local_pawn();
            if (!lp || !self) return;
            Ref ow = x0::call_o(self, L"GetOwner");
            if (!ow.valid() || !ow.same(lp)) return;
            if (F_shotdetect.i() == 1) SO.pending++;
            Vec3 o, d;
            if (!camera_ray(o, d)) return;
            m01::note_local_shot(o, d);
        }

        void shots_out_tick()
        {
            if (F_shotdetect.i() != 1) return;
            Ref pawn = local_pawn();
            if (!pawn) return;
            int n = SO.pending;
            if (n == 0) return;
            SO.pending = 0;
            Vec3 o, d;
            if (!camera_ray(o, d)) { x0::log("SHOT camera erro camera ilegivel"); return; }
            load_tables();
            std::string wname = "?";
            const WStat* ws = nullptr;
            Ref w = x0::call_o(pawn, L"GetWeaponInHandsMeshComponent");
            if (w.valid())
            {
                Ref a = x0::call_o(w, L"GetSkeletalMeshAsset");
                if (a.valid())
                {
                    wname = after_last(x0::full_name(a), ".");
                    auto it = g_weapons.find(wname);
                    if (it != g_weapons.end()) ws = &it->second;
                }
            }
            std::string tail = fmt("%.1f", F_shotdmg.d());
            if (ws)
            {
                auto it = g_diff.find(x0::flag("difficulty").str());
                double mult = it != g_diff.end() ? it->second : 1.0;
                tail = fmt("%.2f;%.2f;%.0f;%.0f;%.3f;%.2f", ws->v[0], ws->v[1], ws->v[2], ws->v[3], ws->v[4], mult);
            }
            double now = x0::now();
            for (int k = 0; k < n; ++k)
            {
                SO.seq++;
                SO.q.push_back({now, fmt("%lld;%.1f;%.1f;%.1f;%.5f;%.5f;%.5f;", (long long)SO.seq, o.x, o.y, o.z, d.x, d.y, d.z) + tail});
            }
            x0::logf("SHOT real: %d disparo(s), seq ate %lld, arma=%s (%s) origem %.0f,%.0f,%.0f dir %.3f,%.3f,%.3f", n, (long long)SO.seq, wname.c_str(),
                     ws ? "tabela" : "dano fixo", o.x, o.y, o.z, d.x, d.y, d.z);
        }

        std::string shots_field()
        {
            double now = x0::now();
            std::vector<SOut::E> keep;
            std::string out;
            for (auto& e : SO.q)
                if (now - e.t < 1.5)
                {
                    keep.push_back(e);
                    if (!out.empty()) out += "|";
                    out += e.s;
                }
            SO.q.swap(keep);
            return out;
        }

        bool god_set(int64_t uid, bool on)
        {
            Ref m = x0::first_of(L"CustomConsoleManagerBuild");
            if (!m) return false;
            Call c(m, L"XSetEntityUnkillableByUID");
            c.i(L"EntityUID", uid).b(L"bGodModeState", on);
            return c.run();
        }

        bool peer_in() { return x0::coop(); }

        bool god_always() { return F_lifegod.i() == 1 && F_lifeguard.i() == 1 && peer_in(); }

        void gm_keep(bool on)
        {
            if (!on && TG.god) return;
            if (LF.gmKeep == on) return;
            LF.gmKeep = on;
            bool ok = s2m_life_gm_keep(on);
            x0::logf("NOLA LIFE: GameManager god off %s o bit 0x2 do jogador (ok=%s)", on ? "agora poupa" : "volta a limpar", tf(ok));
        }

        void god_take(const Ref& pw, bool always)
        {
            if (!pw.same(LF.godPawn))
            {
                LF.godPawn = pw;
                LF.god.clear();
                LF.godFail = false;
            }
            if (!LF.god.empty()) return;
            if (!always && F_downgod.i() != 1) return;
            int64_t uid = 0;
            bool ok = guid_of(pw, uid) && god_set(uid, true);
            if (ok) LF.god = std::to_string((long long)uid);
            if (ok) gm_keep(true);
            if (!ok && LF.godFail) return;
            LF.godFail = !ok;
            if (always)
                x0::logf("NOLA LIFE: vivo protegido Unkillable (lifegod: nenhum golpe mata, golpe letal = caido) uid=%s ok=%s", ok ? LF.god.c_str() : "nil", tf(ok));
            else
                x0::logf("NOLA LIFE: caido protegido Unkillable uid=%s ok=%s", ok ? LF.god.c_str() : "nil", tf(ok));
        }

        void god_drop(const char* why)
        {
            if (LF.god.empty()) return;
            std::string uid = LF.god;
            LF.god.clear();
            gm_keep(false);
            if (g_links.god_other && g_links.god_other())
            {
                x0::logf("NOLA LIFE: Unkillable uid=%s fica (%s): cinematica/emissao tambem segura", uid.c_str(), why);
                return;
            }
            if (TG.god && std::to_string((long long)TG.uid) == uid)
            {
                x0::logf("NOLA LIFE: Unkillable uid=%s fica (%s): testgod tambem segura", uid.c_str(), why);
                return;
            }
            bool ok = god_set(std::strtoll(uid.c_str(), nullptr, 10), false);
            x0::logf("NOLA LIFE: Unkillable uid=%s desligado (%s) ok=%s", uid.c_str(), why, tf(ok));
        }

        void testgod_release(const char* why)
        {
            if (!TG.god) return;
            TG.god = false;
            bool held = LF.god == std::to_string((long long)TG.uid) || (g_links.god_other && g_links.god_other());
            bool ok = held || god_set(TG.uid, false);
            if (LF.god.empty()) gm_keep(false);
            x0::logf("NOLA testgod: DESLIGADO uid=%lld (%s) ok=%s%s; %d recargas, %d danos roteados ignorados", (long long)TG.uid, why, tf(ok), held ? " (Unkillable fica: lifegod/cinematica segura)" : "", TG.refills, TG.skipped);
            TG.refills = 0;
            TG.skipped = 0;
        }

        void testgod_tick()
        {
            if (F_testgod.i() != 1)
            {
                testgod_release("testgod 0");
                TG.pawn = Ref();
                return;
            }
            Ref pw = local_pawn();
            if (!pw)
            {
                testgod_release("sem pawn");
                TG.pawn = Ref();
                return;
            }
            if (!pw.same(TG.pawn))
            {
                testgod_release("pawn trocado");
                TG.pawn = pw;
                TG.seenMax = 100;
                TG.uid = 0;
            }
            double hp = hp_of(pw, std::nan(""));
            if (std::isnan(hp) || !alive_of(pw)) return;
            if (hp > TG.seenMax) TG.seenMax = hp;
            if (!TG.god && guid_of(pw, TG.uid) && god_set(TG.uid, true))
            {
                TG.god = true;
                gm_keep(true);
                x0::logf("NOLA testgod: LIGADO uid=%lld: Unkillable + hp de volta ao maximo a cada tick, dano roteado e queda ignorados", (long long)TG.uid);
            }
            double mx = x0::call_d(pw, L"GetMaxHP", -1);
            if (mx <= 0) mx = TG.seenMax;
            if (hp >= mx) return;
            force_hp(pw, mx);
            TG.refills++;
            if (TG.refills <= 5 || TG.refills % 50 == 0) x0::logf("NOLA testgod: hp %.1f -> %.1f (recarga #%d)", hp, mx, TG.refills);
        }

        std::string rv_out()
        {
            if (multi())
            {
                if (RV.item > 0) return fmt("#RV:%lld,%lld,%lld,%d", (long long)RV.rev, (long long)RV.die, (long long)RV.tpid, RV.item);
                return fmt("#RV:%lld,%lld,%lld", (long long)RV.rev, (long long)RV.die, (long long)RV.tpid);
            }
            if (RV.item > 0) return fmt("#RV:%lld,%lld,%d", (long long)RV.rev, (long long)RV.die, RV.item);
            return fmt("#RV:%lld,%lld", (long long)RV.rev, (long long)RV.die);
        }

        bool rv_fields(const std::string& s, size_t lo, size_t hi, std::vector<int64_t>& f)
        {
            f.clear();
            size_t b = 0;
            while (true)
            {
                size_t e = s.find(',', b);
                std::string p = s.substr(b, e == std::string::npos ? std::string::npos : e - b);
                if (p.empty() || p.size() > 18) return false;
                for (char ch : p)
                    if (ch < '0' || ch > '9') return false;
                f.push_back(std::strtoll(p.c_str(), nullptr, 10));
                if (e == std::string::npos) break;
                b = e + 1;
            }
            return f.size() >= lo && f.size() <= hi;
        }

        bool parse_rv(int64_t& rev, int64_t& die, int64_t& item)
        {
            std::string s;
            std::vector<int64_t> f;
            if (!x0::rpart("RV", s) || !rv_fields(s, 2, 3, f)) return false;
            rev = f[0];
            die = f[1];
            item = f.size() == 3 ? f[2] : 0;
            return true;
        }

        const std::vector<std::string>& rv_items()
        {
            if (RV.itemsLoaded) return RV.items;
            RV.itemsLoaded = true;
            std::vector<x0::TsvRow> rows;
            std::string err;
            if (x0::data_tsv("revive_items.tsv", rows, &err))
                for (auto& r : rows)
                    if (!r.v.empty()) RV.items.push_back(r.v);
            x0::logf("NOLA LIFE: itens de reanimar (Item.WoundedHeal): %d%s", (int)RV.items.size(), RV.items.empty() ? (" (" + err + ")").c_str() : "");
            return RV.items;
        }

        int rv_item_index(const std::string& sid)
        {
            const std::vector<std::string>& v = rv_items();
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i] == sid) return (int)i + 1;
            return 0;
        }

        void heal_load()
        {
            if (RV.healLoaded) return;
            RV.healLoaded = true;
            std::vector<x0::TsvRow> rows;
            std::string err;
            if (!x0::data_tsv("revive_heal.tsv", rows, &err))
            {
                x0::logf("NOLA LIFE: tabela de cura dos itens ilegivel (%s): reanimar usa revivehp", err.c_str());
                return;
            }
            for (auto& r : rows)
            {
                if (r.key.size() != 2) continue;
                if (r.ktype.size() == 2 && r.ktype[1] == 'i') RV.healFx[r.key[0]][std::strtoll(r.key[1].c_str(), nullptr, 10)] = r.v;
                else if (r.key[1] == "hp") RV.healHp[r.key[0]] = std::strtod(r.v.c_str(), nullptr);
            }
            x0::logf("NOLA LIFE: cura dos itens de reanimar lida da tabela: %d itens", (int)RV.healFx.size());
        }

        bool revive_heal(const Ref& pw, int idx)
        {
            const std::vector<std::string>& items = rv_items();
            if (idx < 1 || (size_t)idx > items.size())
            {
                x0::logf("NOLA LIFE: reviveheal: item #%d do pedido nao existe na tabela (%d itens): usa revivehp", idx, (int)items.size());
                return false;
            }
            const std::string& sid = items[idx - 1];
            heal_load();
            auto it = RV.healFx.find(sid);
            if (it == RV.healFx.end() || it->second.empty())
            {
                x0::logf("NOLA LIFE: reviveheal: %s sem efeitos na tabela de cura: usa revivehp", sid.c_str());
                return false;
            }
            Ref rk = x0::first_of(L"CustomConsoleManagerRK");
            if (!rk)
            {
                x0::logf("NOLA LIFE: reviveheal: %s sem CustomConsoleManagerRK: usa revivehp", sid.c_str());
                return false;
            }
            double hp0 = hp_of(pw, std::nan(""));
            force_hp(pw, F_downhp.d() + 1);
            int ok = 0;
            std::string list;
            for (auto& e : it->second)
            {
                Call c(rk, L"XApplyEffectOnPlayer");
                c.s(L"EffectPrototypeSID", std::wstring(e.second.begin(), e.second.end()));
                bool r = c.run();
                if (r) ok++;
                list += (list.empty() ? "" : ",") + e.second + (r ? "" : "(recusado)");
            }
            if (ok != (int)it->second.size())
            {
                x0::logf("NOLA LIFE: reviveheal: %s efeitos %s: so %d de %d aplicados: usa revivehp", sid.c_str(), list.c_str(), ok, (int)it->second.size());
                return false;
            }
            auto hh = RV.healHp.find(sid);
            RV.tr.on = true;
            RV.tr.sid = sid;
            RV.tr.t0 = x0::now();
            RV.tr.base = hp_of(pw, std::nan(""));
            RV.tr.expect = hh == RV.healHp.end() ? std::nan("") : hh->second;
            RV.tr.next = 0;
            x0::logf("NOLA LIFE: reviveheal: %s aplicou os efeitos do item %s (hp %s -> base %s, cura de estoque +%s)", sid.c_str(), list.c_str(),
                     std::isnan(hp0) ? "nil" : lnum(hp0).c_str(), std::isnan(RV.tr.base) ? "nil" : lnum(RV.tr.base).c_str(),
                     std::isnan(RV.tr.expect) ? "?" : lnum(RV.tr.expect).c_str());
            return true;
        }

        void heal_trace(double now)
        {
            static const double T[] = {0.5, 1, 2, 3, 4, 6, 8, 12};
            if (!RV.tr.on) return;
            if (RV.tr.next >= sizeof(T) / sizeof(T[0])) { RV.tr.on = false; return; }
            if (now - RV.tr.t0 < T[RV.tr.next]) return;
            Ref pw = local_pawn();
            double hp = pw ? hp_of(pw, std::nan("")) : std::nan("");
            x0::logf("NOLA LIFE: reviveheal %s +%.1f s: hp %s (base %s, cura de estoque +%s)", RV.tr.sid.c_str(), now - RV.tr.t0, std::isnan(hp) ? "nil" : lnum(hp).c_str(),
                     std::isnan(RV.tr.base) ? "nil" : lnum(RV.tr.base).c_str(), std::isnan(RV.tr.expect) ? "?" : lnum(RV.tr.expect).c_str());
            RV.tr.next++;
        }

        int item_count(const Ref& pw, const std::string& sid)
        {
            Ref med = x0::find_path(L"/Script/Stalker2.Default__CppMediator");
            if (!med) return -1;
            std::wstring w(sid.begin(), sid.end());
            Call c(med, L"GetItemsCountInInventory");
            c.o(L"Obj", pw).s(L"ItemName", w);
            if (!c.run()) return -1;
            return (int)c.ri();
        }

        bool remote_near(const Ref& pw, double now, double radius, double& dist, int view = 0)
        {
            const x0::Snap& r = x0::remote(view);
            Vec3 lp;
            dist = -1;
            if (!r.has_pos || (now - r.recv) >= 2 || !loc_of(pw, lp)) return false;
            dist = std::sqrt((r.x - lp.x) * (r.x - lp.x) + (r.y - lp.y) * (r.y - lp.y));
            return dist <= radius;
        }

        bool view_fresh(int v, double now)
        {
            const x0::Snap& r = x0::remote(v);
            const x0::Peer& p = x0::peer(v);
            return r.ok && r.has_pos && (now - r.recv) < 2 && p.state == x0::Peer::up && !p.left;
        }

        bool parse_rv3(int view, int64_t& rev, int64_t& die, int64_t& tpid, bool& three, int64_t& item)
        {
            std::string s;
            std::vector<int64_t> f;
            if (!x0::rpart("RV", s, view) || !rv_fields(s, 2, 4, f)) return false;
            three = f.size() >= 3;
            rev = f[0];
            die = f[1];
            tpid = three ? f[2] : (int64_t)x0::my_pid();
            item = f.size() == 4 ? f[3] : 0;
            return true;
        }

        bool parse_go(const std::string& g, int64_t& seq, std::string& kind, int& pid)
        {
            auto p = pieces(g, ',');
            if (p.size() < 2 || p.size() > 3) return false;
            for (char c : p[0])
                if (c < '0' || c > '9') return false;
            seq = std::strtoll(p[0].c_str(), nullptr, 10);
            kind = p[1];
            if (kind != "alldown" && kind != "bleed") return false;
            pid = -1;
            if (p.size() == 3)
            {
                for (char c : p[2])
                    if (c < '0' || c > '9') return false;
                pid = std::atoi(p[2].c_str());
            }
            return seq > 0;
        }

        void vl_session(int v)
        {
            const x0::Peer& p = x0::peer(v);
            VLife& L = VL[v];
            if (p.id == L.id && p.sessions == L.ses) return;
            L.id = p.id;
            L.ses = p.sessions;
            L.seen = false;
            L.rRev = L.rDie = 0;
            L.shotSeen.clear();
            L.ffSeen.clear();
            L.mdSeen.clear();
        }

        void go_apply(const std::string& kind, int pid, double now)
        {
            int me = x0::my_pid();
            if (kind == "bleed" && pid == me) return;
            std::string who = kind == "alldown" ? std::string("todos os jogadores caidos") : pname(pid) + " morreu";
            if (F_bothdie.i() != 1)
            {
                x0::logf("NOLA LIFE: FIM DE JOGO (%s) mas bothdie=0: eu continuo vivo", who.c_str());
                return;
            }
            GO.dying = true;
            if (LF.down)
            {
                LF.both = true;
                x0::logf("NOLA LIFE: FIM DE JOGO (%s): eu (%s) estou caido -> morro agora", who.c_str(), pname(me).c_str());
                return;
            }
            if (!RV.pd)
            {
                RV.pd = true;
                RV.pdWait = false;
                RV.pdNext = now;
                RV.pdTries = 0;
                x0::logf("NOLA LIFE: FIM DE JOGO (%s): eu (%s) morro tambem; o host carrega o ultimo save e os clientes seguem", who.c_str(), pname(me).c_str());
            }
        }

        void go_raise(const char* kind, int pid, double now)
        {
            if (!x0::is_host() || !multi() || F_bothdie.i() != 1) return;
            if (GO.latched)
            {
                x0::logf("NOLA LIFE: fim de jogo #%lld ja anunciado; %s%s ignorado", (long long)GO.seq, kind, pid >= 0 ? (" " + pname(pid)).c_str() : "");
                return;
            }
            GO.seq++;
            GO.latched = true;
            GO.at = now;
            GO.until = now + 10;
            GO.txt = pid >= 0 ? fmt("%lld,%s,%d", (long long)GO.seq, kind, pid) : fmt("%lld,%s", (long long)GO.seq, kind);
            x0::logf("NOLA LIFE: FIM DE JOGO PARA TODOS #GO:%s (%s)", GO.txt.c_str(), std::string(kind) == "alldown" ? "todos caidos" : (pname(pid) + " morreu").c_str());
            go_apply(kind, pid, now);
        }

        std::string go_out()
        {
            if (!multi() || !x0::is_host() || GO.txt.empty() || x0::now() >= GO.until) return "";
            return "#GO:" + GO.txt;
        }

        void go_watch(double now)
        {
            if (x0::is_host())
            {
                if (GO.latched && now - GO.at > 60) GO.latched = false;
                return;
            }
            const x0::Peer& p = x0::peer(0);
            if (p.id != GO.hostId || p.sessions != GO.hostSes)
            {
                GO.hostId = p.id;
                GO.hostSes = p.sessions;
                GO.armed = false;
                GO.last = 0;
            }
            if (!x0::remote(0).ok) return;
            std::string g;
            int64_t seq = 0;
            std::string kind;
            int pid = -1;
            bool ok = x0::rpart("GO", g, 0) && parse_go(g, seq, kind, pid);
            if (!GO.armed)
            {
                GO.armed = true;
                GO.last = ok ? seq : 0;
                if (ok) x0::logf("NOLA LIFE: #GO:%s ja estava no host quando cheguei: ignorado", g.c_str());
                return;
            }
            if (!ok || seq <= GO.last) return;
            GO.last = seq;
            x0::logf("NOLA LIFE: host anunciou FIM DE JOGO #GO:%s", g.c_str());
            go_apply(kind, pid, now);
        }

        void go_alldown(double now)
        {
            if (!x0::is_host() || F_bothdown.i() != 1 || F_bothdie.i() != 1 || !LF.down || LF.both) return;
            int up = 0;
            bool all = true;
            x0::each_view([&](int v, const x0::Snap& s, const x0::Peer& p) {
                if (!s.ok || p.left) return;
                if (p.state != x0::Peer::up) { all = false; return; }
                up++;
                char st = 0;
                double rh, rm;
                if (!(parse_life(st, rh, rm, v) && st == 'w' && (now - s.recv) < 2)) all = false;
            });
            if (up > 0 && all) go_raise("alldown", -1, now);
        }

        void rv_watch_multi(double now)
        {
            x0::each_view([&](int v, const x0::Snap&, const x0::Peer&) {
                vl_session(v);
                VLife& L = VL[v];
                int64_t a = 0, b = 0, tp = 0, it = 0;
                bool three = false;
                if (!parse_rv3(v, a, b, tp, three, it)) return;
                if (!L.seen || a < L.rRev || b < L.rDie)
                {
                    L.seen = true;
                    L.rRev = a;
                    L.rDie = b;
                    return;
                }
                int pid = x0::view_pid(v);
                if (a > L.rRev)
                {
                    L.rRev = a;
                    if (tp == x0::my_pid())
                    {
                        RV.req = a;
                        RV.reqAt = now;
                        RV.reqView = v;
                        RV.reqItem = (int)it;
                    }
                }
                if (b > L.rDie)
                {
                    L.rDie = b;
                    if (x0::is_host()) go_raise("bleed", pid, now);
                }
            });
        }

        int rv_best_down(const Ref& pw, double now)
        {
            int best = -1;
            double bd = 0;
            Vec3 lp;
            bool hl = pw && loc_of(pw, lp);
            x0::each_view([&](int v, const x0::Snap& s, const x0::Peer&) {
                char st = 0;
                double rh, rm;
                bool dn = view_fresh(v, now) && parse_life(st, rh, rm, v) && st == 'w';
                if (!dn) return;
                double d = hl ? (s.x - lp.x) * (s.x - lp.x) + (s.y - lp.y) * (s.y - lp.y) : 1e30;
                if (best < 0 || d < bd) { best = v; bd = d; }
            });
            return best;
        }

        bool any_alive_near(const Ref& pw, double now, double rr)
        {
            Vec3 lp;
            if (!pw || !loc_of(pw, lp)) return false;
            bool hit = false;
            x0::each_view([&](int v, const x0::Snap& s, const x0::Peer&) {
                char st = 0;
                double rh, rm;
                if (!view_fresh(v, now) || !parse_life(st, rh, rm, v) || st != 'a') return;
                if ((s.x - lp.x) * (s.x - lp.x) + (s.y - lp.y) * (s.y - lp.y) <= rr * rr) hit = true;
            });
            return hit;
        }

        void rv_watch(double now)
        {
            if (multi()) { rv_watch_multi(now); return; }
            int64_t a = 0, b = 0, it = 0;
            if (!parse_rv(a, b, it)) return;
            if (!RV.seen || a < RV.rRev || b < RV.rDie)
            {
                RV.seen = true;
                RV.rRev = a;
                RV.rDie = b;
                return;
            }
            if (a > RV.rRev)
            {
                RV.rRev = a;
                RV.req = a;
                RV.reqAt = now;
                RV.reqItem = (int)it;
            }
            if (b > RV.rDie)
            {
                RV.rDie = b;
                if (F_bothdie.i() != 1) { x0::logf("NOLA LIFE: o outro jogador morreu (#%lld); bothdie=0: eu continuo vivo", (long long)b); return; }
                if (!RV.pd)
                {
                    RV.pd = true;
                    RV.pdWait = false;
                    RV.pdNext = now;
                    RV.pdTries = 0;
                    x0::logf("NOLA LIFE: o outro jogador morreu (#%lld): eu morro tambem (os dois recomecam do ultimo save do host)", (long long)b);
                }
            }
        }

        bool life_kill(const Ref& pw)
        {
            int c = LF.down ? LF.cause : 0;
            int nt = F_deathneutral.i() == 1 ? 0 : 1;
            int ty = c ? (c & 0xff) : 6 * nt, src = c ? ((c >> 8) & 0xff) : 2 * nt;
            return brief_damage(pw, 100000.0, F_dmgap10.d() / 10.0, L"None", false, 0, ty, src);
        }

        bool partner_death(const Ref& pw, double now)
        {
            if (!RV.pd) return false;
            if (!alive_of(pw)) { RV.pd = false; return false; }
            if (g_links.god_other && g_links.god_other())
            {
                if (!RV.pdWait)
                {
                    RV.pdWait = true;
                    x0::log(GO.dying ? "NOLA LIFE: morte do fim de jogo espera: a cinematica/emissao segura o Unkillable"
                                     : "NOLA LIFE: morte junto com o outro jogador espera: a cinematica/emissao segura o Unkillable");
                }
                return false;
            }
            if (now < RV.pdNext) return true;
            RV.pdNext = now + 1;
            if (RV.pdTries >= 10)
            {
                x0::logf("NOLA LIFE: morte %s desistiu depois de %d tentativas (ainda vivo)", GO.dying ? "do fim de jogo" : "junto com o outro jogador", RV.pdTries);
                RV.pd = false;
                return false;
            }
            god_drop(GO.dying ? "fim de jogo" : "o outro jogador morreu");
            double h0 = hp_of(pw, std::nan(""));
            bool ok = life_kill(pw);
            double h1 = hp_of(pw, std::nan(""));
            RV.pdTries++;
            x0::logf("NOLA LIFE: MORTE JUNTO %s (ReceiveDamageStruct ok=%s hp %s -> %s alive=%s, tentativa %d)", GO.dying ? "no fim de jogo" : "com o outro jogador", tf(ok),
                     std::isnan(h0) ? "nil" : lnum(h0).c_str(), std::isnan(h1) ? "nil" : lnum(h1).c_str(), tf(alive_of(pw)), RV.pdTries);
            return true;
        }

        std::string rv_partner(int view)
        {
            return view >= 0 ? pname(x0::view_pid(view)) : nola_text::tx(nola_text::T::rev_partner);
        }

        void rv_say(const char* id, nola_text::T t, const std::string& name, double secs)
        {
            if (x0::fi("revmsg", 1) != 1) return;
            char b[256];
            std::snprintf(b, sizeof b, nola_text::tx(t).c_str(), name.c_str());
            x0::ui_msg(id, b, secs);
        }

        void rv_prompt(bool ctx, const std::string& name, double now)
        {
            static std::string lastName;
            static double lastAt = -1e9;
            static bool shown = false;
            if (!ctx)
            {
                lastName.clear();
                lastAt = -1e9;
                if (shown) x0::screen_text_clear("revprompt");
                shown = false;
                return;
            }
            if (name == lastName && now - lastAt < 1.5) return;
            lastName = name;
            lastAt = now;
            shown = true;
            rv_say("revprompt", nola_text::T::rev_prompt, name, 2.5);
        }

        int rv_down_left()
        {
            double l = F_downsecs.d() - (x0::now() - LF.at);
            return l > 0 ? (int)std::ceil(l) : 0;
        }

        void rv_down_say(bool fresh = true)
        {
            if (x0::fi("revmsg", 1) != 1) return;
            using nola_text::T;
            bool die = F_downdie.i() == 1;
            int left = rv_down_left();
            RV.downLeft = left;
            char b[320];
            if (F_reviveitem.i() == 1)
                std::snprintf(b, sizeof b, nola_text::tx(die ? T::rev_down_item_die : T::rev_down_item_up).c_str(), (double)left);
            else
                std::snprintf(b, sizeof b, nola_text::tx(die ? T::rev_down_prox_die : T::rev_down_prox_up).c_str(), F_revivesecs.d(), (double)left);
            if (fresh) RV.done.clear();
            RV.downTxt = true;
            x0::ui_top("revdown", b, 0);
        }

        void rv_down_clear()
        {
            if (!RV.downTxt) return;
            RV.downTxt = false;
            x0::ui_top_clear("revdown");
        }

        void rv_done_set(const std::string& name)
        {
            if (x0::fi("revmsg", 1) != 1) return;
            char b[256];
            std::snprintf(b, sizeof b, nola_text::tx(nola_text::T::rev_done).c_str(), name.c_str());
            RV.done = b;
        }

        void rv_done_flush(bool camBusy, bool alive)
        {
            if (!LF.down) rv_down_clear();
            else if (RV.downTxt && rv_down_left() != RV.downLeft) rv_down_say(false);
            if (RV.done.empty() || camBusy) return;
            if (alive && !LF.down) x0::ui_top("revdone", RV.done, 4);
            RV.done.clear();
        }

        void rv_tick_multi()
        {
            double now = x0::now();
            bool press = RV.press || x0::key_down('F');
            RV.press = false;
            Ref pw = local_pawn();
            bool on = F_reviveitem.i() == 1 && F_lifeguard.i() == 1;
            bool meOk = on && pw && !LF.down && alive_of(pw);
            int best = meOk ? rv_best_down(pw, now) : -1;
            std::string who = best >= 0 ? pname(x0::view_pid(best)) : std::string();
            double dist = -1;
            RV.ctx = best >= 0 && remote_near(pw, now, F_reviveradius.d(), dist, best);
            rv_prompt(RV.ctx && now >= RV.sentUntil, who, now);
            if (!press || !RV.ctx) return;
            if (now < RV.sentUntil || now < RV.noUntil) return;
            std::string got;
            int have = 0;
            for (const std::string& sid : rv_items())
            {
                int n = item_count(pw, sid);
                if (n > 0) { got = sid; have = n; break; }
            }
            if (got.empty())
            {
                RV.noUntil = now + 1;
                x0::logf("NOLA LIFE: F perto de %s caido (%.0f cm) sem medkit (%d tipos procurados): nada feito", who.c_str(), dist, (int)rv_items().size());
                rv_say("revneed", nola_text::T::rev_nomedkit, who, 3);
                return;
            }
            bool ok = g_links.item_take && g_links.item_take(got);
            if (!ok)
            {
                RV.noUntil = now + 1;
                x0::logf("NOLA LIFE: F perto de %s caido: %s nao pode ser consumido (ItemRemove recusado): nada enviado", who.c_str(), got.c_str());
                return;
            }
            RV.rev++;
            g_rvStart = x0::now();
            RV.item = F_reviveheal.i() == 1 ? rv_item_index(got) : 0;
            RV.tpid = x0::view_pid(best);
            RV.sentUntil = now + 5;
            x0::logf("NOLA LIFE: REANIMAR: F perto de %s caido (%.0f cm): 1 %s consumido (tinha %d), pedido #%lld para pid %lld enviado", who.c_str(), dist, got.c_str(), have,
                     (long long)RV.rev, (long long)RV.tpid);
            rv_say("revsent", nola_text::T::rev_sent, who, 3);
        }

        void rv_tick()
        {
            heal_trace(x0::now());
            if (multi()) { rv_tick_multi(); return; }
            double now = x0::now();
            bool press = RV.press || x0::key_down('F');
            RV.press = false;
            Ref pw = local_pawn();
            char st = 0;
            double rh, rm;
            bool on = F_reviveitem.i() == 1 && F_lifeguard.i() == 1;
            bool partnerDown = on && parse_life(st, rh, rm) && st == 'w' && pw && !LF.down && alive_of(pw);
            double dist = -1;
            RV.ctx = partnerDown && remote_near(pw, now, F_reviveradius.d(), dist);
            std::string who = rv_partner(-1);
            rv_prompt(RV.ctx && now >= RV.sentUntil, who, now);
            if (!press || !RV.ctx) return;
            if (now < RV.sentUntil || now < RV.noUntil) return;
            std::string got;
            int have = 0;
            for (const std::string& sid : rv_items())
            {
                int n = item_count(pw, sid);
                if (n > 0) { got = sid; have = n; break; }
            }
            if (got.empty())
            {
                RV.noUntil = now + 1;
                x0::logf("NOLA LIFE: F perto do outro jogador caido (%.0f cm) sem medkit (%d tipos procurados): nada feito", dist, (int)rv_items().size());
                rv_say("revneed", nola_text::T::rev_nomedkit, who, 3);
                return;
            }
            bool ok = g_links.item_take && g_links.item_take(got);
            if (!ok)
            {
                RV.noUntil = now + 1;
                x0::logf("NOLA LIFE: F perto do outro jogador caido: %s nao pode ser consumido (ItemRemove recusado): nada enviado", got.c_str());
                return;
            }
            RV.rev++;
            g_rvStart = x0::now();
            RV.item = F_reviveheal.i() == 1 ? rv_item_index(got) : 0;
            RV.sentUntil = now + 5;
            x0::logf("NOLA LIFE: REANIMAR: F perto do outro jogador caido (%.0f cm): 1 %s consumido (tinha %d), pedido #%lld enviado", dist, got.c_str(), have, (long long)RV.rev);
            rv_say("revsent", nola_text::T::rev_sent, who, 3);
        }

        void bleed_out(const Ref& pw, double now)
        {
            DI.downUntil = std::max(DI.downUntil, now + 1);
            if (g_links.god_other && g_links.god_other())
            {
                if (!LF.dieWait)
                {
                    LF.dieWait = true;
                    x0::logf("NOLA LIFE: tempo esgotado (%lld s) mas a cinematica/emissao segura o Unkillable: a morte espera", (long long)F_downsecs.i());
                }
                return;
            }
            if (now < LF.killAt) return;
            LF.killAt = now + 1;
            LF.dying = true;
            rv_down_clear();
            god_drop(LF.both ? (multi() ? "fim de jogo" : "os dois caidos") : "tempo esgotado");
            double h0 = hp_of(pw, std::nan(""));
            bool ok = life_kill(pw);
            double h1 = hp_of(pw, std::nan(""));
            LF.kills++;
            if (LF.both && multi())
                x0::logf("NOLA LIFE: FIM DE JOGO para todos (eu caido ha %.1f s): morro agora (ReceiveDamageStruct ok=%s hp %s -> %s alive=%s, tentativa %d)", now - LF.at,
                         tf(ok), std::isnan(h0) ? "nil" : lnum(h0).c_str(), std::isnan(h1) ? "nil" : lnum(h1).c_str(), tf(alive_of(pw)), LF.kills);
            else if (LF.both)
                x0::logf("NOLA LIFE: OS DOIS CAIDOS ao mesmo tempo (eu ha %.1f s): FIM DE JOGO agora (ReceiveDamageStruct ok=%s hp %s -> %s alive=%s, tentativa %d)", now - LF.at,
                         tf(ok), std::isnan(h0) ? "nil" : lnum(h0).c_str(), std::isnan(h1) ? "nil" : lnum(h1).c_str(), tf(alive_of(pw)), LF.kills);
            else
                x0::logf("NOLA LIFE: ninguem veio em %.1f s: SANGROU ATE MORRER (ReceiveDamageStruct ok=%s hp %s -> %s alive=%s, tentativa %d)", now - LF.at, tf(ok),
                         std::isnan(h0) ? "nil" : lnum(h0).c_str(), std::isnan(h1) ? "nil" : lnum(h1).c_str(), tf(alive_of(pw)), LF.kills);
        }

        void life_reset()
        {
            DI.downUntil = 0;
            LF.dying = false;
            LF.both = false;
        }

        void load_guard(const Ref& pw)
        {
            bool dead = false;
            if (pw)
            {
                Call c(pw, L"IsAlive");
                dead = c.run() && !c.rb();
            }
            bool want = F_dieload.i() == 1 && x0::is_client() && dead && peer_in();
            if (want != LF.loadBlock)
            {
                LF.loadBlock = want;
                bool ok = s2m_life_load_block(want);
                if (want)
                {
                    LF.loadTries = s2m_life_load_tries();
                    s2m_life_load_hidden(&LF.loadBuilds);
                    x0::logf("NOLA LIFE: cliente morto: carregar save pela tela de morte bloqueado e botoes de carregar escondidos (so o host carrega; n7 segue o host) ok=%s", tf(ok));
                }
                else x0::log(dead ? "NOLA LIFE: carregar save liberado (sem o host na sessao: o cliente carrega o proprio save)" : "NOLA LIFE: carregar save liberado (cliente nao esta mais morto)");
            }
            if (LF.loadBlock)
            {
                int b = 0;
                int h = s2m_life_load_hidden(&b);
                if (b > LF.loadBuilds)
                {
                    x0::logf("NOLA LIFE: tela de morte do cliente montada sem os botoes de carregar (montagem #%d, %d botoes escondidos no total)", b, h);
                    LF.loadBuilds = b;
                }
            }
            int n = LF.loadBlock ? s2m_life_load_tries() : LF.loadTries;
            if (LF.loadBlock && n > LF.loadTries)
            {
                x0::logf("NOLA LIFE: o cliente tentou carregar um save na tela de morte (%d vezes): recusado, espera o host", n);
                LF.loadTries = n;
            }
        }

        void death_watch(const Ref& pw)
        {
            if (!pw) return;
            if (!pw.same(RV.dwPawn)) { RV.dwPawn = pw; RV.dwAlive = false; GO.dying = false; }
            Call c(pw, L"IsAlive");
            if (!c.run()) return;
            if (c.rb()) { RV.dwAlive = true; return; }
            if (!RV.dwAlive) return;
            RV.dwAlive = false;
            const char* how = LF.down ? (LF.both ? (multi() ? "fim de jogo" : "os dois caidos") : LF.kills > 0 ? "sangrou ate morrer" : "morreu caido") : "morte direta sem cair";
            life_reset();
            bool byGo = GO.dying;
            GO.dying = false;
            if (F_bothdie.i() != 1) return;
            if (RV.pd || byGo)
            {
                if (byGo) x0::log("NOLA LIFE: MORRI pelo fim de jogo (#GO do host): nada mandado de volta");
                else x0::log("NOLA LIFE: MORRI pela morte do outro jogador (#RV recebido): nada mandado de volta");
                if (x0::is_client() && g_links.die_follow) g_links.die_follow();
                return;
            }
            if (multi())
            {
                bool any = false;
                x0::each_view([&](int, const x0::Snap&, const x0::Peer& p) { if (p.state == x0::Peer::up) any = true; });
                if (!any)
                {
                    x0::logf("NOLA LIFE: MORRI (%s) sem outro jogador na sessao: nada mandado", how);
                    return;
                }
                RV.die++;
                x0::logf("NOLA LIFE: MORRI (%s, hp %s): morte #%lld de %s; fim de jogo para todos, o host carrega o ultimo save", how, lnum(hp_of(pw, std::nan(""))).c_str(),
                         (long long)RV.die, pname(x0::my_pid()).c_str());
                if (x0::is_host()) go_raise("bleed", 0, x0::now());
                if (x0::is_client() && g_links.die_follow) g_links.die_follow();
                return;
            }
            if (x0::peer().state != x0::Peer::up)
            {
                x0::logf("NOLA LIFE: MORRI (%s) sem o outro jogador na sessao: nada mandado", how);
                return;
            }
            RV.die++;
            x0::logf("NOLA LIFE: MORRI (%s, hp %s): morte #%lld mandada ao outro jogador; ele morre junto e os dois recomecam do ultimo save do host", how,
                     lnum(hp_of(pw, std::nan(""))).c_str(), (long long)RV.die);
            if (x0::is_client() && g_links.die_follow) g_links.die_follow();
        }

        void life_guard()
        {
            double now = x0::now();
            Ref pw = local_pawn();
            rv_watch(now);
            if (multi()) go_watch(now);
            death_watch(pw);
            load_guard(pw);
            if (g_links.nd_dead && g_links.nd_dead()) return;
            if (pw && partner_death(pw, now)) return;
            if (F_lifeguard.i() != 1 || !pw)
            {
                if (LF.down && pw && LF.frozen) move_back(pw);
                god_drop(pw ? "lifeguard desligado" : "sem pawn");
                LF.down = false;
                LF.frozen = false;
                return;
            }
            double hp = hp_of(pw, std::nan(""));
            bool alive = alive_of(pw);
            bool held = !LF.god.empty() && pw.same(LF.godPawn) && alive && !std::isnan(hp) && (LF.down || god_always());
            if (std::isnan(hp) || !alive || (hp <= 0 && !held))
            {
                if (LF.down)
                    x0::logf("NOLA LIFE: MORREU ENQUANTO CAIDO (hp=%s alive=%s, caido ha %.1f s, ultimo sangramento=%s)", std::isnan(hp) ? "nil" : lnum(hp).c_str(),
                             tf(alive), now - LF.at, LF.lastBleed.c_str());
                god_drop("morte");
                if (LF.down || LF.dying) life_reset();
                LF.down = false;
                LF.frozen = false;
                return;
            }
            if (god_always() && !LF.dying) god_take(pw, true);
            else if (!god_always() && !LF.down && !LF.god.empty()) god_drop(peer_in() ? "lifegod desligado" : "sem o outro jogador na sessao: morte normal do jogo");
            if (!LF.down)
            {
                if (RV.req)
                {
                    x0::logf("NOLA LIFE: pedido de reanimacao #%lld ignorado: nao estou caido", (long long)RV.req);
                    RV.req = 0;
                }
                if (hp <= F_downhp.d() && peer_in() && F_testgod.i() != 1)
                {
                    LF.down = true;
                    LF.at = now;
                    LF.hasNear = false;
                    LF.downs++;
                    DI.downUntil = now + F_downsecs.d();
                    LF.lethal = 0;
                    LF.dieWait = false;
                    LF.killAt = 0;
                    LF.kills = 0;
                    RV.req = 0;
                    LF.dying = false;
                    LF.both = false;
                    double lhAge = 1e9;
                    int lhC = g_links.last_cause ? g_links.last_cause(lhAge) : 0;
                    LF.cause = lhAge <= 1.0 ? lhC : 0;
                    force_hp(pw, F_downhp.d());
                    god_take(pw, false);
                    LF.lastBleed = lnum(bleed_clear(pw));
                    if (F_downfreeze.i() == 1) LF.frozen = move_off(pw);
                    if (F_reviveitem.i() == 1)
                        x0::logf("NOLA LIFE: CAIDO #%d (hp %.1f <= %lld); %s em %lld s ou com F + medkit do outro jogador a %lld cm", LF.downs, hp, (long long)F_downhp.i(),
                                 F_downdie.i() == 1 ? "morre" : "levanta", (long long)F_downsecs.i(), (long long)F_reviveradius.i());
                    else
                        x0::logf("NOLA LIFE: CAIDO #%d (hp %.1f <= %lld); %s em %lld s ou com o outro jogador a %lld cm por %lld s", LF.downs, hp, (long long)F_downhp.i(),
                                 F_downdie.i() == 1 ? "morre" : "levanta", (long long)F_downsecs.i(), (long long)F_reviveradius.i(), (long long)F_revivesecs.i());
                    rv_down_say();
                }
                return;
            }
            if (hp <= 0)
            {
                LF.lethal++;
                if (LF.lethal <= 5 || LF.lethal % 50 == 0)
                    x0::logf("NOLA LIFE: golpe letal segurado pelo Unkillable (hp %s, caido ha %.1f s, %d vezes)", lnum(hp).c_str(), now - LF.at, LF.lethal);
            }
            if (hp < F_downhp.d()) force_hp(pw, F_downhp.d());
            LF.lastBleed = lnum(bleed_clear(pw));
            std::string why;
            const x0::Snap& r = x0::remote();
            Vec3 lp;
            char st = 0;
            double rh, rm;
            bool rl = parse_life(st, rh, rm);
            if (multi()) go_alldown(now);
            else if (F_bothdown.i() == 1 && F_bothdie.i() == 1 && !LF.both && rl && st == 'w' && (now - r.recv) < 2 && x0::peer().state == x0::Peer::up)
            {
                LF.both = true;
                x0::logf("NOLA LIFE: OS DOIS CAIDOS (eu ha %.1f s, o outro tambem): ninguem pode reanimar, os dois morrem agora", now - LF.at);
            }
            if (LF.both) { bleed_out(pw, now); return; }
            double rr = F_reviveradius.d();
            int standItem = 0;
            bool byReq = false;
            bool byNear = false;
            std::string byName;
            if (F_reviveitem.i() == 1 && RV.req && now - RV.reqAt < F_revwait.d())
            {
                if (RV.reqWaitLog != RV.req)
                {
                    RV.reqWaitLog = RV.req;
                    x0::logf("NOLA LIFE: pedido de reanimacao #%lld recebido: fica caido %.1f s enquanto o outro jogador aplica o medkit", (long long)RV.req, F_revwait.d());
                }
                DI.downUntil = std::max(DI.downUntil, now + 1);
                return;
            }
            if (F_reviveitem.i() == 1)
            {
                if (RV.req && multi())
                {
                    double dist = -1;
                    int rv = RV.reqView;
                    char st2 = 0;
                    double rh2, rm2;
                    bool rl2 = parse_life(st2, rh2, rm2, rv);
                    bool inR = remote_near(pw, now, rr * 1.5, dist, rv);
                    std::string who = pname(x0::view_pid(rv));
                    if (inR && rl2 && st2 == 'a')
                    {
                        why = fmt("reanimado por %s com F + medkit, pedido #%lld a %.0f cm", who.c_str(), (long long)RV.req, dist);
                        standItem = RV.reqItem;
                        byReq = true;
                        byName = who;
                    }
                    else x0::logf("NOLA LIFE: pedido de reanimacao #%lld de %s recusado: a %.0f cm (max %.0f), estado %c", (long long)RV.req, who.c_str(), dist, rr * 1.5, rl2 ? st2 : '?');
                    RV.req = 0;
                }
                else if (RV.req)
                {
                    double dist = -1;
                    bool inR = remote_near(pw, now, rr * 1.5, dist);
                    if (inR && rl && st == 'a')
                    {
                        why = fmt("reanimado pelo outro jogador com F + medkit, pedido #%lld a %.0f cm", (long long)RV.req, dist);
                        standItem = RV.reqItem;
                        byReq = true;
                        byName = rv_partner(-1);
                    }
                    else x0::logf("NOLA LIFE: pedido de reanimacao #%lld recusado: outro jogador a %.0f cm (max %.0f), estado %c", (long long)RV.req, dist, rr * 1.5, rl ? st : '?');
                    RV.req = 0;
                }
            }
            else if (multi())
            {
                if (any_alive_near(pw, now, rr))
                {
                    if (!LF.hasNear) { LF.hasNear = true; LF.nearAt = now; }
                    if (now - LF.nearAt >= F_revivesecs.d())
                    {
                        why = "reanimado por outro jogador";
                        byNear = true;
                        byName = rv_partner(-1);
                    }
                }
                else LF.hasNear = false;
            }
            else if (r.has_pos && loc_of(pw, lp) && rl && st == 'a' && (now - r.recv) < 2 && (r.x - lp.x) * (r.x - lp.x) + (r.y - lp.y) * (r.y - lp.y) <= rr * rr)
            {
                if (!LF.hasNear) { LF.hasNear = true; LF.nearAt = now; }
                if (now - LF.nearAt >= F_revivesecs.d())
                {
                    why = "reanimado pelo outro jogador";
                    byNear = true;
                    byName = rv_partner(-1);
                }
            }
            else LF.hasNear = false;
            if (why.empty() && now - LF.at >= F_downsecs.d())
            {
                if (F_downdie.i() == 1) { bleed_out(pw, now); return; }
                why = "tempo";
            }
            if (!why.empty())
            {
                bool healed = F_reviveheal.i() == 1 && standItem > 0 && revive_heal(pw, standItem);
                if (!healed && byReq && standItem <= 0 && F_reviveheal.i() == 1) x0::log("NOLA LIFE: reviveheal: o pedido veio sem item: usa revivehp");
                if (!healed) force_hp(pw, F_revivehp.d());
                if (!god_always()) god_drop("de pe");
                if (LF.frozen) move_back(pw);
                DI.downUntil = 0;
                DI.protectUntil = now + F_dmgspawn.d();
                if (healed) x0::logf("NOLA LIFE: DE PE (%s) depois de %.1f s, com a cura do item", why.c_str(), now - LF.at);
                else x0::logf("NOLA LIFE: DE PE (%s) depois de %.1f s, hp=%lld", why.c_str(), now - LF.at, (long long)F_revivehp.i());
                LF.down = false;
                LF.frozen = false;
                rv_down_clear();
                if (byReq || byNear) rv_done_set(byName);
            }
            else DI.downUntil = std::max(DI.downUntil, now + 1);
        }

        struct NoWeap
        {
            bool on = false;
            Ref pawn;
            bool fail = false;
        } NW;

        const wchar_t* const NW_SID = L"Nola_DownedBlock";

        bool nw_effect(bool on) { return player_fx(on, NW_SID); }

        void noweap_tick()
        {
            Ref pw = local_pawn();
            bool alive = pw && alive_of(pw);
            bool want = F_downnoweap.i() == 1 && F_lifeguard.i() == 1 && LF.down && alive && peer_in();
            if (pw && !pw.same(NW.pawn))
            {
                NW.pawn = pw;
                if (!NW.on && F_downnoweap.i() == 1 && !want)
                {
                    bool ok = nw_effect(false);
                    x0::logf("NOLA LIFE: pawn novo de pe: efeito Nola_DownedBlock retirado por garantia (save feito caido) ok=%s", tf(ok));
                }
            }
            if (want == NW.on) return;
            NW.on = want;
            bool ok = nw_effect(want);
            if (want)
            {
                if (!ok && NW.fail) return;
                NW.fail = !ok;
                x0::logf("NOLA LIFE: caido sem armas: efeito Nola_DownedBlock aplicado (atirar, mirar, recarregar, granada, faca, consumiveis, mochila bloqueados) ok=%s", tf(ok));
                return;
            }
            const char* why = F_downnoweap.i() != 1 ? "downnoweap desligado" : F_lifeguard.i() != 1 ? "lifeguard desligado" : !pw ? "sem pawn" : !alive ? "morte" : !peer_in() ? "sem o outro jogador na sessao" : "de pe";
            if (!pw) x0::log("NOLA LIFE: armas liberadas (sem pawn, carga): efeito Nola_DownedBlock sai com o pawn, sem chamar o console (o proximo pawn o retira por garantia)");
            else x0::logf("NOLA LIFE: armas liberadas (%s): efeito Nola_DownedBlock retirado ok=%s", why, tf(ok));
        }

        struct DownView
        {
            bool cam = false, hands = false, hold = false, getup = false, fx = false;
            Ref pawn, fxPawn;
            double fxAt = -1, fxProbe = -1;
            std::vector<Ref> hid;
            int camFail = 0;
            double getupAt = -1;
            std::string getupWhy;
        } DV;

        const double kGetupMax = 4.0;

        bool dv_want(const Ref& pw, bool alive) { return F_lifeguard.i() == 1 && LF.down && alive && peer_in() && pw; }

        const char* dv_why(const x0::Flag& f, const Ref& pw, bool alive)
        {
            return f.i() != 1 ? "flag desligada" : F_lifeguard.i() != 1 ? "lifeguard desligado" : !pw ? "sem pawn" : !pw.same(DV.pawn) ? "pawn novo (carga)" : !alive ? "morte" : !peer_in() ? "sem o outro jogador na sessao" : "de pe";
        }

        void dv_unhold(const char* why)
        {
            if (!DV.hold) return;
            DV.hold = false;
            int r = s2m_life_kdhold(DV.pawn.address(), 0);
            x0::logf("NOLA LIFE: regra de levantar do jogo de volta (%s) r=%d", why, r);
        }

        void dv_knock_off(const char* why)
        {
            int r = DV.pawn.valid() ? s2m_life_knock(DV.pawn.address(), 0) : 0;
            x0::logf("NOLA LIFE: camera de volta (%s): estado KnockDown desligado r=%d", why, r);
        }

        void dv_getup_end(const char* why, double left)
        {
            DV.getup = false;
            dv_knock_off(why);
            x0::logf("NOLA LIFE: levantou: animacao do jogo terminou depois de %.1f s (resto %.2f s)", x0::now() - DV.getupAt, left);
        }

        void dv_cam(const Ref& pw, bool want, bool alive)
        {
            if (DV.hold && F_downhold.i() != 1) dv_unhold("flag downhold desligada");
            if (want && DV.getup)
            {
                dv_getup_end("caiu de novo", -1);
                return;
            }
            if (want == DV.cam) return;
            if (want)
            {
                int r = s2m_life_knock(pw.address(), 1);
                if (r == 1)
                {
                    DV.cam = true;
                    DV.pawn = pw;
                    DV.camFail = 0;
                    DV.hold = F_downhold.i() == 1 && s2m_life_kdhold(pw.address(), 1) == 1;
                    x0::logf("NOLA LIFE: caido: camera no chao (estado KnockDown do jogo ligado, sem a acao de 10 s); fica deitado ate levantar: %s", DV.hold ? "sim" : "nao");
                }
                else if (DV.camFail++ == 0) x0::logf("NOLA LIFE: caido: camera no chao NAO aplicada (r=%d)", r);
                return;
            }
            const char* why = dv_why(F_downcam, pw, alive);
            bool hadHold = DV.hold;
            dv_unhold(why);
            DV.cam = false;
            if (hadHold && !LF.down && pw && pw.same(DV.pawn) && alive && F_lifeguard.i() == 1)
            {
                DV.getup = true;
                DV.getupAt = x0::now();
                DV.getupWhy = why;
                x0::logf("NOLA LIFE: levantando (%s): animacao de levantar do jogo (KnockDown Out)", why);
                return;
            }
            dv_knock_off(why);
        }

        void dv_getup_tick(const Ref& pw, bool alive)
        {
            if (!DV.getup) return;
            double t = x0::now() - DV.getupAt;
            if (!pw || !pw.same(DV.pawn) || !alive || F_lifeguard.i() != 1)
            {
                dv_getup_end(dv_why(F_downcam, pw, alive), -1);
                return;
            }
            double left = s2m_life_kdout_left(pw.address());
            if (t >= kGetupMax || (t >= 0.3 && left >= 0 && left <= 0.05)) dv_getup_end(DV.getupWhy.c_str(), left);
        }

        void dv_hands(const Ref& pw, bool want, bool alive, const char* forced = nullptr)
        {
            if (want == DV.hands) return;
            if (want)
            {
                Ref m;
                if (!x0::prop<Ref>(pw, "Mesh", m) || !m.valid()) return;
                std::vector<Ref> list{m};
                Call c(m, L"GetChildrenComponents");
                c.b(L"bIncludeAllDescendants", true);
                if (c.run())
                    for (const Ref& r : c.rarr(L"Children")) list.push_back(r);
                DV.hid.clear();
                for (const Ref& r : list)
                {
                    if (!r.valid() || x0::obj_name(r) == "ShadowMesh" || !x0::is_a(r, L"PrimitiveComponent") || !x0::call_b(r, L"IsVisible", false)) continue;
                    if (Call(r, L"SetVisibility").b(L"bNewVisibility", false).b(L"bPropagateToChildren", false).run()) DV.hid.push_back(r);
                }
                DV.hands = true;
                DV.pawn = pw;
                x0::logf("NOLA LIFE: caido: maos e arma escondidas (%d componentes)", (int)DV.hid.size());
                return;
            }
            int n = 0, gone = 0;
            for (const Ref& r : DV.hid)
            {
                if (!r.valid()) gone++;
                else if (Call(r, L"SetVisibility").b(L"bNewVisibility", true).b(L"bPropagateToChildren", false).run()) n++;
            }
            if (gone)
                x0::logf("NOLA LIFE: maos e arma de volta (%s): %d de %d componentes reexibidos; os outros %d o jogo ja destruiu (morte ou carga) e as maos novas vem visiveis", forced ? forced : dv_why(F_downhands, pw, alive), n, (int)DV.hid.size(), gone);
            else
                x0::logf("NOLA LIFE: maos e arma de volta (%s): %d de %d componentes", forced ? forced : dv_why(F_downhands, pw, alive), n, (int)DV.hid.size());
            DV.hid.clear();
            DV.hands = false;
        }

        const double kFxWait = 2.5, kFxMin = 0.05;

        bool dv_fx_effect(bool on) { return player_fx(on, L"LowHealthPostProcess"); }

        void dv_fx(const Ref& pw, bool want, bool alive)
        {
            double now = x0::now();
            if (want && DV.fx && !pw.same(DV.fxPawn)) want = false;
            if (!want)
            {
                DV.fxAt = DV.fxProbe = -1;
                if (!DV.fx) return;
                DV.fx = false;
                bool ok = dv_fx_effect(false);
                if (!pw) x0::log("NOLA LIFE: efeito de vida baixa do jogo sai com o pawn (sem pawn, carga), sem chamar o console");
                else x0::logf("NOLA LIFE: efeito de vida baixa do jogo retirado (%s) ok=%s", F_downfx.i() != 1 ? "flag desligada" : F_lifeguard.i() != 1 ? "lifeguard desligado" : !pw ? "sem pawn" : !pw.same(DV.fxPawn) ? "pawn novo (carga)" : !alive ? "morte" : !peer_in() ? "sem o outro jogador na sessao" : "de pe", tf(ok));
                return;
            }
            if (DV.fx)
            {
                if (DV.fxProbe >= 0 && now >= DV.fxProbe)
                {
                    DV.fxProbe = -1;
                    x0::logf("NOLA LIFE: caido: efeito de vida baixa do jogo: LowHPIntensity agora %.2f", s2m_life_lowhp());
                }
                return;
            }
            if (DV.fxAt == -2) return;
            if (DV.fxAt < 0)
            {
                DV.fxAt = now + kFxWait;
                return;
            }
            if (now < DV.fxAt) return;
            DV.fxAt = -2;
            double v = s2m_life_lowhp();
            if (v >= kFxMin)
            {
                x0::logf("NOLA LIFE: caido: efeito de vida baixa do jogo ja ativo (LowHPIntensity %.2f), nada aplicado", v);
                return;
            }
            bool ok = dv_fx_effect(true);
            DV.fx = ok;
            DV.fxPawn = pw;
            DV.fxProbe = ok ? now + kFxWait : -1;
            x0::logf("NOLA LIFE: caido: efeito de vida baixa do jogo aplicado (LowHealthPostProcess; LowHPIntensity era %.2f) ok=%s", v, tf(ok));
        }

        void downview_tick()
        {
            Ref pw = local_pawn();
            bool alive = pw && alive_of(pw);
            bool want = dv_want(pw, alive);
            if ((DV.cam || DV.hands || DV.getup) && pw && !pw.same(DV.pawn)) want = false;
            dv_cam(pw, want && F_downcam.i() == 1, alive);
            dv_getup_tick(pw, alive);
            dv_hands(pw, (want || DV.getup) && F_downhands.i() == 1, alive);
            dv_fx(pw, dv_want(pw, alive) && F_downfx.i() == 1, alive);
            rv_done_flush(DV.cam || DV.getup, alive);
        }

        void downview_unload()
        {
            const char* why = "DLL descarregada";
            dv_unhold(why);
            bool gt = x0::on_game_thread();
            if ((DV.cam || DV.getup) && gt) dv_knock_off(why);
            if (DV.hands && gt) dv_hands(Ref{}, false, false, why);
            if (DV.fx && gt) x0::logf("NOLA LIFE: efeito de vida baixa do jogo retirado (%s) ok=%s", why, tf(dv_fx_effect(false)));
            if ((DV.cam || DV.getup || DV.hands || DV.fx) && !gt) x0::log("NOLA LIFE: DLL descarregada fora da thread do jogo: regra de levantar restaurada, camera, maos e efeito de vida baixa ficam ate o proximo pawn");
            DV.cam = DV.getup = DV.hands = DV.fx = false;
            DV.hid.clear();
        }

        struct NoBag
        {
            bool on = false;
            Ref pawn;
            bool fail = false;
        } NB;

        bool nb_effect(bool on) { return player_fx(on, L"Nola_ReviveNoBag"); }

        void nobag_tick()
        {
            Ref pw = local_pawn();
            bool ctx = F_revnobag.i() == 1 && RV.ctx && peer_in();
            bool held = x0::key_down('F');
            if (pw && !pw.same(NB.pawn))
            {
                NB.pawn = pw;
                if (!NB.on && F_revnobag.i() == 1 && !ctx)
                {
                    bool ok = nb_effect(false);
                    x0::logf("NOLA LIFE: pawn novo: efeito Nola_ReviveNoBag retirado por garantia ok=%s", tf(ok));
                }
            }
            bool want = ctx || (NB.on && held && F_revnobag.i() == 1 && pw);
            if (want == NB.on) return;
            NB.on = want;
            bool ok = nb_effect(want);
            if (want)
            {
                if (!ok && NB.fail) return;
                NB.fail = !ok;
                x0::logf("NOLA LIFE: parceiro caido ao alcance: efeito Nola_ReviveNoBag aplicado (F reanima e nao abre o inventario) ok=%s", tf(ok));
                return;
            }
            if (!pw) x0::log("NOLA LIFE: inventario no F liberado (sem pawn, carga): efeito Nola_ReviveNoBag sai com o pawn, sem chamar o console (o proximo pawn o retira por garantia)");
            else x0::logf("NOLA LIFE: inventario no F liberado (%s, F solto): efeito Nola_ReviveNoBag retirado ok=%s", F_revnobag.i() != 1 ? "revnobag desligado" : !pw ? "sem pawn" : "fora do contexto de reanimar", tf(ok));
        }

        void down_unload()
        {
            downview_unload();
            bool gt = x0::on_game_thread();
            if (NW.on && gt) x0::logf("NOLA LIFE: armas liberadas (DLL descarregada): efeito Nola_DownedBlock retirado ok=%s", tf(nw_effect(false)));
            if (NB.on && gt) x0::logf("NOLA LIFE: inventario no F liberado (DLL descarregada): efeito Nola_ReviveNoBag retirado ok=%s", tf(nb_effect(false)));
            if ((NW.on || NB.on) && !gt) x0::log("NOLA LIFE: DLL descarregada fora da thread do jogo: os efeitos de bloqueio ficam ate a DLL carregar de novo (retirados por garantia no primeiro tick)");
            NW.on = NB.on = false;
            NW.pawn = NB.pawn = Ref{};
        }

        void rel_mir_one(double now);

        void rel_mir_tick()
        {
            if (F_relmir.i() != 1 || !x0::is_host()) return;
            double now = x0::now();
            if (now - RM.at < F_relmirs.d()) return;
            RM.at = now;
            int nv = multi() ? x0::views() : 1;
            for (int v = 0; v < nv; ++v)
            {
                g_sv = v;
                rel_mir_one(now);
            }
            g_sv = 0;
        }

        void rel_mir_one(double now)
        {
            (void)now;
            Ref tg = m01::target(g_sv);
            int64_t tu;
            if (!tg.valid() || !tgt_uid(tu) || m01::target_parked(g_sv)) return;
            Ref pw = local_pawn();
            if (!pw) return;
            int64_t hu;
            if (!rel_host(pw, hu) || hu == tu) return;
            Vec3 tp;
            if (!loc_of(tg, tp)) return;
            double r2 = F_relmirr.d() * F_relmirr.d();
            Ref pup = m01::puppet(g_sv);
            std::vector<uint32_t> list;
            for (const Ref& o : x0::all_of(L"Pawn"))
            {
                if (list.size() >= 60) break;
                if (!o.valid() || o.same(pw) || o.same(tg) || (pup.valid() && o.same(pup)) || (multi() && m01::is_m1_body(o)) || !alive_of(o)) continue;
                if (!contains(x0::full_name(o), "BP_AI_")) continue;
                Vec3 q;
                if (!loc_of(o, q) || d2xy(q, tp.x, tp.y) > r2) continue;
                int64_t u;
                if (guid_of(o, u) && u != hu && u != tu) list.push_back((uint32_t)u);
            }
            if (list.empty()) return;
            int tol = (int)F_relmirtol.i();
            int n = 0, seen = 0, fail = 0, worst = 0;
            for (uint32_t x : list)
            {
                if (seen >= 64) break;
                ++seen;
                int32_t vs = 0, vd = 0;
                int ls, ld;
                if (s2rel::get(x, (uint32_t)hu, vs, ls) || s2rel::get(x, (uint32_t)tu, vd, ld)) { ++fail; continue; }
                int dd = vs > vd ? vs - vd : vd - vs;
                if (dd <= tol) continue;
                if (s2rel::set(x, (uint32_t)tu, vs)) { ++fail; continue; }
                ++n;
                worst = std::max(worst, dd);
            }
            RM.n++;
            if (n > 0)
            {
                RM.set += n;
                std::string txt = "vistos=" + std::to_string(seen) + " copiados=" + std::to_string(n) + " falhas=" + std::to_string(fail) + " maior_dif=" + std::to_string(worst);
                if (RM.set <= 20 || RM.n % 30 == 0)
                    x0::logf("NOLA relmir: alvo do cliente uid=%lld recebeu a relacao do host uid=%lld com %d NPCs (%s; total %d)", (long long)tu, (long long)hu, n, txt.c_str(),
                             RM.set);
            }
        }

        void foco_scan(double now)
        {
            if (!x0::is_host() || now - CH.focoAt < 1) return;
            CH.focoAt = now;
            Ref tg = m01::target();
            if (!tg.valid()) return;
            Vec3 tp;
            if (!loc_of(tg, tp)) return;
            Ref m = mediator();
            if (!m) return;
            int n = 0, calls = 0;
            for (const Ref& o : x0::all_of(L"Pawn"))
            {
                if (calls >= 40) break;
                if (!o.valid() || o.same(tg)) continue;
                if (!contains(x0::full_name(o), "BP_AI_") || !alive_of(o)) continue;
                Vec3 q;
                if (!loc_of(o, q) || d2xy(q, tp.x, tp.y) > 6000.0 * 6000.0) continue;
                ++calls;
                Call c(m, L"GetFocusedEnemy");
                c.o(L"Obj", o);
                if (c.run() && c.ro().same(tg)) ++n;
            }
            CH.foco += n;
            CH.focoRuns++;
            CH.focoMax = std::max(CH.focoMax, n);
        }

        void host_events()
        {
            m01::Dmg& D = m01::dmg();
            if (!CH.hasLast) { CH.hasLast = true; CH.lastSeq = D.seq; CH.lastDrop = D.dropped; return; }
            if (D.seq > CH.lastSeq) CH.events += (int)(D.seq - CH.lastSeq);
            if (D.dropped > CH.lastDrop) CH.ignoredN += (int)(D.dropped - CH.lastDrop);
            CH.lastSeq = D.seq;
            CH.lastDrop = D.dropped;
        }

        void chain_tick()
        {
            if (x0::fi("dmgchain", 1) != 1) return;
            double now = x0::now();
            if (x0::is_host())
            {
                host_events();
                foco_scan(now);
            }
            if (now - CH.logAt < 10) return;
            CH.logAt = now;
            if (x0::peer().state == x0::Peer::up)
            {
                const char* rota = route_nd() ? "natdmg" : "D";
                if (x0::is_host())
                    x0::logf("NOLA dmg cadeia 10s (host): rota=%s alvo=%s foco_no_alvo=%d (max %d em %d leituras) eventos=%d ignorados=%d enviados=%d seq_max=%lld rota_nd=%d nd_enviados=%d nd_dano=%.1f",
                             rota, m01::target().valid() ? (m01::target_parked() ? "estacionado" : "sim") : "nao", CH.foco, CH.focoMax, CH.focoRuns, CH.events, CH.ignoredN,
                             CH.sent, (long long)CH.sentMax, CH.rotaNd, CH.ndSent, CH.ndSum);
                else
                    x0::logf("NOLA dmg cadeia 10s (cliente): rota=%s dmgin=%s recebidos=%d novos=%d perdidos=%d desligado=%d rota_nd=%d nd_aplicados=%d hp_perdido=%.1f",
                             rota, x0::flag("dmgin").str().c_str(), CH.rx, CH.novos, CH.perdidos, CH.off, CH.nd, CH.ndAp, CH.hpLost);
            }
            CH.rx = CH.novos = CH.perdidos = CH.off = CH.nd = CH.ndAp = 0;
            CH.hpLost = 0;
            CH.events = CH.ignoredN = CH.sent = CH.rotaNd = CH.ndSent = 0;
            CH.ndSum = 0;
            CH.foco = CH.focoMax = CH.focoRuns = 0;
        }

        bool ff_on() { return F_friendlyfire.i() == 1 && peer_in() && !host_cine_now(); }

        std::string ff_num(double v)
        {
            char b[32];
            snprintf(b, sizeof b, "%.6g", v);
            return b;
        }

        void ff_bodies_tick(bool on)
        {
            if (!g_links.ff_bodies) return;
            std::vector<FfBody> b;
            std::string txt;
            if (on || F_ffswallow.i() == 1)
                x0::each_view([&](int v, const x0::Snap&, const x0::Peer& p) {
                    if (p.state != x0::Peer::up || p.left) return;
                    int pid = x0::view_pid(v);
                    if (pid < 0) return;
                    Ref t = m01::target(v), u = m01::puppet(v);
                    if (t) b.push_back({v, pid, t, false});
                    if (u) b.push_back({v, pid, u, true});
                    if (t || u) txt += fmt(" %s (view %d:%s%s)", pname(pid).c_str(), v, t ? " alvo" : "", u ? " boneco" : "");
                });
            g_links.ff_bodies(on, b);
            if (txt == FF.bodies) return;
            FF.bodies = txt;
            x0::logf("NOLA ff: corpos sob fogo amigo:%s", txt.empty() ? " nenhum" : txt.c_str());
        }

        void ff_send(double now, bool on)
        {
            std::vector<FfHit> hs;
            if (g_links.ff_take) hs = g_links.ff_take();
            bool tgtHit[x0::kMaxClients] = {};
            for (const FfHit& h : hs)
                if (!h.puppet && h.view >= 0 && h.view < x0::kMaxClients) tgtHit[h.view] = true;
            for (const FfHit& h : hs)
            {
                if (!on || h.pid < 0 || !view_fresh(h.view, now))
                {
                    FF.sentDrop++;
                    continue;
                }
                if (h.puppet && h.view >= 0 && h.view < x0::kMaxClients && tgtHit[h.view])
                {
                    FF.dupPup++;
                    continue;
                }
                if (++FF.seq > 999999) FF.seq = 1;
                std::string s = fmt("%lld,%d", (long long)FF.seq, h.pid);
                for (double x : h.f) s += "," + ff_num(x);
                s += fmt(",%d,%d,%d,%d,%lld", h.ty, h.src, h.b7a, h.b7b, (long long)h.bn);
                for (double x : h.v) s += "," + ff_num(x);
                s += "," + h.bone;
                m01::ff_flinch(h.view, h.f[0], h.bone);
                FF.q.push_back({now, s});
                FF.sent++;
                FF.sentT++;
                if (++FF.logN <= 30 || FF.logN % 50 == 0)
                    x0::logf("NOLA ff: acertei %s dano=%.2f armadura=%.2f perfuracao=%.2f sangra=%.2f/%.2f osso=%s tipo=%d fonte=%d (consumido no %s, enviado #%lld)",
                             pname(h.pid).c_str(), h.f[0], h.f[1], h.f[2], h.f[3], h.f[4], h.bone.c_str(), h.ty, h.src, h.puppet ? "boneco" : "alvo", (long long)FF.seq);
            }
            while (!FF.q.empty() && (now - FF.q.front().t > 1.0 || FF.q.size() > 12)) FF.q.erase(FF.q.begin());
        }

        std::string ff_out()
        {
            if (FF.q.empty()) return {};
            std::string o = "#FF:";
            for (size_t i = 0; i < FF.q.size(); ++i)
            {
                if (i) o += ";";
                o += FF.q[i].s;
            }
            return o;
        }

        bool ff_parse(const std::string& e, int64_t& seq, int& tpid, FfHit& h)
        {
            auto p = pieces(e, ',');
            if (p.size() != 19) return false;
            double n[18];
            for (int k = 0; k < 18; ++k)
            {
                char* q = nullptr;
                n[k] = std::strtod(p[k].c_str(), &q);
                if (q == p[k].c_str() || *q || !std::isfinite(n[k])) return false;
            }
            seq = (int64_t)n[0];
            tpid = (int)n[1];
            for (int k = 0; k < 5; ++k) h.f[k] = n[2 + k];
            h.ty = (int)n[7];
            h.src = (int)n[8];
            h.b7a = (int)n[9];
            h.b7b = (int)n[10];
            h.bn = (int64_t)n[11];
            for (int k = 0; k < 6; ++k) h.v[k] = n[12 + k];
            h.bone = p[18];
            for (char c : h.bone)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
            return seq > 0 && h.f[0] >= 0 && h.ty >= 0 && h.ty < 256 && h.src >= 0 && h.src < 256;
        }

        const char* ff_block(const Ref& pw, double now)
        {
            if (F_friendlyfire.i() != 1) return "friendlyfire 0";
            if (!peer_in()) return "sem sessao";
            if (host_cine_now()) return "cinematica";
            if (F_lifeguard.i() != 1) return "lifeguard 0";
            if (!pw) return "sem pawn";
            double hp = hp_of(pw, std::nan(""));
            if (!alive_of(pw) || std::isnan(hp) || hp <= 0) return "morto";
            if (LF.down || now < DI.downUntil) return "caido";
            if (LF.dying || GO.dying || RV.pd) return "morrendo";
            if (LF.god.empty() || !pw.same(LF.godPawn)) return "sem Unkillable (lifegod)";
            if (now < DI.protectUntil) return "protecao";
            if (F_ffscale.d() <= 0) return "ffscale 0";
            return nullptr;
        }

        void ff_hit(const Ref& pw, int v, const FfHit& h0)
        {
            FfHit h = h0;
            h.f[0] *= F_ffscale.d();
            std::string who = pname(x0::view_pid(v));
            double hp0 = hp_of(pw, std::nan(""));
            int r = g_links.ff_apply ? g_links.ff_apply(pw, h) : 0;
            if (r <= 0)
            {
                FF.drop++;
                FF.dropT++;
                FF.why[r < 0 ? "excecao" : "sem gancho nativo"]++;
                if (++FF.inLogN <= 30 || FF.inLogN % 50 == 0)
                    x0::logf("NOLA ff: golpe de %s nao aplicado (%s) dano=%.2f osso=%s", who.c_str(), r < 0 ? "excecao" : "sem gancho nativo", h.f[0], h.bone.c_str());
                return;
            }
            double hp1 = hp_of(pw, std::nan(""));
            FF.ok++;
            FF.okT++;
            FF.sum += h.f[0];
            if (!std::isnan(hp0) && !std::isnan(hp1) && hp0 > hp1) FF.hpLost += hp0 - hp1;
            bool lethal = !std::isnan(hp1) && hp1 <= F_downhp.d();
            if (++FF.inLogN <= 30 || FF.inLogN % 50 == 0 || lethal)
                x0::logf("NOLA ff: %s me acertou dano=%.2f armadura=%.2f perfuracao=%.2f osso=%s tipo=%d fonte=%d hp %s -> %s%s", who.c_str(), h.f[0], h.f[1], h.f[2],
                         h.bone.c_str(), h.ty, h.src, std::isnan(hp0) ? "nil" : lnum(hp0).c_str(), std::isnan(hp1) ? "nil" : lnum(hp1).c_str(),
                         lethal ? " (golpe letal: o lifeGuard deixa caido)" : "");
        }

        void ff_in(double now)
        {
            Ref pw;
            bool pwDone = false;
            int me = x0::my_pid();
            x0::each_view([&](int v, const x0::Snap&, const x0::Peer&) {
                if (v < 0 || v >= x0::kMaxClients) return;
                vl_session(v);
                VLife& L = VL[v];
                if (now - L.ffPruneAt >= 1)
                {
                    L.ffPruneAt = now;
                    for (auto it = L.ffSeen.begin(); it != L.ffSeen.end();)
                    {
                        if (now - it->second > 30) it = L.ffSeen.erase(it);
                        else ++it;
                    }
                }
                std::string s;
                if (!x0::rpart("FF", s, v) || s.empty()) return;
                for (auto& e : pieces(s, ';'))
                {
                    int64_t seq = 0;
                    int tp = -1;
                    FfHit h;
                    if (!ff_parse(e, seq, tp, h))
                    {
                        FF.bad++;
                        continue;
                    }
                    if (tp != me) continue;
                    auto it = L.ffSeen.find(seq);
                    if (it != L.ffSeen.end())
                    {
                        it->second = now;
                        continue;
                    }
                    L.ffSeen[seq] = now;
                    FF.rx++;
                    if (!pwDone)
                    {
                        pw = local_pawn();
                        pwDone = true;
                    }
                    const char* why = ff_block(pw, now);
                    if (why)
                    {
                        FF.drop++;
                        FF.dropT++;
                        FF.why[why]++;
                        if (++FF.inLogN <= 30 || FF.inLogN % 50 == 0)
                            x0::logf("NOLA ff: golpe #%lld de %s recusado (%s) dano=%.2f osso=%s", (long long)seq, pname(x0::view_pid(v)).c_str(), why, h.f[0], h.bone.c_str());
                        continue;
                    }
                    ff_hit(pw, v, h);
                }
            });
        }

        void ff_log(double now)
        {
            if (now - FF.logAt < 10) return;
            FF.logAt = now;
            bool any = false;
            std::string nat = g_links.ff_stat ? g_links.ff_stat(any) : std::string();
            if (FF.sent || FF.sentDrop || FF.dupPup || FF.rx || FF.bad || any)
            {
                std::string w;
                for (auto& kv : FF.why) w += fmt(" %s=%d", kv.first.c_str(), kv.second);
                x0::logf("NOLA ff 10 s: friendlyfire=%lld ffscale=%s enviados=%d descartados_no_envio=%d boneco_junto_do_alvo=%d recebidos=%d aplicados=%d dano=%.1f hp_perdido=%.1f recusados=%d%s mal_formados=%d%s%s",
                         (long long)F_friendlyfire.i(), lnum(F_ffscale.d()).c_str(), FF.sent, FF.sentDrop, FF.dupPup, FF.rx, FF.ok, FF.sum, FF.hpLost, FF.drop, w.c_str(), FF.bad,
                         nat.empty() ? "" : " nativo: ", nat.c_str());
            }
            FF.sent = FF.sentDrop = FF.dupPup = FF.rx = FF.ok = FF.drop = FF.bad = 0;
            FF.sum = FF.hpLost = 0;
            FF.why.clear();
        }

        void ff_tick()
        {
            double now = x0::now();
            bool on = ff_on();
            ff_bodies_tick(on);
            ff_send(now, on);
            ff_in(now);
            ff_log(now);
        }

        bool md_on() { return F_meleesync.i() == 1 && peer_in() && !host_cine_now(); }

        void md_send(double now, bool on)
        {
            std::vector<MdHit> hs;
            if (g_links.md_take) hs = g_links.md_take();
            for (const MdHit& h : hs)
            {
                if (!on || x0::is_host())
                {
                    MD.sentDrop++;
                    continue;
                }
                if (++MD.seq > 999999) MD.seq = 1;
                std::string s = fmt("%lld,%lld", (long long)MD.seq, (long long)h.uid);
                for (double x : h.f) s += "," + ff_num(x);
                s += fmt(",%d,%d,%d,%d,%lld", h.ty, h.src, h.b7a, h.b7b, (long long)h.bn);
                for (double x : h.v) s += "," + ff_num(x);
                s += "," + h.bone;
                MD.q.push_back({now, s});
                MD.sent++;
                if (++MD.logN <= 30 || MD.logN % 50 == 0)
                    x0::logf("NOLA md: golpe meu em copia de NPC uid=%lld dano=%.2f armadura=%.2f perfuracao=%.2f sangra=%.2f/%.2f osso=%s tipo=%d fonte=%d (enviado ao host #%lld)",
                             (long long)h.uid, h.f[0], h.f[1], h.f[2], h.f[3], h.f[4], h.bone.c_str(), h.ty, h.src, (long long)MD.seq);
            }
            while (!MD.q.empty() && (now - MD.q.front().t > 1.0 || MD.q.size() > 16)) MD.q.erase(MD.q.begin());
        }

        std::string md_out()
        {
            if (MD.q.empty()) return {};
            std::string o = "#MD:";
            for (size_t i = 0; i < MD.q.size(); ++i)
            {
                if (i) o += ";";
                o += MD.q[i].s;
            }
            return o;
        }

        bool md_parse(const std::string& e, int64_t& seq, MdHit& h)
        {
            auto p = pieces(e, ',');
            if (p.size() != 19) return false;
            double n[18];
            for (int k = 0; k < 18; ++k)
            {
                char* q = nullptr;
                n[k] = std::strtod(p[k].c_str(), &q);
                if (q == p[k].c_str() || *q || !std::isfinite(n[k])) return false;
            }
            seq = (int64_t)n[0];
            h.uid = (int64_t)n[1];
            for (int k = 0; k < 5; ++k) h.f[k] = n[2 + k];
            h.ty = (int)n[7];
            h.src = (int)n[8];
            h.b7a = (int)n[9];
            h.b7b = (int)n[10];
            h.bn = (int64_t)n[11];
            for (int k = 0; k < 6; ++k) h.v[k] = n[12 + k];
            h.bone = p[18];
            for (char c : h.bone)
                if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
            return seq > 0 && h.uid > 0 && h.f[0] >= 0 && h.ty >= 0 && h.ty < 256 && h.src >= 0 && h.src < 256;
        }

        bool md_bullet_src(int s) { return s == 2 || s == 23 || s == 24; }

        void md_refuse(const char* why, int v, const MdHit& h)
        {
            MD.drop++;
            MD.why[why]++;
            if (++MD.inLogN <= 30 || MD.inLogN % 50 == 0)
                x0::logf("NOLA md: golpe de %s em uid=%lld recusado (%s) dano=%.2f fonte=%d", pname(x0::view_pid(v)).c_str(), (long long)h.uid, why, h.f[0], h.src);
        }

        void md_hit(const Ref& pawn, int v, const MdHit& h)
        {
            if (md_bullet_src(h.src) || h.ty == 10 || h.src == 19) return md_refuse("tiro ou sangramento", v, h);
            Ref npc = g_links.md_find ? g_links.md_find(h.uid) : Ref{};
            if (!npc) return md_refuse("npc nao achado no host", v, h);
            Ref body = m01::puppet(), body2 = m01::target();
            std::string full = x0::full_name(npc);
            if (npc.same(pawn) || (body.valid() && npc.same(body)) || (body2.valid() && npc.same(body2)) || (multi() && m01::is_m1_body(npc)) || !contains(full, "BP_AI_"))
                return md_refuse("alvo nao e NPC do host", v, h);
            if (!alive_of(npc)) return md_refuse("npc morto", v, h);
            if (g_links.shot_deny ? g_links.shot_deny(npc) : s2m_lock::shot_deny(npc)) return md_refuse("npc protegido", v, h);
            std::string sid = after_last(full, ".");
            double h0 = hp_of(npc, -1);
            g_sv = v;
            int64_t uid = 0;
            bool has = pick_attacker(pawn, npc, h.f[0], h0, uid);
            g_sv = 0;
            int r = g_links.md_apply ? g_links.md_apply(npc, h, has ? uid : -1) : 0;
            double h1 = hp_of(npc, -1);
            if (r <= 0)
            {
                RP.pend = false;
                return md_refuse(r < 0 ? "excecao" : "sem gancho nativo", v, h);
            }
            rep_after(npc, sid, h0, h1, true);
            MD.ok++;
            MD.sum += h.f[0];
            bool dead = h0 > 0 && h1 >= 0 && h1 <= 0.001;
            if (dead) MD.kill++;
            if (!std::isnan(h0) && !std::isnan(h1) && h0 > h1) MD.hpLost += h0 - h1;
            if (++MD.inLogN <= 30 || MD.inLogN % 50 == 0 || dead)
                x0::logf("NOLA md: golpe de %s em %s uid=%lld fonte=%d tipo=%d dano=%.2f armadura=%.2f osso=%s em nome de %s hp %.1f -> %.1f%s", pname(x0::view_pid(v)).c_str(), sid.c_str(),
                         (long long)h.uid, h.src, h.ty, h.f[0], h.f[1], h.bone.c_str(), has ? ("uid " + std::to_string((long long)uid)).c_str() : "jogador do host", h0, h1,
                         dead ? dead_txt(npc).c_str() : "");
        }

        void md_in(double now)
        {
            if (!x0::is_host()) return;
            Ref pw;
            bool pwDone = false;
            x0::each_view([&](int v, const x0::Snap&, const x0::Peer&) {
                if (v < 0 || v >= x0::kMaxClients) return;
                vl_session(v);
                VLife& L = VL[v];
                if (now - L.mdPruneAt >= 1)
                {
                    L.mdPruneAt = now;
                    for (auto it = L.mdSeen.begin(); it != L.mdSeen.end();)
                    {
                        if (now - it->second > 30) it = L.mdSeen.erase(it);
                        else ++it;
                    }
                }
                std::string s;
                if (!x0::rpart("MD", s, v) || s.empty()) return;
                for (auto& e : pieces(s, ';'))
                {
                    int64_t seq = 0;
                    MdHit h;
                    if (!md_parse(e, seq, h))
                    {
                        MD.bad++;
                        continue;
                    }
                    auto it = L.mdSeen.find(seq);
                    if (it != L.mdSeen.end())
                    {
                        it->second = now;
                        continue;
                    }
                    L.mdSeen[seq] = now;
                    MD.rx++;
                    if (F_meleesync.i() != 1) { md_refuse("meleesync 0", v, h); continue; }
                    if (!peer_in()) { md_refuse("sem sessao", v, h); continue; }
                    if (host_cine_now()) { md_refuse("cinematica", v, h); continue; }
                    if (!pwDone)
                    {
                        pw = local_pawn();
                        pwDone = true;
                    }
                    if (!pw) { md_refuse("sem pawn", v, h); continue; }
                    md_hit(pw, v, h);
                }
            });
        }

        void md_log(double now)
        {
            if (now - MD.logAt < 10) return;
            MD.logAt = now;
            bool any = false;
            std::string nat = g_links.md_stat ? g_links.md_stat(any) : std::string();
            if (MD.sent || MD.sentDrop || MD.rx || MD.bad || any)
            {
                std::string w;
                for (auto& kv : MD.why) w += fmt(" %s=%d", kv.first.c_str(), kv.second);
                x0::logf("NOLA md 10 s: meleesync=%lld enviados=%d descartados_no_envio=%d recebidos=%d aplicados=%d mortes=%d dano=%.1f hp_perdido=%.1f recusados=%d%s mal_formados=%d%s%s",
                         (long long)F_meleesync.i(), MD.sent, MD.sentDrop, MD.rx, MD.ok, MD.kill, MD.sum, MD.hpLost, MD.drop, w.c_str(), MD.bad, nat.empty() ? "" : " nativo: ", nat.c_str());
            }
            MD.sent = MD.sentDrop = MD.rx = MD.ok = MD.drop = MD.bad = MD.kill = 0;
            MD.sum = MD.hpLost = 0;
            MD.why.clear();
        }

        void md_tick()
        {
            double now = x0::now();
            bool on = md_on();
            if (on != MD.cfg)
            {
                MD.cfg = on;
                if (g_links.md_cfg) g_links.md_cfg(on);
                x0::logf("NOLA md: golpes corpo a corpo e explosivos do cliente em NPCs do host %s", on ? "ligados" : "desligados");
            }
            md_send(now, on);
            md_in(now);
            md_log(now);
        }

        std::string status_line()
        {
            std::string o = fmt("dmg rota=%s alvo=%s seq_out=%lld aplicados_total=%d caido=%s tiros_out=%lld", route_nd() ? "natdmg" : "D", m01::target().valid() ? "sim" : "nao",
                                (long long)m01::dmg().seq, DI.total, tf(LF.down), (long long)SO.seq);
            if (FF.sentT || FF.okT || FF.dropT) o += fmt(" ff=enviados %d aplicados %d recusados %d", FF.sentT, FF.okT, FF.dropT);
            if (multi())
            {
                o += fmt(" eu=%s fim_de_jogo=#%lld%s", pname(x0::my_pid()).c_str(), (long long)(x0::is_host() ? GO.seq : GO.last), GO.latched ? " (anunciado)" : "");
                x0::each_view([&](int v, const x0::Snap&, const x0::Peer&) {
                    char st = 0;
                    double rh, rm;
                    bool rl = parse_life(st, rh, rm, v);
                    o += fmt(" | %s vida=%c", pname(x0::view_pid(v)).c_str(), rl ? st : '?');
                });
            }
            return o;
        }
    }

    Links& links() { return g_links; }

    void protect_until(double t) { DI.protectUntil = std::max(DI.protectUntil, t); }

    void down_until(double t) { DI.downUntil = std::max(DI.downUntil, t); }

    void respawn_reset(double protect_t)
    {
        DI.downUntil = 0;
        DI.protectUntil = protect_t;
    }

    bool remote_life(char& st, double& hp, double& mx) { return parse_life(st, hp, mx); }

    double revive_start_at() { return g_rvStart; }
    bool remote_down()
    {
        char st;
        double hp, mx;
        return parse_life(st, hp, mx) && st != 'a';
    }

    bool remote_life(int view, char& st, double& hp, double& mx) { return parse_life(st, hp, mx, view); }

    bool remote_down(int view)
    {
        char st;
        double hp, mx;
        return parse_life(st, hp, mx, view) && st != 'a';
    }

    bool local_downed() { return LF.down; }
    bool life_frozen_now() { return LF.frozen; }
    bool life_god_now() { return !LF.god.empty() || TG.god; }
    double last_shot_heard() { return g_anyShotAt; }
    int shots_heard() { return g_anyShots; }

    bool route_nd() { return g_links.nd_on && g_links.nd_on(); }

    bool nd_send_allowed() { return !host_cine_now() && !remote_down(); }

    bool nd_send_allowed(int view) { return !host_cine_now() && !remote_down(view); }

    bool nd_apply_allowed()
    {
        double now = x0::now();
        if (F_dmgin.i() != 1) return false;
        if (g_links.nd_dead && g_links.nd_dead()) return false;
        Ref pw = local_pawn();
        if (pw) dead_track(pw, now);
        if (pw && dead_held(pw)) return false;
        return now >= DI.protectUntil && now >= DI.downUntil;
    }

    void nd_sent(double amount)
    {
        CH.ndSent++;
        CH.ndSum += amount;
    }

    void nd_applied(double amount, double hp0, double hp1)
    {
        (void)amount;
        CH.ndAp++;
        DI.total++;
        if (hp0 > hp1) CH.hpLost += hp0 - hp1;
    }
}

extern "C" bool s2m_damage_life_frozen() { return s2m::damage::local_downed() && s2m::damage::life_frozen_now(); }
extern "C" bool s2m_damage_god_held() { return s2m::damage::life_god_now(); }
extern "C" void s2m_downview_unload() { s2m::damage::down_unload(); }

X0_MODULE(damage)
{
    using namespace s2m::damage;
    x0::iface().local_downed = [] { return local_downed(); };
    x0::own_tag(x0::Mod::damage, "D", [] { return x0::hub() ? std::string() : d_out(); });
    x0::own_tag_view(x0::Mod::damage, "Dv", [](int v) { return x0::hub() ? d_out(v) : std::string(); });
    x0::own_tag(x0::Mod::damage, "GO", [] { return go_out(); });
    x0::own_tag(x0::Mod::damage, "L", [] { return l_out(); });
    x0::own_tag(x0::Mod::damage, "RV", [] { return rv_out(); });
    x0::own_tag(x0::Mod::damage, "FF", [] { return ff_out(); });
    x0::own_tag(x0::Mod::damage, "MD", [] { return md_out(); });
    x0::key(x0::Mod::damage, 'F', [] { RV.press = true; });
    x0::own_tag(x0::Mod::damage, "@shots", [] { return shots_field(); });
    x0::hook(x0::Mod::damage, L"/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart", false, [](const x0::Ref& self) { on_shot_sound(self); });
    x0::every(x0::Mod::damage, "testGod", x0::Rate::t100, [] { testgod_tick(); });
    x0::every(x0::Mod::damage, "lifeGuard", x0::Rate::t100, [] { life_guard(); noweap_tick(); downview_tick(); });
    x0::every(x0::Mod::damage, "revive", x0::Rate::t100, [] { rv_tick(); nobag_tick(); });
    x0::every(x0::Mod::damage, "dmgIn", x0::Rate::t100, [] { dmg_in(); });
    x0::every(x0::Mod::damage, "ff", x0::Rate::t100, [] { ff_tick(); });
    x0::every(x0::Mod::damage, "md", x0::Rate::t100, [] { md_tick(); });
    x0::every(x0::Mod::damage, "shotsOut", x0::Rate::t100, [] { shots_out_tick(); });
    x0::every(x0::Mod::damage, "shotsIn", x0::Rate::t100, [] { shots_in(); });
    x0::every(x0::Mod::damage, "cadeia", x0::Rate::t100, [] { chain_tick(); });
    x0::every(x0::Mod::damage, "relMir", x0::Rate::t1000, [] { rel_mir_tick(); });
    x0::status(x0::Mod::damage, [] { return status_line(); });
    x0::logf("NOLA dmg: modulo C++ m02 pronto (papel=%s dmgin=%s natdmg=%s)", x0::role().c_str(), x0::flag("dmgin").str().c_str(), x0::flag("natdmg").str().c_str());
}
