#include "s2m_save.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace s2m_save;

static std::string unesc(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            char c = s[++i];
            if (c == 'n') o += '\n';
            else if (c == 't') o += '\t';
            else if (c == 'r') o += '\r';
            else if (c == 's') o += ' ';
            else o += c;
        }
        else o += s[i];
    }
    return o;
}

static bool rawRead(const fs::path& p, std::string& out)
{
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}
static bool rawWrite(const fs::path& p, const std::string& d)
{
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    fs::path tmp = p;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(d.data(), (std::streamsize)d.size());
        if (!f) return false;
    }
    fs::remove(p, ec);
    fs::rename(tmp, p, ec);
    return !ec;
}

struct World : Env
{
    fs::path root;
    std::ostream* out = &std::cout;
    double t = 0;
    bool host = false;
    std::map<std::string, double> flags;
    std::map<std::string, std::string> flagTxt;
    Id pw = 0;
    std::map<std::string, int> inv;
    std::map<std::string, int> loaded;
    double cash = 0;
    V3 lpos, rpos;
    bool hasR = false;
    std::map<std::string, std::string> parts;
    int pst = 0, psess = 0;
    bool plive = true;
    std::optional<std::string> stamp = std::string("STEAM=1:1;NULL=-");
    bool savedir = true;
    bool saveOk = true, loadOk = true, createOk = true, pullOk = true;
    int kh = 0;
    int loadN = 0;
    std::string loadGuid = "none";
    std::optional<long long> gen;
    std::optional<std::set<std::string>> qtab;
    bool busy = false, frozen = false, rcine = false, down = false, emns = false;
    int held = 0;
    int mm = 1;
    std::string sxState = "none", sxId, sxItems, sxQs, sxHand = "-", sxW;
    double sxMoney = 0;
    std::string sxInSt = "none", sxInD = "-";
    bool sxInstOk = false;
    std::string sxInstG, sxInstCid;
    int sxInstKh = -1;
    bool sxSnapOk = true;
    std::string sxSnapId = "SX1", sxSnapHand = "-";
    int sxSnapN = 0;
    double sxSnapMoney = 0;
    bool sxSnapReused = false;
    std::vector<std::string> wants;
    bool wpHave = false;
    std::string wpFile, wpCid;
    int wpKh = -1, wpHs = -1;
    std::string wspSt = "none", wspD = "-";

    std::string mask(std::string s)
    {
        for (size_t i = 0; i < wants.size(); ++i)
        {
            const std::string& id = wants[i];
            if (id.empty()) continue;
            size_t p;
            while ((p = s.find(id)) != std::string::npos) s.replace(p, id.size(), "REQ" + std::to_string(i + 1));
        }
        return s;
    }
    void stub(const std::string& s) { *out << "STUB " << mask(s) << "\n"; }
    fs::path br(const std::string& n) { return root / "B" / n; }
    std::string savedRoot() { return (root / "saved").string() + "\\"; }

    double now() override { return t; }
    double flag(const char* key, double def) override
    {
        auto it = flags.find(key);
        return it == flags.end() ? def : it->second;
    }
    std::string flagText(const char* key, const char* def) override
    {
        auto it = flagTxt.find(key);
        return it == flagTxt.end() ? std::string(def) : it->second;
    }
    bool isHost() override { return host; }
    void log(const std::string& line) override { *out << "LOG " << mask(line) << "\n"; }
    bool readText(const std::string& name, std::string& o) override
    {
        std::string d;
        if (!rawRead(br(name), d)) return false;
        o.clear();
        for (size_t i = 0; i < d.size(); ++i)
            if (!(d[i] == '\r' && i + 1 < d.size() && d[i + 1] == '\n')) o += d[i];
        return true;
    }
    bool writeText(const std::string& name, const std::string& text) override
    {
        std::string d;
        for (char c : text) { if (c == '\n') d += '\r'; d += c; }
        return rawWrite(br(name), d);
    }
    bool writeBin(const std::string& name, const std::string& bytes) override { return rawWrite(br(name), bytes); }
    bool readAbs(const std::string& path, std::string& o, bool bin) override
    {
        std::string d;
        if (!rawRead(fs::path(path), d)) return false;
        if (bin) { o = d; return true; }
        o.clear();
        for (size_t i = 0; i < d.size(); ++i)
            if (!(d[i] == '\r' && i + 1 < d.size() && d[i + 1] == '\n')) o += d[i];
        return true;
    }
    bool writeAbs(const std::string& path, const std::string& bytes) override
    {
        if (!rawWrite(fs::path(path), bytes)) return false;
        std::error_code ec;
        auto n = fs::file_size(fs::path(path), ec);
        return !ec && n == bytes.size();
    }
    bool statTail(const std::string& path, long long& size, std::string& tail) override
    {
        std::string d;
        if (!rawRead(fs::path(path), d)) return false;
        size = (long long)d.size();
        tail = d.size() >= 16 ? d.substr(d.size() - 16) : std::string();
        return true;
    }
    bool saveInfo(std::string& dir, std::string& st, std::string& plat, std::string& newest) override
    {
        if (!savedir) return false;
        dir = savedRoot();
        st = stamp ? *stamp : std::string();
        plat = "STEAM";
        newest = "";
        return true;
    }
    std::string saveList() override
    {
        std::string o;
        std::error_code ec;
        fs::path d = root / "saved" / "STEAM" / "SaveGames" / "Data";
        std::vector<std::string> names;
        for (auto& e : fs::directory_iterator(d, ec))
            if (e.is_regular_file() && e.path().extension() == ".sav") names.push_back(e.path().stem().string());
        std::sort(names.begin(), names.end());
        for (auto& n : names) { if (!o.empty()) o += ","; o += n; }
        return o;
    }
    std::string dateText() override { return "DATE"; }

    std::string sxHostSt = "ok";
    std::string sxHost(int k) override
    {
        stub("sxHost " + std::to_string(k) + (sxHostSt == "ok" ? std::string() : " -> " + sxHostSt));
        return sxHostSt;
    }
    bool sxSnap(Id p, const std::string& sids, const std::string& wc, std::string& id, int& n, double& money, std::string& hand, bool& reused, std::string& err) override
    {
        stub("sxSnap " + std::to_string(p) + (wc.empty() ? std::string() : " w=" + wc));
        if (!sxSnapOk) { err = "falhou"; return false; }
        id = sxSnapId; n = sxSnapN; money = sxSnapMoney; hand = sxSnapHand; reused = sxSnapReused;
        return true;
    }
    std::string sxGet(std::string& id, double& money, std::string& items, std::string& qs, std::string& hand, std::string& w) override
    {
        id = sxId; money = sxMoney; items = sxItems; qs = sxQs; hand = sxHand; w = sxW;
        return sxState;
    }
    bool sxMark(const std::string& id, const std::string& st) override { stub("sxMark " + id + " " + st); return true; }
    bool sxWant(const std::string& id) override
    {
        if (id.size() > 8 && id[0] == '9' && std::find(wants.begin(), wants.end(), id) == wants.end()) wants.push_back(id);
        stub("sxWant " + id);
        return true;
    }
    bool wpLast(std::string& file, std::string& cid, int& k, int& hs) override
    {
        if (!wpHave) return false;
        file = wpFile; cid = wpCid; k = wpKh; hs = wpHs;
        return true;
    }
    bool wspStart(const std::string&, const std::string& guid, const std::string& file, bool own, bool strip, std::string&) override
    {
        stub("wspStart " + guid + " " + file + (own ? " own" : " hostpos") + (strip ? " strip" : ""));
        wspSt = "run";
        return true;
    }
    std::string wspPoll(std::string& d) override { d = wspD; return wspSt; }
    std::string sxIn(const std::string& id, std::string& d) override { d = sxInD; return sxInSt; }
    bool sxInstall(const std::string& id, std::string& g, int& k, std::string& cid, std::string& err) override
    {
        stub("sxInstall " + id);
        if (!sxInstOk) { err = "recusado"; return false; }
        g = sxInstG; k = sxInstKh; cid = sxInstCid;
        return true;
    }
    bool sxUndo(const std::string& g) override { stub("sxUndo " + g); return true; }

    Id localPawn() override { return pw; }
    bool hp(Id, double& o) override { o = 100; return true; }
    bool movementMode(Id, int& o) override { o = mm; return true; }
    bool setMovementMode(Id, int m) override { stub("setMovementMode " + std::to_string(m)); mm = m; return true; }
    bool location(Id, V3& o) override { o = lpos; return true; }
    bool localPos(V3& o) override { o = lpos; return pw != 0; }
    bool money(Id, double& o) override { o = cash; return true; }
    bool modifyMoney(Id, long long d) override { stub("modifyMoney " + std::to_string(d)); cash += (double)d; return true; }
    bool itemCount(Id, const std::string& sid, int& o) override
    {
        auto it = inv.find(sid);
        o = it == inv.end() ? 0 : it->second;
        return true;
    }
    bool ammoLoaded(Id, const std::string& sid, int& o) override
    {
        auto it = loaded.find(sid);
        o = it == loaded.end() ? 0 : it->second;
        return true;
    }
    bool createItem(const std::string& sid, int n, std::string& err) override
    {
        stub("createItem " + sid + " " + std::to_string(n));
        if (!createOk) { err = "falhou"; return false; }
        inv[sid] += n;
        return true;
    }
    bool forcedAutoSave(std::string& err) override
    {
        stub("forcedAutoSave");
        if (!saveOk) { err = "falhou"; return false; }
        return true;
    }
    bool joinAutoSave(std::string& err) override
    {
        stub("joinAutoSave");
        if (!saveOk) { err = "CanSaveNow(Auto) asked 1, allowed 0"; return false; }
        return true;
    }
    bool loadLastSave(std::string& err) override
    {
        stub("loadLastSave");
        if (!loadOk) { err = "falhou"; return false; }
        return true;
    }
    int loadBegins(std::string& guid) override
    {
        guid = loadGuid;
        return loadN;
    }
    bool part(const char* tag, std::string& o) override
    {
        auto it = parts.find(tag);
        if (it == parts.end()) return false;
        o = it->second;
        return true;
    }
    int peerState() override { return pst; }
    bool peerLiveFalse() override { return !plive; }
    int peerSessions() override { return psess; }
    bool remotePos(V3& o) override { if (!hasR) return false; o = rpos; return true; }

    QuestResult questApply(const std::string& kind, const std::optional<std::string>& a, const std::optional<std::string>& b, const std::optional<std::string>& c,
                           const std::optional<std::string>& sid, bool live) override
    {
        auto s = [](const std::optional<std::string>& v) { return v ? *v : std::string("nil"); };
        stub("quest " + kind + " " + s(a) + " " + s(b) + " " + s(c) + " " + s(sid) + " " + (live ? "true" : "nil"));
        QuestResult r;
        r.ok = true; r.r = 1;
        return r;
    }
    void rbDone(const std::string& why) override { stub("rbDone " + why); qs().rbWait.reset(); }
    int hostLogSize() override { return kh; }
    std::optional<long long> hostGen() override { return gen; }
    void qiReset() override { stub("qiReset"); }
    bool questTableOn() override { return qtab.has_value(); }
    bool isQuestItem(const std::string& sid) override { return qtab && qtab->count(sid); }
    void itemRemove(const std::string& sid, int n) override
    {
        stub("itemRemove " + sid + " " + std::to_string(n));
        auto lf = loaded.find(sid);
        int lo = lf == loaded.end() ? 0 : lf->second;
        inv[sid] -= std::min(n, std::max(0, inv[sid] - lo));
        if (inv[sid] <= 0) inv.erase(sid);
    }
    bool itemsBusy() override { return busy; }
    int questsHeld() override { return held; }
    bool cineFrozen() override { return frozen; }
    bool cineJoinBusy() override { return frozen || rcine; }
    bool downed() override { return down; }
    bool emNoSave() override { return emns; }
    void leashSet(const std::string& name, double m, double secs, const std::string& why, bool hard) override { stub("lrSet " + name + " " + why); }
    void leashDrop(const std::string& name, const std::string& why) override { stub("lrDrop " + name + " " + why); }
    bool leashPull(const std::string& name, double m, double secs, const std::string& why) override
    {
        stub("lrPull " + name + " " + why);
        return pullOk;
    }
};

static std::string rest(std::istringstream& is)
{
    std::string r;
    std::getline(is, r);
    if (!r.empty() && r[0] == ' ') r.erase(0, 1);
    return r;
}

int main(int argc, char** argv)
{
    if (argc < 4) { std::fprintf(stderr, "usage: test_save <scenario> <rootdir> <loot_sids.txt>\n"); return 2; }
    World w;
    w.root = argv[2];
    std::ofstream logf((w.root / "run.log").string(), std::ios::binary);
    w.out = &logf;
    std::vector<std::string> loot;
    {
        std::ifstream lf(argv[3]);
        std::string s;
        while (std::getline(lf, s)) { if (!s.empty() && s.back() == '\r') s.pop_back(); if (!s.empty()) loot.push_back(s); }
    }
    reset();
    bind(&w);
    setLootSids(loot);
    std::ifstream sc(argv[1]);
    std::string line;
    int ln = 0;
    while (std::getline(sc, line))
    {
        ++ln;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream is(line);
        std::string c;
        is >> c;
        logf << "CMD " << line << "\n";
        if (c == "role") { std::string r; is >> r; w.host = r == "host"; }
        else if (c == "flag") { std::string k, v; is >> k >> v; w.flags[k] = std::atof(v.c_str()); w.flagTxt[k] = v; }
        else if (c == "t") { is >> w.t; }
        else if (c == "pawn") { is >> w.pw; }
        else if (c == "inv") { std::string s; int n; is >> s >> n; if (n > 0) w.inv[s] = n; else w.inv.erase(s); }
        else if (c == "invclear") w.inv.clear();
        else if (c == "loaded") { std::string s; int n; is >> s >> n; if (n > 0) w.loaded[s] = n; else w.loaded.erase(s); }
        else if (c == "money") is >> w.cash;
        else if (c == "pos") is >> w.lpos.x >> w.lpos.y >> w.lpos.z;
        else if (c == "rpos") { std::string a; is >> a; if (a == "none") w.hasR = false; else { w.hasR = true; w.rpos.x = std::atof(a.c_str()); is >> w.rpos.y >> w.rpos.z; } }
        else if (c == "part") { std::string tg; is >> tg; w.parts[tg] = unesc(rest(is)); }
        else if (c == "nopart") { std::string tg; is >> tg; w.parts.erase(tg); }
        else if (c == "peer") { std::string s; int live; is >> s >> w.psess >> live; w.pst = s == "up" ? 1 : (s == "silent" ? 2 : 0); w.plive = live != 0; }
        else if (c == "stamp") { std::string s = rest(is); if (s == "none") w.stamp.reset(); else w.stamp = s; }
        else if (c == "savedir") { int v; is >> v; w.savedir = v != 0; }
        else if (c == "file") { std::string p; is >> p; rawWrite(w.root / p, unesc(rest(is))); }
        else if (c == "rm") { std::string p; is >> p; std::error_code ec; fs::remove(w.root / p, ec); }
        else if (c == "saveok") { int v; is >> v; w.saveOk = v != 0; }
        else if (c == "loadok") { int v; is >> v; w.loadOk = v != 0; }
        else if (c == "createok") { int v; is >> v; w.createOk = v != 0; }
        else if (c == "pullok") { int v; is >> v; w.pullOk = v != 0; }
        else if (c == "kh") is >> w.kh;
        else if (c == "gen") { std::string s; is >> s; if (s == "none") w.gen.reset(); else w.gen = std::atoll(s.c_str()); }
        else if (c == "qtab") { std::string s; is >> s; if (s == "none") w.qtab.reset(); else { w.qtab.emplace(); std::stringstream ss(s); std::string x; while (std::getline(ss, x, ',')) w.qtab->insert(x); } }
        else if (c == "busy") { int v; is >> v; w.busy = v != 0; }
        else if (c == "held") is >> w.held;
        else if (c == "frozen") { int v; is >> v; w.frozen = v != 0; }
        else if (c == "rcine") { int v; is >> v; w.rcine = v != 0; }
        else if (c == "downed") { int v; is >> v; w.down = v != 0; }
        else if (c == "emnosave") { int v; is >> v; w.emns = v != 0; }
        else if (c == "mm") is >> w.mm;
        else if (c == "rbaddr") { int v; is >> v; qs().rbAddrKnown = v != 0; }
        else if (c == "rbcheck") { int v; is >> v; qs().rbCheckPending = v != 0; }
        else if (c == "rbdone") qs().rbWait.reset();
        else if (c == "qlok") { int v; is >> v; qs().qlOk = v != 0; }
        else if (c == "sxget")
        {
            std::string st, id, items, q, wc;
            is >> st >> id >> w.sxMoney >> items >> q >> wc;
            w.sxState = st; w.sxId = id == "-" ? "" : id; w.sxItems = items == "-" ? "" : items; w.sxQs = q == "-" ? "" : q; w.sxW = wc == "-" ? "" : wc;
        }
        else if (c == "sxin") { is >> w.sxInSt >> w.sxInD; }
        else if (c == "wplast")
        {
            size_t n;
            is >> w.wpCid >> w.wpKh >> w.wpHs >> n;
            w.wpHave = true;
            w.wpFile = w.wpCid + "__" + (n >= 1 && n <= w.wants.size() ? w.wants[n - 1] : std::string("none")) + ".pkg";
        }
        else if (c == "wsp") { is >> w.wspSt >> w.wspD; }
        else if (c == "sxhost") { w.sxHostSt = rest(is); }
        else if (c == "sxinstall") { int ok; is >> ok >> w.sxInstG >> w.sxInstKh >> w.sxInstCid; w.sxInstOk = ok != 0; }
        else if (c == "sxsnap") { int ok, re; is >> ok >> w.sxSnapId >> w.sxSnapN >> w.sxSnapMoney >> w.sxSnapHand >> re; w.sxSnapOk = ok != 0; w.sxSnapReused = re != 0; }
        else if (c == "rw") { std::string q; is >> q; bool r = n7Rw(q); logf << "RET " << (r ? "true" : "false") << "\n"; }
        else if (c == "out") { logf << "OUT N=" << csOut() << " B=" << n7B() << "\n"; }
        else if (c == "tick") tick100();
        else if (c == "diefollow") n7DieFollow();
        else if (c == "go") n7Go(rest(is));
        else if (c == "hostload") { is >> w.loadGuid; w.loadN += 1; }
        else if (c == "hld") { logf << "OUT HLD=" << n7HL() << "\n"; }
        else if (c == "ticks")
        {
            int n; double dt;
            is >> n >> dt;
            for (int i = 0; i < n; ++i) { tick100(); w.t += dt; }
        }
        else { std::fprintf(stderr, "line %d: unknown command %s\n", ln, c.c_str()); return 3; }
    }
    return 0;
}
