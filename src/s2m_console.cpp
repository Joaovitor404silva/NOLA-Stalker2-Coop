#include "s2x.h"
#include "s2m_quest.h"

#include <string>
#include <string_view>
#include <vector>

namespace s2m_console
{
    std::string cmd_name(std::string_view full)
    {
        std::vector<std::string> parts;
        std::string cur;
        for (char c : full)
        {
            if (c == ' ')
            {
                if (!cur.empty()) parts.push_back(cur);
                cur.clear();
                continue;
            }
            cur.push_back(c);
        }
        if (!cur.empty()) parts.push_back(cur);
        if (parts.size() > 1) return parts[0];
        return std::string(full);
    }

    bool handle(std::string_view full)
    {
        if (cmd_name(full) != "s2q") return false;
        try { s2m_quest::console(full); }
        catch (...) {}
        return true;
    }
}

#ifndef S2M_CONSOLE_PURE
#include <Unreal/Hooks/Hooks.hpp>
#include <windows.h>
#include "s2x_hot.h"

namespace
{
    std::string utf8(const wchar_t* w)
    {
        if (!w || !*w) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 1) return {};
        std::string s((size_t)n - 1, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    void console_init()
    {
        bool ok = true;
        std::string err;
        try
        {
            x0::hot::cb(RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                [](auto& info, RC::Unreal::UObject*, const RC::Unreal::TCHAR* cmd, RC::Unreal::FOutputDevice&, RC::Unreal::UObject*) {
                    if (!x0::port(x0::Mod::quest)) return;
                    if (s2m_console::handle(utf8(reinterpret_cast<const wchar_t*>(cmd)))) info.TrySetReturnValue(true);
                },
                {false, false, STR("NolaNative"), STR("s2q")}));
        }
        catch (const std::exception& e)
        {
            ok = false;
            err = e.what();
        }
        x0::log(std::string("NOLA quest: RegisterConsoleCommandHandler s2q ok=") + (ok ? "true" : "false") + " " + err);
    }
    ::x0::ModReg g_console_reg(::x0::Mod::quest, "console", &console_init);
}
#endif
