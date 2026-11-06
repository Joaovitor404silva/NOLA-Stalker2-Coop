#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include "s2x.h"

namespace s2m_pop { struct VendHit; }

namespace s2m_lock
{
    struct MqEpisode
    {
        double since = -1;
        bool on = false;
    };

    bool lead_mine();
    bool lead_dlg();
    bool host_dlg_open();
    x0::Ref dialog_on_screen(double ttl_s = -1);
    bool peer_leads();
    int lead_owner_pid();
    bool shot_deny(const x0::Ref& npc);
    bool trig_deny(std::string_view sid, std::string_view seq);
    void trig_entered(std::string_view sid, double x, double y, double z);
    bool use_deny(const x0::Ref& obj, std::string_view key);
    bool is_main(std::string_view sid);
    void mq_seen(std::string_view sid);
    int mq_flag(bool episode_on, bool sleeping, bool dialog, MqEpisode& ep);
    bool mq_ok(bool main_episode);
    bool mq_sid(std::string_view sid);
    std::string story_guid(const x0::Ref& npc);
    void set_beds_blocked_probe(std::function<bool()> fn);

    namespace detail
    {
        struct NpcSnap
        {
            x0::Ref ref;
            x0::Ref comp;
            uint64_t addr = 0;
            bool alive = false;
            bool is_local = false;
            bool own = false;
            bool comp_ok = false;
            bool active = false;
            bool vendor = false;
            x0::Vec3 pos;
        };

        struct CompSnap
        {
            std::string name;
            x0::Ref ref;
            bool active = false;
        };

        struct UseSnap
        {
            x0::Ref obj;
            std::string hex;
            std::string key;
            std::vector<CompSnap> comps;
        };

        struct UseComp
        {
            x0::Ref comp;
            std::string hex;
        };

        struct World
        {
            virtual ~World() = default;
            virtual bool local_pawn(uint64_t& addr, x0::Vec3& pos) = 0;
            virtual x0::Ref dialog_view() = 0;
            virtual x0::Ref vend_view() = 0;
            virtual bool interacted_actor(x0::Vec3* pos) = 0;
            virtual bool interacted_vendor(s2m_pop::VendHit& out) = 0;
            virtual int answers_vis(const x0::Ref& view) = 0;
            virtual bool set_answers_vis(const x0::Ref& view, int vis) = 0;
            virtual bool close_dialog(const x0::Ref& view) = 0;
            virtual bool open_trade(const x0::Ref& view, bool upgrade) = 0;
            virtual int trade_hint_vis(const x0::Ref& view) = 0;
            virtual std::vector<NpcSnap> humans() = 0;
            virtual bool set_active(const x0::Ref& comp, bool on) = 0;
            virtual std::string npc_guid(const x0::Ref& npc) = 0;
            virtual bool npc_pos(const x0::Ref& npc, x0::Vec3& pos) = 0;
            virtual std::string npc_name(const x0::Ref& npc) = 0;
            virtual x0::Ref placeholder_actor(const std::string& hex) = 0;
            virtual std::vector<UseSnap> usables(const x0::Vec3& center, double r2, const std::function<bool(const std::string&)>& want) = 0;
            virtual bool use_hex(const x0::Ref& obj, std::string& hex) = 0;
            virtual std::vector<UseComp> inactive_use_comps() = 0;
            virtual bool beds(int& total, int& off) = 0;
            virtual bool scene_take() { return false; }
        };

        class Engine
        {
          public:
            explicit Engine(World& w);
            ~Engine();
            Engine(const Engine&) = delete;
            Engine& operator=(const Engine&) = delete;
            void vend_trade_tick();
            void ld_tick();
            void ch_use_tick();
            void ld_blk_tick();
            void lg_tick();
            std::string ld_out();
            std::string lo_out();
            int owner_pid();
            bool mine();
            bool mine_dlg();
            bool dlg_open();
            bool peer();
            std::string owner();
            bool shot_deny(const x0::Ref& npc);
            bool trig_deny(std::string_view sid, std::string_view seq);
            void trig_entered(std::string_view sid, double x, double y, double z);
            bool use_deny(const x0::Ref& obj, std::string_view key);
            std::string story_guid(const x0::Ref& npc);
            void set_beds_probe(std::function<bool()> fn);

          private:
            struct Impl;
            std::unique_ptr<Impl> p_;
        };

        void bind(Engine* e);
    }
}
