#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
#include "s2x.h"
#include "s2m_census_logic.h"
#include "s2m_bodies.h"
#include "s2m_items.h"
#include "s2m_save.h"
#include "s2m_world.h"
#include "s2n_nat_api.h"

#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "s2x_fr.h"
#if defined(S2X_FR)
#define NOLA_CEN_FR 1
#else
#define NOLA_CEN_FR 0
#endif

namespace s2m_world_cen
{
    void census(std::vector<s2m::World::CenRow>& out);
}

namespace
{
    const char* kInvTsv =
#include "s2m_census_inv.inc"
        ;

    struct Ev
    {
        long long seq = 0;
        double at = 0;
        std::string fam, why, key;
    };

    struct St
    {
        std::deque<Ev> evs;
        long long evSeq = 0;
        std::vector<cen::Inv> inv;
        std::string invSrc, invErr;
        double invLoadAt = -100;
        cen::InvState is;
        double invAt = -100, whAt = -100;
        bool divTold = false;
        std::map<std::string, std::set<int32_t>> nolaPid;
        std::vector<cen::Hit> lastHits;
        int runs = 0;
    };
    St& S()
    {
        static St s;
        return s;
    }

    std::string role_s() { return x0::is_host() ? "host" : (x0::is_client() ? "client" : x0::role()); }

    void load_inv()
    {
        St& s = S();
        if (x0::now() - s.invLoadAt < 30 && !s.inv.empty()) return;
        s.invLoadAt = x0::now();
        std::string text, src = "builtin";
        std::wstring loose = x0::bridge_file(L"s2x_data\\invariants.tsv");
        if (x0::read_text(loose, text))
            src = "bridge";
        else
            text = kInvTsv;
        std::vector<cen::Inv> v;
        std::string err;
        if (!cen::parse_inv(text, v, err))
        {
            if (s.invErr != err) x0::log("NOLA INV table error (" + src + "): " + err);
            s.invErr = err;
            if (src == "bridge" && cen::parse_inv(kInvTsv, v, err)) src = "builtin";
            else return;
        }
        if (s.invSrc != src || s.inv.size() != v.size()) x0::logf("NOLA INV table %s: %d rules", src.c_str(), (int)v.size());
        s.inv = std::move(v);
        s.invSrc = src;
    }

    x0::Ref mediator() { return x0::find_path(L"/Script/Stalker2.Default__CppMediator"); }

    int64_t med_i(const x0::Ref& med, const wchar_t* fn, const x0::Ref& a)
    {
        if (!med.valid() || !a.valid()) return 0;
        x0::Call c(med, fn);
        c.o(L"Actor", a);
        return c.run() ? c.ri() : 0;
    }

    bool loc(const x0::Ref& r, x0::Vec3& v) { return x0::call_v(r, L"K2_GetActorLocation", v); }

    double d2(const x0::Vec3& a, double x, double y) { return std::sqrt((a.x - x) * (a.x - x) + (a.y - y) * (a.y - y)); }

    struct NolaB
    {
        std::string kind;
        int view = 0;
    };

    void collect(std::vector<cen::Row>& rows, std::vector<std::pair<std::string, std::string>>* head)
    {
        St& s = S();
        double R = x0::fd("censusr", 5000);
        x0::Vec3 me;
        bool hasMe = false;
        x0::Ref lp = x0::first_of(L"BP_Stalker2Character_C");
        if (lp.valid()) hasMe = loc(lp, me);
        struct PeerPos
        {
            int view;
            double x, y;
        };
        std::vector<PeerPos> peers;
        x0::each_view([&](int view, const x0::Snap& sn, const x0::Peer& p) {
            if (sn.has_pos && p.state == x0::Peer::up) peers.push_back({view, sn.x, sn.y});
        });
        if (peers.empty() && x0::remote().has_pos && x0::peer().state == x0::Peer::up) peers.push_back({0, x0::remote().x, x0::remote().y});

        std::unordered_map<uint64_t, NolaB> nola;
        int liveViews = 0;
        for (int v = 0; v < x0::kMaxClients; ++v)
        {
            bool live = m01::view_live(v);
            if (live && (v == 0 ? x0::peer().state != x0::Peer::none : true)) ++liveViews;
            struct B
            {
                const char* k;
                x0::Ref r;
            } bs[] = {{"puppet", m01::puppet(v)}, {"target", m01::target(v)}, {"far", m01::far_body(v)}};
            for (auto& b : bs)
            {
                bool valid = b.r.valid();
                if (!valid && !live) continue;
                cen::Row r("body", std::string(b.k) + ":" + std::to_string(v));
                r.s("kind", b.k).n("view", v).b("valid", valid).b("live", live);
                if (valid)
                {
                    nola[b.r.address()] = {b.k, v};
                    bool h = false;
                    if (x0::prop<bool>(b.r, "bHidden", h)) r.b("hidden", h);
                    x0::Vec3 q;
                    if (loc(b.r, q)) r.n("x", std::round(q.x)).n("y", std::round(q.y)).n("z", std::round(q.z));
                }
                if (std::string(b.k) == "target") r.b("parked", m01::target_parked(v));
                if (std::string(b.k) == "far") r.b("on", m01::far_on(v));
                rows.push_back(std::move(r));
            }
        }

        std::vector<s2nat::CenNat> nat;
        int hu = 0, hkLive = 0;
        s2nat::census_nat(nat, hu, hkLive);
        std::unordered_map<uint64_t, std::vector<const s2nat::CenNat*>> byA;
        for (auto& c : nat)
        {
            if (c.a) byA[c.a].push_back(&c);
            if (c.kind == 1)
            {
                cen::Row r("natpair", "u" + std::to_string(c.uid));
                r.n("st", c.st).b("shown", c.shown).n("luid", c.luid).b("body", c.a != 0).n("hp", c.hp).n("age", std::round(c.age * 10) / 10).s("sid", c.sid);
                r.n("x", std::round(c.hx)).n("y", std::round(c.hy)).n("z", std::round(c.hz));
                rows.push_back(std::move(r));
            }
        }

        x0::Ref med = mediator();
        std::vector<x0::Ref> pawns = x0::all_of(L"Pawn");
        std::map<int32_t, std::string> pidKind;
        for (auto& [k, set] : s.nolaPid)
            for (int32_t p : set) pidKind[p] = k;
        auto& ext = m01::ext();
        int64_t natnpc = x0::fi("natnpc", 0);
        for (const x0::Ref& o : pawns)
        {
            if (!o.valid()) continue;
            x0::Vec3 q;
            if (!loc(o, q)) continue;
            cen::Row r;
            r.fam = "npc";
            std::string cls = x0::class_name(o);
            bool isObj = x0::is_a(o, L"Obj");
            bool medOk = isObj || x0::fi("censusobj", 1) == 0;
            int64_t guid = medOk ? med_i(med, L"GetGUID", o) : 0;
            int64_t pid = medOk ? med_i(med, L"GetPrototypeID", o) : 0;
            bool alive = isObj ? x0::call_b(o, L"IsAlive", true) : true;
            double hp = isObj ? x0::call_d(o, L"GetHP", -1) : -1;
            double maxhp = isObj ? x0::call_d(o, L"GetMaxHP", -1) : -1;
            bool hidden = false;
            x0::prop<bool>(o, "bHidden", hidden);
            bool coll = x0::call_b(o, L"GetActorEnableCollision", true);
            bool rendered = false;
            {
                x0::Call c(o, L"WasRecentlyRendered");
                c.f(L"Tolerance", 0.5f);
                if (c.run()) rendered = c.rb();
            }
            x0::Ref ctl = x0::call_o(o, L"GetController");
            bool player = lp.valid() && o.same(lp);
            auto nb = nola.find(o.address());
            bool tracked = nb != nola.end();
            if (tracked && pid) s.nolaPid[nb->second.kind].insert((int32_t)pid);
            std::string nolak;
            int view = -1;
            if (tracked)
            {
                nolak = nb->second.kind;
                view = nb->second.view;
            }
            else if (pid && pidKind.count((int32_t)pid))
                nolak = pidKind[(int32_t)pid];
            bool paired = false, twin = false, inert = false, own = false, hsel = false, vend = false;
            int32_t hostUid = 0;
            auto na = byA.find(o.address());
            if (na != byA.end())
                for (auto* c : na->second)
                {
                    if (c->kind == 1)
                    {
                        paired = true;
                        hostUid = c->uid;
                    }
                    else if (c->kind == 2)
                        twin = true;
                    else if (c->kind == 3)
                        inert = true;
                    else if (c->kind == 4)
                        own = true;
                    else if (c->kind == 5)
                        hsel = true;
                    else if (c->kind == 6)
                        vend = true;
                }
            if (player)
                r.key = "player:local";
            else if (!nolak.empty())
                r.key = tracked ? "nola:" + nolak + ":" + std::to_string(view) : "nola:" + nolak + ":orphan@" + cen::q1(q.x) + "," + cen::q1(q.y);
            else if (paired)
                r.key = "u" + std::to_string(hostUid);
            else if (guid > 0)
                r.key = "u" + std::to_string(guid);
            else
                r.key = cen::pos_key(cls, q.x, q.y, q.z);
            if (hidden && !paired && !player && nolak.empty() && x0::fi("natdupfix", 1) == 1) r.key = "hid:" + r.key;
            double dist = hasMe ? std::sqrt((q.x - me.x) * (q.x - me.x) + (q.y - me.y) * (q.y - me.y)) : -1;
            bool shared = hasMe && dist <= R && !peers.empty();
            double pd = -1;
            for (auto& p : peers)
            {
                double dd = std::sqrt((q.x - p.x) * (q.x - p.x) + (q.y - p.y) * (q.y - p.y));
                if (pd < 0 || dd < pd) pd = dd;
                if (dd > R) shared = false;
            }
            r.s("cls", cls).n("uid", (double)guid).n("pid", (double)pid);
            r.n("x", std::round(q.x)).n("y", std::round(q.y)).n("z", std::round(q.z)).n("dist", std::round(dist)).n("peerdist", std::round(pd));
            r.b("alive", alive).n("hp", std::round(hp * 10) / 10).n("maxhp", std::round(maxhp)).b("hidden", hidden).b("coll", coll).b("rendered", rendered);
            r.s("ai", ctl.valid() ? x0::class_name(ctl) : std::string("none"));
            r.b("player", player).b("nola", !nolak.empty()).s("nolak", nolak.empty() ? std::string("-") : nolak).n("view", view).b("tracked", tracked);
            r.b("paired", paired).b("twin", twin).b("inert", inert).b("own", own).b("hostsel", hsel).b("vend", vend);
            r.b("hk", guid > 0 && s2nat::census_hk((int32_t)guid)).b("hutab", hu > 0 && hkLive == 1);
            r.b("loot", ext.loot_body ? ext.loot_body(o) : false).b("shared", shared).n("natnpc", (double)natnpc);
            rows.push_back(std::move(r));
        }

        std::vector<s2m::World::CenRow> wr;
        s2m_world_cen::census(wr);
        for (auto& w : wr)
        {
            cen::Row r(w.fam, w.key);
            if (w.fam == "lock") r.b("locked", w.v != 0);
            if (w.fam == "door") r.b("open", w.v != 0);
            rows.push_back(std::move(r));
        }

        s2m_items::census(rows);

        for (auto& [node, n] : s2m_save::applied()) rows.push_back(std::move(cen::Row("quest", node).n("n", n)));

        double now = x0::now();
        while (!s.evs.empty() && now - s.evs.front().at > 120) s.evs.pop_front();
        for (auto& e : s.evs)
        {
            cen::Row r("ev", std::to_string(e.seq));
            r.s("ev", e.fam).s("why", e.why).s("k", e.key).n("age", std::round((now - e.at) * 10) / 10);
            rows.push_back(std::move(r));
        }

        cen::dedupe_keys(rows);

        if (head)
        {
            auto& h = *head;
            h.push_back({"role", cen::jstr(role_s())});
            h.push_back({"pid", std::to_string(x0::my_pid())});
            h.push_back({"views", std::to_string(liveViews)});
            h.push_back({"t", cen::Row::num(std::round(now * 10) / 10)});
            h.push_back({"wall_ms", std::to_string((long long)x0::wall_ms())});
            h.push_back({"radius", cen::Row::num(R)});
            h.push_back({"hu", std::to_string(hu)});
            h.push_back({"inv_src", cen::jstr(s.invSrc)});
            if (hasMe) h.push_back({"me", "[" + cen::Row::num(std::round(me.x)) + "," + cen::Row::num(std::round(me.y)) + "," + cen::Row::num(std::round(me.z)) + "]"});
            std::string pj = "[";
            for (size_t i = 0; i < peers.size(); ++i) pj += std::string(i ? "," : "") + "{\"view\":" + std::to_string(peers[i].view) + ",\"x\":" + cen::Row::num(std::round(peers[i].x)) + ",\"y\":" + cen::Row::num(std::round(peers[i].y)) + "}";
            h.push_back({"peers", pj + "]"});
        }
        s.runs += 1;
    }

    int live_views()
    {
        int n = 0;
        for (int v = 0; v < x0::kMaxClients; ++v)
            if (m01::view_live(v) && x0::peer(v).state != x0::Peer::none) ++n;
        return n;
    }

    void trigger(const std::string& why)
    {
        x0::fr_trigger(why);
    }

    void inv_tick(const std::vector<cen::Row>& rows)
    {
        St& s = S();
        load_inv();
        std::vector<cen::Hit> cur;
        auto fired = s.is.eval(s.inv, rows, role_s(), live_views(), x0::now(), &cur);
        s.lastHits = cur;
        for (auto& h : fired)
        {
            x0::log("NOLA INV " + h.id + " first=" + cen::Row::num(std::round(h.first)) + " key=" + h.gkey + " " + h.detail);
            trigger("inv_" + h.id);
        }
    }

    void tick()
    {
        St& s = S();
        double now = x0::now();
        bool doInv = x0::fi("devinv", 1) >= 1 && now - s.invAt >= x0::fd("devinvsec", 5);
        bool doWh = x0::fi("devwh", 1) == 1 && now - s.whAt >= x0::fd("devwhsec", 10);
        if (!doInv && !doWh) return;
        if (!m01::body_gate()) return;
        std::vector<cen::Row> rows;
        collect(rows, nullptr);
        if (doInv)
        {
            s.invAt = now;
            inv_tick(rows);
        }
        if (doWh)
        {
            s.whAt = now;
            x0::log(cen::wh_line(rows, x0::wall_ms(), now, role_s()));
        }
    }

    std::string cmd_census(const std::vector<std::string>& a)
    {
        St& s = S();
        load_inv();
        std::vector<cen::Row> rows;
        std::vector<std::pair<std::string, std::string>> head;
        collect(rows, &head);
        std::vector<cen::Hit> cur;
        s.is.eval(s.inv, rows, role_s(), live_views(), x0::now(), &cur);
        std::wstring path = x0::bridge_file(L"census.json");
        if (a.size() >= 2)
        {
            path.clear();
            for (char c : a[1]) path += (wchar_t)(unsigned char)c;
        }
        std::string npath;
        for (wchar_t c : path) npath += c < 128 ? (char)c : '?';
        std::string j = cen::census_json(rows, head, cur, rows, x0::wall_ms(), x0::now(), role_s());
        bool ok = x0::write_text(path, j);
        std::map<std::string, int> per;
        for (auto& r : rows) per[r.fam] += 1;
        std::string o = std::string("census ") + (ok ? "ok" : "WRITE FAILED") + " " + npath + " rows=" + std::to_string(rows.size()) + " inv=" + std::to_string(cur.size());
        for (auto& [f, n] : per) o += " " + f + "=" + std::to_string(n);
        x0::log("NOLA census: " + o.substr(7));
        return o;
    }

    std::string cmd_inv(const std::vector<std::string>&)
    {
        St& s = S();
        std::string o = "inv src=" + s.invSrc + " rules=" + std::to_string(s.inv.size()) + " fired=" + std::to_string(s.is.total()) + " open=" + std::to_string(s.lastHits.size());
        for (auto& h : s.lastHits) o += "\n" + h.id + " " + h.gkey + " first=" + cen::Row::num(std::round(h.first)) + " " + h.detail;
        return o;
    }

    std::string cmd_frtrig(const std::vector<std::string>& a)
    {
        std::string why;
        for (size_t i = 1; i < a.size(); ++i) why += (why.empty() ? "" : " ") + a[i];
        if (why.empty()) why = "worlddiff";
        x0::log("NOLA census: fr_trigger " + why);
        trigger(why);
        return std::string("frtrig ") + (NOLA_CEN_FR ? "sent " : "no recorder in this build ") + why;
    }

    void init()
    {
        load_inv();
        x0::every(x0::Mod::x0, "census", x0::Rate::t1000, [] { tick(); });
        x0::cmd(x0::Mod::x0, "census", cmd_census);
        x0::cmd(x0::Mod::x0, "inv", cmd_inv);
        x0::cmd(x0::Mod::x0, "frtrig", cmd_frtrig);
        x0::status(x0::Mod::x0, [] {
            St& s = S();
            return "census runs=" + std::to_string(s.runs) + " inv rules=" + std::to_string(s.inv.size()) + " fired=" + std::to_string(s.is.total()) + " open=" + std::to_string(s.lastHits.size());
        });
    }
    ::x0::ModReg g_reg(x0::Mod::x0, "census", &init);
}

namespace cen
{
    void ev(const char* fam, const char* why, const std::string& key)
    {
        St& s = S();
        Ev e;
        e.seq = ++s.evSeq;
        e.at = x0::now();
        e.fam = fam ? fam : "";
        e.why = why ? why : "";
        e.key = key;
        s.evs.push_back(std::move(e));
        while (s.evs.size() > 256) s.evs.pop_front();
    }
}
#endif
