#pragma once
#include <string_view>

namespace x0
{
    void ui_top(std::string_view id, std::string_view text, double secs = 0);
    void ui_top_clear(std::string_view id);
}
