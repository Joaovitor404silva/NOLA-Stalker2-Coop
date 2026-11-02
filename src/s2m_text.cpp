#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <string>

#include "s2x.h"
#include "s2m_text.h"

namespace
{
    std::wstring g_seen;

    std::wstring probe()
    {
        if (!x0::on_game_thread()) return {};
        x0::Ref k = x0::find_path(L"/Script/Engine.Default__KismetInternationalizationLibrary");
        if (!k) return {};
        x0::Call c(k, L"GetCurrentLanguage");
        if (!c.ok() || !c.run()) return {};
        std::wstring l = c.rs();
        if (l != g_seen)
        {
            g_seen = l;
            x0::logf("NOLA text: game language %ls: texts in %s", l.c_str(), nola_text::lang_name(nola_text::lang_of(l)));
        }
        return l;
    }

    struct Reg
    {
        Reg() { nola_text::set_probe(&probe); }
    } g_reg;
}
