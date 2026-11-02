#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace s2n_ds
{
    enum Cat { NoAtk = 0, Self = 1, Copy = 2, Inert = 3, Puppet = 4, Target = 5, LocalHidden = 6, LocalVisible = 7, NoPawn = 8, Other = 9 };
    constexpr uint32_t kAnomaly = 0x8000000u;
    constexpr int64_t kTtlMs = 5000;
    constexpr size_t kMax = 256;

    inline int classify(uint32_t atk, uint32_t plId, bool found, bool ours, bool inert, bool pup, bool tgt, bool hidden)
    {
        if (atk >= kAnomaly) return NoAtk;
        if (atk == plId) return Self;
        if (!found) return NoPawn;
        if (ours) return Copy;
        if (inert) return Inert;
        if (pup) return Puppet;
        if (tgt) return Target;
        return hidden ? LocalHidden : LocalVisible;
    }
    inline bool is_local(int c) { return c == LocalHidden || c == LocalVisible; }

    struct Tracker
    {
        std::vector<std::pair<uint32_t, int64_t>> v;
        void note(uint32_t id, int64_t now)
        {
            for (auto& p : v) if (p.first == id) { p.second = now; return; }
            if (v.size() < kMax) v.emplace_back(id, now);
        }
        bool drop(uint32_t atk, uint32_t plId, uint8_t ty, uint8_t src, int64_t now)
        {
            if (atk >= kAnomaly || atk == plId || ty == 10 || src == 19) return false;
            bool hit = false;
            for (size_t i = 0; i < v.size();)
            {
                if (now - v[i].second > kTtlMs) { v.erase(v.begin() + i); continue; }
                if (v[i].first == atk) hit = true;
                ++i;
            }
            return hit;
        }
    };
}
