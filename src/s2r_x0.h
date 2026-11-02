#pragma once
#include <cstdint>
#include <string>

namespace s2r
{
    struct Config;
    struct Roster;
    struct Stats;
    Config x0_config();
    Config x0_config_role(bool host);
    void x0_init();
    bool x0_start();
    void x0_stop();
    bool x0_active();
    bool x0_mailbox();
    void x0_out(std::string text);
    bool x0_in(std::string& text);
    std::string x0_status();
    bool x0_bye(int wait_ms);
    void x0_pre_exit();
    bool x0_take_bye(uint64_t& seen, std::string& sid);
    bool x0_in_view(int view, std::string& text);
    bool x0_take_bye_view(int view, uint64_t& seen, std::string& sid);
    bool x0_roster(Roster& r);
    bool x0_session(int mode, const std::string& peer, int port, std::string& why);
    int x0_session_mode();
    std::string x0_session_peer();
    bool x0_dormant();
    bool x0_stats(Stats& out);
    void x0_world(const std::string& w);
}
