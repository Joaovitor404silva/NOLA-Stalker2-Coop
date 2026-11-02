#include "s2m_travel.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>

namespace s2t {

namespace {

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    int n = std::vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (static_cast<size_t>(n) < sizeof buf) return std::string(buf, static_cast<size_t>(n));
    std::string big(static_cast<size_t>(n) + 1, '\0');
    va_start(ap, f);
    std::vsnprintf(&big[0], big.size(), f, ap);
    va_end(ap);
    big.resize(static_cast<size_t>(n));
    return big;
}

const char* tf(bool b) { return b ? "true" : "false"; }

double rad(double deg) { return deg * 3.14159265358979323846 / 180.0; }

double dist3(const Vec3& a, double bx, double by, double bz) {
    return std::sqrt((a.x - bx) * (a.x - bx) + (a.y - by) * (a.y - by) + (a.z - bz) * (a.z - bz));
}

double dist2(double ax, double ay, double bx, double by) {
    return std::sqrt((ax - bx) * (ax - bx) + (ay - by) * (ay - by));
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

size_t spanWhile(const std::string& s, size_t i, bool (*pred)(char)) {
    while (i < s.size() && pred(s[i])) ++i;
    return i;
}

bool numChar(char c) { return isDigit(c) || c == '.'; }
bool wordChar(char c) { return isDigit(c) || isAlpha(c) || c == '_'; }

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> out;
    size_t a = 0;
    for (;;) {
        size_t b = line.find('\t', a);
        if (b == std::string::npos) {
            out.push_back(line.substr(a));
            break;
        }
        out.push_back(line.substr(a, b - a));
        a = b + 1;
    }
    return out;
}

std::string unescape(const std::string& v) {
    std::string o;
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i] == '\\' && i + 1 < v.size()) {
            char c = v[i + 1];
            if (c == '\\') { o += '\\'; ++i; }
            else if (c == 't') { o += '\t'; ++i; }
            else if (c == 'r') { o += '\r'; ++i; }
            else if (c == 'n') { o += '\n'; ++i; }
            else o += v[i];
        } else {
            o += v[i];
        }
    }
    return o;
}

struct GuidePoint {
    const char* sid;
    double x, y, z;
};

const GuidePoint kGuidePoints[] = {
    {"terricon", 421180, 412370, 2120},      {"asylum", 478540, 526470, 340},
    {"sultansk", 665202, 617990, 252},       {"rookievillage", 445964, 657845, -2349},
    {"concreteplant", 498260, 379420, 849},  {"rostok", 321267, 412054, 360},
    {"yanov", 215900, 270210, 946},          {"malahit", 107594, 471669, 424},
    {"himzavod", 318986, 526376, 613},       {"zalesie", 398700, 545500, 990},
    {"pripyat", 232150, 159790, 1114},       {"kazkovy", 259517, 599445, 568},
    {"purification_facility", 409220, 258260, 1040},
    {"bunker", 384673, 341937, 565},
};

}

std::string luaNum(double v) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
    return fmt("%.14g", v);
}

std::string luaFloat(double v) {
    std::string s = luaNum(v);
    if (s.find_first_of(".en") == std::string::npos) s += ".0";
    return s;
}

bool parseCineTsv(const std::string& text, CineTable& out, std::string& err) {
    out.scene.clear();
    out.lead.clear();
    size_t pos = 0;
    int lineNo = 0;
    bool header = true;
    std::map<std::string, int> seen;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (header) {
            header = false;
            continue;
        }
        if (line.empty()) continue;
        std::vector<std::string> c = splitTabs(line);
        if (c.size() < 5) {
            err = fmt("linha %d: colunas", lineNo);
            return false;
        }
        const std::string& tbl = c[0];
        if (c[1].size() < 2 || c[1][0] != 's' || c[2].size() < 2 || c[2][0] != 'i') {
            err = fmt("linha %d: chave", lineNo);
            return false;
        }
        std::string id = c[1].substr(1);
        int idx = std::atoi(c[2].c_str() + 1);
        const std::string& ty = c[3];
        std::string v = unescape(c[4]);
        std::string key = tbl + "/" + id + "/" + std::to_string(idx);
        if (seen[key]++) {
            err = fmt("linha %d: chave repetida", lineNo);
            return false;
        }
        if (tbl == "sscene") {
            SceneRow& r = out.scene[id];
            if (idx == 1 && ty == "s") r.kind = v;
            else if (idx == 2 && ty == "b") r.tpFirst = (v == "true");
            else if (idx == 3 && ty == "b") r.s2c = (v == "true");
            else {
                err = fmt("linha %d: coluna de cena", lineNo);
                return false;
            }
        } else if (tbl == "slead") {
            LeadRow& r = out.lead[id];
            if (idx == 1 && ty == "s") r.scene = v;
            else if (idx == 2 && ty == "i") r.steps = std::atoi(v.c_str());
            else if (idx == 3 && ty == "b") r.tpOnPath = (v == "true");
            else {
                err = fmt("linha %d: coluna de causa", lineNo);
                return false;
            }
        } else {
            err = fmt("linha %d: tabela %s", lineNo, tbl.c_str());
            return false;
        }
    }
    return true;
}

double Travel::fl(const char* name, double def) {
    double v = 0;
    if (env_->flag(name, v)) return v;
    return def;
}

bool Travel::loadCine(const std::string& tsvText) {
    std::string err;
    CineTable t;
    if (!parseCineTsv(tsvText, t, err)) {
        log("NOLA cjoin: s2u_cine erro " + err);
        cjLoaded_ = false;
        return false;
    }
    cj_ = std::move(t);
    cjLoaded_ = true;
    return true;
}

bool Travel::isMain(const std::string& sid) {
    if (sid.rfind("Arch_Bossfight", 0) == 0) return true;
    if (sid.rfind("Arch_E", 0) == 0) {
        size_t i = 6;
        size_t j = spanWhile(sid, i, isDigit);
        if (j > i && j < sid.size() && sid[j] == '_') return true;
    }
    if (sid.size() > 1 && sid[0] == 'E') {
        size_t j = spanWhile(sid, 1, isDigit);
        if (j > 1 && sid.compare(j, 5, "_Duga") == 0) return true;
    }
    size_t s = 0;
    if (sid.rfind("C_", 0) == 0) s = 2;
    if (s >= sid.size() || sid[s] != 'E') return false;
    size_t d = spanWhile(sid, s + 1, isDigit);
    if (d == s + 1 || d >= sid.size() || sid[d] != '_') return false;
    size_t a = spanWhile(sid, d + 1, isAlpha);
    if (a == d + 1) return false;
    std::string k = sid.substr(d + 1, a - d - 1);
    if (k == "MQ") return true;
    return k.rfind("Boss", 0) == 0 || k.rfind("Flashback", 0) == 0;
}

void Travel::mqSeen(const std::string& sid) {
    if (isMain(sid)) {
        mq_.has = true;
        mq_.at = env_->now();
        mq_.sid = sid;
    }
}

bool Travel::mqSid(const std::string& sid) { return fl("mqpull", 0) != 1 || isMain(sid); }

bool Travel::mqOk() { return fl("mqpull", 0) != 1 || cv_.mEp; }

int Travel::mqFlag() {
    if (!cv_.on) {
        cv_.hasMq0 = false;
        cv_.mqOn = false;
        return 0;
    }
    double now = env_->now();
    if (!cv_.hasMq0) {
        cv_.hasMq0 = true;
        cv_.mq0 = now;
    }
    if (!cv_.mqOn && (cv_.sleep || (mq_.has && mq_.at >= cv_.mq0 - fl("mqwin", 15)))) {
        cv_.mqOn = true;
        std::string why = cv_.sleep ? std::string("dormindo") : fmt("%s ha %.1f s", mq_.sid.c_str(), now - mq_.at);
        log(fmt("NOLA mq: %s do host e de MISSAO PRINCIPAL (%s)", cv_.kind == "d" ? "dialogo" : "cinematica", why.c_str()));
    }
    return cv_.mqOn ? 1 : 0;
}

std::string Travel::lrFmt(double m) {
    if (m >= 1e9) return "parada";
    return fmt("%g m", m);
}

double Travel::lrEff(double now) {
    bool hard = false, soft = false, havePull = false;
    double pull = 0;
    for (auto& kv : lr_.req) {
        const LeashReq& q = kv.second;
        if (!q.hasUntil || now < q.untilT) {
            if (q.m >= 1e9) {
                if (q.hard) hard = true;
                else soft = true;
            } else if (!havePull || q.m < pull) {
                havePull = true;
                pull = q.m;
            }
        }
    }
    double base = lr_.base;
    if (hard) return 1e9;
    if (havePull) return std::min(pull, base);
    if (soft) return 1e9;
    return base;
}

void Travel::lrOn() {
    if (lr_.on) return;
    lr_.on = true;
    lr_.req.clear();
    lr_.refused.clear();
    lr_.hasPull = false;
    lr_.hasLast = false;
    double v = 0;
    bool have = env_->flag("leashm", v);
    lr_.base = (have && v < 1e9) ? v : 350;
    lr_.flagSeen = have ? v : 350;
    lr_.hasFlagSeen = true;
    log("NOLA coleira: dono unico ligado (leashown=1): base " + lrFmt(lr_.base) + " (os outros interruptores soltos)");
}

void Travel::lrSet(const std::string& name, double m, double secs, const std::string& why, bool hard) {
    lrOn();
    double now = env_->now();
    bool hasSecs = secs >= 0;
    auto it = lr_.req.find(name);
    if (it != lr_.req.end() && !hasSecs && !it->second.hasUntil && it->second.m == m) {
        it->second.why = why;
        return;
    }
    LeashReq q;
    q.m = m;
    q.hasUntil = hasSecs;
    q.untilT = hasSecs ? now + secs : 0;
    q.hard = hard;
    q.why = why;
    lr_.req[name] = q;
    std::string s = fmt("NOLA coleira: pedido %s %s", name.c_str(), lrFmt(m).c_str());
    if (hasSecs) s += fmt(" (%g s)", secs);
    if (!why.empty()) s += " [" + why + "]";
    s += " -> efetiva " + lrFmt(lrEff(now));
    log(s);
}

void Travel::lrDrop(const std::string& name, const std::string& why) {
    auto it = lr_.req.find(name);
    if (it == lr_.req.end()) return;
    lr_.req.erase(it);
    log("NOLA coleira: fim " + name + " (" + why + ") -> efetiva " + lrFmt(lrEff(env_->now())));
}

bool Travel::lrPull(const std::string& name, double m, double secs, const std::string& why) {
    lrOn();
    double now = env_->now();
    std::string by, txt;
    for (auto& kv : lr_.req) {
        const LeashReq& q = kv.second;
        if (q.hard && q.m >= 1e9 && (!q.hasUntil || now < q.untilT)) {
            by = kv.first;
            txt = "coleira parada por " + kv.first;
        }
    }
    if (by.empty() && lr_.hasPull && lr_.pullName != name && now < lr_.pullUntil) {
        by = lr_.pullName;
        txt = fmt("%s puxa por mais %.0f s", lr_.pullName.c_str(), lr_.pullUntil - now);
    }
    if (!by.empty()) {
        auto& ref = lr_.refused[name];
        if (ref != by) {
            ref = by;
            log("NOLA coleira: pedido " + name + " " + lrFmt(m) + " RECUSADO (" + txt + ")");
        }
        return false;
    }
    lr_.refused.erase(name);
    lr_.hasPull = true;
    lr_.pullName = name;
    lr_.pullUntil = now + std::max(fl("leashpullwin", 20), secs >= 0 ? secs : 0);
    lrSet(name, m, secs, why, false);
    return true;
}

void Travel::lrApply() {
    lrOn();
    double now = env_->now();
    double f = 0;
    if (env_->flag("leashm", f) && lr_.hasFlagSeen && f != lr_.flagSeen) {
        log("NOLA coleira: flags.txt leashm " + luaNum(lr_.base) + " -> " + luaNum(f));
        lr_.base = f;
        lr_.flagSeen = f;
    }
    std::string gone;
    for (auto it = lr_.req.begin(); it != lr_.req.end();) {
        if (it->second.hasUntil && now >= it->second.untilT) {
            gone += (gone.empty() ? "" : ",") + it->first;
            it = lr_.req.erase(it);
        } else {
            ++it;
        }
    }
    if (lr_.hasPull && now >= lr_.pullUntil) lr_.hasPull = false;
    double e = lrEff(now);
    if (!gone.empty()) log("NOLA coleira: fim " + gone + " (tempo) -> efetiva " + lrFmt(e));
    lr_.last = e;
    lr_.hasLast = true;
}

double Travel::leashM() {
    if (lr_.hasLast) return lr_.last;
    return fl("leashm", 350);
}

void Travel::leashCheck(const Remote& r, const Vec3& p) {
    double leash = leashM();
    double d = dist2(r.x, r.y, p.x, p.y) / 100;
    const char* st = d > leash ? "FORA" : (d > leash - 50 ? "AVISO" : "ok");
    if (leashState_ != st) {
        leashState_ = st;
        log(fmt("COLEIRA %s d=%.0fm", st, d));
    }
    if (std::strcmp(st, "FORA") == 0 && role_client()) {
        if (!tpfLeash(r.x + 300, r.y, r.z)) env_->legacySafeTeleport(r.x + 300, r.y, r.z);
    }
}

bool Travel::remoteCine() {
    if (cv_.rk == "d") return false;
    return fl("cine", 0) == 1 && (cv_.rc || env_->now() < cv_.hideUntil);
}

std::string Travel::outV() {
    if (fl("cine", 0) != 1 || !(env_->isHost() || fl("cinetest", 0) == 1)) return "";
    int mf = mqFlag();
    return fmt("#V:%d,%d,%s,%d", cv_.on ? 1 : 0, cv_.tseq, cv_.kind.empty() ? "c" : cv_.kind.c_str(), mf == 1 ? 1 : 0);
}

void Travel::cineTick0() {
    double now = env_->now();
    if (fl("cine", 0) != 1) {
        cv_.on = false;
        cv_.rc = false;
        return;
    }
    bool host = env_->isHost();
    bool test = fl("cinetest", 0) == 1;
    if (host || test) {
        bool on = env_->pcCinematicLocked();
        if (fl("cinesleep", 0) == 1) {
            bool slp = env_->pawnSleeping();
            if (slp != cv_.sleep) {
                cv_.sleep = slp;
                log(std::string("NOLA cine: host ") + (slp ? "DORMINDO (conta como cinematica para o cliente)" : "acordou"));
            }
            on = on || slp;
        }
        bool dlg = false;
        if (fl("cinedlg", 0) == 1 && !on && (fl("lead", 0) != 1 || env_->leadLockMine())) {
            if (!env_->dialogValid() || now - cv_.dwAt > 2) {
                cv_.dwAt = now;
                env_->dialogRescan();
            }
            if (env_->dialogValid()) dlg = env_->dialogVisible();
            on = dlg;
        }
        cv_.kind = dlg ? "d" : "c";
        if (on != cv_.on) {
            cv_.on = on;
            log(std::string("NOLA cine: ") + (on ? (dlg ? "DIALOGO do host comecou" : "CINEMATICA do host comecou (tiros e dano em pausa)") : "cinematica/dialogo do host terminou"));
        }
        Vec3 q;
        if (env_->pawnValid() && env_->pawnPos(q)) {
            if (cv_.hasPos && now - cv_.posT < 1) {
                double d = dist3(q, cv_.pos.x, cv_.pos.y, cv_.pos.z);
                if (d > fl("cinejump", 3000)) {
                    cv_.tseq += 1;
                    log(fmt("NOLA cine: TELEPORTE do host (%.0f m num passo) #%d", d / 100, cv_.tseq));
                }
            }
            cv_.pos = q;
            cv_.posT = now;
            cv_.hasPos = true;
        }
    }
    if (host && !test) return;

    std::string v;
    bool have = env_->x1part("V", v);
    bool parsed = false;
    bool cOn = false;
    long ts = 0;
    std::string rk, rm;
    if (have) {
        size_t i = 0;
        if (i < v.size() && isDigit(v[i])) {
            char c = v[i];
            ++i;
            if (i < v.size() && v[i] == ',') {
                ++i;
                size_t j = spanWhile(v, i, isDigit);
                if (j > i) {
                    ts = std::strtol(v.substr(i, j - i).c_str(), nullptr, 10);
                    i = j;
                    if (i < v.size() && v[i] == ',') ++i;
                    if (i < v.size() && isAlpha(v[i])) { rk = std::string(1, v[i]); ++i; }
                    if (i < v.size() && v[i] == ',') ++i;
                    if (i < v.size() && isDigit(v[i])) rm = std::string(1, v[i]);
                    parsed = true;
                    cOn = (c == '1');
                    cv_.rk = rk.empty() ? "c" : rk;
                    cv_.rm = (rm == "1");
                    if (cOn && cv_.rm) cv_.mEp = true;
                }
            }
        }
    }
    if (!parsed) {
        if (cv_.rc) {
            cv_.rc = false;
            cv_.hideUntil = now + 1;
        }
        return;
    }
    if (cOn != cv_.rc) {
        cv_.rc = cOn;
        if (cOn) {
            cv_.mEp = cv_.rm;
            cv_.cjWant = (cv_.rk == "d");
        }
        if (cOn && cv_.rk == "d") {
            log(mqOk() ? "NOLA cine: host em DIALOGO -> cliente congelado ao lado" : "NOLA mq: host em DIALOGO fora da missao principal: sem puxar nem congelar (mqpull)");
        } else if (cOn) {
            log(std::string("NOLA cine: host em cinematica -> corpo do host escondido") + (mqOk() ? "" : " (fora da missao principal: sem congelar, mqpull)"));
        } else {
            cv_.hideUntil = now + 1;
            cv_.cjWant = false;
            bool ok = mqOk();
            cv_.hasFollow = ok;
            cv_.followUntil = ok ? now + 3 : 0;
            log(std::string("NOLA cine: cinematica do host terminou -> corpo de volta em 1 s") + (ok ? "" : " (fora da missao principal: sem seguir, mqpull)"));
        }
    }
    if (cv_.cjWant && mqOk()) {
        cv_.cjWant = false;
        Remote r;
        bool hr = env_->remoteIn("cjoin", r);
        Vec3 q;
        if (fl("cjoin", 0) == 1 && hr && env_->pawnValid() && now - r.recv < 1 && env_->pawnPos(q)) {
            double d = dist3(q, r.x, r.y, r.z);
            if (d > fl("cjoinm", 1500)) {
                bool ok;
                if (fl("tpfloor", 0) == 1) ok = tpfNow(r.x, r.y, r.z, r.hasYaw ? std::optional<double>(r.yaw) : std::nullopt, "cjoin dialogo");
                else ok = env_->pawnSetLocation(r.x + fl("cjoff", 150), r.y, r.z + 50);
                log(fmt("NOLA cjoin: dialogo do host a %.0f m -> teleporte ok=%s", d / 100, tf(ok)));
            }
        }
    }
    if (cv_.hasRts && ts != cv_.rts) {
        cv_.hasFollow = true;
        cv_.followUntil = now + 3;
        log(fmt("NOLA cine: host teleportado (#%ld)", ts));
    }
    cv_.rts = ts;
    cv_.hasRts = true;
    if (cv_.hasFollow && !cOn && !cv_.fz) {
        if (now > cv_.followUntil) {
            cv_.hasFollow = false;
        } else {
            Remote r;
            bool hr = env_->remoteIn("cine segue", r);
            Vec3 q;
            if (hr && env_->pawnValid() && now - r.recv < 1 && env_->pawnPos(q)) {
                double d = dist2(r.x, r.y, q.x, q.y);
                if (d > fl("cinejump", 3000)) {
                    double follow = fl("cinefollow", 25);
                    double hold = fl("cinehold", 6);
                    if (lrPull("cine", follow, hold, fmt("host a %.0f m", d / 100))) {
                        cv_.hasFollow = false;
                        log(fmt("NOLA cine: host a %.0f m -> sigo ja (coleira %s m por %s s)", d / 100, luaNum(follow).c_str(), luaNum(hold).c_str()));
                    }
                }
            }
        }
    }
}

bool Travel::cineRestore(const std::string& why) {
    if (!cv_.fz) return false;
    Freeze F = *cv_.fz;
    cv_.fz.reset();
    std::string r;
    if (env_->pawnValid()) {
        auto add = [&](const std::string& s) { r += (r.empty() ? "" : " ") + s; };
        if (F.move && !env_->lifeDownFrozen()) add(std::string("mov=") + tf(env_->setMovementMode(1)));
        if (F.input && *F.input) add(std::string("input=") + tf(env_->enableInput()));
        if (!F.god.empty() && !env_->emGodHeld()) add(std::string("god=") + tf(env_->unkillableOff(F.god)));
    }
    log(fmt("NOLA cine: cliente LIBERADO (%s) depois de %.1f s %s", why.c_str(), env_->now() - F.at, r.c_str()));
    return true;
}

void Travel::cineFreeze() {
    double now = env_->now();
    if (env_->isHost() && fl("cinetest", 0) != 1) return;
    bool want = false;
    std::string why = "cinematica terminou";
    if (fl("cine", 0) != 1 || fl("cinefreeze", 1) != 1) why = "cine/cinefreeze desligado";
    else if (!env_->peerUp()) why = "outro lado em silencio";
    else if (!mqOk()) why = "fora da missao principal (mqpull)";
    else if (cv_.rc || now < cv_.hideUntil) want = true;
    if (!cv_.rc) cv_.fzBlock = false;
    if (want && cv_.fzBlock) return;
    double cinemax = fl("cinemax", 180);
    if (want && cv_.fz && now - cv_.fz->at > cinemax) {
        cineRestore("tempo maximo de " + luaNum(cinemax) + " s");
        cv_.fzBlock = true;
        return;
    }
    if (!want) {
        if (cv_.fz) cineRestore(why);
        return;
    }
    if (!env_->pawnValid()) return;
    if (!cv_.fz) {
        cv_.fz = Freeze();
        cv_.fz->at = now;
        log("NOLA cine: cliente CONGELADO e protegido (host em cinematica)");
    }
    Freeze& F = *cv_.fz;
    std::uint64_t addr = env_->pawnAddr();
    if (!F.hasAddr || F.addr != addr) {
        F.hasAddr = true;
        F.addr = addr;
        double hp = 0;
        bool gotHp = env_->getHP(hp);
        F.hp.reset();
        if (gotHp && hp > 0) F.hp = hp;
        F.move = env_->disableMovement();
        if (fl("cineinput", 0) == 1) F.input = env_->disableInput();
        if (fl("cinegod", 1) == 1) F.god = env_->unkillableOn();
        log(fmt("NOLA cine: congelado mov=%s input=%s god=%s hp=%s", tf(F.move), F.input ? tf(*F.input) : "nil",
                F.god.empty() ? "nil" : F.god.c_str(), F.hp ? luaFloat(*F.hp).c_str() : "nil"));
    }
    if (env_->movementMode() != 0) env_->disableMovement();
    if (F.hp) {
        double hp = 0;
        if (env_->getHP(hp)) {
            if ((hp > 0 || !F.god.empty()) && hp < *F.hp) {
                env_->forceSetHP(*F.hp);
                F.held += 1;
            }
        }
    }
    double b = 0;
    if (env_->getBleeding(b) && b > 0) env_->forceSetBleeding(0.0);
    env_->dmgProtectUntil(now + 1);
}

void Travel::shutdown() {
    cineRestore("modulo descarregado");
    tpfEnd(nullptr);
}

void Travel::cjTables() {
    if (cjLoaded_ && !cjAnnounced_) {
        cjAnnounced_ = true;
        log("NOLA cjoin: tabelas carregadas");
    }
}

bool Travel::cjBeside(double x, double y, double z) {
    double off = fl("cjoff", 150);
    double tx = x + off, tz = z + 50;
    if (fl("tpfloor", 0) == 1) return tpfNow(tx - off, y, tz - 50, std::nullopt, "cjoin cena");
    return env_->pawnSetLocation(tx, y, tz);
}

double Travel::cjBeforeStart(const std::string& sid) {
    if (fl("cjoin", 0) != 1 || !mqSid(sid)) return 0;
    cjTables();
    if (!cjLoaded_) return 0;
    auto it = cj_.scene.find(sid);
    if (it == cj_.scene.end() || it->second.tpFirst) return 0;
    Remote r;
    bool hr = env_->remoteIn("cj68", r);
    Vec3 p;
    bool hp = env_->pawnValid() && env_->pawnPos(p);
    if (!(hr && hp) || env_->now() - r.recv > 1) return 0;
    if (dist3(p, r.x, r.y, r.z) <= fl("cjoinm", 1500)) return 0;
    bool ok = cjBeside(r.x, r.y, r.z);
    log(fmt("NOLA cjoin: antes de %s_S2C -> teleporte para junto do outro ok=%s", sid.c_str(), tf(ok)));
    return ok ? fl("cjdelay", 0.7) : 0;
}

bool Travel::cjOnRemoteCause(const std::string& cause) {
    if (fl("cjoin", 0) != 1 || !mqSid(cause)) return false;
    cjTables();
    if (!cjLoaded_) return false;
    auto it = cj_.lead.find(cause);
    if (it == cj_.lead.end() || it->second.tpOnPath) return false;
    Remote r;
    bool hr = env_->remoteIn("cj68", r);
    Vec3 p;
    bool hp = env_->pawnValid() && env_->pawnPos(p);
    if (!(hr && hp) || env_->now() - r.recv > 1) return false;
    if (dist3(p, r.x, r.y, r.z) <= fl("cjoinm", 1500)) return false;
    bool ok = cjBeside(r.x, r.y, r.z);
    log(fmt("NOLA cjoin: causa %s do cliente leva a %s -> host teleportado para junto do cliente ok=%s", cause.c_str(), it->second.scene.c_str(), tf(ok)));
    return ok;
}

std::optional<double> Travel::tpfFloor(double x, double y, double z) {
    TraceHit t = env_->trace(x, y, z + 60, x, y, z - 300);
    if (t.ok && t.hit && t.hasZ && std::fabs(t.z - (z - 90)) <= 150) return t.z;
    return std::nullopt;
}

Travel::Spot Travel::tpfSpot(double x, double y, double z, std::optional<double> yaw) {
    double b = fl("tpfback", 100);
    if (b > 0 && yaw) {
        double a = rad(*yaw + 180);
        double bx = x + std::cos(a) * b, by = y + std::sin(a) * b;
        TraceHit blk = env_->trace(x, y, z + 60, bx, by, z + 60);
        bool clear = blk.ok && !blk.hit;
        std::optional<double> fz;
        if (clear) fz = tpfFloor(bx, by, z);
        if (fz) return {bx, by, z + 10, "atras", fmt("linha livre, chao %.0f", *fz)};
        std::string tr = clear ? "atras sem chao" : ("atras " + std::string(blk.ok ? "parede" : "sem trace"));
        return {x, y, z + 10, "ponto do outro", tr};
    }
    return {x, y, z + 10, "ponto do outro", "sem yaw/tpfback"};
}

std::vector<Travel::Cand> Travel::tpfCands(double x, double y, std::optional<double> yaw) {
    double b = fl("tpfback", 100), r = fl("tpfring", 150), y0 = yaw ? *yaw : 0;
    std::vector<Cand> C;
    struct Dir {
        double ang;
        const char* name;
    };
    const Dir dirs[] = {{180, "atras"}, {-90, "esquerda"}, {90, "direita"}, {0, "frente"}};
    for (const Dir& d : dirs) {
        if (b > 0) {
            double a = rad(y0 + d.ang);
            C.push_back({x + std::cos(a) * b, y + std::sin(a) * b, d.name});
        }
    }
    if (r > 0) {
        for (int k = 0; k <= 7; ++k) {
            double a = rad(y0 + 180 + k * 45);
            C.push_back({x + std::cos(a) * r, y + std::sin(a) * r, "anel " + luaNum(r) + " #" + std::to_string(k + 1)});
        }
    }
    C.push_back({x, y, "ponto do outro"});
    return C;
}

Travel::FitResult Travel::tpfFit(double x, double y, double z, std::optional<double> yaw, bool floor, const std::string& why) {
    double rotYaw = yaw ? *yaw : 0;
    double py = 0;
    if (env_->pawnYaw(py)) rotYaw = py;
    std::vector<Cand> C = tpfCands(x, y, yaw);
    int nt = 0, ns = 0;
    for (size_t n = 0; n < C.size(); ++n) {
        const Cand& c = C[n];
        bool skip = false;
        if (c.x != x || c.y != y) {
            TraceHit w = env_->trace(x, y, z + 60, c.x, c.y, z + 60);
            if (w.ok && w.hit) skip = true;
            else if (floor && !tpfFloor(c.x, c.y, z)) skip = true;
        }
        if (skip) {
            ++ns;
            continue;
        }
        ++nt;
        if (env_->pawnTeleportTo(c.x, c.y, z + 10, rotYaw)) {
            Vec3 q;
            if (!env_->pawnPos(q)) q = {c.x, c.y, z + 10};
            log(fmt("NOLA tpfloor: %s: K2_TeleportTo tentativa %d ok (%s; %d recusadas, %d puladas) -> %.0f,%.0f,%.0f", why.c_str(), static_cast<int>(n + 1),
                    c.label.c_str(), nt - 1, ns, q.x, q.y, q.z));
            return {true, q.x, q.y, q.z, c.label};
        }
    }
    Vec3 q;
    if (!env_->pawnPos(q)) q = {0, 0, 0};
    log(fmt("NOLA tpfloor: %s: K2_TeleportTo recusado nas %d tentativas (%d puladas por parede/sem chao) -> fico onde estou %.0f,%.0f,%.0f (nunca dentro de objetos)",
            why.c_str(), nt, ns, q.x, q.y, q.z));
    return {false, q.x, q.y, q.z, "fiquei onde estava"};
}

void Travel::tpfEnd(const char* why) {
    if (!tpf_.job) return;
    TpfJob J = *tpf_.job;
    tpf_.job.reset();
    if (J.src) env_->destroyActor(J.src);
    if (why) log("NOLA tpfloor: " + J.why + ": " + why);
}

bool Travel::tpfStart(double x, double y, double z, std::optional<double> yaw, const std::string& why) {
    if (tpf_.job) return false;
    if (!env_->inWorld(x, y, z)) {
        log("NOLA tpfloor: " + why + ": destino fora do mundo; nada");
        return false;
    }
    ActorRef src = env_->spawnStreamingSource(x, y, z + 500);
    double now = env_->now();
    tpf_.n += 1;
    TpfJob J;
    J.why = why;
    J.x = x;
    J.y = y;
    J.z = z;
    J.hasYaw = yaw.has_value();
    J.yaw = yaw ? *yaw : 0;
    J.t0 = now;
    J.next = now + 1;
    J.stage = "wait";
    J.src = src;
    tpf_.job = J;
    log(fmt("NOLA tpfloor: %s (#%d): streaming em %.0f,%.0f,%.0f; espero o mundo carregar em volta do outro jogador", why.c_str(), tpf_.n, x, y, z));
    return true;
}

bool Travel::tpfNow(double ax, double ay, double az, std::optional<double> yaw, const std::string& why) {
    if (!env_->pawnValid()) return false;
    tpfEnd(tpf_.job ? ("substituido por " + why).c_str() : nullptr);
    if (!yaw) {
        Remote r;
        if (env_->remoteIn("tpfloor", r) && (r.x - ax) * (r.x - ax) + (r.y - ay) * (r.y - ay) < 250000 && r.hasYaw) yaw = r.yaw;
    }
    bool fly = !tpfFloor(ax, ay, az).has_value();
    double tx, ty, tz;
    std::string how, tr;
    if (fly) {
        tx = ax;
        ty = ay;
        tz = az + 10;
        how = "ponto do outro";
        tr = "chao do outro nao carregado";
    } else {
        Spot s = tpfSpot(ax, ay, az, yaw);
        tx = s.x;
        ty = s.y;
        tz = s.z;
        how = s.how;
        tr = s.tr;
    }
    bool ok;
    bool fit = fl("tpfit", 0) == 1 && !fly;
    if (fit) {
        FitResult f = tpfFit(ax, ay, az, yaw, true, why);
        ok = f.ok;
        tx = f.x;
        ty = f.y;
        tz = f.z;
        how = f.label;
        tr = "b113 K2_TeleportTo";
    } else {
        ok = env_->pawnSetLocation(tx, ty, tz);
    }
    if (fly) {
        env_->setMovementMode(5);
        double now = env_->now();
        tpf_.n += 1;
        TpfJob J;
        J.why = why;
        J.x = ax;
        J.y = ay;
        J.z = az;
        J.t0 = now;
        J.next = now + 1;
        J.hasYaw = yaw.has_value();
        J.yaw = yaw ? *yaw : 0;
        J.stage = "land";
        J.tx = tx;
        J.ty = ty;
        J.tz = tz;
        J.aside = true;
        tpf_.job = J;
    }
    log(fmt("NOLA tpfloor: %s: -> %.0f,%.0f,%.0f (%s; outro z=%.0f dz=%.0f) ok=%s%s | %s", why.c_str(), tx, ty, tz, how.c_str(), az, tz - az, tf(ok),
            fly ? ", voo ate o chao" : "", tr.c_str()));
    return ok;
}

bool Travel::tpfLeash(double x, double y, double z) {
    if (fl("tpfloor", 0) != 1 || !role_client()) return false;
    if (tpf_.job) return true;
    double ax = x - 300, ay = y, az = z;
    std::optional<double> yaw;
    Remote r;
    if (env_->remoteIn("tpfloor", r) && r.hasYaw && std::fabs(r.x + 300 - x) < 5 && std::fabs(r.y - y) < 5) yaw = r.yaw;
    tpfStart(ax, ay, az, yaw, "coleira");
    return true;
}

void Travel::tpfStep(TpfJob& J, double now) {
    if (J.stage == "wait") {
        J.w += 1;
        Remote r;
        if (env_->remoteIn("tpfloor", r) && now - r.recv < 2 && (r.x - J.x) * (r.x - J.x) + (r.y - J.y) * (r.y - J.y) < 1e8) {
            J.x = r.x;
            J.y = r.y;
            J.z = r.z;
            if (r.hasYaw) {
                J.yaw = r.yaw;
                J.hasYaw = true;
            }
        }
        bool loaded = tpfFloor(J.x, J.y, J.z).has_value();
        int ready = loaded ? 1 : env_->streamReady(J.x, J.y, J.z, fl("tpfsr", 2000));
        if (ready != 1 && J.w < static_cast<int>(fl("tpfwait", 40))) return;
        std::optional<double> jy = J.hasYaw ? std::optional<double>(J.yaw) : std::nullopt;
        double tx = J.x, ty = J.y, tz = J.z + 10;
        std::string how = "ponto do outro",
                    tr = (ready == 1 ? "mundo carregado, colisao do chao so perto do jogador" : "mundo nao carregado") + std::string(" em ") + std::to_string(J.w) + " s";
        if (loaded) {
            Spot s = tpfSpot(J.x, J.y, J.z, jy);
            tx = s.x;
            ty = s.y;
            tz = s.z;
            how = s.how;
            tr = s.tr;
        }
        bool ok;
        bool fit = fl("tpfit", 0) == 1 && loaded;
        if (fit) {
            FitResult f = tpfFit(J.x, J.y, J.z, jy, true, J.why);
            ok = f.ok;
            tx = f.x;
            ty = f.y;
            tz = f.z;
            how = f.label;
            tr = "b113 K2_TeleportTo";
        } else {
            ok = env_->pawnSetLocation(tx, ty, tz);
        }
        if (!ok && fit) {
            std::string m = fmt("teleporte apos %d s: nenhum ponto livre perto do outro jogador -> fico onde estou", J.w);
            tpfEnd(m.c_str());
            env_->setMovementMode(1);
            return;
        }
        if (!loaded) env_->setMovementMode(5);
        J.tx = tx;
        J.ty = ty;
        J.tz = tz;
        J.aside = !loaded;
        J.stage = "land";
        log(fmt("NOLA tpfloor: %s: teleporte apos %d s -> %.0f,%.0f,%.0f (%s; outro z=%.0f dz=%.0f) ok=%s%s | %s", J.why.c_str(), J.w, tx, ty, tz, how.c_str(), J.z, tz - J.z,
                tf(ok), loaded ? "" : ", modo voo", tr.c_str()));
        return;
    }
    J.m += 1;
    Vec3 q;
    if (!env_->pawnPos(q)) return;
    if (q.z < J.tz - 3000 && J.falls < 3) {
        J.falls += 1;
        J.tx = J.x;
        J.ty = J.y;
        J.tz = J.z + 10;
        J.aside = true;
        env_->pawnSetLocation(J.tx, J.ty, J.tz);
        env_->setMovementMode(5);
        log(fmt("NOLA tpfloor: %s: caiu (z %.0f) -> voo de novo (%d)", J.why.c_str(), q.z, J.falls));
        return;
    }
    std::optional<double> g = tpfFloor(q.x, q.y, q.z);
    if (g && J.aside && fl("tpfit", 0) == 1) {
        std::optional<double> jy = J.hasYaw ? std::optional<double>(J.yaw) : std::nullopt;
        FitResult f = tpfFit(J.x, J.y, J.z, jy, true, J.why + " (chao apareceu)");
        if (f.ok) q = {f.x, f.y, f.z};
    }
    if (g || J.m >= 60) {
        env_->setMovementMode(1);
        Remote r;
        bool hr = env_->remoteIn("tpfloor", r);
        double oz = hr ? r.z : J.z;
        log(fmt("NOLA tpfloor: CHEGOU (%s) em %.0f s: eu z=%.0f outro z=%.0f dz=%.0f cm chao=%s %s", J.why.c_str(), now - J.t0, q.z, oz, q.z - oz, tf(g.has_value()),
                std::fabs(q.z - oz) <= 150 ? "MESMO ANDAR" : "ANDAR DIFERENTE"));
        tpfEnd(nullptr);
    }
}

void Travel::tpfTick() {
    double now = env_->now();
    if (fl("tpfloor", 0) != 1) {
        if (tpf_.job) {
            tpfEnd("tpfloor desligado");
            if (env_->pawnValid()) env_->setMovementMode(1);
        }
        return;
    }
    if (!tpf_.job || now < tpf_.job->next) return;
    tpf_.job->next = now + 1;
    bool pawnOk = env_->pawnValid();
    if (now - tpf_.job->t0 > fl("tpfwait", 40) + 75) {
        tpfEnd("desisti (tempo)");
        if (pawnOk) env_->setMovementMode(1);
        return;
    }
    if (pawnOk) {
        TpfJob J = *tpf_.job;
        tpfStep(J, now);
        if (tpf_.job) *tpf_.job = J;
    }
}

void Travel::cfollowTick() {
    if (!role_client()) return;
    Remote r;
    if (!env_->remoteIn("cfollow/cfall", r)) return;
    if (!env_->peerUp()) return;
    double now = env_->now();
    if (now - r.recv > 3) return;
    if (!env_->pawnValid()) return;
    Vec3 p;
    if (!env_->pawnPos(p)) return;
    if (fl("cfall", 1) == 1) {
        bool falling = false;
        env_->isFalling(falling);
        if (falling) {
            if (!hasCfFall_) {
                hasCfFall_ = true;
                cfFallT_ = now;
            }
        } else {
            hasCfFall_ = false;
        }
        if (hasCfFall_ && now - cfFallT_ >= fl("cfallsec", 3) && p.z < r.z - fl("cfallm", 30) * 100) {
            hasCfFall_ = false;
            bool ok;
            if (fl("tpfloor", 0) == 1) ok = tpfNow(r.x, r.y, r.z, r.hasYaw ? std::optional<double>(r.yaw) : std::nullopt, "cfall");
            else ok = env_->pawnSetLocation(r.x + 100, r.y, r.z + 150);
            bool okS = env_->stopMovement();
            log(fmt("NOLA cfall: cliente caindo fora do mundo (z=%.0f, host z=%.0f) -> posto ao lado do host ok=%s stop=%s", p.z, r.z, tf(ok), tf(okS)));
            return;
        }
    }
    if (fl("cfollow", 0) != 1) return;
    bool f = false;
    env_->isFalling(f);
    if (f) return;
    double dx = r.x - p.x, dy = r.y - p.y;
    double d = std::sqrt(dx * dx + dy * dy);
    double keep = fl("cfollowm", 30) * 100;
    if (d <= keep) return;
    double step = std::min(fl("cfollowstep", 3) * 100, d - keep);
    double nx = p.x + dx / d * step, ny = p.y + dy / d * step;
    bool ok = env_->pawnSetLocation(nx, ny, p.z + fl("cfollowlift", 100));
    if (!ok) log("NOLA cfollow erro: pawnSetLocation");
    else if (cfLog_ < now) {
        cfLog_ = now + 2;
        log(fmt("NOLA cfollow: %.0f m -> passo %.0f cm", d / 100, step));
    }
}

const char* Travel::ftNear(double x, double y, double z, double& dist) {
    const char* best = nullptr;
    bool have = false;
    double bd = 0;
    for (const GuidePoint& g : kGuidePoints) {
        double d = std::sqrt((x - g.x) * (x - g.x) + (y - g.y) * (y - g.y) + (z - g.z) * (z - g.z));
        if (!have || d < bd) {
            best = g.sid;
            bd = d;
            have = true;
        }
    }
    dist = bd;
    return best;
}

bool Travel::ftMe(Vec3& out) { return env_->pawnValid() && env_->pawnPos(out); }

bool Travel::ftRemote(double now, double maxAge, Vec3& out) {
    Remote r;
    int v = ft_.pend ? ft_.pend->view : 0;
    if ((v == 0 ? env_->remoteIn("ft", r) : env_->remoteInV(v, "ft", r)) && now - r.recv < maxAge) {
        out = {r.x, r.y, r.z};
        return true;
    }
    return false;
}

std::string Travel::ftLockNow() { return ftLock(env_->now()); }

std::string Travel::ftLock(double now) {
    if (env_->pcCinematicLocked()) return "cinematica";
    if (!env_->dialogValid() || now - ft_.dwAt > 2) {
        ft_.dwAt = now;
        env_->dialogRescan();
    }
    if (env_->dialogValid() && env_->dialogVisible()) return "dialogo";
    if (cv_.fz) return "congelado (cinematica do host)";
    if (env_->ndDead() || env_->lifeDown()) return "morto/caido";
    return "";
}

void Travel::ftPark(bool on, const std::string& why) {
    if (on) {
        ft_.hasPark = true;
        ft_.park = why;
        lrSet("ft_park", 1e9, -1, why, false);
    } else if (ft_.hasPark) {
        ft_.hasPark = false;
        ft_.park.clear();
        lrDrop("ft_park", why);
    }
}

void Travel::ftSee(double now) {
    Vec3 q;
    if (!ftMe(q)) return;
    FtLp cur;
    cur.p = q;
    cur.addr = env_->pawnAddr();
    bool hadLp = ft_.hasLp;
    FtLp L = ft_.lp;
    ft_.lp = cur;
    ft_.hasLp = true;
    if (!hadLp) return;
    double d = dist3(q, L.p.x, L.p.y, L.p.z);
    if (d < fl("ftjump", 30000)) return;
    double dd = 0;
    const char* sid = ftNear(q.x, q.y, q.z, dd);
    std::string why;
    if (dd > fl("ftrad", 10000)) why = "longe dos pontos de guia";
    else if (cur.addr != L.addr && fl("ftpawn", 0) != 1) why = "pawn trocado (load)";
    else if (now < ft_.selfUntil) why = "teleporte nosso (seguir)";
    else {
        Vec3 r;
        if (ftRemote(now, 2, r) && dist3(r, q.x, q.y, q.z) <= fl("ftnear", 5000)) why = "o outro jogador ja esta aqui";
    }
    log(fmt("NOLA ft: salto de %.0f m, ponto de guia mais perto %s a %.0f m -> %s", d / 100, sid, dd / 100, why.empty() ? "VIAGEM RAPIDA" : why.c_str()));
    if (why.empty()) {
        FtCand c;
        c.sid = sid;
        c.at = now;
        c.d = d;
        ft_.cand = c;
        ft_.dlgAt = -1000;
    }
}

void Travel::ftSend(double now) {
    if (!ft_.cand) return;
    FtCand C = *ft_.cand;
    if (now - C.at < fl("ftsettle", 3)) return;
    if (ft_.lk == "cinematica" && now - C.at < 30) return;
    Vec3 q;
    if (!ftMe(q)) return;
    double dd = 0;
    const char* sid = ftNear(q.x, q.y, q.z, dd);
    if (C.sid != sid || dd > fl("ftrad", 10000)) {
        ft_.cand.reset();
        log("NOLA ft: saiu do ponto " + C.sid + " antes do envio (outro teleporte?); nada enviado");
        return;
    }
    ft_.seq += 1;
    ft_.ev = fmt("#FT:%d,%s,%.0f,%.0f,%.0f,%lld", ft_.seq, C.sid.c_str(), q.x, q.y, q.z, env_->unixTime());
    ft_.evUntil = now + fl("ftkeep", 30);
    FtMine m;
    m.at = now;
    m.sid = C.sid;
    m.seq = ft_.seq;
    ft_.mine = m;
    ft_.cand.reset();
    log(fmt("NOLA ft: VIAGEM RAPIDA minha para %s (salto de %.0f m) -> #FT %d enviado; o outro jogador vem junto", C.sid.c_str(), C.d / 100, ft_.seq));
}

void Travel::ftWaitOther(double now) {
    if (!ft_.mine) return;
    FtMine Mi = *ft_.mine;
    Vec3 q, r;
    bool hq = ftMe(q);
    bool hr = ftRemote(now, 1, r);
    if (hq && hr && dist3(q, r.x, r.y, r.z) <= fl("ftnear", 5000)) {
        log(fmt("NOLA ft: o outro jogador chegou junto (%.0f m) %.0f s depois da minha viagem para %s", dist3(q, r.x, r.y, r.z) / 100, now - Mi.at, Mi.sid.c_str()));
        ft_.mine.reset();
    } else if (now - Mi.at > 2 * fl("ftwait", 45)) {
        log(fmt("NOLA ft: o outro jogador NAO chegou em %.0f s (viagem para %s)", now - Mi.at, Mi.sid.c_str()));
        ft_.mine.reset();
    }
}

int Travel::ftNv() {
    if (!env_->isHost() || !env_->hub() || fl("mcft", 1) != 1) return 1;
    return std::max(1, std::min(4, env_->views()));
}

void Travel::ftIn(double now) {
    int nv = ftNv();
    for (int v = 0; v < nv; ++v) {
        std::string sg;
        if (!(v == 0 ? env_->x1part("FT", sg) : env_->x1partV(v, "FT", sg))) continue;
        ftInV(now, v, sg);
    }
}

void Travel::ftInV(double now, int v, const std::string& sg) {
    size_t i = 0;
    size_t j = spanWhile(sg, i, isDigit);
    if (j == i || j >= sg.size() || sg[j] != ',') return;
    std::string seq = sg.substr(i, j - i);
    i = j + 1;
    j = spanWhile(sg, i, wordChar);
    if (j == i || j >= sg.size() || sg[j] != ',') return;
    std::string sid = sg.substr(i, j - i);
    i = j + 1;
    std::string nums[3];
    for (int k = 0; k < 3; ++k) {
        size_t s = i;
        if (i < sg.size() && sg[i] == '-') ++i;
        size_t e = spanWhile(sg, i, numChar);
        if (e == i || e >= sg.size() || sg[e] != ',') return;
        nums[k] = sg.substr(s, e - s);
        i = e + 1;
    }
    j = spanWhile(sg, i, isDigit);
    if (j == i) return;
    std::string ts = sg.substr(i, j - i);
    std::string opid;
    if (j < sg.size() && sg[j] == ',') {
        size_t k = spanWhile(sg, j + 1, isDigit);
        if (k > j + 1) opid = sg.substr(j + 1, k - j - 1);
    }
    std::string key = seq + "@" + ts;
    std::string& rk = v == 0 ? ft_.rkey : ft_.rkeyV[v & 3];
    if (key == rk) return;
    rk = key;
    int nv = ftNv();
    std::string who = nv > 1 ? fmt(" (pid %d)", env_->viewPid(v)) : std::string();
    if (!opid.empty() && !env_->isHost() && fl("mcft", 1) == 1 && std::atoi(opid.c_str()) == env_->myPid()) {
        log("NOLA ft: o host repassou a minha propria viagem (#FT " + seq + ", " + sid + "): nada a seguir");
        return;
    }
    long long age = env_->unixTime() - std::strtoll(ts.c_str(), nullptr, 10);
    if (age > fl("ftmaxage", 120)) {
        log("NOLA ft: #FT " + seq + " (" + sid + ") antigo (" + std::to_string(age) + " s), ignorado");
        return;
    }
    const std::string* ownSid = nullptr;
    if (ft_.cand) ownSid = &ft_.cand->sid;
    else if (ft_.mine && now - ft_.mine->at < fl("ftkeep", 30)) ownSid = &ft_.mine->sid;
    if (ownSid) {
        if (env_->isHost()) {
            log("NOLA ft: os dois viajaram (" + *ownSid + " / " + sid + "): o host vence, nao sigo o cliente" + who);
            return;
        }
        log("NOLA ft: os dois viajaram (" + sid + " / " + *ownSid + "): o host vence, sigo o host");
    }
    if (nv > 1 && ft_.pend && ft_.pend->view != v) {
        log(fmt("NOLA ft: o cliente%s viajou para %s (#FT %s) mas o host ja segue outra viagem (%s): fica para tras; o grupo segue a primeira", who.c_str(), sid.c_str(), seq.c_str(),
                ft_.pend->sid.c_str()));
        return;
    }
    ft_.cand.reset();
    ft_.mine.reset();
    FtPend P;
    P.seq = seq;
    P.sid = sid;
    P.x = std::strtod(nums[0].c_str(), nullptr);
    P.y = std::strtod(nums[1].c_str(), nullptr);
    P.z = std::strtod(nums[2].c_str(), nullptr);
    P.at = now;
    P.view = v;
    ft_.pend = P;
    log(fmt("NOLA ft: o outro jogador viajou para %s (#FT %s) -> vou junto%s", sid.c_str(), seq.c_str(), who.c_str()));
    if (nv > 1) {
        ft_.seq += 1;
        ft_.ev = fmt("#FT:%d,%s,%s,%s,%s,%s,%d", ft_.seq, sid.c_str(), nums[0].c_str(), nums[1].c_str(), nums[2].c_str(), ts.c_str(), env_->viewPid(v));
        ft_.evUntil = now + fl("ftkeep", 30);
        log(fmt("NOLA ft: viagem do cliente%s repassada aos outros clientes (#FT %d): todos vem junto", who.c_str(), ft_.seq));
    }
}

bool Travel::ftArrived(const Vec3* q, double now, double t0) {
    Vec3 r;
    bool hr = ftRemote(now, 2, r);
    bool have = q && hr;
    double d = have ? dist3(*q, r.x, r.y, r.z) : 0;
    if (have && d <= fl("ftnear", 5000)) {
        log(fmt("NOLA ft: CHEGUEI junto do outro jogador (%.0f m) em %.1f s", d / 100, now - t0));
    } else {
        log(fmt("NOLA ft: seguir terminou LONGE do outro jogador (%s m) em %.1f s", have ? fmt("%.0f", d / 100).c_str() : "?", now - t0));
    }
    return have && d <= fl("ftnear", 5000);
}

bool Travel::ftGround(double x, double y, double z) {
    TraceHit t = env_->trace(x, y, z + 2000, x, y, z - 6000);
    return t.ok && t.hit && t.hasZ && std::fabs(t.z - z) < 3000;
}

void Travel::ftTpStep(double now) {
    FtTp& T = *ft_.tp;
    if (T.tpf) {
        if (!tpf_.job) {
            Vec3 q;
            bool hq = ftMe(q);
            ftArrived(hq ? &q : nullptr, now, T.t0);
            ft_.pend.reset();
            ft_.tp.reset();
        }
        return;
    }
    if (now < T.next) return;
    T.next = now + 1;
    Vec3 q;
    bool hq = ftMe(q);
    auto go = [&](bool& okF) {
        bool ok = env_->pawnSetLocation(T.x, T.y, T.z + 100);
        okF = env_->setMovementMode(5);
        T.stage = "fly";
        T.t1 = now;
        return ok;
    };
    if (T.stage == "go") {
        bool okF;
        bool ok = go(okF);
        log(fmt("NOLA ft: host teleportado para junto do cliente (%s; %.0f,%.0f,%.0f) ok=%s voo=%s; espero o chao", T.sid.c_str(), T.x, T.y, T.z, tf(ok), tf(okF)));
    } else if (T.stage == "fly") {
        bool g = now - T.t1 >= 3 && ftGround(T.x, T.y, T.z);
        if (g || now - T.t1 >= fl("ftfly", 40)) {
            bool okW = env_->setMovementMode(1);
            T.stage = "walk";
            T.t2 = now;
            log(fmt("NOLA ft: modo andando ok=%s (chao=%s apos %.0f s)", tf(okW), tf(g), now - T.t1));
            if (!T.told) {
                T.told = true;
                ftArrived(hq ? &q : nullptr, now, T.t0);
                ft_.pend.reset();
            }
        }
    } else if (T.stage == "walk") {
        if (hq && q.z < T.z - 300 && T.falls < 2) {
            T.falls += 1;
            bool okF;
            bool ok = go(okF);
            log(fmt("NOLA ft: host caiu (z %.0f < %.0f) -> voo de novo ok=%s (%d)", q.z, T.z, tf(ok), T.falls));
        } else if (now - T.t2 >= 15) {
            ft_.tp.reset();
        }
    }
}

void Travel::ftFollow(double now) {
    if (ft_.tp) {
        ftTpStep(now);
        return;
    }
    if (ft_.fol) {
        FtFol Fo = *ft_.fol;
        Vec3 q, r;
        bool hq = ftMe(q);
        bool hr = ftRemote(now, 1, r);
        if (hq && hr && dist3(q, r.x, r.y, r.z) <= fl("ftnear", 5000)) {
            ftArrived(&q, now, Fo.t0);
            ft_.fol.reset();
            ft_.pend.reset();
        } else if (now > Fo.waitUntil) {
            ft_.fol.reset();
            if (ft_.pend && Fo.tries >= 3) {
                log("NOLA ft: seguir nao funcionou em 3 tentativas; desisto (" + ft_.pend->sid + ")");
                ft_.pend.reset();
            } else if (ft_.pend) {
                ft_.pend->tries = Fo.tries;
                log("NOLA ft: seguir sem efeito em " + luaNum(fl("ftwait", 45)) + " s; de novo");
            }
        }
        return;
    }
    if (!ft_.pend) return;
    FtPend& P = *ft_.pend;
    if (now - P.at > fl("ftqmax", 300)) {
        log("NOLA ft: seguir para " + P.sid + " esperou demais (" + P.held + "); desisto");
        ft_.pend.reset();
        return;
    }
    if (!ft_.lk.empty()) {
        if (P.held != ft_.lk) {
            P.held = ft_.lk;
            log("NOLA ft: seguir segurado: estou em " + ft_.lk + " (na fila)");
        }
        return;
    }
    if (!P.held.empty()) {
        log("NOLA ft: " + P.held + " acabou -> sigo agora");
        P.held.clear();
    }
    double tx = P.x, ty = P.y, tz = P.z;
    Vec3 r;
    if (ftRemote(now, 1, r) && dist3(r, P.x, P.y, P.z) <= fl("ftrad", 10000)) {
        tx = r.x;
        ty = r.y;
        tz = r.z;
    }
    if (!env_->inWorld(tx, ty, tz)) {
        log("NOLA ft: destino fora do mundo; nao sigo");
        ft_.pend.reset();
        return;
    }
    Vec3 q;
    if (!ftMe(q)) return;
    double d = dist3(q, tx, ty, tz);
    if (d <= fl("ftnear", 5000)) {
        log(fmt("NOLA ft: ja estou no destino %s (%.0f m) -> nada a fazer", P.sid.c_str(), d / 100));
        ft_.pend.reset();
        return;
    }
    ft_.selfUntil = now + fl("ftwait", 45) + 20;
    if (role_client()) {
        double leash = fl("ftleash", 30);
        if (!lrPull("ft", leash, 1, P.sid)) return;
        if (ft_.hasPark) {
            ft_.hasPark = false;
            ft_.park.clear();
            lrDrop("ft_park", "seguir");
        }
        ft_.dlgAt = -1000;
        FtFol Fo;
        Fo.hasLow = true;
        Fo.lowUntil = now + 1;
        Fo.waitUntil = now + fl("ftwait", 45);
        Fo.t0 = now;
        Fo.tries = P.tries + 1;
        ft_.fol = Fo;
        log(fmt("NOLA ft: cliente segue o host ate %s: coleira em %s m por 1 s (%.0f m ate o destino, tentativa %d)", P.sid.c_str(), luaNum(leash).c_str(), d / 100, Fo.tries));
    } else if (fl("tpfloor", 0) == 1 && tpfStart(tx, ty, tz, std::nullopt, "ft host " + P.sid)) {
        FtTp T;
        T.tpf = true;
        T.t0 = now;
        T.sid = P.sid;
        ft_.tp = T;
    } else {
        FtTp T;
        T.x = tx + fl("ftoff", 200);
        T.y = ty;
        T.z = tz;
        T.stage = "go";
        T.t0 = now;
        T.sid = P.sid;
        ft_.tp = T;
        ftTpStep(now);
    }
}

std::string Travel::outFt() {
    if (fl("ftfollow", 0) != 1 || ft_.ev.empty() || env_->now() > ft_.evUntil) return "";
    return ft_.ev;
}

void Travel::ftTick() {
    if (fl("ftfollow", 0) != 1) {
        bool held = ft_.hasPark || ft_.fol || ft_.tp || ft_.pend || ft_.cand || ft_.mine || ft_.hasLp;
        if (held) {
            if (ft_.hasPark) ftPark(false, "ftfollow desligado");
            if (ft_.tp && env_->pawnValid()) env_->setMovementMode(1);
            ft_.fol.reset();
            ft_.tp.reset();
            ft_.pend.reset();
            ft_.cand.reset();
            ft_.mine.reset();
            ft_.hasLp = false;
        }
        return;
    }
    double now = env_->now();
    if (now < ft_.nextAt) return;
    ft_.nextAt = now + 0.2;
    if (!env_->pawnValid()) return;
    ft_.lk = ftLock(now);
    ftIn(now);
    ftSee(now);
    ftSend(now);
    ftWaitOther(now);
    ftFollow(now);
    if (role_client()) {
        bool talk = ft_.lk == "dialogo" || ft_.lk == "cinematica";
        if (talk) ft_.dlgAt = now;
        std::string want;
        if (!ft_.fol) {
            if (talk) want = ft_.lk;
            else if (now - ft_.dlgAt < fl("ftgrace", 15)) want = "logo depois de um dialogo";
            else if (ft_.cand) want = "minha viagem";
            else if (ft_.mine) want = "minha viagem: espero o host";
        }
        if (!want.empty()) ftPark(true, want);
        else if (ft_.hasPark) ftPark(false, ft_.fol ? "seguir" : "livre");
    }
}

void Travel::tickA() {
    cineTick0();
    cineFreeze();
}

void Travel::tickB() {
    cfollowTick();
    ftTick();
    lrApply();
    tpfTick();
}

}

namespace {
std::unique_ptr<s2t::Travel> g_travel;
}

void s2m_travel_init(s2t::Env* env, const std::string& cineTsvText) {
    g_travel = std::make_unique<s2t::Travel>(env);
    g_travel->loadCine(cineTsvText);
}

bool s2m_travel_ready() { return g_travel != nullptr; }
void s2m_travel_tick_a() { if (g_travel) g_travel->tickA(); }
void s2m_travel_tick_b() { if (g_travel) g_travel->tickB(); }
std::string s2m_travel_out_v() { return g_travel ? g_travel->outV() : std::string(); }
std::string s2m_travel_out_ft() { return g_travel ? g_travel->outFt() : std::string(); }
bool s2m_travel_remote_cine() { return g_travel && g_travel->remoteCine(); }
bool s2m_travel_cine_on() { return g_travel && g_travel->cineOn(); }
bool s2m_travel_frozen() { return g_travel && g_travel->frozen(); }
bool s2m_travel_god_held() { return g_travel && g_travel->godHeld(); }
bool s2m_travel_tpf_busy() { return g_travel && g_travel->tpf().job.has_value(); }
std::string s2m_travel_ft_lock() { return g_travel ? g_travel->ftLockNow() : std::string(); }
void s2m_travel_shutdown() { if (g_travel) g_travel->shutdown(); }
double s2m_travel_leash_m() { return g_travel ? g_travel->leashM() : 350; }
void s2m_travel_leash_check(const s2t::Remote& r, const s2t::Vec3& p) { if (g_travel) g_travel->leashCheck(r, p); }
void s2m_travel_lr_set(const std::string& n, double m, double secs, const std::string& why, bool hard) { if (g_travel) g_travel->lrSet(n, m, secs, why, hard); }
void s2m_travel_lr_drop(const std::string& n, const std::string& why) { if (g_travel) g_travel->lrDrop(n, why); }
bool s2m_travel_lr_pull(const std::string& n, double m, double secs, const std::string& why) { return g_travel && g_travel->lrPull(n, m, secs, why); }
bool s2m_travel_pull_now(double x, double y, double z, std::optional<double> yaw, const std::string& why) { return g_travel && g_travel->tpfNow(x, y, z, yaw, why); }
void s2m_travel_mq_seen(const std::string& sid) { if (g_travel) g_travel->mqSeen(sid); }
bool s2m_travel_mq_sid(const std::string& sid) { return g_travel ? g_travel->mqSid(sid) : true; }
bool s2m_travel_is_main(const std::string& sid) { return s2t::Travel::isMain(sid); }
double s2m_travel_cj_before_start(const std::string& sid) { return g_travel ? g_travel->cjBeforeStart(sid) : 0; }
bool s2m_travel_cj_on_remote_cause(const std::string& sid) { return g_travel && g_travel->cjOnRemoteCause(sid); }
