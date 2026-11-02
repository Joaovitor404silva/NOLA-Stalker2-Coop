#include "../../src/s2m_savefile.h"
#include "../../src/s2m_strip.h"
#include "../../src/s2m_wsplice.h"

#include <Windows.h>

#include <cstdio>
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

using CompFn = intptr_t(__stdcall*)(int, const void*, intptr_t, void*, int, const void*, const void*, const void*, void*, intptr_t);
using DecFn = intptr_t(__stdcall*)(const void*, intptr_t, void*, intptr_t, int, int, int, void*, intptr_t, void*, void*, void*, intptr_t, int);
using BoundFn = intptr_t(__stdcall*)(int, intptr_t);
CompFn g_comp;
DecFn g_dec;
BoundFn g_bound;

auto codec() -> s2m_wsplice::Codec
{
    s2m_wsplice::Codec c;
    c.dec = [](const std::string& st, uint32_t rs, std::string& raw, std::string& err) -> bool {
        raw.assign(rs, '\0');
        intptr_t n = g_dec(st.data(), (intptr_t)st.size(), raw.data(), rs, 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3);
        if (n != (intptr_t)rs) { err = "oo2core decode " + std::to_string(n); return false; }
        return true;
    };
    c.enc = [](const std::string& raw, std::string& st, std::string& err) -> bool {
        intptr_t cap = g_bound(8, (intptr_t)raw.size());
        st.assign((size_t)cap, '\0');
        intptr_t n = g_comp(8, raw.data(), (intptr_t)raw.size(), st.data(), 3, nullptr, nullptr, nullptr, nullptr, 0);
        if (n <= 0 || n > cap) { err = "oo2core encode " + std::to_string(n); return false; }
        st.resize((size_t)n);
        return true;
    };
    return c;
}
}

auto main(int argc, char** argv) -> int
{
    if (argc < 4)
    {
        std::printf("usage: test_wsplice_be <oo2core dll> <sav dir> <client sav> <host sav>...\n");
        return 2;
    }
    HMODULE m = LoadLibraryA(argv[1]);
    if (m)
    {
        g_comp = reinterpret_cast<CompFn>(GetProcAddress(m, "OodleLZ_Compress"));
        g_dec = reinterpret_cast<DecFn>(GetProcAddress(m, "OodleLZ_Decompress"));
        g_bound = reinterpret_cast<BoundFn>(GetProcAddress(m, "OodleLZ_GetCompressedBufferSizeNeeded"));
    }
    if (!m || !g_comp || !g_dec || !g_bound)
    {
        std::printf("FAIL oo2core not loaded from %s\n", argv[1]);
        return 1;
    }
    g_acceptU21 = true;
    fs::path dir = argv[2];
    auto c = codec();
    std::string cf = rd(dir / argv[3]), rc, e;
    bool cok = s2m_wsplice::decode(cf, c, rc, e) && u32at(rc, 0) == kVersionU21;
    check(cok, std::string("client save ") + argv[3] + " decodes to a be raw (" + std::to_string(rc.size()) + " B) " + e);
    std::string reenc, back;
    check(s2m_wsplice::encode(rc, c, reenc, e) && s2m_wsplice::decode(reenc, c, back, e) && back == rc, "client raw re-encodes and decodes back byte-equal " + e);
    Char dc{};
    try
    {
        Parsed dP = parse(rc);
        dc = character(rc, dP);
        check(true, "client raw parses: money " + std::to_string(dc.money) + ", items " + std::to_string(dc.items.size()));
    }
    catch (const std::exception& x)
    {
        check(false, std::string("client raw parse: ") + x.what());
    }
    for (int i = 4; i < argc; ++i)
    {
        std::string hf = rd(dir / argv[i]), hr;
        std::string nm = argv[i];
        check(s2m_wsplice::decode(hf, c, hr, e) && u32at(hr, 0) == kVersionU21, nm + " host world decodes to a be raw " + e);
        s2m_strip::Report sr;
        std::string se;
        int sc = s2m_strip::strip(hr, sr, se);
        std::printf("info %s strip code %d %s\n", nm.c_str(), sc, sc == s2m_strip::kOk ? sr.summary().c_str() : se.c_str());
        s2m_wsplice::Out out;
        std::string err;
        bool ok = s2m_wsplice::run(cf, hf, c, Opt{}, out, err);
        check(ok, nm + " splice + strip + Oodle encode end to end: " + (ok ? "world " + std::to_string(out.worldRaw) + " B -> " + std::to_string(out.outRaw) + " B, strip: " + out.strip : err));
        if (!ok) continue;
        std::string orw;
        bool dok = s2m_wsplice::decode(out.file, c, orw, e);
        check(dok && orw.size() == out.outRaw && u32at(orw, 0) == kVersionU21, nm + " spliced .sav decodes (" + std::to_string(orw.size()) + " B raw, version be) " + e);
        if (!dok) continue;
        try
        {
            Parsed oP = parse(orw);
            Char oc = character(orw, oP);
            std::string why;
            check(same_char(oc, dc, why), nm + " spliced save re-parses and the character is the client's (money " + std::to_string(oc.money) + ", items " + std::to_string(oc.items.size()) + ") " + why);
            check(oc.money == dc.money && oc.items.size() == dc.items.size(), nm + " money and item count equal the client's");
        }
        catch (const std::exception& x)
        {
            check(false, nm + " spliced save parse: " + x.what());
        }
    }
    std::printf("%s %d/%d\n", g_ok == g_n ? "PASS" : "FAIL", g_ok, g_n);
    return g_ok == g_n ? 0 : 1;
}
