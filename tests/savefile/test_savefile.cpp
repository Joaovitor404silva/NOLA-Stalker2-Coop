#include "../../src/s2m_savefile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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
auto ms(std::chrono::steady_clock::time_point a) -> long long
{
    return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - a).count();
}
}

auto main(int argc, char** argv) -> int
{
    if (argc < 4)
    {
        std::printf("usage: test_savefile <dec root> <oracle dir> <pairs.txt> [sav dir]\n");
        return 2;
    }
    fs::path root = argv[1], orc = argv[2];
    std::vector<fs::path> raws;
    for (auto sub : {"master", "pair2"})
        for (auto& e : fs::directory_iterator(root / sub))
            if (e.path().extension() == ".raw") raws.push_back(e.path());
    std::sort(raws.begin(), raws.end());
    int self = 0;
    for (auto& p : raws)
    {
        std::string b = rd(p), nm = p.filename().string().substr(0, 14);
        auto t0 = std::chrono::steady_clock::now();
        try
        {
            Parsed P = parse(b);
            Char c = character(b, P);
            bool nt = P.names.blob() == b.substr(P.names.nto);
            Report rep;
            std::string o = splice(b, b, Opt{}, rep);
            bool same = o == b;
            if (same) ++self;
            check(nt && same, nm + " parse items " + std::to_string(P.items.size()) + "/" + std::to_string(P.icnt) + " conts " + std::to_string(P.conts.size()) +
                                  " char items " + std::to_string(c.items.size()) + " money " + std::to_string(c.money) + " names " + (nt ? "exact" : "DIFF") +
                                  " self-splice " + (same ? "byte-identical" : "DIFF") + " " + std::to_string(ms(t0)) + " ms");
        }
        catch (const std::exception& e)
        {
            check(false, nm + " " + e.what());
        }
    }
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
            Opt o;
            o.keepPos = flags.find('p') == std::string::npos;
            o.vitals = flags.find('v') == std::string::npos;
            Report rep;
            auto t0 = std::chrono::steady_clock::now();
            std::string out = splice(hb, db, o, rep);
            long long t = ms(t0);
            check(out == want, name + " cross splice == python oracle (" + std::to_string(out.size()) + " B, " + std::to_string(rep.namesAdded.size()) + " names, " +
                                   std::to_string(rep.dropped.size()) + " dropped, " + std::to_string(t) + " ms)");
            Parsed oP = parse(out), dP = parse(db);
            std::string why;
            Char oc = character(out, oP), dc = character(db, dP);
            if (!o.vitals) std::copy(std::begin(dc.u148), std::end(dc.u148), oc.u148);
            check(same_char(oc, dc, why), name + " output re-parses, character equals the donor " + why);
        }
        catch (const std::exception& e)
        {
            check(false, name + " " + e.what());
        }
    }
    if (argc > 4)
    {
        int n = 0, good = 0;
        for (auto& e : fs::directory_iterator(argv[4]))
        {
            if (e.path().extension() != ".sav") continue;
            std::string f = rd(e.path()), st, err;
            uint32_t rs = 0;
            ++n;
            if (file_split(f, rs, st, err) && file_wrap(st, rs) == f) ++good;
        }
        check(n > 0 && good == n, "file crc split/wrap identical on " + std::to_string(good) + "/" + std::to_string(n) + " .sav files");
    }
    check(self == (int)raws.size(), "self-splice byte-identical on " + std::to_string(self) + "/" + std::to_string(raws.size()) + " saves");
    std::printf("%s %d/%d\n", g_ok == g_n ? "PASS" : "FAIL", g_ok, g_n);
    return g_ok == g_n ? 0 : 1;
}
