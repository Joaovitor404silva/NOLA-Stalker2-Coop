#include "../../src/s2m_savefile.h"
#include "../../src/s2m_strip.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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
}

auto main(int argc, char** argv) -> int
{
    if (argc < 3)
    {
        std::printf("usage: test_savefile_be <raw dir> <expect.tsv>\n");
        return 2;
    }
    std::map<std::string, std::pair<uint32_t, size_t>> want;
    {
        std::ifstream ef(argv[2]);
        std::string name;
        uint32_t money;
        size_t items;
        while (ef >> name >> money >> items) want[name] = {money, items};
    }
    std::vector<fs::path> raws;
    for (auto& e : fs::directory_iterator(argv[1]))
        if (e.path().extension() == ".raw") raws.push_back(e.path());
    std::sort(raws.begin(), raws.end());
    int be = 0, bb = 0;
    for (auto& p : raws)
    {
        std::string b = rd(p), nm = p.stem().string();
        uint32_t ver = b.size() >= 4 ? u32at(b, 0) : 0;
        if (ver == kVersionU21) ++be;
        if (ver == kVersion) ++bb;
        try
        {
            g_acceptU21 = true;
            Parsed P = parse(b);
            Char c = character(b, P);
            std::string next = P.names.name(1, (uint8_t)P.nextUnitSidT, (uint16_t)P.nextUnitSidI);
            Report rep;
            bool same = splice(b, b, Opt{}, rep) == b;
            size_t upg = 0;
            for (auto& it : c.items) upg += it.upgA.size() + it.upgB.size() + it.set48.size() + it.set98.size();
            check(same && !next.empty(), nm + " ver " + std::to_string(ver) + " unit0 end " + std::to_string(P.unit.end) + " next unit " + next + " money " +
                                            std::to_string(c.money) + " items " + std::to_string(c.items.size()) + " upgrades " + std::to_string(upg) +
                                            " self-splice " + (same ? "byte-identical" : "DIFF"));
            auto w = want.find(nm);
            if (w != want.end())
                check(c.money == w->second.first && c.items.size() == w->second.second,
                      nm + " expected money " + std::to_string(w->second.first) + " items " + std::to_string(w->second.second));
        }
        catch (const std::exception& e)
        {
            check(false, nm + " " + e.what());
        }
        if (ver == kVersionU21)
        {
            g_acceptU21 = false;
            bool refused = false;
            try
            {
                parse(b);
            }
            catch (const std::exception&)
            {
                refused = true;
            }
            g_acceptU21 = true;
            check(refused, nm + " savebe=0 refuses the be save");
        }
        if (ver == kVersion)
        {
            s2m_strip::Report sr;
            std::string err;
            int sc = s2m_strip::strip(b, sr, err);
            check(sc == s2m_strip::kOk, nm + " strip on bb save " + std::to_string(sc) + " " + err);
        }
    }
    fs::path hp = fs::path(argv[1]) / "h_bk0202_12B96995.raw", dp = fs::path(argv[1]) / "c_231525_3DD47EF2.raw";
    try
    {
        std::string hb = rd(hp), db = rd(dp);
        Report rep;
        std::string out = splice(hb, db, Opt{}, rep);
        Parsed oP = parse(out), dP = parse(db);
        Char oc = character(out, oP), dc = character(db, dP);
        std::string why;
        check(same_char(oc, dc, why), "be cross splice host world + client character: re-parses, character equals the donor (money " + std::to_string(oc.money) +
                                          ", items " + std::to_string(oc.items.size()) + ") " + why);
    }
    catch (const std::exception& e)
    {
        check(false, std::string("be cross splice ") + e.what());
    }
    check(be >= 5 && bb >= 1, "data: " + std::to_string(be) + " be saves, " + std::to_string(bb) + " bb saves");
    std::printf("%s %d/%d\n", g_ok == g_n ? "PASS" : "FAIL", g_ok, g_n);
    return g_ok == g_n ? 0 : 1;
}
