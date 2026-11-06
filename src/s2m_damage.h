#pragma once
#include <functional>
#include <string>
#include <vector>
#include "s2x.h"

namespace s2m::damage
{
    struct FfBody
    {
        int view = 0;
        int pid = 0;
        x0::Ref body;
        bool puppet = false;
    };
    struct FfHit
    {
        int view = 0;
        int pid = 0;
        bool puppet = false;
        double f[5] = {};
        int ty = 0, src = 0, b7a = 0, b7b = 0;
        int64_t bn = 0;
        double v[6] = {};
        std::string bone;
    };
    struct MdHit
    {
        int64_t uid = 0;
        double f[5] = {};
        int ty = 0, src = 0, b7a = 0, b7b = 0;
        int64_t bn = 0;
        double v[6] = {};
        std::string bone;
    };
    struct Links
    {
        std::function<bool(const x0::Ref& npc)> shot_deny;
        std::function<bool()> nd_dead;
        std::function<bool()> host_cine;
        std::function<bool()> cine_frozen;
        std::function<bool()> nd_on;
        std::function<int(double&)> last_cause;
        std::function<bool()> god_other;
        // Invoke once, synchronously; the caller verifies the inventory change.
        std::function<bool(const std::string& sid)> item_take;
        std::function<bool(const std::string& sid)> item_refund;
        std::function<void()> die_follow;
        std::function<void(bool on, const std::vector<FfBody>& bodies)> ff_bodies;
        std::function<std::vector<FfHit>()> ff_take;
        std::function<int(const x0::Ref& pawn, const FfHit& hit)> ff_apply;
        std::function<std::string(bool& any)> ff_stat;
        std::function<void(bool on)> md_cfg;
        std::function<std::vector<MdHit>()> md_take;
        std::function<x0::Ref(int64_t uid)> md_find;
        std::function<int(const x0::Ref& npc, const MdHit& hit, int64_t atk)> md_apply;
        std::function<std::string(bool& any)> md_stat;
    };
    Links& links();

    double revive_start_at();
    void protect_until(double t);
    void down_until(double t);
    void respawn_reset(double protect_t);

    bool remote_life(char& st, double& hp, double& mx);
    bool remote_down();
    bool remote_life(int view, char& st, double& hp, double& mx);
    bool remote_down(int view);
    bool local_downed();
    bool life_frozen_now();
    bool life_god_now();
    double last_shot_heard();
    int shots_heard();

    bool route_nd();
    bool nd_send_allowed();
    bool nd_send_allowed(int view);
    bool nd_apply_allowed();
    void nd_sent(double amount);
    void nd_applied(double amount, double hp0, double hp1);
}

extern "C" bool s2m_damage_life_frozen();
extern "C" bool s2m_damage_god_held();
extern "C" bool s2m_life_gm_keep(bool on);
extern "C" int s2m_life_knock(uint64_t pawn, int on);
extern "C" int s2m_life_kdhold(uint64_t pawn, int on);
extern "C" double s2m_life_kdout_left(uint64_t pawn);
extern "C" double s2m_life_lowhp();
extern "C" void s2m_downview_unload();
extern "C" bool s2m_life_load_block(bool on);
extern "C" int s2m_life_load_tries();
extern "C" int s2m_life_load_hidden(int* builds);
extern "C" int s2m_load_begins(char* guid33);
