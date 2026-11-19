#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>

namespace s2m::hitshield
{
    // One atomic snapshot keeps the entity identity and short lease consistent
    // across game/native hook threads. A stopped life tick cannot leave god mode on.
    inline std::atomic<uint64_t> state{0};
    inline uint32_t clock_ms()
    {
        return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    inline void clear() { state.store(0, std::memory_order_release); }
    inline void renew(uint32_t uid, uint32_t now)
    {
        state.store((uint64_t(now + 500u) << 32) | (uid + 1u), std::memory_order_release);
    }
    inline bool blocks(uint32_t uid, uint32_t now)
    {
        uint64_t s = state.load(std::memory_order_acquire);
        uint32_t remaining = uint32_t(s >> 32) - now;
        return s != 0 && uint32_t(s) == uid + 1u && remaining > 0 && remaining <= 500u;
    }
}
