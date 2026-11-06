#include "s2m_world.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace s2m
{
namespace
{

std::string fmt(const char* f, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    int n = std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (n < (int)sizeof buf) return std::string(buf, (size_t)n);
    std::string s((size_t)n + 1, '\0');
    va_start(ap, f);
    std::vsnprintf(&s[0], s.size(), f, ap);
    va_end(ap);
    s.resize((size_t)n);
    return s;
}

const char* tf(bool v) { return v ? "true" : "false"; }

std::vector<std::string> split(const std::string& s, char c)
{
    std::vector<std::string> r;
    size_t a = 0;
    for (;;)
    {
        size_t b = s.find(c, a);
        if (b == std::string::npos) { r.push_back(s.substr(a)); break; }
        r.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return r;
}

std::vector<std::string> split_nonempty(const std::string& s, char c)
{
    std::vector<std::string> r;
    for (auto& p : split(s, c)) if (!p.empty()) r.push_back(p);
    return r;
}

std::string unescape(const std::string& s)
{
    std::string r;
    r.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            char n = s[i + 1];
            if (n == '\\') { r += '\\'; ++i; continue; }
            if (n == 't') { r += '\t'; ++i; continue; }
            if (n == 'r') { r += '\r'; ++i; continue; }
            if (n == 'n') { r += '\n'; ++i; continue; }
        }
        r += s[i];
    }
    return r;
}

bool all_of_set(const std::string& s, const char* set)
{
    if (s.empty()) return false;
    for (char c : s) if (!std::strchr(set, c)) return false;
    return true;
}

bool parse_long(const std::string& s, long& out)
{
    if (s.empty()) return false;
    char* e = nullptr;
    out = std::strtol(s.c_str(), &e, 10);
    return e && *e == '\0';
}

struct TsvRow
{
    std::vector<std::string> keys;
    std::string t;
    std::string v;
};

bool read_tsv(const std::string& text, std::vector<TsvRow>& rows)
{
    std::istringstream in(text);
    std::string line;
    if (!std::getline(in, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    auto hdr = split(line, '\t');
    if (hdr.size() < 3) return false;
    size_t depth = hdr.size() - 2;
    while (std::getline(in, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto c = split(line, '\t');
        while (c.size() < depth + 2) c.push_back(std::string());
        TsvRow r;
        for (size_t i = 0; i < depth; ++i)
            if (!c[i].empty()) r.keys.push_back(c[i]);
        r.t = c[depth];
        r.v = unescape(c[depth + 1]);
        rows.push_back(std::move(r));
    }
    return true;
}

std::string key_text(const std::string& cell) { return cell.size() > 1 ? cell.substr(1) : std::string(); }

bool key_index(const std::string& cell, long& out)
{
    if (cell.empty() || cell[0] != 'i') return false;
    return parse_long(cell.substr(1), out);
}

double ang_diff(double a, double b)
{
    double d = std::fmod(a - b, 360.0);
    if (d < 0) d += 360.0;
    if (d > 180) d -= 360;
    return d;
}

double d2xy(const Vec3& a, const Vec3& b) { return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y); }

std::string name_of(const std::string& full)
{
    size_t sp = full.find(' ');
    if (sp == std::string::npos) return full;
    return full.substr(sp + 1);
}

std::string first_token(const std::string& full)
{
    size_t sp = full.find(' ');
    return sp == std::string::npos ? full : full.substr(0, sp);
}

std::string last_segment(const std::string& full)
{
    size_t p = full.find_last_of(".:");
    return p == std::string::npos ? full : full.substr(p + 1);
}

template <class V>
void put_out(std::vector<V>& v, const std::string& key, V&& e)
{
    for (auto& x : v) if (x.key == key) { x = std::move(e); return; }
    v.push_back(std::move(e));
}

const char* kLeverPrefix = "BP_Lever";
const char* kHatchEvent = "BndEvt__BP_BunkerHatch_Valve_SingleClick_K2Node_ComponentBoundEvent_4_InteractSignature__DelegateSignature";

}

bool load_destruct(Env& env, std::unordered_map<std::string, DestructRow>& out)
{
    std::string text;
    if (!env.read_data("s2u_destruct.tsv", text)) return false;
    std::vector<TsvRow> rows;
    if (!read_tsv(text, rows)) return false;
    for (auto& r : rows)
    {
        if (r.keys.size() < 2) continue;
        auto& d = out[key_text(r.keys[0])];
        std::string f = key_text(r.keys[1]);
        if (f == "k") d.kind = r.v;
        else if (f == "nm") d.mesh_asset = r.v;
        else if (f == "fx") d.fx = r.v;
        else if (f == "au") d.au = r.v;
        else if (f == "p") { long n = 1; parse_long(r.v, n); d.phases = (int)n; }
    }
    return true;
}

bool load_actions(Env& env, std::unordered_map<std::string, ActionRow>& out)
{
    std::string text;
    if (!env.read_data("s2u_actions.tsv", text)) return false;
    std::vector<TsvRow> rows;
    if (!read_tsv(text, rows)) return false;
    std::map<std::string, std::map<long, std::array<std::string, 4>>> clicks;
    for (auto& r : rows)
    {
        if (r.keys.size() < 2) continue;
        std::string cls = key_text(r.keys[0]);
        auto& a = out[cls];
        std::string f = key_text(r.keys[1]);
        if (r.keys.size() == 2)
        {
            if (f == "rule") a.rule = r.v;
            else if (f == "cc") a.cc = r.v;
            else if (f == "on") a.on = r.v;
            else if (f == "off") a.off = r.v;
        }
        else if (r.keys.size() == 4 && f == "click")
        {
            long i = 0, j = 0;
            if (!key_index(r.keys[2], i) || !key_index(r.keys[3], j) || j < 1 || j > 4) continue;
            clicks[cls][i][(size_t)(j - 1)] = r.v;
        }
    }
    for (auto& [cls, m] : clicks)
        for (auto& [i, c] : m)
            out[cls].click.push_back(ClickStep{c[0], c[1], c[2], c[3]});
    return true;
}

bool load_anomaly(Env& env, std::unordered_map<std::string, AnomalyRow>& out)
{
    std::string text;
    if (!env.read_data("s2u_anomaly.tsv", text)) return false;
    std::vector<TsvRow> rows;
    if (!read_tsv(text, rows)) return false;
    for (auto& r : rows)
    {
        if (r.keys.size() < 2) continue;
        auto& a = out[key_text(r.keys[0])];
        std::string f = key_text(r.keys[1]);
        long n = 0;
        if (f == "cd") { parse_long(r.v, n); a.cd = (int)n; }
        else if (f == "h") { parse_long(r.v, n); a.h = (int)n; }
        else if (f == "r") { parse_long(r.v, n); a.r = (int)n; }
        else if (f == "n") a.n = r.v;
    }
    return true;
}

bool load_lockbrk(Env& env, std::unordered_map<std::string, std::string>& out)
{
    std::string text;
    if (!env.read_data("s2u_lockbrk.tsv", text)) return false;
    std::vector<TsvRow> rows;
    if (!read_tsv(text, rows)) return false;
    for (auto& r : rows)
    {
        if (r.keys.size() < 2 || key_text(r.keys[1]) != "brk") continue;
        out[key_text(r.keys[0])] = r.v;
    }
    return true;
}

bool load_tables(Env& env, Tables& t)
{
    t.have_destruct = load_destruct(env, t.destruct);
    t.have_actions = load_actions(env, t.actions);
    t.have_anomaly = load_anomaly(env, t.anomaly);
    t.have_lockbrk = load_lockbrk(env, t.lockbrk);
    return t.have_destruct && t.have_actions && t.have_anomaly;
}

std::string dump_tables(const Tables& t)
{
    std::vector<std::string> lines;
    for (auto& [m, d] : t.destruct)
        lines.push_back(fmt("destruct\t%s\t%s\t%s\t%d\t%s\t%s", m.c_str(), d.kind.c_str(), d.mesh_asset.c_str(), d.phases, d.fx.c_str(), d.au.c_str()));
    for (auto& [c, a] : t.actions)
    {
        lines.push_back(fmt("action\t%s\t%s\t%s\t%s\t%s\t%d", c.c_str(), a.rule.c_str(), a.cc.c_str(), a.on.c_str(), a.off.c_str(), (int)a.click.size()));
        int i = 0;
        for (auto& s : a.click)
            lines.push_back(fmt("click\t%s\t%d\t%s\t%s\t%s\t%s", c.c_str(), ++i, s.comp.c_str(), s.ev.c_str(), s.fn.c_str(), s.tag.c_str()));
    }
    for (auto& [c, a] : t.anomaly)
        lines.push_back(fmt("anomaly\t%s\t%d\t%d\t%s\t%d", c.c_str(), a.cd, a.h, a.n.c_str(), a.r));
    std::sort(lines.begin(), lines.end());
    std::string out;
    for (auto& l : lines) { out += l; out += '\n'; }
    return out;
}

World::World(Env& env) : env_(env) {}

bool World::load()
{
    if (loaded_) return true;
    loaded_ = true;
    return load_tables(env_, tab_);
}

std::string World::key_of(const Vec3& q) const { return fmt("%.0f,%.0f,%.0f", q.x / 10, q.y / 10, q.z / 10); }

bool World::key_parts(const std::string& k, std::string& p, long& x, long& y, long& z) const
{
    size_t c2 = k.rfind(',');
    if (c2 == std::string::npos || c2 == 0) return false;
    size_t c1 = k.rfind(',', c2 - 1);
    if (c1 == std::string::npos || c1 == 0) return false;
    std::string zs = k.substr(c2 + 1), ys = k.substr(c1 + 1, c2 - c1 - 1);
    size_t xe = c1;
    size_t xs = xe;
    while (xs > 0 && std::isdigit((unsigned char)k[xs - 1])) --xs;
    if (xs == xe) return false;
    if (xs > 0 && k[xs - 1] == '-') --xs;
    std::string xstr = k.substr(xs, xe - xs);
    auto okint = [](const std::string& s) {
        size_t i = (!s.empty() && s[0] == '-') ? 1 : 0;
        if (i >= s.size()) return false;
        for (; i < s.size(); ++i) if (!std::isdigit((unsigned char)s[i])) return false;
        return true;
    };
    if (!okint(xstr) || !okint(ys) || !okint(zs)) return false;
    p = k.substr(0, xs);
    x = std::strtol(xstr.c_str(), nullptr, 10);
    y = std::strtol(ys.c_str(), nullptr, 10);
    z = std::strtol(zs.c_str(), nullptr, 10);
    return true;
}

bool World::key_close(const std::string& a, const std::string& b)
{
    if (a == b) return true;
    if (env_.flag("keyfix", 0) != 1) return false;
    std::string pa, pb;
    long ax, ay, az, bx, by, bz;
    if (!key_parts(a, pa, ax, ay, az) || !key_parts(b, pb, bx, by, bz)) return false;
    if (pa != pb) return false;
    long t = (long)env_.flag("keytol", 1);
    return std::labs(ax - bx) <= t && std::labs(ay - by) <= t && std::labs(az - bz) <= t;
}

Ref World::key_find(const std::vector<Ref>& objs, const std::string& k)
{
    Ref best, exact;
    long bd = -1;
    std::string kp;
    long kx = 0, ky = 0, kz = 0;
    bool kok = key_parts(k, kp, kx, ky, kz);
    bool keyfix = env_.flag("keyfix", 0) == 1;
    for (Ref o : objs)
    {
        if (exact) break;
        if (!env_.valid(o)) continue;
        std::string ok2;
        if (!door_key(o, ok2)) continue;
        if (ok2 == k) { exact = o; break; }
        if (keyfix && kok && key_close(ok2, k))
        {
            std::string op;
            long ox, oy, oz;
            if (!key_parts(ok2, op, ox, oy, oz)) continue;
            long d = std::labs(ox - kx) + std::labs(oy - ky) + std::labs(oz - kz);
            if (bd < 0 || d < bd) { best = o; bd = d; }
        }
    }
    return exact ? exact : best;
}

bool World::door_key(Ref o, std::string& k)
{
    Vec3 q;
    if (!env_.location(o, q)) return false;
    k = key_of(q);
    return true;
}

bool World::door_leaf_yaw(Ref o, double& yaw)
{
    Ref c;
    if (!env_.prop_obj(o, "FirstLeafMeshComponent", c) || !env_.valid(c)) return false;
    Vec3 r;
    if (!env_.call_vec(c, "K2_GetComponentRotation", r)) return false;
    yaw = r.y;
    return true;
}

bool World::lock_on() { return env_.flag("lockbrk", 1) == 1; }

std::string World::brk_event(Ref o)
{
    load();
    auto it = tab_.lockbrk.find(env_.class_name(o));
    return it == tab_.lockbrk.end() ? std::string() : it->second;
}

void World::lock_scan(Ref o, const std::string& k, double now)
{
    Door& D = door_;
    bool lk = false;
    if (!env_.call_bool(o, "IsLocked", lk)) return;
    auto it = D.lseen.find(k);
    bool first = it == D.lseen.end();
    if (!first && it->second.locked == lk) return;
    char st = lk ? 'l' : 'u';
    auto ap = D.lapp.find(k);
    bool echo = ap != D.lapp.end() && ap->second == st;
    LockSeen& s = D.lseen[k];
    s.locked = lk;
    if (first)
    {
        if (lk && !env_.is_host() && !echo)
        {
            put_out(D.lout, k, OutEnt{k, true, now});
            env_.log("NOLA doorlock: " + k + " trancada aqui: pergunto ao host");
        }
        return;
    }
    s.chg = true;
    D.lapp[k] = st;
    if (echo) return;
    put_out(D.lout, k, OutEnt{k, lk, now});
    env_.log(fmt("NOLA doorlock: %s %s", k.c_str(), lk ? "TRANCADA" : "DESTRANCADA"));
}

void World::lock_receive(const std::string& k, char st, double now)
{
    Door& D = door_;
    bool ask = st == 'l' && env_.is_host();
    std::string sts(1, st);
    auto pit = D.lpend.find(k);
    bool has_pn = pit != D.lpend.end() && pit->second.st == sts;
    DoorPend pn = has_pn ? pit->second : DoorPend();
    if (has_pn && now - pn.at < (pn.n < 10 ? 2 : 10)) return;
    if (!has_pn)
    {
        if (ask)
        {
            auto a = D.lans.find(k);
            if (a != D.lans.end() && now - a->second < env_.flag("doorhold", 6)) return;
        }
        else
        {
            auto ap = D.lapp.find(k);
            if (ap != D.lapp.end() && ap->second == st) return;
        }
    }
    std::vector<Ref> doors;
    env_.list_class("DoorView", doors);
    Ref hit = key_find(doors, k);
    bool cur = false;
    if (!hit || !env_.call_bool(hit, "IsLocked", cur))
    {
        int n = pn.n + 1;
        if (n == 1) env_.log("NOLA doorlock: porta " + k + " nao encontrada neste PC (fica pendente ate carregar)");
        if (n > 200)
        {
            D.lpend.erase(k);
            if (ask) D.lans[k] = now;
            env_.log("NOLA doorlock: porta " + k + " nunca apareceu aqui: desisto");
        }
        else D.lpend[k] = DoorPend{sts, now, n};
        return;
    }
    D.lpend.erase(k);
    std::string lk = k;
    door_key(hit, lk);
    if (ask)
    {
        D.lans[k] = now;
        if (!cur)
        {
            put_out(D.lout, lk, OutEnt{lk, false, now});
            env_.log("NOLA doorlock: " + k + " trancada no outro PC e destrancada aqui: reenvio destrancada");
        }
        return;
    }
    bool want = st == 'l';
    if (cur == want)
    {
        D.lapp[k] = st;
        D.lapp[lk] = st;
        return;
    }
    bool ok = env_.call(hit, want ? "LockDoor" : "UnlockDoor", {});
    bool nl = cur;
    bool have = env_.call_bool(hit, "IsLocked", nl);
    if (have && nl == want)
    {
        D.lapp[k] = st;
        D.lapp[lk] = st;
    }
    else
    {
        // A rejected native call is not an applied network state. Retry with
        // the existing backoff; a fresh update can restart an expired request.
        D.lapp.erase(k);
        D.lapp.erase(lk);
        if (pn.n < 200) D.lpend[k] = DoorPend{sts, now, pn.n + 1};
    }
    env_.log(fmt("NOLA doorlock: %s -> %s ok=%s locked=%s %s", k.c_str(), want ? "LockDoor" : "UnlockDoor", tf(ok), have ? tf(nl) : "nil", ok ? "" : "erro"));
}

void World::frame()
{
    dobj_tick();
}

void World::tick100()
{
    door_tick();
    ien_tick();
    anom_tick();
}

void World::door_tick()
{
    if (env_.flag("doors", 1) != 1) return;
    Door& D = door_;
    Vec3 lpos;
    int64_t pid = 0;
    if (!env_.local_pawn(lpos, pid)) return;
    double now = env_.now();
    bool dfix = env_.flag("doorfix", 1) != 0;
    bool host = env_.is_host();
    if (env_.flag("doorre", 0) == 1)
    {
        if (host)
        {
            std::string nl;
            if ((mc_on() ? rem_union("N", nl) : env_.remote("N", nl)) && (!D.nl || *D.nl != nl))
            {
                if (D.nl) D.sess = -1;
                D.nl = nl;
            }
        }
        else if (!D.pw || *D.pw != pid)
        {
            if (D.pw)
            {
                D.applied.clear();
                D.seen.clear();
                D.pend.clear();
                D.lseen.clear();
                D.lapp.clear();
                D.lpend.clear();
                D.lans.clear();
                env_.log("NOLA door: b187 pawn novo (load): registro de portas zerado");
            }
            D.pw = pid;
        }
    }
    if (dfix && host)
    {
        int sn = mc_on() ? sessions_all() : env_.peer_sessions();
        if (!D.sess || *D.sess != sn)
        {
            if (D.sess)
            {
                int c = 0;
                for (auto& [k, e] : D.seen)
                    if (e.open || e.chg)
                    {
                        put_out(D.out, k, OutEnt{k, e.open, now});
                        ++c;
                    }
                if (c > 0) env_.log(fmt("NOLA door: sessao #%d do outro lado: %d estado(s) de porta reenviados", sn, c));
                int lc = 0;
                if (lock_on())
                    for (auto& [k, e] : D.lseen)
                        if (e.chg)
                        {
                            put_out(D.lout, k, OutEnt{k, e.locked, now});
                            ++lc;
                        }
                if (lc > 0) env_.log(fmt("NOLA doorlock: sessao #%d do outro lado: %d tranca(s) reenviadas", sn, lc));
            }
            D.sess = sn;
        }
    }
    std::vector<std::pair<std::string, std::string>> ents;
    std::unordered_set<std::string> have;
    std::vector<std::pair<std::string, char>> lents;
    std::unordered_set<std::string> lhave;
    bool lon = lock_on();
    std::string rem;
    if (mc_on() ? rem_merge("O", rem, vm_o_, true) : env_.remote("O", rem))
        for (auto& ent : split_nonempty(rem, '|'))
        {
            size_t sc = ent.rfind(';');
            if (sc == std::string::npos || sc + 2 != ent.size()) continue;
            std::string k = ent.substr(0, sc);
            char st = ent[sc + 1];
            if (!all_of_set(k, "-0123456789,")) continue;
            if (st == 'u' || st == 'l')
            {
                if (lon)
                {
                    lents.push_back({k, st});
                    lhave.insert(k + st);
                }
                continue;
            }
            if (st != '0' && st != '1') continue;
            ents.push_back({k, std::string(1, st)});
            have.insert(k);
        }
    if (lon)
    {
        for (auto& [k, p] : D.lpend)
            if (!p.st.empty() && !lhave.count(k + p.st[0])) lents.push_back({k, p.st[0]});
        for (auto& [k, st] : lents) lock_receive(k, st, now);
    }
    if (dfix)
        for (auto& [k, p] : D.pend)
            if (!have.count(k)) ents.push_back({k, p.st});
    for (auto& e2 : ents)
    {
        const std::string& k = e2.first;
        const std::string& st = e2.second;
        auto pit = D.pend.find(k);
        bool has_pn = pit != D.pend.end();
        DoorPend pn = has_pn ? pit->second : DoorPend();
        auto ap = D.applied.find(k);
        if (ap != D.applied.end() && ap->second == st)
        {
            if (has_pn) D.pend.erase(k);
            continue;
        }
        double lim = (has_pn ? (pn.n < 10 ? 2 : 10) : 0);
        if (has_pn && pn.st == st && now - pn.at < lim) continue;
        if (!dfix) D.applied[k] = st;
        std::vector<Ref> doors;
        env_.list_class("DoorView", doors);
        Ref hit = key_find(doors, k);
        if (hit)
        {
            D.applied[k] = st;
            D.pend.erase(k);
            std::string lk = k;
            door_key(hit, lk);
            if (lk != k)
            {
                D.applied[lk] = st;
                env_.log("NOLA keyfix: porta " + k + " = chave local " + lk);
            }
            double y0 = 0;
            bool has_y0 = door_leaf_yaw(hit, y0);
            bool ok = env_.call(hit, st == "1" ? "ForceOpenDoor" : "ForceCloseDoor", {});
            auto s0 = D.seen.find(lk);
            if (s0 != D.seen.end()) { s0->second.open = (st == "1"); s0->second.chg = true; }
            else if (has_y0) { DoorSeen ns; ns.y0 = y0; ns.open = (st == "1"); ns.chg = true; D.seen[lk] = ns; }
            env_.log(fmt("NOLA door: %s -> %s ok=%s %s", k.c_str(), st == "1" ? "ForceOpenDoor" : "ForceCloseDoor", tf(ok), ok ? "" : "erro"));
        }
        else if (dfix)
        {
            int n = ((has_pn && pn.st == st) ? pn.n : 0) + 1;
            if (n == 1) env_.log("NOLA door: porta " + k + " nao encontrada neste PC (fica pendente ate carregar)");
            if (n > 200)
            {
                D.pend.erase(k);
                D.applied[k] = st;
                env_.log("NOLA door: porta " + k + " nunca apareceu aqui: desisto");
            }
            else
            {
                DoorPend np;
                np.st = st;
                np.at = now;
                np.n = n;
                D.pend[k] = np;
            }
        }
        else env_.log("NOLA door: porta " + k + " nao encontrada neste PC");
    }
    if (now - D.list_at < env_.flag("doorsec", 1)) return;
    D.list_at = now;
    double dr = env_.flag("doorr", 2000);
    double r2 = dr * dr;
    std::vector<Ref> doors;
    env_.list_class("DoorView", doors);
    for (Ref o : doors)
    {
        if (!env_.valid(o)) continue;
        Vec3 q;
        if (!env_.location(o, q)) continue;
        if (d2xy(q, lpos) > r2) continue;
        std::string k = key_of(q);
        if (lon) lock_scan(o, k, now);
        double y;
        if (!door_leaf_yaw(o, y)) continue;
        auto sit = D.seen.find(k);
        if (sit == D.seen.end())
        {
            DoorSeen ns;
            ns.y0 = y;
            ns.open = false;
            D.seen[k] = ns;
            continue;
        }
        DoorSeen& s = sit->second;
        bool open = std::fabs(ang_diff(y, s.y0)) > 20;
        if (open != s.open)
        {
            s.open = open;
            s.chg = true;
            put_out(D.out, k, OutEnt{k, open, now});
            D.applied[k] = open ? "1" : "0";
            D.pend.erase(k);
            env_.log(fmt("NOLA door: %s %s (folha %.0f -> %.0f)", k.c_str(), open ? "ABERTA" : "FECHADA", s.y0, y));
        }
    }
}

std::string World::out_o()
{
    double now = env_.now();
    double hold = env_.flag("doorhold", 6);
    std::string t;
    std::vector<OutEnt> keep;
    for (auto& e : door_.out)
    {
        if (now - e.at < hold)
        {
            if (!t.empty()) t += '|';
            t += e.key + ";" + (e.open ? "1" : "0");
            keep.push_back(e);
        }
    }
    door_.out = std::move(keep);
    std::vector<OutEnt> lkeep;
    for (auto& e : door_.lout)
    {
        if (now - e.at < hold)
        {
            if (!t.empty()) t += '|';
            t += e.key + ";" + (e.open ? "l" : "u");
            lkeep.push_back(e);
        }
    }
    door_.lout = std::move(lkeep);
    if (t.empty()) return std::string();
    return "#O:" + t;
}

void World::dobj_reset()
{
    Dobj& B = dobj_;
    B.broken.clear();
    B.seen.clear();
    B.miss.clear();
    B.out.clear();
    B.cls.clear();
    B.home.clear();
    B.sigs.clear();
    B.quiet.clear();
    B.pend.clear();
    B.mine.clear();
    B.last_z.clear();
}

std::string World::dg_name(Ref o)
{
    std::string f = env_.full_name(o);
    if (f.empty()) return f;
    return name_of(f);
}

std::string World::dg_mesh(Ref o)
{
    std::string m;
    MeshRead r = env_.mesh_name(o, m);
    if (r == MeshRead::Ok) return m;
    if (r == MeshRead::NullMesh)
    {
        ++dobj_.mesh_null;
        int n = dobj_.mesh_null;
        if (n <= 10 || n % 100 == 0)
        {
            std::string tail = last_segment(env_.full_name(o));
            env_.log(fmt("NOLA meshsafe: malha nula (%s.%s) n=%d %s", "StaticMeshComponent", "StaticMesh", n, tail.empty() ? "?" : tail.c_str()));
        }
    }
    return std::string();
}

void World::dg_sig(Ref o, int& p, std::string& m)
{
    p = -1;
    int v = 0;
    if (env_.call_int(o, "GetCurrentPhaseIndex", v)) p = v;
    m = dg_mesh(o);
    if (m.empty()) m = "?";
}

World::DCls World::dg_info(Ref o, const std::string& name)
{
    auto it = dobj_.cls.find(name);
    if (it != dobj_.cls.end()) return it->second;
    std::string ms = dg_mesh(o);
    const DestructRow* e = nullptr;
    if (tab_.have_destruct && !ms.empty())
    {
        auto t = tab_.destruct.find(ms);
        if (t != tab_.destruct.end()) e = &t->second;
    }
    DCls c;
    c.k = e ? e->kind : "?";
    c.e = e;
    if (e && (!e->mesh_asset.empty() || e->kind == "physics" || e->kind == "explode" || e->phases > 1)) c.w = true;
    bool safe = env_.flag("meshsafe", 1) == 1;
    if (!ms.empty() || !safe)
    {
        dobj_.cls[name] = c;
    }
    return c;
}

std::string World::dobj_hide(Ref o)
{
    bool ok = env_.call(o, "SetActorHiddenInGame", {Arg::boolean(true)}) && env_.call(o, "SetActorEnableCollision", {Arg::boolean(false)});
    return std::string("hide ok=") + tf(ok) + (ok ? "" : " erro");
}

bool World::dobj_hidden(Ref o)
{
    bool h = false;
    return env_.prop_bool(o, "bHidden", h) && h;
}

std::string World::dg_replay(Ref o, const DCls& c, std::optional<int> ph, double now)
{
    static const DestructRow none;
    const DestructRow& e = c.e ? *c.e : none;
    std::vector<std::string> r;
    Dobj& B = dobj_;
    if (now - B.fx_at >= 1) { B.fx_at = now; B.fx_n = 0; }
    if (B.fx_n < (int)env_.flag("dobjfxmax", 4))
    {
        ++B.fx_n;
        if (!e.fx.empty())
        {
            Ref ns = env_.asset(e.fx.c_str());
            if (ns && env_.spawn_fx(o, ns)) r.push_back("vfx");
        }
        if (!e.au.empty())
        {
            Ref ev = env_.asset(e.au.c_str());
            if (ev && env_.play_sound(o, ev)) r.push_back("som");
        }
    }
    const std::string& k = c.k;
    if (!ph && (k == "destroy" || k == "fx" || k == "?")) r.push_back(dobj_hide(o));
    else if (!e.mesh_asset.empty())
    {
        bool ok = false;
        Ref comp, mesh = env_.asset(e.mesh_asset.c_str());
        if (mesh && env_.prop_obj(o, "StaticMeshComponent", comp) && env_.valid(comp))
            ok = env_.call(comp, "SetStaticMesh", {Arg::object(mesh)});
        r.push_back(std::string("malha ok=") + tf(ok));
    }
    else if (k == "physics")
    {
        bool ok = false;
        Ref comp;
        if (env_.prop_obj(o, "StaticMeshComponent", comp) && env_.valid(comp))
            ok = env_.call(comp, "SetSimulatePhysics", {Arg::boolean(true)});
        r.push_back(std::string("fisica ok=") + tf(ok));
    }
    else if (!ph) r.push_back(dobj_hide(o));
    std::string out = "replay";
    for (auto& s : r) out += " " + s;
    if (r.empty()) out += " ";
    return out;
}

std::string World::dg_apply(Ref o, const std::string& name, const std::string& k, std::optional<int> ph, double now)
{
    Dobj& B = dobj_;
    DCls c = dg_info(o, name);
    double quiet = env_.flag("dobjquiet", 6);
    B.quiet[name] = now + quiet;
    if (!ph)
    {
        auto h = B.home.find(name);
        if (h != B.home.end()) B.broken.insert(h->second);
    }
    int m = (int)env_.flag("dobjfx", 0);
    if (c.k == "explode" && env_.flag("dobjexp", 0) != 1) m = 2;
    if (m == 1)
    {
        int p0;
        std::string m0;
        dg_sig(o, p0, m0);
        bool ok = env_.call(o, "StartDestructActions", {});
        if (ok)
        {
            DPend p;
            p.at = now;
            p.p = p0;
            p.m = m0;
            p.k = k;
            p.ph = ph;
            B.pend[name] = p;
            B.quiet[name] = now + quiet;
            return c.k + " StartDestructActions ok (medido em 2 s)";
        }
        env_.log("NOLA dobjgen: StartDestructActions erro erro -> replay");
    }
    else if (m == 0 && !ph) return c.k + " " + dobj_hide(o);
    return c.k + " " + dg_replay(o, c, ph, now);
}

bool World::dg_find(const std::string& k, Ref& found, std::string& fname)
{
    Dobj& B = dobj_;
    long kx = 0, ky = 0, kz = 0;
    std::string pre;
    bool kok = key_parts(k, pre, kx, ky, kz) && pre.empty();
    double near = env_.flag("dobjnear", 120);
    double bd = near * near;
    Ref best;
    std::string bn;
    std::vector<Ref> all;
    env_.list_class("DestructibleObject", all);
    for (Ref o : all)
    {
        if (!env_.valid(o)) continue;
        std::string n = dg_name(o);
        if (n.empty()) continue;
        Vec3 q;
        if (!env_.location(o, q)) continue;
        std::string ck = key_of(q);
        auto h = B.home.find(n);
        if ((h != B.home.end() && h->second == k) || (h == B.home.end() && ck == k))
        {
            found = o;
            fname = n;
            return true;
        }
        bool hidden = false;
        env_.prop_bool(o, "bHidden", hidden);
        if (kok && !hidden)
        {
            double d = (q.x - kx * 10.0) * (q.x - kx * 10.0) + (q.y - ky * 10.0) * (q.y - ky * 10.0) + (q.z - kz * 10.0) * (q.z - kz * 10.0);
            if (d < bd) { best = o; bn = n; bd = d; }
        }
    }
    found = best;
    fname = bn;
    return (bool)best;
}

bool World::dg_alive(const std::string& name, bool has_pos, double x, double y)
{
    std::vector<Ref> all;
    env_.list_class("DestructibleObject", all);
    for (Ref o : all)
    {
        if (!env_.valid(o)) continue;
        if (dg_name(o) != name) continue;
        if (!has_pos) return true;
        Vec3 q;
        if (!env_.location(o, q)) continue;
        if ((q.x - x) * (q.x - x) + (q.y - y) * (q.y - y) <= 500.0 * 500.0) return true;
    }
    return false;
}

void World::dg_report(const std::string& k, double now, const std::string& why)
{
    if ((int)env_.flag("dobjgen", 0) == 2)
    {
        env_.log("NOLA dobjgen: (seco) quebrado " + k + " (" + why + ")");
        return;
    }
    put_out(dobj_.out, k, DOut{k, now});
    dobj_.mine.push_back(k);
    env_.log("NOLA dobj: quebrado " + k + " (" + why + ")");
    env_.flush_out();
}

void World::dobj_receive(double now)
{
    Dobj& B = dobj_;
    std::string z;
    bool mc = mc_on();
    if (!(mc ? rem_union("Z", z) : env_.remote("Z", z))) z.clear();
    if (z == B.last_z) return;
    B.last_z = z;
    int gen = (int)env_.flag("dobjgen", 0);
    bool keep = mc && env_.is_host();
    for (auto& ent : split_nonempty(z, '|'))
    {
        if (B.broken.count(ent)) continue;
        B.broken.insert(ent);
        if (keep && std::find(B.mine.begin(), B.mine.end(), ent) == B.mine.end()) B.mine.push_back(ent);
        std::string k = ent;
        std::optional<int> ph;
        size_t at = ent.find('@');
        if (at != std::string::npos)
        {
            std::string ks = ent.substr(0, at), ps = ent.substr(at + 1);
            long pv;
            if (all_of_set(ks, "-0123456789,") && parse_long(ps, pv) && all_of_set(ps, "0123456789")) { k = ks; ph = (int)pv; }
        }
        if (gen == 2) env_.log("NOLA dobjgen: (seco) recebido " + ent);
        else
        {
            Ref o;
            std::string n;
            if (dg_find(k, o, n))
                env_.log(fmt("NOLA dobjgen: %s -> fx=%d %s", ent.c_str(), (int)env_.flag("dobjfx", 0), dg_apply(o, n, k, ph, now).c_str()));
            else env_.log("NOLA dobjgen: " + ent + " nao encontrado neste PC");
        }
    }
}

void World::dobj_evaluate(std::map<std::string, DSeen>& cur, double now, const Vec3& lpos, double r2)
{
    Dobj& B = dobj_;
    int gen = (int)env_.flag("dobjgen", 0);
    bool safe = env_.flag("meshsafe", 1) == 1;
    for (auto it = B.pend.begin(); it != B.pend.end();)
    {
        if (now - it->second.at >= 2)
        {
            std::string name = it->first;
            DPend p = it->second;
            it = B.pend.erase(it);
            auto v = cur.find(name);
            if (v == cur.end()) env_.log("NOLA dobjgen: SDA " + p.k + " efeito=sumiu (stock break ok)");
            else
            {
                int ph;
                std::string ms;
                dg_sig(v->second.ref, ph, ms);
                if (ph != p.p || ms != p.m || dobj_hidden(v->second.ref))
                    env_.log(fmt("NOLA dobjgen: SDA %s efeito=fase %d->%d malha %s->%s", p.k.c_str(), p.p, ph, p.m.c_str(), ms.c_str()));
                else
                {
                    DCls c = dg_info(v->second.ref, name);
                    env_.log("NOLA dobjgen: SDA " + p.k + " SEM efeito -> " + dg_replay(v->second.ref, c, p.ph, now));
                }
                B.quiet[name] = now + 2;
            }
        }
        else ++it;
    }
    for (auto& [name, v] : cur)
    {
        auto qt = B.quiet.find(name);
        double quiet = qt == B.quiet.end() ? 0 : qt->second;
        if (B.broken.count(v.k) && gen == 1 && !B.pend.count(name) && now >= quiet && !dobj_hidden(v.ref))
        {
            std::string r = dobj_hide(v.ref);
            ++B.rehid;
            if (B.rehid <= 20) env_.log(fmt("NOLA dobjgen: %s reapareceu -> %s (n=%d)", v.k.c_str(), r.c_str(), B.rehid));
        }
        else if (v.watched && !(safe && v.m == "?"))
        {
            auto sit = B.sigs.find(name);
            if (sit == B.sigs.end() || now < quiet)
            {
                DSig s;
                s.p = v.p;
                s.m = v.m;
                s.n = sit == B.sigs.end() ? 0 : sit->second.n;
                B.sigs[name] = s;
            }
            else if (sit->second.p != v.p || sit->second.m != v.m)
            {
                DSig old = sit->second;
                int step = (v.p > old.p) ? v.p : old.n + 1;
                std::string ent = fmt("%s@%d", v.k.c_str(), step);
                DSig s;
                s.p = v.p;
                s.m = v.m;
                s.n = step;
                B.sigs[name] = s;
                if (!B.broken.count(ent))
                {
                    B.broken.insert(ent);
                    dg_report(ent, now, fmt("fase %d->%d malha %s->%s", old.p, v.p, old.m.c_str(), v.m.c_str()));
                }
            }
        }
    }
    bool fast = env_.flag("dobjfast", 1) == 1;
    for (auto& [name, v] : B.seen)
    {
        if (!cur.count(name) && !B.miss.count(name))
        {
            Vec3 still;
            if (fast && env_.valid(v.ref) && env_.location(v.ref, still) && (still.x - v.x) * (still.x - v.x) + (still.y - v.y) * (still.y - v.y) <= 500.0 * 500.0) continue;
            double dx = v.x - lpos.x, dy = v.y - lpos.y;
            if (dx * dx + dy * dy <= r2)
            {
                DMiss m;
                m.x = v.x;
                m.y = v.y;
                m.k = v.k;
                m.since = now;
                m.last_seen = v.at;
                B.miss[name] = m;
            }
        }
    }
    int need = fast ? (int)env_.flag("dobjfmiss", 1) : (int)env_.flag("dobjmiss", 3);
    for (auto it = B.miss.begin(); it != B.miss.end();)
    {
        const std::string name = it->first;
        DMiss& e = it->second;
        if (cur.count(name)) { it = B.miss.erase(it); continue; }
        if ((e.x - lpos.x) * (e.x - lpos.x) + (e.y - lpos.y) * (e.y - lpos.y) > r2) { it = B.miss.erase(it); continue; }
        ++e.n;
        if (e.n >= need)
        {
            DMiss m = e;
            it = B.miss.erase(it);
            if (!dg_alive(name, true, m.x, m.y))
            {
                auto qt = B.quiet.find(name);
                if (B.broken.count(m.k) || now < (qt == B.quiet.end() ? 0 : qt->second)) B.broken.insert(m.k);
                else
                {
                    B.broken.insert(m.k);
                    dg_report(m.k, now, fast ? fmt("sumiu %d varreduras %.2f s, ultimo visto ha %.2f s", m.n, now - m.since, now - m.last_seen) : std::string("sumiu"));
                }
            }
        }
        else ++it;
    }
    B.seen = cur;
}

void World::dobj_scan(double now, const Vec3& lpos, double r2)
{
    Dobj& B = dobj_;
    double t0 = env_.now();
    std::map<std::string, DSeen> cur;
    int n_all = 0, n_w = 0;
    std::vector<Ref> all;
    env_.list_class("DestructibleObject", all);
    for (Ref o : all)
    {
        ++n_all;
        if (!env_.valid(o)) continue;
        std::string name = dg_name(o);
        if (name.empty()) continue;
        auto ct = B.cls.find(name);
        if (ct != B.cls.end() && ct->second.k == "movable") continue;
        Vec3 q;
        if (!env_.location(o, q)) continue;
        double dx = q.x - lpos.x, dy = q.y - lpos.y;
        if (dx * dx + dy * dy > r2) continue;
        DCls c = ct != B.cls.end() ? ct->second : dg_info(o, name);
        if (c.k == "movable") continue;
        auto h = B.home.find(name);
        std::string k;
        if (h == B.home.end()) { k = key_of(q); B.home[name] = k; }
        else k = h->second;
        DSeen v;
        v.ref = o;
        v.x = q.x;
        v.y = q.y;
        v.k = k;
        v.at = now;
        if (c.w)
        {
            v.watched = true;
            dg_sig(o, v.p, v.m);
            ++n_w;
        }
        cur[name] = v;
    }
    dobj_evaluate(cur, now, lpos, r2);
    B.st_n += 1;
    B.st_ms += (env_.now() - t0) * 1000;
    B.st_all = n_all;
    B.st_near = (int)B.seen.size();
    B.st_w = n_w;
    if (B.st_at == 0) B.st_at = now;
    if (env_.flag("dobjstat", 1) == 1 && now - B.st_at >= 60)
    {
        std::string line = fmt("NOLA dobjgen: varredura %d x, media %.2f ms, destrutiveis carregados=%d perto(nao moveis)=%d vigiados=%d",
                               B.st_n, B.st_n ? B.st_ms / B.st_n : 0.0, B.st_all, B.st_near, B.st_w);
        if (env_.flag("dobjfast", 1) == 1) line += fmt(" sondagens=%d intervalo_max=%.0f ms", B.poll_n, B.poll_gap_max * 1000);
        env_.log(line);
        B.st_at = now;
        B.st_n = 0;
        B.st_ms = 0;
        B.poll_n = 0;
        B.poll_gap_max = 0;
    }
}

void World::dobj_poll(double now, const Vec3& lpos, double r2)
{
    Dobj& B = dobj_;
    std::map<std::string, DSeen> cur;
    for (auto& [name, v] : B.seen)
    {
        if (!env_.valid(v.ref)) continue;
        Vec3 q;
        if (!env_.location(v.ref, q)) continue;
        double dx = q.x - lpos.x, dy = q.y - lpos.y;
        if (dx * dx + dy * dy > r2) continue;
        DSeen n = v;
        n.x = q.x;
        n.y = q.y;
        n.at = now;
        if (n.watched) dg_sig(v.ref, n.p, n.m);
        cur[name] = n;
    }
    dobj_evaluate(cur, now, lpos, r2);
}

void World::dobj_tick()
{
    if (env_.flag("dobj", 0) != 1) return;
    int gen = (int)env_.flag("dobjgen", 0);
    if (gen < 1) return;
    Dobj& B = dobj_;
    if (!B.init)
    {
        load();
        if (tab_.have_destruct) env_.log(fmt("NOLA dobjgen: tabela carregada, %d malhas", (int)tab_.destruct.size()));
        else env_.log("NOLA dobjgen: falta s2u_destruct.tsv (sem tabela: todo destrutivel conta como 'destroy')");
        dobj_reset();
        B.init = true;
        B.list_at = 0;
    }
    bool fast = env_.flag("dobjfast", 1) == 1;
    double period = fast ? env_.flag("dobjfsec", 0.15) : env_.flag("dobjsec", 1);
    if (fast)
    {
        double t0 = env_.now();
        if (t0 - B.poll_at < period && t0 - B.list_at < env_.flag("dobjsec", 1)) return;
    }
    Vec3 lpos;
    int64_t pid = 0;
    if (!env_.local_pawn(lpos, pid)) return;
    double now = env_.now();
    std::string key;
    bool open = true;
    bool has_gate = env_.gate(key, open);
    if (has_gate && (!B.wkey || *B.wkey != key))
    {
        if (B.wkey) env_.log("NOLA dobjgen: jogador local trocado (load?) -> estado reiniciado");
        B.wkey = key;
        dobj_reset();
    }
    if (has_gate && !open)
    {
        B.seen.clear();
        B.miss.clear();
    }
    std::string lvl;
    if (env_.level_name(lvl) && (!B.level || *B.level != lvl))
    {
        B.level = lvl;
        dobj_reset();
        env_.log("NOLA dobjgen: nivel " + lvl.substr(0, 60) + " -> estado reiniciado");
    }
    dobj_receive(now);
    double full_period = env_.flag("dobjsec", 1);
    bool full_due = now - B.list_at >= full_period;
    if (!fast)
    {
        if (!full_due) return;
    }
    else
    {
        if (now - B.poll_at < period && !full_due) return;
    }
    double dr = env_.flag("dobjr", 2500);
    double r2 = dr * dr;
    if (fast)
    {
        if (B.poll_at > 0) B.poll_gap_max = std::max(B.poll_gap_max, now - B.poll_at);
        ++B.poll_n;
        B.poll_at = now;
    }
    if (full_due || !fast)
    {
        B.list_at = now;
        dobj_scan(now, lpos, r2);
    }
    else dobj_poll(now, lpos, r2);
}

std::string World::out_z()
{
    Dobj& B = dobj_;
    double now = env_.now();
    double hold = env_.flag("dobjhold", 6);
    std::vector<std::string> t;
    std::vector<DOut> keep;
    for (auto& e : B.out)
    {
        if (now - e.at < hold) { t.push_back(e.key); keep.push_back(e); }
    }
    B.out = std::move(keep);
    int n_mine = (int)B.mine.size();
    if (n_mine > 0)
    {
        int n = std::min(n_mine, (int)env_.flag("dobjrot", 10));
        if (now - B.rot_at >= 1)
        {
            B.rot_at = now;
            B.rot = (B.rot + n) % n_mine;
        }
        for (int i = 0; i < n; ++i) t.push_back(B.mine[(size_t)((B.rot + i) % n_mine)]);
    }
    if (t.empty()) return std::string();
    std::string s = "#Z:";
    for (size_t i = 0; i < t.size(); ++i) { if (i) s += '|'; s += t[i]; }
    return s;
}

std::string World::ien_apply_act(Ref o, const std::string& cls, bool target)
{
    auto en = [&](Ref r, bool& v) { bool b = false; if (!env_.call_bool(r, "IsEnabled", b)) return false; v = b; return true; };
    if (env_.flag("ientab", 0) != 1) return "off";
    bool cur;
    if (!en(o, cur)) return "noactor";
    if (cur == target) return "same";
    auto rit = tab_.actions.find(cls);
    const ActionRow* r = rit == tab_.actions.end() ? nullptr : &rit->second;
    std::vector<std::string> steps;
    auto step = [&](const std::string& fn, const std::vector<Arg>& args) {
        bool ok = env_.call(o, fn.c_str(), args);
        steps.push_back(fn.substr(0, 40) + (ok ? "" : "!erro"));
        return ok;
    };
    const char* want = target ? "on" : "off";
    if (r)
    {
        const std::string& named = target ? r->on : r->off;
        bool bnd = named.rfind("BndEvt__", 0) == 0;
        if (!named.empty() && !bnd) step(named, {});
        else if (!r->click.empty() && (r->cc == "toggle" || r->cc == want))
        {
            std::string comp;
            bool found = false;
            for (auto& c : r->click)
            {
                Ref co;
                bool act = false;
                if (env_.prop_obj(o, c.comp.c_str(), co) && env_.valid(co)) env_.prop_bool(co, "bIsInteractionActive", act);
                if (act) { comp = c.comp; found = true; break; }
            }
            if (!found) comp = r->click[0].comp;
            for (auto& c : r->click) if (c.comp == comp) step(c.fn, {});
        }
        else if (!named.empty()) step(named, {});
    }
    bool now_en;
    if (!en(o, now_en) || now_en != target) step("SetEnabled", {Arg::boolean(target)});
    if (env_.flag("ienanim", 0) == 1)
    {
        step("PauseSkeletalComponent", {Arg::boolean(false)});
        if (!r || r->rule != "explicit") env_.call(o, "StateChange", {});
    }
    bool nw = false;
    bool have_now = en(o, nw);
    std::string nows = have_now ? tf(nw) : "nil";
    std::string curs = tf(cur);
    std::string joined;
    for (size_t i = 0; i < steps.size(); ++i) { if (i) joined += ','; joined += steps[i]; }
    env_.log(fmt("NOLA ientab: %s %s -> %s via %s (now %s)", cls.c_str(), curs.c_str(), tf(target), joined.c_str(), nows.c_str()));
    return (have_now && nw == target) ? "ok" : "miss";
}

void World::ien_apply_mask(Ref o, const std::string& mask, const std::string& k)
{
    if (!env_.valid(o)) return;
    std::vector<NamedRef> cs;
    env_.owned_components(o, "InteractionComponent", cs);
    std::sort(cs.begin(), cs.end(), [](const NamedRef& a, const NamedRef& b) { return a.name < b.name; });
    int n = 0;
    for (size_t i = 0; i < cs.size() && i < mask.size(); ++i)
    {
        char b = mask[i];
        if (b != '0' && b != '1') continue;
        bool cur = false;
        bool got = env_.prop_bool(cs[i].ref, "bIsInteractionActive", cur);
        if (!got || cur != (b == '1'))
            if (env_.call(cs[i].ref, "SetInteractionActive", {Arg::boolean(b == '1')})) ++n;
    }
    if (n > 0) env_.log(fmt("NOLA ien: %s prompts %s -> %s (%d mudados)", k.c_str(), "?", mask.c_str(), n));
}

std::string World::ien_mask(Ref o)
{
    std::string m;
    if (!env_.valid(o)) return m;
    std::vector<NamedRef> cs;
    env_.owned_components(o, "InteractionComponent", cs);
    std::sort(cs.begin(), cs.end(), [](const NamedRef& a, const NamedRef& b) { return a.name < b.name; });
    for (auto& c : cs)
    {
        bool a = false;
        env_.prop_bool(c.ref, "bIsInteractionActive", a);
        m += a ? '1' : '0';
    }
    return m;
}

void World::ien_receive(double now)
{
    Ien& I = ien_;
    for (auto it = I.pm.begin(); it != I.pm.end();)
    {
        if (now >= it->second.at)
        {
            std::string qk = it->first;
            IenPm q = it->second;
            it = I.pm.erase(it);
            std::vector<Ref> objs;
            env_.list_class("InteractableObject", objs);
            Ref h = key_find(objs, qk);
            if (h && env_.valid(h)) ien_apply_mask(h, q.mask, qk);
        }
        else ++it;
    }
    std::string rem;
    std::unordered_set<std::string> bhave;
    bool got_rem = mc_on() ? rem_merge("P", rem, vm_p_, false) : env_.remote("P", rem);
    for (auto& ent : split_nonempty(got_rem ? rem : std::string(), '|'))
    {
        size_t sc = ent.find(';');
        if (sc == std::string::npos || sc + 1 >= ent.size()) continue;
        std::string k = ent.substr(0, sc);
        if (!all_of_set(k, "-0123456789,")) continue;
        char st = ent[sc + 1];
        if (st != '0' && st != '1' && st != 'b') continue;
        size_t pos = sc + 2;
        if (pos < ent.size() && ent[pos] == ';') ++pos;
        if (pos < ent.size() && ent[pos] == 'm') ++pos;
        std::string mk = ent.substr(pos);
        bool okmk = true;
        for (char c : mk) if (c != '0' && c != '1') okmk = false;
        if (!okmk) continue;
        if (st == 'b')
        {
            bhave.insert(k);
            ien_break(k, now);
            continue;
        }
        bool want = st == "1"[0];
        std::string wants = want ? "1" : "0";
        auto sit = I.seen.find(k);
        auto apit = I.app.find(k);
        bool recent = apit != I.app.end() && apit->second.w == wants && now - apit->second.at < env_.flag("ienecho", 10);
        if (!((sit == I.seen.end() || sit->second != want) && !recent)) continue;
        std::vector<Ref> objs;
        env_.list_class("InteractableObject", objs);
        Ref hit = key_find(objs, k);
        bool refused = false;
        if (hit && use_deny_ && use_deny_(hit, k))
        {
            IenApp a;
            a.w = wants;
            a.at = now;
            I.app[k] = a;
            refused = true;
        }
        if (!hit)
        {
            env_.log("NOLA ien: objeto " + k + " nao encontrado neste PC");
            continue;
        }
        if (refused) continue;
        if (qobj::skip_sync(qobj_on(), env_.quest_obj(hit)))
        {
            if (I.qmiss.insert("P" + k).second) env_.log("NOLA objstate: entrada P de " + k + " (" + env_.class_name(hit) + ") ignorada: objetivo de missao, so o host manda");
            continue;
        }
        bool cur_val = false;
        bool cur_is_bool = env_.prop_bool(hit, "bEnabled", cur_val);
        bool lamp = false;
        if (env_.flag("ienlamp", 0) == 1) lamp = env_.class_chain_has(hit, "BP_Light_Base_C", 4);
        if (lamp)
        {
            bool ok = env_.call(hit, want ? "LightOn" : "LightOff", {});
            bool be = false;
            if (env_.prop_bool(hit, "bEnabled", be) && be != want) env_.call(hit, "SetEnabled", {Arg::boolean(want)});
            std::string vis = "nil";
            Ref ml;
            if (env_.prop_obj(hit, "MainLight", ml) && env_.valid(ml))
            {
                bool v = false;
                if (env_.call_bool(ml, "IsVisible", v)) vis = tf(v);
            }
            env_.log(fmt("NOLA ien: %s -> Light%s ok=%s vis=%s %s", k.c_str(), want ? "On" : "Off", tf(ok), vis.c_str(), ok ? "" : "erro"));
            cur_val = want;
            cur_is_bool = true;
        }
        std::string cls = env_.class_name(hit);
        bool use_tab = env_.flag("ientab", 0) == 1 && ien_table();
        if (use_tab && tab_.actions.count(cls) && cur_is_bool && cur_val != want)
        {
            ien_apply_act(hit, cls, want);
            cur_val = want;
        }
        if (cur_is_bool && cur_val != want)
        {
            std::string how = "SetEnabled";
            bool ok = false;
            bool hatch = false;
            if (env_.flag("ienhatch", 0) == 1 && want) hatch = cls == "BP_BunkerHatch_Valve_C";
            if (hatch)
            {
                how = "HatchClick";
                ok = env_.call(hit, kHatchEvent, {});
            }
            else if (env_.flag("ientoggle", 1) == 1)
            {
                how = "Toggle";
                ok = env_.call(hit, "Toggle", {});
                bool n2 = false;
                bool g2 = env_.prop_bool(hit, "bEnabled", n2);
                if (!g2 || n2 != want)
                {
                    how = "Toggle+SetEnabled";
                    ok = env_.call(hit, "SetEnabled", {Arg::boolean(want)});
                }
            }
            else ok = env_.call(hit, "SetEnabled", {Arg::boolean(want)});
            env_.call(hit, "StateChange", {});
            if (env_.flag("ienanim", 0) == 1)
            {
                for (const char* cn : {"SkeletalMesh", "Hatch", "Mesh"})
                {
                    bool okA = env_.set_anim_door_state(hit, cn, want ? 0 : 1);
                    if (okA)
                    {
                        how += fmt("+DoorState%d", want ? 0 : 1);
                        break;
                    }
                }
                if (env_.call(hit, "PauseSkeletalComponent", {Arg::boolean(false)})) how += "+Unpause";
            }
            env_.log(fmt("NOLA ien: %s -> %s(%s)+StateChange ok=%s %s", k.c_str(), how.c_str(), st == '1' ? "1" : "0", tf(ok), ok ? "" : "erro"));
        }
        I.seen[k] = want;
        IenApp a;
        a.w = wants;
        a.at = now;
        I.app[k] = a;
        std::string lk;
        if (door_key(hit, lk) && lk != k)
        {
            I.seen[lk] = want;
            I.app[lk] = a;
        }
        if (env_.flag("ienprompt", 0) == 1 && !mk.empty())
        {
            IenPm pm;
            pm.mask = mk;
            pm.at = now + 1.5;
            I.pm[k] = pm;
        }
    }
    if (!lock_on()) return;
    std::vector<std::string> pk;
    for (auto& [k, p] : I.bpend)
        if (!bhave.count(k)) pk.push_back(k);
    for (auto& k : pk) ien_break(k, now);
}

bool World::ien_break(const std::string& k, double now)
{
    Ien& I = ien_;
    bool lon = lock_on();
    auto pit = I.bpend.find(k);
    bool has_pn = lon && pit != I.bpend.end();
    if (has_pn && now - pit->second.at < (pit->second.n < 10 ? 2 : 10)) return false;
    if (I.brk.count(k) && I.brk[k]) return true;
    I.brk[k] = true;
    std::vector<Ref> objs;
    env_.list_class("InteractableObject", objs);
    Ref hit = key_find(objs, k);
    if (!hit)
    {
        I.brk.erase(k);
        if (!lon)
        {
            env_.log("NOLA ien: quebrado " + k + " nao encontrado neste PC");
            return false;
        }
        int n = (has_pn ? pit->second.n : 0) + 1;
        if (n == 1) env_.log("NOLA ien: quebrado " + k + " nao encontrado neste PC (fica pendente ate carregar)");
        if (n > 200)
        {
            I.bpend.erase(k);
            env_.log("NOLA ien: quebrado " + k + " nunca apareceu aqui: desisto");
        }
        else I.bpend[k] = DoorPend{"b", now, n};
        return false;
    }
    I.bpend.erase(k);
    if (qobj::skip_sync(qobj_on(), env_.quest_obj(hit)))
    {
        I.brk.erase(k);
        if (I.qmiss.insert("b" + k).second) env_.log("NOLA objstate: quebra de " + k + " (" + env_.class_name(hit) + ") ignorada: objetivo de missao, so o host manda");
        return true;
    }
    std::string lk;
    if (door_key(hit, lk) && lk != k)
    {
        I.brk[lk] = true;
        I.bseen[lk] = true;
    }
    bool cur_broken = false;
    bool got = env_.prop_bool(hit, "bBroken", cur_broken);
    bool ok = true;
    std::string ev = (lon && !(got && cur_broken)) ? brk_event(hit) : std::string();
    if (!ev.empty())
    {
        bool rok = env_.call(hit, ev.c_str(), {});
        bool rb = false;
        bool have_rb = env_.prop_bool(hit, "bBroken", rb);
        env_.log(fmt("NOLA ien: %s -> quebra replay %s ok=%s broken=%s", k.c_str(), env_.class_name(hit).c_str(), tf(rok), have_rb ? tf(rb) : "nil"));
        if (have_rb && rb)
        {
            env_.call(hit, "StateChange", {});
            return true;
        }
    }
    if (!(got && cur_broken)) ok = env_.call(hit, "SetBroken", {Arg::boolean(true)});
    env_.call(hit, "StateChange", {});
    bool nb = false;
    bool have_nb = env_.prop_bool(hit, "bBroken", nb);
    env_.log(fmt("NOLA ien: %s -> SetBroken(true) ok=%s broken=%s %s", k.c_str(), tf(ok), have_nb ? tf(nb) : "nil", ok ? "" : "erro"));
    return true;
}

void World::ien_scan(double now, const Vec3& lpos)
{
    Ien& I = ien_;
    if (now - I.list_at < env_.flag("iensec", 1)) return;
    I.list_at = now;
    double rr = env_.flag("ienr", 2500);
    double r2 = rr * rr;
    std::vector<Ref> objs;
    env_.list_class("InteractableObject", objs);
    bool keyfix = env_.flag("keyfix", 0) == 1;
    double echo = env_.flag("ienecho", 10);
    for (Ref o : objs)
    {
        if (!env_.valid(o)) continue;
        Vec3 q;
        if (!env_.location(o, q)) continue;
        if (d2xy(q, lpos) > r2) continue;
        if (qobj::skip_sync(qobj_on(), env_.quest_obj(o))) continue;
        bool en = false;
        if (!env_.prop_bool(o, "bEnabled", en)) continue;
        std::string k = key_of(q);
        bool br = false;
        bool has_br = env_.prop_bool(o, "bBroken", br);
        auto bs = I.bseen.find(k);
        if (has_br && br && bs != I.bseen.end() && !bs->second && !(I.brk.count(k) && I.brk[k]))
        {
            I.brk[k] = true;
            bool dup = false;
            for (auto& b : I.bout) if (b.first == k) { b.second = now; dup = true; }
            if (!dup) I.bout.push_back({k, now});
            env_.log("NOLA ien: " + k + " QUEBRADO");
        }
        if (has_br) I.bseen[k] = br;
        auto sit = I.seen.find(k);
        if (sit == I.seen.end()) { I.seen[k] = en; continue; }
        bool s = sit->second;
        const IenApp* ap = nullptr;
        auto ait = I.app.find(k);
        if (ait != I.app.end()) ap = &ait->second;
        std::string foundk;
        if (s != en && !ap && keyfix)
        {
            std::string kp;
            long x, y, z;
            if (key_parts(k, kp, x, y, z))
            {
                long t = (long)env_.flag("keytol", 1);
                for (long dx = -t; dx <= t && !ap; ++dx)
                    for (long dy = -t; dy <= t && !ap; ++dy)
                        for (long dz = -t; dz <= t && !ap; ++dz)
                        {
                            auto a2 = I.app.find(fmt("%s%ld,%ld,%ld", kp.c_str(), x + dx, y + dy, z + dz));
                            if (a2 != I.app.end()) ap = &a2->second;
                        }
            }
        }
        if (s != en && ap && now - ap->at < echo) { I.seen[k] = en; continue; }
        if (s != en)
        {
            I.seen[k] = en;
            IenOut e;
            e.key = k;
            e.en = en;
            e.at = now;
            std::string cn = env_.class_name(o);
            if (env_.flag("ienprompt", 0) == 1 && cn.compare(0, 8, kLeverPrefix) == 0) e.mask = ien_mask(o);
            put_out(I.out, k, std::move(e));
            env_.log(fmt("NOLA ien: %s %s", k.c_str(), en ? "ON" : "OFF"));
        }
    }
}

bool World::ien_table()
{
    if (!ien_logged_)
    {
        ien_logged_ = true;
        load();
        if (tab_.have_actions) env_.log(fmt("NOLA ientab: tabela carregada, %d classes", (int)tab_.actions.size()));
        else env_.log("NOLA ientab: falta s2u_actions.tsv");
    }
    return tab_.have_actions;
}

void World::ien_tick()
{
    if (env_.flag("ien", 0) != 1) return;
    Vec3 lpos;
    int64_t pid = 0;
    if (!env_.local_pawn(lpos, pid)) return;
    double now = env_.now();
    Ien& I = ien_;
    if (env_.is_host() && lock_on())
    {
        int sn = mc_on() ? sessions_all() : env_.peer_sessions();
        if (I.sess && *I.sess != sn)
        {
            int c = 0;
            for (auto& [k, b] : I.brk)
            {
                auto bs = I.bseen.find(k);
                if (!b || bs == I.bseen.end() || !bs->second) continue;
                bool dup = false;
                for (auto& o : I.bout) if (o.first == k) { o.second = now; dup = true; }
                if (!dup) I.bout.push_back({k, now});
                ++c;
            }
            if (c > 0) env_.log(fmt("NOLA ien: sessao #%d do outro lado: %d quebrado(s) reenviados", sn, c));
        }
        I.sess = sn;
    }
    ien_receive(now);
    ien_scan(now, lpos);
    qobj_tick(now);
}

bool World::qobj_on() { return env_.flag("qobjsync", 1) == 1; }

bool World::qobj_read(Ref o, qobj::ObjState& s)
{
    bool b = false;
    s.hid = env_.prop_bool(o, "bHidden", b) && b;
    s.en = !env_.prop_bool(o, "bEnabled", b) || b;
    s.br = env_.prop_bool(o, "bBroken", b) && b;
    s.st = -1;
    std::vector<NamedRef> cs;
    env_.owned_components(o, "TopazScannerComponent", cs);
    int v = 0;
    if (!cs.empty() && env_.call_int(cs[0].ref, "GetCurrentState", v)) s.st = v;
    return true;
}

void World::qobj_tick(double now)
{
    if (!qobj_on()) return;
    Ien& I = ien_;
    if (now - I.qlist_at < env_.flag("iensec", 1)) return;
    I.qlist_at = now;
    std::vector<Ref> objs;
    env_.list_class("InteractableObject", objs);
    if (env_.is_host())
    {
        for (Ref o : objs)
        {
            if (!env_.valid(o) || !env_.quest_obj(o)) continue;
            Vec3 q;
            if (!env_.location(o, q)) continue;
            std::string k = key_of(q);
            qobj::ObjState s;
            qobj_read(o, s);
            std::string e = qobj::enc(s);
            auto it = I.qst.find(k);
            if (it != I.qst.end() && it->second == e) continue;
            I.qst[k] = e;
            env_.log(qobj::line(env_.class_name(o), k, s));
        }
        return;
    }
    std::string rem;
    if (!env_.remote("QO", rem)) return;
    for (auto& ent : split_nonempty(rem, '|'))
    {
        std::string k;
        qobj::ObjState want;
        if (!qobj::wire_parse(ent, k, want)) continue;
        Ref hit = key_find(objs, k);
        if (!hit || !env_.valid(hit))
        {
            if (I.qmiss.insert("Q" + k).second) env_.log("NOLA objstate: objetivo " + k + " do host nao encontrado neste PC (fica pendente ate carregar)");
            continue;
        }
        qobj::ObjState have;
        qobj_read(hit, have);
        std::vector<qobj::Step> calls;
        int steps = qobj::plan(have, want, calls);
        std::string how;
        for (auto& c : calls)
        {
            bool ok = env_.call(hit, c.fn.c_str(), {Arg::boolean(c.arg)});
            if (!how.empty()) how += ',';
            how += c.fn + "(" + tf(c.arg) + ")" + (ok ? "" : "!erro");
        }
        if (steps > 0)
        {
            std::vector<NamedRef> cs;
            env_.owned_components(hit, "TopazScannerComponent", cs);
            int done = 0;
            for (int i = 0; i < steps && i < 5 && !cs.empty(); ++i)
            {
                if (!env_.call(cs[0].ref, "ChangeState", {})) break;
                ++done;
            }
            if (!how.empty()) how += ',';
            how += fmt("ChangeState x%d/%d", done, steps);
        }
        qobj::ObjState after;
        qobj_read(hit, after);
        std::string base = qobj::enc(want) + "|" + qobj::enc(after) + "|";
        auto ap = I.qapp.find(k);
        if (ap != I.qapp.end() && (how.empty() ? ap->second.rfind(base, 0) == 0 : ap->second == base + how))
        {
            if (how.empty()) ap->second = base;
            continue;
        }
        I.qapp[k] = base + how;
        std::string cls = env_.class_name(hit);
        env_.log(qobj::line(cls, k, after) + (how.empty() ? std::string(" (igual ao host)") : " via " + how + " (host " + qobj::enc(want) + ")"));
    }
}

std::string World::out_qo()
{
    Ien& I = ien_;
    if (!qobj_on() || !env_.is_host() || I.qst.empty()) return std::string();
    std::string s = "#QO:";
    bool first = true;
    for (auto& [k, e] : I.qst)
    {
        if (!first) s += '|';
        first = false;
        s += k + ";" + e;
    }
    return s;
}

std::string World::out_p()
{
    Ien& I = ien_;
    double now = env_.now();
    double hold = env_.flag("ienhold", 6);
    std::vector<std::string> t;
    std::vector<IenOut> keep;
    for (auto& e : I.out)
    {
        if (now - e.at < hold)
        {
            t.push_back(e.key + ";" + (e.en ? "1" : "0") + (e.mask.empty() ? "" : ";m" + e.mask));
            keep.push_back(e);
        }
    }
    I.out = std::move(keep);
    std::vector<std::pair<std::string, double>> bk;
    for (auto& b : I.bout)
    {
        if (now - b.second < hold) { t.push_back(b.first + ";b"); bk.push_back(b); }
    }
    I.bout = std::move(bk);
    if (t.empty()) return std::string();
    std::string s = "#P:";
    for (size_t i = 0; i < t.size(); ++i) { if (i) s += '|'; s += t[i]; }
    return s;
}

void World::anom_tick()
{
    if (env_.flag("anomsync", 0) != 1) return;
    Anom& A = anom_;
    if (!anom_logged_)
    {
        anom_logged_ = true;
        load();
        if (tab_.have_anomaly) env_.log(fmt("NOLA b73: s2u_anomaly.tsv carregado, %d linhas", (int)tab_.anomaly.size()));
        else env_.log("NOLA b73: falta s2u_anomaly.tsv");
    }
    if (!tab_.have_anomaly) return;
    Vec3 pp;
    int64_t pid = 0;
    if (!env_.local_pawn(pp, pid)) return;
    double now = env_.now();
    if (now >= A.list_at)
    {
        A.list_at = now + 2;
        double R = env_.flag("anomr", 6000);
        std::vector<AnomEnt> L;
        std::vector<Ref> objs;
        env_.list_class("Anomaly", objs);
        for (Ref o : objs)
        {
            if (!env_.valid(o)) continue;
            std::string c = first_token(env_.full_name(o));
            auto row = tab_.anomaly.find(c);
            if (row == tab_.anomaly.end()) continue;
            Vec3 q;
            if (!env_.location(o, q)) continue;
            if ((q.x - pp.x) * (q.x - pp.x) + (q.y - pp.y) * (q.y - pp.y) >= R * R) continue;
            Vec3 s{1, 1, 1};
            Vec3 s2;
            if (env_.call_vec(o, "GetActorScale3D", s2)) s = s2;
            AnomEnt a;
            a.key = fmt("%.0f,%.0f,%.0f", q.x, q.y, q.z);
            a.cls = c;
            a.x = q.x;
            a.y = q.y;
            a.z = q.z;
            a.r = row->second.r * std::max(s.x, s.y);
            a.h = row->second.h * s.z;
            a.cd = std::max((double)row->second.cd, env_.flag("anomcd", 3));
            L.push_back(a);
        }
        A.list = std::move(L);
    }
    for (auto& a : A.list)
    {
        bool inside = (a.x - pp.x) * (a.x - pp.x) + (a.y - pp.y) * (a.y - pp.y) <= a.r * a.r && std::fabs(pp.z - a.z) <= std::max(a.h, 200.0);
        auto st = A.sent.find(a.key);
        double last = st == A.sent.end() ? -1e9 : st->second;
        if (inside && !A.inside.count(a.key) && now - last >= a.cd)
        {
            A.sent[a.key] = now;
            ++A.seq;
            std::string e = fmt("%d;%s;%s", A.seq, a.cls.c_str(), a.key.c_str());
            A.out.push_back(AnomOut{e, now + 3});
            env_.log("NOLA ANOM: entrei em " + e);
        }
        if (inside) A.inside[a.key] = true;
        else A.inside.erase(a.key);
    }
    if (mc_on())
    {
        for (int v = 0; v < env_.views(); ++v)
        {
            std::string rv;
            if (!env_.view_loopback(v) && env_.remote_view("AN", rv, v)) anom_recv(rv, v == 0 ? std::string() : std::to_string(v) + "/", now);
        }
    }
    else
    {
        bool echo = env_.peer_loopback();
        std::string rem;
        if (!echo && env_.remote("AN", rem)) anom_recv(rem, std::string(), now);
    }
    for (auto it = A.seen.begin(); it != A.seen.end();)
    {
        if (now - it->second > 30) it = A.seen.erase(it);
        else ++it;
    }
}

void World::anom_recv(const std::string& rem, const std::string& pre, double now)
{
    Anom& A = anom_;
    {
        for (auto& ent : split_nonempty(rem, '|'))
        {
            if (A.seen.count(pre + ent)) continue;
            A.seen[pre + ent] = now;
            auto f = split(ent, ';');
            if (f.size() != 3) continue;
            long seq;
            if (!parse_long(f[0], seq) || !all_of_set(f[0], "0123456789")) continue;
            const std::string& c = f[1];
            if (!all_of_set(c, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_")) continue;
            auto p = split(f[2], ',');
            if (p.size() != 3) continue;
            long lx, ly, lz;
            if (!all_of_set(p[0], "-0123456789") || !all_of_set(p[1], "-0123456789") || !all_of_set(p[2], "-0123456789")) continue;
            if (!parse_long(p[0], lx) || !parse_long(p[1], ly) || !parse_long(p[2], lz)) continue;
            double x = (double)lx, y = (double)ly, z = (double)lz;
            auto row = tab_.anomaly.find(c);
            if (row == tab_.anomaly.end()) continue;
            bool mine = false;
            for (auto& [k, t] : A.sent)
            {
                size_t cm = k.find(',');
                if (cm == std::string::npos) continue;
                size_t c2 = k.find(',', cm + 1);
                std::string xs = k.substr(0, cm), ys = k.substr(cm + 1, c2 == std::string::npos ? std::string::npos : c2 - cm - 1);
                long kx, ky;
                if (!parse_long(xs, kx) || !parse_long(ys, ky)) continue;
                if (now - t < 3 && ((double)kx - x) * ((double)kx - x) + ((double)ky - y) * ((double)ky - y) < 300.0 * 300.0) mine = true;
            }
            if (mine) { env_.log("NOLA ANOM: " + ent + " ja disparada por mim (eco)"); continue; }
            Ref best;
            double bd = 300.0 * 300.0;
            std::vector<Ref> objs;
            env_.list_class("Anomaly", objs);
            for (Ref o : objs)
            {
                if (!env_.valid(o)) continue;
                if (first_token(env_.full_name(o)) != c) continue;
                Vec3 q;
                if (!env_.location(o, q)) continue;
                double d = (q.x - x) * (q.x - x) + (q.y - y) * (q.y - y) + (q.z - z) * (q.z - z);
                if (d < bd) { best = o; bd = d; }
            }
            if (best)
            {
                A.quiet.until = now + 3;
                A.quiet.x = x;
                A.quiet.y = y;
                A.quiet.r = row->second.r + 300.0;
                env_.note_anom_quiet(x, y, A.quiet.r, A.quiet.until);
                bool ok = env_.call(best, "ForceActivateOnce", {});
                env_.log(fmt("NOLA ANOM: %s -> ForceActivateOnce a %.0f cm ok=%s %s", ent.c_str(), std::sqrt(bd), tf(ok), ok ? "" : "erro"));
            }
            else env_.log("NOLA ANOM: " + ent + " sem essa anomalia a 3 m aqui (nao carregada ou nao existe neste PC)");
        }
    }
}

std::vector<std::string> ViewMerge::merge(const std::vector<std::vector<std::pair<std::string, std::string>>>& per, double now)
{
    std::map<std::string, std::map<int, bool>> live;
    for (int v = 0; v < (int)per.size(); ++v)
        for (const auto& [k, e] : per[(size_t)v])
        {
            auto& slot = m[k];
            auto it = slot.find(v);
            if (it == slot.end() || it->second.first != e) slot[v] = {e, now};
            live[k][v] = true;
        }
    std::vector<std::string> out;
    for (auto it = m.begin(); it != m.end();)
    {
        auto lk = live.find(it->first);
        for (auto s = it->second.begin(); s != it->second.end();)
        {
            if (lk == live.end() || !lk->second.count(s->first)) s = it->second.erase(s);
            else ++s;
        }
        if (it->second.empty()) { it = m.erase(it); continue; }
        const std::pair<std::string, double>* best = nullptr;
        for (const auto& [v, e] : it->second)
            if (!best || e.second > best->second) best = &e;
        out.push_back(best->first);
        ++it;
    }
    return out;
}

bool World::mc_on() { return env_.views() > 1 && env_.flag("mcworld", 1) == 1; }

bool World::rem_merge(const char* tag, std::string& out, ViewMerge& vm, bool door)
{
    out.clear();
    bool any = false;
    std::vector<std::vector<std::pair<std::string, std::string>>> per((size_t)env_.views());
    for (int v = 0; v < env_.views(); ++v)
    {
        std::string s;
        if (!env_.remote_view(tag, s, v)) continue;
        any = true;
        for (auto& ent : split_nonempty(s, '|'))
        {
            size_t sc = door ? ent.rfind(';') : ent.find(';');
            if (sc == std::string::npos) continue;
            std::string k = ent.substr(0, sc);
            if (!door && sc + 1 < ent.size() && ent[sc + 1] == 'b') k += "#b";
            if (door && sc + 1 < ent.size() && (ent[sc + 1] == 'u' || ent[sc + 1] == 'l')) k += std::string("#") + ent[sc + 1];
            per[(size_t)v].push_back({k, ent});
        }
    }
    std::vector<std::string> m = vm.merge(per, env_.now());
    for (size_t i = 0; i < m.size(); ++i) { if (i) out += '|'; out += m[i]; }
    return any;
}

bool World::rem_union(const char* tag, std::string& out)
{
    out.clear();
    bool any = false;
    for (int v = 0; v < env_.views(); ++v)
    {
        std::string s;
        if (!env_.remote_view(tag, s, v)) continue;
        any = true;
        if (s.empty()) continue;
        if (!out.empty()) out += '|';
        out += s;
    }
    return any;
}

int World::sessions_all()
{
    int n = 0;
    for (int v = 0; v < env_.views(); ++v) n += env_.view_sessions(v);
    return n;
}

std::string World::out_an()
{
    Anom& A = anom_;
    if (env_.flag("anomsync", 0) != 1) return std::string();
    double now = env_.now();
    std::vector<std::string> r;
    std::vector<AnomOut> keep;
    for (auto& o : A.out)
        if (now < o.until) { r.push_back(o.e); keep.push_back(o); }
    A.out = std::move(keep);
    while (r.size() > 4) r.erase(r.begin());
    if (r.empty()) return std::string();
    std::string s = "#AN:";
    for (size_t i = 0; i < r.size(); ++i) { if (i) s += '|'; s += r[i]; }
    return s;
}

bool World::anom_quiet(double x, double y)
{
    const AnomQuiet& q = anom_.quiet;
    if (env_.now() >= q.until) return false;
    return (x - q.x) * (x - q.x) + (y - q.y) * (y - q.y) < q.r * q.r;
}

#if !defined(S2X_RELEASE) && defined(S2X_DEVCMD)
void World::census(std::vector<CenRow>& out) const
{
    for (auto& [k, s] : door_.seen) out.push_back({"door", k, s.open ? 1 : 0});
    for (auto& [k, s] : door_.lseen) out.push_back({"lock", k, s.locked ? 1 : 0});
    for (auto& k : dobj_.broken) out.push_back({"broken", k, 1});
}
#endif

std::string World::status()
{
    return fmt("world door.seen=%d door.pend=%d dobj.seen=%d dobj.miss=%d dobj.broken=%d dobj.mine=%d ien.seen=%d anom.list=%d door.lock=%d door.lpend=%d ien.bpend=%d",
               (int)door_.seen.size(), (int)door_.pend.size(), (int)dobj_.seen.size(), (int)dobj_.miss.size(), (int)dobj_.broken.size(),
               (int)dobj_.mine.size(), (int)ien_.seen.size(), (int)anom_.list.size(), (int)door_.lseen.size(), (int)door_.lpend.size(), (int)ien_.bpend.size());
}

}
