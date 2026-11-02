static void test_texts()
{
    using K = s2r::SteamNote::Kind;
    s2r::SteamNote n;
    n.kind = K::hosting;
    n.n = 1;
    n.max = 4;
    n.lobby = 42;
    bool a = nola_steam::text(n, nola_text::Lang::en, false) == "Your call for help is on the air on Steam (1/4): press Home to invite a friend!";
    bool b = nola_steam::text(n, nola_text::Lang::en, true) == "Your call for help is on the air on Steam (1/4): press Home to invite a friend! Lobby 42";
    n.kind = K::member_in;
    n.name = "Gal";
    n.n = 2;
    bool c = nola_steam::text(n, nola_text::Lang::en, false) == "Stalker Gal entered the Zone!";
    n.kind = K::member_out;
    n.name.clear();
    bool d = nola_steam::text(n, nola_text::Lang::en, false) == "Stalker ? left the Zone! (2/4 stalkers)";
    n.kind = K::join_fail;
    n.code = 4;
    bool e = nola_steam::text(n, nola_text::Lang::en, false) == "Couldn't reach that call on Steam: the host is full";
    n.code = 7;
    bool f = nola_steam::text(n, nola_text::Lang::en, false) == "Couldn't reach that call on Steam: this Steam account is limited and cannot join lobbies";
    n.code = s2r::steam::ENTER_NOT_NOLA;
    n.why = "not a NOLA co-op lobby";
    bool g = nola_steam::text(n, nola_text::Lang::en, false) == "Couldn't reach that call on Steam: not a NOLA co-op lobby";
    n.code = 4;
    bool h = nola_steam::text(n, nola_text::Lang::pt, false) == "N\xc3\xa3o deu para alcan\xc3\xa7" "ar esse chamado pela Steam: o host est\xc3\xa1 cheio";
    n.kind = K::joined;
    bool i = nola_steam::text(n, nola_text::Lang::en, false).empty();
    n.kind = K::none;
    bool j = nola_steam::text(n, nola_text::Lang::pt, false).empty();
    n.kind = K::unavailable;
    bool k = nola_steam::text(n, nola_text::Lang::en, false) == "Steam is silent right now. LAN co-op still works!";
    check("menu texts: hosting (with/without lobby id), member in/out, fail reasons by code, pt-BR, silent kinds", a && b && c && d && e && f && g && h && i && j && k);
}

struct MPeer
{
    FakeDll d;
    st::Link L;
    Logs lg;
    Evs ev;
    Pump pump;
    void* slot = nullptr;
    void* real = nullptr;
    s2r::Net net;
    int netport = 0;
    uint64_t id = 0;
};

static void multi_test(World& w, FakeDll& A, FakeDll& B, st::Link& LA, st::Link& LB, Logs& la, Logs& lb, Evs& ea, Evs& eb, uint64_t A_ID, uint64_t B_ID, uint64_t S_ID)
{
    const uint64_t C_ID = 76561198000000003ULL, D_ID = 76561198000000004ULL, E_ID = 76561198000000005ULL;
    w.names[C_ID] = "CeeCee";
    w.names[D_ID] = "DeeDee";
    w.names[E_ID] = "LateEe";
    A.app(480);
    B.app(480);
    LB.leave();
    check("multi: B leaves the 1-slot lobby", wait_for([&] { return LB.state() == st::State::ready && LB.lobby() == 0; }, 3));

    wchar_t cwd[MAX_PATH];
    GetCurrentDirectoryW(MAX_PATH, cwd);
    std::wstring base = std::wstring(cwd) + L"\\e2e\\";
    for (const wchar_t* dd : {L"e2e\\mh", L"e2e\\mb", L"e2e\\mc", L"e2e\\md"}) CreateDirectoryW(dd, nullptr);
    Logs ln;
    s2r::Net H;
    s2r::Config hc;
    hc.role = "host";
    hc.listen = free_port();
    hc.peer_host = "127.0.0.1";
    hc.peer_port = free_port();
    hc.maxclients = 3;
    hc.local_only = true;
    hc.loop_ms = 5;
    hc.dir = base + L"mh\\";
    check("multi: hub relay (maxclients 3, loopback only) starts", H.start(hc, ln.fn("hub")) && H.hub());

    uint64_t old = LA.lobby();
    ea.clear();
    st::HostOpts ho;
    ho.members = 4;
    ho.multi = true;
    ho.open = true;
    ho.tag = "nola-test";
    ho.relay_port = hc.listen;
    LA.host_open(ho);
    bool up = wait_for([&] { return LA.state() == st::State::hosting && LA.lobby() != 0 && LA.lobby() != old && LA.multi(); }, 5);
    uint64_t L2 = LA.lobby();
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        auto& L = w.lobbies[L2];
        check("multi: host reopens as a public dev lobby for 4 players (maxclients 3 + host), data nola/host/max/name/nola_dev",
              up && !w.member_of(old, A_ID) && L.type == 2 && L.max == 4 && L.data["nola"] == "2" && L.data["host"] == std::to_string(A_ID) && L.data["max"] == "4" &&
                  L.data["name"] == "HostGuy" && L.data["nola_dev"] == "nola-test",
              LA.status());
    }
    uint64_t dt = w.be.create_lobby(&w, S_ID, 2, 4);
    w.be.set_data(&w, S_ID, dt, "nola", "2");
    w.be.set_data(&w, S_ID, dt, "nola_dev", "other-tag");
    uint64_t df = w.be.create_lobby(&w, S_ID, 2, 1);
    w.be.set_data(&w, S_ID, df, "nola", "2");
    w.be.set_data(&w, S_ID, df, "nola_dev", "nola-test");
    w.be.set_data(&w, S_ID, df, "name", "FullGuy");
    {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        w.friends[C_ID] = {S_ID, A_ID};
        w.playing[A_ID] = {480ULL, L2};
        w.playing[S_ID] = {1643320ULL, dt};
    }

    std::vector<std::unique_ptr<MPeer>> ps;
    const wchar_t* dlls[3] = {L"fake_c.dll", L"fake_d.dll", L"fake_e.dll"};
    const uint64_t ids[3] = {C_ID, D_ID, E_ID};
    const char* tags[3] = {"C", "D", "E"};
    bool all = true;
    for (int i = 0; i < 3; ++i)
    {
        auto p = std::make_unique<MPeer>();
        p->id = ids[i];
        if (!p->d.load(LoadLibraryW(dlls[i])))
        {
            all = false;
            break;
        }
        {
            std::lock_guard<std::recursive_mutex> g(w.mu);
            w.route[ids[i]] = p->d.deliver;
        }
        p->d.init(&w.be, ids[i], 1, 1, 1);
        p->d.app(480);
        p->real = (void*)GetProcAddress(p->d.mod, "SteamAPI_RunCallbacks");
        p->slot = p->real;
        p->pump.start(&p->slot);
        st::Config c;
        c.role = "client";
        c.net_port = free_port();
        c.wait_s = 10;
        c.loop_ms = 1;
        c.poll_s = 0.05;
        c.unhook_wait_s = 2;
        c.module = p->d.mod;
        c.pump_slot = &p->slot;
        c.hook = 2 + i;
        p->netport = c.net_port;
        all &= p->L.start(c, p->lg.fn(tags[i]), {}, p->ev.fn());
        ps.push_back(std::move(p));
    }
    all = all && wait_for([&] {
              for (auto& p : ps)
                  if (p->L.state() != st::State::ready) return false;
              return true;
          }, 5);
    check("multi: three more Steam games (fake_c/d/e, hooks 2-4) ready", all && ps.size() == 3);
    if (!all || ps.size() != 3) return;
    MPeer& C = *ps[0];
    MPeer& D = *ps[1];
    MPeer& E = *ps[2];

    s2r::Net NB;
    s2r::Config bc;
    bc.role = "client";
    bc.listen = LB.net_port();
    bc.peer_host = "127.0.0.1";
    bc.peer_port = LB.port();
    bc.loop_ms = 5;
    bc.dir = base + L"mb\\";
    s2r::Config cc = bc;
    cc.listen = C.netport;
    cc.peer_port = C.L.port();
    cc.dir = base + L"mc\\";
    s2r::Config dc = bc;
    dc.listen = D.netport;
    dc.peer_port = D.L.port();
    dc.dir = base + L"md\\";
    check("multi: client relays start on their Steam links", NB.start(bc, ln.fn("nb")) && C.net.start(cc, ln.fn("nc")) && D.net.start(dc, ln.fn("nd")));

    eb.clear();
    st::FindOpts fs;
    fs.friends = false;
    fs.search = true;
    fs.tag = "nola-test";
    LB.find(fs);
    bool bj = wait_for([&] { return LB.state() == st::State::joined && LB.lobby() == L2 && LB.peer() == A_ID; }, 5);
    st::Event je;
    check("multi: B joins by lobby search on 480 (nola=2 + nola_dev tag), no friend invite", bj && eb.find(st::Event::Kind::joined, &je) && je.why == "find", LB.status());
    check("multi: search skips the full lobby, never lists another tag", lb.has("find: lobby " + std::to_string(df) + " of FullGuy is full (1/1): skipped") &&
                                                                            !lb.has(std::to_string(dt)));

    st::FindOpts ff;
    ff.friends = true;
    ff.search = false;
    C.L.find(ff);
    bool cj = wait_for([&] { return C.L.state() == st::State::joined && C.L.lobby() == L2 && C.L.peer() == A_ID; }, 5);
    check("multi: C joins through a friend playing the same appid (lobby from GetFriendGamePlayed)",
          cj && C.lg.has("friend HostGuy (76561198000000001) is in lobby " + std::to_string(L2)) && C.lg.has("find: 2 friend(s), 1 in this game, 1 lobby(s)"), C.L.status());

    D.L.join(L2);
    bool dj = wait_for([&] { return D.L.state() == st::State::joined && D.L.lobby() == L2; }, 5);
    check("multi: D joins by lobby id (the id the host shows)", dj && D.ev.find(st::Event::Kind::joined, &je) && je.why == "command" && je.n == 4 && je.max == 4);

    bool socks3 = wait_for([&] {
        int pb = LA.member_port(B_ID), pc = LA.member_port(C_ID), pd = LA.member_port(D_ID);
        return LA.members().size() == 3 && pb && pc && pd && pb != pc && pc != pd && pb != pd && LA.stats().socks == 3;
    }, 3);
    int port_b = LA.member_port(B_ID), port_c = LA.member_port(C_ID);
    check("multi: host opens one loopback socket per Steam member (3 distinct ports)", socks3, LA.status());
    {
        st::Event m;
        int n2 = 0, n3 = 0, n4 = 0;
        {
            std::lock_guard<std::mutex> g(ea.mu);
            for (auto& e : ea.v)
                if (e.kind == st::Event::Kind::member_in)
                {
                    if (e.n == 2 && e.id == B_ID) n2++;
                    if (e.n == 3 && e.id == C_ID) n3++;
                    if (e.n == 4 && e.id == D_ID) n4++;
                }
        }
        check("multi: member_in events count up 2/4, 3/4, 4/4", n2 == 1 && n3 == 1 && n4 == 1);
    }

    auto fh = make_frames("C:\\s2coop_inst\\A\\out.json", 10, 11, 5, "mc-host");
    auto fb = make_frames("C:\\s2coop_inst\\B\\out.json", 10, 12, 2, "mc-B");
    auto fc = make_frames("C:\\s2coop_inst\\B\\out.json", 10, 13, 2, "mc-C");
    auto fd = make_frames("C:\\s2coop_inst\\B\\out.json", 10, 14, 2, "mc-D");
    for (int i = 0; i < 30; ++i)
    {
        H.put_out(fh[(size_t)i % fh.size()]);
        NB.put_out(fb[(size_t)i % fb.size()]);
        C.net.put_out(fc[(size_t)i % fc.size()]);
        D.net.put_out(fd[(size_t)i % fd.size()]);
        Sleep(50);
    }
    bool admitted = wait_for([&] {
        s2r::Roster r = H.roster();
        return r.peers.size() == 3 && NB.roster().you > 0 && C.net.roster().you > 0 && D.net.roster().you > 0;
    }, 5);
    s2r::Roster hr = H.roster();
    std::set<int> pids;
    std::set<std::string> addrs;
    for (auto& e : hr.peers)
    {
        pids.insert(e.pid);
        addrs.insert(e.addr);
    }
    std::set<int> yous = {NB.roster().you, C.net.roster().you, D.net.roster().you};
    std::string rd;
    for (auto& e : hr.peers) rd += std::to_string(e.pid) + "=" + e.sid + "@" + e.addr + " ";
    check("multi: hub admits 3 Steam clients as 3 distinct pids from 3 distinct loopback addresses", admitted && pids.size() == 3 && addrs.size() == 3 && yous.size() == 3, rd);
    std::string tb, tc, td;
    uint64_t sb = 0, sc = 0, sd = 0;
    NB.take_in(tb, sb);
    C.net.take_in(tc, sc);
    D.net.take_in(td, sd);
    check("multi: host frames reach every Steam client", !tb.empty() && !tc.empty() && !td.empty());

    w.limited.insert(E_ID);
    E.ev.clear();
    E.L.join(L2);
    bool lim = wait_for([&] { return E.ev.find(st::Event::Kind::join_fail, &je, 7); }, 3);
    check("multi: limited Steam account -> join_fail code 7 (k_EChatRoomEnterResponseLimited)", lim && je.n == 0 && E.lg.has("limited"), E.L.status());
    w.limited.erase(E_ID);
    E.ev.clear();
    E.L.join(L2);
    bool full = wait_for([&] { return E.ev.find(st::Event::Kind::join_fail, &je, 4); }, 3);
    check("multi: 4th client refused, lobby full (code 4), nobody else disturbed", full && !w.member_of(L2, E_ID) && LA.members().size() == 3);
    E.ev.clear();
    E.L.find(fs);
    st::Event fe;
    bool sfull = wait_for([&] { return E.ev.find(st::Event::Kind::join_fail, &je, 4); }, 3) && E.ev.find(st::Event::Kind::found, &fe);
    check("multi: search when every tagged host is full -> found 0 (2 full), join_fail code 4 from find", sfull && fe.n == 0 && fe.code == 2 && je.n == 1,
          "found n=" + std::to_string(fe.n) + " code=" + std::to_string(fe.code));

    LA.host_limit(5, true);
    bool l5 = wait_for([&] {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        auto& L = w.lobbies[L2];
        return L.max == 5 && L.data["max"] == "5";
    }, 3);
    check("multi: maxclients change -> SetLobbyMemberLimit + lobby data max", l5 && la.has("player limit now 5"));
    LA.host_limit(4, true);
    wait_for([&] {
        std::lock_guard<std::recursive_mutex> g(w.mu);
        return w.lobbies[L2].max == 4;
    }, 3);

    ea.clear();
    D.L.leave();
    bool dl = wait_for([&] { return LA.members().size() == 2 && LA.member_port(D_ID) == 0 && LA.stats().socks == 2; }, 3);
    check("multi: one member leaves -> only its loopback socket closes, others keep their ports",
          dl && LA.member_port(B_ID) == port_b && LA.member_port(C_ID) == port_c && ea.find(st::Event::Kind::member_out, &je) && je.id == D_ID && je.n == 3, LA.status());
    uint64_t sb0 = sb;
    for (int i = 0; i < 6; ++i)
    {
        H.put_out(fh[(size_t)i % fh.size()] + std::string((size_t)i + 1, ' '));
        NB.put_out(fb[(size_t)i % fb.size()]);
        Sleep(50);
    }
    bool still = wait_for([&] { return NB.take_in(tb, sb) || sb > sb0; }, 2);
    check("multi: remaining client still gets host frames after a member left", still);

    NB.stop();
    C.net.stop();
    D.net.stop();
    H.stop();
    for (auto& p : ps)
    {
        p->L.stop();
        p->pump.stop();
    }
    bool clean = true;
    std::string cd;
    for (auto& p : ps)
    {
        auto k = p->d.counters();
        clean &= p->slot == p->real && k.registered_cbs == 0 && k.registered_results == 0 && k.off_pump == 0;
        cd += std::to_string(k.registered_cbs) + "/" + std::to_string(k.registered_results) + "/" + std::to_string(k.off_pump) + " ";
    }
    check("multi: C/D/E stop clean (slots restored, nothing registered, every call on their pump)", clean, cd);
    LB.leave();
    wait_for([&] { return LB.state() == st::State::ready; }, 3);
    A.app(1643320);
    B.app(1643320);
}
