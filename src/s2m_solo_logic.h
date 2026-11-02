#pragma once
#include <cstdint>

namespace solo
{
    enum Pol : long { polStock = 0, polHost = 1, polClient = 2 };
    constexpr uint32_t kAutoBit = 1u << 1;
    constexpr uint32_t kClientMask = 0x400u | kAutoBit;
    constexpr uint32_t kAutoType = 1;

    inline uint32_t eff_mask(uint32_t st, long pol, bool joinPass)
    {
        uint32_t m = pol == polHost ? (st & ~kAutoBit) : pol == polClient ? kClientMask : st;
        return joinPass ? (m | kAutoBit) : m;
    }

    struct PassCount
    {
        int asked = 0, allowed = 0;
    };

    inline void count_pass(PassCount& c, bool pass, uint32_t type, bool result)
    {
        if (!pass || type != kAutoType) return;
        ++c.asked;
        if (result) ++c.allowed;
    }
}
