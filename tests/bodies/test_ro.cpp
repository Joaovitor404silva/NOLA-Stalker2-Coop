#include "s2m_bodies_ro.h"

#include <cstdio>
#include <cstring>

namespace lg = m01::lg;
namespace ro = m01::ro;

static int fails = 0;
static double g_ro = 1, g_fist = 1, g_det = 1, g_barm = 1, g_bdet = 1;
static double fl(const char* k, double d) { return std::strcmp(k, "pabdet") == 0 ? g_bdet : std::strcmp(k, "pabarm") == 0 ? g_barm : std::strcmp(k, "paro") == 0 ? g_ro : std::strcmp(k, "parofist") == 0 ? g_fist : std::strcmp(k, "parodet") == 0 ? g_det : d; }
static void check(bool ok, const char* what)
{
    if (!ok)
    {
        ++fails;
        std::printf("FAIL %s\n", what);
    }
}
static bool has(const std::vector<std::string>& v, const char* s)
{
    for (const auto& x : v)
        if (x == s) return true;
    return false;
}
static ro::Skel skel()
{
    ro::Skel k;
    k.names = {"jnt_root", "jnt_hips", "jnt_spine_01", "jnt_r_clavicle", "jnt_r_upperarm", "jnt_r_forearm", "jnt_r_hand", "jnt_r_index_01", "jnt_r_weapon", "jnt_l_clavicle", "jnt_l_upperarm", "jnt_l_forearm", "jnt_l_hand", "jnt_l_index_01", "jnt_item", "jnt_r_thigh"};
    k.par = {-1, 0, 1, 2, 3, 4, 5, 6, 6, 2, 9, 10, 11, 12, 0, 1};
    return k;
}
static void ident(ro::Tf& t, double x, double y, double z)
{
    std::memset(&t, 0, sizeof t);
    t.q[3] = 1;
    t.t[0] = x; t.t[1] = y; t.t[2] = z;
    t.s[0] = t.s[1] = t.s[2] = 1;
}
static std::vector<ro::Tf> pose(const ro::Skel& k)
{
    std::vector<ro::Tf> b(k.names.size());
    for (size_t i = 0; i < b.size(); i++) ident(b[i], (double)i * 10, (double)i, 100);
    return b;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-6; }

int main()
{
    lg::Ctx C{&fl, nullptr, nullptr};
    const std::string pm = "/Game/_STALKER2/SkeletalMeshes/weapons/pt/pm/SK_pm.SK_pm";
    const std::string knife = "/Game/_STALKER2/SkeletalMeshes/weapons/knifes/knife/SK_knife.SK_knife";
    const std::string bolt = "/Game/_STALKER2/SkeletalMeshes/weapons/bolt/SK_bolt.SK_bolt";
    const std::string ak = "/Game/_STALKER2/SkeletalMeshes/weapons/ar/ak74/SK_ak74.SK_ak74";
    check(ro::rh_fam(pm) == "pt" && ro::rh_fam(knife) == "kn" && ro::rh_fam(bolt) == "bt" && ro::rh_fam(ak) == "" && ro::rh_fam("") == "", "PD2 right-hand family per item");
    check(ro::rh_fam("/Game/_STALKER2/SkeletalMeshes/Weapons/Grenades/f1/SK_f1.SK_f1") == "bt", "PD2 grenade is a closed fist");

    auto f = ro::fams(C, "pt", true, "idle", false, true);
    check(f.size() == 1 && f[0] == "pt", "PD2 pistol+detector idle: right arm pistol pose only, detector idle clip keeps the left arm");
    f = ro::fams(C, "pt", true, "walk", false, false);
    check(f.size() == 2 && has(f, "pt") && has(f, "de"), "PD2 pistol+detector walk: pistol arm + detector arm");
    check(ro::fams(C, "pt", false, "idle", false, false).empty(), "PD2 pistol alone unchanged");
    check(ro::fams(C, "pt", false, "walk", false, false).empty(), "PD2 pistol alone walking unchanged");
    f = ro::fams(C, "kn", true, "idle", false, true);
    check(f == std::vector<std::string>({"kn"}), "PF1 knife+detector idle: knife grip only, no pistol arm");
    f = ro::fams(C, "bt", false, "walk", false, false);
    check(f.size() == 1 && f[0] == "bo", "BV2 bolt alone: stock bolt-holding arm");
    f = ro::fams(C, "bt", true, "walk", false, false);
    check(f == std::vector<std::string>({"bo", "de"}), "BV2 bolt+detector walk: bolt arm + detector arm, no pistol arm");
    g_barm = 0;
    f = ro::fams(C, "bt", false, "walk", false, false);
    check(f.size() == 1 && f[0] == "bt", "BV2 pabarm 0: old closed fist");
    g_barm = 1;
    check(ro::fams(C, "", true, "walk", false, false) == std::vector<std::string>{"de"}, "PD2 empty right hand + detector walk: detector arm only");
    check(ro::fams(C, "pt", true, "acrl", true, false).empty(), "PD2 reload action: no override");
    check(ro::fams(C, "pt", true, "eqpt", false, false).empty() && ro::fams(C, "pt", true, "uqpt", false, false).empty(), "PD2 equip/unequip: no override");
    check(ro::fams(C, "pt", true, "ladder", false, false).empty() && ro::fams(C, "bt", false, "fall", false, false).empty(), "PD2 ladder/fall: no override");
    check(ro::fams(C, "pt", true, "ladul", false, false).empty() && ro::fams(C, "bt", false, "laddr", false, false).empty(), "LD1 ladder step clips: no arm override");
    g_ro = 0;
    check(ro::fams(C, "pt", true, "idle", false, true).empty() && ro::fams(C, "bt", false, "idle", false, false).empty(), "PD2 paro 0 = old behaviour");
    g_ro = 1; g_fist = 0;
    check(ro::fams(C, "bt", false, "idle", false, false).empty() && ro::fams(C, "pt", true, "idle", false, true).size() == 1, "PD2 parofist 0 drops only the fist");
    g_fist = 1; g_det = 0;
    check(ro::fams(C, "pt", true, "walk", false, false) == std::vector<std::string>{"pt"}, "PD2 parodet 0 drops only the detector arm");
    g_det = 1;

    ro::Skel k = skel();
    check(ro::hand_r(k) == 6 && ro::clav(k, 6) == 3 && ro::mirror("jnt_r_hand") == "jnt_l_hand" && ro::clav(k, 12) == 9, "PD2 skeleton rules find hands and clavicles");
    ro::Pick p = ro::pick(k, "pt");
    check(p.bones == std::vector<int>({3, 4, 5, 6, 7, 8}) && p.cp[8] == 6, "PD2 pistol pose covers the whole right arm and the weapon bone");
    p = ro::pick(k, "bt");
    check(p.bones == std::vector<int>({7, 8}) && p.cp[8] == 6, "PD2 fist covers right fingers and puts the weapon bone in the palm, hand itself untouched");
    p = ro::pick(k, "bo", 2);
    check(p.bones == std::vector<int>({3, 4, 5, 6, 7, 8, 14}) && p.cp[8] == 6 && p.cp[14] == 6 && p.rot == 3, "BV2 bolt arm covers the whole right arm, weapon bone and jnt_item follow the right hand");
    p = ro::pick(k, "de");
    check(p.bones == std::vector<int>({9, 10, 11, 12, 13, 14}) && p.cp[14] == 12, "PD2 detector arm covers the left arm and jnt_item follows the left hand");

    std::vector<ro::Tf> ref = pose(k);
    ref[4].q[2] = std::sin(0.4); ref[4].q[3] = std::cos(0.4);
    ref[13].q[0] = std::sin(0.3); ref[13].q[3] = std::cos(0.3);
    p = ro::pick(k, "pt");
    std::vector<ro::Loc> cap = ro::capture(ref.data(), (int)ref.size(), p);
    std::map<int, ro::Loc> cm;
    for (size_t i = 0; i < p.bones.size(); i++) cm[p.bones[i]] = cap[i];
    std::vector<ro::Tgt> tg = ro::targets(k, "pt", &cm);
    check(tg.size() == 6, "PD2 captured pose gives one target per picked bone");

    std::vector<ro::Tf> b = pose(k);
    for (auto& t : b) t.t[2] += 50;
    std::vector<ro::Tf> b0 = b;
    ro::apply(b.data(), (int)b.size(), k.par, tg);
    ro::Loc l = ro::local(b[3], b[4]), lr = ro::local(ref[3], ref[4]);
    check(near(l.q[2], lr.q[2]) && near(l.q[3], lr.q[3]), "PD2 upperarm gets the stock local rotation");
    ro::Loc lw = ro::local(b[6], b[8]), lwr = ro::local(ref[6], ref[8]);
    check(near(lw.t[0], lwr.t[0]) && near(lw.t[1], lwr.t[1]) && near(lw.t[2], lwr.t[2]), "PD2 weapon bone stays glued to the hand");
    bool same = true;
    for (int i : {0, 1, 2, 9, 10, 11, 12, 13, 14, 15})
        for (int q = 0; q < 4; q++) same = same && near(b[i].q[q], b0[i].q[q]) && (q == 3 || near(b[i].t[q], b0[i].t[q]));
    check(same, "PD2 legs, spine and left arm untouched by the pistol pose");
    check(near(b[3].t[0], b0[3].t[0]) && near(b[3].t[2], b0[3].t[2]), "PD2 clavicle stays on the spine (body moves, arm follows)");

    std::vector<ro::Tf> h = pose(k);
    for (auto& t : tg) t.w = 0.5;
    ro::apply(h.data(), (int)h.size(), k.par, tg);
    ro::Loc lh = ro::local(h[3], h[4]);
    check(lh.q[2] > 0.01 && lh.q[2] < lr.q[2] - 0.01, "PD2 half weight blends between clip and stock pose");
    std::vector<ro::Tf> z = pose(k), z0 = z;
    for (auto& t : tg) t.w = 0;
    ro::apply(z.data(), (int)z.size(), k.par, tg);
    check(std::memcmp(z.data(), z0.data(), z.size() * sizeof(ro::Tf)) == 0, "PD2 zero weight leaves the pose bit-identical");
    std::vector<ro::Tf> e = pose(k), e0 = e;
    ro::apply(e.data(), (int)e.size() - 1, k.par, tg);
    check(std::memcmp(e.data(), e0.data(), e.size() * sizeof(ro::Tf)) == 0, "PD2 bone count mismatch does nothing");

    std::map<std::string, double> a;
    ro::weights(a, {"pt"}, 0.1, 0.2);
    check(near(a["pt"], 0.5), "PD2 blend in over parobt");
    ro::weights(a, {"pt"}, 0.1, 0.2);
    ro::weights(a, {"pt"}, 0.1, 0.2);
    check(near(a["pt"], 1), "PD2 blend in saturates at 1");
    ro::weights(a, {"pt", "de"}, 0.1, 0.2);
    check(near(a["pt"], 1) && near(a["de"], 0.5), "PD2 starting to walk adds the detector arm without dropping the pistol arm");
    ro::weights(a, {}, 0.1, 0.2);
    check(near(a["pt"], 0.5) && !a.count("de"), "PD2 blend out, no snap");
    ro::weights(a, {}, 0.1, 0.2);
    check(a.empty(), "PD2 finished blend out removes the family");

    check(ro::samp_clip("pt") && ro::samp_clip("kn") && ro::samp_clip("bt") && ro::samp_clip("de") && ro::samp_clip("pc") && ro::samp_fam("pc") == "pt" && ro::samp_fam("kn") == "kn" && !ro::samp_clip("none") && !ro::samp_clip("nil"), "PD2 sample keys map to stock clips, none/nil off");
    {
        ro::Pick p2 = ro::pick(k, "pt", 2);
        check(p2.rot == 3 && p2.cp[3] == 1 && p2.cp[4] == 3 && p2.bones == std::vector<int>({3, 4, 5, 6, 7, 8}), "PD4 paroclav 2: clavicle copied in hips space, rest of arm on its parent");
        check(ro::pick(k, "pt", 0).bones == std::vector<int>({4, 5, 6, 7, 8}) && ro::pick(k, "pt", 0).rot == -1, "PD4 paroclav 0: pose starts below the clavicle");
        std::vector<ro::Tf> r2 = pose(k);
        r2[3].q[1] = std::sin(0.2); r2[3].q[3] = std::cos(0.2);
        std::vector<ro::Loc> c2 = ro::capture(r2.data(), (int)r2.size(), p2);
        std::map<int, ro::Loc> m2;
        for (size_t i = 0; i < p2.bones.size(); i++) m2[p2.bones[i]] = c2[i];
        std::vector<ro::Tgt> t2 = ro::targets(k, "pt", &m2, 2);
        bool rotFlag = false;
        for (const auto& t : t2) rotFlag = rotFlag || (t.bone == 3 && t.rot && t.cp == 1);
        check(t2.size() == 6 && rotFlag, "PD4 paroclav 2 targets mark the clavicle rotation-only");
        std::vector<ro::Tf> s2 = pose(k);
        for (int i : {2, 3, 4, 5, 6, 7, 8}) { s2[i].q[1] = std::sin(-0.25); s2[i].q[3] = std::cos(-0.25); }
        ro::Tf before = s2[3];
        ro::apply(s2.data(), (int)s2.size(), k.par, t2);
        check(near(s2[3].q[1], r2[3].q[1]) && near(s2[3].q[3], r2[3].q[3]), "PD4 bent spine: clavicle ends at the stock hips-space rotation (hand height follows the stock clip, not the spine)");
        check(near(s2[3].t[0], before.t[0]) && near(s2[3].t[1], before.t[1]) && near(s2[3].t[2], before.t[2]), "PD4 clavicle stays where the spine puts it");
        ro::Loc la = ro::local(s2[3], s2[4]), lb = ro::local(r2[3], r2[4]);
        check(near(la.t[0], lb.t[0]) && near(la.q[3], lb.q[3]), "PD4 upper arm follows the clavicle with the stock local pose");
    }
    check(ro::row("bo", "jnt_item") && ro::row("boh", "jnt_r_shoulder") && ro::row("bo", "jnt_r_hand_thumb_01") && !ro::row("bo", "jnt_l_hand"), "BV2 baked stock bolt arm: right arm, fingers and the jnt_item bolt offset");
    check(ro::samp_clip("bo") && ro::samp_clip("bd") && ro::samp_fam("bd") == "bo" && ro::samp_fam("bo") == "bo", "BV2 bolt sample keys map to the stock NPC bolt clips");
    {
        const double h = 15 * 3.14159265358979323846 / 180;
        double pp, yy, rr;
        double qi[4] = {0, 0, 0, 1};
        ro::q2r(qi, pp, yy, rr);
        check(near(pp, 0) && near(yy, 0) && near(rr, 0), "BV2 identity quat is a zero rotator");
        double qp[4] = {0, -std::sin(h), 0, std::cos(h)};
        ro::q2r(qp, pp, yy, rr);
        check(std::fabs(pp - 30) < 1e-6 && std::fabs(yy) < 1e-6 && std::fabs(rr) < 1e-6, "BV2 UE pitch quat converts to pitch 30");
        double qy[4] = {0, 0, std::sin(h), std::cos(h)};
        ro::q2r(qy, pp, yy, rr);
        check(std::fabs(yy - 30) < 1e-6 && std::fabs(pp) < 1e-6 && std::fabs(rr) < 1e-6, "BV2 UE yaw quat converts to yaw 30");
        double qr[4] = {-std::sin(h), 0, 0, std::cos(h)};
        ro::q2r(qr, pp, yy, rr);
        check(std::fabs(rr - 30) < 1e-6 && std::fabs(pp) < 1e-6 && std::fabs(yy) < 1e-6, "BV2 UE roll quat converts to roll 30");
    }
    {
        double q[4], pp, yy, rr;
        ro::r2q(2.4, -0.9, -168.2, q);
        ro::q2r(q, pp, yy, rr);
        check(std::fabs(pp - 2.4) < 1e-6 && std::fabs(yy + 0.9) < 1e-6 && std::fabs(rr + 168.2) < 1e-6, "PR2 rotator to quat and back keeps the grip rotation");
        ro::Loc g;
        ro::r2q(2.4, -0.9, -168.2, g.q);
        g.t[0] = 9.65; g.t[1] = 1.1; g.t[2] = -2.4;
        ro::Tf off;
        ident(off, 0, 0, 0);
        ro::Loc c = ro::grip_cancel(g, off);
        check(near(c.t[0], 9.65) && near(c.t[1], 1.1) && near(c.t[2], -2.4) && std::fabs(std::fabs(c.q[0] * g.q[0] + c.q[1] * g.q[1] + c.q[2] * g.q[2] + c.q[3] * g.q[3]) - 1) < 1e-9, "PR2 resting gun clip leaves the grip unchanged");
        ro::Tf mv;
        ident(mv, 3, -2, 5);
        ro::r2q(25, 40, -10, mv.q);
        ro::Loc rel = ro::grip_cancel(g, mv);
        ro::Loc ml;
        for (int k = 0; k < 4; k++) ml.q[k] = mv.q[k];
        for (int k = 0; k < 3; k++) ml.t[k] = mv.t[k];
        ro::Loc w = ro::cmp(rel, ml);
        check(near(w.t[0], g.t[0]) && near(w.t[1], g.t[1]) && near(w.t[2], g.t[2]) && std::fabs(std::fabs(w.q[0] * g.q[0] + w.q[1] * g.q[1] + w.q[2] * g.q[2] + w.q[3] * g.q[3]) - 1) < 1e-9, "PR2 moving gun clip: the gun body lands on the grip in the hand");
        ro::Loc a;
        ro::r2q(10, 20, 30, a.q);
        a.t[0] = 1; a.t[1] = 2; a.t[2] = 3;
        ro::Loc id = ro::cmp(a, ro::inv(a));
        check(near(id.t[0], 0) && near(id.t[1], 0) && near(id.t[2], 0) && std::fabs(std::fabs(id.q[3]) - 1) < 1e-9, "PR2 transform times its inverse is identity");
    }
    {
        std::vector<ro::Tf> r3 = pose(k), r4 = pose(k);
        r3[14].t[0] = 63; r3[14].t[1] = 7; r3[14].t[2] = 104;
        r4[14].t[0] = -35; r4[14].t[1] = 28; r4[14].t[2] = 96;
        std::map<std::string, std::map<int, ro::Loc>> caps;
        for (const char* fm : {"bo", "de"})
        {
            ro::Pick pk = ro::pick(k, fm, 2);
            const std::vector<ro::Tf>& rf = std::string(fm) == "bo" ? r3 : r4;
            std::vector<ro::Loc> c3 = ro::capture(rf.data(), (int)rf.size(), pk);
            for (size_t i = 0; i < pk.bones.size(); i++) caps[fm][pk.bones[i]] = c3[i];
        }
        auto run = [&](bool det, const std::vector<std::string>& fs) {
            std::vector<ro::Tgt> all;
            for (const auto& fq : fs)
            {
                std::vector<ro::Tgt> t = ro::targets(k, fq, &caps[fq], 2);
                if (ro::item_free(C, fq, det)) ro::drop_bone(t, k.idx("jnt_item"));
                all.insert(all.end(), t.begin(), t.end());
            }
            std::vector<ro::Tf> bb = pose(k);
            bb[12].t[0] = -40; bb[12].t[1] = 30;
            ro::apply(bb.data(), (int)bb.size(), k.par, all);
            return bb;
        };
        auto item_on = [&](const std::vector<ro::Tf>& bb, int hand) {
            const std::vector<ro::Tf>& rf = hand == 6 ? r3 : r4;
            ro::Loc a = ro::local(bb[hand], bb[14]), r = ro::local(rf[hand], rf[14]);
            return near(a.t[0], r.t[0]) && near(a.t[1], r.t[1]) && near(a.t[2], r.t[2]);
        };
        std::vector<ro::Tf> base = pose(k);
        base[12].t[0] = -40; base[12].t[1] = 30;
        check(ro::targets(k, "bo", &caps["bo"], 2).size() == 7, "DW1 bolt arm still captures jnt_item for the bolt grip offset");
        check(ro::item_free(C, "bo", true) && !ro::item_free(C, "bo", false) && !ro::item_free(C, "de", true) && !ro::item_free(C, "pt", true), "DW1 only the bolt arm with a detector frees jnt_item");
        std::vector<ro::Tf> idle = run(true, ro::fams(C, "bt", true, "idle", false, true));
        bool kept = true;
        for (int q = 0; q < 3; q++) kept = kept && near(idle[14].t[q], base[14].t[q]);
        check(kept && !item_on(idle, 6), "DW1 bolt+detector idle: jnt_item (detector) stays where the detector idle clip puts it, not in the right hand");
        std::vector<ro::Tf> walk = run(true, ro::fams(C, "bt", true, "walk", false, false));
        check(item_on(walk, 12), "DW1 bolt+detector walk: jnt_item (detector) follows the left hand");
        std::vector<ro::Tf> solo = run(false, ro::fams(C, "bt", false, "idle", false, false));
        check(item_on(solo, 6), "DW1 bolt alone: jnt_item still follows the right hand as before");
        std::vector<ro::Tf> dsolo = run(true, ro::fams(C, "", true, "walk", false, false));
        check(item_on(dsolo, 12), "DW1 detector alone walking: jnt_item follows the left hand");
        std::vector<ro::Tf> kn = run(true, ro::fams(C, "kn", true, "idle", false, true));
        bool knk = true;
        for (int q = 0; q < 3; q++) knk = knk && near(kn[14].t[q], base[14].t[q]);
        check(knk, "DW1 knife+detector idle: jnt_item untouched");
        g_bdet = 0;
        std::vector<ro::Tf> old = run(true, ro::fams(C, "bt", true, "idle", false, true));
        check(item_on(old, 6), "DW1 pabdet 0: old behaviour, jnt_item pulled to the right hand");
        g_bdet = 1;
    }
    std::printf(fails ? "ro: %d FAIL\n" : "ro: all ok\n", fails);
    return fails ? 1 : 0;
}
