#pragma once
#include "s2x.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s2m_wshot
{
    bool target(const std::string& nm, x0::Ref& actor, x0::Ref& weapon, x0::Ref& body);
    std::string out_ws();
    void on_shot(const x0::Ref& self);
    void tick_ws();
    void tick_tr();
    void reset();

    namespace pure
    {
        bool ws_safe(std::string_view s);
        bool wv_safe(std::string_view s);
        struct WvRule
        {
            std::string path, vx, sid;
        };
        std::optional<WvRule> wv_rule(std::string_view path);
        std::string strip_dot(std::string_view p);
        std::string strip_class(std::string_view full);
        struct WsEnt
        {
            double sq = 0;
            std::string nm, k, pl;
            double n = 0;
            bool dir = false;
            double ap = 0, ay = 0;
        };
        std::vector<WsEnt> ws_parse(std::string_view part);
        bool has_dot_leaf(std::string_view p);
        std::string last_seg(std::string_view p);
        struct WsRow
        {
            std::string ev, fam;
            double fi = 0.1;
        };
        struct WsTab
        {
            std::map<std::string, WsRow> shot;
            std::map<std::string, std::pair<std::string, std::string>> fam;
            std::vector<std::string> mz;
            std::string pfx;
            bool ok = false;
        };
        WsTab ws_tab(const std::vector<x0::TsvRow>& rows);
    }
}
