#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace pcomp
{
    constexpr uint8_t kPartyLeader = 29;
    constexpr int kViews = 4;
    constexpr uint8_t kExplored = 3;

    inline uint8_t start_state(uint8_t st, bool on) { return (on && st < kExplored) ? kExplored : st; }

    struct Op
    {
        bool add;
        int32_t uid;
        int view;
    };

    using Uids = std::array<int32_t, kViews>;

    inline int32_t uid32(int64_t u) { return (u > 0 && u <= INT32_MAX) ? static_cast<int32_t>(u) : 0; }

    inline std::vector<Op> step(Uids& cur, const Uids& want, bool on)
    {
        std::vector<Op> ops;
        for (int v = 0; v < kViews; ++v)
        {
            int32_t w = on ? want[v] : 0;
            if (cur[v] && cur[v] != w) ops.push_back({false, cur[v], v});
            if (w) ops.push_back({true, w, v});
            cur[v] = w;
        }
        return ops;
    }

    inline bool changed(const Uids& before, const Uids& after) { return before != after; }
}
