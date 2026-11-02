#include "s2m_quest.h"
#include "s2m_text.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace s2m_quest;

static std::vector<std::string> OUTL;
static void emit(const std::string& s) { OUTL.push_back(s); }
static void stub(const std::string& s) { emit("STUB " + s); }

static std::string fmtnum(double v)
{
    char b[64];
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
    else std::snprintf(b, sizeof b, "%.14g", v);
    return b;
}

static bool isnum(const std::string& s, double& v)
{
    if (s.empty()) return false;
    char* e = nullptr;
    v = std::strtod(s.c_str(), &e);
    return e && *e == '\0';
}

struct World
{
    double t = 0;
    Id pw = 0;
    std::map<Id, std::map<std::string, int>> inv;
    double cash = 0;
    V3 pos;
    std::map<std::string, std::string> parts;
    std::optional<double> gs;
    bool aa = true, aaFail = false, dk = true, rk = true, mp = true, loadOk = true;
    std::string lead = "none";
    bool hostDlg = false, deny = false, settled = true, dead = false, keyd = false, invOpen = false, joinp = false;
    std::string busy;
    std::vector<std::pair<bool, bool>> dlgViews;
    std::string n7stage = "idle";
    std::string followTo;
    Id hv = 0, inter = 0;
    std::string role = "client";
    std::map<std::string, std::string> flags;
    int peerState = 0, sessions = 0;
    bool loopback = false;
    int nviews = 1;
    std::map<int, std::map<std::string, std::string>> vparts;
    std::map<int, int> vstate, vsess;
    std::map<int, bool> vloop;
    std::set<std::string> qbAct, dlgAns, muted;
    bool qbErr = false;
};

static World Wd;
static fs::path BR, DATA;
static std::vector<std::string> LOOT;
static s2m_save::QState QS;
static s2m_save::Counts AP;

static std::string rd(const fs::path& p, bool& ok)
{
    std::ifstream f(p, std::ios::binary);
    ok = static_cast<bool>(f);
    if (!ok) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool wire_part(const char* tag, const std::string& sent, std::string& out)
{
    std::string pat = "#" + std::string(tag) + ":";
    std::string wire = !sent.empty() && sent[0] == '#' ? sent : pat + sent;
    size_t p = wire.find(pat);
    if (p == std::string::npos) return false;
    size_t b = p + pat.size(), e = wire.find('#', b);
    out = wire.substr(b, e == std::string::npos ? std::string::npos : e - b);
    return true;
}

struct FakeEnv final : Env
{
    double now() override { return Wd.t; }
    bool host() override { return Wd.role == "host"; }
    double fl(const char* key, double def) override
    {
        auto it = Wd.flags.find(key);
        double v;
        if (it == Wd.flags.end() || !isnum(it->second, v)) return def;
        return v;
    }
    std::string fls(const char* key) override
    {
        auto it = Wd.flags.find(key);
        return it == Wd.flags.end() ? std::string() : it->second;
    }
    void fset(const char* key, double v, const char*) override { Wd.flags[key] = fmtnum(v); }
    void log(const std::string& line) override { emit("LOG " + line); }
    void screen(const char* id, const std::string& text, double secs) override { emit(std::string("SCREEN ") + id + " " + fmtnum(secs) + " " + text); }
    bool part(const char* tag, std::string& out) override
    {
        auto it = Wd.parts.find(tag);
        if (it == Wd.parts.end()) return false;
        return wire_part(tag, it->second, out);
    }
    int peer_state() override { return Wd.peerState; }
    int peer_sessions() override { return Wd.sessions; }
    bool peer_loopback() override { return Wd.loopback; }
    int nviews() override { return Wd.nviews; }
    bool part_v(int v, const char* tag, std::string& out) override
    {
        if (v == 0) return part(tag, out);
        auto vi = Wd.vparts.find(v);
        if (vi == Wd.vparts.end()) return false;
        auto it = vi->second.find(tag);
        if (it == vi->second.end()) return false;
        return wire_part(tag, it->second, out);
    }
    int peer_state_v(int v) override { return v == 0 ? Wd.peerState : Wd.vstate[v]; }
    int peer_sessions_v(int v) override { return v == 0 ? Wd.sessions : Wd.vsess[v]; }
    bool peer_loopback_v(int v) override { return v == 0 ? Wd.loopback : Wd.vloop[v]; }
    int view_pid(int v) override { return host() ? v + 1 : (v == 0 ? 0 : -1); }
    bool look_ok() override { return true; }
    std::string look_put(const std::string& text) override { return "H" + std::to_string(text.size()) + text.substr(0, 4); }
    bool read(const std::string& name, std::string& out) override
    {
        bool ok;
        out = rd(BR / name, ok);
        return ok;
    }
    bool write(const std::string& name, const std::string& text) override
    {
        std::ofstream f(BR / name, std::ios::binary | std::ios::trunc);
        f << text;
        return static_cast<bool>(f);
    }
    bool append(const std::string& name, const std::string& text) override
    {
        std::ofstream f(BR / name, std::ios::binary | std::ios::app);
        f << text;
        return static_cast<bool>(f);
    }
    bool exists(const std::string& name) override { return fs::exists(BR / name); }
    bool remove(const std::string& name) override
    {
        std::error_code ec;
        return fs::remove(BR / name, ec);
    }
    bool rename(const std::string& a, const std::string& b) override
    {
        std::error_code ec;
        fs::rename(BR / a, BR / b, ec);
        return !ec;
    }
    bool data(const char* name, std::string& out) override
    {
        bool ok;
        out = rd(DATA / name, ok);
        if (ok) return true;
        out = rd(BR / name, ok);
        return ok;
    }
    int64_t unix_time() override { return 1700000000; }
    int rnd(int, int) override { return 4660; }
    std::optional<double> game_secs() override { return Wd.gs; }
    Id pawn() override { return Wd.pw; }
    Id pawn_pc() override { return Wd.pw; }
    bool pos(Id a, V3& out) override
    {
        if (!a) return false;
        out = Wd.pos;
        return true;
    }
    Id interacted(Id) override { return Wd.inter; }
    int count(Id obj, const std::string& sid) override
    {
        auto& m = Wd.inv[obj];
        auto it = m.find(sid);
        return it == m.end() ? 0 : it->second;
    }
    bool give(const std::string& sid, int n, std::string&) override
    {
        stub("createItem " + sid + " " + std::to_string(n));
        Wd.inv[Wd.pw][sid] += n;
        return true;
    }
    bool aa_present() override { return Wd.aa; }
    int trig_enter(const std::string& guid, int mode, std::string& info) override
    {
        stub(std::string("trig ") + guid + (mode == 0 ? " x" : mode == 1 ? " hold" : " leave"));
        info = "fake";
        return 1;
    }
    int aa(const char* fn, const std::string& sid, std::string& err) override
    {
        if (!Wd.aa) return mgr_missing;
        stub(std::string("aa ") + fn + " " + sid);
        if (Wd.aaFail)
        {
            err = "falhou";
            return call_failed;
        }
        if (sid.rfind("S2QI_", 0) == 0 && std::string(fn) == "XStartQuestNodeBySID" && sid.size() > 5) Wd.inv[Wd.pw].erase(sid.substr(5));
        return call_ok;
    }
    int dk(const char* fn, const std::string& a, const std::string* b, int win, std::string&) override
    {
        if (!Wd.dk) return mgr_missing;
        std::string s = std::string("dk ") + fn + " " + a;
        if (b) s += " " + *b;
        if (win >= 0) s += win == 1 ? " true" : " false";
        stub(s);
        return call_ok;
    }
    int rk_bool(const std::string& var, bool v, std::string&) override
    {
        if (!Wd.rk) return mgr_missing;
        stub("rk XSetGlobalBool " + var + (v ? " true" : " false"));
        return call_ok;
    }
    bool load_last_save(std::string& err) override
    {
        if (!Wd.mp)
        {
            err = "CustomConsoleManagerMP ausente";
            return false;
        }
        stub("loadLastSave");
        if (!Wd.loadOk)
        {
            err = "falhou";
            return false;
        }
        return true;
    }
    bool journal(int*) override { return false; }
    Inv inv_snap(Id pw) override
    {
        Inv r;
        for (auto& s : LOOT)
        {
            int n = count(pw, s);
            if (n > 0) r.items[s] = n;
        }
        r.money = Wd.cash;
        return r;
    }
    double money(Id) override { return Wd.cash; }
    bool add_money(Id, int64_t d) override
    {
        stub("modifyMoney " + std::to_string(d));
        Wd.cash += static_cast<double>(d);
        return true;
    }
    bool qi_npcadd(const std::string&) override { return false; }
    std::optional<std::string> qr_npc(const std::string&) override { return std::nullopt; }
    bool nat_guid_listed(const std::string&) override { return false; }
    bool inv_open() override { return Wd.invOpen; }
    Id hovered() override { return Wd.invOpen ? Wd.hv : 0; }
    bool inv_call(const char* fn, std::string&) override
    {
        stub(std::string("invCall ") + fn);
        return true;
    }
    bool key_d() override { return Wd.keyd; }
    std::string bind_d() override { return "ok"; }
    bool lead_mine() override { return Wd.lead != "none"; }
    bool lead_dlg() override { return Wd.lead == "dlg"; }
    bool host_dlg_open() override { return Wd.hostDlg; }
    bool busy(std::string& why) override
    {
        if (!Wd.busy.empty())
        {
            why = Wd.busy;
            return true;
        }
        BusyIn in;
        in.lead_dlg = lead_dlg();
        in.host_dlg = host_dlg_open();
        in.views = Wd.dlgViews;
        return busy_of(in, why);
    }
    bool trig_deny(const std::string&, const std::string&) override { return Wd.deny; }
    void trig_entered(const std::string& sid, double, double, double) override { stub("ldTrig " + sid); }
    void mq_seen(const std::string&) override {}
    bool mq_sid(const std::string&) override { return false; }
    void cj_cause(const std::string&) override {}
    void cj_before(const std::string&) override {}
    DeadRes qg_dead(Id, bool) override
    {
        DeadRes r;
        r.skip = Wd.dead;
        return r;
    }
    void qf_arm(const char* why) override { stub(std::string("qfArm ") + why); }
    bool n7_idle() override { return Wd.n7stage == "idle"; }
    void n7_follow() override
    {
        stub("n7Follow");
        if (!Wd.followTo.empty()) Wd.n7stage = Wd.followTo;
    }
    bool n7_rw(const std::string&) override { return false; }
    std::string n7_b() override { return ""; }
    bool ql_settled() override { return Wd.settled; }
    bool join_pending() override { return Wd.joinp; }
    bool quest_muted(const std::string& q) override { return Wd.muted.count(q) > 0; }
    void cp_flush(const char* why) override { stub(std::string("cpFlush ") + why); }
    void campaign_rotated() override { stub("takenLoad"); }
    s2m_save::QState& sq() override { return QS; }
    s2m_save::Counts& applied() override { return AP; }
    int qb_force(const std::string& node, const std::string&, const std::string& pin, bool fin, std::string& err) override
    {
        stub("qbForce " + node + " " + (pin.empty() ? std::string("-") : pin) + (fin ? " fin" : " mid"));
        if (Wd.qbErr)
        {
            err = "falhou";
            return -1;
        }
        if (!fin) return 1;
        if (!Wd.qbAct.count(node)) return 0;
        Wd.qbAct.erase(node);
        return 1;
    }
    bool dlg_has(const std::string& phrase) override { return Wd.dlgAns.count(phrase) > 0; }
    bool dlg_pick(const std::string& phrase, std::string&) override
    {
        stub("dlgPick " + phrase + " " + (qb_answer(phrase) ? "aceita" : "recusada"));
        Wd.dlgAns.clear();
        return true;
    }
};

static std::string unesc(const std::string& s)
{
    std::string r;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            char c = s[++i];
            r += c == 'n' ? '\n' : c == 't' ? '\t' : c == 'r' ? '\r' : c == 's' ? ' ' : c;
        }
        else r += s[i];
    }
    return r;
}

static std::string outs()
{
    return out_qn() + out_w() + out_j() + out_qd() + out_s() + out_b() + out_qi() + out_qp();
}

static std::string tfs(bool b) { return b ? "true" : "false"; }

static std::string retApplied(const Applied& a)
{
    if (!a.ok) return "RET false " + a.err + " nil";
    std::string r1 = a.r == 1 ? "true" : a.r == 0 ? "false" : "nil";
    return "RET true " + r1 + " " + (a.why ? *a.why : std::string("nil"));
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        std::cerr << "usage: test_quest <scen> <root> <data> <loot>\n";
        return 2;
    }
    fs::path root = argv[2];
    BR = root / "B";
    DATA = argv[3];
    {
        bool ok;
        std::string l = rd(argv[4], ok);
        std::stringstream ss(l);
        std::string ln;
        while (std::getline(ss, ln))
        {
            if (!ln.empty() && ln.back() == '\r') ln.pop_back();
            if (!ln.empty()) LOOT.push_back(ln);
        }
    }
    static FakeEnv env;
    bind(&env);
    reset();
    std::ifstream sf(argv[1], std::ios::binary);
    std::string line;
    while (std::getline(sf, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        emit("CMD " + line);
        size_t sp = line.find(' ');
        std::string c = line.substr(0, sp), r = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        std::vector<std::string> a;
        {
            std::stringstream ss(r);
            std::string w;
            while (ss >> w) a.push_back(w);
        }
        auto rest1 = [&]() {
            size_t p = r.find(' ');
            return p == std::string::npos ? std::string() : r.substr(p + 1);
        };
        auto I = [&](size_t i) { return static_cast<Id>(std::atoll(a[i].c_str())); };
        if (c == "role") Wd.role = a[0];
        else if (c == "flag") Wd.flags[a[0]] = fmtnum(std::atof(a[1].c_str()));
        else if (c == "lang") nola_text::force(a[0] == "pt" ? nola_text::Lang::pt : a[0] == "es" ? nola_text::Lang::es : nola_text::Lang::en);
        else if (c == "flags") Wd.flags[a[0]] = a.size() > 1 ? a[1] : std::string();
        else if (c == "noflag") Wd.flags.erase(a[0]);
        else if (c == "t") Wd.t = std::atof(a[0].c_str());
        else if (c == "pawn") Wd.pw = I(0);
        else if (c == "muted") Wd.muted.insert(a[0]);
        else if (c == "inv")
        {
            int n = std::atoi(a[1].c_str());
            if (n > 0) Wd.inv[Wd.pw][a[0]] = n;
            else Wd.inv[Wd.pw].erase(a[0]);
        }
        else if (c == "oinv")
        {
            int n = std::atoi(a[2].c_str());
            if (n > 0) Wd.inv[I(0)][a[1]] = n;
            else Wd.inv[I(0)].erase(a[1]);
        }
        else if (c == "money") Wd.cash = std::atof(a[0].c_str());
        else if (c == "pos") Wd.pos = {std::atof(a[0].c_str()), std::atof(a[1].c_str()), std::atof(a[2].c_str())};
        else if (c == "part") Wd.parts[a[0]] = unesc(rest1());
        else if (c == "nopart") Wd.parts.erase(a[0]);
        else if (c == "views") Wd.nviews = std::atoi(a[0].c_str());
        else if (c == "vpart")
        {
            int v = std::atoi(a[0].c_str());
            size_t p1 = r.find(' ');
            size_t p2 = p1 == std::string::npos ? std::string::npos : r.find(' ', p1 + 1);
            std::string val = p2 == std::string::npos ? std::string() : unesc(r.substr(p2 + 1));
            if (v == 0) Wd.parts[a[1]] = val;
            else Wd.vparts[v][a[1]] = val;
        }
        else if (c == "vnopart")
        {
            int v = std::atoi(a[0].c_str());
            if (v == 0) Wd.parts.erase(a[1]);
            else Wd.vparts[v].erase(a[1]);
        }
        else if (c == "vpeer")
        {
            int v = std::atoi(a[0].c_str());
            int st = a[1] == "up" ? 1 : a[1] == "silent" ? 2 : 0;
            int ss = std::atoi(a[2].c_str());
            bool lp = a.size() > 3 && a[3] == "1";
            if (v == 0)
            {
                Wd.peerState = st;
                Wd.sessions = ss;
                Wd.loopback = lp;
            }
            else
            {
                Wd.vstate[v] = st;
                Wd.vsess[v] = ss;
                Wd.vloop[v] = lp;
            }
        }
        else if (c == "vout")
        {
            for (int v = 0; v < Wd.nviews; ++v) emit("VOUT " + std::to_string(v) + " QD=" + out_qd_v(v) + " S=" + out_s_v(v));
        }
        else if (c == "vrun")
        {
            int n = std::atoi(a[0].c_str());
            for (int i = 0; i < n; ++i)
            {
                for (int k = 0; k < 10; ++k)
                {
                    tick100();
                    Wd.t = Wd.t + 0.1;
                }
                tick1000();
                emit("OUT " + out_qn() + out_w() + out_j() + out_b() + out_qi() + out_qp());
                for (int v = 0; v < Wd.nviews; ++v) emit("VOUT " + std::to_string(v) + " QD=" + out_qd_v(v) + " S=" + out_s_v(v));
            }
        }
        else if (c == "peer")
        {
            Wd.peerState = a[0] == "up" ? 1 : a[0] == "silent" ? 2 : 0;
            Wd.sessions = std::atoi(a[1].c_str());
            Wd.loopback = a.size() > 2 && a[2] == "1";
        }
        else if (c == "gs")
        {
            if (a[0] == "none") Wd.gs.reset();
            else Wd.gs = std::atof(a[0].c_str());
        }
        else if (c == "aa") Wd.aa = a[0] != "0";
        else if (c == "aafail") Wd.aaFail = a[0] != "0";
        else if (c == "dk") Wd.dk = a[0] != "0";
        else if (c == "rk") Wd.rk = a[0] != "0";
        else if (c == "loadok") Wd.loadOk = a[0] != "0";
        else if (c == "lead") Wd.lead = a[0];
        else if (c == "hostdlg") Wd.hostDlg = a[0] != "0";
        else if (c == "dlgview")
        {
            Wd.dlgViews.clear();
            for (auto& w : a)
                if (w.size() == 2) Wd.dlgViews.emplace_back(w[0] == '1', w[1] == '1');
        }
        else if (c == "busy") Wd.busy = a.empty() || a[0] == "-" ? std::string() : a[0];
        else if (c == "qf") emit("QF " + out_qf());
        else if (c == "qb") emit("QB " + out_qb());
        else if (c == "qbact")
            for (auto& x : a) Wd.qbAct.insert(x);
        else if (c == "qbnoact")
            for (auto& x : a) Wd.qbAct.erase(x);
        else if (c == "qberr") Wd.qbErr = a[0] != "0";
        else if (c == "dlg")
        {
            Wd.dlgAns.clear();
            for (auto& x : a)
                if (x != "-") Wd.dlgAns.insert(x);
        }
        else if (c == "fire")
        {
            std::string sub;
            int k = qb_fire(a[0], a[1] == "-" ? std::string() : a[1], a[2] == "fin", sub);
            emit("RET fire " + std::to_string(k) + " " + (sub.empty() ? std::string("-") : sub));
        }
        else if (c == "answer") emit("RET answer " + tfs(qb_answer(a[0])));
        else if (c == "status") emit("STATUS " + status());
        else if (c == "deny") Wd.deny = a[0] != "0";
        else if (c == "settled") Wd.settled = a[0] != "0";
        else if (c == "joinp") Wd.joinp = a[0] != "0";
        else if (c == "n7stage") Wd.n7stage = a[0];
        else if (c == "followto") Wd.followTo = a[0] == "-" ? std::string() : a[0];
        else if (c == "dead") Wd.dead = a[0] != "0";
        else if (c == "keyd") Wd.keyd = a[0] != "0";
        else if (c == "invopen") Wd.invOpen = a[0] != "0";
        else if (c == "hover") Wd.hv = I(0);
        else if (c == "inter") Wd.inter = I(0);
        else if (c == "press") key_d_pressed();
        else if (c == "file")
        {
            std::ofstream f(BR / a[0], std::ios::binary | std::ios::trunc);
            f << unesc(rest1());
        }
        else if (c == "s2q") console(line);
        else if (c == "apply") emit(retApplied(quest_apply_ev(a[0], false)));
        else if (c == "ishost") emit("RET " + tfs(qi_host_keep(a[0], Wd.pw)));
        else if (c == "corpse") emit("RET " + tfs(qi_corpse(I(0), a[1])));
        else if (c == "fin") quest_finished(a[0]);
        else if (c == "out") emit("OUT " + outs());
        else if (c == "tick") tick100();
        else if (c == "ticks")
        {
            int n = std::atoi(a[0].c_str());
            double dt = std::atof(a[1].c_str());
            for (int i = 0; i < n; ++i)
            {
                tick100();
                Wd.t = Wd.t + dt;
            }
        }
        else if (c == "tick1k") tick1000();
        else if (c == "run")
        {
            int n = std::atoi(a[0].c_str());
            for (int i = 0; i < n; ++i)
            {
                for (int k = 0; k < 10; ++k)
                {
                    tick100();
                    Wd.t = Wd.t + 0.1;
                }
                tick1000();
                emit("OUT " + outs());
            }
        }
        else
        {
            std::cerr << "unknown command " << c << "\n";
            return 3;
        }
    }
    std::ofstream o(root / "run.log", std::ios::binary | std::ios::trunc);
    for (auto& s : OUTL) o << s << "\n";
    return 0;
}
