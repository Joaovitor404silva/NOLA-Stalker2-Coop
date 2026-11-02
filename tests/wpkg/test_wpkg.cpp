#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "s2m_wpkg.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
using namespace s2m_wpkg;

static int fails = 0, passes = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    ok ? ++passes : ++fails;
}

static std::string rd(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

static void wr(const fs::path& p, const std::string& d)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(d.data(), (std::streamsize)d.size());
}

static int count_pkgs(const fs::path& dir)
{
    int n = 0;
    if (!fs::exists(dir)) return 0;
    for (auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".pkg") ++n;
    return n;
}

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("usage: test_wpkg <test .sav file>\n");
        return 2;
    }
    std::string sav = rd(argv[1]);
    check(sav.size() > 1024, "test save readable (" + std::to_string(sav.size()) + " B)");

    check(world_id(1760000000000LL) == "91760000000000", "world_id of a 13-digit wall clock");
    check(world_id(5) == "90000000000005", "world_id pads to 14 digits");
    check(is_world_id("91760000000000"), "is_world_id accepts a world id");
    check(!is_world_id("1760000000000"), "is_world_id refuses a join id");
    check(!is_world_id("81760000000000"), "is_world_id refuses a leading 8");
    check(!is_world_id("9176000000000x"), "is_world_id refuses a non-digit");

    Pkg a;
    a.id = "91760000000001";
    a.guid = "0123456789ABCDEF0123456789ABCDEF";
    a.cid = "camp1";
    a.kh = 2;
    a.hs = 7;
    a.own = "p_ab12";
    a.ap = "#S2CA 1 camp1\nW\tcamp1\n2\tQ1\n";
    a.data = std::string("\x01\x02\x00\x03payload", 11);
    Pkg b;
    std::string e;
    {
        e.clear();
        bool r = parse(pack(a), b, e) && b.id == a.id && b.guid == a.guid && b.cid == a.cid && b.kh == 2 && b.hs == 7 && b.own == a.own && b.ap == a.ap && b.data == a.data;
        check(r, "pack/parse round trip with hs and own " + e);
    }
    Pkg o = a;
    o.hs = -1;
    o.own.clear();
    Pkg ob;
    std::string op = pack(o);
    {
        e.clear();
        bool r = op.find(" hs=") == std::string::npos && parse(op, ob, e) && ob.hs == -1 && ob.own.empty() && ob.data == a.data;
        check(r, "old join package has no hs and parses as hs=-1");
    }
    std::string bad = pack(a);
    bad.back() ^= 1;
    {
        e.clear();
        bool r = !parse(bad, b, e) && e == "save hash does not match";
        check(r, "parse refuses a changed save byte");
    }

    fs::path root = fs::temp_directory_path() / ("s2wpkg_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);
    fs::create_directories(root / "br");
    fs::create_directories(root / "cl");
    std::wstring br = (root / "br").wstring(), cl = (root / "cl").wstring() + L"\\";
    fs::path savp = root / "0123456789ABCDEF0123456789ABCDEF.sav";
    wr(savp, sav);

    std::string g = "0123456789ABCDEF0123456789ABCDEF";
    {
        e.clear();
        bool r = freeze(br, savp.wstring(), g, 3, 2, "camp1", e);
        check(r, "freeze a real host save " + e);
    }
    check(rd(root / "br" / "wp_host.sav") == sav, "wp_host.sav is byte-identical to the host save");
    Frozen f;
    std::string fd;
    {
        e.clear();
        bool r = frozen(br, f, &fd, e) && f.guid == g && f.cid == "camp1" && f.kh == 2 && f.hs == 3 && f.size == (long long)sav.size() && fd == sav;
        check(r, "frozen reads the copy back " + e);
    }

    std::string bsav = sav;
    bsav[bsav.size() / 2] ^= 0x5a;
    fs::path badp = root / "bad.sav";
    wr(badp, bsav);
    {
        e.clear();
        bool r = !freeze(br, badp.wstring(), g, 4, 2, "camp1", e);
        check(r, "freeze refuses a save with a bad crc (" + e + ")");
    }
    {
        e.clear();
        bool r = frozen(br, f, &fd, e) && f.hs == 3 && fd == sav;
        check(r, "a refused freeze keeps the previous frozen copy");
    }
    {
        e.clear();
        bool r = !freeze(br, savp.wstring(), "short", 4, 2, "camp1", e);
        check(r, "freeze refuses a bad guid");
    }

    std::string log = "1.5\tQ1\n2.0\tQ2\n3.0\tQ3\n";
    std::string out;
    {
        e.clear();
        bool r = !pack_world(br, "1760000000000", log, "", out, f, e) && e == "not a world request";
        check(r, "pack_world refuses a join id");
    }
    {
        e.clear();
        bool r = !pack_world(br, "91760000000002", "1.5\tQ1\n", "", out, f, e);
        check(r, "pack_world refuses a quest log shorter than kh (" + e + ")");
    }
    {
        e.clear();
        bool r = pack_world(br, "91760000000002", log, "p_ab12", out, f, e);
        check(r, "pack_world from the frozen copy " + e);
    }
    Pkg w;
    {
        e.clear();
        bool r = parse(out, w, e) && w.id == "91760000000002" && w.hs == 3 && w.kh == 2 && w.own == "p_ab12" && w.guid == g && w.data == sav;
        check(r, "world package carries the frozen save, hs and own");
    }
    check(w.ap == "#S2CA 1 camp1\nW\tcamp1\n1\tQ1\n1\tQ2\n", "world package applied record holds the first kh quest events");

    Stored s;
    {
        e.clear();
        bool r = !store(cl, op, 2, s, e);
        check(r, "store refuses a join package (" + e + ")");
    }
    Pkg wb;
    parse(out, wb, e);
    wb.data = bsav;
    {
        e.clear();
        bool r = !store(cl, pack(wb), 2, s, e);
        check(r, "store refuses a world package whose save crc is bad (" + e + ")");
    }
    check(count_pkgs(root / "cl" / "wp_in") == 0, "nothing stored after refusals");

    std::string ids[3] = {"91760000000010", "91760000000020", "91760000000030"};
    for (int i = 0; i < 3; ++i)
    {
        std::string o2;
        Frozen f2;
        pack_world(br, ids[i], log, "", o2, f2, e);
        {
            e.clear();
            bool r = store(cl, o2, 2, s, e) && s.id == ids[i] && s.hs == 3 && s.kh == 2 && s.cid == "camp1" && s.size == (long long)sav.size();
            check(r, "store world package " + ids[i] + " " + e);
        }
    }
    check(count_pkgs(root / "cl" / "wp_in") == 2, "store keeps the newest 2 packages");
    check(!fs::exists(root / "cl" / "wp_in" / ("camp1__" + ids[0] + ".pkg")) && fs::exists(root / "cl" / "wp_in" / ("camp1__" + ids[2] + ".pkg")), "store pruned the oldest");
    Pkg back;
    {
        e.clear();
        bool r = parse(rd(root / "cl" / "wp_in" / ("camp1__" + ids[2] + ".pkg")), back, e) && back.data == sav;
        check(r, "stored package holds the host save bytes");
    }
    Stored ls;
    check(last(cl, ls) && ls.id == ids[2] && ls.cid == "camp1" && ls.hs == 3 && ls.kh == 2 && ls.guid == g && ls.file == "camp1__" + ids[2] + ".pkg", "last.txt names the newest package");

    std::string hs = rd(root / "br" / "wp_host.sav");
    hs[100] ^= 1;
    wr(root / "br" / "wp_host.sav", hs);
    {
        e.clear();
        bool r = !frozen(br, f, &fd, e) && !pack_world(br, "91760000000040", log, "", out, f, e);
        check(r, "a changed wp_host.sav is refused (" + e + ")");
    }

    fs::remove_all(root);
    std::printf("%s %d/%d\n", fails ? "FAIL" : "PASS", passes, passes + fails);
    return fails ? 1 : 0;
}
