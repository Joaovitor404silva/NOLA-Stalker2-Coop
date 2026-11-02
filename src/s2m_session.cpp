#include "s2m_session.h"
#include "s2m_text.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>

namespace s2m {

namespace {

using nola_text::tx;
using nola_text::T;

std::string fmt(const char* f, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof buf, f, ap);
    va_end(ap);
    return buf;
}

std::string ns(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) return fmt("%lld", (long long)v);
    return fmt("%.14g", v);
}

std::string os(const std::optional<double>& v) { return v ? ns(*v) : "nil"; }
std::string tb(bool b) { return b ? "true" : "false"; }
double lmod(double a, double b) { double r = std::fmod(a, b); if (r != 0 && ((r < 0) != (b < 0))) r += b; return r; }

std::string hm(const std::optional<double>& t) {
    if (!t) return "?";
    double v = lmod(*t, 86400);
    return fmt("%02d:%02d", (int)std::floor(v / 3600), (int)std::floor(lmod(v, 3600) / 60));
}

std::string hmc(double t) { return hm(std::optional<double>(t)); }

std::string cleanNum(const std::string& s) {
    std::string r;
    for (char c : s) if (isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_') r += c;
    return r;
}

std::string cleanAlnum(const std::string& s) {
    std::string r;
    for (char c : s) if (isalnum((unsigned char)c)) r += c;
    return r;
}

std::string whyText(char c) {
    switch (c) {
        case 'B': return "ocupado (dialogo/cinematica)";
        case 'F': return "em combate";
        case 'X': return "morto/caido";
        case 'T': return "nao deitou numa cama a tempo";
        case 'O': return "sem pawn";
    }
    return std::string(1, c);
}

std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> o;
    size_t a = 0;
    for (;;) {
        size_t b = s.find(d, a);
        if (b == std::string::npos) { o.push_back(s.substr(a)); break; }
        o.push_back(s.substr(a, b - a));
        a = b + 1;
    }
    return o;
}

bool isDigits(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!isdigit((unsigned char)c)) return false;
    return true;
}

bool isNumTok(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!(isdigit((unsigned char)c) || c == '.' || c == '-')) return false;
    return true;
}

bool toNum(const std::string& s, double& out) {
    if (s.empty()) return false;
    char* e = nullptr;
    out = strtod(s.c_str(), &e);
    return e && *e == 0;
}

bool isFlagKeyChar(char c) { return isalnum((unsigned char)c) || c == '_'; }

}

std::string Val::str() const { return t == 2 ? s : ns(n); }

const char* Session::kHcKeys =
    "qsync qrollback qrelay lead ldtrig ldkill ldhold ldclose ldblk ldblkr mqpull mqwin qpick choice n3kill n3trig n3use qitem qimirror n7 dmir dmirpick cjoin "
    "locks taken takenown ground loot ptake qreward itake tsync lootshare lscont sstash artsync idrop qshare "
    "emsync cine doors dobj dobjgen dobjfx ien ientab ienprompt ienanim ienlamp ienhatch ientoggle lockbrk anomsync iamir vendmir vendone npsmove "
    "lifeguard downhp downsecs revivehp reviveheal reviveradius revivesecs revwait downgod downdie reviveitem bothdie lifegod bothdown dieload nopause ftfollow sleepsync sleepmode difficulty friendlyfire ffscale";

Session::Session(Env& env) : env_(env) {}

std::unique_ptr<Session> session_create(Env& env) { return std::unique_ptr<Session>(new Session(env)); }

void Session::refreshClock() {
    Clock c;
    if (!env_.readClock(c)) { gameSecs_.reset(); return; }
    gameSecs_ = c.gs;
    gtod_ = lmod(c.gs / 150 + 9, 24) * 3600;
    if (c.wt) wtod_ = *c.wt;
    int w = env_.targetWeather();
    if (w >= 0) wxCache_ = w; else wxCache_.reset();
}

void Session::fastTime() {
    Clock c;
    if (!env_.readClock(c)) return;
    gameSecs_ = c.gs;
    gtod_ = lmod(c.gs / 150 + 9, 24) * 3600;
    if (c.wt) wtod_ = *c.wt;
}

bool Session::setGameTime(int h, int m, int s, const std::string& why, bool weather) {
    std::optional<double> bg = gtod_, bw = wtod_;
    SetTime r = env_.cmdSetTime(h, m, s, weather);
    if (r == SetTime::NoMgr) { log("TIME: CustomConsoleManagerAA nao achado"); return false; }
    bool okT = r == SetTime::Ok;
    refreshClock();
    log(fmt("TIME: %s(%d,%d,%d) %s ok=%s %s jogo %.0f -> %.0f s do dia, clima %.0f -> %.0f s do dia", weather ? "XSetWeatherTime" : "XSetGameTime", h, m, s,
        why.c_str(), tb(okT).c_str(), "", bg ? *bg : -1, gtod_ ? *gtod_ : -1, bw ? *bw : -1, wtod_ ? *wtod_ : -1));
    return okT;
}

std::string Session::headTimeFields() {
    return fmt("\"gt\":%s,\"wt\":%s,\"wx\":%d", gtod_ ? fmt("%.1f", *gtod_).c_str() : "-1", wtod_ ? fmt("%.1f", *wtod_).c_str() : "-1", wxCache_ ? *wxCache_ : -1);
}

void Session::tick1000() {
    double now = env_.clock();
    if (now - clkAt_ >= 5) {
        clkAt_ = now;
        refreshClock();
        int st = (int)F("settime", 0);
        if (st != 0 && st != settimeDone_) {
            settimeDone_ = st;
            setGameTime((st / 100) % 24, st % 100, 0, "flag settime", false);
        }
    }
}

void Session::onSnapshot() {
    bool client = !env_.host();
    bool st = F("synctime", 0) == 1 && (client || F("synctimetest", 0) == 1);
    if (st) fastTime();
    if (st && gtod_ && wtod_) {
        struct C { const char* k; double mine; bool w; double* at; } cs[2] = {{"gt", *gtod_, false, &syncAtGt_}, {"wt", *wtod_, true, &syncAtWt_}};
        for (auto& c : cs) {
            double r;
            if (env_.remoteTod(c.k, r) && r >= 0) {
                RemoteInfo ri;
                double age = env_.remote(ri) ? env_.clock() - ri.recv : 0;
                if (age < 0) age = 0;
                if (age > F("peersilent", 10)) continue;
                double tod = lmod(r + age * 24 + F("synctimetest", 0) * 3 * 3600, 86400);
                double diff = std::fabs(tod - c.mine);
                if (diff > 43200) diff = 86400 - diff;
                bool ahead = lmod(tod - c.mine, 86400) > 43200;
                if (!c.w && ahead && F("syncback", 0) != 1) {
                    if (diff > F("syncthresh", 600) && env_.clock() > *c.at) {
                        *c.at = env_.clock() + F("syncgap", 30);
                        log(fmt("TIME: sync %s ignorado, cliente adiantado %.0f s (voltar custaria um dia de jogo)", c.k, diff));
                    }
                    continue;
                }
                if (diff > F("syncthresh", 600) && env_.clock() > *c.at) {
                    *c.at = env_.clock() + F("syncgap", 30);
                    setGameTime((int)std::floor(tod / 3600), (int)std::floor(lmod(tod, 3600) / 60), (int)std::floor(lmod(tod, 60)),
                        fmt("sync %s (diferenca %.0f s)", c.k, diff), c.w);
                }
            }
        }
    }
    if (F("syncweather", 0) == 1 && (client || F("wxtest", 99) < 99) && wxCache_) {
        int rwx = env_.remoteWx();
        if (F("wxtest", 99) < 99) rwx = (int)F("wxtest", 99);
        if (rwx >= 0 && rwx <= 9 && rwx != *wxCache_ && env_.clock() > wxAt_) {
            wxAt_ = env_.clock() + 60;
            int was = *wxCache_;
            bool okW = env_.cmdWeather(false, rwx);
            refreshClock();
            log(fmt("WEATHER: XSwitchToWeather(%d) ok=%s %s alvo %d -> %s", rwx, tb(okW).c_str(), okW ? "" : "false", was, wxCache_ ? ns(*wxCache_).c_str() : "nil"));
        }
    }
}

void Session::peerTick() {
    Peer& P = peer_;
    double now = env_.clock();
    RemoteInfo r;
    bool have = env_.remote(r);
    bool fresh = have && !r.left && (now - r.recv) < F("peersilent", 10);
    if (!fresh) {
        if (P.state == "up") {
            P.state = "silent";
            P.silentAt = now;
            log(fmt("NOLA PEER SILENT: sem snapshot novo ha %d s (travas soltas, corpo/alvo escondidos, espelhos removidos)", (int)F("peersilent", 10)));
            env_.onPeerSilent();
            if (!r.left) env_.screen("s2m_session_peer", tx(T::sess_peer_silent), 0);
        }
        if (P.state == "silent" && have && r.left) {
            P.state = "left";
            P.leftAt = now;
            P.lefts++;
            if (r.local) {
                log(fmt("NOLA PEER END (#%d): this game ended the co-op session (main menu or session off); no peer notice", P.lefts));
                env_.screenClear("s2m_session_peer");
                return;
            }
            log(fmt("NOLA PEER LEFT (#%d): %s depois de %.0f s em silencio; jogo segue sozinho", P.lefts, r.bye ? "o outro jogo fechou (BYE)" : "sem resposta (peerleft)", now - P.silentAt));
            if (env_.maxClients() > 1) log(fmt("NOLA PEER view 0 = pid %d left; players %d of %d", env_.viewPid(0), playersUp(), env_.maxClients() + 1));
            env_.screenClear("s2m_session_peer");
            env_.screen("s2m_session_peer", leftMsg(env_.viewPid(0)), F("peerleftmsg", 10));
        }
        return;
    }
    if (P.live != 1) {
        if (!P.firstSeen) { P.firstSeen = true; P.firstT = r.t; P.live = -1; }
        else if (r.t != P.firstT) P.live = 1;
    }
    std::string role, mod, core, diff, nonce;
    auto f = split(r.hello, ',');
    P.helloOk = !r.hello.empty() && f.size() == 5;
    if (P.helloOk) { role = f[0]; mod = f[1]; core = f[2]; diff = f[3]; nonce = f[4]; }
    std::string id = r.sid + "/" + (P.helloOk ? nonce : "nil");
    std::string was = P.state;
    if (was != "up") {
        P.state = "up";
        P.ups++;
        log(fmt("NOLA PEER UP (#%d)%s", P.ups, was == "silent" ? fmt(" depois de %.0f s em silencio", now - P.silentAt).c_str() : was == "left" ? fmt(" (voltou %.0f s depois de sair)", now - P.leftAt).c_str() : ""));
        if (env_.maxClients() > 1) log(fmt("NOLA PEER view 0 = pid %d up; players %d of %d", env_.viewPid(0), playersUp(), env_.maxClients() + 1));
        env_.screenClear("s2m_session_peer");
        if (was == "left") env_.screen("s2m_session_peer", tx(T::sess_peer_back), 6);
    }
    auto nz = [&](const std::string& s) { return P.helloOk ? s : std::string("nil"); };
    if (!P.hasId || id != P.id) {
        bool first = !P.hasId;
        P.hasId = true;
        P.id = id; P.sid = r.sid; P.nonce = nonce; P.role = role; P.mod = mod; P.core = core; P.diff = diff;
        P.sessions++;
        P.warned = false;
        env_.onNewSession();
        log(fmt("NOLA PEER SESSION #%d%s: papel=%s mod=%s modulo=%s dificuldade=%s id=%s", P.sessions, first ? "" : " (o outro lado relancou)", nz(role).c_str(),
            nz(mod).c_str(), nz(core).c_str(), nz(diff).c_str(), id.c_str()));
    }
    if (!P.warned) {
        P.warned = true;
        P.loopback = P.helloOk && nonce == env_.myNonce();
        if (!P.helloOk) log("NOLA PEER AVISO: o outro lado nao manda #H (modulo antigo): versoes nao conferidas");
        else if (P.loopback) log("NOLA PEER: e o nosso proprio snapshot (loopback); papel nao conferido");
        else {
            std::string myRole = env_.host() ? "host" : "client";
            if (role == myRole) {
                log("NOLA PEER ERRO: os dois lados estao com papel=" + role + " (um tem de ser host e o outro client: role.txt)");
                env_.screen("s2m_session_role", fmt(tx(T::sess_role_both).c_str(), role.c_str()), 20);
            }
            if (mod != cleanNum(env_.modVer())) log("NOLA PEER AVISO: main.lua diferente (aqui " + env_.modVer() + ", la " + mod + ")");
            if (core != cleanNum(env_.coreVer())) log("NOLA PEER AVISO: s2core.lua diferente (aqui " + env_.coreVer() + ", la " + core + ")");
        }
    }
}

int Session::playersUp() const {
    int n = 1 + (peer_.state == "up" ? 1 : 0);
    for (int v = 1; v < kViews; ++v) n += vpeer_[v].state == "up" ? 1 : 0;
    return n;
}

std::string Session::leftMsg(int pid) const {
    if (playersUp() > 1) return fmt(tx(T::sess_left_n).c_str(), pid);
    int gone = peer_.state == "left" ? 1 : 0;
    for (int v = 1; v < kViews; ++v) gone += vpeer_[v].state == "left" ? 1 : 0;
    return tx(gone > 1 ? T::sess_alone_many : T::sess_alone_one);
}

void Session::viewsTick() {
    int nv = env_.views();
    double now = env_.clock();
    for (int v = 1; v < kViews; ++v) {
        Peer& P = vpeer_[v];
        std::string scr = fmt("s2m_session_peer_v%d", v);
        if (v >= nv) {
            if (P.state != "none") {
                env_.screenClear(scr.c_str());
                P = Peer{};
            }
            continue;
        }
        RemoteInfo r;
        bool have = env_.remoteView(v, r);
        int pid = env_.viewPid(v);
        bool fresh = have && !r.left && (now - r.recv) < F("peersilent", 10);
        if (!fresh) {
            if (P.state == "up") {
                P.state = "silent";
                P.silentAt = now;
                log(fmt("NOLA PEER SILENT pid=%d (view %d): no new snapshot for %d s; players %d of %d", pid, v, (int)F("peersilent", 10), playersUp(), env_.maxClients() + 1));
                if (!r.left) env_.screen(scr.c_str(), fmt(tx(T::sess_view_silent).c_str(), pid), 0);
            }
            if (P.state == "silent" && have && r.left) {
                P.state = "left";
                P.leftAt = now;
                P.lefts++;
                if (r.local) {
                    log(fmt("NOLA PEER END pid=%d (view %d, #%d): this game ended the co-op session; no peer notice", pid, v, P.lefts));
                    env_.screenClear(scr.c_str());
                    continue;
                }
                log(fmt("NOLA PEER LEFT pid=%d (view %d, #%d): %s after %.0f s silent; players %d of %d", pid, v, P.lefts, r.bye ? "that game closed (BYE)" : "no answer (peerleft)",
                    now - P.silentAt, playersUp(), env_.maxClients() + 1));
                env_.screenClear(scr.c_str());
                env_.screen(scr.c_str(), leftMsg(pid), F("peerleftmsg", 10));
            }
            continue;
        }
        if (P.live != 1) {
            if (!P.firstSeen) { P.firstSeen = true; P.firstT = r.t; P.live = -1; }
            else if (r.t != P.firstT) P.live = 1;
        }
        std::string role, mod, core, diff, nonce;
        auto f = split(r.hello, ',');
        P.helloOk = !r.hello.empty() && f.size() == 5;
        if (P.helloOk) { role = f[0]; mod = f[1]; core = f[2]; diff = f[3]; nonce = f[4]; }
        std::string id = r.sid + "/" + (P.helloOk ? nonce : "nil");
        std::string was = P.state;
        if (was != "up") {
            P.state = "up";
            P.ups++;
            log(fmt("NOLA PEER UP pid=%d (view %d, #%d)%s; players %d of %d", pid, v, P.ups,
                was == "silent" ? fmt(" after %.0f s silent", now - P.silentAt).c_str() : was == "left" ? fmt(" (back %.0f s after leaving)", now - P.leftAt).c_str() : "", playersUp(),
                env_.maxClients() + 1));
            env_.screenClear(scr.c_str());
            if (was == "left") env_.screen(scr.c_str(), fmt(tx(T::sess_view_back).c_str(), pid), 6);
        }
        auto nz = [&](const std::string& s) { return P.helloOk ? s : std::string("nil"); };
        if (!P.hasId || id != P.id) {
            bool first = !P.hasId;
            P.hasId = true;
            P.id = id; P.sid = r.sid; P.nonce = nonce; P.role = role; P.mod = mod; P.core = core; P.diff = diff;
            P.sessions++;
            P.warned = false;
            log(fmt("NOLA PEER SESSION pid=%d (view %d) #%d%s: role=%s mod=%s core=%s diff=%s id=%s", pid, v, P.sessions, first ? "" : " (new game on that slot)", nz(role).c_str(),
                nz(mod).c_str(), nz(core).c_str(), nz(diff).c_str(), id.c_str()));
        }
        if (!P.warned) {
            P.warned = true;
            P.loopback = P.helloOk && nonce == env_.myNonce();
            if (!P.helloOk) log(fmt("NOLA PEER AVISO pid=%d: no #H from that game (old module): versions not checked", pid));
            else if (P.loopback) log(fmt("NOLA PEER pid=%d: our own snapshot (loopback); role not checked", pid));
            else {
                if (env_.host() && role == "host") {
                    log(fmt("NOLA PEER ERRO pid=%d: that game is also set as host (every joining game must be client: role.txt)", pid));
                    env_.screen("s2m_session_role", fmt(tx(T::sess_role_host_n).c_str(), pid), 20);
                }
                if (mod != cleanNum(env_.modVer())) log(fmt("NOLA PEER AVISO pid=%d: main.lua differs (here %s, there %s)", pid, env_.modVer().c_str(), mod.c_str()));
                if (core != cleanNum(env_.coreVer())) log(fmt("NOLA PEER AVISO pid=%d: s2core.lua differs (here %s, there %s)", pid, env_.coreVer().c_str(), core.c_str()));
            }
        }
    }
}

std::string Session::outHello() {
    return "#H:" + std::string(env_.host() ? "host" : "client") + "," + cleanNum(env_.modVer()) + "," + cleanNum(env_.coreVer()) + "," +
        cleanAlnum(env_.fstr("difficulty", "Medium")) + "," + env_.myNonce();
}

void Session::wxLock(double now, const std::string& why) {
    if (F("wxlock", 1) != 1) return;
    double before = 0, after = 0;
    bool hb = env_.getReqDur(before);
    env_.setReqDur(1e9);
    bool ha = env_.getReqDur(after);
    wx_.lockAt = now + 10;
    if ((hb ? before : -12345.0) != (ha ? after : -12345.0))
        log(fmt("NOLA clima: seletor travado (%s): duracao pedida %s -> %s", why.c_str(), hb ? ns(before).c_str() : "nil", ha ? ns(after).c_str() : "nil"));
}

void Session::wxTick() {
    if (env_.host() || F("wxfollow", 1) != 1) return;
    if (!(peer_.state == "up" && peer_.live != -1)) return;
    Wx& W = wx_;
    int rwx = env_.remoteWx();
    int mine = env_.targetWeather();
    if (rwx < 0 || rwx > 9 || mine < 0) return;
    double now = env_.clock();
    uintptr_t paddr;
    bool hasP = env_.ioPawn(paddr);
    bool fresh = false;
    if (hasP && (!W.paddr || *W.paddr != paddr)) { W.paddr = paddr; fresh = true; W.done = false; wxLock(now, "pawn novo"); }
    if (peer_.sessions != W.sess) { W.sess = peer_.sessions; fresh = true; W.done = false; }
    if (fresh) W.instant = true;
    if (now >= W.lockAt) {
        double cur;
        if (env_.getReqDur(cur) && cur < 1e8 && mine != 8 && mine != 7) wxLock(now, "o jogo escolheu de novo");
        else W.lockAt = now + 10;
    }
    if (W.checkAt && now >= *W.checkAt) {
        int want = W.checkWant;
        W.checkAt.reset();
        if (mine == want) log(fmt("NOLA clima: troca para %d confirmada (%s)", want, W.checkHow.c_str()));
        else if (!W.retried) {
            W.retried = true;
            bool ok = env_.cmdWeather(true, want);
            W.checkAt = now + 2;
            W.checkHow = "XForceWeather (nova tentativa)";
            log(fmt("NOLA clima: troca para %d NAO pegou em 2 s (alvo %d) -> XForceWeather(%d) ok=%s", want, mine, want, tb(ok).c_str()));
        } else log(fmt("NOLA clima: troca para %d NAO pegou nem com XForceWeather (alvo %d)", want, mine));
    }
    if (rwx != W.host) { W.host = rwx; W.done = false; W.retried = false; }
    if (mine != W.mine) {
        if (W.mine != -2 && mine != W.setTo) W.instant = true;
        W.mine = mine;
        W.done = false;
    }
    wxAt_ = now + 60;
    if (rwx == 8) rwx = mine;
    if (rwx == 7 && mine != 7) W.instant = true;
    bool hard = (mine == 7) != (rwx == 7);
    if (mine != rwx && now >= W.next && (!W.done || (hard && now >= W.retryAt))) {
        W.next = now + 2;
        W.done = true;
        W.retryAt = now + 60;
        bool inst = F("wxinstant", 1) == 1 && W.instant;
        const char* how = inst ? "XForceWeather" : "XSwitchToWeather";
        bool ok = env_.cmdWeather(inst, rwx);
        W.instant = false;
        W.setTo = rwx;
        wxAt_ = now + 60;
        log(fmt("NOLA clima: host em %d, eu em %d -> %s(%d) ok=%s%s", rwx, mine, how, rwx, tb(ok).c_str(), hard ? " (clima de emissao so de um lado)" : ""));
        W.checkAt = now + 2;
        W.checkWant = rwx;
        W.checkHow = how;
        if (rwx != 7) wxLock(now, "depois da troca");
    }
}

void Session::emRelease(const std::string& why) {
    if (!em_.paused) return;
    bool ok = env_.cmdEm("XResumeScheduledEmission");
    em_.paused = false;
    em_.addr.reset();
    log("NOLA emission: agenda propria de volta (" + why + ") ok=" + tb(ok));
}

std::string Session::outEm() {
    if (F("emsync", 0) != 1 || !env_.host() || !em_.on) return "";
    return fmt("#E:%d,%d", *em_.on ? 1 : 0, em_.n);
}

bool Session::emReset(const std::string& why) {
    bool a = env_.cmdEm("XGetEmissionState");
    bool p = env_.cmdEm("XPauseScheduledEmission");
    bool b = env_.cmdEm("XGetEmissionState");
    log("NOLA emission: agenda propria pausada de novo (" + why + "): XGetEmissionState antes ok=" + tb(a) + ", XPauseScheduledEmission ok=" + tb(p) +
        ", XGetEmissionState depois ok=" + tb(b) + " (a saida do estado vai para o console do jogo)");
    return p;
}

void Session::emTick() {
    Em& E = em_;
    double now = env_.clock();
    if (F("emsync", 0) != 1) { if (E.paused) emRelease("emsync desligado"); return; }
    if (!env_.host() && !env_.coop()) {
        if (E.paused) emRelease(peer_.state == "left" ? "o host saiu da sessao" : "sem host na sessao: jogo solo");
        E.noSave = false;
        E.armed = false;
        if (!E.god.empty() || E.hp) {
            if (!E.god.empty() && !env_.cineGodHeld()) env_.cmdUnkillable(E.god, false);
            E.god.clear();
            E.hp.reset();
            E.godLogged = false;
            log("NOLA emission: protecao do cliente terminada (sem host na sessao)");
        }
        return;
    }
    if (now < E.at) return;
    E.at = now + 1;
    bool host = env_.host();
    if (host) {
        int on = env_.emissionActive();
        if (on >= 0 && (!E.on || *E.on != (on == 1))) {
            bool o = on == 1;
            if (o) E.n++;
            if (E.on || o) log("NOLA emission: host " + (o ? ("EMISSAO #" + std::to_string(E.n) + " comecou") : std::string("emissao terminou")));
            E.on = o;
        }
        if (F("emtest", 0) != 1) return;
    }
    uintptr_t addr;
    if (!env_.ioPawn(addr) && !env_.anyPawn(addr)) return;
    if (!E.addr || *E.addr != addr) {
        E.addr = addr;
        E.stopAt = 0;
        E.stops = 0;
        E.offAt = now - F("emgrace", 10);
        E.newAt = now;
        E.gs0 = gameSecs_;
        E.ran = false;
        if (F("emreset", 0) == 1) E.paused = emReset("pawn novo");
        else E.paused = env_.cmdEm("XPauseScheduledEmission");
        log("NOLA emission: agenda propria de emissoes em pausa ok=" + tb(E.paused));
    }
    std::string c;
    int n = 0;
    bool haveC = false;
    if (host) { c = (E.on && *E.on) ? "1" : "0"; n = E.n; haveC = true; }
    else {
        std::string e;
        if (env_.x1part("E", e) && e.size() >= 3 && isdigit((unsigned char)e[0]) && e[1] == ',') {
            size_t i = 2;
            while (i < e.size() && isdigit((unsigned char)e[i])) i++;
            if (i > 2) { c = e.substr(0, 1); n = atoi(e.substr(2, i - 2).c_str()); haveC = true; }
        }
    }
    {
        int m = env_.emissionActive();
        if (m == 1) E.calmAt = now + F("emprotect", 120);
        E.noSave = (m == 1) || now < E.calmAt || (haveC && c == "1");
        if (haveC && c == "1") E.hostOnAt = now;
        if (m == 1) { if (!E.mineOn) E.mineOn = now; } else E.mineOn.reset();
        if (E.newAt && !E.ran) {
            if (gameSecs_ && E.gs0 && *gameSecs_ != *E.gs0) E.ran = true; else E.newAt = now;
            if (gameSecs_ && !E.gs0) E.gs0 = gameSecs_;
        }
        bool loadCase = m == 1 && E.newAt && now - *E.newAt <= 10;
        bool cNot1 = !(haveC && c == "1");
        bool alone = m == 1 && cNot1 && E.mineOn && now - std::max(*E.mineOn, E.hostOnAt ? *E.hostOnAt : -1e9) > F("emskew", 15);
        if ((loadCase || alone) && !E.armed && !host) {
            E.armed = true;
            log(fmt("NOLA emission: protecao armada (%s)", loadCase ? "save carregado dentro de emissao" : ("minha emissao ha mais de " + ns(F("emskew", 15)) + " s sem a do host").c_str()));
        }
        if (E.armed && m != 1 && now >= E.calmAt) E.armed = false;
        bool want = E.armed && cNot1 && !host;
        if (want) {
            if (E.god.empty() || now >= E.godAt) {
                E.godAt = now + 5;
                std::string uid;
                bool ok = env_.pawnGuid(uid) && env_.cmdUnkillable(uid, true);
                if (ok) E.god = uid;
                if (!E.godLogged) {
                    E.godLogged = true;
                    log(fmt("NOLA emission: minha emissao sem a do host -> cliente PROTEGIDO (Unkillable ok=%s, sem saves) ate %d s depois do fim", tb(ok).c_str(), (int)F("emprotect", 120)));
                }
            }
            double hp;
            if (env_.pawnHp(hp) && hp > 0) {
                if (!E.hp || hp > *E.hp) E.hp = hp;
                if (hp < *E.hp) env_.forceSetHp(*E.hp);
            }
        } else if (!E.god.empty() || E.hp) {
            bool keep = env_.cineGodHeld();
            if (!E.god.empty() && !keep) env_.cmdUnkillable(E.god, false);
            E.god.clear();
            E.hp.reset();
            E.godLogged = false;
            log("NOLA emission: protecao do cliente terminada");
        }
    }
    if (!haveC) return;
    bool ron = c == "1";
    int mineI = env_.emissionActive();
    bool mineKnown = mineI >= 0;
    bool mine = mineI == 1;
    if (ron && !E.ron && F("emreset", 0) == 1 && !host) emReset("emissao do host #" + std::to_string(n));
    if (ron && !E.ron) { E.hk++; E.hostSince = now; E.seenK = -1; }
    if (ron && mine) E.seenK = E.hk;
    if (ron && E.hostSince && now - *E.hostSince >= F("emwait", 20) && E.chkK != E.hk && mineKnown) {
        E.chkK = E.hk;
        int wx = env_.targetWeather();
        log(fmt("NOLA emission: host em emissao ha %s s; a minha %s (alvo do clima %s)", ns(F("emwait", 20)).c_str(),
            mine ? "ativa" : (E.seenK == E.hk ? "inativa agora, ativa antes" : "inativa"), wx >= 0 ? std::to_string(wx).c_str() : "nil"));
        if (F("emstart", 0) == 2 && !mine && E.seenK != E.hk && !host) {
            bool ok = env_.cmdEm("XStartEmission");
            log("NOLA emission: a minha nao comecou com o clima -> XStartEmission (uma vez nesta emissao do host) ok=" + tb(ok) + " " + (ok ? "" : "false"));
        }
    }
    if (ron != E.ron) log(std::string("NOLA emission: host ") + (ron ? "em emissao" : "sem emissao") + "; a minha " + (mine ? "ativa" : "inativa"));
    if (mineKnown) {
        if (ron) {
            E.offAt.reset();
            E.stops = 0;
            if (F("emstart", 0) == 1 && !mine && E.startedN != n) {
                E.startedN = n;
                bool ok = env_.cmdEm("XStartEmission");
                log("NOLA emission: host em emissao #" + std::to_string(n) + " -> XStartEmission (uma vez) ok=" + tb(ok) + " " + (ok ? "" : "false"));
            }
        } else if (mine) {
            if (!E.offAt) E.offAt = now;
            if (now - *E.offAt >= F("emgrace", 10) && now >= E.stopAt && E.stops < 3) {
                E.stops++;
                E.stopAt = now + F("emstopgap", 60);
                int rwx = env_.remoteWx();
                std::string okW = "-";
                if (rwx >= 0 && rwx <= 9 && rwx != 7) okW = tb(env_.cmdWeather(false, rwx));
                bool ok = env_.cmdEm("XStopEmission");
                log(fmt("NOLA emission: o host nao esta em emissao e a minha esta ativa ha %.0f s -> clima do host %s ok=%s, XStopEmission ok=%s (parada %d de 3)", now - *E.offAt,
                    rwx >= 0 ? std::to_string(rwx).c_str() : "nil", okW.c_str(), tb(ok).c_str(), E.stops));
            }
        } else { E.offAt.reset(); E.stops = 0; }
    }
    E.ron = ron;
}

void Session::noPause() {
    if (F("nopause", 0) != 1 || !env_.coop()) return;
    Np& N = np_;
    double now = env_.clock();
    uintptr_t addr;
    if (!env_.anyPawn(addr)) { N.addr.reset(); return; }
    if (!N.addr || *N.addr != addr) { N.addr = addr; N.at = now; }
    if (now - N.at < F("npwait", 3) || now < N.holdUntil) return;
    bool paused;
    if (!env_.gamePaused(paused) || !paused) return;
    if (env_.nodeathDead()) return;
    bool alive = true;
    double hp = 1;
    env_.pawnAlive(alive);
    env_.pawnHp(hp);
    if (!alive || hp <= 0) return;
    if (F("npcine", 1) != 1 && env_.inCinematic()) return;
    std::vector<double> h;
    for (double x : N.hits) if (now - x < 5) h.push_back(x);
    h.push_back(now);
    N.hits = h;
    if ((int)h.size() > (int)F("npmax", 10)) {
        N.holdUntil = now + 30;
        N.hits.clear();
        log("NOLA nopause: o jogo pausou " + std::to_string(h.size()) + " vezes em 5 s -> nao despauso por 30 s");
        return;
    }
    bool ret = false;
    bool ok = env_.setGamePaused(false, ret);
    bool still = true;
    env_.gamePaused(still);
    N.n++;
    if (now - N.logAt >= 5) {
        log(fmt("NOLA nopause: jogo pausado -> despausado #%d ok=%s ret=%s ainda_pausado=%s (+%d sem log)", N.n, tb(ok).c_str(), tb(ret).c_str(), tb(still).c_str(), N.sup));
        N.logAt = now;
        N.sup = 0;
    } else N.sup++;
}

void Session::slTick() {
    if (env_.host()) return;
    uintptr_t a;
    if (!env_.anyPawn(a)) return;
    bool slp = env_.pawnSleeping();
    
    if (slp && !cslp_) log("NOLA sono: o cliente dormiu; isso so pula o relogio LOCAL, que volta ao do host (synctime)");
    cslp_ = slp;
}

std::optional<Clock> Session::slClock() {
    Clock c;
    if (!env_.readClock(c)) return std::nullopt;
    c.gt = lmod(c.gs / 150 + 9, 24) * 3600;
    return c;
}

Stats Session::slStats() {
    Stats s;
    double v;
    if (env_.getSleepiness(v)) s.sl = v;
    if (env_.pawnHp(v)) s.hp = v;
    if (env_.getHunger(v)) s.hu = v;
    return s;
}

bool Session::slSetTod(std::optional<double> gt, std::optional<double> wt, const std::string& why) {
    auto parts = [&](double t, bool w) {
        double v = std::floor(lmod(t, 86400));
        return setGameTime((int)std::floor(v / 3600), (int)std::floor(lmod(v, 3600) / 60), (int)lmod(v, 60), why, w);
    };
    bool ok = true;
    if (gt) ok = parts(*gt, false) && ok;
    if (wt) ok = parts(*wt, true) && ok;
    return ok;
}

bool Session::slSetChk(std::optional<double> gt, std::optional<double> wt, const std::string& why, std::optional<Clock> c0, std::string& msg, std::optional<Clock>& r0, std::optional<Clock>& r1) {
    if (!c0) c0 = slClock();
    bool okS = slSetTod(gt, wt, why);
    auto c1 = slClock();
    auto off = [](std::optional<double> a, std::optional<double> b) -> std::optional<double> {
        if (!a || !b) return std::nullopt;
        double d = lmod(*a - *b, 86400);
        return d > 43200 ? d - 86400 : d;
    };
    auto dg = off(c1 ? std::optional<double>(c1->gt) : std::nullopt, gt);
    auto dw = off(c1 ? c1->wt : std::nullopt, wt);
    std::vector<std::string> bad;
    if (!okS) bad.push_back("S2_setGameTime falhou");
    if (gt && !(dg && std::fabs(*dg) <= 300)) bad.push_back("jogo " + (dg ? fmt("%+.0f s", *dg) : std::string("sem leitura")) + " do alvo");
    if (wt && !(dw && std::fabs(*dw) <= 300)) bad.push_back("clima " + (dw ? fmt("%+.0f s", *dw) : std::string("sem leitura")) + " do alvo");
    std::string s = fmt("jogo %s -> %s (alvo %s), clima %s -> %s (alvo %s), gs %+.0f", hm(c0 ? std::optional<double>(c0->gt) : std::nullopt).c_str(),
        hm(c1 ? std::optional<double>(c1->gt) : std::nullopt).c_str(), hm(gt).c_str(), hm(c0 ? c0->wt : std::nullopt).c_str(), hm(c1 ? c1->wt : std::nullopt).c_str(),
        hm(wt).c_str(), (c0 && c1) ? c1->gs - c0->gs : 0.0);
    if (!bad.empty()) {
        std::string j;
        for (size_t i = 0; i < bad.size(); i++) j += (i ? ", " : "") + bad[i];
        s += "; AVISO: o relogio NAO chegou ao alvo (" + j + ")";
    }
    msg = s;
    r0 = c0;
    r1 = c1;
    return bad.empty();
}

bool Session::slSkip(double h, const std::string& why, const Pre& pre, std::string& msg) {
    auto c0o = slClock();
    if (!c0o) { msg = "sem TimeManager"; return false; }
    Clock c0 = *c0o;
    Clock b = (pre.valid && env_.clock() - pre.at < 120) ? pre.c : c0;
    double add = h * 3600;
    std::optional<double> bw = b.wt ? b.wt : c0.wt;
    double tg = b.gt + add;
    std::optional<double> tw = bw ? std::optional<double>(*bw + add) : std::nullopt;
    std::string s;
    std::optional<Clock> r0, r1;
    bool ok = slSetChk(tg, tw, why, c0, s, r0, r1);
    if (r1 && r1->gs < c0.gs - 150) {
        bool okK = env_.cmdSkipHours(1);
        auto c2 = slClock();
        double fwd = c2 ? c2->gs - c0.gs : 0;
        double dd = c2 ? std::fabs(lmod(c2->gt - r1->gt + 43200, 86400) - 43200) : 99999;
        bool good = fwd > 0 && dd <= 300;
        ok = ok && good;
        s += fmt("; gs voltou %+.0f -> XSkipTimeHours(1) ok=%s: jogo %s, clima %s, gs total %+.0f%s", r1->gs - c0.gs, tb(okK).c_str(),
            hm(c2 ? std::optional<double>(c2->gt) : std::nullopt).c_str(), hm(c2 ? c2->wt : std::nullopt).c_str(), fwd,
            good ? " (+24 h, hora do dia igual)" : " AVISO: gs ainda atras ou a hora do dia mudou");
    }
    msg = fmt("S2_setGameTime: antes do sono %s + %.2f h; %s", hmc(b.gt).c_str(), h, s.c_str());
    return ok;
}

void Session::slUndo(const Pre& pre, double now, const std::string& why) {
    if (!pre.valid) { log("NOLA dormir: CANCELADO (" + why + ") mas sem a hora de antes do sono; nada desfeito"); return; }
    double back = (now - pre.at) * F("sleeprate", 24);
    double gt = pre.c.gt + back;
    std::optional<double> wt = pre.c.wt ? std::optional<double>(*pre.c.wt + back) : std::nullopt;
    std::string how;
    std::optional<Clock> c0, c1;
    bool ok = slSetChk(gt, wt, "dormir cancelado", std::nullopt, how, c0, c1);
    double s0v, s1v;
    bool h0 = env_.getSleepiness(s0v);
    std::string okS = "-";
    if (pre.st.sl) okS = tb(env_.setSleepiness(*pre.st.sl));
    bool h1 = env_.getSleepiness(s1v);
    double dgs = (c0 && c1) ? c1->gs - c0->gs : 0;
    log(fmt("NOLA dormir: CANCELADO (%s) -> relogio de volta ok=%s (%s)%s; sonolencia %s -> %s (antes do sono %s) ok=%s; vida/fome do sono ficam", why.c_str(), tb(ok).c_str(), how.c_str(),
        dgs > 300 ? " (AVISO: o dia andou para frente)" : "", h0 ? ns(s0v).c_str() : "nil", h1 ? ns(s1v).c_str() : "nil", os(pre.st.sl).c_str(), okS.c_str()));
}

void Session::slBeds(char ob, double now) {
    Sl& S = sl_;
    if (!ob && !S.bedOff) return;
    if (ob && S.bedOff == ob && now < S.bedAt) return;
    uintptr_t a;
    if (env_.ioPawn(a) && env_.pawnSleeping()) return;
    int n = 0, okN = 0;
    std::vector<Bed> beds;
    env_.beds(beds);
    for (auto& b : beds) {
        if (b.key.empty() || b.key.find("Default__") != std::string::npos) continue;
        int cur = b.cur;
        if (ob) {
            if (!S.bedSv.count(b.key) && cur >= 0 && cur != 0) S.bedSv[b.key] = cur;
            if (S.bedSv.count(b.key)) {
                n++;
                if (cur == 0 || env_.bedSet(b.key, 0)) okN++;
            }
        } else if (S.bedSv.count(b.key)) {
            n++;
            if (env_.bedSet(b.key, S.bedSv[b.key])) okN++;
        }
    }
    if (ob && S.bedOff != ob) log(fmt("NOLA dormir: cama bloqueada: o outro esta %s; %d/%d camas sem colisao", whyText(ob).c_str(), okN, n));
    else if (!ob) { log(fmt("NOLA dormir: cama liberada (o outro esta livre); %d/%d camas com a colisao de antes", okN, n)); S.bedSv.clear(); }
    S.bedOff = ob;
    S.bedAt = now + 3;
}

void Session::slPark(std::optional<double> untilT) {
    if (env_.host()) return;
    Sl& S = sl_;
    if (untilT) {
        syncAtGt_ = std::max(syncAtGt_, *untilT);
        syncAtWt_ = std::max(syncAtWt_, *untilT);
        if (!S.park) log(fmt("NOLA dormir: synctime parado (%.0f s no maximo)", *untilT - env_.clock()));
        S.park = syncAtGt_;
    } else if (S.park) {
        if (syncAtGt_ == *S.park) syncAtGt_ = 0;
        if (syncAtWt_ == *S.park) syncAtWt_ = 0;
        S.park.reset();
        log("NOLA dormir: synctime de volta (o relogio segue o host)");
    }
}

char Session::slBusy(double now) {
    bool alive = true;
    double hp = 0;
    env_.pawnAlive(alive);
    bool hasHp = env_.pawnHp(hp);
    if (!alive || (hasHp && hp <= 0) || env_.nodeathDead() || env_.lifeDown()) return 'X';
    std::string lk = env_.ftLock();
    if (lk == "dialogo" || lk == "cinematica") return 'B';
    if (now - sl_.hurtAt < F("sleepfight", 10)) return 'F';
    return 0;
}

void Session::slBed(double now) {
    Sl& S = sl_;
    char b = slBusy(now);
    S.busyTx = b ? std::string(1, b) : "-";
    std::string ob;
    char o = 0;
    if (env_.x1part("SB", ob) && !ob.empty() && (ob[0] == 'B' || ob[0] == 'F' || ob[0] == 'X')) o = ob[0];
    for (int v = 1, nv = slNv(); v < nv && !o; ++v)
        if (env_.viewState(v) == 1 && env_.x1partView(v, "SB", ob) && !ob.empty() && (ob[0] == 'B' || ob[0] == 'F' || ob[0] == 'X')) o = ob[0];
    S.obusy = o;
    if ((S.mine && !S.mine->endAt) || (S.fol && S.fol->phase != "done" && S.fol->phase != "no")) return;
    slBeds(o, now);
}

void Session::slAnswer(const Fol& Fo, char a, double now) {
    Ans an;
    an.seq = Fo.seq;
    an.ts = Fo.ts;
    an.a = a;
    an.untilT = now + F("sleepkeep", 30);
    an.pid = Fo.pid;
    sl_.ans = an;
}

bool Session::slPull(int seq, int view) {
    if (F("sleeppull", 0) != 1) return false;
    RemotePos r;
    double qx, qy, qz;
    bool hasR = view == 0 ? env_.remotePos(r) : env_.remotePosView(view, r);
    bool hasQ = env_.localPos(qx, qy, qz);
    if (!hasR || !hasQ || env_.clock() - r.recv > 5) {
        log("NOLA dormir: puxar para perto de quem dorme (#SL " + std::to_string(seq) + "): sem posicao recente do outro ou sem pawn -> fico onde estou");
        return false;
    }
    double d = std::sqrt((qx - r.x) * (qx - r.x) + (qy - r.y) * (qy - r.y) + (qz - r.z) * (qz - r.z));
    if (d <= F("sleeppullr", 300)) {
        log(fmt("NOLA dormir: ja perto de quem dorme (#SL %d, %.1f m) -> sem puxar", seq, d / 100));
        return false;
    }
    bool ok = env_.tpfNow(r.x, r.y, r.z, r.yaw, "dormir");
    double ax, ay, az;
    bool has = env_.localPos(ax, ay, az);
    log(fmt("NOLA dormir: puxado para perto de quem dorme (#SL %d, estava a %.0f m) ok=%s dz=%s dxy=%s", seq, d / 100, tb(ok).c_str(), has ? fmt("%.0f", az - r.z).c_str() : "?",
        has ? fmt("%.0f", std::sqrt((ax - r.x) * (ax - r.x) + (ay - r.y) * (ay - r.y))).c_str() : "?"));
    return ok;
}

void Session::slAsk(double now, const std::string& key, int seq, long long ts, std::optional<double> h, int view, int pid) {
    Sl& S = sl_;
    if (S.fol && S.fol->phase != "done" && S.fol->phase != "no")
        log("NOLA dormir: pedido novo (#SL " + std::to_string(seq) + ") com o anterior (#SL " + std::to_string(S.fol->seq) + ") aberto; o anterior fica para tras");
    Fol Fo;
    Fo.key = key; Fo.seq = seq; Fo.ts = ts; Fo.t0 = now; Fo.pre = S.pre; Fo.h = h; Fo.view = view; Fo.pid = pid;
    S.fol = Fo;
    slPark(now + F("sleepans", 15) + F("sleepwait", 60) + 180);
    Fol& f = *S.fol;
    if (S.mine && !S.mine->endAt) {
        f.phase = "tie";
        f.pre = S.mine->pre;
        slAnswer(f, 'Y', now);
        log(fmt("NOLA dormir: os dois foram dormir ao mesmo tempo (#SL meu %d / do outro %d): os dois concordaram", S.mine->seq, seq));
        return;
    }
    char why = slBusy(now);
    if (why) {
        f.phase = "no";
        slAnswer(f, why, now);
        log(fmt("NOLA dormir: o outro jogador foi dormir (#SL %d) mas NAO posso: %s -> recusado", seq, whyText(why).c_str()));
        return;
    }
    slPull(seq, view);
    if ((int)F("sleepmode", 1) == 2) {
        f.phase = "wait";
        slAnswer(f, '-', now);
        log(fmt("NOLA dormir: o outro jogador foi dormir (#SL %d) -> deite numa cama em %d s para dormir junto (modo 2)", seq, (int)F("sleepwait", 60)));
        return;
    }
    f.phase = "start";
    slAnswer(f, 'Y', now);
    bool ok = env_.cmdStartSleep();
    log(fmt("NOLA dormir: o outro jogador foi dormir (#SL %d) -> durmo junto (modo 1): XStartSleep ok=%s", seq, tb(ok).c_str()));
}

int Session::slNv() {
    if (F("mcsleep", 1) != 1) return 1;
    int n = env_.views();
    return n < 1 ? 1 : (n > 4 ? 4 : n);
}

void Session::slMineAns(int v, char ans, double now) {
    Mine& Mi = *sl_.mine;
    Mi.va[v] = ans;
    int pid = env_.viewPid(v);
    if (ans != 'Y' && ans != 'D') {
        if (Mi.no.empty()) {
            Mi.no = whyText(ans) + fmt(" (pid %d)", pid);
            log(fmt("NOLA dormir: o jogador pid %d RECUSOU (#SL %d): %s; todos precisam aceitar", pid, Mi.seq, whyText(ans).c_str()));
        }
        return;
    }
    log(fmt("NOLA dormir: o jogador pid %d %s (#SL %d) %.1f s depois do pedido", pid, ans == 'D' ? "acordou" : "ACEITOU", Mi.seq, now - Mi.t0));
    int up = 0, yes = 0, dn = 0;
    for (int u = 0; u < Mi.nv; ++u) {
        if (env_.viewState(u) != 1) continue;
        ++up;
        auto it = Mi.va.find(u);
        if (it == Mi.va.end()) continue;
        if (it->second == 'Y' || it->second == 'D') ++yes;
        if (it->second == 'D') ++dn;
    }
    if (!Mi.ok && up > 0 && yes == up) { Mi.ok = now; log(fmt("NOLA dormir: TODOS aceitaram (#SL %d, %d jogadores) %.1f s depois do pedido", Mi.seq, up, now - Mi.t0)); }
    if (!Mi.done && up > 0 && dn == up) { Mi.done = now; log(fmt("NOLA dormir: todos acordaram (#SL %d)", Mi.seq)); }
}

void Session::slIn(double now) {
    int nv = slNv();
    for (int v = 0; v < nv; ++v) {
        std::string sg;
        if (!(v == 0 ? env_.x1part("SL", sg) : env_.x1partView(v, "SL", sg))) continue;
        slInV(now, v, sg);
    }
}

void Session::slInV(double now, int v, const std::string& sg) {
    Sl& S = sl_;
    int nv = slNv();
    auto f = split(sg, ',');
    if (f.size() < 7) return;
    if (!isDigits(f[0]) || f[1].size() != 1 || !(isalpha((unsigned char)f[1][0]) || f[1][0] == '-') || !isNumTok(f[2]) || !isDigits(f[3]) || !isDigits(f[4]) || !isDigits(f[5]) || f[6].empty()) return;
    char ans = f[6][0];
    if (!(isalpha((unsigned char)ans) || ans == '-')) return;
    std::string rseq = f[0];
    char rst = f[1][0];
    std::string rh = f[2], rts = f[3];
    int aseq = atoi(f[4].c_str());
    long long ats = atoll(f[5].c_str());
    bool forMe = nv == 1 || f.size() < 8 || atoi(f[7].c_str()) == env_.myPid();
    if (nv > 1 && forMe && S.mine && !S.mine->endAt && S.mine->nv > 1 && aseq == S.mine->seq && ats == S.mine->ts && ans != '-') {
        auto it = S.mine->va.find(v);
        if (it == S.mine->va.end() || it->second != ans) slMineAns(v, ans, now);
    } else if (forMe && S.mine && !S.mine->endAt && aseq == S.mine->seq && ats == S.mine->ts && ans != '-' && ans != S.mine->ra) {
        Mine& Mi = *S.mine;
        Mi.ra = ans;
        if (ans == 'Y' || ans == 'D') {
            if (!Mi.ok) { Mi.ok = now; log(fmt("NOLA dormir: o outro ACEITOU (#SL %d) %.1f s depois do pedido", Mi.seq, now - Mi.t0)); }
            if (ans == 'D') { Mi.done = now; log(fmt("NOLA dormir: o outro acordou (#SL %d)", Mi.seq)); }
        } else {
            Mi.no = whyText(ans);
            log(fmt("NOLA dormir: o outro RECUSOU (#SL %d): %s", Mi.seq, Mi.no.c_str()));
        }
    }
    if (rseq == "0" || rst == '-') return;
    std::string key = (nv > 1 ? std::to_string(env_.viewPid(v)) + "/" : std::string()) + rseq + "@" + rts;
    if (S.fol && S.fol->key == key) {
        Fol& Fo = *S.fol;
        double hv = 0;
        bool hasH = toNum(rh, hv);
        if (rst == 'W' && hasH && !Fo.h) Fo.h = hv;
        if (rst == 'C' && !Fo.cancel) { Fo.cancel = now; log("NOLA dormir: o outro desistiu do sono (#SL " + rseq + ")"); }
        if (rst == 'A' && !Fo.closed) { Fo.closed = now; log(fmt("NOLA dormir: o outro confirmou: DORMIMOS JUNTOS (#SL %s, ele dormiu %.2f h)", rseq.c_str(), hasH ? hv : 0.0)); }
        return;
    }
    std::string& rk = v == 0 ? S.rkey : S.rkeyV[v & 3];
    if (key == rk) return;
    rk = key;
    if (rst != 'S' && rst != 'W') return;
    long long age = env_.unixTime() - atoll(rts.c_str());
    if (age > F("sleepmaxage", 120)) { log("NOLA dormir: #SL " + rseq + " antigo (" + std::to_string(age) + " s), ignorado"); return; }
    double hv = 0;
    std::optional<double> hh;
    if (rst == 'W' && toNum(rh, hv)) hh = hv;
    slAsk(now, key, atoi(rseq.c_str()), atoll(rts.c_str()), hh, v, nv > 1 ? env_.viewPid(v) : -1);
}

void Session::sleepStep() {
    Sl& S = sl_;
    if (F("sleepsync", 0) != 1 || !env_.coop()) {
        if (S.active && (S.park || S.mine || S.fol)) {
            slPark(std::nullopt);
            S.mine.reset();
            S.fol.reset();
            log("NOLA dormir: sleepsync desligado; estado limpo");
        }
        if (S.active && S.bedOff) slBeds(0, env_.clock());
        return;
    }
    S.active = true;
    double now = env_.clock();
    double keep = F("sleepkeep", 30);
    uintptr_t addr;
    if (!env_.ioPawn(addr)) { S.wasSlp.reset(); return; }
    if (!S.addr || *S.addr != addr) { S.addr = addr; S.wasSlp.reset(); }
    bool slp = env_.pawnSleeping();
    std::optional<bool> prev = S.wasSlp;
    S.wasSlp = slp;
    if (!prev) return;
    bool rise = slp && !*prev, fall = *prev && !slp;
    if (!rise && !fall && now < S.nextAt) return;
    S.nextAt = now + 0.5;
    if (!slp && !fall) {
        auto c = slClock();
        Stats st = slStats();
        if (st.hp && S.lastHp && *st.hp < *S.lastHp - 0.5) S.hurtAt = now;
        S.lastHp = st.hp;
        if (c) {
            if (S.pre.valid && c->gs - S.pre.c.gs > (now - S.pre.at) + 300) S.jumpAt = now;
            S.prePrev = S.pre;
            S.pre.valid = true;
            S.pre.c = *c;
            S.pre.st = st;
            S.pre.at = now;
        }
        slBed(now);
    }
    if (rise) {
        Fol* Fo = S.fol ? &*S.fol : nullptr;
        if (Fo && (Fo->phase == "start" || Fo->phase == "wait" || Fo->phase == "virt")) {
            if (Fo->virt) log(fmt("NOLA dormir: o sono do jogo comecou DEPOIS do efeito por script (#SL %d); o relogio pode pular duas vezes", Fo->seq));
            Fo->phase = "sleep";
            Fo->nat = now;
            slAnswer(*Fo, 'Y', now);
            log(fmt("NOLA dormir: durmo junto (#SL %d): o sono do jogo comecou %.1f s depois do pedido", Fo->seq, now - Fo->t0));
        } else if (Fo && (Fo->phase == "tie" || Fo->phase == "sleep")) {
        } else if (now < S.coolUntil) {
            log("NOLA dormir: dormi de novo logo depois de um sono junto; nao conta como pedido");
        } else {
            Pre pre = S.pre;
            if (S.prePrev.valid && now - S.jumpAt < 2) pre = S.prePrev;
            S.seq++;
            double lim = F("sleepans", 15) + (((int)F("sleepmode", 1) == 2) ? F("sleepwait", 60) : 0);
            Mine Mi;
            Mi.seq = S.seq; Mi.ts = env_.unixTime(); Mi.st = 'S'; Mi.h = 0; Mi.t0 = now; Mi.pre = pre; Mi.lim = lim; Mi.untilT = now + keep; Mi.nv = slNv();
            S.mine = Mi;
            slPark(now + lim + F("sleepwait", 60) + 120);
            log(fmt("NOLA dormir: DORMI (pedido #SL %d enviado; hora %s, sonolencia %s); o outro tem %d s", S.seq, pre.valid ? hmc(pre.c.gt).c_str() : "?",
                pre.valid ? os(pre.st.sl).c_str() : "nil", (int)lim));
        }
    }
    if (fall) {
        if (S.mine && !S.mine->endAt && !S.mine->woke) {
            Mine& Mi = *S.mine;
            Mi.woke = now;
            auto c = slClock();
            if (c && Mi.pre.valid) Mi.h = std::max(0.0, (c->gs - Mi.pre.c.gs) / 150);
            Mi.st = 'W';
            log(fmt("NOLA dormir: acordei (#SL %d): dormi %.2f h de jogo em %.0f s (jogo %s -> %s, clima %s -> %s)", Mi.seq, Mi.h, now - Mi.t0,
                Mi.pre.valid ? hmc(Mi.pre.c.gt).c_str() : "?", c ? hmc(c->gt).c_str() : "?", Mi.pre.valid ? hm(Mi.pre.c.wt).c_str() : "?", c ? hm(c->wt).c_str() : "?"));
        }
        if (S.fol && S.fol->phase == "sleep") {
            Fol& Fo = *S.fol;
            Fo.phase = "done";
            Fo.doneAt = now;
            S.coolUntil = now + F("sleepcool", 30);
            slAnswer(Fo, 'D', now);
            auto c = slClock();
            log(fmt("NOLA dormir: acordei junto (#SL %d) depois de %.0f s (jogo %s) -> D enviado", Fo.seq, now - (Fo.nat ? *Fo.nat : now), c ? hmc(c->gt).c_str() : "?"));
        }
    }
    slIn(now);
    if (S.fol) {
        Fol& Fo = *S.fol;
        if (Fo.phase != "done" && S.ans && S.ans->seq == Fo.seq) S.ans->untilT = now + keep;
        double cap = F("sleepans", 15) + F("sleepwait", 60) + 180;
        if (Fo.phase == "tie" && !slp) {
            Fo.phase = "done";
            Fo.doneAt = now;
            if (!Fo.nat) Fo.nat = Fo.t0;
            S.coolUntil = now + F("sleepcool", 30);
            slAnswer(Fo, 'D', now);
        } else if (Fo.phase == "wait" && now - Fo.t0 > F("sleepwait", 60)) {
            Fo.phase = "no";
            slAnswer(Fo, 'T', now);
            log(fmt("NOLA dormir: nao deitei numa cama em %d s (#SL %d) -> recusado", (int)F("sleepwait", 60), Fo.seq));
        } else if (Fo.phase == "start" && now - Fo.t0 > F("sleepnat", 4)) {
            double s0, s1;
            bool h0 = env_.getSleepiness(s0);
            bool ok = env_.setSleepiness(F("sleepvirt", 0));
            bool h1 = env_.getSleepiness(s1);
            Fo.virt = now;
            Fo.phase = "virt";
            log(fmt("NOLA dormir: o sono do jogo nao comecou em %s s -> efeito por script (#SL %d): sonolencia %s -> %s ok=%s", ns(F("sleepnat", 4)).c_str(), Fo.seq,
                h0 ? ns(s0).c_str() : "nil", h1 ? ns(s1).c_str() : "nil", tb(ok).c_str()));
        }
        if (Fo.phase == "virt") {
            if (!env_.host()) {
                Fo.phase = "done";
                Fo.doneAt = now;
                S.coolUntil = now + F("sleepcool", 30);
                slAnswer(Fo, 'D', now);
                log(fmt("NOLA dormir: dormi junto por script (#SL %d); o relogio segue o do host -> D enviado", Fo.seq));
            } else if (Fo.h) {
                bool ok = true;
                std::string how = "o relogio do cliente nao andou: nada a pular";
                if (*Fo.h > 0.01) ok = slSkip(*Fo.h, "dormir junto: host pula as horas do cliente", Fo.pre, how);
                Fo.skip = ok;
                Fo.phase = "done";
                Fo.doneAt = now;
                S.coolUntil = now + F("sleepcool", 30);
                slAnswer(Fo, 'D', now);
                log(fmt("NOLA dormir: host dormiu junto por script (#SL %d): pulei %.2f h como o cliente ok=%s (%s) -> D enviado", Fo.seq, *Fo.h, tb(ok).c_str(), how.c_str()));
            } else if (now - Fo.t0 > cap) {
                Fo.phase = "no";
                slAnswer(Fo, 'O', now);
                log(fmt("NOLA dormir: as horas do cliente nao chegaram (#SL %d) -> desisto", Fo.seq));
            }
        }
        if (Fo.cancel && !Fo.undone && !slp) {
            Fo.undone = now;
            if (Fo.nat || Fo.virt) slUndo(Fo.pre, now, "o outro desistiu (#SL " + std::to_string(Fo.seq) + ")");
            if (Fo.phase != "done") Fo.phase = "no";
        }
        if (Fo.closed || Fo.undone || now - Fo.t0 > cap) {
            if (!(Fo.closed || Fo.undone)) log(fmt("NOLA dormir: o pedido #SL %d nao fechou em %.0f s; esqueco", Fo.seq, cap));
            if (Fo.phase == "done" || Fo.phase == "no" || now - Fo.t0 > cap) S.fol.reset();
        }
    }
    if (S.mine && !S.mine->endAt) {
        Mine& Mi = *S.mine;
        Mi.untilT = now + keep;
        if (!Mi.ok && Mi.no.empty() && now - Mi.t0 > Mi.lim) {
            Mi.no = fmt("sem resposta em %d s", (int)Mi.lim);
            if (Mi.nv > 1) {
                std::string miss;
                for (int u = 0; u < Mi.nv; ++u)
                    if (env_.viewState(u) == 1 && !Mi.va.count(u)) miss += (miss.empty() ? "" : ",") + std::to_string(env_.viewPid(u));
                log(fmt("NOLA dormir: nem todos responderam (#SL %d) em %d s: falta pid %s", Mi.seq, (int)Mi.lim, miss.empty() ? "-" : miss.c_str()));
            } else
            log(fmt("NOLA dormir: o outro NAO respondeu (#SL %d) em %d s", Mi.seq, (int)Mi.lim));
        }
        if (!Mi.no.empty()) {
            if (!slp) {
                slUndo(Mi.pre, now, "#SL " + std::to_string(Mi.seq) + ": " + Mi.no);
                Mi.st = 'C';
                Mi.endAt = now;
                S.coolUntil = now + F("sleepcool", 30);
            }
        } else if (Mi.ok && Mi.done && Mi.woke) {
            Mi.st = 'A';
            Mi.endAt = now;
            S.coolUntil = now + F("sleepcool", 30);
            auto c = slClock();
            log(fmt("NOLA dormir: DORMIMOS JUNTOS (#SL %d): eu dormi %.2f h, o outro acordou %.0f s depois de mim; hora aqui %s (o host manda no relogio)", Mi.seq, Mi.h,
                *Mi.done - *Mi.woke, c ? hmc(c->gt).c_str() : "?"));
        } else if (Mi.ok && now - Mi.t0 > Mi.lim + F("sleepwait", 60) + 120 && !slp) {
            Mi.st = 'A';
            Mi.endAt = now;
            log(fmt("NOLA dormir: o outro aceitou mas nao avisou que acordou (#SL %d); fecho assim", Mi.seq));
        }
    }
    if (S.park && !((S.mine && !S.mine->endAt) || (S.fol && S.fol->phase != "done"))) slPark(std::nullopt);
}

std::string Session::outTail() {
    std::string r;
    Sl& S = sl_;
    if (F("sleepsync", 0) == 1 && S.active) {
        double now = env_.clock();
        const Mine* m = (S.mine && now < S.mine->untilT) ? &*S.mine : nullptr;
        const Ans* a = (S.ans && now < S.ans->untilT) ? &*S.ans : nullptr;
        std::string sb = S.busyTx.empty() ? "" : "#SB:" + S.busyTx;
        if (!m && !a) r += sb;
        else r += fmt("#SL:%d,%c,%.2f,%lld,%d,%lld,%c", m ? m->seq : 0, m ? m->st : '-', m ? m->h : 0.0, m ? m->ts : 0LL, a ? a->seq : 0, a ? a->ts : 0LL, a ? a->a : '-') +
                  (a && a->pid >= 0 ? "," + std::to_string(a->pid) : std::string()) + sb;
    }
    if (F("hostcfg", 0) == 1) {
        std::string md = env_.buildMd5().empty() ? "-" : env_.buildMd5();
        if (env_.host()) {
            double now = env_.clock();
            if (!hc_.hasOut || now - hc_.outAt >= 1) {
                std::string t;
                std::string keys = kHcKeys;
                size_t i = 0;
                while (i < keys.size()) {
                    size_t j = keys.find(' ', i);
                    if (j == std::string::npos) j = keys.size();
                    std::string k = keys.substr(i, j - i);
                    i = j + 1;
                    if (k.empty() || env_.ftype(k.c_str()) == 0) continue;
                    if (!t.empty()) t += ";";
                    t += k + "=" + cleanNum(env_.fget(k.c_str()).str());
                }
                hc_.outS = t;
                hc_.outAt = now;
                hc_.hasOut = true;
            }
            r += "#HC:" + md + ";" + hc_.outS;
        } else r += "#HC:" + md;
    }
    return r;
}

void Session::hcGiveBack(const std::string& why, const std::string* only) {
    std::vector<std::string> ch;
    for (auto it = hc_.ad.begin(); it != hc_.ad.end();) {
        if (!only || *only == it->first) {
            auto ov = hc_.own.find(it->first);
            if (ov != hc_.own.end()) env_.fset(it->first.c_str(), ov->second);
            ch.push_back(it->first + " " + it->second.str() + " -> " + (ov != hc_.own.end() ? ov->second.str() : "nil"));
            if (ov != hc_.own.end()) hc_.own.erase(ov);
            it = hc_.ad.erase(it);
        } else ++it;
    }
    std::sort(ch.begin(), ch.end());
    if (!ch.empty()) {
        std::string j;
        for (size_t i = 0; i < ch.size(); i++) j += (i ? ", " : "") + ch[i];
        log("NOLA hostcfg: de volta ao valor daqui (" + why + "): " + j);
    }
}

void Session::hcAdopt(const std::string& rest) {
    std::map<std::string, bool> set, seen;
    {
        std::string keys = kHcKeys;
        size_t i = 0;
        while (i < keys.size()) {
            size_t j = keys.find(' ', i);
            if (j == std::string::npos) j = keys.size();
            set[keys.substr(i, j - i)] = true;
            i = j + 1;
        }
    }
    int n = 0;
    for (auto& kv : split(rest, ';')) {
        size_t e = kv.find('=');
        if (e == std::string::npos || e == 0) continue;
        std::string k = kv.substr(0, e), v = kv.substr(e + 1);
        bool okk = true;
        for (char c : k) if (!isFlagKeyChar(c)) okk = false;
        if (!okk) continue;
        int ty = env_.ftype(k.c_str());
        if (!set.count(k) || ty == 0 || seen.count(k)) continue;
        Val hv;
        if (ty == 1) {
            double d;
            if (!toNum(v, d)) continue;
            hv.t = 1; hv.n = d;
        } else { hv.t = 2; hv.s = v; }
        Val cur = env_.fget(k.c_str());
        seen[k] = true;
        n++;
        auto ad = hc_.ad.find(k);
        if (ad != hc_.ad.end() && hc_.own.count(k) && hv == hc_.own[k]) {
            env_.fset(k.c_str(), hv);
            hc_.ad.erase(k);
            hc_.own.erase(k);
            log("NOLA hostcfg: " + k + " = " + hv.str() + " (igual ao flags.txt daqui; antes " + cur.str() + ")");
        } else if (!(cur == hv)) {
            if (ad == hc_.ad.end()) hc_.own[k] = cur;
            env_.fset(k.c_str(), hv);
            hc_.ad[k] = hv;
            log("NOLA hostcfg: " + k + " " + cur.str() + " -> " + hv.str() + " (valor do host; flags.txt daqui = " + (hc_.own.count(k) ? hc_.own[k].str() : "nil") + ")");
        }
    }
    std::vector<std::string> gone;
    for (auto& kv : hc_.ad) if (!seen.count(kv.first)) gone.push_back(kv.first);
    for (auto& k : gone) hcGiveBack("o host nao manda mais " + k, &k);
    if (hc_.sumKey != peer_.id) {
        hc_.sumKey = peer_.id;
        log(fmt("NOLA hostcfg: %d flags compartilhadas recebidas do host (sessao %s); %d com o valor do host diferente de flags.txt daqui", n, peer_.id.c_str(), (int)hc_.ad.size()));
    }
}

void Session::onFlagChanged(const std::string& key, const Val& after) {
    auto it = hc_.ad.find(key);
    if (it == hc_.ad.end() || after == it->second) return;
    hc_.own[key] = after;
    env_.fset(key.c_str(), it->second);
}

void Session::hcTick() {
    Hc& H = hc_;
    if (F("hostcfg", 0) != 1) {
        if (!H.ad.empty()) hcGiveBack("hostcfg=0", nullptr);
        H.sumKey.clear();
        H.mdKey.clear();
        return;
    }
    double now = env_.clock();
    std::string sg;
    if (!env_.x1part("HC", sg)) {
        if (peer_.state == "up" && peer_.live != -1 && !peer_.loopback) {
            if (!H.noAt) H.noAt = now;
            if (now - *H.noAt >= 10 && H.noLog != peer_.id) {
                H.noLog = peer_.id;
                log("NOLA hostcfg: o outro lado nao manda #HC ha 10 s (hostcfg=0 la, ou core antigo): versao e flags NAO conferidas");
            }
        } else H.noAt.reset();
        return;
    }
    H.noAt.reset();
    if (peer_.loopback) return;
    std::string rmd, rest;
    size_t p = sg.find(';');
    if (p == std::string::npos) rmd = sg; else { rmd = sg.substr(0, p); rest = sg.substr(p + 1); }
    std::string md5 = env_.buildMd5();
    if (!md5.empty() && !rmd.empty() && rmd != "-") {
        std::string key = peer_.id + "/" + rmd + "/" + md5;
        if (H.mdKey != key) {
            H.mdKey = key;
            if (rmd != md5) {
                env_.screen("s2m_session_ver", tx(T::sess_version), 20);
                log("NOLA hostcfg AVISO: s2core.lua DIFERENTE do outro lado (aqui md5 " + md5 + " ver=" + env_.coreVer() + "; la (" + peer_.role + ") md5 " + rmd + " ver=" + peer_.core +
                    "): o codigo NAO e trocado, instale o mesmo s2core.lua nos dois");
            }
            else log("NOLA hostcfg: s2core.lua igual nos dois lados (md5 " + rmd + ")");
        }
    }
    if (env_.host()) return;
    hcAdopt(rest);
}

void Session::tick100() {
    auto guard = [&](const char* name, int& cnt, void (Session::*fn)()) {
        try {
            (this->*fn)();
        } catch (const std::exception& e) {
            errs_++;
            if (++cnt <= 5) log(std::string("NOLA ") + name + " erro: " + e.what());
        } catch (...) {
            errs_++;
            if (++cnt <= 5) log(std::string("NOLA ") + name + " erro");
        }
    };
    guard("noPause", errNp_, &Session::noPause);
    guard("peerTick", errTick_, &Session::peerTick);
    guard("viewsTick", errTick_, &Session::viewsTick);
    guard("wxTick", errTick_, &Session::wxTick);
    guard("slTick", errTick_, &Session::slTick);
    guard("emTick", errTick_, &Session::emTick);
    guard("sleepTick", errSleep_, &Session::sleepStep);
    guard("hostcfg", errHc_, &Session::hcTick);
}

}
