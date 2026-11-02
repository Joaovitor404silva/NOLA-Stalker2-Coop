#include "s2m_bodies_logic.h"

#include <cstdio>
#include <cstring>

namespace lg = m01::lg;

static int fails = 0;
static double g_hand = 1, g_rl = 1, g_dg = 1, g_add = 1, g_wrl = 1, g_wchr = 1, g_whold = 1, g_wskel = 1, g_bolt = 1, g_rlar = 1, g_rlcb = 1, g_rlstk = 1, g_rlcan = 1, g_rlpchr = 1, g_knife = 1, g_knml = 1.5, g_rlms = 1, g_sh = 1, g_shfb = 1, g_shgun = 1, g_shbi = -1;
static double fl(const char* k, double d)
{
    if (std::strcmp(k, "pawrl") == 0) return g_wrl;
    if (std::strcmp(k, "pawrlchr") == 0) return g_wchr;
    if (std::strcmp(k, "pawrlhold") == 0) return g_whold;
    if (std::strcmp(k, "pwskel") == 0) return g_wskel;
    if (std::strcmp(k, "pabolt") == 0) return g_bolt;
    if (std::strcmp(k, "paknife") == 0) return g_knife;
    if (std::strcmp(k, "paknml") == 0) return g_knml;
    if (std::strcmp(k, "pwrlar") == 0) return g_rlar;
    if (std::strcmp(k, "pawrlcb") == 0) return g_rlcb;
    if (std::strcmp(k, "pwrlstk") == 0) return g_rlstk;
    if (std::strcmp(k, "pwrlcan") == 0) return g_rlcan;
    if (std::strcmp(k, "pwrlpchr") == 0) return g_rlpchr;
    if (std::strcmp(k, "pwrlms") == 0) return g_rlms;
    if (std::strcmp(k, "pashoot") == 0) return g_sh;
    if (std::strcmp(k, "pashootfb") == 0) return g_shfb;
    if (std::strcmp(k, "pashgun") == 0) return g_shgun;
    if (std::strcmp(k, "pashbi") == 0) return g_shbi;
    return std::strcmp(k, "pwhand") == 0 ? g_hand : std::strcmp(k, "pwrlhand") == 0 ? g_rl : std::strcmp(k, "padetg") == 0 ? g_dg : std::strcmp(k, "paadd") == 0 ? g_add : d;
}
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}

static void rc1_shoot(const lg::Ctx& C);

static const m01::lg::WrlEnt* pmw_probe()
{
    return m01::lg::wrl_ent("SK_pm_Skeleton");
}

int main()
{
    lg::Ctx C{&fl, nullptr, nullptr};
    const std::string knife = "/Game/_STALKER2/SkeletalMeshes/weapons/knifes/knife/SK_knife.SK_knife";
    const std::string pm = "/Game/_STALKER2/SkeletalMeshes/weapons/pt/pm/SK_pm.SK_pm";
    check(lg::w_sock(C, knife, "") == "jnt_r_hand", "KN1 knife goes on the right hand bone");
    check(lg::w_knife(C, knife) && !lg::w_knife(C, pm), "KN1 only knife meshes take the knife grip");
    check(lg::ac_dur(C, "ml", true, 2.53) == 1.5, "KN1 knife melee ends before the pistol clip re-grips");
    check(lg::ac_dur(C, "ml", false, 2.53) == 2.53, "KN1 pistol melee keeps the whole clip");
    check(lg::ac_dur(C, "rl", true, 2.0) == 2.0, "KN1 other actions untouched");
    g_knml = 0;
    check(lg::ac_dur(C, "ml", true, 2.53) == 2.53, "KN1 paknml=0 plays the whole clip");
    g_knml = 1.5;
    g_knife = 0;
    check(!lg::w_knife(C, knife), "KN1 paknife=0 off");
    check(lg::w_sock(C, knife, "") == "jnt_r_weapon", "knife goes on the right hand weapon bone");
    check(lg::w_sock(C, "/game/_stalker2/skeletalmeshes/Weapons/Knifes/k2/SK_k2.SK_k2", "") == "jnt_r_weapon", "any knife mesh, any case");
    check(lg::w_sock(C, knife, "GripPointTPP") == "jnt_r_weapon", "knife ignores the sender socket");
    g_knife = 1;
    check(lg::w_sock(C, knife, "GripPointTPP") == "jnt_r_hand", "KN1 knife ignores the sender socket");
    check(lg::w_sock(C, pm, "") == "GripPointTPP", "pistol keeps GripPointTPP");
    check(lg::w_sock(C, pm, "SomeSock") == "SomeSock", "sender socket kept for guns");
    const std::optional<std::string> rl = std::string("acrl"), ml = std::string("acml");
    check(lg::w_rl_hand(C, rl, true, "pt"), "pistol reload goes to the hand bone");
    check(!lg::w_rl_hand(C, rl, false, "pt"), "reload not playing keeps the grip socket");
    check(!lg::w_rl_hand(C, ml, true, "pt"), "other pistol actions keep the grip socket");
    check(!lg::w_rl_hand(C, rl, true, "ar"), "rifle reload untouched");
    check(!lg::w_rl_hand(C, std::nullopt, true, "pt"), "no action keeps the grip socket");
    check(lg::w_det_hand(C, true, "pt"), "pistol with the detector goes to the hand bone");
    check(!lg::w_det_hand(C, false, "pt"), "pistol alone keeps the grip socket");
    check(!lg::w_det_hand(C, true, "bh"), "knife or bolt with the detector untouched");
    g_dg = 0;
    check(!lg::w_det_hand(C, true, "pt"), "padetg=0 restores the old socket");
    g_dg = 1;
    g_rl = 2;
    check(lg::w_rl_hand(C, std::nullopt, false, "pt"), "pwrlhand=2 keeps the pistol in the hand always");
    check(!lg::w_rl_hand(C, std::nullopt, false, "bh"), "pwrlhand=2 only for pistols");
    g_rl = 0;
    check(!lg::w_rl_hand(C, rl, true, "pt"), "pwrlhand=0 restores the old socket");
    const lg::PaEnt* ar = lg::ac_ent("acrl", "ar");
    check(ar && std::strstr(ar->rel, "AS_tp_ar_combat_ak74_reload"), "rifle reload clip is the stock MG_tp_ar_combat_ak74_reload sequence");
    check(lg::ac_layer(C, 1), "additive local-space clip plays as a layer over the locomotion");
    check(lg::ac_layer(C, 2), "additive mesh-space clip plays as a layer over the locomotion");
    check(!lg::ac_layer(C, 0), "full-pose clip keeps the DefaultSlot replace path");
    g_add = 0;
    check(!lg::ac_layer(C, 1), "paadd=0 restores the old DefaultSlot path");
    g_add = 1;
    g_hand = 0;
    check(lg::w_sock(C, knife, "") == "GripPointTPP", "pwhand=0 restores the old socket");
    g_hand = 1;
    check(lg::w_rl_ar(C, "ar", true), "rifle goes to the right hand bone while its reload clip plays");
    check(!lg::w_rl_ar(C, "ar", false), "rifle keeps the grip socket outside the reload clip");
    check(!lg::w_rl_ar(C, "pt", true), "pistol reload stays on the pwrlhand path");
    check(!lg::w_rl_ar(C, "", true), "no weapon family no hand move");
    g_rlar = 0;
    check(!lg::w_rl_ar(C, "ar", true), "pwrlar=0 restores the old socket");
    g_rlar = 1;
    check(lg::pa_rl_base(C, "idle", true) == "aidle", "additive reload over idle uses the combat idle base");
    check(lg::pa_rl_base(C, "walk", true) == "awalk", "additive reload over walk uses the combat walk base");
    check(lg::pa_rl_base(C, "idle", false) == "idle", "no reload keeps the relax idle");
    check(lg::pa_rl_base(C, "run", true) == "run", "run keeps its own base");
    g_rlcb = 0;
    check(lg::pa_rl_base(C, "idle", true) == "idle", "pawrlcb=0 restores the relax base");
    g_rlcb = 1;
    check(lg::w_rl_stock(C, true, "pt"), "live pistol reload clip puts the gun on its stock socket");
    check(lg::w_rl_stock(C, true, "ar"), "RR2 rifle reload keeps the gun on its stock socket with the full gun clip");
    check(!lg::w_rl_stock(C, false, "ar"), "RR2 no reload clip leaves the rifle socket alone");
    check(!lg::w_rl_ar(C, "ar", true) || lg::w_rl_stock(C, true, "ar"), "RR2 stock socket wins over the hand grip");
    g_rlms = 0;
    check(!lg::w_rl_stock(C, true, "ar"), "RR2 pwrlms=0 restores the rifle hand grip");
    g_rlms = 1;
    check(!lg::w_rl_stock(C, false, "pt"), "no reload clip keeps the hand grip");
    g_rlstk = 2;
    check(lg::w_rl_stock(C, true, "ar"), "pwrlstk=2 puts every family on its stock socket");
    g_rlstk = 0;
    check(!lg::w_rl_stock(C, true, "pt"), "pwrlstk=0 restores the hand grip during the clip");
    g_rlstk = 1;
    const lg::WrlEnt* ak = lg::wrl_pick(C, "rl", "SK_AK74_Skeleton");
    check(ak && std::strstr(ak->wpn, "AS_ak74_tp_ar_combat_reload") && std::strstr(ak->chr, "AS_tp_ar_combat_ak74_reload"), "AK74 skeleton maps to the stock tp weapon and character reload pair");
    check(ak && ak->len > 3.6 && ak->len < 3.7 && ak->add == 1, "AK74 weapon clip length matches the character clip, character clip additive");
    check(lg::wrl_chr(C, ak) && std::strstr(lg::wrl_chr(C, ak), "ak74"), "additive character clip comes from the table");
    const lg::WrlEnt* pkp = lg::wrl_pick(C, "rl", "SK_pkp00000_Skeleton");
    check(pkp && lg::wrl_ms(C, pkp, "ar") && std::strstr(lg::wrl_ms(C, pkp, "ar"), "AS_tp_ar_combat_pkp_reload"), "RR2 PKP plays its own full body reload clip in the action slot");
    check(lg::wrl_ms(C, ak, "ar") && std::strstr(lg::wrl_ms(C, ak, "ar"), "AS_tp_ar_combat_ak74_reload"), "RR2 AK plays its own additive reload clip in the action slot");
    check(!lg::wrl_ms(C, pmw_probe(), "pt"), "RR2 pistols keep the pr3 path");
    check(!lg::wrl_ms(C, nullptr, "ar"), "RR2 no table row plays nothing new");
    g_rlms = 0;
    check(!lg::wrl_ms(C, pkp, "ar"), "RR2 pwrlms=0 restores the old rifle body clip");
    g_rlms = 1;
    const lg::WrlEnt* pmw = lg::wrl_pick(C, "rl", "SK_pm_Skeleton");
    check(pmw && std::strstr(pmw->wpn, "AS_pm_tp_pt_combat_reload"), "PM pistol gets its tp weapon reload clip");
    check(!lg::wrl_chr(C, pmw), "non-additive pistol character clip keeps the old path");
    check(lg::wrl_body(C, pmw, "pt") && std::strstr(lg::wrl_body(C, pmw, "pt"), "AS_tp_pt_combat_pm_reload"), "PR2 PM body reload clip comes from its own table row");
    const lg::WrlEnt* udp = lg::wrl_pick(C, "rl", "SKEL_udp");
    check(udp && lg::wrl_body(C, udp, "pt") && std::strstr(lg::wrl_body(C, udp, "pt"), "AS_tp_pt_combat_udp_reload"), "PR2 UDP plays its own body reload clip, not the PM one");
    const lg::WrlEnt* rh = lg::wrl_pick(C, "rl", "skel_rhino00000");
    check(rh && lg::wrl_body(C, rh, "pt") && std::strstr(lg::wrl_body(C, rh, "pt"), "rhino"), "PR2 Rhino plays its own body reload clip");
    check(!lg::wrl_body(C, ak, "ar"), "PR2 additive rifle rows keep the layer path");
    const lg::WrlEnt* gp = lg::wrl_pick(C, "rl", "SKEL_Gp3a");
    check(gp && !lg::wrl_body(C, gp, "ar"), "PR2 non-additive rifle rows are not touched by the pistol body clip");
    check(!lg::wrl_body(C, nullptr, "pt"), "PR2 no table row keeps the generic body clip");
    g_rlpchr = 0;
    check(!lg::wrl_body(C, udp, "pt"), "PR2 pwrlpchr=0 restores the generic PM body clip");
    g_rlpchr = 1;
    check(lg::w_rl_cancel(C, "pt", true), "PR2 pistol in the hand cancels the gun clip offset motion");
    check(!lg::w_rl_cancel(C, "pt", false), "PR2 pistol on the stock socket keeps the gun clip motion");
    check(!lg::w_rl_cancel(C, "ar", true), "PR2 rifles keep their own reload path");
    g_rlcan = 0;
    check(!lg::w_rl_cancel(C, "pt", true), "PR2 pwrlcan=0 restores the raw gun clip on the hand");
    g_rlcan = 1;
    check(!lg::wrl_pick(C, "ml", "SK_AK74_Skeleton"), "melee plays no weapon reload clip");
    check(!lg::wrl_pick(C, "rl", "SK_unknown_Skeleton"), "unknown skeleton plays no weapon clip");
    check(!lg::wrl_pick(C, "rl", ""), "gun without skeleton plays no weapon clip");
    int nrows = 0;
    for (const auto& e : lg::kWrl) { (void)e; nrows++; }
    check(nrows >= 30, "generated table covers every tp weapon collection");
    const std::string w1 = "W1~/Game/x/SK_AK74.SK_AK74@@SK_AK74_C@GripPointTPP~a>b>jnt_magazine>m>0,0,0,0,0,0,1,1,1";
    const std::string w2 = "W1~/Game/x/SK_AK74.SK_AK74@@SK_AK74_C@GripPointTPP~";
    const std::string w3 = "W1~/Game/x/SK_pm.SK_pm@@SK_pm_C@GripPointTPP~";
    check(lg::wrl_hold(C, w1, w2, true), "magazine statics change during the clip: gun kept");
    check(!lg::wrl_hold(C, w1, w3, true), "weapon switch during the clip: gun rebuilt");
    check(!lg::wrl_hold(C, w1, w2, false), "no clip: statics update at once");
    check(!lg::wrl_hold(C, "W1~~", "W1~~x", true), "empty hands never held");
    g_whold = 0;
    check(!lg::wrl_hold(C, w1, w2, true), "pawrlhold=0 restores the old rebuild");
    g_whold = 1;
    g_wrl = 0;
    check(!lg::wrl_pick(C, "rl", "SK_AK74_Skeleton"), "pawrl=0 restores the old reload");
    g_wrl = 1;
    g_wchr = 0;
    check(!lg::wrl_chr(C, ak), "pawrlchr=0 keeps the family clip");
    g_wchr = 1;
    check(std::strstr(lg::w_cls(C), "SkeletalMeshComponent") != nullptr, "gun is a skeletal mesh so it can play its clip");
    g_wskel = 0;
    check(std::strstr(lg::w_cls(C), "PoseableMeshComponent") != nullptr, "pwskel=0 restores the poseable gun");
    const std::string bolt = "/Game/_STALKER2/SkeletalMeshes/weapons/bolt/SK_bolt.SK_bolt";
    const std::string boltW = "W1~" + bolt + "@@WeaponInHandsMesh@GripPointTPP~";
    const std::string pmW = "W1~" + pm + "@@WeaponInHandsMesh@GripPointTPP~";
    check(lg::w_sock(C, bolt, "GripPointTPP") == "jnt_r_hand", "BV2 bolt goes on the right hand bone at the stock jnt_item offset");
    check(lg::pa_famc(C, boltW) == "bh", "bolt uses the one-hand bare-hands set, not the rifle set");
    check(lg::pa_famc(C, boltW + "~D/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_echo/SK_detector_echo.SK_detector_echo") == "bh", "bolt with the detector stays one-hand");
    check(lg::pa_famc(C, pmW) == "pt", "pistol family unchanged");
    check(lg::pa_equip_swap(C, boltW, pmW) == std::optional<std::string>("eqpt"), "bolt to pistol plays the pistol draw");
    check(lg::pa_equip_swap(C, pmW, boltW) == std::optional<std::string>("uqpt"), "pistol to bolt plays the pistol holster");
    check(!lg::w_det_hand(C, true, lg::pa_famc(C, boltW)), "bolt with the detector keeps its own hand socket");
    g_bolt = 0;
    check(lg::pa_famc(C, boltW) == "bh", "pabolt=0 keeps the base bare-hands family");
    check(lg::w_sock(C, bolt, "GripPointTPP") == "GripPointTPP", "pabolt=0 restores the old socket");
    g_bolt = 1;
    g_hand = 0;
    check(lg::w_sock(C, knife, "") == "GripPointTPP", "pwhand=0 restores the old socket");
    const std::optional<std::string> echo = std::string("/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_echo/SK_detector_echo.SK_detector_echo");
    auto ov = lg::pa_ov(C, "idle", "pt", echo, false, true);
    check(ov.size() == 1 && ov[0].prof == "right_hand_blend_mask" && ov[0].rel == lg::kPaOvPt && ov[0].slot == "ContextualSlot", "PD1 idle pistol+detector: right arm pistol layer only");
    ov = lg::pa_ov(C, "walk", "pt", echo, false, false);
    check(ov.size() == 2 && ov[1].prof == "left_hand_blend_mask" && ov[1].slot == "MainActionSlot" && ov[1].rel.find("detector_echo_idle_a") != std::string::npos, "PD1 walk pistol+detector: right arm pistol + left arm detector layers");
    ov = lg::pa_ov(C, "walk", "bh", echo, false, false);
    check(ov.size() == 1 && ov[0].prof == "left_hand_blend_mask", "PD1 walk knife/bolt+detector: left arm detector layer only");
    check(lg::pa_ov(C, "idle", "pt", std::nullopt, false, false).empty(), "PD1 pistol alone: no layer");
    check(lg::pa_ov(C, "acrl", "pt", echo, true, false).empty(), "PD1 action clip: no layer");
    check(lg::pa_ov(C, "eqpt", "pt", echo, false, false).empty(), "PD1 equip clip: no layer");
    check(lg::pa_ov(C, "ladder", "pt", echo, false, false).empty(), "PD1 ladder: no layer");
    check(lg::pa_ov(C, "idle", "pt", std::string(), false, true).size() == 1, "PD1 unknown detector mesh still layers the pistol arm");
    rc1_shoot(C);
    std::printf(fails ? "wsock: %d FAIL\n" : "wsock: all ok\n", fails);
    return fails ? 1 : 0;
}

static void rc1_shoot(const lg::Ctx& C)
{
    const lg::WshEnt* ak = lg::wsh_pick(C, "SK_AK74_Skeleton", "ar");
    check(ak && std::string(ak->chr).find("AS_tp_ar_combat_ak74_shoot") != std::string::npos && ak->add > 0, "RC1 AK74 skeleton picks its own additive tp shoot clip");
    const lg::WshEnt* pm = lg::wsh_pick(C, "SK_pm_Skeleton", "pt");
    check(pm && std::string(pm->chr).find("AS_tp_pt_combat_pm_shoot") != std::string::npos, "RC1 PM skeleton picks the pistol shoot clip");
    const lg::WshEnt* pk = lg::wsh_pick(C, "SK_pkp00000_Skeleton", "ar");
    check(pk && std::string(pk->chr).find("pkp_shoot") != std::string::npos && pk->rate == 2.0, "RC1 PKP shoot clip with its table play rate 2.0");
    const lg::WshEnt* dn = lg::wsh_pick(C, "SK_dnipro_Skeleton", "ar");
    check(dn && std::string(dn->chr).find("dnipro_shoot") != std::string::npos, "RC1 Dnipro keys on its own skeleton although its gun montage is Lavina's");
    check(lg::wsh_pick(C, "SK_rpg7_Skeleton", "ar") == ak, "RC1 RPG7 (no tp collection) falls back to the rifle clip");
    check(lg::wsh_pick(C, "SK_unknown", "pt") == pm, "RC1 unknown pistol falls back to the PM clip");
    check(lg::wsh_pick(C, "SK_AK74_Skeleton", "bh") == nullptr, "RC1 knife/bolt/bare hands never kick");
    g_shfb = 0;
    check(lg::wsh_pick(C, "SK_rpg7_Skeleton", "ar") == nullptr && lg::wsh_pick(C, "SK_AK74_Skeleton", "ar") == ak, "RC1 pashootfb=0 drops only the fallback");
    g_shfb = 1;
    g_sh = 0;
    check(lg::wsh_pick(C, "SK_AK74_Skeleton", "ar") == nullptr, "RC1 pashoot=0 restores the old no-kick path");
    g_sh = 1;
    int seen = -1;
    check(!lg::wsh_new(seen, 7) && seen == 7, "RC1 first look only syncs the shot counter (no kick on spawn)");
    check(!lg::wsh_new(seen, 7), "RC1 no new shot, no kick");
    check(lg::wsh_new(seen, 8) && seen == 8, "RC1 new shot kicks");
    check(lg::wsh_new(seen, 11) && seen == 11, "RC1 several shots in one tick kick once");
    check(!lg::wsh_new(seen, 0) && seen == 0, "RC1 counter reset resyncs without a kick");
    check(lg::wsh_go(10, 9.5, false, false), "RC1 kick after the reload clip ended");
    check(!lg::wsh_go(10, 12, false, false), "RC1 no kick over a live reload clip");
    check(!lg::wsh_go(10, 0, true, false) && !lg::wsh_go(10, 0, false, true), "RC1 no kick during an action clip or on a ladder");
    for (const auto& e : lg::kWsh) check(e.chr && std::strlen(e.chr) > 10 && e.len > 0 && e.rate > 0, "RC1 every shoot table row has a clip, length and rate");
    for (const auto& e : lg::kWsh) check(e.wpn && std::strstr(e.wpn, "/Weapons/") && e.bi > 0 && e.bo > 0, "RC2 every shoot table row has its gun clip and stock blend times");
    {
        const lg::WshEnt* sg = lg::wsh_ent("SK_m86000_Skeleton");
        check(sg && lg::wsh_gun(C, sg, "SK_m86000_Skeleton") && std::strstr(lg::wsh_gun(C, sg, "SK_m86000_Skeleton"), "AS_m860_tp_ar_combat_shoot"), "RC2 M860 pumps its own gun clip");
        const lg::WshEnt* ml = lg::wsh_ent("SKEL_ThreeLine");
        check(ml && lg::wsh_gun(C, ml, "SKEL_ThreeLine") && std::strstr(ml->wpn, "AS_threeline_tp_ar_combat_shoot"), "RC2 Mosin cycles its own bolt clip");
        check(lg::wsh_gun(C, lg::wsh_pick(C, "SK_rpg7_Skeleton", "ar"), "SK_rpg7_Skeleton") == nullptr, "RC2 a family fallback never plays another gun's clip on the mesh");
        g_shgun = 0;
        check(lg::wsh_gun(C, sg, "SK_m86000_Skeleton") == nullptr, "RC2 pashgun=0 keeps the gun still");
        g_shgun = 1;
        check(lg::wsh_blend(C, "pashbi", 0.05) == 0.05, "RC2 blend in defaults to the stock montage time");
        g_shbi = 0.2;
        check(lg::wsh_blend(C, "pashbi", 0.05) == 0.2, "RC2 pashbi >= 0 overrides the stock blend");
        g_shbi = -1;
        const lg::WshEnt* pm = lg::wsh_ent("SK_pm_Skeleton");
        check(pm && pm->bi == 0.05 && pm->bo == 0.1, "RC2 PM uses the stock 0.05/0.1 montage blends");
    }
    {
        const char* cand[] = {"wsh_sids.tsv", "bodies/wsh_sids.tsv", "tests/bodies/wsh_sids.tsv", "../wsh_sids.tsv"};
        FILE* f = nullptr;
        for (const char* c : cand)
            if (!f && fopen_s(&f, c, "r") != 0) f = nullptr;
        check(f != nullptr, "RC1 wsh_sids.tsv found");
        int n = 0, own = 0, fb = 0, none = 0, bad = 0;
        char line[1024];
        while (f && std::fgets(line, sizeof line, f))
        {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            std::vector<std::string> col;
            size_t a = 0;
            for (size_t b; (b = s.find('\t', a)) != std::string::npos; a = b + 1) col.push_back(s.substr(a, b - a));
            col.push_back(s.substr(a));
            if (col.size() < 6 || col[0] == "sid") continue;
            n++;
            const lg::WshEnt* e = lg::wsh_pick(C, col[2] == "-" ? std::string() : col[2], col[3]);
            const char* how = !e ? "NONE" : e == lg::wsh_ent(col[2]) ? "own" : "fallback";
            (e ? (std::strcmp(how, "own") == 0 ? own : fb) : none)++;
            if (col[4] != how) { bad++; std::printf("  sid %s skel %s fam %s: table says %s, logic says %s\n", col[0].c_str(), col[2].c_str(), col[3].c_str(), col[4].c_str(), how); }
            if (!e && col[3] != "bh") { bad++; std::printf("  sid %s (fam %s) has no shoot clip\n", col[0].c_str(), col[3].c_str()); }
        }
        if (f) std::fclose(f);
        std::printf("RC1 weapon SIDs %d: own clip %d, family fallback %d, none %d (none = launcher data rows)\n", n, own, fb, none);
        check(n > 100 && bad == 0, "RC1 every equippable weapon SID resolves a shoot clip");
    }
}
