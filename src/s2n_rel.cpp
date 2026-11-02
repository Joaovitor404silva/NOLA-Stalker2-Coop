#include <string>
#include <LuaMadeSimple/LuaMadeSimple.hpp>
#include "s2n_rel_core.inc"

using namespace RC;

namespace
{
    auto uid_arg(const LuaMadeSimple::Lua& lua, uint32_t& u) -> bool
    {
        if (!lua.is_number()) return false;
        double d = lua.get_number();
        if (d < 0 || d > 4294967279.0) return false;
        u = static_cast<uint32_t>(d);
        return true;
    }

    auto lua_rel(const LuaMadeSimple::Lua& lua) -> int
    {
        uint32_t a, b;
        if (!uid_arg(lua, a) || !uid_arg(lua, b)) { lua.set_nil(); lua.set_string("uso: s2n_rel(uidA, uidB)"); return 2; }
        int32_t v = 0; int lev = -1;
        int e = s2rel::get(a, b, v, lev);
        if (e) { lua.set_nil(); lua.set_string(e == 1 ? std::string("natives: ") + s2rel::N.why : "e=" + std::to_string(e)); return 2; }
        lua.set_integer(v);
        lua.set_integer(lev);
        return 2;
    }

    auto lua_status(const LuaMadeSimple::Lua& lua) -> int
    {
        bool ok = s2rel::resolve();
        lua.set_bool(ok);
        lua.set_string(s2rel::N.why);
        return 2;
    }

    template <int (*F)(const LuaMadeSimple::Lua&)>
    auto guarded(const LuaMadeSimple::Lua& lua) -> int
    {
        try { return F(lua); }
        catch (...) { lua.set_nil(); lua.set_string("s2n_rel: excecao C++"); return 2; }
    }
}

void s2n_rel_register(RC::LuaMadeSimple::Lua& l)
{
    l.register_function("s2n_rel", guarded<lua_rel>);
    l.register_function("s2n_rel_status", guarded<lua_status>);
}
