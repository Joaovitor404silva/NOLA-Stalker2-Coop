#pragma once
#include <array>
#include <cstdint>
#include <vector>

namespace pcomp
{
    constexpr uint8_t kPartyLeader = 29;
    constexpr int kViews = 4;
    constexpr uint8_t kExplored = 3;
    constexpr uint32_t kUidBase = 0x0BB00000u;
    constexpr uint32_t kUidNoObject = 0x8000000u;
    constexpr uint32_t kUidReserved = 0xfffffff0u;
    constexpr int kGridSide = 0x50;
    constexpr int kGridCells = kGridSide * kGridSide;
    constexpr double kGridCell = 10150.0;
    constexpr uint32_t kNoCell = 0xffffffffu;
    constexpr uint8_t kFadeMaskStock = 0x08;
    constexpr uint8_t kFadeMaskPatched = 0x28;

    inline uint8_t start_state(uint8_t st, bool on) { return (on && st < kExplored) ? kExplored : st; }

    inline int32_t uid_for(int view) { return static_cast<int32_t>(kUidBase + static_cast<uint32_t>(view)); }

    inline bool synthetic(int32_t uid)
    {
        uint32_t u = static_cast<uint32_t>(uid);
        return u >= kUidBase && u < kUidBase + kViews;
    }

    inline int view_of(int32_t uid) { return synthetic(uid) ? static_cast<int>(static_cast<uint32_t>(uid) - kUidBase) : -1; }

    inline int grid_axis(double v)
    {
        int i = static_cast<int>(v / kGridCell);
        if (i < 1) return 0;
        return i > kGridSide - 2 ? kGridSide - 1 : i;
    }

    inline uint32_t cell(double x, double y) { return static_cast<uint32_t>(grid_axis(x) + grid_axis(y) * kGridSide); }

    inline bool cell_valid(uint32_t c) { return c < static_cast<uint32_t>(kGridCells); }

    inline bool fade_byte_known(uint8_t b) { return b == kFadeMaskStock || b == kFadeMaskPatched; }
    inline uint8_t fade_byte(bool on) { return on ? kFadeMaskPatched : kFadeMaskStock; }

    struct Op
    {
        bool add;
        int view;
        bool fresh;
    };

    using Flags = std::array<bool, kViews>;

    inline std::vector<Op> plan(const Flags& present, const Flags& want, bool on)
    {
        std::vector<Op> ops;
        for (int v = 0; v < kViews; ++v)
        {
            bool w = on && want[v];
            if (w) ops.push_back({true, v, !present[v]});
            else if (present[v]) ops.push_back({false, v, false});
        }
        return ops;
    }
}
