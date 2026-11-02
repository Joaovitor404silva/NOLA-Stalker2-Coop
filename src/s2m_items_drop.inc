bool dropHas(Obj o)
{
    if (!g_st) return false;
    Drop& D = S().drop;
    for (auto& l : D.loc) if (l.o == o) return true;
    for (auto& p : D.pre) if (p.o == o) return true;
    return false;
}
bool dropSeen(Obj o)
{
    if (dropHas(o)) return true;
    Drop& D = S().drop;
    return D.done.count(o) != 0;
}
bool dropTbl()
{
    Drop& D = S().drop;
    if (D.loaded) return !D.m2s.empty();
    D.loaded = true;
    std::string text;
    if (!E().dataText("s2u_drop.tsv", text))
    {
        LOG("NOLA idrop: s2u_drop.tsv missing from the data");
        return false;
    }
    for (auto& l : lines(text))
    {
        auto f = splitAll(l, '\t');
        if (f.size() < 2 || f[0].empty() || f[1].empty() || f[0] == "sid") continue;
        D.m2s[f[1]].push_back(f[0]);
        D.s2m[f[0]] = f[1];
    }
    LOG(sf("NOLA idrop: %d meshes, %d items in the drop table", (int)D.m2s.size(), (int)D.s2m.size()));
    return !D.m2s.empty();
}
std::string dKey(Obj o) { return sf("%llu|%s", (unsigned long long)E().addr(o), E().name(o).c_str()); }
std::string dStOk(const std::string& s)
{
    std::vector<s2x_is::IState> v;
    return s2x_is::decList(s, v) ? s : std::string();
}
std::string dropState(Obj pw, Obj ground, const std::string& sid, long long n)
{
    if (!isOn()) return "";
    Drop& D = S().drop;
    bool ok = false;
    auto g = s2x_is::ofSid(s2x_is::expand(isRead(ground, 2, ok), true), sid);
    std::vector<s2x_is::IState> r;
    const char* src = "the ground item";
    if (ok && !g.empty()) r = g;
    else
    {
        src = "the inventory before the drop";
        auto now = isRead(pw, 0, ok);
        if (!ok) return "";
        std::set<uint32_t> have;
        for (auto& i : now) have.insert(i.uid);
        for (auto it = D.binst.begin(); it != D.binst.end() && (long long)r.size() < n;)
        {
            if (it->second.st.sid != sid || have.count(it->first)) { ++it; continue; }
            r.push_back(it->second.st);
            it = D.binst.erase(it);
        }
    }
    if ((long long)r.size() > n) r.resize((size_t)n);
    std::string e = s2x_is::encList(r);
    if (!e.empty()) LOG("NOLA istate: drop of " + sid + " state from " + src + ": " + isShow(r));
    return e;
}
void dropIset(DLoc& L)
{
    if (L.iset || L.st.empty() || !isOn() || !VAL(L.o))
    {
        L.iset = true;
        return;
    }
    std::vector<s2x_is::IState> want;
    if (!s2x_is::decList(L.st, want) || want.empty())
    {
        L.iset = true;
        return;
    }
    bool ok = false;
    auto hv = isRead(L.o, 2, ok);
    if (!ok) return;
    L.iset = true;
    std::vector<std::string> lg;
    s2x_is::Plan p = s2x_is::plan(want, hv);
    for (auto& a : p.apply)
    {
        bool r = E().natApplyI(hv[(size_t)a.first].uid, a.second);
        lg.push_back("={" + s2x_is::show(hv[(size_t)a.first].st) + "}->{" + s2x_is::show(a.second) + "}" + (r ? "" : "(erro)"));
    }
    LOG(sf("NOLA istate: drop mirror %s set to its state: [%s]%s", L.id.c_str(), join(lg, " ").c_str(), p.keep.size() == want.size() ? " (already exact)" : ""));
}
std::vector<s2x_is::Inst> dNew(Obj pw, const DClaim& C, bool& ok)
{
    std::vector<s2x_is::Inst> r;
    auto v = isRead(pw, 0, ok);
    if (!ok) return r;
    for (auto& i : v)
        if (i.st.sid == C.sid && !C.before.count(i.uid)) r.push_back(i);
    return r;
}

bool dFar(Obj o, V3& q) { return E().loc(o, q) && std::fabs(q.x) + std::fabs(q.y) > 1000; }
double dDist(const V3& a, const V3& b) { return std::sqrt(sq(a.x - b.x) + sq(a.y - b.y) + sq(a.z - b.z)); }
bool dCount(Obj pw, const std::string& sid, long long& n)
{
    n = 0;
    return VAL(pw) && E().invReadable(pw) && E().count(pw, sid, n);
}
const DEnt* dEnt(const std::string& id)
{
    for (auto& e : S().drop.ents) if (e.id == id) return &e;
    return nullptr;
}
const DTake* dTake(const std::string& id)
{
    for (auto& t : S().drop.takes) if (t.id == id) return &t;
    return nullptr;
}
std::string dropHostAdd(int owner, long long seq, const std::string& tok, const std::string& sid, const std::string& mesh, long long n, double x, double y, double z, const std::string& st)
{
    Drop& D = S().drop;
    std::string id = tok + "." + lld(seq);
    if (dEnt(id)) return id;
    DEnt e;
    e.id = id;
    e.owner = owner;
    e.seq = seq;
    e.sid = sid;
    e.mesh = mesh;
    e.n = n;
    e.x = x;
    e.y = y;
    e.z = z;
    e.st = st;
    e.t = NOW();
    D.ents.push_back(e);
    D.ever = true;
    D.nReg += 1;
    LOG(sf("NOLA idrop: registry +%s owner=%d %s x%lld at %.0f,%.0f,%.0f %s (total %d)", id.c_str(), owner, sid.c_str(), n, x, y, z, mesh.c_str(), (int)D.ents.size()));
    if (!st.empty()) LOG("NOLA istate: drop " + id + " carries state " + st);
    long long mx = FI("idropmax", 60);
    while ((long long)D.ents.size() > mx)
    {
        LOG("NOLA idrop: registry full, oldest entry " + D.ents.front().id + " dropped");
        D.ents.erase(D.ents.begin());
    }
    return id;
}
void dropClear(const char* why)
{
    Drop& D = S().drop;
    if (D.loc.empty() && D.ents.empty() && D.pre.empty() && D.cl.empty() && D.takes.empty()) return;
    LOG(sf("NOLA idrop: state cleared (%s): %d local, %d registry, %d claims", why, (int)D.loc.size(), (int)D.ents.size(), (int)D.cl.size()));
    D.loc.clear();
    D.ents.clear();
    D.pre.clear();
    D.cl.clear();
    D.takes.clear();
    D.kept.clear();
    D.rbAt = 0;
    D.rbGsOk = false;
    D.known.clear();
    D.rej.clear();
    D.done.clear();
    D.binst.clear();
    D.tbi.clear();
    D.armed = false;
    D.snap = false;
    D.ever = false;
}
void dropRegister(DPre& p, double now)
{
    Drop& D = S().drop;
    DLoc L;
    L.seq = ++D.seq;
    L.o = p.o;
    L.mine = true;
    L.sid = p.sid;
    L.mesh = p.mesh;
    L.st = p.st;
    L.iset = true;
    L.n = p.n;
    L.p = p.p;
    L.last = p.p;
    L.born = now;
    if (HOST()) L.id = dropHostAdd(0, L.seq, "0.0", L.sid, L.mesh, L.n, L.p.x, L.p.y, L.p.z, L.st);
    else LOG(sf("NOLA idrop: local drop #%lld %s x%lld at %.0f,%.0f,%.0f %s sent to the host", L.seq, L.sid.c_str(), L.n, L.p.x, L.p.y, L.p.z, L.mesh.c_str()));
    D.loc.push_back(L);
}
void dropPre(double now)
{
    Drop& D = S().drop;
    double settle = F("idropsettle", 1.0);
    for (size_t i = D.pre.size(); i-- > 0;)
    {
        DPre& p = D.pre[i];
        V3 q;
        if (!VAL(p.o) || !E().loc(p.o, q) || std::fabs(q.x) + std::fabs(q.y) < 1000)
        {
            LOG(sf("NOLA idrop: %s x%lld taken back before it settled", p.sid.c_str(), p.n));
            D.done[p.o] = now;
            D.pre.erase(D.pre.begin() + (long)i);
            continue;
        }
        if (dDist(q, p.p) > 5)
        {
            p.p = q;
            p.since = now;
        }
        if (now - p.since >= settle || now - p.born > 15)
        {
            p.p = q;
            DPre c = p;
            D.pre.erase(D.pre.begin() + (long)i);
            dropRegister(c, now);
        }
    }
}
void dropScan(Obj pw, double now)
{
    Drop& D = S().drop;
    bool open = true;
    if (F("idropui", 1) == 1)
    {
        open = false;
        Obj w = E().first("InventoryNew");
        bool v = false;
        bool got = VAL(w) && E().visible(w, v);
        if (got) open = v;
        int state = !VAL(w) ? 0 : (got ? (v ? 2 : 1) : 3);
        if (state != D.wState)
        {
            D.wState = state;
            LOG(sf("NOLA idrop: inventory widget state %d (0 none, 1 hidden, 2 visible, 3 unreadable)", state));
        }
    }
    if (open)
    {
        if (!D.wasOpen)
        {
            D.wasOpen = true;
            if (now - D.lastOpen >= F("idropidle", 60))
            {
                D.armed = false;
                D.known.clear();
                D.rej.clear();
                D.base.clear();
                D.binst.clear();
                D.baseAt = -1e9;
            }
        }
        D.lastOpen = now;
        if (now - D.baseAt >= F("idropbase", 1.5))
        {
            D.baseAt = now;
            for (auto& kv : countAll(pw))
            {
                long long& b = D.base[kv.first];
                if (kv.second > b) b = kv.second;
            }
            if (isOn())
            {
                bool ok = false;
                for (auto& i : isRead(pw, 0, ok))
                    if (!i.st.plain()) D.binst[i.uid] = i;
            }
        }
    }
    else D.wasOpen = false;
    if (!(open || now - D.lastOpen < F("idropwin", 6)))
    {
        if (now - D.lastOpen >= F("idropidle", 60)) D.armed = false;
        return;
    }
    if (now - D.scanAt < F("idropscan", 0.5)) return;
    D.scanAt = now;
    V3 pp;
    if (!E().loc(pw, pp)) return;
    double R = F("idropr", 500);
    struct Cd
    {
        Obj o;
        std::string key, mesh;
        V3 q;
    };
    std::vector<Cd> cds;
    for (Obj o : E().all("BP_StaticItemContainer_C"))
    {
        if (!VAL(o)) continue;
        bool h = false;
        if (E().hidden(o, h) && h) continue;
        V3 q;
        if (!E().loc(o, q)) continue;
        std::string key = dKey(o);
        bool far = std::fabs(q.x) + std::fabs(q.y) > 1000;
        auto it = D.known.find(key);
        bool was = it != D.known.end() && it->second;
        D.known[key] = far;
        if (!D.armed || !far || was || dropHas(o)) continue;
        if (dDist(q, pp) > R) continue;
        bool spawning = false;
        for (auto& L : D.loc) if (L.wait && !L.o && sq(q.x - L.p.x) + sq(q.y - L.p.y) < sq(300)) spawning = true;
        if (spawning) continue;
        cds.push_back(Cd{o, key, meshOf(o), q});
    }
    D.armed = true;
    if (cds.empty()) return;
    struct Pick
    {
        size_t ci;
        std::string sid;
        long long dec, cur;
    };
    std::vector<Pick> picks;
    for (size_t i = 0; i < cds.size(); ++i)
    {
        auto m = D.m2s.find(cds[i].mesh);
        bool ok = false;
        if (m != D.m2s.end())
        {
            Pick best{i, "", 0, 0};
            for (auto& s : m->second)
            {
                if (qs(s)) continue;
                long long cur = 0;
                if (!dCount(pw, s, cur)) continue;
                long long dec = g0(D.base, s) - cur;
                if (dec > best.dec) best = Pick{i, s, dec, cur};
            }
            if (best.dec > 0)
            {
                picks.push_back(best);
                ok = true;
            }
        }
        if (!ok)
        {
            int& r = D.rej[cds[i].key];
            r += 1;
            if (m != D.m2s.end() && r < 6) D.known[cds[i].key] = false;
        }
    }
    std::map<std::string, std::vector<size_t>> bySid;
    for (size_t k = 0; k < picks.size(); ++k) bySid[picks[k].sid].push_back(k);
    for (auto& kv : bySid)
    {
        long long total = picks[kv.second[0]].dec;
        long long k = (long long)kv.second.size();
        if (total < k) k = total;
        for (size_t j = 0; j < kv.second.size(); ++j)
        {
            Pick& pk = picks[kv.second[j]];
            if ((long long)j >= k) continue;
            long long n = total / k + ((long long)j < total % k ? 1 : 0);
            Cd& c = cds[pk.ci];
            DPre p;
            p.o = c.o;
            p.sid = pk.sid;
            p.mesh = c.mesh;
            p.n = n;
            p.p = c.q;
            p.since = now;
            p.born = now;
            p.st = dropState(pw, c.o, pk.sid, n);
            D.pre.push_back(p);
            LOG(sf("NOLA idrop: drop seen %s x%lld mesh %s at %.0f,%.0f,%.0f", p.sid.c_str(), n, p.mesh.c_str(), c.q.x, c.q.y, c.q.z));
        }
        D.base[kv.first] = picks[kv.second[0]].cur;
    }
}
std::string dropTok(int v) { return vTok(v); }
void dropIntake(double now)
{
    Drop& D = S().drop;
    for (int v = 0; v < E().views(); ++v)
    {
        std::string s;
        if (!PARTV("IDR", s, v) || s.empty()) continue;
        std::string tok = dropTok(v);
        int pid = E().viewPid(v);
        for (auto& ent : splitNE(s, '|'))
        {
            auto f = splitAll(ent, ';');
            if (f.size() != 7 && f.size() != 8) continue;
            std::string key = tok + ":" + f[0];
            if (D.rseen.count(key)) continue;
            D.rseen[key] = now;
            double seq, n, x, y, z;
            if (!fullInt(f[0], seq) || !allOf(f[1], isW) || !fullInt(f[2], n) || !fullNum(f[3], x) || !fullNum(f[4], y) || !fullNum(f[5], z) || !wq(f[6])) continue;
            if (n < 1 || n > 100000 || !D.s2m.count(f[1])) continue;
            dropHostAdd(pid, (long long)seq, tok, f[1], f[6], (long long)n, x, y, z, f.size() == 8 ? dStOk(f[7]) : std::string());
        }
    }
}
void dropClaimsIn(double now)
{
    Drop& D = S().drop;
    for (int v = 0; v < E().views(); ++v)
    {
        std::string s;
        if (!PARTV("IDU", s, v) || s.empty()) continue;
        std::string tok = dropTok(v);
        int pid = E().viewPid(v);
        for (auto& ent : splitNE(s, '|'))
        {
            auto f = splitAll(ent, ';');
            if (f.size() != 2 || f[1].empty()) continue;
            std::string key = tok + ":" + f[0];
            if (D.useen.count(key)) continue;
            D.useen[key] = now;
            const std::string id = f[1];
            bool okId = true;
            for (char c : id) if (!(isD(c) || c == '.')) okId = false;
            if (!okId) continue;
            for (size_t i = 0; i < D.ents.size(); ++i)
            {
                if (D.ents[i].id != id) continue;
                D.ents.erase(D.ents.begin() + (long)i);
                for (size_t q = D.kept.size(); q-- > 0;)
                    if (D.kept[q].e.id == id) D.kept.erase(D.kept.begin() + (long)q);
                D.takes.push_back(DTake{id, pid, now});
                LOG(sf("NOLA idrop: %s taken by pid %d (first to ask wins)", id.c_str(), pid));
                break;
            }
            if (!dTake(id))
            {
                D.takes.push_back(DTake{id, pid, now});
                LOG(sf("NOLA idrop: %s unknown to the registry, given to pid %d", id.c_str(), pid));
            }
            else
            {
                const DTake* t = dTake(id);
                if (t->pid != pid) LOG(sf("NOLA idrop: pid %d asked for %s too late, pid %d got it", pid, id.c_str(), t->pid));
            }
        }
    }
}
void dropSnapIn()
{
    Drop& D = S().drop;
    std::string s;
    if (!PART("IDS", s) || s.empty()) return;
    std::vector<DEnt> ents;
    std::vector<DTake> takes;
    double now = NOW();
    if (s != "-")
    {
        for (auto& ent : splitNE(s, '|'))
        {
            auto f = splitAll(ent, ';');
            if ((f.size() == 10 || f.size() == 11) && f[0] == "E")
            {
                double owner, seq, n, x, y, z;
                if (!fullInt(f[2], owner) || !fullInt(f[3], seq) || !fullInt(f[5], n) || !fullNum(f[6], x) || !fullNum(f[7], y) || !fullNum(f[8], z)) continue;
                if (!allOf(f[4], isW) || !wq(f[9]) || f[1].empty()) continue;
                DEnt e;
                e.id = f[1];
                e.owner = (int)owner;
                e.seq = (long long)seq;
                e.sid = f[4];
                e.n = (long long)n;
                e.x = x;
                e.y = y;
                e.z = z;
                e.mesh = f[9];
                if (f.size() == 11) e.st = dStOk(f[10]);
                ents.push_back(e);
            }
            else if (f.size() == 3 && f[0] == "T")
            {
                double pid;
                if (!fullInt(f[2], pid) || f[1].empty()) continue;
                takes.push_back(DTake{f[1], (int)pid, now});
            }
        }
    }
    D.ents = ents;
    D.takes = takes;
    D.snap = true;
}
bool dRbOn() { return E().fnum("idroprb", 1) != 0; }
void dropRollback(double now)
{
    Drop& D = S().drop;
    if (D.rbAt <= 0 || now < D.rbAt) return;
    if (!HOST() || !dRbOn() || D.kept.empty())
    {
        D.rbAt = 0;
        D.kept.clear();
        return;
    }
    std::optional<double> gt;
    if (D.rbGsOk) gt = D.rbGs;
    else gt = E().gameSecs();
    if (!gt)
    {
        if (now > D.rbAt + 30)
        {
            D.rbAt = 0;
            D.rbGsOk = false;
        }
        return;
    }
    D.rbAt = 0;
    D.rbGsOk = false;
    int back = 0;
    for (auto& k : D.kept)
    {
        if (k.restored)
        {
            if (*gt < k.gs) continue;
            bool removed = false;
            for (size_t i = D.ents.size(); i-- > 0;)
                if (D.ents[i].id == k.e.id)
                {
                    D.ents.erase(D.ents.begin() + (long)i);
                    removed = true;
                }
            k.restored = false;
            if (removed)
            {
                D.takes.push_back(DTake{k.e.id, 0, now});
                LOG(sf("NOLA idrop: host load after the pick-up of %s (game time %.0f >= %.0f), registry entry removed again", k.e.id.c_str(), *gt, k.gs));
            }
            continue;
        }
        if (k.gs <= *gt) continue;
        const DTake* t = dTake(k.e.id);
        if (dEnt(k.e.id) || (t && t->pid != 0)) continue;
        for (size_t i = D.takes.size(); i-- > 0;)
            if (D.takes[i].id == k.e.id) D.takes.erase(D.takes.begin() + (long)i);
        for (size_t i = D.cl.size(); i-- > 0;)
            if (D.cl[i].id == k.e.id) D.cl.erase(D.cl.begin() + (long)i);
        D.fail.erase(k.e.id);
        DEnt e = k.e;
        e.t = now;
        D.ents.push_back(e);
        k.restored = true;
        ++back;
        LOG(sf("NOLA idrop: host load before the pick-up of %s (game time %.0f < %.0f), registry entry restored", e.id.c_str(), *gt, k.gs));
    }
    if (back) LOG(sf("NOLA idrop: %d host pick-ups rolled back by the load", back));
}
void dropClaim(DLoc& L, double now)
{
    Drop& D = S().drop;
    DClaim C;
    C.id = L.id;
    C.sid = L.sid;
    C.st = L.st;
    C.n = L.n;
    C.t = now;
    auto bi = D.tbi.find(L.sid);
    if (bi != D.tbi.end())
    {
        C.before = bi->second;
        C.hasBefore = true;
    }
    auto tb = D.tb.find(L.sid);
    if (tb != D.tb.end()) C.base = tb->second;
    else
    {
        long long cur = 0;
        Obj pw = E().pawn();
        dCount(pw, L.sid, cur);
        C.base = cur - L.n;
    }
    {
        long long pc = 0;
        dCount(E().pawn(), L.sid, pc);
        C.peak = std::max(C.base, pc);
    }
    if (HOST())
    {
        bool had = false;
        for (size_t i = 0; i < D.ents.size(); ++i)
            if (D.ents[i].id == C.id)
            {
                auto gs = E().gameSecs();
                for (size_t q = D.kept.size(); q-- > 0;)
                    if (D.kept[q].e.id == C.id) D.kept.erase(D.kept.begin() + (long)q);
                if (dRbOn() && gs)
                {
                    D.kept.push_back(DKept{D.ents[i], *gs});
                    while (D.kept.size() > 64) D.kept.erase(D.kept.begin());
                }
                D.ents.erase(D.ents.begin() + (long)i);
                had = true;
                break;
            }
        if (had)
        {
            D.takes.push_back(DTake{C.id, 0, now});
            C.verdict = 1;
            LOG(sf("NOLA idrop: %s taken by the host", C.id.c_str()));
        }
        else
        {
            C.verdict = -1;
            LOG(sf("NOLA idrop: %s was already gone, the host's copy is undone", C.id.c_str()));
        }
    }
    else
    {
        C.cseq = ++D.cseq;
        LOG(sf("NOLA idrop: %s picked up here, asking the host (claim %lld)", C.id.c_str(), C.cseq));
    }
    D.cl.push_back(C);
}
bool dClaimHas(const std::string& id)
{
    for (auto& c : S().drop.cl) if (c.id == id) return true;
    return false;
}
void dropGone(Obj pw, double now)
{
    Drop& D = S().drop;
    V3 pp;
    bool hp = E().loc(pw, pp);
    double nearR = F("idropnear", 600);
    for (size_t i = D.loc.size(); i-- > 0;)
    {
        DLoc& L = D.loc[i];
        if (L.gone)
        {
            if (!L.id.empty())
            {
                dropClaim(L, now);
                D.loc.erase(D.loc.begin() + (long)i);
            }
            else if (now - L.born > 20)
            {
                LOG(sf("NOLA idrop: local drop #%lld was picked up before the host answered, giving up", L.seq));
                D.loc.erase(D.loc.begin() + (long)i);
            }
            continue;
        }
        if (!L.o) continue;
        bool gone = false;
        V3 q;
        if (!VAL(L.o)) gone = true;
        else if (E().loc(L.o, q))
        {
            if (std::fabs(q.x) + std::fabs(q.y) < 1000) gone = true;
            else L.last = q;
        }
        if (!gone) continue;
        Obj o = L.o;
        D.done[o] = now;
        L.o = 0;
        L.wait = false;
        bool near = hp && dDist(pp, L.last) < nearR;
        if (!near)
        {
            L.tries = 0;
            L.nextT = now + 2;
            LOG(sf("NOLA idrop: %s left the world here without a pick-up (streamed out), will be respawned", L.id.c_str()));
            if (L.mine && L.id.empty()) D.loc.erase(D.loc.begin() + (long)i);
            continue;
        }
        if (L.id.empty())
        {
            L.gone = true;
            continue;
        }
        dropClaim(L, now);
        D.loc.erase(D.loc.begin() + (long)i);
    }
}
bool dropBind(DLoc& L)
{
    Obj found = 0;
    double bd = 0;
    for (Obj o : E().all("BP_StaticItemContainer_C"))
    {
        if (!VAL(o) || dropHas(o)) continue;
        bool h = false;
        if (E().hidden(o, h) && h) continue;
        V3 q;
        if (!dFar(o, q)) continue;
        if (L.before.count(dKey(o))) continue;
        double d = std::sqrt(sq(q.x - L.p.x) + sq(q.y - L.p.y));
        if (d > 250 || (found && d >= bd)) continue;
        found = o;
        bd = d;
    }
    if (!found) return false;
    L.o = found;
    L.wait = false;
    L.before.clear();
    V3 q;
    if (E().loc(found, q)) L.last = q;
    std::string m = meshOf(found);
    LOG(sf("NOLA idrop: mirror of %s bound to %s (mesh %s%s) %.0f cm from the spot", L.id.c_str(), E().name(found).c_str(), m.c_str(), m == L.mesh ? "" : ", differs from the table", bd));
    dropIset(L);
    return true;
}
void dropSync(Obj pw, double now)
{
    Drop& D = S().drop;
    int me = E().myPid();
    bool valid = HOST() || D.snap;
    for (auto& L : D.loc)
    {
        if (!L.id.empty() || !L.mine) continue;
        for (auto& e : D.ents)
            if (e.owner == me && e.seq == L.seq && e.sid == L.sid && dDist(V3{e.x, e.y, e.z}, L.p) < 150)
            {
                L.id = e.id;
                LOG(sf("NOLA idrop: local drop #%lld is %s in the registry", L.seq, e.id.c_str()));
                break;
            }
    }
    if (valid)
    {
        for (size_t i = D.loc.size(); i-- > 0;)
        {
            DLoc& L = D.loc[i];
            if (L.id.empty() || dEnt(L.id)) continue;
            if (VAL(L.o))
            {
                gHide(L.o);
                D.done[L.o] = now;
            }
            LOG(sf("NOLA idrop: %s is gone from the registry, local copy hidden", L.id.c_str()));
            D.loc.erase(D.loc.begin() + (long)i);
        }
    }
    V3 pp;
    bool hp = E().loc(pw, pp);
    bool open = E().worldOpen();
    for (auto it = D.fail.begin(); it != D.fail.end();)
        if (now > it->second) it = D.fail.erase(it); else ++it;
    if (valid && hp && open)
    {
        double rng = F("idropspawn", 4000);
        for (auto& e : D.ents)
        {
            bool have = false;
            for (auto& L : D.loc) if (L.id == e.id) have = true;
            if (have || dClaimHas(e.id) || dTake(e.id) || D.fail.count(e.id)) continue;
            if (dDist(V3{e.x, e.y, e.z}, pp) > rng) continue;
            if ((long long)D.loc.size() >= FI("idropmax", 60)) break;
            DLoc L;
            L.id = e.id;
            L.sid = e.sid;
            L.mesh = e.mesh;
            L.st = e.st;
            L.iset = e.st.empty();
            L.n = e.n;
            L.p = V3{e.x, e.y, e.z};
            L.last = L.p;
            L.born = now;
            D.loc.push_back(L);
        }
    }
    for (size_t i = D.loc.size(); i-- > 0;)
    {
        DLoc& L = D.loc[i];
        if (L.o && !L.iset && now >= L.nextT)
        {
            L.nextT = now + 1;
            dropIset(L);
        }
        if (L.o || L.gone || L.id.empty()) continue;
        if (!L.wait)
        {
            if (now < L.nextT || !open) continue;
            L.before.clear();
            for (Obj o : E().all("BP_StaticItemContainer_C")) if (VAL(o)) L.before.insert(dKey(o));
            std::vector<s2x_is::IState> sw;
            bool si = isOn() && !L.st.empty() && s2x_is::decList(L.st, sw) && !sw.empty() && sw[0].sid == L.sid;
            bool sp = si ? E().natSpawnI(sw[0], L.p.x, L.p.y, L.p.z + F("idropz", 30)) : E().spawnItem(L.sid, L.p.x, L.p.y, L.p.z + F("idropz", 30));
            if (si && sp)
            {
                L.iset = true;
                LOG(sf("NOLA istate: drop mirror %s spawned with its state: %s", L.id.c_str(), s2x_is::show(sw[0]).c_str()));
            }
            if (!sp)

            {
                L.tries += 1;
                L.nextT = now + 3;
                LOG(sf("NOLA idrop: spawn of %s for %s failed (try %d)", L.sid.c_str(), L.id.c_str(), L.tries));
                if (L.tries >= 3)
                {
                    D.fail[L.id] = now + 30;
                    D.loc.erase(D.loc.begin() + (long)i);
                }
                continue;
            }
            L.wait = true;
            L.spawnT = now;
            L.nextT = now + 0.4;
            D.nSpawn += 1;
        }
        else
        {
            if (now < L.nextT) continue;
            L.nextT = now + 0.4;
        }
        if (dropBind(L)) continue;
        if (now - L.spawnT > 5)
        {
            L.wait = false;
            L.tries += 1;
            L.nextT = now + 2;
            LOG(sf("NOLA idrop: no mirror of %s showed up in 5 s (try %d)", L.id.c_str(), L.tries));
            if (L.tries >= 3)
            {
                D.fail[L.id] = now + 30;
                D.loc.erase(D.loc.begin() + (long)i);
            }
        }
    }
}
std::vector<DClaim*> dGroup(DClaim& C, bool& pend, double now)
{
    std::vector<DClaim*> g{&C};
    pend = false;
    if (F("idropgrp", 1) != 1) return g;
    Drop& D = S().drop;
    std::vector<DClaim*> all;
    for (auto& X : D.cl)
        if (!X.done && X.sid == C.sid) all.push_back(&X);
    std::stable_sort(all.begin(), all.end(), [](const DClaim* a, const DClaim* b) { return a->t < b->t; });
    double gap = F("idropfix", 0.8);
    std::vector<DClaim*> cl;
    for (DClaim* X : all)
    {
        if (!cl.empty() && X->t - cl.back()->t > gap)
        {
            if (std::find(cl.begin(), cl.end(), &C) != cl.end()) break;
            cl.clear();
        }
        cl.push_back(X);
    }
    g.clear();
    for (DClaim* X : cl)
    {
        if (X->verdict == 0 || now - X->t < gap) pend = true;
        if (X->verdict == C.verdict) g.push_back(X);
    }
    return g;
}
void dResolve(DClaim& C, int me, double now)
{
    if (C.verdict != 0) return;
    if (const DTake* t = dTake(C.id))
    {
        C.verdict = t->pid == me ? 1 : -1;
        LOG(sf("NOLA idrop: %s verdict from the host: %s", C.id.c_str(), C.verdict > 0 ? "mine" : "lost"));
    }
    else if (now - C.t > F("idropverdict", 12))
    {
        C.verdict = 1;
        LOG(sf("NOLA idrop: no verdict for %s in time, keeping the item", C.id.c_str()));
    }
}
void dropSettle(Obj pw, double now)
{
    Drop& D = S().drop;
    int me = E().myPid();
    bool grp = F("idropgrp", 1) == 1;
    if (grp)
        for (auto& C : D.cl) dResolve(C, me, now);
    for (auto& C0 : D.cl)
    {
        if (!grp) dResolve(C0, me, now);
        if (C0.verdict == 0 || C0.done || now - C0.t < F("idropfix", 0.8)) continue;
        bool pend = false;
        std::vector<DClaim*> G = dGroup(C0, pend, now);
        if (pend) continue;
        DClaim& C = *G.front();
        for (DClaim* g : G) g->done = true;
        long long n = 0, base = C.base;
        std::vector<std::string> gids;
        for (DClaim* g : G)
        {
            n += g->n;
            base = std::min(base, g->base);
            gids.push_back(g->id);
        }
        long long cur = 0;
        if (!dCount(pw, C.sid, cur))
        {
            LOG(sf("NOLA idrop: %s count unreadable, no correction", C.id.c_str()));
            continue;
        }
        bool pkOn = F("idroppeak", 1) == 1;
        long long peak = cur;
        if (pkOn)
            for (DClaim* g : G) peak = std::max(peak, g->peak);
        long long got = (pkOn ? peak : cur) - base;
        long long gone = pkOn && cur < peak ? peak - cur : 0;
        if (G.size() > 1) LOG(sf("NOLA idrop: grouped %s (%d claims, %lld x %s, base %lld, now %lld)", join(gids, ",").c_str(), (int)G.size(), n, C.sid.c_str(), base, cur));
        std::vector<s2x_is::IState> want;
        std::vector<s2x_is::Inst> nw;
        bool iso = isOn() && C.hasBefore;
        for (DClaim* g : G)
        {
            std::vector<s2x_is::IState> w;
            if (g->st.empty() || !s2x_is::decList(g->st, w) || w.empty())
            {
                iso = false;
                break;
            }
            want.insert(want.end(), w.begin(), w.end());
        }
        if (iso && want.empty()) iso = false;
        if (iso) nw = dNew(pw, C, iso);
        std::vector<std::string> ilg;
        if (C.verdict > 0)
        {
            D.nTake += (long long)G.size();
            if (got <= 0) LOG(sf("NOLA idrop: %s picked up, no count change seen (%s %lld -> %lld), left as is", C.id.c_str(), C.sid.c_str(), base, cur));
            else if (got < n)
            {
                std::vector<s2x_is::IState> mk = iso ? s2x_is::minus(want, s2x_is::common(want, s2x_is::expand(nw, false))) : std::vector<s2x_is::IState>();
                if ((long long)mk.size() > n - got) mk.resize((size_t)(n - got));
                long long k = iso ? isPutN(pw, 0, C.sid, n - got, mk, ilg) : 0;
                if (n - got - k > 0) E().createItem(C.sid, 0, n - got - k);
                D.nFix += 1;
                LOG(sf("NOLA idrop: %s got %lld of %lld, added %lld", C.id.c_str(), got, n, n - got));
            }
            else if (got > n)
            {
                long long rmn = std::max(0LL, got - n - gone);
                long long k = 0;
                if (iso && rmn > 0)
                {
                    s2x_is::Plan p = s2x_is::plan(want, nw);
                    k = isRmN(pw, 0, C.sid, rmn, {}, nw, p.left, ilg);
                }
                if (rmn - k > 0) rmP(C.sid, rmn - k);
                D.nFix += 1;
                LOG(sf("NOLA idrop: %s got %lld of %lld, removed %lld", C.id.c_str(), got, n, rmn));
            }
            else LOG(sf("NOLA idrop: %s got exactly %lld", C.id.c_str(), got));
            if (iso && got > 0)
            {
                bool ok = false;
                auto nw2 = dNew(pw, C, ok);
                if (ok)
                {
                    s2x_is::Plan p = s2x_is::plan(want, nw2);
                    for (auto& a : p.apply)
                    {
                        bool r = E().natApplyI(nw2[(size_t)a.first].uid, a.second);
                        ilg.push_back("={" + s2x_is::show(nw2[(size_t)a.first].st) + "}->{" + s2x_is::show(a.second) + "}" + (r ? "" : "(erro)"));
                    }
                }
            }
        }
        else
        {
            D.nLose += (long long)G.size();
            long long tk = std::max(0LL, got - gone);
            if (tk > 0)
            {
                long long k = 0;
                if (iso)
                {
                    std::vector<long long> left;
                    for (auto& i : nw) left.push_back(i.n);
                    k = isRmN(pw, 0, C.sid, tk, want, nw, left, ilg);
                }
                if (tk - k > 0) rmP(C.sid, tk - k);
                LOG(sf("NOLA idrop: %s lost the race, %lld x %s taken back", C.id.c_str(), tk, C.sid.c_str()));
            }
            else LOG(sf("NOLA idrop: %s lost the race, nothing to take back", C.id.c_str()));
        }
        if (!ilg.empty()) LOG(sf("NOLA istate: %s: picked-up drop matched to its state: [%s]", C.id.c_str(), join(ilg, " ").c_str()));
    }
    for (size_t i = D.cl.size(); i-- > 0;)
        if (D.cl[i].done && now - D.cl[i].t > 20) D.cl.erase(D.cl.begin() + (long)i);
}
void dropTick()
{
    State& st = S();
    Drop& D = st.drop;
    if (!FON("idrop"))
    {
        dropClear("idrop=0");
        return;
    }
    Obj pw = E().pawn();
    if (!VAL(pw) || !dropTbl()) return;
    double now = NOW();
    std::string wk = E().worldKey();
    if (wk != D.wk)
    {
        if (!D.wk.empty() && (!D.loc.empty() || !D.pre.empty()))
        {
            LOG("NOLA idrop: world changed, local copies forgotten");
            D.loc.clear();
            D.pre.clear();
            D.cl.clear();
            D.known.clear();
            D.armed = false;
        }
        if (!D.wk.empty() && HOST() && dRbOn() && !D.kept.empty())
        {
            D.rbAt = now + F("idroprbsec", 3);
            auto g0 = E().gameSecs();
            D.rbGsOk = g0.has_value();
            if (g0) D.rbGs = *g0;
        }
        D.wk = wk;
    }
    if (HOST()) dropRollback(now);
    dropPre(now);
    dropScan(pw, now);
    if (HOST())
    {
        dropIntake(now);
        dropClaimsIn(now);
    }
    else dropSnapIn();
    dropGone(pw, now);
    dropSync(pw, now);
    dropSettle(pw, now);
    std::set<std::string> want;
    for (auto& L : D.loc) if (L.o) want.insert(L.sid);
    std::set<std::string> pk;
    if (F("idroppeak", 1) == 1)
        for (auto& C : D.cl) if (!C.done) pk.insert(C.sid);
    Counts tb;
    int k = 0;
    for (auto& s : want)
    {
        if (++k > 12) break;
        long long c = 0;
        if (dCount(pw, s, c)) tb[s] = c;
    }
    for (auto& s : pk)
    {
        long long c = 0;
        if (!dCount(pw, s, c)) continue;
        for (auto& C : D.cl) if (!C.done && C.sid == s && c > C.peak) C.peak = c;
    }
    D.tb = tb;
    D.tbi.clear();
    if (isOn() && !want.empty())
    {
        bool ok = false;
        auto v = isRead(pw, 0, ok);
        if (ok)
        {
            for (auto& s : want) D.tbi[s];
            for (auto& i : v)
                if (want.count(i.st.sid)) D.tbi[i.st.sid].insert(i.uid);
        }
    }
    auto prune = [&](std::map<std::string, double>& m, double age) {
        for (auto it = m.begin(); it != m.end();)
            if (now - it->second > age) it = m.erase(it); else ++it;
    };
    prune(D.rseen, 300);
    prune(D.useen, 300);
    for (auto it = D.done.begin(); it != D.done.end();)
        if (now - it->second > 10) it = D.done.erase(it); else ++it;
    for (size_t i = D.takes.size(); i-- > 0;)
        if (HOST() && now - D.takes[i].t > 90) D.takes.erase(D.takes.begin() + (long)i);
}
std::string dropOutR()
{
    if (!g_env || HOST() || !FON("idrop")) return "";
    Drop& D = S().drop;
    double now = NOW();
    std::vector<std::string> r;
    for (auto& L : D.loc)
    {
        if (!L.mine || !L.id.empty() || now - L.born > 20) continue;
        r.push_back(sf("%lld;%s;%lld;%.0f;%.0f;%.0f;%s", L.seq, L.sid.c_str(), L.n, L.p.x, L.p.y, L.p.z, L.mesh.c_str()) + (L.st.empty() ? std::string() : ";" + L.st));
    }
    return r.empty() ? std::string() : "#IDR:" + join(r, "|");
}
std::string dropOutU()
{
    if (!g_env || HOST() || !FON("idrop")) return "";
    Drop& D = S().drop;
    double now = NOW();
    std::vector<std::string> r;
    for (auto& C : D.cl)
        if (C.verdict == 0 && now - C.t < F("idropverdict", 12)) r.push_back(sf("%lld;%s", C.cseq, C.id.c_str()));
    return r.empty() ? std::string() : "#IDU:" + join(r, "|");
}
std::string dropOutS()
{
    if (!g_env || !HOST() || !FON("idrop")) return "";
    Drop& D = S().drop;
    double now = NOW();
    std::vector<std::string> r;
    for (auto& e : D.ents) r.push_back(sf("E;%s;%d;%lld;%s;%lld;%.0f;%.0f;%.0f;%s", e.id.c_str(), e.owner, e.seq, e.sid.c_str(), e.n, e.x, e.y, e.z, e.mesh.c_str()) + (e.st.empty() ? std::string() : ";" + e.st));
    for (auto& t : D.takes) if (now - t.t < 60) r.push_back(sf("T;%s;%d", t.id.c_str(), t.pid));
    if (r.empty()) return D.ever ? std::string("#IDS:-") : std::string();
    return "#IDS:" + join(r, "|");
}
