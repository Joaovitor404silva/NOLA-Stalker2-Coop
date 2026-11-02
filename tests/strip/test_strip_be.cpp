#include "../../src/s2m_savefile.h"
#include "../../src/s2m_strip.h"
#include "../../src/s2m_wsplice.h"

#include <Windows.h>

#include <algorithm>
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
    std::fflush(stdout);
}

using DecFn = intptr_t(__stdcall*)(const void*, intptr_t, void*, intptr_t, int, int, int, void*, intptr_t, void*, void*, void*, intptr_t, int);
DecFn g_dec;

auto codec() -> s2m_wsplice::Codec
{
    s2m_wsplice::Codec c;
    c.dec = [](const std::string& st, uint32_t rs, std::string& raw, std::string& err) -> bool {
        raw.assign(rs, '\0');
        intptr_t n = g_dec(st.data(), (intptr_t)st.size(), raw.data(), rs, 1, 0, 0, nullptr, 0, nullptr, nullptr, nullptr, 0, 3);
        if (n != (intptr_t)rs) { err = "oo2core decode " + std::to_string(n); return false; }
        return true;
    };
    return c;
}

auto where(const std::string& raw, const std::string& err) -> std::string
{
    size_t at = err.rfind(" at ");
    if (at == std::string::npos) return "";
    size_t off = std::strtoull(err.c_str() + at + 4, nullptr, 16);
    if (off >= raw.size()) return "";
    size_t best = std::string::npos;
    for (const char* kw : {"struct F", "class U"})
    {
        size_t p = raw.rfind(kw, off);
        if (p != std::string::npos && (best == std::string::npos || p > best)) best = p;
    }
    if (best == std::string::npos) return "";
    size_t e = best;
    while (e < raw.size() && raw[e] >= 32 && raw[e] < 127) ++e;
    return " [in " + raw.substr(best, e - best) + " +" + std::to_string(off - best) + "]";
}

auto char_of(const std::string& raw, uint32_t& money, size_t& items, std::string& err) -> bool
{
    try
    {
        Parsed P = parse(raw);
        Char c = character(raw, P);
        money = c.money;
        items = c.items.size();
        return true;
    }
    catch (const std::exception& e)
    {
        err = e.what();
        return false;
    }
}
}

auto main(int argc, char** argv) -> int
{
    if (argc < 3)
    {
        std::printf("usage: test_strip_be <oo2core dll> <dir>...\n");
        return 2;
    }
    HMODULE m = LoadLibraryA(argv[1]);
    if (m) g_dec = reinterpret_cast<DecFn>(GetProcAddress(m, "OodleLZ_Decompress"));
    if (!g_dec)
    {
        std::printf("FAIL oo2core not loaded from %s\n", argv[1]);
        return 1;
    }
    g_acceptU21 = true;
    auto c = codec();
    std::vector<fs::path> files;
    for (int i = 2; i < argc; ++i)
        for (auto& e : fs::directory_iterator(argv[i]))
            if (e.path().extension() == ".raw" || e.path().extension() == ".sav") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    int be = 0, bb = 0;
    for (auto& p : files)
    {
        std::string nm = p.filename().string(), raw, err;
        if (p.extension() == ".sav")
        {
            if (!s2m_wsplice::decode(rd(p), c, raw, err))
            {
                check(false, nm + " decode " + err);
                continue;
            }
        }
        else raw = rd(p);
        uint32_t ver = raw.size() >= 4 ? u32at(raw, 0) : 0;
        be += ver == kVersionU21;
        bb += ver == kVersion;
        s2m_strip::Report rep;
        int rc = s2m_strip::strip(raw, rep, err);
        check(rc == s2m_strip::kOk, nm + " ver " + std::to_string(ver) + " strip rc " + std::to_string(rc) + " " +
                                        (rc == s2m_strip::kOk ? rep.summary() : err + where(raw, err)));
        if (rc != s2m_strip::kOk) continue;
        s2m_strip::Report r2;
        std::string e2;
        int rc2 = s2m_strip::strip(rep.raw, r2, e2);
        bool idem = rc2 == s2m_strip::kOk && r2.raw == rep.raw && r2.fixTotal == 0;
        for (int k = 0; k < 5; ++k)
            if (r2.removed[k]) idem = false;
        check(idem, nm + " stripped raw re-parses, nothing left to strip rc " + std::to_string(rc2) + " " + e2);
        uint32_t m0 = 0, m1 = 0;
        size_t i0 = 0, i1 = 0;
        std::string ce0, ce1;
        bool a = char_of(raw, m0, i0, ce0), b = char_of(rep.raw, m1, i1, ce1);
        check(a && b && m0 == m1 && i0 == i1, nm + " character kept: money " + std::to_string(m0) + " -> " + std::to_string(m1) + ", items " +
                                                 std::to_string(i0) + " -> " + std::to_string(i1) + " " + ce0 + ce1);
    }
    check(be >= 7, "data: " + std::to_string(be) + " be saves, " + std::to_string(bb) + " bb saves");
    std::printf("%s %d/%d\n", g_ok == g_n ? "PASS" : "FAIL", g_ok, g_n);
    return g_ok == g_n ? 0 : 1;
}
