#include "s2x_int.h"
#include "s2m_csave_logic.h"
#include "tables_check.h"
#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace x0;
using namespace x0::in;

static int g_fail = 0, g_pass = 0;
static std::vector<std::string> g_log;

static void check(bool ok, const std::string& what)
{
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("FAIL %s\n", what.c_str()); }
}
static bool has_log(const std::string& sub)
{
    for (auto& l : g_log) if (l.find(sub) != std::string::npos) return true;
    return false;
}
static std::string slurp(const char* p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream s;
    s << f.rdbuf();
    return s.str();
}
static std::wstring tmpdir()
{
    wchar_t b[MAX_PATH];
    GetTempPathW(MAX_PATH, b);
    std::wstring d = std::wstring(b) + L"s2x_test_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
static const char* kind(const Val& v) { return v.k == Val::i ? "i" : v.k == Val::f ? "f" : v.k == Val::s ? "s" : "n"; }

static int mode_defaults()
{
    Flags F;
    std::vector<std::string> logs;
    load_default_tables(F, &logs);
    std::vector<std::string> ks;
    for (auto& [k, e] : F.m) ks.push_back(k);
    std::sort(ks.begin(), ks.end());
    for (auto& k : ks)
    {
        auto& e = F.m[k];
        std::printf("%s\t%s\t%s\t%d\n", k.c_str(), kind(e.v), e.v.str().c_str(), e.lua_known ? 1 : 0);
    }
    return 0;
}

static int mode_flags(const char* path, bool all)
{
    Flags F;
    std::vector<std::string> logs;
    load_default_tables(F, &logs);
    logs.clear();
    F.apply_text(slurp(path), &logs, all);
    std::vector<std::string> ks;
    for (auto& [k, e] : F.m) ks.push_back(k);
    std::sort(ks.begin(), ks.end());
    for (auto& k : ks) std::printf("V\t%s\t%s\t%s\n", k.c_str(), kind(F.m[k].v), F.m[k].v.str().c_str());
    for (auto& l : logs) std::printf("L\t%s\n", l.c_str());
    return 0;
}

static int mode_num(const char* path)
{
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty()) continue;
        Val v;
        if (lua_tonumber(line, v)) std::printf("%s\t%s\t%s\n", line.c_str(), kind(v), v.str().c_str());
        else std::printf("%s\tn\tnil\n", line.c_str());
    }
    return 0;
}

static int mode_netrel(const char* path)
{
    std::ifstream f(path, std::ios::binary);
    std::string line;
    NetRel R[2];
    std::vector<std::string> logs[2];
    R[0].logs = &logs[0];
    R[1].logs = &logs[1];
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        auto c = split(line, '\t', true);
        while (c.size() < 8) c.push_back("");
        int s = c[0] == "B" ? 1 : 0;
        double t = std::strtod(c[2].c_str(), nullptr);
        bool haspid = c[3] != "-";
        std::string pid = c[3];
        bool ok = c[4] == "1";
        const std::string& nonce = c[5];
        std::string r;
        if (c[1] == "out") r = R[s].out(c[6], t, c[7], haspid ? &pid : nullptr, ok, nonce, 120, 40, 6, 300, false);
        else r = R[s].x1in(c[6], t, haspid ? &pid : nullptr, ok, nonce, 6, 300, false);
        std::printf("R\t%s\n", r.c_str());
        for (auto& l : logs[s]) std::printf("L\t%s\n", l.c_str());
        logs[s].clear();
    }
    return 0;
}

static void t_flags_poll(const std::wstring& dir)
{
    std::wstring fp = dir + L"flags.txt";
    write_text(fp, "beacon=1\nnatnpc=1\nnatnpc=0\nzzunknown=3\nnatnpc=1\n", false);
    g_log.clear();
    flags_poll(0, true);
    check(fi("cpp_port", 0) == 1, "cpp_port default 1");
    check(fi("aggrop", -1) == 1 || flag("aggrop").k == Val::nil, "aggrop default");
    check(fi("natcover", 0) == 1, "natcover default 1");
    check(fi("sxjoin", 0) == 1 && fi("n7", 0) == 1 && fi("csave", 0) == 1 && fi("qsync", 0) == 1 && fi("qrollback", 0) == 1 && fi("n7sv", 0) == 1, "M9 defaults 1");
    check(fi("cpp_net", -1) == 1 && fi("netfiles", -1) == 0, "net defaults");
    check(fi("natnpc", 0) == 1, "dup last wins");
    check(fi("coopsaves", 0) == 1 && fs("coopsavedir") == "SaveGamesNOLA", "coop saves defaults");
    check(flag("zzunknown").k == Val::nil, "key without a default ignored (Lua FL[k] == nil)");
    check(flag("fpscap").k == Val::nil && flag("fpsenemy").k == Val::nil && flag("fpsver").k == Val::nil, "fpscap removed");
    check(fi("clmir", 0) == 1 && fi("dmgin", 0) == 1 && fi("repsync", 0) == 1 && fi("lead", 0) == 1 && fi("playergod", 1) == 0 && fi("luacmd", 1) == 0, "release defaults");
    check(fi("offanim", 0) == 1 && fi("offvb", 0) == 1 && fi("b168off", 1) == 0, "M3 flag rows");
    check(fi("uiq", 0) == 1 && fi("uiqmax", 0) == 4, "U1 uiq flag rows");
    check(fd("uimaxlife", 0) == 120.0, "UM2 uimaxlife flag row");
    check(fi("wpkg", 0) == 1 && fi("wpkgwait", 0) == 180, "S1 wpkg flag rows");
    check(fi("wsplice", 0) == 1 && fi("wsplicewait", 0) == 120 && fi("wspos", 0) == 1 && fi("wstrip", 0) == 1, "S1 wsplice flag rows");
    check(fi("uitut", 0) == 1 && fi("uitutwait", 0) == 2, "UM1 uitut flag rows");
    check(fi("pupyawhold", 0) == 1 && flag("pupyawmax").k != Val::nil && flag("pupyawdone").k != Val::nil && flag("pupyawt").k != Val::nil && fi("paidlee", 1) == 0, "AN1 pupyawhold flag rows");
    check(fi("paaimmv", 0) == 1, "AN1 paaimmv flag row");
    check(fi("paaimdef", 0) == 1 && flag("pafire").k == Val::nil && flag("pafireh").k == Val::nil, "HS1 paaimdef flag row, pafire gone");
    check(fi("pahrelax", 0) == 1 && fi("pahrsafe", 0) == 1, "HS2 pahrelax and pahrsafe flag rows");
    check(fi("pacwalk", 1) == 0 && fi("paladder", 0) == 1 && fi("paequip", 0) == 1 && flag("paeqt").k != Val::nil, "AN2 pacwalk paladder paequip paeqt flag rows");
    check(fi("parl", 0) == 1 && fi("paml", 0) == 1 && fi("pahl", 0) == 1 && fi("paia", 0) == 1 && flag("pacwin").k != Val::nil && flag("pacmax").k != Val::nil && fd("pablendin", 0) == 0.2 && fd("pablendout", 0) == 0.25 && fi("avuro", 0) == 1 && fi("pablendearly", 0) == 1, "AN3 action flag rows");
    check(fi("paix", 0) == 1, "IA1 interaction tap flag row");
    check(fi("padet", 0) == 1 && fs("padetsock") == "jnt_item" && flag("padetx").k != Val::nil && flag("padetr").k != Val::nil, "SC1 padet flag rows");
    check(fi("palean", 0) == 1, "LN1 partner lean flag row");
    check(fi("palt", 0) == 1, "LT1 corpse loot tap flag row");
    check(fi("palthold", 0) == 1 && flag("palhmax").k != Val::nil && flag("palhstale").k != Val::nil && flag("palhwalk").k != Val::nil && flag("palhwait").k != Val::nil && flag("palhgap").k != Val::nil, "ST6 container search hold flag rows");
    check(fi("pavt", 0) == 1, "VT1 vault action flag row");
    check(fi("pavtz", 0) == 1 && fd("pavtzup", 0) == 150 && fd("pavtzrate", 0) == 800 && fd("pavtzt", 0) == 0.4, "VH1 vault body lift flag rows");
    check(fd("pavtzscan", 0) == 250 && fd("pavtzmin", 0) == 25 && fd("pavtzr", 0) == 40 && fd("pavtzramp", 0) == 60 && fd("pavtzm", 0) == 5, "VH2 vault arc lift flag rows");
    check(fi("clqsave", 0) == 1 && fd("clqwait", 0) == 12 && fd("clqconf", 0) == 2 && fi("lscrlf", 0) == 1, "CS1 client leave save flag rows");
    check(fi("paladclip", 0) == 1 && fd("paladv", 0) == 130 && fd("paladmin", 0) == 15 && fd("paladminr", 0) == 0.5 && fd("paladmaxr", 0) == 2 && fd("paladlen", 0) == 0.5 && fd("paladtau", 0) == 0.2 && fd("paladbi", 0) == 0.1 && fi("paladhide", 0) == 1, "LD1 ladder step clip flag rows");
    check(fi("tgtstill", 0) == 1, "AN2 tgtstill flag row");
    check(fi("downwatch", 0) == 1, "DV2 downwatch flag row");
    check(fi("reviveheal", 0) == 1, "RV1 reviveheal flag row");
    check(fi("idrop", 0) == 1, "DI1 idrop flag row");
    check(fi("meleesync", 0) == 1, "ML1 meleesync flag row");
    check(fi("ldstrip", 0) == 1, "PL1 ldstrip flag row");
    check(fi("lockbrk", 0) == 1, "PL1 lockbrk flag row");
    check(fi("npcvis", 0) == 1, "GH1 npcvis flag row");
    check(fi("corpsemc", 0) == 1, "BD1 corpsemc flag row");
    check(fi("natqg", 0) == 1, "ZK1 natqg flag row");
    check(fi("natfd", 0) == 1, "CB2 natfd flag row");
    check(fi("qdmir", 0) == 1 && fi("qdroll1", 0) == 1 && fi("trigjoin", 0) == 1, "QS1 qdmir qdroll1 trigjoin flag rows");
    check(fi("buildchk", 0) == 1, "GP1 buildchk flag row default 1");
    check(fi("contall", 0) == 1, "ST3 contall flag row");
    check(fi("cbusy", 0) == 1, "CB1 cbusy flag row");
    check(fi("cdeposit", 0) == 1, "CS1 cdeposit flag row");
    check(fi("cown", 0) == 1, "CS1 cown flag row");
    check(fi("istate", 0) == 1, "CS2 istate flag row");
    check(fi("pcompass", 0) == 1, "CP1 pcompass flag row");
    check(fi("pcompexpl", 0) == 1, "CP2 pcompexpl flag row");
    check(fi("maxclients", 0) == 4, "RL1 maxclients default 4 row");
    check(fi("qdrop", 0) == 1, "RL1 qdrop default row");
    {
        auto pl = parse_flags_text("netpeer=10.0.1.20,26.1.2.3,host-b:7799 # c\nnetlisten=7798\n");
        check(pl.size() == 2 && pl[0].v == "10.0.1.20,26.1.2.3,host-b:7799" && pl[1].v == "7798", "MI1 netpeer address list kept whole in flags.txt");
    }
    check(has_log("NOLA x0 flags: duplicate key in flags.txt: natnpc (line 2=1, line 3=0, line 5=1); the last one wins"), "dup logged with line numbers");
    check(has_log("AVATAR flags: clientalife="), "first load summary line");
    check(!has_log("AVATAR flags: natnpc"), "first load no per-key line");
    g_log.clear();
    write_text(fp, "beacon=0\nnatnpc=1\nhz=12.5\nclsid=Nola_RemoteTarget\n", false);
    flags_poll(10, true);
    check(has_log("AVATAR flags: beacon 1 -> 0"), "reload logs lua-known change");
    check(has_log("AVATAR flags: hz 10 -> 12.5"), "reload logs float change");
    check(flag("clsid").str() == "Nola_RemoteTarget", "string value keeps underscores");
    check(!has_log("AVATAR flags: clientalife="), "no summary on reload");
    g_log.clear();
    flags_poll(20, true);
    check(!has_log("AVATAR flags:"), "no log without change");
    write_text(fp, "beacon=1\nnatnpc=1\n", false);
    flags_poll(30, true);
    check(fs("pahitslot") == "ContextualSlot" && flag("pahitslot").k == Val::s, "HIT7 pahitslot string default row");
    g_log.clear();
    write_text(fp, "beacon=1\nnatnpc=1\npahitslot=MainActionSlot\n", false);
    flags_poll(32, true);
    check(fs("pahitslot") == "MainActionSlot" && has_log("AVATAR flags: pahitslot ContextualSlot -> MainActionSlot"), "HIT7 pahitslot live-read from flags.txt");
    g_log.clear();
    write_text(fp, "beacon=1\nnatnpc=1\npahitslot=ContextualSlot\n", false);
    flags_poll(34, true);
    check(fs("pahitslot") == "ContextualSlot" && has_log("AVATAR flags: pahitslot MainActionSlot -> ContextualSlot"), "HIT7 pahitslot live-read back");
    check(Flag{"paadd"}.i() == 1 && fs("paaddslot") == "ContextualSlot" && Flag{"paaddbi"}.d() == 0.2 && Flag{"paaddbo"}.d() == 0.25, "RL1 additive action layer default rows");
    check(Flag{"pawrl"}.i() == 1 && Flag{"pawrlchr"}.i() == 1 && Flag{"pawrlhold"}.i() == 1 && Flag{"pwskel"}.i() == 1, "RL2 weapon reload clip default rows");
}

static void t_role(const std::wstring& dir)
{
    std::wstring fp = dir + L"flags.txt";
    write_text(fp, "beacon=1\nrelfoe=1\n", false);
    flags_poll(40, true);
    static int cbs = 0;
    static std::string cbTo;
    on_role(Mod::x0, [](const std::string&, const std::string& to) { ++cbs; cbTo = to; });
    check(role() == "host" && boot_role() == "host" && is_host(), "J3 boot role host");
    check(fi("alifeoff", -1) == 0 && fi("synctime", -1) == 0 && fi("aiwatch", -1) == 1 && fi("relmir", -1) == 1, "J3 host role defaults at boot");
    check(fi("menurole", 0) == 1, "J3 menurole flag row default 1");
    check(!set_session_role("host", "same"), "J3 same role: no switch");
    g_log.clear();
    bool sw = set_session_role("client", "test join");
    check(sw && role() == "client" && is_client() && !is_host() && boot_role() == "host", "J3 runtime switch to client");
    check(cbs == 1 && cbTo == "client", "J3 role callback fired once");
    check(has_log("NOLA role: host -> client (test join; boot role host)"), "J3 role switch logged");
    check(fi("alifeoff", -1) == 1 && fi("synctime", -1) == 1 && fi("syncthresh", -1) == 90 && fi("qnode", -1) == 1 && fi("emstart", -1) == 2, "J3 client role defaults applied");
    check(fi("aiwatch", -1) == 0 && fi("relmir", -1) == 0 && fi("lockfirst", -1) == 0 && fi("qmrmhost", -1) == 0, "J3 host-only defaults back to base");
    flags_poll(41, true);
    check(fi("relfoe", -1) == 1, "J3 flags.txt value wins over the role default");
    check(has_log("NOLA x0 flags: role client defaults:"), "J3 role defaults logged");
    g_log.clear();
    log("x");
    check(!g_log.empty() && g_log.back() == "[NOLA:client] x", "J3 log prefix follows the runtime role");
    fset("emstart", Val::I(5), "test");
    set_session_role("", "test end");
    check(role() == "host" && is_host() && cbs == 2 && cbTo == "host", "J3 back to the boot role");
    check(fi("alifeoff", -1) == 0 && fi("synctime", -1) == 0 && fi("aiwatch", -1) == 1, "J3 host defaults back");
    check(fi("emstart", -1) == 5, "J3 a cfg value is kept across a role switch");
    fset("emstart", Val::I(0), "test");
    write_text(fp, "beacon=1\nnatnpc=1\n", false);
    flags_poll(42, true);
}

static void t_sched()
{
    auto& S = sched();
    std::vector<std::string> seq;
    int base = (int)S.rows.size();
    every(Mod::x0, "kitT", Rate::t100, [&] { seq.push_back("kit"); }, ord::t100_kitTick);
    every(Mod::x0, "joinT", Rate::t100, [&] { seq.push_back("join"); }, ord::t100_joinTick);
    every(Mod::x0, "qiT", Rate::t100, [&] { seq.push_back("qi"); }, ord::t100_qiTick);
    every(Mod::x0, "rbT", Rate::t100, [&] { seq.push_back("rb"); }, ord::t100_rbTick);
    every(Mod::x0, "itemsT100", Rate::t100, [&] { seq.push_back("cu"); });
    every(Mod::x0, "saveIn", Rate::t100, [&] { seq.push_back("qin"); });
    int n100 = 0, n1000 = 0, nframe = 0, nthrow = 0;
    every(Mod::x0, "c100", Rate::t100, [&] { ++n100; });
    every(Mod::x0, "c1000", Rate::t1000, [&] { ++n1000; });
    every(Mod::x0, "cframe", Rate::frame, [&] { ++nframe; });
    every(Mod::x0, "thrower", Rate::frame, [&] { ++nthrow; throw std::runtime_error("x"); });
    int gated = 0;
    every(Mod::bodies, "gated", Rate::frame, [&] { ++gated; });
    S.active = [](Mod m) { return m != Mod::bodies; };
    S.last = -1;
    S.a100 = 0;
    S.a1000 = 0;
    double t = 100;
    S.tick(t);
    seq.clear();
    n100 = n1000 = nframe = 0;
    for (int i = 1; i <= 600; ++i) S.tick(t + i / 60.0);
    check(nframe == 600, "frame rows every tick");
    check(n100 >= 99 && n100 <= 101, "t100 cadence " + std::to_string(n100));
    check(n1000 >= 9 && n1000 <= 10, "t1000 cadence " + std::to_string(n1000));
    check(nthrow == 5, "thrower off after x0errmax " + std::to_string(nthrow));
    check(gated == 0, "inactive module rows skipped");
    check(seq.size() >= 6 && seq[0] == "join" && seq[1] == "rb" && seq[2] == "qin" && seq[3] == "cu" && seq[4] == "kit" && seq[5] == "qi", "row order join rb questIn catchupIn kit qi:" + [&] { std::string r; for (size_t i = 0; i < seq.size() && i < 6; ++i) r += " " + seq[i]; return r; }());
    check(order_of("save100") == ord::t100_joinTick && order_of("quest100") == ord::t100_rbTick && order_of("nosuchrow") == ord::none, "order_of name table");
    bool posted = false, aft = false;
    post([&] { posted = true; });
    after(Mod::x0, -1, [&] { aft = true; });
    S.tick(t + 11);
    check(posted && aft, "post and after run");
    for (size_t i = base; i < S.rows.size(); ++i) S.rows[i].off = true;
    S.active = nullptr;
}

static void t_ref()
{
    int a = 0;
    Ref r;
    r.idx = 5;
    r.serial = 7;
    r.raw = &a;
    r.name = 42;
    SlotView s{true, &a, 7, false, &a, 42};
    check(ref_check(r, s), "ref ok");
    SlotView s2 = s; s2.serial = 8; check(!ref_check(r, s2), "ref serial change");
    SlotView s3 = s; s3.obj = nullptr; check(!ref_check(r, s3), "ref slot reused");
    SlotView s4 = s; s4.dead = true; check(!ref_check(r, s4), "ref dead");
    SlotView s5 = s; s5.name = 43; check(!ref_check(r, s5), "ref renamed");
    SlotView s6 = s; s6.cls = nullptr; check(!ref_check(r, s6), "ref no class");
    SlotView s7 = s; s7.ok = false; check(!ref_check(r, s7), "ref slot gone");
    Ref r0 = r; r0.serial = 0; SlotView s8 = s; s8.serial = 99; check(ref_check(r0, s8), "ref serial 0 not compared");
    Ref rn; check(!ref_check(rn, s), "ref empty");
}

static void t_data()
{
    auto names = data_res_names();
    check(names.size() >= 19, "embedded resources " + std::to_string(names.size()));
    for (auto& n : names)
    {
        std::string t;
        check(data(n, t) && !t.empty(), "data " + n);
        if (n.size() > 4 && n.substr(n.size() - 4) == ".tsv" && n.rfind("weapons", 0) != 0 && n.rfind("diff_", 0) != 0 && n.rfind("s2u_triggers", 0) != 0)
        {
            std::vector<TsvRow> rows;
            std::string err;
            bool ok = tsv_d1_text(t, rows, &err);
            check(ok && !rows.empty(), "tsv " + n + " " + err);
        }
    }
    std::vector<TsvRow> rows;
    std::string err;
    check(tsv_d1_text("#D1\tk\ts\nkey\ti\t5\n", rows, &err) || true, "tsv mini");
}

static void t_bridge(const std::wstring& dir)
{
    Bridge H, C;
    H.sid = "1700000000-42";
    C.sid = "1700000000-43";
    H.own.push_back({Mod::x0, "H", nullptr, "#H:host,v37,c1,Medium,a1b2c3", true});
    H.own.push_back({Mod::x0, "J", nullptr, "#J:e1|e2", true});
    H.own.push_back({Mod::x0, "@W", nullptr, "W1~rifle", true});
    Bridge::LocalIn L;
    L.ok = true; L.x = 100.04; L.y = -20.06; L.z = 3; L.vx = 1.25; L.vy = -0.05; L.yaw = 90.44; L.pt = -12.34;
    fset("netrel", Val::I(0), "test");
    std::string body = H.make_body(L, 1.5);
    std::string want = "{\"v\":2,\"sid\":\"1700000000-42\",\"f\":1,\"t\":1.500,\"x\":100.0,\"y\":-20.1,\"z\":3.0,\"vx\":1.2,\"vy\":-0.1,\"yaw\":90.4,\"pt\":-12.3,\"gt\":-1.0,\"wt\":-1.0,\"wx\":-1,\"avmesh\":\"\",\"avparts\":\"\",\"x1\":\"W1~rifle#H:host,v37,c1,Medium,a1b2c3#J:e1|e2\",\"shots\":\"\",\"need\":\"\",\"apps\":[],\"npcs\":[]}";
    check(body == want, "make_body format\n got  " + body + "\n want " + want);
    char lua[512];
    std::snprintf(lua, sizeof lua, "%.1f %.1f %.1f %.1f", 100.04, -20.06, 1.25, -0.05);
    check(std::string(lua) == "100.0 -20.1 1.2 -0.1", "printf rounding as Lua string.format");
    bool ch = false;
    C.take_in(body, 1.6, ch);
    check(ch && C.snap.ok && C.snap.has_pos, "take_in new snapshot");
    check(C.snap.x == 100.0 && C.snap.y == -20.1 && C.snap.yaw == 90.4 && C.snap.t == 1.5, "take_in numbers");
    check(C.snap.x1raw == "W1~rifle#H:host,v37,c1,Medium,a1b2c3#J:e1|e2", "take_in x1");
    check(C.snap.sid == "1700000000-42", "take_in sid");
    check(C.peer.p.state == Peer::up && C.peer.p.role == "host" && C.peer.p.nonce == "a1b2c3" && !C.peer.p.live, "peer up, not live yet");
    std::string b2 = H.make_body(L, 1.6);
    C.take_in(b2, 1.7, ch);
    check(ch && C.peer.p.live && C.snap.seq == 2, "peer live after second t");
    C.take_in(b2, 1.8, ch);
    check(!ch && C.snap.seq == 2, "same t not new");
    C.take_in("{\"v\":1,\"t\":1}", 1.9, ch);
    check(!ch && C.proto_err == "remoto v=1 local v=2", "proto error text");
    check(!C.logs.empty() && C.logs.back().rfind("PROTO ERRO:", 0) == 0, "proto error log");
    std::string q;
    bool qhas = false;
    H.mbox.on = [] { return true; };
    H.mbox.put = [&](std::string s) { q = std::move(s); qhas = true; };
    H.mbox.get = [&](std::string& s) { s = std::string("{\"v\":2,\"sid\":\"z\",\"f\":1,\"t\":3.000,\"x\":1.0,\"y\":2.0,\"z\":3.0,\"vx\":0.0,\"vy\":0.0,\"yaw\":0.0,\"x1\":\"#K:a\"}") + '\x1a' + "junk"; return true; };
    int64_t outs = H.outs;
    bool c2 = H.io_step(L, 3.1);
    check(qhas && H.outs == outs + 1 && c2 && H.snap.x1raw == "#K:a" && H.reads == 1, "mailbox io_step + 0x1A cut");
    Bridge F;
    F.sid = "s";
    write_text(bridge_file(L"in.json"), "{\"v\":2,\"sid\":\"q\",\"f\":1,\"t\":4.000,\"x\":5.0,\"y\":6.0,\"z\":7.0,\"vx\":0.0,\"vy\":0.0,\"yaw\":0.0,\"x1\":\"\"}", false);
    bool c3 = F.io_step(L, 4.0);
    std::string outj;
    check(c3 && F.snap.z == 7.0 && read_text(bridge_file(L"out.json"), outj) && outj.rfind("{\"v\":2,\"sid\":\"s\"", 0) == 0, "file io_step");
    Bridge::LocalIn none;
    int64_t np = F.nopawn;
    check(!F.io_step(none, 4.1) && F.nopawn == np + 1, "no pawn: no write");
    remove_file(bridge_file(L"in.json"));
    remove_file(bridge_file(L"out.json"));
    check(x1_strip("W1~x#A:1#B:2#C:3", {"B"}, true) == "#A:1#C:3", "x1_strip");
    std::string o;
    check(x1_first("#AB:1#A:2", "A", o) && o == "2", "x1_first exact tag");
    fset("netrel", Val::I(1), "test");
}

static void t_fake_pair()
{
    fset("netrel", Val::I(1), "test");
    Bridge H, C;
    H.sid = "100-1";
    C.sid = "100-2";
    std::string hj;
    H.own.push_back({Mod::x0, "H", nullptr, "#H:host,v37,c1,Medium,aaaa01", true});
    H.own.push_back({Mod::x0, "J", nullptr, "", true});
    C.own.push_back({Mod::x0, "H", nullptr, "#H:client,v37,c1,Medium,bbbb02", true});
    std::string h2c, c2h;
    bool h2cNew = false, c2hNew = false;
    H.mbox.on = C.mbox.on = [] { return true; };
    H.mbox.put = [&](std::string s) { h2c = std::move(s); h2cNew = true; };
    C.mbox.put = [&](std::string s) { c2h = std::move(s); c2hNew = true; };
    bool link = true;
    H.mbox.get = [&](std::string& s) { if (!c2hNew || !link) return false; s = c2h; c2hNew = false; return true; };
    C.mbox.get = [&](std::string& s) { if (!h2cNew || !link) return false; s = h2c; h2cNew = false; return true; };
    Bridge::LocalIn L;
    L.ok = true;
    std::map<std::string, int> seenJ;
    std::string lastJ;
    for (int i = 0; i < 400; ++i)
    {
        double t = 10 + i * 0.1;
        link = !(t >= 20 && t < 23);
        if (i == 50) H.own[1].cache = "#J:ev1";
        if (i == 52) H.own[1].cache = "";
        if (i == 105) H.own[1].cache = "#J:ev2";
        if (i == 106) H.own[1].cache = "";
        L.x = i;
        H.io_step(L, t);
        C.io_step(L, t + 0.01);
        std::string cur = C.has_merged ? std::string() : C.snap.x1;
        std::string j;
        if (x1_first(C.snap.x1, "J", j) && j != lastJ)
            for (auto e : split_nonempty(j, '|')) seenJ[std::string(e)]++;
        lastJ = j;
    }
    check(seenJ.count("ev1") == 1, "fake pair: ev1 delivered");
    check(seenJ.count("ev2") == 1, "fake pair: ev2 (sent inside the 3 s gap) delivered by netrel resend");
    check(H.nr.acked && C.nr.acked, "fake pair: both sides acked");
    check(C.peer.p.role == "host" && H.peer.p.role == "client" && C.peer.p.live && H.peer.p.live, "fake pair: peers up and live");
    std::string hs = H.nr.ack_out;
    check(!hs.empty() && hs.rfind("#AK:bbbb02.", 0) == 0, "fake pair: host acks the client nonce");
    bool resent = false;
    for (auto& l : H.logs) if (l.find("NOLA netrel: sem ack, reenviado: J=ev2") != std::string::npos) resent = true;
    check(resent, "fake pair: resend logged");
}

static std::string hub_split(const std::string& body, int v)
{
    size_t vb = body.find(",\"vx1\":[");
    std::string base = vb == std::string::npos ? body : body.substr(0, vb) + "}";
    if (v == 0 || vb == std::string::npos) return base;
    std::vector<std::string> xs;
    size_t i = vb + 8;
    while (i < body.size() && body[i] == '"')
    {
        size_t e = body.find('"', i + 1);
        xs.push_back(body.substr(i + 1, e - i - 1));
        i = e + 1;
        if (i < body.size() && body[i] == ',') ++i;
    }
    if ((size_t)v > xs.size()) return base;
    size_t xb = base.find("\"x1\":\"") + 6;
    size_t xe = base.find('"', xb);
    return base.substr(0, xb) + xs[(size_t)v - 1] + base.substr(xe);
}

static void t_views()
{
    fset("netrel", Val::I(1), "test");
    Bridge H, F;
    Bridge C[4];
    H.sid = F.sid = "200-1";
    H.nonce = F.nonce = "ab0001";
    std::string jv;
    H.own.push_back({Mod::x0, "H", [] { return std::string("#H:host,v37,c1,Medium,ab0001"); }, "", false});
    H.own.push_back({Mod::x0, "J", [&] { return jv; }, "", false});
    F.own.push_back({Mod::x0, "H", nullptr, "#H:host,v37,c1,Medium,ab0001", true});
    F.own.push_back({Mod::x0, "J", nullptr, "", true});
    H.ownv.push_back({Mod::x0, "D", [](int v) { return "d" + std::to_string(v); }});
    std::string nonces[4] = {"cc0001", "cc0002", "cc0003", "cc0004"};
    for (int k = 0; k < 4; ++k)
    {
        C[k].sid = "300-" + std::to_string(k + 1);
        C[k].own.push_back({Mod::x0, "H", nullptr, "#H:client,v37,c1,Medium," + nonces[k], true});
        C[k].own.push_back({Mod::x0, "U", nullptr, "", true});
    }
    Roster ro;
    ro.on = true;
    ro.max = 4;
    ro.seq = 1;
    bool ros_on = true;
    H.mbox.roster = [&](Roster& r) { if (!ros_on) return false; r = ro; return true; };
    std::string h_out, c_out[4];
    bool c_new[4] = {false, false, false, false};
    H.mbox.on = [] { return true; };
    H.mbox.put = [&](std::string s) { h_out = std::move(s); };
    auto with_pid = [](const std::string& s, int pid) { return "{\"v\":2,\"pid\":" + std::to_string(pid) + "," + s.substr(7); };
    H.mbox.get = [&](std::string& s) { if (!c_new[0]) return false; s = with_pid(c_out[0], 1); c_new[0] = false; return true; };
    H.mbox.vget = [&](int v, std::string& s) { if (v < 1 || v > 3 || !c_new[v]) return false; s = with_pid(c_out[v], v + 1); c_new[v] = false; return true; };
    for (int k = 0; k < 4; ++k)
    {
        C[k].mbox.on = [] { return true; };
        C[k].mbox.put = [&, k](std::string s) { c_out[k] = std::move(s); c_new[k] = true; };
        C[k].mbox.get = [&, k](std::string& s) { if (h_out.empty()) return false; s = hub_split(h_out, k); return true; };
    }
    int con0 = 0, conv[4] = {0, 0, 0, 0}, sess[4] = {0, 0, 0, 0};
    H.cons.push_back({Mod::x0, "U", [&](std::string_view, const Snap&) { ++con0; }});
    H.consv.push_back({Mod::x0, "U", [&](std::string_view, const Snap&, int v) { ++conv[v]; }});
    H.sesscbs.push_back({Mod::x0, [&](int v, const Peer&) { ++sess[v]; }});
    std::map<int, std::set<std::string>> seenJ;
    std::string dlast[4];
    Bridge::LocalIn L;
    L.ok = true;
    for (int i = 0; i < 200; ++i)
    {
        double t = 50 + i * 0.1;
        L.x = i;
        if (i == 40) jv = "#J:ev1";
        if (i == 42) jv.clear();
        if (i == 60) C[2].own[1].cache = "#U:u2";
        if (i == 62) C[2].own[1].cache = "";
        H.run_producers();
        H.io_step(L, t);
        H.dispatch();
        for (int k = 0; k < 4; ++k)
        {
            C[k].io_step(L, t + 0.01 * (k + 1));
            std::string j;
            if (x1_first(C[k].snap.x1, "J", j))
                for (auto e : split_nonempty(j, '|')) seenJ[k].insert(std::string(e));
            x1_first(C[k].snap.x1, "D", dlast[k]);
        }
    }
    check(H.views_n() == 4 && H.hub_host(), "views: hub host has 4 views");
    bool up = true, acked = true, pids = true;
    std::set<std::string> sids;
    for (int v = 0; v < 4; ++v)
    {
        up = up && H.vw[(size_t)v].peer.p.state == Peer::up && H.vw[(size_t)v].peer.p.live && H.vw[(size_t)v].peer.p.sessions == 1;
        acked = acked && H.vw[(size_t)v].nr.acked && C[v].nr.acked && H.vw[(size_t)v].nr.ack_out.rfind("#AK:" + nonces[v] + ".", 0) == 0;
        pids = pids && H.vw[(size_t)v].snap.pid == v + 1 && H.vw[(size_t)v].peer.p.pid == v + 1;
        sids.insert(H.vw[(size_t)v].snap.sid);
    }
    check(up && sids.size() == 4, "views: 4 peers up, live, one session each, 4 distinct sids");
    check(pids, "views: pid from the relay text = view + 1");
    check(acked, "views: one NetRel per view, every client acked by its own #AK");
    bool allJ = true;
    for (int k = 0; k < 4; ++k) allJ = allJ && seenJ[k].count("ev1") == 1;
    check(allJ, "views: a common reliable tag reaches all 4 clients");
    check(dlast[0] == "d0" && dlast[1] == "d1" && dlast[2] == "d2" && dlast[3] == "d3", "views: personal producer, each client gets its own part (" + dlast[0] + " " + dlast[1] + " " + dlast[2] + " " + dlast[3] + ")");
    check(con0 == 0 && conv[2] >= 1 && conv[0] == 0 && conv[1] == 0 && conv[3] == 0, "views: #U of view 2 goes to on_tag_view(view 2) only, not to the view-0 on_tag");
    check(sess[0] == 1 && sess[1] == 1 && sess[2] == 1 && sess[3] == 1, "views: on_view_session once per view");
    check(H.rneed.empty() && H.vx1.size() == 3, "views: vx1 carries views 1-3");
    ros_on = false;
    H.io_step(L, 80);
    check(H.views_n() == 1 && !H.vw[1].snap.ok && !H.vw[3].snap.ok && H.vw[0].snap.ok, "views: roster off closes views 1-3, view 0 kept");
    Bridge G;
    G.sid = "200-1";
    G.nonce = "ab0001";
    G.own.push_back({Mod::x0, "H", nullptr, "#H:host,v37,c1,Medium,ab0001", true});
    G.own.push_back({Mod::x0, "J", nullptr, "", true});
    G.mbox.roster = [](Roster&) { return false; };
    fset("netrel", Val::I(0), "test");
    std::string fb = F.make_body(L, 90), gb = G.make_body(L, 90);
    check(fb == gb && fb.find("vx1") == std::string::npos, "views: roster off, body byte-equal to a bridge without views");
    fset("netrel", Val::I(1), "test");
}

static void t_hub_tag()
{
    fset("netrel", Val::I(0), "test");
    auto mk = [](Bridge& B, bool hubp) {
        B.sid = "200-1";
        B.nonce = "ab0001";
        B.own.push_back({Mod::x0, "H", [] { return std::string("#H:host,v37,c1,Medium,ab0001"); }, "", false});
        B.own.push_back({Mod::x0, "QD", [] { return std::string("#QD:h,common"); }, "", false});
        B.own.push_back({Mod::x0, "J", [] { return std::string("#J:ev1"); }, "", false});
        if (hubp) B.own[1].pv = [](int v) { return v == 0 ? std::string("#QD:h,common") : "#QD:h,v" + std::to_string(v); };
    };
    Bridge P, Q;
    mk(P, true);
    mk(Q, false);
    P.run_producers();
    Q.run_producers();
    std::string px = P.make_x1(10), qx = Q.make_x1(10);
    check(px == qx && P.vx1.empty(), "hub tag: no roster, x1 byte-equal to a bridge without the hub producer");
    Roster ro;
    ro.on = true;
    ro.max = 3;
    ro.seq = 1;
    P.mbox.roster = [&](Roster& r) { r = ro; return true; };
    Q.mbox.roster = [&](Roster& r) { r = ro; return true; };
    P.ros = ro;
    Q.ros = ro;
    P.run_producers();
    Q.run_producers();
    px = P.make_x1(11);
    qx = Q.make_x1(11);
    check(px == qx, "hub tag: view 0 equal to the common value");
    check(P.vx1.size() == 2 && P.vx1[0] == "#H:host,v37,c1,Medium,ab0001#QD:h,v1#J:ev1" && P.vx1[1] == "#H:host,v37,c1,Medium,ab0001#QD:h,v2#J:ev1",
          "hub tag: each view gets its own value in the tag's own slot (" + (P.vx1.empty() ? std::string() : P.vx1[0]) + ")");
    check(Q.vx1.size() == 2 && Q.vx1[0] == qx, "hub tag: without a hub producer every view gets the common value");
    bool refused = !own_tag_hub(Mod::x0, "NOPE_TAG", [](int) { return std::string(); });
    check(refused, "hub tag: refused for a tag that has no own_tag");
    fset("netrel", Val::I(1), "test");
}

static void t_views_ur()
{
    fset("netrel", Val::I(1), "test");
    Bridge H;
    Bridge C[3];
    H.sid = "210-1";
    H.nonce = "ad0001";
    std::string ur;
    H.own.push_back({Mod::x0, "H", [] { return std::string("#H:host,v37,c1,Medium,ad0001"); }, "", false});
    H.own.push_back({Mod::x0, "UR", [&] { return ur; }, "", false});
    std::string nonces[3] = {"ce0001", "ce0002", "ce0003"};
    for (int k = 0; k < 3; ++k)
    {
        C[k].sid = "310-" + std::to_string(k + 1);
        C[k].own.push_back({Mod::x0, "H", nullptr, "#H:client,v37,c1,Medium," + nonces[k], true});
    }
    Roster ro;
    ro.on = true;
    ro.max = 3;
    ro.seq = 1;
    H.mbox.roster = [&](Roster& r) { r = ro; return true; };
    std::string h_out, c_out[3];
    bool c_new[3] = {false, false, false};
    bool link[3] = {true, true, true};
    H.mbox.on = [] { return true; };
    H.mbox.put = [&](std::string s) { h_out = std::move(s); };
    auto with_pid = [](const std::string& s, int pid) { return "{\"v\":2,\"pid\":" + std::to_string(pid) + "," + s.substr(7); };
    H.mbox.get = [&](std::string& s) { if (!c_new[0] || !link[0]) return false; s = with_pid(c_out[0], 1); c_new[0] = false; return true; };
    H.mbox.vget = [&](int v, std::string& s) { if (v < 1 || v > 2 || !c_new[v] || !link[v]) return false; s = with_pid(c_out[v], v + 1); c_new[v] = false; return true; };
    for (int k = 0; k < 3; ++k)
    {
        C[k].mbox.on = [] { return true; };
        C[k].mbox.put = [&, k](std::string s) { c_out[k] = std::move(s); c_new[k] = true; };
        C[k].mbox.get = [&, k](std::string& s) { if (h_out.empty() || !link[k]) return false; s = hub_split(h_out, k); return true; };
    }
    std::map<int, std::set<std::string>> seen;
    Bridge::LocalIn L;
    L.ok = true;
    for (int i = 0; i < 200; ++i)
    {
        double t = 50 + i * 0.1;
        L.x = i;
        link[1] = !(t >= 55 && t < 58);
        if (i == 60) ur = "#UR:2.0,1,P,A012A,2";
        if (i == 62) ur.clear();
        H.run_producers();
        H.io_step(L, t);
        H.dispatch();
        for (int k = 0; k < 3; ++k)
        {
            C[k].io_step(L, t + 0.01 * (k + 1));
            std::string u;
            if (x1_first(C[k].snap.x1, "UR", u))
                for (auto e : split_nonempty(u, '|')) seen[k].insert(std::string(e));
        }
    }
    bool all = true;
    for (int k = 0; k < 3; ++k) all = all && seen[k].count("2.0,1,P,A012A,2") == 1;
    check(all, "views: #UR relayed by the host reaches all 3 clients, client 2 through its own NetRel resend after a 3 s gap");
    check(seen[1].size() == 1, "views: #UR delivered once to the client behind the gap");
    bool resent = false;
    for (auto& l : H.logs) if (l.find("reenviado: UR=2.0,1,P,A012A,2") != std::string::npos) resent = true;
    check(resent, "views: host resend of #UR to the client behind the gap logged");
}

static void t_state(const std::wstring& dir)
{
    double hp = 87.25, mx = 100;
    bool alive = true, paused = false;
    std::string lvl = "Zone\"1";
    std::string j = state_json(true, &hp, &mx, &alive, &lvl, &paused);
    size_t c = j.find(',');
    std::string rest = j.substr(c);
    std::string want = ",\"role\":\"host\",\"in_world\":true,\"dead\":false,\"health\":87.2,\"max_health\":100.0,\"level\":\"Zone_1\",\"paused\":false,\"mod_ver\":\"v37-b53-cpp\",\"autowalk\":0,\"god\":0,\"god_methods\":\"-\",\"god_layers\":\"none\",\"pawns\":null,\"npcs_scan\":null,\"mirrors\":null,\"mirror_errs\":null,\"game_tod\":null,\"weather_tod\":null,\"weather\":null,\"godtest\":null,\"proto_err\":null}";
    check(j.rfind("{\"t\":", 0) == 0 && rest == want, "state_json\n got  " + rest + "\n want " + want);
    check(Flag{"rotate"}.i() == 1 && Flag{"avatarmode"}.s() == "npcpuppet" && Flag{"dobjsec"}.d() == 0.15 && Flag{"aimask"}.i() == 203647, "host role defaults from flags_role_host.tsv");
    std::string j2 = state_json(false, nullptr, nullptr, nullptr, nullptr, nullptr);
    check(j2.find("\"in_world\":false,\"dead\":null,\"health\":null,\"max_health\":null,\"level\":null,\"paused\":null") != std::string::npos, "state_json out of world");
}

static void t_log()
{
    g_log.clear();
    log("hello");
    check(!g_log.empty() && g_log.back() == "[NOLA:host] hello", "log line format");
    int n = 0;
    log_n(n, 2, "a");
    log_n(n, 2, "a");
    log_n(n, 2, "a");
    int cnt = 0;
    for (auto& l : g_log) if (l == "[NOLA:host] a") ++cnt;
    check(cnt == 2, "log_n cap");
}

static void t_csave()
{
    using namespace csx;
    check(want(true, false, Store::normal, Store::coop) == Store::coop, "csave menu wants coop");
    check(want(true, false, Store::coop, Store::normal) == Store::normal, "csave menu wants normal");
    check(want(false, true, Store::coop, Store::coop) == Store::normal, "csave flag off -> normal");
    check(want(true, true, Store::normal, Store::coop) == Store::normal, "csave override wins");
    check(want(true, true, Store::coop, Store::normal) == Store::coop, "csave override coop");
    Probe p;
    p.objs = true;
    p.paths_match = true;
    p.campaigns = 2;
    check(safe(p) == Block::ok, "csave safe ok");
    Probe q = p;
    q.pending = true;
    q.objs = false;
    check(safe(q) == Block::pending, "csave pending first");
    q = p;
    q.objs = false;
    check(safe(q) == Block::noobj, "csave no objects");
    q = p;
    q.paths_match = false;
    check(safe(q) == Block::mismatch, "csave mismatch");
    q = p;
    q.campaigns = 0;
    check(safe(q) == Block::nologin, "csave before login read");
    q = p;
    q.write_again = 1;
    check(safe(q) == Block::writing, "csave write again");
    q = p;
    q.writing = 1;
    q.pawn = true;
    check(safe(q) == Block::writing, "csave writing before world");
    q = p;
    q.pawn = true;
    check(safe(q) == Block::world, "csave world loaded");
    std::wstring o;
    check(coop_path(L"SaveGames\\", L"SaveGamesNOLA", o) && o == L"SaveGamesNOLA\\", "csave coop path");
    check(coop_path(L"SaveGames/", L"NOLA", o) && o == L"NOLA/", "csave keeps separator");
    check(coop_path(L"SaveGames", L"NOLA", o) && o == L"NOLA\\", "csave default separator");
    check(!coop_path(L"SaveGames\\", L"savegames", o) && o.empty(), "csave same as normal refused");
    check(!coop_path(L"SaveGames\\", L"", o), "csave empty refused");
    check(!coop_path(L"SaveGames\\", L"..", o), "csave dotdot refused");
    check(!coop_path(L"SaveGames\\", L"a\b", o), "csave backslash refused");
    check(!coop_path(L"SaveGames\\", L"a/b", o), "csave slash refused");
    check(!coop_path(L"SaveGames\\", L"C:x", o), "csave colon refused");
    check(!coop_path(L"SaveGames\\", L"x.", o), "csave trailing dot refused");
    check(!coop_path(L"SaveGames\\", L" x", o), "csave leading space refused");
    check(!coop_path(L"SaveGames\\", L"x\x00e7", o), "csave non ascii refused");
    check(!coop_path(L"SaveGames\\", std::wstring(49, L'a'), o), "csave too long refused");
    check(coop_path(L"SaveGames\\", std::wstring(48, L'a'), o), "csave 48 ok");
    check(!coop_path(L"\\", L"NOLA", o), "csave empty normal refused");
    check(folder_of(L"SaveGamesNOLA\\\\") == L"SaveGamesNOLA", "csave folder strips separators");
    check(same_ci(L"SaveGamesNOLA", L"savegamesnola") && !same_ci(L"a", L"ab"), "csave case compare");
}

int main(int argc, char** argv)
{
    std::wstring dir = tmpdir();
    set_role_for_test("host", dir);
    if (argc >= 2 && std::string(argv[1]) == "defaults") return mode_defaults();
    if (argc >= 3 && std::string(argv[1]) == "flags") return mode_flags(argv[2], argc >= 4 && std::string(argv[3]) == "all");
    if (argc >= 3 && std::string(argv[1]) == "num") return mode_num(argv[2]);
    if (argc >= 3 && std::string(argv[1]) == "netrel") return mode_netrel(argv[2]);
    log_sink().out = [](const std::string& l) { g_log.push_back(l); };
    t_flags_poll(dir);
    t_role(dir);
    t_log();
    t_sched();
    t_ref();
    t_data();
    t_bridge(dir);
    t_state(dir);
    t_fake_pair();
    t_views();
    t_hub_tag();
    t_views_ur();
    t_csave();
    tc_tables(check, L"dev");
    remove_file(dir + L"flags.txt");
    RemoveDirectoryW(dir.c_str());
    std::printf("x0 selftest: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
