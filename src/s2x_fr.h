#pragma once
#include <cstdint>
#include <string_view>

#if defined(S2X_FR) && !defined(S2X_RELEASE)
namespace x0::fr
{
    void trigger(std::string_view reason);
    void track(std::string_view tag);
    void handled(std::string_view tag, std::string_view value);
    void event(uint8_t fam, std::string_view tag, std::string_view text);
    void on_log(std::string_view line);
    void tx_snap(std::string_view sid, int64_t f, size_t bytes, std::string_view x1);
    void tx_view(int view, int peer_pid, int64_t f, std::string_view x1);
    void rx_snap(int view, int peer_pid, std::string_view text, std::string_view sid, std::string_view x1raw);
    void rx_drop(int view, int peer_pid, std::string_view why);
    void state(std::string_view what);
    void session_end(std::string_view why);
}
namespace x0
{
    inline void fr_trigger(std::string_view reason) { fr::trigger(reason); }
}
#define S2FR(x) x
#else
namespace x0
{
    inline void fr_trigger(std::string_view) {}
}
#define S2FR(x) ((void)0)
#endif
