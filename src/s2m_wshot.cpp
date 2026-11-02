#include "s2m_wshot.h"
#include "s2m_bodies.h"
#include "s2n_nat_api.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

extern "C" uint64_t s2x_load_asset(const wchar_t* path);


namespace s2m_wshot::pure
{
    namespace
    {
        bool safe_ch(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '/' || c == '.' || c == '-'; }
        bool upper(char c) { return c >= 'A' && c <= 'Z'; }
    }

    bool ws_safe(std::string_view s)
    {
        if (s.empty() || s.size() > 160) return false;
        for (char c : s)
            if (!safe_ch(c)) return false;
        return true;
    }

    bool wv_safe(std::string_view s)
    {
        if (s.empty() || s.size() > 200) return false;
        for (char c : s)
            if (!safe_ch(c)) return false;
        return true;
    }

    std::string strip_dot(std::string_view p)
    {
        size_t sl = p.rfind('/');
        size_t from = sl == std::string_view::npos ? 0 : sl + 1;
        size_t d = p.find('.', from);
        return std::string(d == std::string_view::npos ? p : p.substr(0, d));
    }

    std::string strip_class(std::string_view full)
    {
        size_t i = 0;
        while (i < full.size() && full[i] != ' ' && full[i] != '\t' && full[i] != '\n' && full[i] != '\r' && full[i] != '\f' && full[i] != '\v') ++i;
        if (i == 0 || i >= full.size() || full[i] != ' ') return std::string(full);
        return std::string(full.substr(i + 1));
    }

    std::optional<WvRule> wv_rule(std::string_view path)
    {
        static const std::string FA = "/Game/_STALKER2/Animations/CharacterHead/";
        std::string p = strip_dot(path);
        size_t sl = p.rfind('/');
        if (sl == std::string::npos || sl + 1 >= p.size()) return std::nullopt;
        std::string folder = p.substr(0, sl), nm = p.substr(sl + 1);
        if (nm.rfind("AS_FAC_", 0) != 0) return std::nullopt;
        size_t i = 7, j = i;
        while (j < nm.size() && upper(nm[j])) ++j;
        if (j == i || j >= nm.size() || nm[j] != '_') return std::nullopt;
        std::string lang = nm.substr(i, j - i), vx, sid;
        std::string rest = nm.substr(j + 1);
        auto vok = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); };
        if (rest.size() >= 8 && rest.rfind("Voice", 0) == 0 && vok(rest[5]) && rest[6] == '_') { vx = rest.substr(5, 1); sid = rest.substr(7); }
        else sid = rest;
        if (sid.empty()) return std::nullopt;
        if (folder.compare(0, FA.size(), FA) != 0) return std::nullopt;
        std::vector<std::string> segs;
        std::string tail = folder.substr(FA.size());
        size_t b = 0;
        while (b <= tail.size())
        {
            size_t c = tail.find('/', b);
            std::string s = tail.substr(b, c == std::string::npos ? std::string::npos : c - b);
            if (!s.empty()) segs.push_back(s);
            if (c == std::string::npos) break;
            b = c + 1;
        }
        if (!segs.empty() && segs.back() == lang) segs.pop_back();
        if (!segs.empty() && segs.back().size() == 6 && segs.back().rfind("Voice", 0) == 0 && vok(segs.back()[5])) segs.pop_back();
        if (segs.empty()) return std::nullopt;
        if (segs[0] != "General" && segs.size() >= 2 && segs[1].substr(0, 3) != "VO_") segs[1] = "VO_" + segs[1];
        std::string out = "Voiceover/";
        for (size_t k = 0; k < segs.size(); ++k) out += (k ? "/" : "") + segs[k];
        out += "/VO_" + sid;
        return WvRule{out, vx, sid};
    }

    std::vector<WsEnt> ws_parse(std::string_view s)
    {
        std::vector<WsEnt> out;
        size_t i = 0, n = s.size();
        auto run = [&](size_t at, auto pred) {
            size_t k = at;
            while (k < n && pred(s[k])) ++k;
            return k;
        };
        auto dig = [](char c) { return c >= '0' && c <= '9'; };
        auto tok = [](char c) { return c != ',' && c != ';'; };
        while (i < n)
        {
            size_t a = i, e1 = run(a, dig);
            bool ok = e1 > a && e1 < n && s[e1] == ',';
            size_t b = e1 + 1, e2 = ok ? run(b, tok) : b;
            ok = ok && e2 > b && e2 < n && s[e2] == ',';
            size_t c = e2 + 1;
            ok = ok && c + 1 < n && (s[c] == 's' || s[c] == 'v') && s[c + 1] == ',';
            size_t d = c + 2, e4 = ok ? run(d, tok) : d;
            ok = ok && e4 > d && e4 < n && s[e4] == ',';
            size_t f = e4 + 1, e5 = ok ? run(f, dig) : f;
            ok = ok && e5 > f;
            if (!ok) { ++i; continue; }
            WsEnt w;
            w.sq = std::strtod(std::string(s.substr(a, e1 - a)).c_str(), nullptr);
            w.nm = std::string(s.substr(b, e2 - b));
            w.k = std::string(1, s[c]);
            w.pl = std::string(s.substr(d, e4 - d));
            w.n = std::strtod(std::string(s.substr(f, e5 - f)).c_str(), nullptr);
            if (e5 + 1 < n && s[e5] == ',' && s[e5 + 1] == 'd')
            {
                size_t g = e5 + 2, e6 = run(g, tok);
                std::string t(s.substr(g, e6 - g));
                size_t m = t.find(':');
                if (m != std::string::npos)
                {
                    std::string sp = t.substr(0, m), sy = t.substr(m + 1);
                    char *ep = nullptr, *ey = nullptr;
                    double vp = std::strtod(sp.c_str(), &ep), vy = std::strtod(sy.c_str(), &ey);
                    if (!sp.empty() && !sy.empty() && ep && !*ep && ey && !*ey && std::abs(vp) <= 360 && std::abs(vy) <= 720)
                    {
                        w.dir = true;
                        w.ap = vp;
                        w.ay = vy;
                    }
                }
                e5 = e6;
            }
            out.push_back(w);
            i = e5;
        }
        std::stable_sort(out.begin(), out.end(), [](const WsEnt& x, const WsEnt& y) { return x.sq < y.sq; });
        return out;
    }

    bool has_dot_leaf(std::string_view p)
    {
        size_t sl = p.rfind('/');
        if (sl == std::string_view::npos) return false;
        return p.find('.', sl + 1) != std::string_view::npos;
    }

    std::string last_seg(std::string_view p)
    {
        size_t sl = p.rfind('/');
        return std::string(sl == std::string_view::npos ? p : p.substr(sl + 1));
    }

    WsTab ws_tab(const std::vector<x0::TsvRow>& rows)
    {
        WsTab t;
        std::map<int, std::string> mz;
        for (auto& r : rows)
        {
            if (r.key.empty()) continue;
            if (r.key[0] == "WSHOT" && r.key.size() == 3)
            {
                WsRow& w = t.shot[r.key[1]];
                if (r.key[2] == "1") w.ev = r.v;
                else if (r.key[2] == "2") w.fam = r.v;
                else if (r.key[2] == "3") w.fi = std::strtod(r.v.c_str(), nullptr);
            }
            else if (r.key[0] == "WSFAM" && r.key.size() == 3)
            {
                auto& f = t.fam[r.key[1]];
                if (r.key[2] == "1") f.first = r.v;
                else if (r.key[2] == "2") f.second = r.v;
            }
            else if (r.key[0] == "WSMZ" && r.key.size() == 2) mz[std::atoi(r.key[1].c_str())] = r.v;
            else if (r.key[0] == "WSPFX" && r.key.size() == 1) t.pfx = r.v;
        }
        for (int i = 1; mz.count(i); ++i) t.mz.push_back(mz[i]);
        t.ok = t.shot.count("SK_AK74") && t.fam.count("A") && !t.pfx.empty();
        return t;
    }
}

namespace s2m_wshot
{
    namespace
    {
        using x0::Call;
        using x0::Ref;
        using x0::Rot;
        using x0::Vec3;
        using namespace pure;
        constexpr x0::Mod MOD = x0::Mod::npc;

        std::string sf(const char* f, ...)
        {
            char b[2048];
            va_list a;
            va_start(a, f);
            int n = std::vsnprintf(b, sizeof b, f, a);
            va_end(a);
            if (n < 0) return {};
            return std::string(b, (size_t)std::min(n, (int)sizeof b - 1));
        }
        void LOG(const std::string& s) { x0::log(s); }
        bool valid(const Ref& r) { return r.valid(); }
        bool loc(const Ref& a, Vec3& v) { return valid(a) && x0::call_v(a, L"K2_GetActorLocation", v); }
        bool rot(const Ref& a, Rot& r)
        {
            if (!valid(a)) return false;
            Call c(a, L"K2_GetActorRotation");
            return c.run() && c.rr(r);
        }
        Ref pref(const Ref& a, const char* p)
        {
            Ref r;
            if (valid(a) && x0::prop<Ref>(a, p, r) && valid(r)) return r;
            return {};
        }
        Ref ocall(const Ref& a, const wchar_t* fn)
        {
            if (!valid(a)) return {};
            Ref r = x0::call_o(a, fn);
            return valid(r) ? r : Ref{};
        }
        Ref local_pawn() { return x0::first_of(L"BP_Stalker2Character_C"); }
        double d2(double ax, double ay, double bx, double by) { return (ax - bx) * (ax - bx) + (ay - by) * (ay - by); }
        std::wstring wide(const std::string& s)
        {
            std::wstring w;
            for (char c : s) w.push_back((wchar_t)(unsigned char)c);
            return w;
        }
        Ref asset(const std::string& path)
        {
            std::wstring w = wide(path);
            Ref a = x0::find_path(w.c_str());
            if (valid(a)) return a;
            uint64_t p = s2x_load_asset(w.c_str());
            if (p) a = Ref::addr(p);
            return valid(a) ? a : Ref{};
        }
        bool sock_exists(const Ref& c, const std::string& s)
        {
            std::wstring w = wide(s);
            Call d(c, L"DoesSocketExist");
            d.n(L"InSocketName", w.c_str());
            return d.run() && d.rb();
        }
        bool sock_loc(const Ref& c, const std::string& s, Vec3& v)
        {
            std::wstring w = wide(s);
            Call d(c, L"GetSocketLocation");
            d.n(L"InSocketName", w.c_str());
            return d.run() && d.rv(v);
        }
        bool sock_rot(const Ref& c, const std::string& s, Rot& r)
        {
            std::wstring w = wide(s);
            Call d(c, L"GetSocketRotation");
            d.n(L"InSocketName", w.c_str());
            return d.run() && d.rr(r);
        }
        std::string addr_s(uint64_t a) { return std::to_string((long long)a); }

        struct WsOw
        {
            double at = 0;
            std::string nm, sk;
            bool far = false;
        };
        struct WsQ
        {
            double t = 0;
            std::string s;
        };
        struct VEnt
        {
            Ref o, bi, fa, spk;
            uint64_t ad = 0;
            std::string nm;
        };
        struct WsS
        {
            double at = 0;
            int hook = 0, npc = 0, mine = 0, noself = 0, far = 0, sent = 0, voice = 0, vnpc = 0, rx = 0, play = 0, vplay = 0, notgt = 0, noev = 0, cap = 0, err = 0, ply = 0, pplay = 0, pnotgt = 0, psnd = 0, pmz = 0, ptr = 0, pw = 0, pdir = 0;
            int vok = 0, vfind = 0, vmiss = 0, vnorule = 0, vdup = 0, vlong = 0, vface = 0, vspk = 0, vsw = 0, vswf = 0;
        };
        constexpr const char* PLY = "PLY";
        bool is_ply(const std::string& nm)
        {
            if (nm.compare(0, 3, PLY) != 0) return false;
            for (size_t i = 3; i < nm.size(); ++i)
                if (nm[i] < '0' || nm[i] > '9') return false;
            return nm.size() < 8;
        }
        int ply_view(const std::string& nm)
        {
            if (nm.size() == 3) return 0;
            return x0::view_of_pid(std::atoi(nm.c_str() + 3));
        }
        struct Ws
        {
            std::string plySk, plyRx;
            std::map<std::string, double> pend;
            std::map<std::string, std::pair<double, double>> pdir;
            std::vector<WsQ> q;
            long long seq = 0;
            std::map<uint64_t, WsOw> ow;
            int owN = 0;
            std::map<std::string, std::string> vl;
            int vlN = 0;
            std::vector<VEnt> vlist;
            double vAt = -100, vPoll = -100, flushAt = 0, budAt = 0;
            std::array<double, x0::kMaxClients> last{-1, -1, -1, -1}, rxAt{-100, -100, -100, -100};
            int bud = 0;
            std::map<std::string, std::string> mz;
            std::set<std::string> mzNone;
            std::map<std::string, Ref> ev;
            std::set<std::string> evMiss;
            std::map<std::string, double> vgap;
            int errT = 0;
            WsS s;
            bool vInit = false;
            std::map<std::string, std::optional<std::string>> vm;
            int vmN = 0;
            struct VRec
            {
                std::string p;
                double t = 0;
            };
            std::map<std::string, VRec> vrec;
            int vrecN = 0;
            std::map<std::string, std::optional<std::string>> vfix;
            std::optional<double> vfixAt;
            std::map<std::string, std::string> vx;
            std::map<std::string, std::optional<std::string>> vma;
            int vmaN = 0;
            int vsc = -1;
            int vuid = 0;
            struct Tr
            {
                Ref c;
                Vec3 o, d;
                double t0 = 0;
            };
            std::vector<Tr> tr;
        };
        std::optional<Ws> g_ws;
        Ws& WS()
        {
            if (!g_ws)
            {
                g_ws.emplace();
                g_ws->s.at = x0::now();
            }
            return *g_ws;
        }
        const WsTab& tab()
        {
            static WsTab t;
            static bool loaded = false;
            if (!loaded)
            {
                loaded = true;
                std::vector<x0::TsvRow> rows;
                std::string err;
                if (x0::data_tsv("s2u_wshot.tsv", rows, &err)) t = ws_tab(rows);
                if (!t.ok) LOG("NOLA wshot erro (tabela) #0: s2u_wshot.tsv " + (err.empty() ? std::string("incompleta") : err));
            }
            return t;
        }
        const WsRow& shot_row(const std::string& sk)
        {
            static const WsRow none;
            const WsTab& t = tab();
            auto it = t.shot.find(sk);
            if (it != t.shot.end()) return it->second;
            it = t.shot.find("SK_AK74");
            return it != t.shot.end() ? it->second : none;
        }
        std::pair<std::string, std::string> fam_of(const std::string& f)
        {
            const WsTab& t = tab();
            auto it = t.fam.find(f);
            if (it != t.fam.end()) return it->second;
            it = t.fam.find("A");
            return it != t.fam.end() ? it->second : std::pair<std::string, std::string>{};
        }
        const std::string& PFX() { return tab().pfx; }

        void ws_err(Ws& W, const char* where, const std::string& e)
        {
            W.s.err++;
            W.errT++;
            if (W.errT <= 5 || W.errT % 100 == 0) LOG(sf("NOLA wshot erro (%s) #%d: %s", where, W.errT, e.substr(0, 200).c_str()));
        }
        std::string what(const std::exception& e) { return e.what(); }

        void ws_shot(Ws& W, const Ref& self)
        {
            W.s.hook++;
            if (!valid(self)) { W.s.noself++; return; }
            Ref ow = ocall(self, L"GetOwner");
            if (!valid(ow)) return;
            uint64_t ad = ow.address();
            Ref lp = local_pawn();
            bool mine = valid(lp) && lp.address() == ad;
            if (mine && x0::fi("wsply", 2) >= 1)
            {
                std::string sk = "-";
                Ref w = ocall(lp, L"GetWeaponInHandsMeshComponent");
                if (valid(w))
                {
                    Ref m = ocall(w, L"GetSkeletalMeshAsset");
                    if (valid(m)) sk = x0::obj_name(m);
                }
                if (!ws_safe(sk)) sk = "-";
                W.plySk = sk;
                W.s.ply++;
                std::string key = std::string(PLY) + ",s," + sk;
                W.pend[key] += 1;
                Rot cr;
                Call cc(lp, L"GetControlRotation");
                if (x0::fi("wstrdir", 1) == 1 && cc.run() && cc.rr(cr))
                {
                    double pp = std::fmod(cr.pitch + 540.0, 360.0) - 180.0, yy = std::fmod(cr.yaw + 540.0, 360.0) - 180.0;
                    W.pdir[key] = {pp, yy};
                }
                return;
            }
            if (!x0::is_host() || x0::fi("wshot", 0) != 1) return;
            for (int v = 0; v < x0::views() && !mine; ++v)
            {
                Ref pup = m01::puppet(v);
                if (valid(pup) && pup.address() == ad) mine = true;
            }
            if (mine) { W.s.mine++; return; }
            double now = x0::now();
            auto it = W.ow.find(ad);
            if (it == W.ow.end() || now - it->second.at > 2)
            {
                WsOw c;
                c.at = now;
                c.nm = x0::obj_name(ow);
                c.sk = "-";
                Ref w = ocall(ow, L"GetWeaponInHandsMeshComponent");
                if (valid(w))
                {
                    Ref m = ocall(w, L"GetSkeletalMeshAsset");
                    if (valid(m)) c.sk = x0::obj_name(m);
                }
                Vec3 q;
                bool anyR = false, near = false;
                double R2 = std::pow(x0::fd("wsr", 6000), 2);
                bool hq = loc(ow, q);
                for (int v = 0; v < x0::views() && hq; ++v)
                {
                    const x0::Snap& r = x0::remote(v);
                    if (!(r.ok && r.has_pos)) continue;
                    anyR = true;
                    if (d2(q.x, q.y, r.x, r.y) <= R2) near = true;
                }
                if (anyR) c.far = !near;
                if (it == W.ow.end())
                {
                    W.owN++;
                    if (W.owN > 300) { W.ow.clear(); W.owN = 1; }
                }
                W.ow[ad] = c;
            }
            const WsOw& c = W.ow[ad];
            if (c.far) { W.s.far++; return; }
            if (!(ws_safe(c.nm) && ws_safe(c.sk))) return;
            W.s.npc++;
            W.pend[c.nm + ",s," + c.sk] += 1;
        }

        void wv_send(Ws& W, const std::string& nm, const std::string& p, const std::string& vx, int& src, double now)
        {
            if (!(wv_safe(p) && ws_safe(nm))) { W.s.vlong++; return; }
            auto r = W.vrec.find(nm);
            if (r != W.vrec.end() && r->second.p == p && now - r->second.t < 3) { W.s.vdup++; return; }
            if (r == W.vrec.end())
            {
                W.vrecN++;
                if (W.vrecN > 300) { W.vrec.clear(); W.vrecN = 1; }
            }
            W.vrec[nm] = Ws::VRec{p, now};
            W.pend[nm + ",v," + p] = !vx.empty() ? (double)(unsigned char)vx[0] : 1.0;
            W.s.voice++;
            src++;
        }

        std::string wv_fix(Ws& W, const std::string& p, const std::string& sid, double now)
        {
            std::string nm = last_seg(p);
            if (valid(x0::find_path(wide(PFX() + p + "." + nm).c_str()))) { W.s.vok++; return p; }
            auto c = W.vfix.find(sid);
            if (c != W.vfix.end()) return c->second ? *c->second : p;
            if (W.vfixAt && now - *W.vfixAt < 2) return p;
            W.vfixAt = now;
            std::string q;
            for (const Ref& o : x0::all_of(L"AkAudioEvent", 2))
                if (valid(o) && x0::obj_name(o) == nm) { q = strip_class(x0::full_name(o)); break; }
            if (q.compare(0, PFX().size(), PFX()) == 0)
            {
                q = strip_dot(q.substr(PFX().size()));
                W.vfix[sid] = q;
                W.s.vfind++;
                return q;
            }
            W.vfix[sid] = std::nullopt;
            W.s.vmiss++;
            return p;
        }

        void wv_scan(Ws& W)
        {
            std::vector<VEnt> hum, oth;
            std::vector<const x0::Snap*> rs;
            for (int v = 0; v < x0::views(); ++v)
            {
                const x0::Snap& r = x0::remote(v);
                if (r.ok && r.has_pos) rs.push_back(&r);
            }
            if (rs.empty()) { W.vlist.clear(); return; }
            std::set<uint64_t> skip;
            Ref lp = local_pawn();
            if (valid(lp)) skip.insert(lp.address());
            for (int v = 0; v < x0::views(); ++v)
            {
                Ref pup = m01::puppet(v);
                if (valid(pup)) skip.insert(pup.address());
            }
            double R2 = std::pow(x0::fd("wsr", 6000), 2);
            size_t mx = (size_t)x0::fi("wvmaxn", 12);
            for (const Ref& o : x0::all_of(L"Pawn", 0))
            {
                if (hum.size() >= mx || hum.size() + oth.size() >= 40) break;
                if (!valid(o) || skip.count(o.address())) continue;
                Vec3 q;
                if (!loc(o, q)) continue;
                bool near = false;
                for (const x0::Snap* r : rs)
                    if (d2(q.x, q.y, r->x, r->y) <= R2) near = true;
                if (!(q.x == 0 && q.y == 0) && near)
                {
                    VEnt e;
                    e.o = o;
                    e.ad = o.address();
                    e.nm = x0::obj_name(o);
                    if (x0::fi("wvface", 0) == 1)
                    {
                        Ref bi = ocall(pref(o, "Mesh"), L"GetAnimInstance");
                        if (valid(bi))
                        {
                            e.bi = bi;
                            Ref fm = pref(bi, "FaceData.FaceMeshComponent");
                            Ref fa = ocall(fm, L"GetAnimInstance");
                            if (valid(fa)) e.fa = fa;
                        }
                    }
                    (valid(e.fa) ? hum : oth).push_back(e);
                }
            }
            for (auto& e : oth)
            {
                if (hum.size() >= mx) break;
                hum.push_back(e);
            }
            if (x0::fi("wvspk", 0) == 1)
            {
                std::map<uint64_t, size_t> byAd;
                for (size_t i = 0; i < hum.size(); ++i) byAd[hum[i].ad] = i;
                int ns = 0;
                for (const Ref& vc : x0::all_of(L"VoiceoverSpeechComponent", 0))
                {
                    ns++;
                    if (ns > 64) break;
                    if (!valid(vc)) continue;
                    Ref spk = pref(vc, "AkVoiceoverSpeakerComponent");
                    if (!valid(spk)) continue;
                    Ref ow = ocall(spk, L"GetOwner");
                    if (!valid(ow)) continue;
                    auto it = byAd.find(ow.address());
                    if (it != byAd.end()) hum[it->second].spk = spk;
                }
                W.vsc = ns;
            }
            W.vlist = hum;
        }

        std::optional<std::string> wv_anim(const Ref& mo)
        {
            std::string p = strip_class(x0::full_name(mo));
            if (p.find("/AS_FAC_") != std::string::npos) return p;
            Ref r = ocall(mo, L"GetFirstAnimReference");
            if (valid(r))
            {
                p = strip_class(x0::full_name(r));
                if (p.find("/AS_FAC_") != std::string::npos) return p;
            }
            return std::nullopt;
        }

        std::optional<std::string> wv_mont(Ws& W, const VEnt& e, std::string& ap)
        {
            for (const Ref* ai : {&e.fa, &e.bi})
            {
                if (!valid(*ai)) continue;
                Ref mo = ocall(*ai, L"GetCurrentActiveMontage");
                if (!valid(mo)) continue;
                std::string mk = addr_s(mo.address()) + ":" + x0::obj_name(mo);
                auto it = W.vma.find(mk);
                std::optional<std::string> an;
                if (it == W.vma.end())
                {
                    an = wv_anim(mo);
                    W.vmaN++;
                    if (W.vmaN > 500) { W.vma.clear(); W.vmaN = 1; }
                    W.vma[mk] = an;
                }
                else an = it->second;
                if (an) { ap = *an; return mk; }
            }
            return std::nullopt;
        }

        void wv_host(Ws& W, double now)
        {
            W.vInit = true;
            if (now - W.vAt >= 1) { W.vAt = now; wv_scan(W); }
            if (now - W.vPoll < 0.2) return;
            W.vPoll = now;
            for (auto& e : W.vlist)
            {
                if (!valid(e.o)) continue;
                W.s.vnpc++;
                std::string key = addr_s(e.ad) + ":" + e.nm;
                if (valid(e.fa) || valid(e.bi))
                {
                    std::string ap;
                    std::optional<std::string> cur = wv_mont(W, e, ap);
                    auto was = W.vm.find(key);
                    bool had = was != W.vm.end();
                    std::optional<std::string> wv = had ? was->second : std::nullopt;
                    if (!had)
                    {
                        W.vmN++;
                        if (W.vmN > 300) { W.vm.clear(); W.vmN = 1; }
                    }
                    W.vm[key] = cur;
                    if (had && cur && cur != wv)
                    {
                        auto rr = wv_rule(ap);
                        if (rr)
                        {
                            if (!rr->vx.empty()) W.vx[key] = rr->vx;
                            wv_send(W, e.nm, wv_fix(W, rr->path, rr->sid, now), rr->vx, W.s.vface, now);
                        }
                        else W.s.vnorule++;
                    }
                }
                if (valid(e.spk))
                {
                    Ref ev = pref(e.spk, "AkAudioEvent");
                    if (valid(ev))
                    {
                        std::string p = strip_class(x0::full_name(ev));
                        auto was = W.vl.find(key);
                        bool had = was != W.vl.end();
                        std::string wp = had ? was->second : std::string();
                        if (!had)
                        {
                            W.vlN++;
                            if (W.vlN > 300) { W.vl.clear(); W.vlN = 1; }
                        }
                        W.vl[key] = p;
                        if (had && wp != p && p.find("/Weapons/") == std::string::npos && p.compare(0, PFX().size(), PFX()) == 0)
                        {
                            auto vx = W.vx.find(key);
                            wv_send(W, e.nm, strip_dot(p.substr(PFX().size())), vx != W.vx.end() ? vx->second : std::string(), W.s.vspk, now);
                        }
                    }
                }
            }
        }

        void ws_flush(Ws& W, double now)
        {
            if (now - W.flushAt < 0.2) return;
            W.flushAt = now;
            for (auto& [key, n] : W.pend)
            {
                W.seq++;
                std::string ln = std::to_string(W.seq) + "," + key + "," + std::to_string((long long)std::floor(n));
                auto d = W.pdir.find(key);
                if (d != W.pdir.end()) ln += sf(",d%.1f:%.1f",d->second.first, d->second.second);
                W.q.push_back({now, ln});
                W.s.sent++;
            }
            W.pend.clear();
            W.pdir.clear();
            std::vector<WsQ> keep;
            for (auto& x : W.q)
                if (now - x.t < 0.6) keep.push_back(x);
            while (keep.size() > 30) keep.erase(keep.begin());
            W.q = keep;
        }

        Ref ws_asset(Ws& W, const std::string& p)
        {
            auto it = W.ev.find(p);
            if (it != W.ev.end() && valid(it->second)) return it->second;
            if (W.evMiss.count(p)) return {};
            Ref a = asset(p);
            if (valid(a)) W.ev[p] = a;
            else { W.ev.erase(p); W.evMiss.insert(p); }
            return a;
        }

        bool target_i(const std::string& nm, Ref& a, Ref& w, Ref& b)
        {
            a = w = b = Ref{};
            if (is_ply(nm))
            {
                int pv = ply_view(nm);
                if (pv < 0) return false;
                a = m01::puppet(pv);
                if (!valid(a)) return false;
                w = m01::puppet_weapon(pv);
                b = pref(a, "Mesh");
                return true;
            }
            uint64_t wa = 0, ma = 0;
            uint64_t aa = 0;
            try { aa = s2nat::actor(nm, wa, ma); }
            catch (...) { aa = 0; }
            if (aa)
            {
                Ref r = Ref::addr(aa);
                if (valid(r))
                {
                    a = r;
                    Ref rw = Ref::addr(wa), rb = Ref::addr(ma);
                    if (wa && valid(rw)) w = rw;
                    if (ma && valid(rb)) b = rb;
                    return true;
                }
            }
            return false;
        }

        void muzzle(Ws& W, const Ref& a, const Ref& w, const Ref& body, const std::string& sk, Vec3& L, Rot& R)
        {
            if (valid(w))
            {
                std::optional<std::string> mz;
                auto it = W.mz.find(sk);
                if (it != W.mz.end()) mz = it->second;
                else if (!W.mzNone.count(sk))
                {
                    for (auto& s : tab().mz)
                        if (sock_exists(w, s)) { mz = s; break; }
                    if (mz) W.mz[sk] = *mz;
                    else W.mzNone.insert(sk);
                }
                if (mz)
                {
                    sock_loc(w, *mz, L);
                    sock_rot(w, *mz, R);
                    return;
                }
                x0::call_v(w, L"K2_GetComponentLocation", L);
                Call c(w, L"K2_GetComponentRotation");
                if (c.run()) c.rr(R);
                return;
            }
            if (valid(body) && sock_exists(body, "jnt_r_hand"))
            {
                sock_loc(body, "jnt_r_hand", L);
                rot(a, R);
                return;
            }
            Vec3 q;
            loc(a, q);
            L = Vec3{q.x, q.y, q.z + (valid(body) ? 140 : 50)};
            rot(a, R);
        }

        Ref spawn_fx(const Ref& lib, const Ref& a, const Ref& ns, const Vec3& L, const Rot& R)
        {
            Call c(lib, L"SpawnSystemAtLocation");
            c.o(L"WorldContextObject", a).o(L"SystemTemplate", ns).v(L"Location", L).r(L"Rotation", R);
            c.v(L"Scale", Vec3{1, 1, 1}).b(L"bAutoDestroy", true).b(L"bAutoActivate", true).i(L"PoolingMethod", 0).b(L"bPreCullCheck", true);
            return c.run() ? c.ro() : Ref{};
        }

        void tr_kill(const Ref& c)
        {
            if (valid(c)) Call(c, L"K2_DestroyComponent").o(L"Object", c).run();
        }

        void tr_add(Ws& W, const Ref& c, const Vec3& L, const Rot& R)
        {
            if (!valid(c)) return;
            if (W.tr.size() >= 64)
            {
                tr_kill(W.tr.front().c);
                W.tr.erase(W.tr.begin());
            }
            double p = R.pitch * 3.14159265358979 / 180, y = R.yaw * 3.14159265358979 / 180;
            W.tr.push_back({c, L, Vec3{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), std::sin(p)}, x0::now()});
        }

        void tr_tick(Ws& W)
        {
            if (W.tr.empty()) return;
            double now = x0::now(), spd = x0::fd("wstrspd", 32000), life = x0::fd("wstrlife", 0.45);
            std::vector<Ws::Tr> keep;
            for (auto& t : W.tr)
            {
                double age = now - t.t0;
                if (!valid(t.c)) continue;
                if (age >= life) { tr_kill(t.c); continue; }
                double k = spd * age;
                Call(t.c, L"K2_SetWorldLocation").v(L"NewLocation", Vec3{t.o.x + t.d.x * k, t.o.y + t.d.y * k, t.o.z + t.d.z * k}).b(L"bSweep", false).b(L"bTeleport", false).run();
                keep.push_back(t);
            }
            W.tr = std::move(keep);
        }

        void ws_fx(Ws& W, const std::string& nm, const std::string& sk, bool tr, std::optional<std::pair<double, double>> aim = std::nullopt)
        {
            Ref a, w, body;
            bool ply = is_ply(nm);
            if (!target_i(nm, a, w, body)) { (ply ? W.s.pnotgt : W.s.notgt)++; return; }
            double now = x0::now();
            if (now - W.budAt >= 1) { W.budAt = now; W.bud = 0; }
            if (W.bud >= x0::fi("wsmax", 30)) { W.s.cap++; return; }
            W.bud++;
            const WsRow& row = shot_row(sk);
            auto fam = fam_of(row.fam);
            Vec3 L;
            Rot R;
            muzzle(W, a, w, body, sk, L, R);
            if (ply && x0::fi("wsaim", 1) == 1)
            {
                int pv = ply_view(nm);
                if (pv >= 0)
                {
                    const x0::Snap& s = x0::remote(pv);
                    if (s.ok && s.has_pt) R = Rot{s.pt, s.yaw, 0};
                }
            }
            Ref ev = ws_asset(W, PFX() + row.ev);
            Ref ak = x0::find_path(L"/Script/AkAudio.Default__AkGameplayStatics");
            if (valid(ev)) Call(ak, L"PostEventAtLocation").o(L"AkEvent", ev).v(L"Location", L).r(L"Orientation", R).o(L"WorldContextObject", a).run();
            else W.s.noev++;
            Ref nf = x0::find_path(L"/Script/Niagara.Default__NiagaraFunctionLibrary");
            Ref ns = ws_asset(W, "/Game/_Stalker_2/VFX/Weapons/MuzzleTP/NS_MuzzleTP_" + fam.first + ".NS_MuzzleTP_" + fam.first);
            if (valid(ns)) spawn_fx(nf, a, ns, L, R);
            if (tr)
            {
                Ref ts = ws_asset(W, "/Game/_Stalker_2/VFX/Weapons/a_Trace/NS_Projectile_NPC_" + fam.second + ".NS_Projectile_NPC_" + fam.second);
                if (valid(ts))
                {
                    Rot TR = R;
                    if (aim)
                    {
                        Vec3 q;
                        loc(a, q);
                        double p = aim->first * 3.14159265358979 / 180, y = aim->second * 3.14159265358979 / 180, D = x0::fd("wstraim", 3000);
                        Vec3 t{q.x + std::cos(p) * std::cos(y) * D, q.y + std::cos(p) * std::sin(y) * D, q.z + x0::fd("wstreye", 64) + std::sin(p) * D};
                        double dx = t.x - L.x, dy = t.y - L.y, dz = t.z - L.z, h = std::sqrt(dx * dx + dy * dy);
                        TR = Rot{std::atan2(dz, h) * 180 / 3.14159265358979, std::atan2(dy, dx) * 180 / 3.14159265358979, 0};
                        W.s.pdir++;
                    }
                    Ref c = spawn_fx(nf, a, ts, L, TR);
                    if (x0::fi("wstrfly", 1) == 1) tr_add(W, c, L, TR);
                }
                if (ply && valid(ts)) W.s.ptr++;
            }
            if (!ply) { W.s.play++; return; }
            W.s.pplay++;
            ::m01::note_partner_shot(ply_view(nm));
            if (valid(ev)) W.s.psnd++;
            if (valid(ns)) W.s.pmz++;
            if (valid(w)) W.s.pw++;
            W.plyRx = sk;
        }

        bool wv_switch(Ws& W, const Ref& G, const Ref& ev, const std::string& vx, const Vec3& L, const Ref& a)
        {
            Ref sw = ws_asset(W, "/Game/_STALKER2/Audio/WwiseAudio/Switches/Voiceover/Voice/Voice-" + vx + ".Voice-" + vx);
            if (!valid(sw)) return false;
            Rot R;
            rot(a, R);
            Call sp(G, L"SpawnAkComponentAtLocation");
            sp.o(L"WorldContextObject", a).o(L"AkEvent", ev).v(L"Location", L).r(L"Orientation", R).b(L"AutoPost", false).b(L"AutoDestroy", true);
            Ref c = sp.run() ? sp.ro() : Ref{};
            if (!valid(c)) return false;
            Call(c, L"SetSwitch").o(L"SwitchValue", sw).run();
            W.vuid = W.vuid % 1000000 + 1;
            struct
            {
                int32_t linkage;
                int32_t uuid;
                uint32_t fnIdx;
                uint32_t fnNum;
                uint64_t target;
            } li{-1, 146000000 + W.vuid, 0, 0, c.address()};
            Call pe(c, L"PostAkEventAndWaitForEnd");
            pe.o(L"AkEvent", ev).raw(L"LatentInfo", &li, (int)sizeof li);
            if (!pe.run()) return false;
            return pe.ri() != 0;
        }

        void wv_play(Ws& W, const WsEnt& e, double now)
        {
            if (x0::fi("wvoice", 0) < 1) return;
            auto g = W.vgap.find(e.nm);
            if (now - (g != W.vgap.end() ? g->second : -100) < 1.5) { W.s.cap++; return; }
            Ref a, w, body;
            if (!target_i(e.nm, a, w, body)) { W.s.notgt++; return; }
            std::string pl = (!e.pl.empty() && e.pl[0] == '/') ? e.pl : (PFX() + e.pl);
            if (!has_dot_leaf(pl)) pl = pl + "." + last_seg(pl);
            Ref ev = ws_asset(W, pl);
            if (!valid(ev)) { W.s.noev++; return; }
            W.vgap[e.nm] = now;
            Vec3 q;
            loc(a, q);
            Vec3 L{q.x, q.y, q.z + (valid(body) ? 160 : 70)};
            if (valid(body) && sock_exists(body, "jnt_head"))
            {
                Vec3 h;
                if (sock_loc(body, "jnt_head", h)) L = h;
            }
            Ref G = x0::find_path(L"/Script/AkAudio.Default__AkGameplayStatics");
            bool hasVx = e.n >= 48 && e.n <= 90;
            if (x0::fi("wvsw", 0) == 1 && hasVx)
            {
                bool r = false;
                try
                {
                    r = wv_switch(W, G, ev, std::string(1, (char)(int)e.n), L, a);
                }
                catch (const std::exception& ex)
                {
                    ws_err(W, "voz_troca", what(ex));
                }
                if (r)
                {
                    W.s.vsw++;
                    W.s.vplay++;
                    return;
                }
                W.s.vswf++;
            }
            Rot R;
            rot(a, R);
            Call(G, L"PostEventAtLocation").o(L"AkEvent", ev).v(L"Location", L).r(L"Orientation", R).o(L"WorldContextObject", a).run();
            W.s.vplay++;
        }

        void ws_play(Ws& W, const WsEnt& e, double now)
        {
            bool ply = is_ply(e.nm);
            int lvl = ply ? x0::fi("wsply", 2) : x0::fi("wsfx", 0);
            if (lvl < 1) return;
            if (e.k == "v") { wv_play(W, e, now); return; }
            const WsRow& row = shot_row(e.pl);
            int n = (int)std::min<double>(e.n, (double)x0::fi(ply ? "wsplynmax" : "wsnmax", ply ? 8 : 4));
            double fi = std::max(row.fi, 0.05);
            if (ply && n > 1) fi = std::min(fi, 0.18 / (n - 1));
            bool tr = lvl >= 2;
            std::optional<std::pair<double, double>> aim;
            if (e.dir && x0::fi("wstrdir", 1) == 1) aim = std::make_pair(e.ap, e.ay);
            ws_fx(W, e.nm, e.pl, tr, aim);
            for (int i = 2; i <= n; ++i)
            {
                long long ms = (long long)std::floor(fi * 1000 * (i - 1));
                if (ms > 190) break;
                std::string nm = e.nm, pl = e.pl;
                x0::after(MOD, (double)ms / 1000.0, [nm, pl, tr, aim] {
                    if (!g_ws) return;
                    try { ws_fx(*g_ws, nm, pl, tr, aim); }
                    catch (const std::exception& ex) { ws_err(*g_ws, "atraso", what(ex)); }
                });
            }
        }

        void ws_client(Ws& W, double now)
        {
            bool host = x0::is_host();
            int nv = host ? std::min(x0::views(), (int)x0::kMaxClients) : 1;
            for (int v = 0; v < nv; ++v)
            {
                std::string part;
                if (!(host ? x0::rpart("WS", part, v) : x0::rpart("WS", part)) || part.empty()) continue;
                for (auto& e : ws_parse(part))
                {
                    if (e.sq <= W.last[v] && now - W.rxAt[v] > 2) W.last[v] = e.sq - 1;
                    if (e.sq <= W.last[v]) continue;
                    W.last[v] = e.sq;
                    W.rxAt[v] = now;
                    W.s.rx++;
                    if (host)
                    {
                        if (!is_ply(e.nm)) continue;
                        e.nm = PLY + std::to_string(x0::view_pid(v));
                    }
                    try { ws_play(W, e, now); }
                    catch (const std::exception& ex) { ws_err(W, "tocar", what(ex)); }
                    if (host && x0::views() > 1 && x0::fi("wsply", 2) >= 1 && W.pend.size() < 64)
                    {
                        double cap = (double)x0::fi("wsplynmax", 8);
                        W.pend[e.nm + ",s," + e.pl] += std::min(e.n, cap);
                        if (e.dir) W.pdir[e.nm + ",s," + e.pl] = {e.ap, e.ay};
                    }
                }
            }
        }

        void ws_tick_i()
        {
            if (x0::fi("wshot", 0) != 1 && x0::fi("wsfx", 0) < 1 && x0::fi("wvoice", 0) < 1 && x0::fi("wsply", 2) < 1) return;
            Ws& W = WS();
            double now = x0::now();
            if (x0::is_host() && x0::fi("wvoice", 0) == 1)
            {
                try { wv_host(W, now); }
                catch (const std::exception& ex) { ws_err(W, "voz", what(ex)); }
            }
            ws_flush(W, now);
            if (!x0::is_host() || x0::fi("wsply", 2) >= 1)
            {
                try { ws_client(W, now); }
                catch (const std::exception& ex) { ws_err(W, "cliente", what(ex)); }
            }
            WsS& s = W.s;
            if (now - s.at >= 10)
            {
                if (s.ply + s.pplay + s.pnotgt > 0)
                    LOG(sf("NOLA wshot 10 s (jogador): meus_tiros=%d arma=%s | parceiro: tocados=%d som=%d clarao=%d tracante=%d mira=%d arma_avatar=%d sem_avatar=%d arma_rx=%s", s.ply, W.plySk.c_str(), s.pplay, s.psnd, s.pmz,
                           s.ptr, s.pdir, s.pw, s.pnotgt, W.plyRx.c_str()));
                if (x0::is_host())
                {
                    if (s.hook + s.voice + s.sent + s.err + s.vnpc > 0)
                        LOG(sf("NOLA wshot 10 s (host): gancho=%d npc=%d meu=%d sem_self=%d longe=%d | voz: npcs_lidos=%d falas=%d (rosto=%d falante=%d comps=%d) evento ok=%d achado=%d falta=%d sem_regra=%d dup=%d longo=%d | enviados=%d erros=%d",
                               s.hook, s.npc, s.mine, s.noself, s.far, s.vnpc, s.voice, s.vface, s.vspk, W.vsc, s.vok, s.vfind, s.vmiss, s.vnorule, s.vdup, s.vlong, s.sent, s.err));
                }
                else if (s.rx + s.err > 0)
                    LOG(sf("NOLA wshot 10 s (cliente): recebidos=%d tiros=%d vozes=%d (troca=%d falhou=%d) sem_alvo=%d sem_evento=%d corte=%d erros=%d", s.rx, s.play, s.vplay, s.vsw, s.vswf, s.notgt, s.noev, s.cap,
                           s.err));
                W.s = WsS{};
                W.s.at = now;
            }
        }
    }

    bool target(const std::string& nm, x0::Ref& actor, x0::Ref& weapon, x0::Ref& body) { return target_i(nm, actor, weapon, body); }

    std::string out_ws()
    {
        if (!g_ws || g_ws->q.empty()) return "";
        std::string s = "#WS:";
        for (size_t i = 0; i < g_ws->q.size(); ++i) s += (i ? ";" : "") + g_ws->q[i].s;
        return s;
    }

    void on_shot(const x0::Ref& self)
    {
        if (!(x0::fi("wshot", 0) == 1 && x0::is_host()) && x0::fi("wsply", 2) < 1) return;
        Ws& W = WS();
        try { ws_shot(W, self); }
        catch (const std::exception& e) { ws_err(W, "gancho", e.what()); }
    }

    void tick_ws() { ws_tick_i(); }

    void tick_tr()
    {
        if (!g_ws) return;
        try { tr_tick(*g_ws); }
        catch (const std::exception& e) { ws_err(*g_ws, "tracante", e.what()); }
    }

    void reset() { g_ws.reset(); }
}

namespace
{
    void wshot_init()
    {
        constexpr x0::Mod M = x0::Mod::npc;
        x0::own_tag(M, "WS", [] { return s2m_wshot::out_ws(); });
        x0::every(M, "wshotWs", x0::Rate::t100, [] { s2m_wshot::tick_ws(); }, x0::ord::t100_wsTick);
        x0::every(M, "wshotTr", x0::Rate::frame, [] { s2m_wshot::tick_tr(); });
        x0::hook(M, L"/Script/Stalker2.ItemAppearanceComponent:OnShotSoundStart", false, [](const x0::Ref& self) { s2m_wshot::on_shot(self); });
    }
    ::x0::ModReg g_wshot_reg(::x0::Mod::npc, "wshot", &wshot_init);
}
