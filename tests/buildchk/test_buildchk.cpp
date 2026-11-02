#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "s2x_buildchk.h"

static int g_fail = 0, g_pass = 0;

static void check(bool ok, const std::string& what)
{
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (ok) ++g_pass;
    else ++g_fail;
}

static bool has(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }

static std::string narrow(const std::wstring& w)
{
    std::string s;
    for (wchar_t c : w) s += c < 128 ? (char)c : '?';
    return s;
}

__declspec(noinline) static int probe_fn(int a) { return a * 7 + 3; }

static std::string hexpat(const uint8_t* p, size_t n, int wild)
{
    std::string t;
    char b[4];
    for (size_t i = 0; i < n; ++i)
    {
        if (i) t += ' ';
        if ((int)i == wild) t += "??";
        else { std::snprintf(b, sizeof b, "%02X", p[i]); t += b; }
    }
    return t;
}

static int run_self()
{
    HMODULE m = GetModuleHandleW(nullptr);
    bchk::MemImage img(m);
    check(img.base != nullptr && img.size > 0x1000, "memory image of the test exe has a SizeOfImage");
    auto* base = reinterpret_cast<const uint8_t*>(m);
    uint32_t lfanew = *reinterpret_cast<const uint32_t*>(base + 0x3C);
    uint32_t ts = *reinterpret_cast<const uint32_t*>(base + lfanew + 8);
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    uint32_t tvs = 0;
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        if (std::memcmp(sec[i].Name, ".text", 6) == 0) tvs = sec[i].Misc.VirtualSize;
    const uint8_t* fp = reinterpret_cast<const uint8_t*>(&probe_fn);
    if (fp[0] == 0xE9) fp = fp + 5 + *reinterpret_cast<const int32_t*>(fp + 1);
    uint32_t frva = (uint32_t)(fp - base);
    std::string good = hexpat(fp, 16, 3);
    std::string bad = good;
    bad[0] = bad[0] == 'F' ? '0' : 'F';
    std::string hdr = hexpat(base, 2, -1);
    bchk::Sig sigs[] = {{"self.hdr", 0, hdr.c_str()}, {"self.fn", frva, good.c_str()}};
    bchk::Expect e{ts, tvs, "", "0", 2, 0, sigs, 2};
    bchk::Result r = bchk::run_check(img, e);
    check(r.ok && r.bad == 0 && r.total == 2, "memory image: own headers and own function bytes match (" + bchk::log_line(r, e) + ")");
    check(has(bchk::log_line(r, e), "NOLA buildchk: OK game build"), "OK line prefix");
    bchk::Sig sigs2[] = {{"self.hdr", 0, hdr.c_str()}, {"self.fn", frva, bad.c_str()}};
    bchk::Expect e2{ts, tvs, "", "0", 2, 0, sigs2, 2};
    r = bchk::run_check(img, e2);
    char want[64];
    std::snprintf(want, sizeof want, "first self.fn@%x", frva);
    check(!r.ok && r.bad == 1 && r.first == "self.fn" && has(bchk::log_line(r, e2), want), "memory image: one changed byte fails and names the row");
    check(has(bchk::log_line(r, e2), "): no hooks installed, NOLA is off, the game runs unmodded"), "log line tail on failure");
    bchk::Sig sigs3[] = {{"self.out", img.size + 0x1000, "00 00"}, {"self.end", img.size - 1, "00 00"}};
    bchk::Expect e3{ts, tvs, "", "0", 2, 0, sigs3, 2};
    r = bchk::run_check(img, e3);
    check(!r.ok && r.unread == 2 && r.first == "self.out" && has(bchk::log_line(r, e3), "2 unreadable"), "memory image: reads outside the image are refused, not faulted");
    bchk::Expect e4{ts ^ 1, tvs, "", "0", 2, 0, sigs, 2};
    r = bchk::run_check(img, e4);
    check(!r.ok && r.bad == 0 && !r.ident && has(bchk::log_line(r, e4), "(0 of 2 signatures differ)"), "memory image: timestamp mismatch alone fails");
    std::wstring msg = bchk::message(r, e4);
    check(has(narrow(msg), "NOLA needs game version ") && has(narrow(msg), ", this game is ") && has(narrow(msg), "NOLA is off"), "player message text");
    bchk::MemImage none(nullptr);
    r = bchk::run_check(none, e);
    check(!r.ok && !r.headers && has(bchk::log_line(r, e), "FAIL game build unreadable"), "null module fails closed");
    check(bchk::enabled(""), "flag: empty flags.txt = check on");
    check(bchk::enabled("netpeer=1.2.3.4\nbuildchk=1\n"), "flag: buildchk=1 = on");
    check(!bchk::enabled("buildchk=0\n"), "flag: buildchk=0 = off");
    check(!bchk::enabled("\xEF\xBB\xBF  buildchk = 0\r\n"), "flag: BOM, spaces and CRLF");
    check(bchk::enabled("buildchk=0\nbuildchk=1\n"), "flag: the last line wins (on)");
    check(!bchk::enabled("buildchk=1\nbuildchk=0"), "flag: the last line wins (off, no newline)");
    check(bchk::enabled("buildchkx=0\nxbuildchk=0\n-- buildchk=0\n"), "flag: other keys and comment lines are ignored");
    check(bchk::enabled("buildchk=abc\n"), "flag: a non-number keeps the default");
    const bchk::Expect& t = bchk::table();
    check(t.n == (size_t)(t.manifest + t.extra) && t.manifest > 0, "embedded table counts " + std::to_string(t.manifest) + " manifest + " + std::to_string(t.extra) + " source = " + std::to_string(t.n));
    return 0;
}

static int run_file(const char* path, const char* want, const char* first, const char* sub)
{
    std::wstring w(path, path + std::strlen(path));
    bchk::FileImage img;
    if (!img.load(w.c_str()))
    {
        std::printf("FAIL cannot read %s\n", path);
        ++g_fail;
        return 0;
    }
    const bchk::Expect& e = bchk::table();
    bchk::Result r = bchk::run_check(img, e);
    std::string line = bchk::log_line(r, e);
    std::printf("  line: %s\n", line.c_str());
    bool wok = std::strcmp(want, "ok") == 0;
    std::string tag = std::string(path) + " -> " + want;
    if (wok)
    {
        check(r.ok && r.bad == 0 && r.unread == 0 && r.ident, tag);
        check(line == "NOLA buildchk: OK game build " + bchk::expect_text(e) + ", " + std::to_string(e.n) + " of " + std::to_string(e.n) + " signatures match (" +
                           std::to_string(e.manifest) + " hook manifest + " + std::to_string(e.extra) + " source)",
              tag + " exact OK line");
    }
    else
    {
        bool f = !r.ok;
        if (first && std::strcmp(first, "-") != 0) f = f && r.first == first;
        if (sub && *sub) f = f && has(line, sub);
        check(f, tag + " (first " + (first ? first : "-") + ", has '" + (sub ? sub : "") + "')");
        check(line.rfind("NOLA buildchk: FAIL game build ", 0) == 0 && has(line, " != " ) && has(line, "): no hooks installed, NOLA is off, the game runs unmodded"), tag + " failure line shape");
        std::printf("  message: %s\n", narrow(bchk::message(r, e)).c_str());
    }
    return 0;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && std::strcmp(argv[1], "self") == 0) run_self();
    else if (argc >= 4 && std::strcmp(argv[1], "file") == 0) run_file(argv[2], argv[3], argc > 4 ? argv[4] : nullptr, argc > 5 ? argv[5] : nullptr);
    else
    {
        std::printf("usage: test_buildchk self | file <exe> ok|fail [first] [substring]\n");
        return 2;
    }
    std::printf("buildchk: %d pass, %d fail\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
