#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "s2x_int.h"
#include "s2x_hot.h"

#include <DynamicOutput/DynamicOutput.hpp>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include <Unreal/Hooks/Hooks.hpp>
#include <Unreal/UObjectGlobals.hpp>

using namespace RC;
using namespace RC::Unreal;

namespace x0::in
{
    namespace
    {
        bool g_tickOn = false;
        bool g_coopLua = false;
        bool g_sinkOn = false;

        void sink_init()
        {
            if (g_sinkOn) return;
            g_sinkOn = true;
            log_sink().out = [](const std::string& line) { Output::send<LogLevel::Default>(STR("{}\n"), u2w(line)); };
        }

        int lua_port(const LuaMadeSimple::Lua& lua)
        {
            bool r = false;
            if (lua.is_string())
            {
                std::string_view n = lua.get_string();
                Mod m;
                if (n == "io") r = port(Mod::x0);
                else if (mod_from_name(n, m) && m != Mod::x0) r = port(m);
            }
            lua.set_bool(r);
            return 1;
        }

        int lua_status(const LuaMadeSimple::Lua& lua)
        {
            std::string s;
            if (lua.is_string()) s = status_text(lua.get_string());
            else s = status_text("");
            lua.set_string(s);
            return 1;
        }

        template <int (*F)(const LuaMadeSimple::Lua&)>
        int guarded(const LuaMadeSimple::Lua& lua)
        {
            try { return F(lua); }
            catch (const std::exception& e) { log(std::string("NOLA x0 lua seam error: ") + e.what()); lua.set_nil(); return 1; }
            catch (...) { log("NOLA x0 lua seam error"); lua.set_nil(); return 1; }
        }
    }
}

namespace x0::in
{
    bool coop_lua_seen() { return g_coopLua; }
}

namespace x0
{
    void dll_register_lua(void* w)
    {
        in::sink_init();
        auto& l = *static_cast<LuaMadeSimple::Lua*>(w);
        l.register_function("s2n_port", in::guarded<in::lua_port>);
        l.register_function("s2x_status", in::guarded<in::lua_status>);
    }

    void dll_unreal_init()
    {
        in::sink_init();
        if (in::g_tickOn) return;
        in::g_tickOn = true;
        x0::hot::cb(Unreal::Hook::RegisterEngineTickPreCallback(
            [](Unreal::Hook::TCallbackIterationData<void>&, Unreal::UEngine*, float, bool) {
                try
                {
                    in::x0_tick_frame();
                    in::hooks_tick();
                    in::x0_tick_end();
                }
                catch (...)
                {
                    static int n = 0;
                    if (++n <= 5) log("NOLA x0 tick: exception");
                }
            },
            {false, false, STR("NolaNative"), STR("x0tick")}));
        logf("NOLA x0 ready: role=%s bridge=%s", role().c_str(), in::w2u(bridge_dir()).c_str());
    }

    void dll_lua_mod_started(const wchar_t* mod_name)
    {
        if (mod_name && std::wstring_view(mod_name) == L"NolaAlife") in::g_coopLua = true;
        static bool once = false;
        if (once) return;
        once = true;
        logf("NOLA x0 lua seam registered (first Lua mod %s)", in::w2u(mod_name ? mod_name : L"?").c_str());
    }
}
