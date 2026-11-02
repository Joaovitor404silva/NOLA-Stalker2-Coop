#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <string>
#include <cwctype>
#include <Windows.h>
#ifndef S2N_SAVE_TEST
#include <LuaMadeSimple/LuaMadeSimple.hpp>
using namespace RC;
#endif

namespace s2n_save
{
    static SRWLOCK g_fl = SRWLOCK_INIT;
    static std::wstring g_folder = L"SaveGames";

    static auto folder() -> std::wstring
    {
        AcquireSRWLockShared(&g_fl);
        std::wstring f = g_folder;
        ReleaseSRWLockShared(&g_fl);
        return f;
    }

    static auto narrow(const std::wstring& w) -> std::string
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    static auto suffix() -> std::wstring
    {
        std::wstring cl = GetCommandLineW() ? GetCommandLineW() : L"";
        std::wstring lo = cl;
        for (auto& c : lo) c = (wchar_t)std::towlower(c);
        const std::wstring key = L"-saveddirsuffix=";
        size_t p = lo.find(key);
        if (p == std::wstring::npos) return {};
        p += key.size();
        bool q = p < cl.size() && cl[p] == L'"';
        if (q) ++p;
        size_t e = p;
        while (e < cl.size() && (q ? cl[e] != L'"' : (cl[e] != L' ' && cl[e] != L'\t'))) ++e;
        return cl.substr(p, e - p);
    }

    static auto saved_dir() -> std::wstring
    {
        wchar_t buf[MAX_PATH * 2] = {};
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
        if (n == 0 || n >= sizeof(buf) / sizeof(buf[0])) return {};
        std::wstring d = std::wstring(buf, n) + L"\\Stalker2\\Saved";
        std::wstring s = suffix();
        if (!s.empty()) d += L"_" + s;
        return d + L"\\";
    }

    static auto stat(const std::wstring& p, ULONGLONG& mt, ULONGLONG& sz) -> bool
    {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) return false;
        mt = ((ULONGLONG)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime;
        sz = ((ULONGLONG)a.nFileSizeHigh << 32) | a.nFileSizeLow;
        return true;
    }

    static auto scan(const std::wstring& dir, std::string& stamp) -> std::wstring
    {
        const wchar_t* plats[] = {L"STEAM", L"NULL"};
        std::wstring best, fo = folder();
        ULONGLONG bestMt = 0;
        stamp.clear();
        for (auto* pl : plats)
        {
            ULONGLONG mt = 0, sz = 0;
            bool ok = stat(dir + pl + L"\\" + fo + L"\\CampaignsSave.sav", mt, sz);
            if (!stamp.empty()) stamp += ";";
            stamp += narrow(pl) + "=" + (ok ? (std::to_string(mt) + ":" + std::to_string(sz)) : std::string("-"));
            if (ok && (best.empty() || mt > bestMt)) { best = pl; bestMt = mt; }
        }
        return best;
    }

    static auto data(const std::wstring& dir, const std::wstring& plat, bool all) -> std::string
    {
        if (plat.empty()) return {};
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((dir + plat + L"\\" + folder() + L"\\Data\\*.sav").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return {};
        std::string out;
        ULONGLONG bestMt = 0;
        do
        {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring nm = fd.cFileName;
            if (nm.size() > 4) nm.resize(nm.size() - 4);
            ULONGLONG mt = ((ULONGLONG)fd.ftLastWriteTime.dwHighDateTime << 32) | fd.ftLastWriteTime.dwLowDateTime;
            if (all) { if (!out.empty()) out += ","; out += narrow(nm); }
            else if (out.empty() || mt > bestMt) { out = narrow(nm); bestMt = mt; }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        return out;
    }

#ifndef S2N_SAVE_TEST
    static auto lua_info(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring dir = saved_dir();
        if (dir.empty()) { lua.set_nil(); lua.set_string("LOCALAPPDATA ausente"); return 2; }
        std::string stamp;
        std::wstring plat = scan(dir, stamp);
        lua.set_string(narrow(dir));
        lua.set_string(stamp);
        lua.set_string(narrow(plat));
        lua.set_string(data(dir, plat, false));
        return 4;
    }

    static auto lua_list(const LuaMadeSimple::Lua& lua) -> int
    {
        std::wstring dir = saved_dir();
        std::string stamp;
        std::wstring plat = dir.empty() ? std::wstring() : scan(dir, stamp);
        lua.set_string(data(dir, plat, true));
        return 1;
    }
#endif
}

extern "C" bool s2n_save_api_info(std::string* dir, std::string* stamp, std::string* plat, std::string* newest)
{
    std::wstring d = s2n_save::saved_dir();
    if (d.empty()) return false;
    std::string st;
    std::wstring pl = s2n_save::scan(d, st);
    *dir = s2n_save::narrow(d);
    *stamp = st;
    *plat = s2n_save::narrow(pl);
    *newest = s2n_save::data(d, pl, false);
    return true;
}

extern "C" bool s2n_save_api_list(std::string* out)
{
    std::wstring d = s2n_save::saved_dir();
    std::string st;
    std::wstring pl = d.empty() ? std::wstring() : s2n_save::scan(d, st);
    *out = s2n_save::data(d, pl, true);
    return true;
}

extern "C" void s2n_save_api_set_folder(const std::wstring* f)
{
    if (!f || f->empty()) return;
    AcquireSRWLockExclusive(&s2n_save::g_fl);
    s2n_save::g_folder = *f;
    ReleaseSRWLockExclusive(&s2n_save::g_fl);
}

extern "C" bool s2n_save_api_folder(std::wstring* out)
{
    if (!out) return false;
    *out = s2n_save::folder();
    return true;
}

#ifndef S2N_SAVE_TEST
void s2n_save_register(RC::LuaMadeSimple::Lua& l)
{
    l.register_function("s2n_save_info", s2n_save::lua_info);
    l.register_function("s2n_save_list", s2n_save::lua_list);
}
#endif
