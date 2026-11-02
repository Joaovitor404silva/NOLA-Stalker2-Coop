#pragma once
#include <cstdint>
#include <string>
#include <vector>

void s2n_nat_install();

namespace s2nat
{
    std::string host(int mode, uint64_t me, double rx, double ry, double rz, uint64_t pup, uint64_t tgt);
    struct Player { int view = 0; int pid = 0; double x = 0, y = 0, z = 0; uint64_t pup = 0, tgt = 0; };
    void host_views(uint64_t me, const std::vector<Player>& pl);
    std::string host_view(int view);
    struct Body { int pid = 0; uint64_t a = 0; };
    void bodies(int mypid, const std::vector<Body>& others);
    void cfg_mc(int on);
    int mc_on();
    bool client(int mode, const std::string& nn, const std::string& nl, uint64_t me, uint64_t pup, uint64_t tgt, int busy, int via);
    bool own(uint64_t a);
    bool dead(const std::string& hid);
    uint64_t actor(const std::string& nm, uint64_t& weapon, uint64_t& mesh);
    bool listed(int32_t uid);
    bool listed_addr(uint64_t a);
    bool park_twin(uint64_t a, int32_t uid);
    bool corpse(const std::string& hid, double& x, double& y, double& z);
    int natbr();
    void natcb(int on);
    void cfg_a1(int lookst, int park, double hys, double parkage);
    struct FfBody { int view = 0; int pid = 0; uint64_t a = 0; int puppet = 0; };
    struct FfHit { int view = 0; int pid = 0; int puppet = 0; float f[5] = {}; uint8_t ty = 0, src = 0, b7a = 0, b7b = 0; uint32_t bn = 0; double v[6] = {}; std::string bone; };
    void ff_bodies(int on, const std::vector<FfBody>& b);
    std::vector<FfHit> ff_take();
    int ff_apply(uint64_t actor, const FfHit& h);
    std::string ff_status(bool& any);
    struct MdHit { int32_t uid = 0; float f[5] = {}; uint8_t ty = 0, src = 0, b7a = 0, b7b = 0; uint32_t bn = 0; double v[6] = {}; std::string bone; };
    void md_cfg(int on);
    std::vector<MdHit> md_take();
    uint64_t md_find(int32_t uid);
    int md_apply(uint64_t actor, const MdHit& h, int64_t atk);
    std::string md_status(bool& any);
    void cfg_dmg(int on);
    int nd_on();
    int last_cause(double& age);
    void cfg_m1(int mut, int adoptKept, int offanim, int offvb);
    void cfg_vis(int on);
    void cfg_qg(int on);
    void cfg_fd(int on);
    void cfg_deadall(int on);
    void cfg_hotkeep(int on);
    void cfg_deadkeep(int on);
    void cfg_deadpos(int on);
    void gc_keep(std::vector<uint64_t>& out);
    void cfg_wide(int on);
    void cfg_rhost(int on);
    void cfg_far(int on, double deadR);
    void cfg_wpn(int sid, int arm);
    void cfg_cpd(int on, int hp);
    void cfg_cover(int on);
    void cfg_msec(int on);
    void cfg_mre(int on);
    void cfg_genmre(int on);
    void cfg_mloop(int on);
    void cfg_msecq(int on);
    void cfg_mkeep(int on);
    void cfg_movl(int on);
    void cfg_rmoff(int on);
    void cfg_navw(int on);
    void cfg_wpnhid(int on);
    void cfg_looksec(int on);
    void cfg_mutlook(int on);
    void cfg_genmk(int on);
    void cfg_midsc(int on);
    int cfg_lookre();
    void cfg_gait(int on);
    void cfg_mab(int on);
    void cfg_cloak(int on);
    void cfg_copy(int on);
    void cfg_sync(int legs, int mbh, int qgsafe, int lostfix, int eqfix);
    void cfg_loco(int kinmv, int eqsync, int turn);
    void cfg_kin(int kin, int ord, int stop, int req, int pin);
    void cfg_gaitcp(int on);
    void cfg_wpnlocal(int on);
    void cfg_loco_host(int on);
    void cfg_loco_prm(int on);
    void cfg_loco_fl(int on);
    void cfg_traj(int on);
    void cfg_traj_v(int on);
    void cfg_loco_sf(int on);
    void cfg_traj_own(int on);
    void cfg_cp_vel(int on);
    void cfg_props(int on);
    void cfg_dupfix(int on);
    void cfg_inert(int on, int dsrc, uint64_t me, uint64_t pup, uint64_t tgt);
    bool inert(uint64_t a, int32_t uid, int on);
    struct ClsRow { std::string cls, kind, sid; double hpmax = 0, walk = 0, run = 0, sprint = 0, invis = 0; };
    void classes(const std::vector<ClsRow>& rows);
    std::string lk_hold(const std::string& slot, const std::string& key, double age, double lease, double hmax);
    int lk_off(uint64_t a, int on, double lease);
    bool cl_swap(const std::string& hid, uint64_t loot, uint64_t src, int& mism);
    int cl_swap_tick();
    uint64_t body(const std::string& hid, int& st);
    void cl_own(uint64_t a, int on);
    bool qi_npcadd(const std::string& sid);
    bool qr_npc(const std::string& sid, std::string& guid);
    std::string b164_status();
#ifndef S2X_RELEASE
    struct CenNat { uint64_t a = 0; int32_t uid = 0; int kind = 0; int st = 0; bool shown = false; int32_t luid = 0; double hx = 0, hy = 0, hz = 0; int hp = 0; double age = -1; std::string sid; };
    void census_nat(std::vector<CenNat>& out, int& hu, int& hk_live);
    bool census_hk(int32_t uid);
#endif
    void feed_mode(bool on);
    void feed(const std::string& text);
}
