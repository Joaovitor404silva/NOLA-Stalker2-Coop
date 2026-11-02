#pragma once
#include "s2x.h"
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace m01
{
    x0::Ref puppet();
    x0::Ref target();
    x0::Ref far_body();
    x0::Ref puppet(int view);
    x0::Ref puppet_weapon(int view);
    x0::Ref target(int view);
    void note_throw();
    double last_th_at();
    void note_partner_shot(int view);
    x0::Ref far_body(int view);
    int view_of_body(const x0::Ref& r);
    void bodies(std::vector<x0::Ref>& out);
    bool view_live(int view);
    bool is_m1_body(const x0::Ref& r);
    bool is_own_body(const x0::Ref& r);
    bool body_gate();
    bool far_on();
    bool far_on(int view);
    bool spawn_pending();
    bool world_held();
    int quests_held();
    bool quest_graph_held();
    bool adopt_pending();
    std::optional<int64_t> puppet_uid();
    std::optional<int64_t> target_uid();
    std::optional<int64_t> puppet_uid(int view);
    std::optional<int64_t> target_uid(int view);
    std::string puppet_state();
    bool target_parked();
    void ff_flinch(int view, double amount, const std::string& bone);
    bool target_parked(int view);
    void target_note(const char* why, double hp);
    void target_park(const char* why);
    void target_back();
    void target_note(int view, const char* why, double hp);
    void target_park(int view, const char* why);
    void target_back(int view);

    struct DmgEvent
    {
        int64_t seq = 0;
        double amount = 0;
        double at = 0;
    };
    struct Dmg
    {
        int64_t seq = 0;
        double total = 0;
        int64_t dropped = 0;
        std::vector<DmgEvent> list;
    };
    Dmg& dmg();
    Dmg& dmg(int view);

    void note_local_shot(const x0::Vec3& origin, const x0::Vec3& dir);
    void note_anom_quiet(double x, double y, double r, double until_t);
    double jump_until();
    double jump_until(int view);

    using OwnCheck = std::function<bool(const x0::Ref&)>;
    void add_own_check(x0::Mod m, OwnCheck f);

    struct Ext
    {
        std::function<bool()> remote_cine;
        std::function<bool()> loot_pending;
        std::function<bool(const x0::Ref&)> loot_body;
        std::function<void(const x0::Ref&)> loot_reshow;
        std::function<bool(int view)> remote_cine_v;
    };
    Ext& ext();
}

