#include "../../src/s2m_strip.h"
#include "../../src/s2m_wsplice.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace s2m_savefile;

namespace
{
int g_n = 0, g_ok = 0;
auto rd(const fs::path& p) -> std::string
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
auto check(bool c, const std::string& what) -> void
{
    ++g_n;
    if (c) ++g_ok;
    std::printf("%s %s\n", c ? "ok  " : "FAIL", what.c_str());
}
auto identity() -> s2m_wsplice::Codec
{
    s2m_wsplice::Codec c;
    c.dec = [](const std::string& st, uint32_t rs, std::string& raw, std::string& err) -> bool {
        if (st.size() != rs) { err = "identity size"; return false; }
        raw = st;
        return true;
    };
    c.enc = [](const std::string& raw, std::string& st, std::string&) -> bool {
        st = raw;
        return true;
    };
    return c;
}
}

auto main(int argc, char** argv) -> int
{
    if (argc < 4)
    {
        std::printf("usage: test_wsplice <dec root> <oracle dir> <pairs.txt>\n");
        return 2;
    }
    fs::path root = argv[1], orc = argv[2];
    auto c = identity();
    std::ifstream pf(argv[3]);
    std::string line;
    while (std::getline(pf, line))
    {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ls(line);
        std::string name, h, d, flags;
        ls >> name >> h >> d >> flags;
        try
        {
            std::string hb = rd(root / h), db = rd(root / d), want = rd(orc / (name + ".raw"));
            std::string wf = file_wrap(hb, (uint32_t)hb.size()), cf = file_wrap(db, (uint32_t)db.size());
            Opt o;
            o.keepPos = flags.find('p') == std::string::npos;
            o.vitals = flags.find('v') == std::string::npos;
            o.strip = false;
            s2m_wsplice::Out out;
            std::string err, raw;
            bool ok = s2m_wsplice::run(cf, wf, c, o, out, err);
            check(ok, name + " run " + err);
            if (!ok) continue;
            uint32_t rs = 0;
            std::string st, e2;
            bool sp = file_split(out.file, rs, st, e2);
            check(sp && st == want && rs == want.size(), name + " wrapped output == python oracle (" + std::to_string(out.outRaw) + " B raw, " + std::to_string(out.items) + " items, money " + std::to_string(out.money) + ")");
            check(out.worldRaw == hb.size() && out.clientRaw == db.size() && out.outRaw == want.size() && out.strip.empty(), name + " sizes reported, strip off");
            s2m_strip::Report sr;
            std::string se;
            int src = s2m_strip::strip(hb, sr, se);
            check(src == 0, name + " host world strips " + sr.summary() + " " + se);
            if (sr.raw.empty()) continue;
            std::string swf = file_wrap(sr.raw, (uint32_t)sr.raw.size());
            s2m_wsplice::Out outPre;
            std::string e3;
            bool ok3 = s2m_wsplice::run(cf, swf, c, o, outPre, e3);
            Opt on = o;
            on.strip = true;
            s2m_wsplice::Out outOn;
            bool ok4 = s2m_wsplice::run(cf, wf, c, on, outOn, e3);
            check(ok3 && ok4 && outOn.file == outPre.file, name + " strip on == splice of the pre-stripped world (" + std::to_string(outOn.outRaw) + " B raw)");
            check(outOn.strip == sr.summary() && outOn.worldRaw == hb.size() && outOn.stripIn == hb.size() && outOn.stripOut == sr.raw.size() && outOn.outRaw < out.outRaw, name + " strip counts reported: " + outOn.strip);
        }
        catch (const std::exception& e)
        {
            check(false, name + " " + e.what());
        }
    }
    {
        std::string hb = rd(root / "master/C8B0980B47818CA8BC2F91B6F0E7D26F.sav.raw"), db = rd(root / "master/22AE45304D8F3226C13B198177EC280B.sav.raw");
        std::string wf = file_wrap(hb, (uint32_t)hb.size()), cf = file_wrap(db, (uint32_t)db.size());
        s2m_wsplice::Out out;
        std::string err;
        auto bad = c;
        bad.enc = [](const std::string& raw, std::string& st, std::string&) -> bool {
            st = raw;
            st[st.size() / 2] ^= 1;
            return true;
        };
        check(!s2m_wsplice::run(cf, wf, bad, Opt{}, out, err) && !err.empty(), "encoder whose read-back differs is refused: " + err);
        std::string cut = wf;
        cut[cut.size() / 3] ^= 1;
        err.clear();
        check(!s2m_wsplice::run(cf, cut, c, Opt{}, out, err) && !err.empty(), "world file with a bad crc is refused: " + err);
        err.clear();
        std::string junk = file_wrap(std::string(4096, 'x'), 4096);
        check(!s2m_wsplice::run(junk, wf, c, Opt{}, out, err) && !err.empty(), "client file that is not a save is refused: " + err);
        s2m_strip::Report sr;
        std::string se;
        check(s2m_strip::strip(hb, sr, se) == 0 && !sr.puppets.empty(), "refusal setup: host world has puppets");
        std::string planted = hb;
        size_t at = planted.find("struct FUnitModel");
        size_t pp = at + 17 + 4;
        uint16_t pn;
        std::memcpy(&pn, planted.data() + pp, 2);
        pp += 2 + (size_t)pn * 8;
        while (planted[pp] == 0) ++pp;
        uint32_t pid = sr.puppets.front();
        std::memcpy(&planted[pp + 1], &pid, 4);
        std::string pwf = file_wrap(planted, (uint32_t)planted.size());
        s2m_wsplice::Out pout;
        err.clear();
        check(!s2m_wsplice::run(cf, pwf, c, Opt{}, pout, err) && err.find("puppet strip refused") != std::string::npos && pout.file.empty(), "puppet id planted in a field with no rule: splice refused, nothing produced: " + err);
        Opt off;
        off.strip = false;
        err.clear();
        check(s2m_wsplice::run(cf, pwf, c, off, pout, err) && !pout.file.empty(), "same world with strip off still splices: " + err);
        auto fail = c;
        fail.dec = [](const std::string&, uint32_t, std::string&, std::string& e) -> bool { e = "dec off"; return false; };
        err.clear();
        check(!s2m_wsplice::run(cf, wf, fail, Opt{}, out, err) && err.find("dec off") != std::string::npos, "decoder failure is passed up: " + err);
    }
    std::printf("%s %d/%d\n", g_ok == g_n ? "PASS" : "FAIL", g_ok, g_n);
    return g_ok == g_n ? 0 : 1;
}
