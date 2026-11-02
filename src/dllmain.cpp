#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <chrono>
#include <string>
#include <thread>
#include <Windows.h>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <Unreal/UObjectGlobals.hpp>
#include <Unreal/UObject.hpp>
#include "s2r_x0.h"
#include "s2x.h"
#include "s2x_buildchk.h"
#include "s2x_hot.h"

using namespace RC;
using namespace RC::Unreal;

static auto utf8_to_wide(std::string_view s) -> std::wstring
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
    return w;
}

static auto wide_to_utf8(const std::wstring& w) -> std::string
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static auto lua_s2n_ping(const LuaMadeSimple::Lua& lua) -> int
{
    lua.set_string("pong");
    return 1;
}

static auto lua_s2n_find(const LuaMadeSimple::Lua& lua) -> int
{
    if (!lua.is_string())
    {
        lua.set_bool(false);
        lua.set_number(0);
        lua.set_nil();
        return 3;
    }
    std::wstring path = utf8_to_wide(lua.get_string());
    auto t0 = std::chrono::steady_clock::now();
    UObject* obj = UObjectGlobals::StaticFindObject<UObject*>(nullptr, nullptr, path);
    auto t1 = std::chrono::steady_clock::now();
    double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    lua.set_bool(obj != nullptr);
    lua.set_number(us);
    if (obj) lua.set_string(wide_to_utf8(std::wstring(obj->GetFullName())));
    else lua.set_nil();
    return 3;
}

extern "C" void s2m_downview_unload();
void s2n_ai_register(RC::LuaMadeSimple::Lua& l);
void s2n_nat_register(RC::LuaMadeSimple::Lua& l);
void s2n_save_register(RC::LuaMadeSimple::Lua& l);
void s2n_join_register(RC::LuaMadeSimple::Lua& l);
void s2n_rel_register(RC::LuaMadeSimple::Lua& l);
namespace x0
{
    void dll_register_lua(void* lua_state_wrapper);
    void dll_unreal_init();
    void dll_lua_mod_started(const wchar_t* mod_name);
}

class NolaNative : public RC::CppUserModBase
{
  public:
    NolaNative() : CppUserModBase()
    {
        ModName = STR("NolaNative");
        ModVersion = STR("0.1");
        ModDescription = STR("NOLA co-op mod (No One Left Alone)");
        ModAuthors = STR("NOLA");
        Output::send<LogLevel::Verbose>(STR("[NolaNative] loaded\n"));
    }
    ~NolaNative() override
    {
        s2m_downview_unload();
        s2r::x0_stop();
    }

    static void reg(LuaMadeSimple::Lua& l)
    {
        l.register_function("s2n_ping", lua_s2n_ping);
        l.register_function("s2n_find", lua_s2n_find);
        s2n_ai_register(l);
        s2n_nat_register(l);
        s2n_save_register(l);
        s2n_join_register(l);
        s2n_rel_register(l);
        x0::dll_register_lua(&l);
    }

    auto on_unreal_init() -> void override { x0::dll_unreal_init(); }

    auto on_lua_start(StringViewType mod_name, LuaMadeSimple::Lua& lua, LuaMadeSimple::Lua& main_lua, LuaMadeSimple::Lua& async_lua, std::vector<LuaMadeSimple::Lua*>& hook_luas) -> void override
    {
        reg(lua);
        reg(main_lua);
        reg(async_lua);
        for (auto* h : hook_luas) reg(*h);
        x0::dll_lua_mod_started(std::wstring(mod_name).c_str());
        Output::send<LogLevel::Verbose>(STR("[NolaNative] bridge registered for Lua mod {}\n"), mod_name);
    }
};

class NolaOff : public RC::CppUserModBase
{
  public:
    NolaOff() : CppUserModBase()
    {
        ModName = STR("NolaNative");
        ModVersion = STR("0.1");
        ModDescription = STR("NOLA is off: unsupported game build");
        ModAuthors = STR("NOLA");
    }
};

static auto buildchk_pass() -> bool
{
    std::string flags;
    if (x0::read_text(x0::bridge_file(L"flags.txt"), flags) && !bchk::enabled(flags))
    {
        Output::send<LogLevel::Warning>(STR("NOLA buildchk: skipped (buildchk=0 in flags.txt)\n"));
        return true;
    }
    const bchk::Expect& e = bchk::table();
    bchk::Result r = bchk::run_check(bchk::MemImage(GetModuleHandleW(nullptr)), e);
    std::wstring line = utf8_to_wide(bchk::log_line(r, e));
    if (r.ok)
    {
        Output::send<LogLevel::Normal>(STR("{}\n"), line);
        return true;
    }
    Output::send<LogLevel::Error>(STR("{}\n"), line);
    std::wstring msg = bchk::message(r, e);
    try
    {
        std::thread([msg] { MessageBoxW(nullptr, msg.c_str(), L"NOLA", MB_OK | MB_ICONWARNING | MB_SETFOREGROUND); }).detach();
    }
    catch (...)
    {
    }
    return false;
}

static auto make_mod() -> RC::CppUserModBase*
{
    if (!buildchk_pass()) return new NolaOff();
    return new NolaNative();
}

extern "C"
{
    __declspec(dllexport) RC::CppUserModBase* start_mod() { return x0::hot::wrap(make_mod()); }
#ifndef S2X_RELEASE
    __declspec(dllexport) RC::CppUserModBase* nola_hot_start() { return make_mod(); }
#endif
    __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod) { delete mod; }
}
