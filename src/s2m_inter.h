#pragma once
#include <string>

namespace s2inter
{
    inline constexpr const char* kStockOwned[] = {"DeadBody"};

    template <class IsA>
    inline bool stockOwned(IsA isA)
    {
        for (const char* c : kStockOwned)
            if (isA(c)) return true;
        return false;
    }

    inline bool flip(bool owned, bool has, bool act, bool on)
    {
        if (owned) return false;
        return (has && act) != on;
    }
}
