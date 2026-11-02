#include "s2m_bodies_logic.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lg = m01::lg;

struct FlagV
{
    double v;
    bool isint;
};
static std::map<std::string, FlagV> g_fl;
static int g_line = 0;

static double FL(const char* k, double d)
{
    auto it = g_fl.find(k);
    return it == g_fl.end() ? d : it->second.v;
}
static std::string FS(const char* k)
{
    auto it = g_fl.find(k);
    if (it == g_fl.end()) return "nil";
    return it->second.isint ? lg::inum((int64_t)it->second.v) : lg::num(it->second.v);
}
static void LOG(const std::string& s) { std::printf("L %d %s\n", g_line, s.c_str()); }
static const lg::Ctx C{&FL, &FS, &LOG};

static bool isnil(const std::string& t) { return t == "nil"; }
static double D(const std::string& t) { return std::strtod(t.c_str(), nullptr); }
static std::optional<double> OD(const std::string& t) { if (isnil(t)) return std::nullopt; return D(t); }
static std::optional<std::string> OS(const std::string& t) { if (isnil(t)) return std::nullopt; return t; }
static std::string G(double v) { return lg::fmt("%.9g", v); }
static std::string GO(const std::optional<double>& v) { return v ? G(*v) : std::string("nil"); }
static std::string SO(const std::optional<std::string>& v) { return v ? *v : std::string("nil"); }
static void R(const std::string& s) { std::printf("R %d %s\n", g_line, s.c_str()); }

struct DmgEvent
{
    int64_t seq = 0;
    double amount = 0;
    double at = 0;
};
struct Dmg
{
    int64_t seq = 0;
    double total = 0;
    int64_t dropped = 0;
    std::vector<DmgEvent> list;
};
struct PupSt
{
    lg::PupPos pos;
    std::optional<double> yaw, lastT, pfH;
    lg::RVel rvel;
    lg::SmStat sm;
    lg::PfState pf;
    std::optional<double> jumpUntil;
    bool hidden = false;
};
struct TgtSt
{
    std::optional<double> ref, realAt;
    Dmg dmg;
    std::optional<double> jumpUntil;
    std::optional<lg::Quiet> quiet;
    std::optional<lg::Shot> shot;
};
struct PaSt
{
    lg::PaState pa;
    int clipBad = 0, clipN = 0;
    std::optional<std::string> clip, paLog, paFace;
    std::optional<double> rate;
    std::map<std::string, bool> miss, loaded;
};
struct NsT
{
    lg::Mover K;
    lg::NB nb;
    lg::NsS s = lg::ns_s0(0);
};
struct PfxT
{
    lg::PfxH H;
    std::vector<double> R;
};

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    std::ifstream in(argv[1]);
    std::string line;
    PupSt pup;
    TgtSt tgt;
    PaSt pa;
    NsT ns;
    PfxT pfx;
    g_fl["paaimmv"] = FlagV{0, true};
    g_fl["pacwalk"] = FlagV{0, true};
    g_fl["paladder"] = FlagV{0, true};
    g_fl["paequip"] = FlagV{0, true};
    while (std::getline(in, line))
    {
        g_line++;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> t;
        {
            std::istringstream ss(line);
            std::string w;
            while (ss >> w) t.push_back(w);
        }
        if (t.empty()) continue;
        const std::string& c = t[0];
        if (c == "F")
        {
            if (isnil(t[2])) g_fl.erase(t[1]);
            else g_fl[t[1]] = FlagV{D(t[2]), t[2].find_first_of(".eE") == std::string::npos};
        }
        else if (c == "RESET")
        {
            pup = PupSt{};
            tgt = TgtSt{};
            pa = PaSt{};
            ns = NsT{};
            pfx = PfxT{};
        }
        else if (c == "fam") R(lg::pa_fam(t.size() > 1 ? t[1] : std::string()));
        else if (c == "sec") R(lg::pa_sector(OD(t[1]), D(t[2]), D(t[3]), D(t[4])));
        else if (c == "pick")
        {
            auto p = lg::pa_pick(C, D(t[1]), t[2], t[3] == "1", t[4] == "1", t[5] == "1", OS(t[6]));
            R(p.first + " " + p.second);
        }
        else if (c == "clip")
        {
            int miss = (int)D(t[3]);
            auto pk = lg::pa_clip(C, t[1], t[2], [&](const lg::PaEnt& e) {
                std::string full = lg::pa_path(e.rel);
                bool ok = !(miss > 0 && (int)full.size() % miss == 0);
                if (!ok) LOG("NOLA pupanim: clipe nao carregou " + full);
                return ok;
            });
            if (!pk) R("nil");
            else R(std::string(pk->e->rel) + " " + pk->st + " " + (pk->e->has_nat ? G(pk->e->nat) : std::string("nil")));
        }
        else if (c == "mv")
        {
            auto m = lg::mv_parse(t.size() > 1 ? t[1] : std::string());
            R(SO(m.c) + " " + SO(m.a) + " " + (m.f ? (m.f->empty() ? std::string("<e>") : *m.f) : std::string("nil")));
        }
        else if (c == "life") R(SO(lg::remote_life(OS(t[1]))));
        else if (c == "ang") R(G(lg::ang_diff(D(t[1]), D(t[2]))));
        else if (c == "num") R(lg::num(D(t[1])));
        else if (c == "pup")
        {
            double now = D(t[1]);
            lg::RSnap r;
            r.recv = D(t[2]);
            r.seq = (uint64_t)D(t[3]);
            r.x = D(t[4]);
            r.y = D(t[5]);
            r.z = D(t[6]);
            r.vx = D(t[7]);
            r.vy = D(t[8]);
            r.yaw = OD(t[9]);
            r.t = OD(t[10]);
            auto fh = OS(t[11]);
            auto own = OD(t[12]);
            bool away = now - r.recv > 5;
            if (away != pup.hidden)
            {
                pup.hidden = away;
                LOG(std::string("NOLA puppet: escondido=") + lg::tf(away) + " ok=true");
            }
            if (away)
            {
                R(pup.pos.has ? G(pup.pos.x) + " " + G(pup.pos.y) + " " + G(pup.pos.z) + " " + GO(pup.yaw) : std::string("nil nil nil ") + GO(pup.yaw));
                continue;
            }
            auto v = lg::r_vel(C, pup.rvel, r, now);
            lg::PupStepIn pin;
            pin.now = now;
            pin.recv = r.recv;
            pin.rx = r.x;
            pin.ry = r.y;
            pin.ryaw = r.yaw;
            pin.vx = v.first;
            pin.vy = v.second;
            pin.tz = lg::pf_z(C, r.z, fh, own, pup.pfH, pup.pf);
            lg::PupStepOut o = lg::pup_step(C, pup.pos, pup.yaw, pup.lastT, pin);
            if (o.jump)
            {
                pup.jumpUntil = now + 1.5;
                LOG(lg::fmt("NOLA puppet: salto de %.0f m; corpos movidos de uma vez, leitura de dano suspensa 1.5 s", o.jump_m));
            }
            lg::sm_stat(C, pup.sm, r.recv, now, o.step);
            R(G(pup.pos.x) + " " + G(pup.pos.y) + " " + G(pup.pos.z) + " " + GO(pup.yaw));
        }
        else if (c == "tjump") tgt.jumpUntil = D(t[1]);
        else if (c == "tquiet") tgt.quiet = lg::Quiet{D(t[1]), D(t[2]), D(t[3]), D(t[4])};
        else if (c == "tshot") tgt.shot = lg::Shot{D(t[1]), D(t[2]), D(t[3]), D(t[4]), D(t[5]), D(t[6]), D(t[7])};
        else if (c == "tgt")
        {
            double now = D(t[1]), hp = D(t[2]);
            double px = D(t[3]), py = D(t[4]), pz = D(t[5]);
            if (tgt.ref && hp < *tgt.ref - 0.05)
            {
                Dmg& Dm = tgt.dmg;
                double amount = (*tgt.ref - hp) * FL("dmgscale", 1);
                lg::HitIn hi;
                hi.ref = *tgt.ref;
                hi.hp = hp;
                hi.now = now;
                hi.px = px;
                hi.py = py;
                hi.pz = pz;
                hi.jump_until = tgt.jumpUntil;
                hi.quiet = tgt.quiet;
                hi.shot = tgt.shot;
                auto why = lg::hit_why(C, hi, amount, tgt.realAt);
                if (why)
                {
                    Dm.dropped++;
                    if (Dm.dropped <= 20 || Dm.dropped % 50 == 0) LOG(lg::fmt("NOLA target: dano %.1f IGNORADO (%s) ignorados=%d", amount, why->c_str(), (int)Dm.dropped));
                }
                else
                {
                    Dm.seq++;
                    Dm.total += amount;
                    Dm.list.push_back(DmgEvent{Dm.seq, amount, now});
                    LOG(lg::fmt("NOLA target: DANO seq=%d %.1f (hp %.1f -> %.1f) total=%.1f vivo=%s", (int)Dm.seq, amount, *tgt.ref, hp, Dm.total, "true"));
                }
            }
            if (hp < lg::kTgtHp - 0.05) hp = lg::kTgtHp;
            tgt.ref = hp;
            R(GO(tgt.ref) + " " + lg::inum(tgt.dmg.seq) + " " + G(tgt.dmg.total) + " " + lg::inum(tgt.dmg.dropped));
        }
        else if (c == "pa")
        {
            double now = D(t[1]);
            double recv = D(t[2]);
            auto ryaw = OD(t[3]);
            auto rt = OD(t[4]);
            double speed = D(t[5]), vx = D(t[6]), vy = D(t[7]);
            std::string wstr = isnil(t[8]) ? std::string() : t[8];
            auto mvs = OS(t[9]);
            int miss = (int)D(t[10]);
            if (pa.clipBad < 5)
            {
                lg::pa_turn_feed(C, pa.pa, ryaw, rt, recv, now);
                auto turn = lg::pa_turn_now(pa.pa, now);
                lg::Mv mv = lg::mv_parse(mvs.value_or(""));
                std::string sec = lg::pa_sector(ryaw, vx, vy, speed);
                auto pk = lg::pa_pick(C, speed, sec, mv.c && *mv.c == "1", mv.a && *mv.a == "1", mv.f && *mv.f == "1", turn);
                std::string fam = lg::pa_famc(C, wstr);
                std::string st = lg::pa_pistol_run(C, pk.first, sec, speed, fam);
                lg::pa_hold(C, pa.pa, st, pk.second);
                pa.paFace = pa.pa.face;
                auto pick = lg::pa_clip(C, *pa.pa.st, fam, [&](const lg::PaEnt& e) {
                    std::string full = lg::pa_path(e.rel);
                    if (pa.loaded.count(full)) return true;
                    if (pa.miss.count(full)) return false;
                    bool ok = !(miss > 0 && (int)full.size() % miss == 0);
                    if (!ok) { pa.miss[full] = true; LOG("NOLA pupanim: clipe nao carregou " + full); }
                    else pa.loaded[full] = true;
                    return ok;
                });
                if (!pick)
                {
                    pa.clipBad++;
                    LOG(lg::fmt("NOLA pupanim: estado %s familia %s sem clipe (falha %d)%s", pa.pa.st->c_str(), fam.c_str(), pa.clipBad, pa.clipBad == 5 ? " -> clipes desligados para este corpo" : ""));
                }
                else
                {
                    const lg::PaEnt& e = *pick->e;
                    double rate = lg::pa_rate(C, e, speed);
                    std::string ck = std::string("pa:") + e.rel;
                    if (!pa.clip || ck != *pa.clip)
                    {
                        pa.clipN++;
                        if (pa.clipN <= 30 || pa.clipN % 50 == 0 || pa.clipBad == 5)
                            LOG(lg::fmt("NOLA pupanim: estado %s -> %s (clipe de %s, familia %s, v=%.0f dir=%s agach=%s mira=%s queda=%s giro=%s olhar=%s ritmo=%.2f) %s ok=%s montagem=%s%s%s",
                                        SO(pa.paLog).c_str(), pa.pa.st->c_str(), pick->st.c_str(), fam.c_str(), speed, sec.c_str(), SO(mv.c).c_str(), SO(mv.a).c_str(), SO(mv.f).c_str(),
                                        SO(turn).c_str(), pa.pa.face->c_str(), rate, lg::leaf_of(e.rel, '/').c_str(), "true", "true", "", pa.clipBad == 5 ? " -> clipes desligados para este corpo" : ""));
                        pa.clip = ck;
                        pa.rate = rate;
                        pa.paLog = pa.pa.st;
                    }
                    else if (std::fabs(rate - pa.rate.value_or(1)) > 0.15) pa.rate = rate;
                }
            }
            R(SO(pa.pa.st) + " " + SO(pa.pa.face) + " " + SO(pa.clip) + " " + GO(pa.rate) + " " + lg::inum(pa.clipBad) + " " + SO(pa.paFace));
        }
        else if (c == "nsk") ns.K = lg::Mover{};
        else if (c == "nss")
        {
            bool ok = lg::ns_sample(C, ns.K, D(t[1]), D(t[2]), D(t[3]), OD(t[4]), OD(t[5]), D(t[6]), OD(t[7]), OD(t[8]), OD(t[9]), t[10] == "1");
            R(ok ? "true" : "false");
        }
        else if (c == "nsd")
        {
            double now = D(t[1]);
            std::optional<double> cur;
            if (ns.K.yaw) cur = D(t[5]);
            lg::DriveOut o = lg::ns_drive(C, ns.nb, ns.s, ns.K, D(t[2]), D(t[3]), D(t[4]), cur, now);
            if (!o.ok) R("false");
            else
                R(std::string("true ") + (o.snap ? G(o.sx) + "," + G(o.sy) + "," + G(o.sz) : std::string("-")) + " " + G(o.vx) + " " + G(o.vy) + " " + G(o.vz) + " " +
                  (o.rot ? G(o.yaw) : std::string("-")) + " " + lg::inum(ns.s.dr) + " " + lg::inum(ns.s.sn) + " " + G(ns.s.emax));
        }
        else if (c == "pfx")
        {
            double now = D(t[1]);
            auto path = OS(t[2]);
            std::optional<int64_t> uid;
            if (!isnil(t[3])) uid = (int64_t)D(t[3]);
            double ax = D(t[4]), ay = D(t[5]), az = D(t[6]), wx = D(t[7]), wy = D(t[8]), speed = D(t[9]);
            double f = std::sqrt((ax - wx) * (ax - wx) + (ay - wy) * (ay - wy));
            R(lg::pfx_tick(C, pfx.H, pfx.R, path, uid, f, ax, ay, az, speed, now) == lg::Pfx::recycle ? "true" : "false");
        }
        else R("?" + c);
    }
    return 0;
}
