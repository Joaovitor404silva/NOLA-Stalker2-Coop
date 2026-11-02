#include "s2m_pid.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <Windows.h>

using namespace s2m_pid;

namespace
{
    int g_total = 0;
    int g_pass = 0;
    bool g_bad = false;
    std::string g_why;

    void expect(bool ok, const char* what, int line)
    {
        if (ok || g_bad) return;
        g_bad = true;
        g_why = std::string(what) + " (line " + std::to_string(line) + ")";
    }

#define EXPECT(x) expect((x), #x, __LINE__)

    template <class F>
    void run(const char* name, F f)
    {
        g_bad = false;
        g_why.clear();
        f();
        ++g_total;
        if (!g_bad)
        {
            ++g_pass;
            std::printf("ok   %s\n", name);
        }
        else std::printf("FAIL %s: %s\n", name, g_why.c_str());
    }

    const std::string A = "aaaaaaaaaaaaaaaa";
    const std::string B = "bbbbbbbbbbbbbbbb";
    const std::string C = "cccccccccccccccc";
    const std::string D = "0123456789abcdef";

    auto utf8_ok(const std::string& s) -> bool
    {
        size_t i = 0;
        while (i < s.size())
        {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
            if (n == 0 || i + n > s.size()) return false;
            for (size_t k = 1; k < n; ++k)
                if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
            i += n;
        }
        return true;
    }

    auto count_cp(const std::string& s) -> size_t
    {
        size_t n = 0;
        for (char c : s)
            if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
        return n;
    }

    auto temp_dir() -> std::wstring
    {
        wchar_t t[MAX_PATH];
        GetTempPathW(MAX_PATH, t);
        return std::wstring(t) + L"nola_pid_test_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    }

    void rm_dir(const std::wstring& d)
    {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((d + L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE)
        {
            do
            {
                std::wstring n = fd.cFileName;
                if (n == L"." || n == L"..") continue;
                DeleteFileW((d + n).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(d.c_str());
    }

    auto slurp(const std::wstring& p) -> std::string
    {
        std::string o;
        HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) return "<absent>";
        char b[512];
        DWORD n = 0;
        while (ReadFile(h, b, sizeof b, &n, nullptr) && n) o.append(b, n);
        CloseHandle(h);
        return o;
    }

    void spit(const std::wstring& p, const std::string& s)
    {
        HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        DWORD n = 0;
        WriteFile(h, s.data(), static_cast<DWORD>(s.size()), &n, nullptr);
        CloseHandle(h);
    }

    auto exists(const std::wstring& p) -> bool
    {
        return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES;
    }

    auto fresh_dir(const wchar_t* sub) -> std::wstring
    {
        std::wstring d = temp_dir() + sub + L"\\";
        rm_dir(d);
        CreateDirectoryW(temp_dir().c_str(), nullptr);
        CreateDirectoryW(d.c_str(), nullptr);
        return d;
    }

    const char* kGoodCampaign = "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\t0123456789abcdef\nentry\tg1\tBob\t3\thost\n";
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    run("normalize collapses spaces, tabs, newlines", [] {
        EXPECT(normalize("  Ivan \t  the   Great \n") == "Ivan the Great");
        EXPECT(normalize("\t\n ") == "");
        EXPECT(normalize("a\x01" "b") == "a b");
        EXPECT(normalize("Solo") == "Solo");
    });

    run("normalize caps ASCII at 32 and leaves no trailing space", [] {
        EXPECT(normalize(std::string(40, 'a')) == std::string(32, 'a'));
        std::string s = std::string(31, 'a') + "     b";
        EXPECT(normalize(s) == std::string(31, 'a'));
        EXPECT(normalize(std::string(32, 'a')).size() == 32);
    });

    run("normalize caps UTF-8 without cutting a sequence", [] {
        std::string two;
        for (int i = 0; i < 40; ++i) two += "\xC3\xA9";
        std::string r = normalize(two);
        EXPECT(utf8_ok(r));
        EXPECT(count_cp(r) == 32);
        EXPECT(r.size() == 64);
        std::string four;
        for (int i = 0; i < 40; ++i) four += "\xF0\x9F\x98\x80";
        r = normalize(four);
        EXPECT(utf8_ok(r));
        EXPECT(count_cp(r) == 32);
        EXPECT(r.size() == 128);
        std::string mix = std::string(30, 'a') + "\xE2\x82\xAC\xE2\x82\xAC\xE2\x82\xAC";
        r = normalize(mix);
        EXPECT(utf8_ok(r));
        EXPECT(count_cp(r) == 32);
    });

    run("normalize replaces invalid UTF-8 bytes", [] {
        std::string r = normalize("ab\xFF" "cd\xC3");
        EXPECT(r == "ab?cd?");
        EXPECT(utf8_ok(r));
    });

    run("compare form folds only A-Z", [] {
        EXPECT(compare_form("  BoB   \xC3\x84 ") == "bob \xC3\x84");
        EXPECT(compare_form("") == "");
    });

    run("empty name gives #iid", [] {
        Campaign c;
        c.cid = "c1";
        EXPECT(resolve(c, "", A, {}) == "#" + A);
        EXPECT(resolve(c, " \t ", B, {}) == "#" + B);
        EXPECT(c.members.size() == 2);
        EXPECT(c.members[0].name.empty());
    });

    run("invalid iid gives no key", [] {
        Campaign c;
        EXPECT(resolve(c, "Bob", "xyz", {}).empty());
        EXPECT(resolve(c, "Bob", "AAAAAAAAAAAAAAAA", {}).empty());
        EXPECT(c.members.empty());
    });

    run("unique name is the key", [] {
        Campaign c;
        EXPECT(resolve(c, "  Ivan   Petrov ", A, {}) == "Ivan Petrov");
        EXPECT(c.members.size() == 1);
        EXPECT(c.members[0].name == "Ivan Petrov");
        EXPECT(c.members[0].iid == A);
    });

    run("same name and iid gives the same key", [] {
        Campaign c;
        std::string k1 = resolve(c, "Bob", A, {});
        std::string k2 = resolve(c, "bob", A, {});
        std::string k3 = resolve(c, "Bob", A, {{"Zed", B}});
        EXPECT(k1 == "Bob" && k2 == "Bob" && k3 == "Bob");
        EXPECT(c.members.size() == 1);
    });

    run("rename keeps the key and updates the name", [] {
        Campaign c;
        EXPECT(resolve(c, "Bob", A, {}) == "Bob");
        EXPECT(resolve(c, "Alice", A, {}) == "Bob");
        EXPECT(c.members.size() == 1);
        EXPECT(c.members[0].name == "Alice");
        EXPECT(c.members[0].key == "Bob");
    });

    run("empty name for a known iid keeps key and name", [] {
        Campaign c;
        resolve(c, "Bob", A, {});
        EXPECT(resolve(c, "", A, {}) == "Bob");
        EXPECT(c.members[0].name == "Bob");
    });

    run("renamed from empty keeps the #iid key", [] {
        Campaign c;
        EXPECT(resolve(c, "", A, {}) == "#" + A);
        EXPECT(resolve(c, "Bob", A, {}) == "#" + A);
        EXPECT(c.members[0].name == "Bob");
    });

    run("clash: only the newcomer gets the suffix", [] {
        Campaign c;
        EXPECT(resolve(c, "Bob", A, {}) == "Bob");
        EXPECT(resolve(c, "Bob", B, {}) == "Bob#bbbbbb");
        EXPECT(resolve(c, "Bob", A, {}) == "Bob");
        EXPECT(resolve(c, "BOB", B, {}) == "Bob#bbbbbb");
        EXPECT(resolve(c, "bob", C, {}) == "bob#cccccc");
        EXPECT(c.members.size() == 3);
    });

    run("clash with a connected peer that is not yet a member", [] {
        Campaign c;
        EXPECT(resolve(c, "Bob", B, {{"bob", A}}) == "Bob#bbbbbb");
        EXPECT(resolve(c, "Eve", C, {{"bob", A}, {"Zed", D}}) == "Eve");
    });

    run("a freed plain name does not steal a renamed member key", [] {
        Campaign c;
        EXPECT(resolve(c, "Bob", A, {}) == "Bob");
        EXPECT(resolve(c, "Alice", A, {}) == "Bob");
        EXPECT(resolve(c, "Bob", B, {}) == "Bob#bbbbbb");
    });

    run("a name that looks like another player's #iid key is suffixed", [] {
        Campaign c;
        EXPECT(resolve(c, "", A, {}) == "#" + A);
        std::string k = resolve(c, "#" + A, B, {});
        EXPECT(k == "#" + A + "#bbbbbb");
    });

    run("two newcomers in one session: lower iid gets the plain name", [] {
        Campaign c;
        std::vector<std::string> k = resolve_all(c, {{"Bob", B}, {"bob", A}});
        EXPECT(k.size() == 2);
        EXPECT(k[0] == "Bob#bbbbbb");
        EXPECT(k[1] == "bob");
        Campaign c2;
        std::vector<std::string> k2 = resolve_all(c2, {{"Bob", A}, {"Bob", B}, {"Bob", C}});
        EXPECT(k2[0] == "Bob" && k2[1] == "Bob#bbbbbb" && k2[2] == "Bob#cccccc");
    });

    run("resolve_all: recorded player keeps the plain name against a lower newcomer", [] {
        Campaign c;
        EXPECT(resolve(c, "Bob", B, {}) == "Bob");
        std::vector<std::string> k = resolve_all(c, {{"Bob", A}, {"Bob", B}, {"Eve", C}, {"", D}});
        EXPECT(k[0] == "Bob#aaaaaa");
        EXPECT(k[1] == "Bob");
        EXPECT(k[2] == "Eve");
        EXPECT(k[3] == "#" + D);
    });

    run("resolve_all: invalid iid gives an empty key", [] {
        Campaign c;
        std::vector<std::string> k = resolve_all(c, {{"Bob", "nope"}, {"Eve", A}});
        EXPECT(k[0].empty() && k[1] == "Eve");
    });

    run("stability across save and load of campaign.txt", [] {
        std::wstring d = fresh_dir(L"camp");
        Campaign c;
        c.cid = "cid-77";
        std::vector<std::string> k1 = resolve_all(c, {{"Bob", B}, {"Bob", A}, {"", C}, {"Eve", D}});
        c.entries.push_back({"g1", k1[0], 4, "host"});
        c.entries.push_back({"g2", k1[1], 0, "splice"});
        c.entries.push_back({"g3", k1[2], 12, "local"});
        std::string err;
        EXPECT(save_campaign(d, c, err));
        EXPECT(!exists(d + L"campaign.txt.tmp"));
        Campaign l;
        EXPECT(load_campaign(d, l, err) == Load::ok);
        EXPECT(dump_campaign(l) == dump_campaign(c));
        std::vector<std::string> k2 = resolve_all(l, {{"Eve", D}, {"", C}, {"Bob", A}, {"Bob", B}});
        EXPECT(k2[0] == k1[3] && k2[1] == k1[2] && k2[2] == k1[1] && k2[3] == k1[0]);
        EXPECT(l.members.size() == 4);
        std::vector<std::string> k3 = resolve_all(l, {{"Bob", B}, {"Bob", A}});
        EXPECT(k3[0] == k1[0] && k3[1] == k1[1]);
        std::string again;
        EXPECT(save_campaign(d, l, again));
        EXPECT(slurp(d + L"campaign.txt") == dump_campaign(c));
        rm_dir(d);
        rm_dir(temp_dir());
    });

    run("campaign text round trip", [] {
        Campaign c;
        std::string err;
        EXPECT(parse_campaign(kGoodCampaign, c, err));
        EXPECT(dump_campaign(c) == kGoodCampaign);
        EXPECT(c.cid == "c1" && c.members.size() == 1 && c.entries.size() == 1);
        EXPECT(c.entries[0].kh == 3 && c.entries[0].made == "host" && c.entries[0].owner == "Bob");
        Campaign e;
        const char* nameless = "#NOLA campaign 1\ncid\tc2\nmember\t#0123456789abcdef\t\t0123456789abcdef\n";
        EXPECT(parse_campaign(nameless, e, err));
        EXPECT(e.members[0].name.empty());
        EXPECT(dump_campaign(e) == nameless);
        EXPECT(parse_campaign("#NOLA campaign 1\ncid\tc3", e, err));
        EXPECT(e.members.empty() && e.cid == "c3");
    });

    run("file form sanitizes", [] {
        Campaign c;
        resolve(c, "Ivan the Great", A, {});
        resolve(c, "a#b", B, {});
        resolve(c, "\xD0\xAF\xD0\xBD", C, {});
        EXPECT(file_form(c, "Ivan the Great") == "Ivan the Great");
        EXPECT(file_form(c, "a#b") == "a_b");
        EXPECT(file_form(c, "\xD0\xAF\xD0\xBD") == "____");
        EXPECT(file_form(c, "Bob#aaaaaa") == "Bob_aaaaaa");
        EXPECT(file_form(c, "x/y\\z:*?\"<>|") == "x_y_z_______");
        EXPECT(file_form(c, "ok_name-1.v2") == "ok_name-1.v2");
        EXPECT(file_form(c, "end.") == "end_");
        EXPECT(file_form(c, "CON") == "CON_");
        EXPECT(file_form(c, "lpt3.x") == "lpt3_.x");
        for (const Member& m : c.members) EXPECT(file_form(c, m.key).find('#') == std::string::npos);
    });

    run("file form collision appends _iid6 to the later member only", [] {
        Campaign c;
        resolve(c, "a#b", A, {});
        resolve(c, "A$B", B, {});
        resolve(c, "a%b", C, {});
        EXPECT(file_form(c, "a#b") == "a_b");
        EXPECT(file_form(c, "A$B") == "A_B_bbbbbb");
        EXPECT(file_form(c, "a%b") == "a_b_cccccc");
        EXPECT(file_form(c, "a&b").size() == 3 + 7);
        EXPECT(file_form(c, "a&b").find('#') == std::string::npos);
        Campaign d;
        resolve(d, "\xD0\xAF\xD0\xBD", A, {});
        resolve(d, "\xD0\x96\xD0\xB5", B, {});
        EXPECT(file_form(d, d.members[0].key) == "____");
        EXPECT(file_form(d, d.members[1].key) == "_____bbbbbb");
    });

    run("iid is created once and reused", [] {
        std::wstring d = temp_dir() + L"iid\\";
        rm_dir(d);
        rm_dir(temp_dir());
        std::string i1, i2, i3, err;
        EXPECT(load_iid(d, i1, err) == Load::absent);
        EXPECT(ensure_iid(d, i1, err));
        EXPECT(valid_iid(i1));
        EXPECT(slurp(d + L"nola_player.txt") == "#NOLA player 1\niid=" + i1 + "\n");
        EXPECT(ensure_iid(d, i2, err));
        EXPECT(i2 == i1);
        EXPECT(load_iid(d, i3, err) == Load::ok && i3 == i1);
        EXPECT(!exists(d + L"nola_player.txt.tmp"));
        rm_dir(d);
        rm_dir(temp_dir());
    });

    run("iid file without header and with CRLF is reused", [] {
        std::wstring d = fresh_dir(L"iid2");
        spit(d + L"nola_player.txt", "iid=0123456789abcdef\r\n");
        std::string i, err;
        EXPECT(ensure_iid(d, i, err));
        EXPECT(i == "0123456789abcdef");
        EXPECT(slurp(d + L"nola_player.txt") == "iid=0123456789abcdef\r\n");
        rm_dir(d);
        rm_dir(temp_dir());
    });

    run("invalid nola_player.txt is never overwritten", [] {
        std::wstring d = fresh_dir(L"iid3");
        const char* bads[] = {"#NOLA player 1\niid=XYZ\n", "#NOLA player 1\n", "iid=0123456789ABCDEF\n", "iid=0123456789abcde\n", "iid=0123456789abcdef\niid=0123456789abcdef\n", ""};
        for (const char* b : bads)
        {
            spit(d + L"nola_player.txt", b);
            std::string i = "keep", err;
            EXPECT(!ensure_iid(d, i, err));
            EXPECT(!err.empty());
            EXPECT(i == "keep");
            EXPECT(load_iid(d, i, err) == Load::bad);
            EXPECT(slurp(d + L"nola_player.txt") == b);
        }
        rm_dir(d);
        rm_dir(temp_dir());
    });

    run("generated iids are valid and differ", [] {
        std::string a, b, err;
        EXPECT(make_iid(a, err) && make_iid(b, err));
        EXPECT(valid_iid(a) && valid_iid(b) && a != b);
    });

    run("campaign parse rejects bad input", [] {
        const char* bads[] = {
            "",
            "cid\tc1\n",
            "#NOLA campaign 2\ncid\tc1\n",
            "#NOLA campaign 1\n",
            "#NOLA campaign 1\nmember\tBob\tBob\t0123456789abcdef\n",
            "#NOLA campaign 1\ncid\tc1\ncid\tc2\n",
            "#NOLA campaign 1\ncid\t\n",
            "#NOLA campaign 1\ncid\tc1\textra\n",
            "#NOLA campaign 1\ncid\tc1\n\n",
            "#NOLA campaign 1\ncid\tc1\nfoo\tx\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\t0123456789abcdef\textra\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\t0123456789ABCDEF\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\tshort\n",
            "#NOLA campaign 1\ncid\tc1\nmember\t\tBob\t0123456789abcdef\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\ta  b\t0123456789abcdef\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob \t0123456789abcdef\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\t0123456789abcdef\nmember\tbob\tBob\taaaaaaaaaaaaaaaa\n",
            "#NOLA campaign 1\ncid\tc1\nmember\tBob\tBob\t0123456789abcdef\nmember\tEve\tEve\t0123456789abcdef\n",
            "#NOLA campaign 1\ncid\tc1\r\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\tBob\t1\n",
            "#NOLA campaign 1\ncid\tc1\nentry\t\tBob\t1\thost\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\t\t1\thost\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\tBob\tx\thost\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\tBob\t-1\thost\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\tBob\t01\thost\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\tBob\t1\tmaybe\n",
            "#NOLA campaign 1\ncid\tc1\nentry\tg\tBob\t1\tHost\n",
        };
        for (const char* b : bads)
        {
            Campaign c;
            c.cid = "untouched";
            std::string err;
            bool ok = parse_campaign(b, c, err);
            if (ok || err.empty() || c.cid != "untouched")
            {
                std::string w = std::string("accepted or no error: ") + b;
                for (char& ch : w)
                    if (ch == '\n' || ch == '\t' || ch == '\r') ch = '|';
                expect(false, w.c_str(), __LINE__);
            }
        }
    });

    run("campaign load reports absent and bad", [] {
        std::wstring d = fresh_dir(L"camp2");
        Campaign c;
        std::string err;
        EXPECT(load_campaign(d, c, err) == Load::absent);
        spit(d + L"campaign.txt", "garbage\n");
        EXPECT(load_campaign(d, c, err) == Load::bad && !err.empty());
        spit(d + L"campaign.txt", kGoodCampaign);
        EXPECT(load_campaign(d, c, err) == Load::ok && c.cid == "c1");
        EXPECT(load_campaign(d.substr(0, d.size() - 1), c, err) == Load::ok);
        rm_dir(d);
        rm_dir(temp_dir());
    });

    std::printf("%s %d/%d\n", g_pass == g_total ? "PASS" : "FAIL", g_pass, g_total);
    return g_pass == g_total ? 0 : 1;
}
