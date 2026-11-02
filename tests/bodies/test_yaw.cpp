#include "s2m_bodies_logic.h"
#include "s2m_throw_logic.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <string>

namespace lg = m01::lg;

static std::map<std::string, double> g_fl;
static double FL(const char* k, double d)
{
    auto it = g_fl.find(k);
    return it == g_fl.end() ? d : it->second;
}
static std::string FS(const char*) { return "nil"; }
static void LOG(const std::string&) {}
static const lg::Ctx C{&FL, &FS, &LOG};

static int fails = 0;
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}

int main()
{
    lg::YawHold h;
    std::optional<double> body = 10.0;
    auto w = lg::yaw_hold(C, h, true, 12.0, body, 0);
    check(w && *w == 10.0, "idle holds the body yaw");
    check(!lg::yaw_hold_turn(C, h, std::string("turnr")), "no turn while held, feed ignored");
    w = lg::yaw_hold(C, h, true, 54.0, body, 0.1);
    check(w && *w == 10.0, "44 deg still held");
    w = lg::yaw_hold(C, h, true, 56.0, body, 0.2);
    check(w && *w == 56.0 && h.dir == 1, "45 deg releases to the aim with a right turn");
    check(lg::yaw_hold_turn(C, h, std::nullopt) == std::optional<std::string>("turnr"), "turn clip right");
    body = 55.0;
    w = lg::yaw_hold(C, h, true, 57.0, body, 0.3);
    check(w && *w == 57.0 && h.dir == 1, "turn kept until pupyawt");
    w = lg::yaw_hold(C, h, true, 57.0, body, 0.7);
    check(h.dir == 0, "turn ends near the aim after pupyawt");
    body = 57.0;
    w = lg::yaw_hold(C, h, true, 20.0, body, 0.8);
    check(w && *w == 57.0, "held again after the turn");
    w = lg::yaw_hold(C, h, true, 0.0, body, 0.9);
    check(w && *w == 0.0 && h.dir == -1, "left release");
    check(lg::yaw_hold_turn(C, h, std::nullopt) == std::optional<std::string>("turnl"), "turn clip left");
    lg::YawHold m;
    w = lg::yaw_hold(C, m, false, 90.0, body, 1.0);
    check(w && *w == 90.0 && !m.held, "moving follows the aim");
    w = lg::yaw_hold(C, m, true, 179.0, std::optional<double>(-179.0), 1.1);
    check(w && *w == -179.0, "wrap: 2 deg across 180 stays held");
    check(lg::pa_still(std::string("idle")) && lg::pa_still(std::string("cidle")) && lg::pa_still(std::string("turnl")), "still states");
    check(!lg::pa_still(std::string("aidle")) && !lg::pa_still(std::string("walk")) && !lg::pa_still(std::nullopt), "aim idle and moves follow the aim");
    g_fl["pupyawhold"] = 0;
    lg::YawHold o;
    w = lg::yaw_hold(C, o, true, 30.0, body, 2.0);
    check(w && *w == 30.0, "off flag follows the aim");
    check(lg::yaw_hold_turn(C, o, std::string("turnl")) == std::optional<std::string>("turnl"), "off flag keeps the rate feed");
    auto all = [](const lg::PaEnt&) { return true; };
    auto noE = [](const lg::PaEnt& e) { return std::string(e.rel).find("_idle_e") == std::string::npos; };
    auto pk = lg::pa_clip(C, "idle", "bh", all);
    check(pk && std::string(pk->e->rel) == "bh/Idles/AS_tp_bh_idle", "paidlee 0 keeps the table idle");
    g_fl["paidlee"] = 1;
    pk = lg::pa_clip(C, "idle", "bh", all);
    check(pk && std::string(pk->e->rel) == "bh/Idles/AS_tp_bh_idle_e", "paidlee 1 bh stock idle");
    pk = lg::pa_clip(C, "idle", "ar", all);
    check(pk && std::string(pk->e->rel) == "ar/relax/idle/add/AS_tp_ar_relax_stand_idle_e", "paidlee 1 ar stock idle");
    pk = lg::pa_clip(C, "idle", "pt", all);
    check(pk && std::string(pk->e->rel) == "pt/relax/idle/AS_tp_pt_relax_stand_idle", "paidlee 1 pt unchanged");
    pk = lg::pa_clip(C, "idle", "ar", noE);
    check(pk && std::string(pk->e->rel) == "ar/relax/idle/AS_tp_ar_relax_stand_idle", "stock clip missing falls back to the table");
    check(lg::pa_pick(C, 150, "f", false, true, false, std::nullopt).first == "awalk", "aim walk");
    check(lg::pa_pick(C, 300, "f", false, true, false, std::nullopt) == std::make_pair(std::string("awalk"), std::string("look")), "AN1 aim while running keeps the aimed walk");
    check(lg::pa_pick(C, 600, "f", false, true, false, std::nullopt).first == "awalk", "AN1 aim at sprint speed keeps the aimed walk");
    check(lg::pa_aim(C, false, "ar", 0) && lg::pa_aim(C, false, "pt", 150), "HS1 rifle and pistol idle and walk raise the weapon by default");
    check(!lg::pa_aim(C, false, "bh", 0), "HS1 empty hands stay unarmed");
    check(!lg::pa_aim(C, false, "ar", 300) && lg::pa_aim(C, true, "ar", 300), "HS1 run keeps the run clip unless the real aim is on");
    check(lg::pa_pick(C, 0, "f", false, lg::pa_aim(C, false, "ar", 0), false, std::nullopt).first == "aidle", "HS1 rifle idle is the combat idle");
    g_fl["paaimdef"] = 0;
    check(!lg::pa_aim(C, false, "ar", 0), "HS1 paaimdef 0 old relaxed idle");
    g_fl["paaimdef"] = 1;
    check(!lg::pa_aim_mv(C, std::string("2"), "ar", 0) && !lg::pa_aim_mv(C, std::string("2"), "ar", 150), "HS2 weapon put away with H shows the relaxed idle and walk");
    check(lg::pa_aim_mv(C, std::string("0"), "ar", 0) && lg::pa_aim_mv(C, std::string("1"), "ar", 300) && !lg::pa_aim_mv(C, std::nullopt, "bh", 0), "HS2 normal aim rules unchanged");
    g_fl["pahrelax"] = 0;
    check(lg::pa_aim_mv(C, std::string("2"), "ar", 0), "HS2 pahrelax 0 old behaviour");
    g_fl["pahrelax"] = 1;
    check(lg::pa_pick(C, 300, "f", false, false, false, std::nullopt).first == "run", "no aim still runs");
    check(lg::pa_pick(C, 150, "l", false, true, false, std::nullopt).first == "left", "aim strafe left unchanged");
    pk = lg::pa_clip(C, "awalk", "pt", all);
    check(pk && std::string(pk->e->rel) == "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd" && pk->st == "awalk", "AN1 pistol aim walk gets the raised-arms strafe clip");
    pk = lg::pa_clip(C, "awalk", "ar", all);
    check(pk && std::string(pk->e->rel) == "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd", "rifle aim walk unchanged");
    const std::string strafeF = "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd";
    g_fl["pacwalk"] = 1;
    check(lg::pa_pick(C, 150, "f", true, false, false, std::nullopt) == std::make_pair(std::string("cwalk"), std::string("move")), "AN2 crouch walk fwd state");
    check(lg::pa_pick(C, 150, "l", true, false, false, std::nullopt).first == "left", "AN2 crouch walk left uses the strafe state");
    check(lg::pa_pick(C, 150, "b", true, true, false, std::nullopt).first == "back", "AN2 crouch walk back uses the strafe state");
    check(lg::pa_pick(C, 10, "f", true, false, false, std::nullopt).first == "cidle", "crouch idle unchanged");
    for (const char* fam : {"bh", "ar", "pt"})
    {
        pk = lg::pa_clip(C, "cwalk", fam, all);
        check(pk && pk->st == "cwalk" && pk->e->rel == strafeF && pk->e->has_nat, "AN2 crouch walk clip has steps");
    }
    check(lg::pa_pick(C, 0, "f", false, false, false, std::nullopt, true).first == "ladder", "AN2 ladder idle on rungs");
    check(lg::pa_pick(C, 300, "f", true, true, true, std::nullopt, true).first == "ladder", "AN2 ladder wins over crouch, aim and fall");
    pk = lg::pa_clip(C, "ladder", "ar", all);
    check(pk && pk->st == "ladder" && pk->e->rel == strafeF && lg::pa_rate(C, *pk->e, 0) == 1.0, "AN2 ladder clip");
    {
        const std::string lu = "/Game/_STALKER2/Animations/Player/AnimSequences/ar/common/ladder/ClimbUp/AS_fp_ladder_up_left";
        auto pl = lg::pa_clip(C, "ladul", "ar", all);
        check(pl && pl->e->rel == lu && lg::pa_path(lu) == lu + ".AS_fp_ladder_up_left", "LD1 ladder step clip is the stock fp ladder clip, full path kept");
        check(lg::pa_path("bh/idles/AS_tp_bh_idle") == std::string(lg::kPaDir) + "bh/idles/AS_tp_bh_idle.AS_tp_bh_idle", "LD1 relative clip path unchanged");
        auto pd = lg::pa_clip(C, "laddr", "pt", all);
        check(pd && pd->e->rel == std::string("/Game/_STALKER2/Animations/Player/AnimSequences/ar/common/ladder/ClimbDown/AS_fp_ladder_down_right"), "LD1 ladder down clip");
        check(lg::pa_lad("ladder") && lg::pa_lad("ladul") && lg::pa_lad("laddr") && !lg::pa_lad("idle") && !lg::pa_lad("left"), "LD1 ladder state names");
        check(lg::pa_ov(C, "ladul", "pt", std::optional<std::string>(""), false, false).empty(), "LD1 ladder step: no overlay");
        lg::LadSt L;
        std::string s = lg::lad_step(C, L, 1, 100, 10.0, 10.0);
        check(s == "ladul" && L.rate == 0, "LD1 first sample holds the up-left pose");
        double t = 10.0;
        double z = 100;
        int flips = 0;
        std::string prev = s;
        for (int i = 1; i <= 40; i++)
        {
            t += 0.05;
            z += 100 * 0.05;
            s = lg::lad_step(C, L, 1 + i, z, t, t);
            if (s != prev) flips++;
            prev = s;
        }
        check(std::fabs(L.vz - 100) < 2 && std::fabs(L.rate - 1.0) < 0.03, "LD1 vz 100 cm/s -> rate 1");
        check(flips >= 3 && flips <= 4 && s.compare(0, 4, "ladu") == 0, "LD1 2 s climb at rate 1 alternates left/right every 0.5 s");
        for (int i = 41; i <= 80; i++)
        {
            t += 0.05;
            z -= 150 * 0.05;
            s = lg::lad_step(C, L, 1 + i, z, t, t);
        }
        check(s.compare(0, 4, "ladd") == 0 && std::fabs(L.rate - 1.5) < 0.05, "LD1 descending picks the down clips at rate 1.5");
        std::string held;
        for (int i = 81; i <= 120; i++)
        {
            t += 0.05;
            s = lg::lad_step(C, L, 1 + i, z, t, t);
            if (i == 100) held = s;
        }
        check(L.rate == 0 && s == held, "LD1 stopped on the rungs holds the pose (rate 0, no flip)");
        lg::LadSt R;
        lg::lad_step(C, R, 1, 0, 1.0, 1.0);
        lg::lad_step(C, R, 2, 900, 1.1, 1.1);
        check(R.rate == 2.0, "LD1 rate capped at paladmaxr");
        lg::LadSt G;
        lg::lad_step(C, G, 1, 0, 1.0, 1.0);
        lg::lad_step(C, G, 2, 500, 3.5, 3.5);
        check(G.vz == 0, "LD1 a snapshot gap over 1 s is not a velocity");
    }
    lg::Mv m4 = lg::mv_parse("0,0,0,1");
    check(m4.l && *m4.l == "1" && m4.f && *m4.f == "0", "AN2 MV ladder field");
    lg::Mv m3 = lg::mv_parse("1,0,1");
    check(!m3.l && m3.f && *m3.f == "1", "old 3-field MV has no ladder");
    {
        check(lg::mv_lcode(false, -1) == 2 && lg::mv_lcode(false, 1) == 3 && lg::mv_lcode(false, 0) == 0 && lg::mv_lcode(true, 1) == 1, "LN1 lean code in the ladder field, ladder wins");
        check(lg::mv_lean(lg::mv_parse("0,1,0,2")) == -1 && lg::mv_lean(lg::mv_parse("0,1,0,3,rl4")) == 1, "LN1 lean left/right parsed");
        check(lg::mv_lean(lg::mv_parse("0,0,0,1")) == 0 && lg::mv_lean(lg::mv_parse("0,0,0,0")) == 0 && lg::mv_lean(lg::mv_parse("1,0,1")) == 0, "LN1 ladder, upright and old 3-field MV have no lean");
        lg::Mv m6 = lg::mv_parse("0,0,0,3,rl4");
        check(m6.act && *m6.act == "rl4" && !(m6.l && *m6.l == "1"), "LN1 lean keeps the action field and is not a ladder for old receivers");
        check(lg::pa_pick(C, 0, "f", false, false, false, std::nullopt, *m6.l == "1").first == "idle", "LN1 lean code does not pick the ladder state");
        lg::LRot b{0, -90, 0};
        double X[3], Y[3], Z[3];
        lg::rot_axes(lg::lean_rot(b, 15), X, Y, Z);
        check(std::fabs(Z[1] - std::sin(15 * 3.14159265358979323846 / 180)) < 1e-6 && std::fabs(Z[2] - std::cos(15 * 3.14159265358979323846 / 180)) < 1e-6 && std::fabs(Z[0]) < 1e-6, "LN1 right lean tips the mesh top to actor +Y");
        lg::rot_axes(lg::lean_rot(b, -15), X, Y, Z);
        check(Z[1] < -0.2 && std::fabs(Z[0]) < 1e-6, "LN1 left lean tips the mesh top to actor -Y");
        lg::LRot z = lg::lean_rot(b, 0);
        check(std::fabs(z.p) < 1e-6 && std::fabs(z.y + 90) < 1e-6 && std::fabs(z.r) < 1e-6, "LN1 zero lean returns the base rotation");
        lg::LRot q{3, 40, -7};
        double X0[3], Y0[3], Z0[3], X1[3], Y1[3], Z1[3];
        lg::rot_axes(q, X0, Y0, Z0);
        lg::rot_axes(lg::rot_of(X0, Y0, Z0), X1, Y1, Z1);
        check(std::fabs(X0[0] - X1[0]) + std::fabs(Y0[1] - Y1[1]) + std::fabs(Z0[2] - Z1[2]) + std::fabs(Z0[0] - Z1[0]) < 1e-9, "LN1 rotator round trip");
        check(lg::lean_step(0, 15, 0.1, 75) == 7.5 && lg::lean_step(7.5, 15, 0.2, 75) == 15 && lg::lean_step(15, 0, 0.1, 75) == 7.5 && lg::lean_step(3, 3, 0, 75) == 3, "LN1 lean eases in and out");
        std::vector<std::string> nm = {"jnt_root", "jnt_hips", "jnt_spine_01", "jnt_spine_02", "jnt_spine_03", "jnt_head", "jnt_thigh_l", "jnt_foot_l", "jnt_weapon", "jnt_ik_foot_root", "jnt_ik_foot_l", "jnt_camera"};
        std::vector<int> par = {-1, 0, 1, 2, 3, 4, 1, 6, 0, 0, 9, 0};
        double pos[12][3] = {{0, 0, 0}, {0, 0, 100}, {0, 0, 110}, {0, 0, 125}, {0, 0, 140}, {0, 0, 170}, {0, 10, 95}, {0, 10, 10}, {30, 20, 140}, {0, 0, 0}, {0, 10, 10}, {0, 0, 165}};
        auto st = lg::ln_stages(par, nm);
        auto has = [&](int s, int i) { for (int k : st[s].bones) if (k == i) return true; return false; };
        check(st.size() == 3 && st[0].pivot == 2 && st[1].pivot == 3 && st[2].pivot == 4, "LN2 three spine stages");
        check(st.size() == 3 && has(0, 2) && has(0, 5) && has(0, 8) && has(0, 11) && !has(0, 0) && !has(0, 1) && !has(0, 6) && !has(0, 7) && !has(0, 9) && !has(0, 10), "LN2 spine, head, weapon and camera bend; root, hips, legs and foot IK do not");
        check(lg::ln_stages(par, {"jnt_root", "jnt_hips"}).empty() && lg::ln_stages({-1, 0}, {"a", "b"}).empty(), "LN2 no stages without the spine bones");
        auto pose = [&](std::vector<lg::LnTf>& v) { v.assign(12, lg::LnTf{}); for (int i = 0; i < 12; i++) { v[i].q[3] = 1; for (int k = 0; k < 3; k++) v[i].t[k] = pos[i][k]; v[i].s[0] = v[i].s[1] = v[i].s[2] = 1; } };
        std::vector<lg::LnTf> bs;
        pose(bs);
        double ax0[3];
        lg::ln_axis(lg::LRot{0, 0, 0}, ax0);
        check(std::fabs(ax0[0] - 1) < 1e-9 && std::fabs(ax0[1]) < 1e-9 && std::fabs(ax0[2]) < 1e-9, "LN2 lean axis is the forward axis for an unrotated mesh");
        lg::ln_apply(bs.data(), 12, st, ax0, 15);
        bool still = true;
        for (int i : {0, 1, 6, 7, 9, 10})
            for (int k = 0; k < 3; k++) still = still && bs[i].t[k] == pos[i][k] && (k < 3 ? bs[i].q[k] == 0 : true);
        check(still, "LN2 right lean leaves root, hips, legs and foot IK untouched");
        check(bs[5].t[1] > 10 && std::fabs(bs[5].t[0]) < 1e-9 && bs[5].t[2] < 170 && bs[5].t[2] > 160, "LN2 right lean moves the head to +Y");
        check(std::fabs(bs[2].t[1]) < 1e-9 && std::fabs(bs[2].t[2] - 110) < 1e-9 && bs[3].t[1] > 0 && bs[4].t[1] > bs[3].t[1], "LN2 bend starts at spine_01 and grows up the chain");
        double hq = std::atan2(2 * (bs[5].q[3] * bs[5].q[0]), 1 - 2 * bs[5].q[0] * bs[5].q[0]) * 180 / 3.14159265358979323846;
        check(std::fabs(hq + 15) < 1e-6, "LN2 head turns the full 15 degrees over three stages");
        double dw0 = std::hypot(pos[8][1] - pos[5][1], pos[8][2] - pos[5][2]), dw1 = std::hypot(bs[8].t[1] - bs[5].t[1], bs[8].t[2] - bs[5].t[2]);
        check(std::fabs(dw0 - dw1) < 1e-9 && bs[8].t[1] > 20, "LN2 weapon follows the chest rigidly");
        std::vector<lg::LnTf> bl;
        pose(bl);
        lg::ln_apply(bl.data(), 12, st, ax0, -15);
        check(bl[5].t[1] < -10 && std::fabs(bl[5].t[1] + bs[5].t[1]) < 1e-9, "LN2 left lean mirrors the right lean");
        std::vector<lg::LnTf> b0;
        pose(b0);
        lg::ln_apply(b0.data(), 12, st, ax0, 0);
        bool same = true;
        for (int i = 0; i < 12; i++)
            for (int k = 0; k < 3; k++) same = same && std::fabs(b0[i].t[k] - pos[i][k]) < 1e-12;
        check(same, "LN2 zero lean is the identity");
        lg::LRot yb{0, -90, 0};
        double axy[3], MX[3], MY[3], MZ[3];
        lg::ln_axis(yb, axy);
        lg::rot_axes(yb, MX, MY, MZ);
        std::vector<lg::LnTf> by;
        pose(by);
        lg::ln_apply(by.data(), 12, st, axy, 15);
        double d[3] = {by[5].t[0] - by[1].t[0], by[5].t[1] - by[1].t[1], by[5].t[2] - by[1].t[2]};
        double wy = d[0] * MX[1] + d[1] * MY[1] + d[2] * MZ[1];
        check(wy > 10, "LN2 right lean tips toward actor +Y for a yawed mesh, same side as LN1");
    }
    check(lg::pa_equip(C, "bh", "ar") == std::optional<std::string>("eqar"), "AN2 draw rifle");
    check(lg::pa_equip(C, "ar", "pt") == std::optional<std::string>("eqpt"), "AN2 switch rifle to pistol");
    check(lg::pa_equip(C, "ar", "ar") == std::optional<std::string>("eqar"), "AN2 switch rifle to rifle");
    check(lg::pa_equip(C, "pt", "bh") == std::optional<std::string>("uqpt"), "AN2 holster pistol");
    check(!lg::pa_equip(C, "bh", "bh"), "bare hands to bare hands no clip");
    check(!lg::pa_equip_swap(C, "W1~/Game/Weapons/pt/SK_pm.SK_pm@@WM@GripPointTPP~", "W1~/Game/Weapons/pt/SK_pm.SK_pm@@WM@GripPointTPP~a|b"), "AN2 attachment rescan of the same gun plays no equip");
    check(lg::pa_equip_swap(C, "W1~/Game/Weapons/knifes/SK_knife.SK_knife@@WM@GripPointTPP~", "W1~/Game/Weapons/pt/SK_pm.SK_pm@@WM@GripPointTPP~") == std::optional<std::string>("eqpt"), "AN2 knife to pistol plays the pistol equip");
    pk = lg::pa_clip(C, "eqar", "ar", all);
    check(pk && std::string(pk->e->rel) == "ar/ak_74/Action/Equip/AS_tp_ar_relax_ak74_equip", "AN2 rifle equip clip");
    pk = lg::pa_clip(C, "uqpt", "bh", all);
    check(pk && std::string(pk->e->rel) == "pt/pm/Action/Equip/AS_tp_pm_unequip", "AN2 pistol unequip clip");
    pk = lg::pa_clip(C, "eqpt", "pt", all);
    check(pk && std::string(pk->e->rel) == "pt/pm/Action/Equip/AS_tp_pm_equip", "AN2 pistol equip clip");
    check(lg::ac_of(C, "MG_fp_ak74_reload") == std::optional<std::string>("rl"), "AN3 reload montage -> rl");
    check(lg::ac_of(C, "MG_fp_ak74_melee") == std::optional<std::string>("ml"), "AN3 melee montage -> ml");
    check(lg::ac_of(C, "MG_fp_medkit_common_use") == std::optional<std::string>("hi"), "AN3 medkit -> hi");
    check(lg::ac_of(C, "MG_fp_injector_wounded_heal") == std::optional<std::string>("hi"), "AN3 injector -> hi");
    check(lg::ac_of(C, "MG_fp_canned_food_use") == std::optional<std::string>("hf") && lg::ac_of(C, "MG_fp_bread_use") == std::optional<std::string>("hf") && lg::ac_of(C, "MG_fp_sausage_use") == std::optional<std::string>("hf"), "HE2 food -> hf");
    check(lg::ac_of(C, "MG_fp_energy_drink_use") == std::optional<std::string>("hd") && lg::ac_of(C, "MG_fp_vodka_use") == std::optional<std::string>("hd") && lg::ac_of(C, "MG_fp_water_use") == std::optional<std::string>("hd") && lg::ac_of(C, "MG_fp_beer_use") == std::optional<std::string>("hd") && lg::ac_of(C, "MG_fp_milk_use") == std::optional<std::string>("hd") && lg::ac_of(C, "MG_fp_energetic_use") == std::optional<std::string>("hd"), "HE2 drinks -> hd");
    check(lg::ac_of(C, "MG_fp_hercules_use") == std::optional<std::string>("hl") && lg::ac_of(C, "MG_fp_cinnamon_use") == std::optional<std::string>("hl") && lg::ac_of(C, "MG_fp_psyblocker_use") == std::optional<std::string>("hl") && lg::ac_of(C, "MG_fp_armymedkit_use") == std::optional<std::string>("hi"), "HE2 every medicine maps to a heal clip");
    check(std::string(lg::ac_flag("hd")) == "pahl" && std::string(lg::ac_flag("hf")) == "pahl", "HE2 food and drink share the heal flag");
    check(lg::ac_ent("achd", "ar") && std::string(lg::ac_ent("achd", "ar")->rel).find("tp_bh_ca_drink_vodka_action") != std::string::npos, "HE2 hd clip is the standing drink");
    check(lg::ac_of(C, "revive") == std::optional<std::string>("hi"), "AN3 revive start -> hi");
    check(lg::ac_of(C, "MG_fp_pills_common_use") == std::optional<std::string>("hl"), "AN3 pills -> hl");
    check(lg::ac_ent("achi", "ar") && std::string(lg::ac_ent("achi", "ar")->rel).find("wounded_give_heal") != std::string::npos, "AN3 hi clip is the kneel inject");
    check(lg::ac_ent("achf", "bh") && std::string(lg::ac_ent("achf", "bh")->rel).find("stand/tp_bh_ca_bread/") != std::string::npos, "HE2 hf clip is the standing eat clip");
    check(lg::ac_of(C, "MG_fp_bandage_use") == std::optional<std::string>("hl"), "AN3 bandage -> hl");
    check(lg::ac_of(C, "MG_fp_bh_lever_big_hor_down_0") == std::optional<std::string>("ia"), "AN3 lever -> ia");
    check(lg::ac_of(C, "MG_fp_bh_hatch_valve_open") == std::optional<std::string>("ia"), "AN3 hatch -> ia");
    check(lg::ac_of(C, "MG_fp_bh_creaturestash_looting_common") == std::optional<std::string>("lt"), "AN3 stash -> lt");
    check(lg::ac_of(C, "MG_fp_dead_body_pickup") == std::optional<std::string>("lt"), "AN3 body loot -> lt");
    check(lg::ac_of(C, "MG_FP_Player_AR_ArtifactPickup_01") == std::optional<std::string>("pk"), "AN3 pickup -> pk");
    check(!lg::ac_of(C, "MG_fp_ak74_equip"), "AN3 equip montage is not an action");
    check(!lg::ac_of(C, "MG_FP_Player_BL01-Arev_SwitchOn_01"), "AN3 attachment switch is not an action");
    check(lg::ac_of(C, "MG_fp_bolt_throw_strong") == std::optional<std::string>("th"), "TH1 bolt throw montage -> th");
    check(lg::ac_of(C, "MG_fp_rgd5_grenade_throw") == std::optional<std::string>("th"), "TH1 grenade montage -> th");
    check(lg::ac_of(C, "MG_fp_ak74_gp25_reload") == std::optional<std::string>("rl"), "TH1 launcher reload stays rl");
    {
        auto pk2 = lg::pa_clip(C, "acth", "pt", all);
        check(pk2 && std::string(pk2->e->rel) == "pt/combat/grenade/AS_tp_pt_combat_grenade", "TH1 pistol throw clip");
        pk2 = lg::pa_clip(C, "acth", "bh", all);
        check(pk2 && std::string(pk2->e->rel) == "ar/combat/grenade/AS_tp_ar_combat_grenade", "TH1 bare hands throw clip");
        lg::AcTx T;
        check(lg::ac_force(C, T, "th", 5, 1.5) && lg::ac_token(C, T, 5) == "th1", "TH1 thrown object forces th1");
        check(!lg::ac_force(C, T, "th", 5.5, 1.5), "TH1 montage + object within 1.5 s is one action");
        check(lg::ac_force(C, T, "th", 7, 1.5) && lg::ac_token(C, T, 7) == "th2", "TH1 next throw th2");
        lg::AcTx T2;
        check(lg::ac_feed(C, T2, "1:MG_fp_bolt_throwing", "MG_fp_bolt_throwing", 20) && !lg::ac_feed(C, T2, "2:MG_bolt_fp_throwing", "MG_bolt_fp_throwing", 21.5) && lg::ac_token(C, T2, 21.5) == "th1", "TH1 arm and bolt montages of one throw are one action");
        check(lg::ac_feed(C, T2, "3:MG_fp_bolt_throwing", "MG_fp_bolt_throwing", 24) && lg::ac_token(C, T2, 24) == "th2", "TH1 next throw after the gap");
        check(lg::pa_fam("W1~/Game/_STALKER2/SkeletalMeshes/weapons/bolt/SM_bolt.SM_bolt@@x@y~") == "bh" && lg::pa_throwable(5) && lg::pa_throwable(6) && !lg::pa_throwable(2) && !lg::pa_throwable(-1), "TH2 grenade or bolt in hand is a bare hands hold");
        lg::AcTx T3;
        check(lg::ac_feed(C, T3, "1:MG_fp_rgd5_throw_without_equip", "MG_fp_rgd5_throw_without_equip", 30) && lg::ac_feed(C, T3, "2:MG_fp_rgd5_throw_without_equip", "MG_fp_rgd5_throw_without_equip", 31.6) && lg::ac_token(C, T3, 31.6) == "th2", "TH2 two grenades 1.6 s apart are two actions");
        check(!lg::ac_feed(C, T3, "3:MG_fp_rgd5_throw_without_equip", "MG_fp_rgd5_throw_without_equip", 32.0), "TH2 same montage again within thgap is one action");
        check(!lg::ac_force(C, T3, "th", 32.5, 2.5) && T3.seq == 2, "TH2 grenade object after its montage is the same action");
        lg::AcTx T4;
        check(lg::ac_feed(C, T4, "1:MG_fp_bolt_throwing", "MG_fp_bolt_throwing", 40) && !lg::ac_feed(C, T4, "2:MG_bolt_fp_throwing", "MG_bolt_fp_throwing", 41.4) && lg::ac_feed(C, T4, "3:MG_fp_bolt_throwing", "MG_fp_bolt_throwing", 42.6) && T4.seq == 2, "TH2 a second bolt right after the release is a new action");
        lg::AcTx T5;
        check(lg::ac_feed(C, T5, "1:MG_fp_rgd5_light_throw", "MG_fp_rgd5_light_throw", 50) && lg::ac_th_at(C, T5, 53.4) == 53.4, "TH4 held throw stays hot while its object is not out");
        check(!lg::ac_force(C, T5, "th", 53.5, 2.5) && T5.seq == 1 && lg::ac_th_at(C, T5, 54) == 53.5, "TH4 grenade released 3.5 s after the press is the same action");
        check(!lg::ac_force(C, T5, "th", 54, 2.5) && lg::ac_feed(C, T5, "2:MG_fp_rgd5_light_throw", "MG_fp_rgd5_light_throw", 58) && T5.seq == 2, "TH4 next throw after a held one is a new action");
        lg::AcTx T6;
        check(lg::ac_feed(C, T6, "1:MG_fp_bolt_throwing", "MG_fp_bolt_throwing", 60) && !lg::ac_feed(C, T6, "2:MG_bolt_fp_throwing", "MG_bolt_fp_throwing", 64) && !lg::ac_force(C, T6, "th", 64.2, 2.5) && T6.seq == 1, "TH4 bolt held 4 s: arm, release and object are one action");
        lg::AcTx T7;
        check(lg::ac_feed(C, T7, "1:MG_fp_rgd5_light_throw", "MG_fp_rgd5_light_throw", 80) && lg::ac_force(C, T7, "th", 89, 2.5) && T7.seq == 2, "TH4 an object long after a cancelled throw is its own action");
        g_fl["thopen"] = 0;
        lg::AcTx T8;
        check(lg::ac_feed(C, T8, "1:MG_fp_rgd5_light_throw", "MG_fp_rgd5_light_throw", 90) && lg::ac_force(C, T8, "th", 93.5, 2.5) && T8.seq == 2 && lg::ac_th_at(C, T8, 94) == 93.5, "TH4 thopen=0 keeps the old time gap");
        g_fl.erase("thopen");
        g_fl["pathr"] = 0;
        check(!lg::ac_force(C, T, "th", 9, 1.5) && !lg::ac_of(C, "MG_fp_bolt_throw"), "TH1 pathr=0 sends no throw action");
        g_fl.erase("pathr");
    }
    {
        namespace tl = s2m_throw::lg;
        tl::Ent e;
        e.seq = 7, e.k = 'g', e.st = 'f', e.x = 100, e.y = -200, e.z = 300, e.vx = 500, e.vy = 0, e.vz = 400;
        e.path = "/Game/Weapons/Grenades/RGD_StaticMesh.RGD_StaticMesh";
        auto v = tl::parse(tl::tag_str({e, e}));
        check(v.size() == 2 && v[0].seq == 7 && v[0].k == 'g' && v[0].st == 'f' && v[0].vz == 400 && v[0].path == e.path, "TH1 entry round trip");
        check(tl::parse("1,q,f,0,0,0,0,0,0,").empty() && tl::parse("1,b,f,0,0,0,0,0,0,bad path").empty() && tl::parse("0,b,f,0,0,0,0,0,0,").empty(), "TH1 bad entries dropped");
        check(tl::parse("1,b,r,1,2,3,0,0,0,").size() == 1, "TH1 empty path accepted");
        double x, y, z;
        tl::fly(e, 0.1, 0.3, x, y, z);
        check(std::fabs(x - 150) < 1e-6 && std::fabs(z - (300 + 40 - 4.9)) < 1e-6, "TH1 ballistic step");
        tl::fly(e, 5, 0.3, x, y, z);
        check(std::fabs(x - 250) < 1e-6, "TH1 extrapolation capped");
        e.st = 'x';
        tl::fly(e, 0.2, 0.3, x, y, z);
        check(x == 100 && z == 300, "TH1 ended object stays put");
        {
            tl::Ent q;
            q.seq = 1, q.k = 'g', q.st = 'f';
            bool z0 = tl::tx_ready(q, true), off = tl::tx_ready(q, false);
            q.vy = 1197;
            bool v1 = tl::tx_ready(q, true);
            q.vy = 0, q.st = 'x';
            check(!z0 && off && v1 && tl::tx_ready(q, true), "TH4 a flying entry waits for a real velocity, ends always go out, thrvel=0 sends at once");
        }
        check(tl::mine(0, 0, 0, 100, 100, 150, 400) && !tl::mine(0, 0, 0, 500, 0, 0, 400), "TH1 near own pawn");
        check(tl::asset_path("StaticMesh /Game/A/SM_bolt.SM_bolt") == "/Game/A/SM_bolt.SM_bolt" && tl::asset_path("None").empty(), "TH1 asset path");
        check(tl::own_ok(1, 0, -100, 3) && !tl::own_ok(0, 0, -0.5, 3) && tl::own_ok(-1, 10, 8, 3) && !tl::own_ok(-1, 10, 2, 3), "TH2 owner filter: own yes, other no, unknown only right after a local throw");
        auto ent = [](int s, char k, char st, double x) { tl::Ent a; a.seq = s, a.k = k, a.st = st, a.x = x; a.path = "/Game/A/SM_g.SM_g"; return a; };
        tl::RxView V;
        auto o = tl::rx_step(V, {ent(1, 'g', 'f', 0)}, 0, 6, 3);
        check(o.spawn.size() == 1 && o.spawn[0] == 1, "TH2 first flying entry spawns one copy");
        o = tl::rx_step(V, {ent(1, 'g', 'f', 0)}, 0.05, 6, 3);
        check(o.spawn.empty() && V.cm.size() == 1, "TH2 same seq again spawns no second copy");
        o = tl::rx_step(V, {ent(1, 'g', 'f', 50)}, 0.1, 6, 3);
        check(o.spawn.empty() && o.boom.empty(), "TH2 movement only updates");
        o = tl::rx_step(V, {ent(1, 'g', 'x', 60)}, 0.2, 6, 3);
        check(o.boom.size() == 1 && V.cm[1].e.x == 60, "TH2 explode at the last position");
        o = tl::rx_step(V, {ent(1, 'g', 'x', 60)}, 0.3, 6, 3);
        auto o2 = tl::rx_step(V, {ent(1, 'g', 'x', 61)}, 0.4, 6, 3);
        check(o.boom.empty() && o2.boom.empty(), "TH2 explode once even if 'x' repeats or moves");
        o = tl::rx_step(V, {ent(1, 'g', 'x', 61)}, 6.5, 6, 3);
        check(o.despawn.size() == 1 && V.cm.empty() && V.done.count(1), "TH2 despawn after thrkeep");
        o = tl::rx_step(V, {ent(1, 'g', 'f', 0)}, 6.6, 6, 3);
        check(o.spawn.empty() && o.boom.empty(), "TH2 a done seq never respawns");
        o = tl::rx_step(V, {ent(2, 'g', 'x', 5)}, 6.7, 6, 3);
        check(o.spawn.empty() && o.boom.empty() && V.done.count(2), "TH2 an 'x' without a seen flight plays no FX");
        o = tl::rx_step(V, {ent(3, 'b', 'f', 0)}, 7, 6, 3);
        o = tl::rx_step(V, {ent(3, 'b', 'r', 9)}, 8, 6, 3);
        check(o.land.size() == 1 && o.boom.empty(), "TH2 bolt lands, no FX");
        o = tl::rx_step(V, {}, 20.5, 6, 3);
        check(o.despawn.size() == 1 && V.cm.empty(), "TH2 landed bolt despawned after thrkeep");
        o = tl::rx_step(V, {}, 24, 6, 3);
        check(V.done.empty() && V.maxSeq == 0, "TH2 empty TH part for threset clears done (rejoin)");
        o = tl::rx_step(V, {ent(1, 'g', 'f', 0)}, 24.1, 6, 3);
        check(o.spawn.size() == 1, "TH2 seq 1 after the reset spawns again");
        tl::RxView W;
        tl::rx_step(W, {ent(40, 'b', 'r', 0)}, 0, 6, 3);
        o = tl::rx_step(W, {ent(2, 'b', 'f', 0)}, 0.1, 6, 3);
        check(o.spawn.size() == 1 && W.maxSeq == 2, "TH2 seq going backwards resets done");
    }
    {
        lg::AcTx T;
        check(lg::ac_token(C, T, 0).empty(), "AN3 no token before any action");
        check(lg::ac_feed(C, T, "1:MG_fp_ak74_reload", "MG_fp_ak74_reload", 10), "AN3 reload start emits");
        check(!lg::ac_feed(C, T, "1:MG_fp_ak74_reload", "MG_fp_ak74_reload", 10.1), "AN3 same montage still playing does not re-emit");
        check(lg::ac_token(C, T, 10.5) == "rl1", "AN3 token rl1");
        check(lg::ac_token(C, T, 12.5).empty(), "AN3 token dropped after pacwin");
        lg::ac_feed(C, T, "", "", 13);
        check(lg::ac_feed(C, T, "1:MG_fp_ak74_reload", "MG_fp_ak74_reload", 14) && lg::ac_token(C, T, 14) == "rl2", "AN3 second reload gets a new seq");
        std::string mv = "0,0,0,0," + lg::ac_token(C, T, 14);
        lg::Mv m5 = lg::mv_parse(mv);
        check(m5.act && *m5.act == "rl2" && m5.l && *m5.l == "0" && m5.c && *m5.c == "0", "AN3 new peer parses the action field");
        check(lg::mv_parse("1,1,0,1,zz9x").act == std::nullopt, "AN3 bad token ignored");
        check(!lg::mv_parse("0,0,0,1").act && !lg::mv_parse("1,0,1").act && !lg::mv_parse("1,0").act, "AN3 old 2/3/4-field MV from an old peer has no action");
        lg::AcRx R;
        int plays = 0;
        for (int i = 0; i < 5; i++)
            if (lg::ac_rx(C, R, m5.act)) plays++;
        check(plays == 1, "AN3 one play per event over repeated snapshots");
        check(!lg::ac_rx(C, R, std::nullopt), "AN3 field gone, nothing plays");
        check(!lg::ac_rx(C, R, std::string("rl2")), "AN3 duplicate seq after a gap does not replay");
        check(lg::ac_rx(C, R, std::string("ia3")) == std::optional<std::string>("ia"), "AN3 next seq plays");
    }
    {
        check(std::string(lg::hit_side(0)) == "f" && std::string(lg::hit_side(44)) == "f" && std::string(lg::hit_side(-170)) == "b" && std::string(lg::hit_side(180)) == "b", "HIT1 front and back sectors");
        check(std::string(lg::hit_side(90)) == "r" && std::string(lg::hit_side(-90)) == "l" && std::string(lg::hit_side(-60)) == "l", "HIT1 left and right sectors");
        check(std::string(lg::hit_enum(1)) == "f" && std::string(lg::hit_enum(2)) == "b" && std::string(lg::hit_enum(3)) == "l" && std::string(lg::hit_enum(4)) == "r", "HIT1 EAnimDirections main values");
        check(std::string(lg::hit_enum(6)) == "f" && std::string(lg::hit_enum(7)) == "b" && !lg::hit_enum(0) && !lg::hit_enum(9), "HIT1 diagonals and None");
        lg::HitSt H;
        lg::AcTx T;
        check(!lg::hit_feed(C, H, 100, 10), "HIT1 first sample never a hit");
        check(!lg::hit_feed(C, H, 99, 10.1), "HIT1 drop under pahitmin is no hit");
        check(lg::hit_feed(C, H, 80, 10.2), "HIT1 real drop is a hit");
        check(!lg::hit_feed(C, H, 60, 11), "HIT1 second hit inside the cooldown is dropped");
        check(lg::hit_feed(C, H, 40, 12.3), "HIT1 hit after the cooldown plays again");
        check(!lg::hit_feed(C, H, 50, 15), "HIT1 heal is no hit");
        check(lg::ac_hit(C, T, "b", 12.3) && lg::ac_token(C, T, 12.4) == "gb1", "HIT1 token gb1");
        check(!lg::ac_hit(C, T, nullptr, 13) && lg::ac_token(C, T, 13) == "gb1", "HIT1 no side, no new token");
        check(lg::ac_hit(C, T, "l", 14) && lg::ac_token(C, T, 14) == "gl2", "HIT1 next hit new seq");
        lg::AcRx R;
        check(lg::ac_rx(C, R, std::string("gl2")) == std::optional<std::string>("gl") && !lg::ac_rx(C, R, std::string("gl2")), "HIT1 receiver plays once per seq");
        check(lg::mv_parse("0,0,0,0,gr7").act == std::optional<std::string>("gr7"), "HIT1 hit token parses in the 5th MV field");
        auto pk = lg::pa_clip(C, "acgb", "ar", all);
        check(pk && std::string(pk->e->rel) == "ar/combat/critical_hit/AS_tp_ar_combat_hit_critical_head_back", "HIT1 rifle back hit clip");
        pk = lg::pa_clip(C, "acgl", "pt", all);
        check(pk && std::string(pk->e->rel) == "pt/combat/critical_hit/AS_tp_pt_combat_hit_critical_head_left", "HIT1 pistol left hit clip");
        pk = lg::pa_clip(C, "acgr", "bh", all);
        check(pk && std::string(pk->e->rel) == "pt/combat/critical_hit/AS_tp_pt_combat_hit_critical_head_right", "HIT1 bare hands right hit clip");
        check(lg::pa_ac_live(C, "acgf", "bh", "idle") && !lg::pa_ac_live(C, "acgf", "ar", "fall"), "HIT1 hit live, fall consumes it");
        check(std::string(lg::hit_partner(0, 90, 90)) == "f" && std::string(lg::hit_partner(0, 90, -90)) == "b" && std::string(lg::hit_partner(90, 0, 0)) == "r", "HIT1 target-relative angle turned into partner-relative side");
        check(std::string(lg::hit_partner(180, 30, -60)) == "l" && std::string(lg::hit_enum(5)) == "f" && *lg::hit_enum_deg(8) == 135, "HIT1 partner side with turned target, diagonals");
        double ga = -100;
        check(lg::hit_gate(C, ga, 5, 1) && !lg::hit_gate(C, ga, 5, 2) && !lg::hit_gate(C, ga, 1, 4) && lg::hit_gate(C, ga, 3, 4), "HIT1 target hit gate: cooldown and minimum damage");
        double gb = -100;
        check(lg::hit_gate(C, gb, 5, 1, 0.4) && lg::hit_gate(C, gb, 5, 1.5, 0.4) && !lg::hit_gate(C, gb, 5, 1.7, 0.4), "HIT1 light hit gate uses its own short cooldown");
        check(!lg::hit_big(C, 10) && lg::hit_big(C, 30), "HIT1 light under pahitbig, critical from pahitbig");
        g_fl["pahitlt"] = 0;
        check(lg::hit_big(C, 3), "HIT1 pahitlt 0 = every hit uses the critical clips");
        g_fl.erase("pahitlt");
        std::optional<std::string> lt = std::string("gb4");
        check(lg::hit_local(C, lt) == std::optional<std::string>("gb") && !lt, "HIT1 host-local hit token consumed once");
        lt = std::string("rl4");
        check(!lg::hit_local(C, lt) && !lt, "HIT1 host-local slot takes only hit tokens");
        g_fl["pahit"] = 0;
        check(!lg::ac_hit(C, T, "f", 20) && !lg::ac_rx(C, R, std::string("gf3")) && !lg::pa_clip(C, "acgf", "ar", all), "HIT1 pahit 0 = old behaviour");
        lg::HitSt H0;
        lg::hit_feed(C, H0, 100, 30);
        check(!lg::hit_feed(C, H0, 50, 31), "HIT1 pahit 0 no hit detected");
        g_fl.erase("pahit");
    }
    {
        auto pk2 = lg::pa_clip(C, "acrl", "ar", all);
        check(pk2 && std::string(pk2->e->rel) == "ar/ak_74/Action/Reload/AS_tp_ar_combat_ak74_reload", "AN3 rifle reload clip");
        pk2 = lg::pa_clip(C, "acrl", "pt", all);
        check(pk2 && std::string(pk2->e->rel) == "pt/pm/Action/Reload/AS_tp_pt_combat_pm_reload", "AN3 pistol reload clip");
        check(!lg::pa_clip(C, "acrl", "bh", all) && !lg::ac_ent("acrl", "bh"), "AN3 no reload clip with bare hands");
        pk2 = lg::pa_clip(C, "acml", "bh", all);
        check(pk2 && std::string(pk2->e->rel) == "ar/Melee/Sequence/AS_tp_pt_melee_attack_01", "AN3 bare-hand melee clip");
        check(lg::pa_clip(C, "acia", "ar", all) && lg::pa_clip(C, "aclt", "pt", all) && lg::pa_clip(C, "achl", "bh", all) && lg::pa_clip(C, "achi", "bh", all) && lg::pa_clip(C, "achf", "bh", all) && lg::pa_clip(C, "achd", "pt", all) && lg::pa_clip(C, "acpk", "bh", all), "AN3 interact, loot, heal, pickup clips");
        std::optional<std::string> eq = std::string("eqar"), ac = std::string("acrl");
        check(lg::pa_use("idle", eq, 5, ac, 5, 1) == "acrl", "AN3 action wins over equip");
        check(lg::pa_use("idle", eq, 5, ac, 5, 6) == "idle", "AN3 action ends, locomotion back");
        check(lg::pa_use("idle", eq, 5, ac, 2, 3) == "eqar", "AN3 equip still shows when action ended first");
        check(lg::pa_use("fall", eq, 5, ac, 5, 1) == "eqar", "AN3 fall beats the action, equip keeps its old priority");
        check(lg::pa_use("ladder", std::nullopt, 0, ac, 5, 1) == "ladder", "AN3 ladder beats the action");
        check(lg::pa_use("walk", std::nullopt, 0, std::nullopt, 0, 1) == "walk", "AN3 no overlay");
        check(lg::pa_ac_live(C, "acrl", "ar", "idle"), "AN3 action live on its family");
        check(!lg::pa_ac_live(C, "acrl", "bh", "idle"), "AN3 fam change to bare hands mid-reload drops the action");
        check(lg::pa_clip(C, "idle", "bh", all).has_value(), "AN3 normal clip resumes after the drop, no clipBad");
        check(!lg::pa_ac_live(C, "acrl", "ar", "fall") && !lg::pa_ac_live(C, "acrl", "ar", "ladder"), "AN3 fall or ladder consumes the action");
        {
            std::optional<std::string> st = std::string("acrl");
            double until = 5;
            int bad = 0;
            const char* fams[] = {"ar", "ar", "bh", "bh", "bh", "bh", "bh", "bh"};
            for (int i = 0; i < 8; i++)
            {
                if (st && !lg::pa_ac_live(C, *st, fams[i], "idle")) { st.reset(); until = 0; }
                std::string u = lg::pa_use("idle", std::nullopt, 0, st, until, 1);
                if (!lg::pa_clip(C, u, fams[i], all)) bad++;
            }
            check(bad == 0, "AN3 reload then bare hands never fails the clip pick");
        }
        {
            std::optional<std::string> st = std::string("acrl");
            double until = 5;
            const char* base[] = {"idle", "fall", "fall", "idle", "idle"};
            int replays = 0;
            for (int i = 0; i < 5; i++)
            {
                if (st && !lg::pa_ac_live(C, *st, "ar", base[i])) { st.reset(); until = 0; }
                if (lg::pa_use(base[i], std::nullopt, 0, st, until, 1) == "acrl" && i > 0) replays++;
            }
            check(replays == 0, "AN3 reload, fall, land does not replay");
        }
        check(lg::ix_id(C, "BP_InteractableDoor_C_2147401234") == std::optional<std::string>("ia"), "IA1 door tap -> ia");
        check(lg::ix_id(C, "BP_StaticItemContainer_C_2147451715") == std::optional<std::string>("pk"), "IA1 ground item tap -> pk");
        check(lg::ix_id(C, "BP_L_ind_lamp_02_C_2147399999") == std::optional<std::string>("ia"), "IA1 lamp switch tap -> ia");
        check(lg::ix_id(C, "BP_StashBackpack_C_1") == std::optional<std::string>("lt"), "IA1 stash tap -> lt");
        check(lg::ix_id(C, "BP_Vodka_Drink_Prop_C_1") == std::optional<std::string>("ia"), "IA1 non-world key in an actor name falls back to interact");
        {
            lg::AcTx T;
            check(lg::ac_ix(C, T, "BP_InteractableDoor_C_1", 20) && lg::ac_token(C, T, 20.1) == "ia1", "IA1 door tap emits ia1");
            check(!lg::ac_ix(C, T, "BP_InteractableDoor_C_1", 20.2), "IA1 one event per tap burst");
            check(!lg::ac_feed(C, T, "7:MG_fp_bh_door_open", "MG_fp_bh_door_open", 20.3) && lg::ac_token(C, T, 20.4) == "ia1", "IA1 door montage of the same interaction does not add a second event");
            check(lg::ac_feed(C, T, "8:MG_fp_ak74_reload", "MG_fp_ak74_reload", 20.5) && lg::ac_token(C, T, 20.6) == "rl2", "IA1 reload right after a tap still emits");
            check(lg::ac_ix(C, T, "BP_StaticItemContainer_C_9", 23) && lg::ac_token(C, T, 23) == "pk3", "IA1 next tap gets a new seq");
            lg::ac_feed(C, T, "", "", 26);
            check(lg::ac_feed(C, T, "9:MG_fp_bh_lever_big_hor_down_0", "MG_fp_bh_lever_big_hor_down_0", 26) && lg::ac_token(C, T, 26) == "ia4", "IA1 montage after the window emits again");
            check(!lg::ac_ix(C, T, "", 30), "IA1 tap with no actor sends nothing");
            lg::AcRx R;
            check(lg::ac_rx(C, R, std::string("pk3")) == std::optional<std::string>("pk"), "IA1 receiver plays pk");
        }
        {
            lg::AcTx T;
            const std::string box = "BP_PlayerStash_C_UAID_D8BBC11C280DF4DD01_1510016301";
            check(lg::ac_ix(C, T, box, 100) && T.id && *T.id == "lh" && T.lhOn && lg::ac_token(C, T, 100) == "lh1", "ST6 stash tap starts the lh hold");
            check(!lg::lh_tick(C, T, false, true, 0, 0, 100, 100.5) && lg::ac_token(C, T, 100.5) == "lh1", "ST6 hold waits for the window to open");
            check(!lg::lh_tick(C, T, true, true, 5, 0, 100, 101) && T.lhSeen, "ST6 window open seen");
            check(!lg::lh_tick(C, T, true, true, 5, 0, 100, 110) && lg::ac_token(C, T, 110) == "lh1", "ST6 token stays past pacwin while the window is open");
            check(!lg::ac_ix(C, T, box, 111), "ST6 tap on the same open stash does not restart the hold");
            const char* w = lg::lh_tick(C, T, false, true, 5, 0, 100, 112);
            check(w && std::string(w) == "janela fechada" && !T.lhOn && lg::ac_token(C, T, 112).empty(), "ST6 window closed ends the hold and drops the token at once");
            check(!lg::ac_ix(C, T, box, 112.6), "ST6 close tap right after the hold does not start a new one");
            check(lg::ac_ix(C, T, box, 120) && lg::ac_token(C, T, 120) == "lh2", "ST6 next open gets a new seq");
            lg::lh_tick(C, T, true, true, 0, 0, 100, 120.1);
            w = lg::lh_tick(C, T, true, true, 300, 0, 100, 121);
            check(w && std::string(w) == "andou para longe" && lg::ac_token(C, T, 121).empty(), "ST6 walking away ends the hold");
            check(lg::ac_ix(C, T, box, 130) && lg::ac_token(C, T, 130) == "lh3", "ST6 third open");
            lg::lh_tick(C, T, true, true, 0, 0, 100, 130.1);
            w = lg::lh_tick(C, T, true, true, 0, 0, 90, 131);
            check(w && std::string(w) == "levou dano" && lg::ac_token(C, T, 131).empty(), "ST6 damage ends the hold");
            check(lg::ac_ix(C, T, box, 140) && lg::ac_token(C, T, 140) == "lh4", "ST6 fourth open");
            check(!lg::lh_tick(C, T, false, true, 0, 0, 100, 142.9), "ST6 hold F takes ~2.8 s before the window opens");
            w = lg::lh_tick(C, T, false, true, 0, 0, 100, 144.1);
            check(w && std::string(w) == "janela nao abriu", "ST6 window that never opens ends the hold");
            check(lg::ac_ix(C, T, box, 150) && lg::lh_tick(C, T, true, true, 0, 0, -1, 150.1) == nullptr, "ST6 fifth open");
            w = lg::lh_tick(C, T, true, true, 0, 0, -1, 450.2);
            check(w && std::string(w) == "limite de tempo", "ST6 safety cap ends the hold");
            check(lg::ac_ix(C, T, box, 460) && lg::ac_feed(C, T, "5:MG_fp_ak74_reload", "MG_fp_ak74_reload", 463), "ST6 sixth open then a reload");
            w = lg::lh_tick(C, T, true, true, 0, 0, 100, 463.1);
            check(w && std::string(w) == "outra acao" && lg::ac_token(C, T, 463.1) == "rl7", "ST6 another action ends the hold and is sent");
            lg::AcTx B;
            check(lg::ac_ix(C, B, lg::ix_body(C, "BP_AI_Human_C_7", true, false), 10) && lg::ac_token(C, B, 10) == "lt1" && !B.lhOn && lg::ac_token(C, B, 12.5).empty(), "ST6 corpse loot stays the lt one-shot");
            g_fl["palthold"] = 0;
            lg::AcTx O;
            check(lg::ac_ix(C, O, box, 10) && lg::ac_token(C, O, 10) == "lt1" && !O.lhOn && lg::ac_token(C, O, 12.5).empty(), "ST6 palthold 0 keeps the old lt one-shot for the stash");
            g_fl.erase("palthold");
            lg::AcTx P;
            check(lg::ac_ix(C, P, "BP_StaticItemContainer_C_9", 10) && lg::ac_token(C, P, 10) == "pk1" && !P.lhOn, "ST6 ground item stays pk");
        }
        {
            lg::AcRx R;
            check(lg::ac_rx(C, R, std::string("lh4")) == std::optional<std::string>("lh") && !lg::ac_rx(C, R, std::string("lh4")), "ST6 receiver starts lh once per token");
            auto all = [](const lg::PaEnt&) { return true; };
            check(lg::pa_clip(C, "acli", "ar", all) && lg::pa_clip(C, "aclh", "pt", all) && lg::pa_clip(C, "aclo", "bh", all), "ST6 in, loop and out clips for every family");
            check(std::string(lg::ac_ent("acli", "ar")->rel).find("AS_tp_bh_ca_search_in") != std::string::npos && std::string(lg::ac_ent("aclh", "ar")->rel).find("AS_tp_bh_ca_search_action_a") != std::string::npos && std::string(lg::ac_ent("aclo", "ar")->rel).find("AS_tp_bh_ca_search_out") != std::string::npos, "ST6 stock MG_tp_bh_ca_search clips");
            check(lg::ac_loop("aclh") && !lg::ac_loop("acli") && !lg::ac_loop("aclo") && !lg::ac_loop("aclt"), "ST6 only the search action loops");
            check(lg::pa_ac_live(C, "aclh", "ar", "idle") && !lg::pa_ac_live(C, "aclh", "ar", "ladder") && !lg::pa_ac_live(C, "acli", "ar", "fall"), "ST6 hold not live on ladder or fall");
            check(std::fabs(lg::lh_len(C, 2.4) - 2.15) < 1e-9 && std::fabs(lg::lh_len(C, 0.5) - 0.5) < 1e-9, "ST6 in length minus the blend out like other actions");
            std::optional<std::string> tok = std::string("lh4");
            lg::LhRx H;
            lg::lh_go(H, "lh4", 10, 2.15);
            check(H.ph == 1 && std::string(lg::lh_st(H.ph)) == "acli", "ST6 hold starts with the in clip");
            check(!lg::lh_rx(C, H, tok, 0.1, 11, 2.42) && H.ph == 1, "ST6 in plays to its end");
            const char* w = lg::lh_rx(C, H, tok, 0.1, 12.2, 2.42);
            check(w && H.ph == 2 && std::string(lg::lh_st(H.ph)) == "aclh", "ST6 in -> search loop");
            check(!lg::lh_rx(C, H, tok, 0.1, 60, 2.42) && H.ph == 2, "ST6 loop holds while the token stays (50 s)");
            w = lg::lh_rx(C, H, std::nullopt, 0.1, 61, 2.42);
            check(w && std::string(w) == "janela fechada" && H.ph == 3 && std::string(lg::lh_st(H.ph)) == "aclo" && std::fabs(H.until - 63.42) < 1e-9, "ST6 token gone -> out clip");
            check(!lg::lh_rx(C, H, tok, 0.1, 62, 2.42) && H.ph == 3, "ST6 out is not undone by a late token");
            w = lg::lh_rx(C, H, std::nullopt, 0.1, 63.5, 2.42);
            check(w && H.ph == 0 && !lg::lh_st(H.ph), "ST6 out ends back to locomotion");
            lg::lh_go(H, "lh5", 100, 2.15);
            w = lg::lh_rx(C, H, std::string("lh5"), 0.1, 101, 2.42);
            check(!w && H.ph == 1, "ST6 second hold in");
            w = lg::lh_rx(C, H, std::nullopt, 0.1, 101.2, 2.42);
            check(w && H.ph == 3, "ST6 close during the in goes straight to out");
            lg::lh_go(H, "lh6", 200, 2.15);
            lg::lh_rx(C, H, std::string("lh6"), 0.1, 203, 2.42);
            w = lg::lh_rx(C, H, std::string("lh6"), 2.5, 210, 2.42);
            check(w && std::string(w) == "parceiro sem dados" && H.ph == 3, "ST6 peer silent -> out");
            lg::lh_go(H, "lh8", 400, 2.15);
            lg::lh_rx(C, H, std::string("lh8"), 0.1, 403, 2.42);
            w = lg::lh_rx(C, H, std::string("lh8"), 0.1, 700.1, 2.42);
            check(w && std::string(w) == "limite de tempo" && H.ph == 3, "ST6 receiver safety cap -> out");
            check(lg::ac_on(C, "lh") && lg::ac_on(C, "li") && lg::ac_on(C, "lo"), "ST6 hold ids under paia");
            g_fl["paia"] = 0;
            check(!lg::ac_on(C, "lh") && lg::ac_token(C, lg::AcTx{}, 0).empty(), "ST6 paia 0 turns the hold off");
            g_fl.erase("paia");
        }
        check(lg::ix_id(C, lg::ix_body(C, "BP_AI_Human_C_2147466096", true, false)) == std::optional<std::string>("lt"), "LT1 dead human corpse tap -> lt");
        check(lg::ix_id(C, lg::ix_body(C, "BP_Flesh_C_2147400001", true, false)) == std::optional<std::string>("lt"), "LT1 dead mutant corpse tap -> lt");
        check(lg::ix_body(C, "BP_AI_Human_C_1", true, true) == "BP_AI_Human_C_1" && lg::ix_id(C, "BP_AI_Human_C_1") == std::optional<std::string>("ia"), "LT1 living NPC tap stays ia");
        check(lg::ix_body(C, "BP_InteractableDoor_C_1", false, false) == "BP_InteractableDoor_C_1", "LT1 non-Obj actor name unchanged");
        check(lg::ix_body(C, "", true, false).empty(), "LT1 no actor stays empty");
        {
            lg::AcTx T;
            check(lg::ac_ix(C, T, lg::ix_body(C, "BP_AI_Human_C_7", true, false), 50) && lg::ac_token(C, T, 50.1) == "lt1", "LT1 corpse tap emits lt1");
            check(!lg::ac_feed(C, T, "3:MG_fp_dead_body_pickup", "MG_fp_dead_body_pickup", 50.2) && lg::ac_token(C, T, 50.3) == "lt1", "LT1 body montage right after the tap adds no second event");
            lg::AcRx R;
            check(lg::ac_rx(C, R, std::string("lt1")) == std::optional<std::string>("lt") && lg::ac_ent("aclt", "ar") && std::string(lg::ac_ent("aclt", "ar")->rel).find("AS_tp_bh_ca_search_action_a") != std::string::npos, "LT1 receiver plays the NPC search clip");
        }
        g_fl["palt"] = 0;
        check(lg::ix_body(C, "BP_AI_Human_C_7", true, false) == "BP_AI_Human_C_7" && lg::ix_id(C, lg::ix_body(C, "BP_AI_Human_C_7", true, false)) == std::optional<std::string>("ia"), "palt 0 corpse tap keeps the old ia");
        g_fl.erase("palt");
        g_fl["paix"] = 0;
        {
            lg::AcTx T;
            check(!lg::ac_ix(C, T, "BP_InteractableDoor_C_1", 40) && lg::ac_token(C, T, 40).empty(), "paix 0 tap sends nothing");
            check(lg::ac_feed(C, T, "1:MG_fp_bh_door_open", "MG_fp_bh_door_open", 40) && lg::ac_token(C, T, 40) == "ia1", "paix 0 montage path unchanged");
        }
        g_fl.erase("paix");
        {
            lg::AcTx T;
            check(!lg::ac_vault(C, T, false, 50) && lg::ac_token(C, T, 50).empty(), "VT1 not vaulting sends nothing");
            check(lg::ac_vault(C, T, true, 51) && lg::ac_token(C, T, 51.1) == "vt1", "VT1 vault start emits vt1");
            check(!lg::ac_vault(C, T, true, 51.2) && !lg::ac_vault(C, T, true, 51.5) && lg::ac_token(C, T, 51.6) == "vt1", "VT1 one event per vault while it lasts");
            check(!lg::ac_vault(C, T, false, 52), "VT1 vault end sends nothing");
            check(lg::ac_vault(C, T, true, 55) && lg::ac_token(C, T, 55) == "vt2", "VT1 next vault gets a new seq");
            lg::ac_vault(C, T, false, 56);
            check(lg::ac_feed(C, T, "3:MG_fp_ak74_reload", "MG_fp_ak74_reload", 56.1) && lg::ac_token(C, T, 56.2) == "rl3", "VT1 reload after a vault still emits");
            lg::AcRx R;
            check(lg::ac_rx(C, R, std::string("vt2")) == std::optional<std::string>("vt"), "VT1 receiver plays vt");
            check(!lg::ac_rx(C, R, std::string("vt2")), "VT1 same seq never replays");
            check(lg::mv_parse("0,0,1,0,vt4").act == std::optional<std::string>("vt4"), "VT1 MV carries the vault token");
            const char* fams[] = {"ar", "pt", "bh"};
            int got = 0;
            for (const char* f : fams)
            {
                auto pv = lg::pa_clip(C, "acvt", f, all);
                if (pv && std::string(pv->e->rel) == "bossfight/Korshunov/AS_tp_ar_combat_korshunov_jump_in") got++;
            }
            check(got == 3, "VT1 vault clip for every family");
            std::optional<std::string> av = std::string("acvt");
            check(lg::pa_use("fall", std::nullopt, 0, av, 5, 1) == "acvt", "VT1 vault beats fall");
            check(lg::pa_use("ladder", std::nullopt, 0, av, 5, 1) == "ladder", "VT1 ladder beats vault");
            check(lg::pa_ac_live(C, "acvt", "ar", "fall") && !lg::pa_ac_live(C, "acvt", "ar", "ladder"), "VT1 vault survives the airborne part");
            {
                std::vector<std::pair<double, std::optional<double>>> win;
                for (double d = 0; d <= 250; d += 10)
                {
                    std::optional<double> z = 1001.0;
                    if (d >= 40 && d <= 110) z = 1085.0;
                    if (d == 70) z = 1088.0;
                    if (d == 200) z = 1140.0;
                    win.push_back({d, z});
                }
                lg::VzScan ws = lg::vz_scan(win, 1000, 150, 25);
                check(ws.ok && ws.d0 == 40 && ws.d1 == 110 && ws.top == 1088, "VH2 window sill found 40..110 cm ahead, top 88 cm");
                std::vector<std::pair<double, std::optional<double>>> flat{{0, 1001.0}, {10, std::nullopt}, {20, 998.0}, {30, 1149.0}};
                check(!lg::vz_scan(flat, 1000, 150, 25).ok, "VH2 floor, no hit, ground below and the trace start are no obstacle");
                check(lg::vz_arc(60, 40, 110, 40, 60) == 1 && lg::vz_arc(0, 40, 110, 40, 60) == 1 && lg::vz_arc(150, 40, 110, 40, 60) == 1, "VH2 full lift while the legs are over the sill");
                check(lg::vz_arc(-60, 40, 110, 40, 60) == 0 && lg::vz_arc(210, 40, 110, 40, 60) == 0, "VH2 no lift before the run-up and after the landing");
                double up = lg::vz_arc(-30, 40, 110, 40, 60), dn = lg::vz_arc(180, 40, 110, 40, 60);
                check(up == 0.5 && dn == 0.5, "VH2 smooth ramp in and out");
                check(lg::vz_lift(ws, 60, 1000, 40, 60, 5) == 93, "VH2 feet clear the sill top plus margin mid-vault");
                check(lg::vz_lift(ws, 60, 1014, 40, 60, 5) == 79, "VH2 network z bump counted, no double lift");
                check(lg::vz_lift(ws, 60, 1100, 40, 60, 5) == 0, "VH2 already on top, no lift");
                check(lg::vz_lift(lg::VzScan{}, 60, 1000, 40, 60, 5) == 0, "VH2 no obstacle no lift");
            }
            check(lg::vz_step(0, 100, 0.05, 800) == 40 && lg::vz_step(40, 100, 0.05, 800) == 80 && lg::vz_step(80, 100, 0.05, 800) == 100, "VH1 lift rises at the rate limit");
            check(lg::vz_step(100, 0, 0.05, 800) == 60 && lg::vz_step(10, 0, 0.05, 800) == 0, "VH1 lift falls back to 0 and stops there");
            check(lg::vz_step(0, 300, 5, 800) == 200, "VH1 long frame gap capped at 0.25 s");
            {
                check(lg::tgt_cmc(C, true, false) == false && lg::tgt_cmc(C, true, true) == false, "SJ3 active target CMC deactivated by default, no 400 ms stale-floor snap");
                check(!lg::tgt_cmc(C, false, true) && !lg::tgt_cmc(C, std::nullopt, false), "SJ3 inactive or unread CMC left alone");
                check(!lg::tgt_mode(C, 2, false) && !lg::tgt_mode(C, 1, true), "SJ3 movement mode untouched while the CMC is off");
                g_fl["tgtnone"] = 0;
                check(lg::tgt_cmc(C, false, true) == true, "SJ3 tgtnone 0 reactivates a CMC we turned off");
                check(!lg::tgt_cmc(C, false, false) && !lg::tgt_cmc(C, true, true), "SJ3 tgtnone 0 leaves a CMC we did not turn off alone");
                check(lg::tgt_mode(C, 2, false) == 1, "SJ1 target NavWalking -> Walking by default, no navmesh snap 128 cm away");
                check(!lg::tgt_mode(C, 1, true) && !lg::tgt_mode(C, 3, false) && !lg::tgt_mode(C, std::nullopt, false), "SJ1 Walking, Falling or unread mode left alone");
                g_fl["tgtwalk"] = 0;
                check(lg::tgt_mode(C, 1, true) == 2, "SJ1 tgtwalk 0 gives NavWalking back once to a target we switched");
                check(!lg::tgt_mode(C, 1, false) && !lg::tgt_mode(C, 2, true), "SJ1 tgtwalk 0 = old behaviour");
                g_fl.erase("tgtwalk");
                g_fl.erase("tgtnone");
            }
            lg::VzRange vr;
            check(!lg::vz_track(vr, false, 800, 1), "VH1 range idle no end");
            lg::vz_track(vr, true, 800, 2);
            lg::vz_track(vr, true, 805, 2.1);
            lg::vz_track(vr, true, 798, 2.2);
            check(lg::vz_track(vr, false, 0, 2.5) && vr.z0 == 800 && vr.lo == 798 && vr.hi == 805 && vr.t0 == 2, "VH1 range keeps start, min, max");
            check(!lg::vz_track(vr, false, 0, 3), "VH1 range ends once");
            check(lg::pa_use("idle", std::nullopt, 0, av, 5, 6) == "idle", "VT1 vault ends, locomotion back");
            g_fl["pavt"] = 0;
            lg::AcTx T0;
            check(!lg::ac_vault(C, T0, true, 60) && lg::ac_token(C, T0, 60).empty(), "pavt 0 sender sends nothing");
            lg::AcRx R0;
            check(!lg::ac_rx(C, R0, std::string("vt9")), "pavt 0 receiver ignores vault");
            check(!lg::pa_clip(C, "acvt", "ar", all) && !lg::pa_ac_live(C, "acvt", "ar", "fall"), "pavt 0 no vault clip");
            g_fl.erase("pavt");
        }
    }
    {
        const std::string echo = "/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_echo/SK_detector_echo.SK_detector_echo";
        const std::string gilka = "/Game/_STALKER2/SkeletalMeshes/weapons/items/detector/SK_detector.SK_detector";
        const std::string pm = "/Game/_STALKER2/SkeletalMeshes/weapons/pt/pm/SK_pm.SK_pm@@Wpn@GripPointTPP";
        check(lg::det_path(echo) && lg::det_path(gilka) && !lg::det_path(pm), "SC1 detector path filter");
        check(lg::det_model(echo) == "echo" && lg::det_model(gilka) == "gilka" && lg::det_model("/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_bear/SK_detector_bear.SK_detector_bear") == "bear"
              && lg::det_model("/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_veles/SK_detector_veles.SK_detector_veles") == "veles"
              && lg::det_model("/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_oko/SK_detector_oko.SK_detector_oko") == "echo", "SC1 detector model from mesh");
        check(lg::det_tag(false, echo).empty() && lg::det_tag(true, echo) == "~D" + echo && lg::det_tag(true, pm) == "~D", "SC1 sender tag");
        const std::string gun = "W1~" + pm + "~st>Wpn>s>/Game/a.a>1,2,3,4,5,6,1,1,1";
        std::string x = gun + lg::det_tag(true, echo), d;
        check(lg::w_det(x, d) && d == echo && x == gun, "SC1 receiver splits detector off a gun string");
        std::string part, st;
        check(lg::w1_split(x, part, st) && part == pm && st == "st>Wpn>s>/Game/a.a>1,2,3,4,5,6,1,1,1", "SC1 gun part unchanged after split");
        std::string b = "W1~~~D" + echo;
        check(lg::w_det(b, d) && d == echo && b == "W1~~" && lg::w1_split(b, part, st) && part.empty(), "SC1 bare hands with detector");
        std::string e = "W1~~~D";
        check(lg::w_det(e, d) && d.empty() && e == "W1~~", "SC1 detector without mesh path");
        std::string n = "W1~" + pm + "~", n0 = n;
        check(!lg::w_det(n, d) && n == n0 && d.empty(), "SC1 no detector leaves string alone");
        check(lg::pa_fam("W1~~~D" + echo) == "bh", "SC1 detector alone is bare-hands family");
        check(!lg::pa_equip_swap(C, "W1~" + pm + "~", "W1~" + pm + "~~D" + echo), "SC1 detector toggle plays no gun equip clip");
        const lg::PaEnt* de = lg::pa_det(C, "idle", std::optional<std::string>(gilka));
        check(de && std::string(de->rel).find("gilka_idle_a") != std::string::npos, "SC1 idle with gilka uses the stock standing detector clip");
        de = lg::pa_det(C, "aidle", std::optional<std::string>(std::string()));
        check(de && std::string(de->fam) == "echo", "SC1 unknown detector mesh falls back to echo clip");
        check(!lg::pa_det(C, "walk", std::optional<std::string>(echo)) && !lg::pa_det(C, "idle", std::nullopt), "SC1 walking or no detector keeps locomotion");
        g_fl["padet"] = 0;
        check(!lg::pa_det(C, "idle", std::optional<std::string>(echo)), "SC1 padet 0 no detector clip");
        g_fl.erase("padet");
    }
    g_fl["parl"] = 0;
    g_fl["paia"] = 0;
    {
        lg::AcTx T;
        check(!lg::ac_ix(C, T, "BP_InteractableDoor_C_1", 1) && lg::ac_token(C, T, 1).empty(), "paia 0 door tap sends nothing");
    }
    {
        lg::AcTx T;
        check(!lg::ac_feed(C, T, "1:MG_fp_ak74_reload", "MG_fp_ak74_reload", 1) && lg::ac_token(C, T, 1).empty(), "parl 0 sender emits no reload");
        lg::AcRx R;
        check(!lg::ac_rx(C, R, std::string("rl5")), "parl 0 receiver ignores reload");
        check(!lg::ac_rx(C, R, std::string("lt6")), "paia 0 receiver ignores loot");
        check(lg::ac_rx(C, R, std::string("ml7")) == std::optional<std::string>("ml"), "paml still on");
        check(!lg::pa_clip(C, "acrl", "ar", all), "parl 0 no reload clip");
        check(!lg::pa_ac_live(C, "acrl", "ar", "idle") && lg::pa_ac_live(C, "acml", "ar", "idle"), "parl 0 mid-action drops only reload");
    }
    g_fl["pacwalk"] = 0;
    g_fl["paladder"] = 0;
    g_fl["paequip"] = 0;
    check(lg::pa_pick(C, 150, "l", true, false, false, std::nullopt).first == "cwalk", "pacwalk 0 old cwalk");
    pk = lg::pa_clip(C, "cwalk", "ar", all);
    check(pk && pk->st == "cidle", "pacwalk 0 old sliding crouch idle");
    check(lg::pa_pick(C, 150, "f", false, false, false, std::nullopt, true).first == "walk", "paladder 0 old walk");
    check(!lg::pa_clip(C, "ladder", "ar", all), "paladder 0 no ladder clip");
    check(!lg::pa_equip(C, "bh", "ar"), "paequip 0 instant swap");
    check(!lg::pa_clip(C, "eqar", "ar", all), "paequip 0 no equip clip");
    g_fl["paaimmv"] = 0;
    check(lg::pa_pick(C, 300, "f", false, true, false, std::nullopt).first == "run", "paaimmv 0 old run");
    pk = lg::pa_clip(C, "awalk", "pt", all);
    check(pk && pk->st == "walk" && std::string(pk->e->rel) == "bh/locomotion/stand/AS_tp_bh_walk_fwd", "paaimmv 0 old pistol arms-down walk");
    std::printf(fails ? "yaw: %d FAIL\n" : "yaw: all ok\n", fails);
    return fails ? 1 : 0;
}
