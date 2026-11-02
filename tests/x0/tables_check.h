#pragma once
#include "s2x_int.h"
#include <Windows.h>
#include <algorithm>
#include <functional>
#include <set>
#include <string>
#include <vector>

inline int tc_rows(const std::string& t)
{
    int n = 0;
    for (auto& l : x0::split(t, '\n', false))
    {
        std::string s = l;
        s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
        if (s.find_first_not_of(" \t") != std::string::npos) ++n;
    }
    return n;
}

inline std::wstring tc_empty_dir(const wchar_t* tag)
{
    wchar_t b[MAX_PATH];
    GetTempPathW(MAX_PATH, b);
    std::wstring d = std::wstring(b) + L"s2x_tab_" + tag + L"_" + std::to_wstring(GetCurrentProcessId()) + L"\\";
    CreateDirectoryW(d.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((d + L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE)
    {
        do
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((d + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return d;
}

inline void tc_tables(const std::function<void(bool, const std::string&)>& check, const wchar_t* tag)
{
    std::wstring dir = tc_empty_dir(tag);
    std::string role = x0::boot_role();
    std::wstring old = x0::bridge_dir();
    x0::in::set_role_for_test(role, dir);
    std::string list;
    check(x0::data("nola_runtime.tsv", list) && !list.empty(), "tables: nola_runtime.tsv embedded");
    std::set<std::string> listed;
    int na = 0;
    for (auto& l : x0::split(list, '\n', false))
    {
        auto c = x0::split(l, '\t', true);
        if (c.size() < 5 || c[0] != "file" || c[2] != "a") continue;
        ++na;
        std::string rn;
        for (char ch : c[1]) rn += (ch >= 'a' && ch <= 'z') ? (char)(ch - 32) : ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) ? ch : '_';
        listed.insert(rn);
        std::string t;
        bool ok = x0::table(c[1], t);
        check(ok, "tables: " + c[1] + " loads with an empty bridge folder");
        std::string raw;
        bool rawok = x0::data(c[1], raw);
        raw.erase(std::remove(raw.begin(), raw.end(), '\r'), raw.end());
        check(ok && rawok && tc_rows(t) > 0 && t == raw, "tables: " + c[1] + " rows " + std::to_string(tc_rows(t)) + " == embedded " + std::to_string(tc_rows(raw)));
        check(t.find('\r') == std::string::npos, "tables: " + c[1] + " has no CR");
    }
    check(na >= 44, "tables: class a rows " + std::to_string(na));
    auto names = x0::data_names();
    check((int)names.size() == na, "tables: embedded resources " + std::to_string(names.size()) + " == class a rows " + std::to_string(na));
    for (auto& n : names) check(listed.count(n) != 0, "tables: resource " + n + " is listed in nola_runtime.tsv");
    std::string t;
    check(!x0::table("taken.txt", t), "tables: a state file has no embedded copy");
    x0::write_text(dir + L"s2u_kit.tsv", "Bandage\t9\n");
    check(x0::table("s2u_kit.tsv", t) && t == "Bandage\t9\n", "tables: the bridge copy wins over the embedded one");
    x0::remove_file(dir + L"s2u_kit.tsv");
    x0::fset("taball", x0::Val::I(0), "test");
    check(!x0::table("s2u_trade.tsv", t), "tables: taball=0 reads the bridge only");
    x0::fset("taball", x0::Val::I(1), "test");
    check(x0::table("s2u_trade.tsv", t) && tc_rows(t) == 107, "tables: taball=1 trade table back");
    RemoveDirectoryW(dir.c_str());
    x0::in::set_role_for_test(role, old);
}
