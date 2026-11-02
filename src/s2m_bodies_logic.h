#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace m01::lg
{
#include "s2m_bodies_tab.inc"

    struct Ctx
    {
        double (*fl)(const char* key, double def) = nullptr;
        std::string (*fstr)(const char* key) = nullptr;
        void (*log)(const std::string& line) = nullptr;
        double f(const char* k, double d) const { return fl ? fl(k, d) : d; }
        std::string s(const char* k) const { return fstr ? fstr(k) : std::string("nil"); }
        void say(const std::string& t) const { if (log) log(t); }
    };

    inline std::string fmt(const char* f, ...)
    {
        char b[2048];
        va_list a;
        va_start(a, f);
        int n = std::vsnprintf(b, sizeof b, f, a);
        va_end(a);
        if (n < 0) return std::string();
        return std::string(b, (size_t)std::min(n, (int)sizeof b - 1));
    }

    inline std::string num(double v)
    {
        if (std::isnan(v)) return v != v && std::signbit(v) ? "-nan" : "nan";
        if (std::isinf(v)) return v > 0 ? "inf" : "-inf";
        char b[64];
        std::snprintf(b, sizeof b, "%.14g", v);
        std::string s = b;
        if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
        return s;
    }

    inline std::string inum(int64_t v) { return fmt("%lld", (long long)v); }

    inline const char* tf(bool b) { return b ? "true" : "false"; }

    inline double lmod(double a, double b) { return a - std::floor(a / b) * b; }
    inline double ang_diff(double a, double b)
    {
        double d = lmod(a - b, 360.0);
        if (d > 180) d = d - 360;
        return d;
    }
    inline double deg(double r) { return r * (180.0 / 3.14159265358979323846); }
    inline double datan2(double y, double x) { return deg(std::atan2(y, x)); }

    inline std::optional<double> tonum(const std::string& s)
    {
        size_t i = 0, n = s.size();
        while (i < n && std::isspace((unsigned char)s[i])) i++;
        if (i == n) return std::nullopt;
        const char* b = s.c_str() + i;
        char* e = nullptr;
        double v = std::strtod(b, &e);
        if (e == b) return std::nullopt;
        size_t j = (size_t)(e - s.c_str());
        while (j < n && std::isspace((unsigned char)s[j])) j++;
        if (j != n) return std::nullopt;
        return v;
    }

    inline bool pa_row(const std::string& st)
    {
        for (const auto& e : kPa) if (st == e.st) return true;
        for (const auto& e : kPaFb) if (st == e.st) return true;
        return false;
    }
    inline const PaEnt* pa_ent(const std::string& st, const char* fam)
    {
        for (const auto& e : kPa) if (st == e.st && std::strcmp(fam, e.fam) == 0) return &e;
        return nullptr;
    }
    inline const char* pa_fb(const std::string& st)
    {
        for (const auto& e : kPaFb) if (st == e.st) return e.fb;
        return nullptr;
    }
    inline const char* pa_sec(const std::string& sec)
    {
        for (const auto& e : kPaSec) if (sec == e.sec) return e.st;
        return nullptr;
    }
    inline const char* pup_anim(const std::string& st)
    {
        for (const auto& e : kPupAnim) if (st == e.st) return e.path;
        return nullptr;
    }
    inline std::optional<double> pup_nat(const std::string& st)
    {
        for (const auto& e : kPupNat) if (st == e.st) return e.nat;
        return std::nullopt;
    }
    inline std::string pa_path(const std::string& rel)
    {
        size_t k = rel.find_last_of('/');
        std::string leaf = k == std::string::npos ? rel : rel.substr(k + 1);
        if (rel.compare(0, 6, "/Game/") == 0) return rel + "." + leaf;
        return std::string(kPaDir) + rel + "." + leaf;
    }
    inline std::string leaf_of(const std::string& s, char sep)
    {
        size_t k = s.find_last_of(sep);
        return k == std::string::npos ? s : s.substr(k + 1);
    }

    inline std::string speed_state(double speed)
    {
        if (speed < 30) return "idle";
        if (speed < kWalkMax) return "walk";
        return "run";
    }

    inline std::string pa_fam(const std::string& w, bool bolt = false)
    {
        std::string c;
        bool got = false;
        for (size_t i = 0; i + 9 <= w.size() && !got; i++)
        {
            if (w[i] != '/' || (w[i + 1] != 'W' && w[i + 1] != 'w') || w.compare(i + 2, 7, "eapons/") != 0) continue;
            size_t j = i + 9, k = j;
            while (k < w.size() && std::isalpha((unsigned char)w[k])) k++;
            if (k > j && k < w.size() && w[k] == '/') { c = w.substr(j, k - j); got = true; }
        }
        if (got && c == "pt") return "pt";
        if (got && c != "knifes" && c != "grenades" && c != "items" && c != "attaches" && c != "bolt") return "ar";
        return "bh";
    }
    inline std::string pa_famc(const Ctx& C, const std::string& w)
    {
        return pa_fam(w, C.f("pabolt", 1) == 1);
    }

    inline bool pa_throwable(int64_t mainHandEquipType) { return mainHandEquipType == 5 || mainHandEquipType == 6; }

    inline std::string pa_sector(std::optional<double> ry, double vx, double vy, double speed)
    {
        if (speed < 30 || !ry) return "f";
        double d = ang_diff(datan2(vy, vx), *ry);
        double a = std::fabs(d);
        if (a <= 60) return "f";
        if (a <= 120) return d < 0 ? "l" : "r";
        if (a <= 150) return d < 0 ? "bl" : "br";
        return "b";
    }

    inline bool pa_aim(const Ctx& C, bool a, const std::string& fam, double speed) { return a || (C.f("paaimdef", 1) == 1 && fam != "bh" && speed < kWalkMax); }
    inline bool pa_aim_mv(const Ctx& C, const std::optional<std::string>& a, const std::string& fam, double speed)
    {
        if (a && *a == "2" && C.f("pahrelax", 1) == 1) return false;
        return pa_aim(C, a && *a == "1", fam, speed);
    }

    inline std::pair<std::string, std::string> pa_pick(const Ctx& C, double speed, const std::string& sec, bool c, bool a, bool f, const std::optional<std::string>& turn, bool l = false)
    {
        if (l && C.f("paladder", 1) == 1) return {"ladder", "look"};
        if (f && C.f("pafall", 1) == 1) return {"fall", "move"};
        if (speed < 30)
        {
            if (c) return {"cidle", "look"};
            if (turn) return {*turn, "look"};
            return {a ? "aidle" : "idle", "look"};
        }
        if (c && C.f("pacwalk", 0) == 1 && pa_sec(sec)) return {pa_sec(sec), "look"};
        if (c) return {"cwalk", (sec == "f" && !a) ? "move" : "look"};
        if (C.f("pabh", 0) == 1 && speed >= kWalkMax && pa_sec(sec)) return {std::string("r") + pa_sec(sec), "look"};
        if (sec == "l") return {"left", "look"};
        if (sec == "r") return {"right", "look"};
        if (sec == "bl") return {"bleft", "look"};
        if (sec == "br") return {"bright", "look"};
        if (sec == "b") return {"back", "look"};
        if (a && C.f("paaimmv", 1) == 1) return {"awalk", "look"};
        if (speed >= C.f("pupmvsprint", 520)) return {"sprint", "move"};
        if (speed >= kWalkMax) return {"run", a ? "look" : "move"};
        return {a ? "awalk" : "walk", a ? "look" : "move"};
    }

    struct ClipPick
    {
        const PaEnt* e = nullptr;
        std::string st;
    };
    inline const PaEnt kPaIdleE[] = {
        {"idle", "bh", "bh/Idles/AS_tp_bh_idle_e", 0.0, false},
        {"idle", "ar", "ar/relax/idle/add/AS_tp_ar_relax_stand_idle_e", 0.0, false},
    };
    inline const PaEnt kPaAimMv[] = {
        {"awalk", "any", "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd", 156.0, true},
    };
    inline const PaEnt kPaCWalk[] = {
        {"cwalk", "any", "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd", 156.0, true},
    };
    inline const PaEnt kPaLadder[] = {
        {"ladder", "any", "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd", 0.0, false},
        {"ladul", "any", "/Game/_STALKER2/Animations/Player/AnimSequences/ar/common/ladder/ClimbUp/AS_fp_ladder_up_left", 0.0, false},
        {"ladur", "any", "/Game/_STALKER2/Animations/Player/AnimSequences/ar/common/ladder/ClimbUp/AS_fp_ladder_up_right", 0.0, false},
        {"laddl", "any", "/Game/_STALKER2/Animations/Player/AnimSequences/ar/common/ladder/ClimbDown/AS_fp_ladder_down_left", 0.0, false},
        {"laddr", "any", "/Game/_STALKER2/Animations/Player/AnimSequences/ar/common/ladder/ClimbDown/AS_fp_ladder_down_right", 0.0, false},
    };
    inline bool pa_lad(const std::string& u) { return u == "ladder" || (u.size() == 5 && u.compare(0, 3, "lad") == 0); }
    struct LadSt
    {
        uint64_t seq = 0;
        std::optional<double> z, t, at;
        double vz = 0, ph = 0, rate = 0;
        int side = 0, dir = 1, n = 0;
    };
    inline std::string lad_step(const Ctx& C, LadSt& L, uint64_t seq, double z, double t, double now)
    {
        if (seq != L.seq)
        {
            L.seq = seq;
            double dt = (L.z && L.t) ? t - *L.t : -1;
            if (dt >= 0.03 && dt <= 1)
            {
                double v = (z - *L.z) / dt;
                double k = std::min(1.0, dt / std::max(0.01, C.f("paladtau", 0.2)));
                L.vz += (v - L.vz) * k;
            }
            if (dt < 0 || dt >= 0.03)
            {
                L.z = z;
                L.t = t;
            }
        }
        double av = std::fabs(L.vz);
        L.rate = av >= C.f("paladmin", 15) ? std::max(C.f("paladminr", 0.5), std::min(C.f("paladmaxr", 2), av / std::max(1.0, C.f("paladv", 100)))) : 0.0;
        if (L.rate > 0) L.dir = L.vz > 0 ? 1 : -1;
        if (L.at && now > *L.at) L.ph += std::min(now - *L.at, 0.25) * L.rate / std::max(0.05, C.f("paladlen", 0.5));
        L.at = now;
        if (L.ph >= 1)
        {
            L.ph -= std::floor(L.ph);
            L.side ^= 1;
            L.n++;
        }
        return std::string(L.dir > 0 ? "ladu" : "ladd") + (L.side ? "r" : "l");
    }
    inline const PaEnt kPaEquip[] = {
        {"eqar", "any", "ar/ak_74/Action/Equip/AS_tp_ar_relax_ak74_equip", 0.0, false},
        {"uqar", "any", "ar/ak_74/Action/Equip/AS_tp_ar_relax_ak74_unequip", 0.0, false},
        {"eqpt", "any", "pt/pm/Action/Equip/AS_tp_pm_equip", 0.0, false},
        {"uqpt", "any", "pt/pm/Action/Equip/AS_tp_pm_unequip", 0.0, false},
    };
    inline const PaEnt kPaDet[] = {
        {"dtidle", "bear", "bh/Contextual/stand/tp_bh_ca_detectors/bear/AS_tp_bh_ca_detector_bear_idle_a", 0.0, false},
        {"dtidle", "echo", "bh/Contextual/stand/tp_bh_ca_detectors/echo/AS_tp_bh_ca_detector_echo_idle_a", 0.0, false},
        {"dtidle", "gilka", "bh/Contextual/stand/tp_bh_ca_detectors/gilka/AS_tp_bh_ca_detector_gilka_idle_a", 0.0, false},
        {"dtidle", "veles", "bh/Contextual/stand/tp_bh_ca_detectors/veles/AS_tp_bh_ca_detector_veles_idle_a", 0.0, false},
    };
    inline constexpr const char* kDetDef = "/Game/_STALKER2/SkeletalMeshes/weapons/items/detector_echo/SK_detector_echo.SK_detector_echo";
    inline std::string det_lower(std::string s)
    {
        for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
        return s;
    }
    inline bool det_path(const std::string& p)
    {
        std::string l = det_lower(p);
        return l.find("/weapons/items/detector") != std::string::npos;
    }
    inline std::string det_model(const std::string& p)
    {
        std::string l = det_lower(p);
        size_t s = l.find_last_of('/');
        std::string leaf = s == std::string::npos ? l : l.substr(s + 1);
        size_t d = leaf.find('.');
        if (d != std::string::npos) leaf = leaf.substr(0, d);
        if (leaf.find("bear") != std::string::npos) return "bear";
        if (leaf.find("veles") != std::string::npos) return "veles";
        if (leaf.find("gilka") != std::string::npos || leaf == "sk_detector") return "gilka";
        return "echo";
    }
    inline bool w_det(std::string& x1, std::string& det)
    {
        det.clear();
        if (x1.compare(0, 3, "W1~") != 0) return false;
        size_t k = x1.find('~', 3);
        if (k == std::string::npos) return false;
        size_t d = x1.find('~', k + 1);
        if (d == std::string::npos || d + 1 >= x1.size() || x1[d + 1] != 'D') return false;
        det = x1.substr(d + 2);
        x1.erase(d);
        return true;
    }
    inline std::string det_tag(bool on, const std::string& path)
    {
        if (!on) return "";
        return "~D" + (det_path(path) ? path : std::string());
    }
    inline const PaEnt* pa_det(const Ctx& C, const std::string& use, const std::optional<std::string>& det)
    {
        if (C.f("padet", 1) != 1 || !det) return nullptr;
        if (use != "idle" && use != "aidle" && use != "turnl" && use != "turnr") return nullptr;
        std::string m = det_model(det->empty() ? std::string(kDetDef) : *det);
        for (const auto& e : kPaDet) if (m == e.fam) return &e;
        return nullptr;
    }
    inline constexpr const char* kPaOvPt = "pt/combat/Idles/AS_tp_pt_combat_idle";
    struct PaOv
    {
        std::string rel, prof, slot;
    };
    inline std::string ov_slot(const Ctx& C, const char* k, const char* d)
    {
        std::string s = C.s(k);
        return (s.empty() || s == "nil") ? std::string(d) : s;
    }
    inline std::vector<PaOv> pa_ov(const Ctx& C, const std::string& use, const std::string& fam, const std::optional<std::string>& det, bool isAc, bool detBase)
    {
        std::vector<PaOv> v;
        if (C.f("padetov", 1) != 1 || C.f("padet", 1) != 1 || !det || isAc) return v;
        if (pa_lad(use) || use == "fall" || (use.size() == 4 && (use.compare(0, 2, "eq") == 0 || use.compare(0, 2, "uq") == 0))) return v;
        if (fam == "pt") v.push_back({kPaOvPt, "right_hand_blend_mask", ov_slot(C, "padetrslot", "ContextualSlot")});
        if (!detBase)
        {
            std::string m = det_model(det->empty() ? std::string(kDetDef) : *det);
            for (const auto& e : kPaDet)
                if (m == e.fam) v.push_back({e.rel, "left_hand_blend_mask", ov_slot(C, "padetlslot", "MainActionSlot")});
        }
        return v;
    }
    inline std::optional<std::string> pa_equip(const Ctx& C, const std::string& oldFam, const std::string& newFam)
    {
        if (C.f("paequip", 1) != 1) return std::nullopt;
        if (newFam != "bh") return "eq" + newFam;
        if (oldFam != "bh") return "uq" + oldFam;
        return std::nullopt;
    }
    inline const PaEnt kPaAct[] = {
        {"acrl", "ar", "ar/ak_74/Action/Reload/AS_tp_ar_combat_ak74_reload", 0.0, false},
        {"acrl", "pt", "pt/pm/Action/Reload/AS_tp_pt_combat_pm_reload", 0.0, false},
        {"acml", "ar", "ar/Melee/Sequence/AS_tp_ar_melee_action_01", 0.0, false},
        {"acml", "any", "ar/Melee/Sequence/AS_tp_pt_melee_attack_01", 0.0, false},
        {"achl", "any", "bh/dialog/idle/stand/unique/AS_tp_quest_bh_unique_drinkpills_new", 0.0, false},
        {"achi", "any", "ar/common/knock/AS_tp_bh_ca_wounded_give_heal", 0.0, false},
        {"achf", "any", "bh/Contextual/stand/tp_bh_ca_bread/AS_tp_bh_ca_bread_action_a", 0.0, false},
        {"achd", "any", "bh/Contextual/stand/tp_bh_ca_drink_vodka/AS_tp_bh_ca_drink_vodka_action_a", 0.0, false},
        {"acia", "any", "ar/relax/weapon/Melee/AS_tp_ar_relax_ca_open_door", 0.0, false},
        {"aclt", "any", "bh/Contextual/stand/tp_bh_ca_search/AS_tp_bh_ca_search_action_a", 0.0, false},
        {"acli", "any", "bh/Contextual/stand/tp_bh_ca_search/AS_tp_bh_ca_search_in", 0.0, false},
        {"aclh", "any", "bh/Contextual/stand/tp_bh_ca_search/AS_tp_bh_ca_search_action_a", 0.0, false},
        {"aclo", "any", "bh/Contextual/stand/tp_bh_ca_search/AS_tp_bh_ca_search_out", 0.0, false},
        {"acpk", "any", "bh/Contextual/stand/tp_bh_ca_shelf_search_down/AS_tp_bh_ca_shelf_search_down_action_a", 0.0, false},
        {"acgf", "ar", "ar/combat/critical_hit/AS_tp_ar_combat_hit_critical_head_front", 0.0, false},
        {"acgb", "ar", "ar/combat/critical_hit/AS_tp_ar_combat_hit_critical_head_back", 0.0, false},
        {"acgl", "ar", "ar/combat/critical_hit/AS_tp_ar_combat_hit_critical_head_left", 0.0, false},
        {"acgr", "ar", "ar/combat/critical_hit/AS_tp_ar_combat_hit_critical_head_right", 0.0, false},
        {"acgf", "any", "pt/combat/critical_hit/AS_tp_pt_combat_hit_critical_head_front", 0.0, false},
        {"acgb", "any", "pt/combat/critical_hit/AS_tp_pt_combat_hit_critical_head_back", 0.0, false},
        {"acgl", "any", "pt/combat/critical_hit/AS_tp_pt_combat_hit_critical_head_left", 0.0, false},
        {"acgr", "any", "pt/combat/critical_hit/AS_tp_pt_combat_hit_critical_head_right", 0.0, false},
        {"acqa", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_head_front_by_ar", 0.0, false},
        {"acqb", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_head_back_by_ar", 0.0, false},
        {"acqc", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_head_left_by_ar", 0.0, false},
        {"acqd", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_head_right_by_ar", 0.0, false},
        {"acqe", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_up_front_by_ar", 0.0, false},
        {"acqf", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_up_back_by_ar", 0.0, false},
        {"acqg", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_up_left_by_ar", 0.0, false},
        {"acqh", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_up_right_by_ar", 0.0, false},
        {"acqi", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_down_front_by_ar", 0.0, false},
        {"acqj", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_down_back_by_ar", 0.0, false},
        {"acqk", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_down_left_by_ar", 0.0, false},
        {"acql", "any", "ar/combat/hit/by_ar/AS_tp_ar_combat_hit_down_right_by_ar", 0.0, false},
        {"acth", "pt", "pt/combat/grenade/AS_tp_pt_combat_grenade", 0.0, false},
        {"acth", "any", "ar/combat/grenade/AS_tp_ar_combat_grenade", 0.0, false},
        {"acvt", "any", "bossfight/Korshunov/AS_tp_ar_combat_korshunov_jump_in", 0.0, false},
    };
    inline const char* const kAcKey[][2] = {
        {"revive", "hi"}, {"reload", "rl"}, {"melee", "ml"}, {"kick", "ml"}, {"throw", "th"}, {"grenade", "th"},
        {"bandage", "hl"}, {"medkit", "hi"}, {"pills", "hl"}, {"injector", "hi"}, {"antirad", "hl"}, {"hercules", "hl"}, {"cinnamon", "hl"}, {"psyblocker", "hl"},
        {"food", "hf"}, {"bread", "hf"}, {"sausage", "hf"}, {"canned", "hf"}, {"drink", "hd"}, {"vodka", "hd"}, {"water", "hd"}, {"beer", "hd"}, {"milk", "hd"}, {"energ", "hd"},
        {"loot", "lt"}, {"stash", "lt"}, {"dead_body", "lt"}, {"pickup", "pk"},
        {"lever", "ia"}, {"button", "ia"}, {"valve", "ia"}, {"hatch", "ia"}, {"door", "ia"}, {"interact", "ia"}, {"cardlock", "ia"},
    };
    inline const char* ac_flag(const std::string& id)
    {
        if (id == "rl") return "parl";
        if (id == "ml") return "paml";
        if (id == "th") return "pathr";
        if (id == "hl" || id == "hi" || id == "hf" || id == "hd") return "pahl";
        if (id == "ia" || id == "lt" || id == "pk" || id == "lh" || id == "li" || id == "lo") return "paia";
        if (id == "gf" || id == "gb" || id == "gl" || id == "gr") return "pahit";
        if (id.size() == 2 && id[0] == 'q' && id[1] >= 'a' && id[1] <= 'l') return "pahit";
        if (id == "vt") return "pavt";
        return nullptr;
    }
    inline bool ac_on(const Ctx& C, const std::string& id)
    {
        const char* f = ac_flag(id);
        return f && C.f(f, 1) == 1;
    }
    inline std::optional<std::string> ac_of(const Ctx& C, const std::string& mont)
    {
        std::string s = mont;
        for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
        for (const auto& k : kAcKey)
            if (s.find(k[0]) != std::string::npos) return ac_on(C, k[1]) ? std::optional<std::string>(k[1]) : std::nullopt;
        return std::nullopt;
    }
    inline const PaEnt* ac_ent(const std::string& st, const std::string& fam)
    {
        for (const auto& e : kPaAct) if (st == e.st && fam == e.fam) return &e;
        for (const auto& e : kPaAct) if (st == e.st && std::strcmp(e.fam, "any") == 0) return &e;
        return nullptr;
    }
    struct AcTx
    {
        std::string last;
        int seq = 0;
        std::optional<std::string> id;
        double at = -100;
        double ixAt = -100;
        std::string thName;
        bool thOpen = false;
        bool thFree = false;
        double thLast = -100;
        bool vlast = false;
        bool lhOn = false, lhSeen = false, lhInit = false;
        double lhX = 0, lhY = 0, lhHp = -1, lhEnd = -100;
        std::string lhName;
    };
    inline bool ac_world(const std::string& id) { return id == "ia" || id == "lt" || id == "pk"; }
    inline bool ac_th_free(const Ctx& C, const AcTx& T, double now)
    {
        return T.id && *T.id == "th" && T.thFree && now - T.at < C.f("thopen", 8);
    }
    inline double ac_th_at(const Ctx& C, const AcTx& T, double now)
    {
        return ac_th_free(C, T, now) ? now : T.thLast;
    }
    inline bool ac_feed(const Ctx& C, AcTx& T, const std::string& key, const std::string& name, double now)
    {
        if (key == T.last) return false;
        T.last = key;
        if (key.empty()) return false;
        auto id = ac_of(C, name);
        if (!id) return false;
        if (ac_world(*id) && now - T.ixAt < C.f("pacwin", 2)) return false;
        if (*id == "th")
        {
            bool prevTh = T.id && *T.id == "th";
            if (prevTh && T.thOpen && name != T.thName && (now - T.at < C.f("pathgap", 2.5) || ac_th_free(C, T, now)))
            {
                T.thOpen = false;
                T.thLast = now;
                return false;
            }
            if (prevTh && now - T.thLast < C.f("thgap", 1.0)) return false;
            T.thName = name;
            T.thOpen = true;
            T.thFree = true;
            T.thLast = now;
        }
        T.seq = T.seq % 999 + 1;
        T.id = id;
        T.at = now;
        return true;
    }
    inline bool ac_force(const Ctx& C, AcTx& T, const std::string& id, double now, double gap)
    {
        if (!ac_on(C, id)) return false;
        if (id == "th" && ac_th_free(C, T, now))
        {
            T.thFree = false;
            T.thLast = now;
            return false;
        }
        double ref = id == "th" && T.thLast > T.at ? T.thLast : T.at;
        if (T.id && *T.id == id && now - ref < gap) return false;
        if (id == "th")
        {
            T.thName.clear();
            T.thOpen = false;
            T.thFree = false;
            T.thLast = now;
        }
        T.seq = T.seq % 999 + 1;
        T.id = id;
        T.at = now;
        return true;
    }
    inline std::optional<std::string> ix_id(const Ctx& C, const std::string& actor)
    {
        auto id = ac_of(C, actor);
        if (id && ac_world(*id)) return id;
        std::string s = actor;
        for (char& ch : s) ch = (char)std::tolower((unsigned char)ch);
        return ac_of(C, s.find("item") != std::string::npos ? "pickup" : "interact");
    }
    inline std::string ix_body(const Ctx& C, const std::string& name, bool obj, bool alive)
    {
        if (name.empty() || !obj || alive || C.f("palt", 1) != 1) return name;
        return "dead_body " + name;
    }
    inline bool lh_want(const Ctx& C, const std::string& actor)
    {
        return C.f("palthold", 1) == 1 && actor.rfind("dead_body ", 0) != 0;
    }
    inline bool ac_ix(const Ctx& C, AcTx& T, const std::string& actor, double now)
    {
        if (C.f("paix", 1) != 1 || actor.empty() || now - T.ixAt < 0.5) return false;
        auto id = ix_id(C, actor);
        if (!id) return false;
        bool hold = *id == "lt" && lh_want(C, actor);
        if (hold && actor == T.lhName && ((T.lhOn && T.id && *T.id == "lh") || now - T.lhEnd < C.f("palhgap", 1))) return false;
        if (hold)
        {
            id = std::string("lh");
            T.lhSeen = false;
            T.lhInit = false;
            T.lhName = actor;
        }
        T.lhOn = hold;
        T.seq = T.seq % 999 + 1;
        T.id = id;
        T.at = now;
        T.ixAt = now;
        return true;
    }
    struct HitSt
    {
        double hp = -1;
        double at = -100;
    };
    inline const char* hit_side(double rel)
    {
        double a = std::fabs(rel);
        if (a <= 45) return "f";
        if (a >= 135) return "b";
        return rel > 0 ? "r" : "l";
    }
    inline std::optional<double> hit_enum_deg(int e)
    {
        static const double k[] = {0, 180, -90, 90, -45, 45, -135, 135};
        if (e < 1 || e > 8) return std::nullopt;
        return k[e - 1];
    }
    inline const char* hit_enum(int e)
    {
        auto d = hit_enum_deg(e);
        return d ? hit_side(*d) : nullptr;
    }
    inline const char* hit_partner(double rel, double bodyYaw, double partnerYaw)
    {
        return hit_side(ang_diff(bodyYaw + rel, partnerYaw));
    }
    inline std::string hit_light_id(int zone, const char* side)
    {
        int z = zone <= 0 ? 0 : (zone <= 2 ? 1 : 2);
        int d = side[0] == 'f' ? 0 : (side[0] == 'b' ? 1 : (side[0] == 'l' ? 2 : 3));
        return std::string("q") + (char)('a' + z * 4 + d);
    }
    inline bool hit_big(const Ctx& C, double amount)
    {
        return C.f("pahitlt", 1) != 1 || amount >= C.f("pahitbig", 30);
    }
    inline bool hit_gate(const Ctx& C, double& at, double amount, double now, double cd = -1)
    {
        if (cd < 0) cd = C.f("pahitcd", 2);
        if (C.f("pahit", 1) != 1 || amount < C.f("pahitmin", 2) || now - at < cd) return false;
        at = now;
        return true;
    }
    inline bool hit_feed(const Ctx& C, HitSt& H, double hp, double now)
    {
        double prev = H.hp;
        H.hp = hp;
        if (prev < 0 || hp < 0) return false;
        return hit_gate(C, H.at, prev - hp, now);
    }
    inline bool ac_hit(const Ctx& C, AcTx& T, const char* side, double now)
    {
        if (!side || C.f("pahit", 1) != 1) return false;
        T.seq = T.seq % 999 + 1;
        T.id = std::string("g") + side;
        T.at = now;
        return true;
    }
    inline bool ac_vault(const Ctx& C, AcTx& T, bool v, double now)
    {
        bool rise = v && !T.vlast;
        T.vlast = v;
        if (!rise || !ac_on(C, "vt")) return false;
        T.seq = T.seq % 999 + 1;
        T.id = std::string("vt");
        T.at = now;
        return true;
    }
    struct VzScan
    {
        bool ok = false;
        double d0 = 0, d1 = 0, top = 0;
    };
    inline VzScan vz_scan(const std::vector<std::pair<double, std::optional<double>>>& pts, double feet, double up, double minh)
    {
        VzScan S;
        for (const auto& p : pts)
        {
            double h = p.second ? *p.second - feet : -1e9;
            bool obst = p.second && h >= minh && h <= up - 5;
            if (!obst)
            {
                if (S.ok) break;
                continue;
            }
            if (!S.ok) { S.ok = true; S.d0 = p.first; S.top = *p.second; }
            S.d1 = p.first;
            S.top = std::max(S.top, *p.second);
        }
        return S;
    }
    inline double vz_arc(double s, double d0, double d1, double reach, double ramp)
    {
        double a = d0 - reach, b = d1 + reach;
        if (s >= a && s <= b) return 1;
        if (ramp <= 0) return 0;
        double u = s < a ? (s - (a - ramp)) / ramp : ((b + ramp) - s) / ramp;
        u = std::max(0.0, std::min(1.0, u));
        return u * u * (3 - 2 * u);
    }
    inline double vz_lift(const VzScan& S, double s, double feet, double reach, double ramp, double margin)
    {
        if (!S.ok) return 0;
        return vz_arc(s, S.d0, S.d1, reach, ramp) * std::max(0.0, S.top + margin - feet);
    }
    inline double vz_step(double cur, double want, double dt, double rate)
    {
        double m = rate * std::max(0.0, std::min(dt, 0.25));
        if (want > cur) return std::min(want, cur + m);
        return std::max(want, cur - m);
    }
    struct VzRange
    {
        bool on = false;
        double z0 = 0, lo = 0, hi = 0, t0 = 0;
    };
    inline bool vz_track(VzRange& R, bool v, double z, double now)
    {
        if (v)
        {
            if (!R.on) { R.on = true; R.z0 = R.lo = R.hi = z; R.t0 = now; }
            else { R.lo = std::min(R.lo, z); R.hi = std::max(R.hi, z); }
            return false;
        }
        bool end = R.on;
        R.on = false;
        return end;
    }
    inline const char* lh_tick(const Ctx& C, AcTx& T, bool open, bool hasPos, double x, double y, double hp, double now)
    {
        if (!T.lhOn) return nullptr;
        const char* why = nullptr;
        if (!T.id || *T.id != "lh") why = "outra acao";
        else
        {
            if (!T.lhInit)
            {
                T.lhInit = true;
                T.lhX = x;
                T.lhY = y;
                T.lhHp = hp;
            }
            if (open) T.lhSeen = true;
            if (hp >= 0 && T.lhHp >= 0 && hp > T.lhHp) T.lhHp = hp;
            if (T.lhSeen && !open) why = "janela fechada";
            else if (!T.lhSeen && now - T.at >= C.f("palhwait", 4)) why = "janela nao abriu";
            else if (hasPos && std::hypot(x - T.lhX, y - T.lhY) > C.f("palhwalk", 150)) why = "andou para longe";
            else if (hp >= 0 && T.lhHp >= 0 && T.lhHp - hp >= C.f("pahitmin", 2)) why = "levou dano";
            else if (now - T.at >= C.f("palhmax", 300)) why = "limite de tempo";
        }
        if (why)
        {
            T.lhOn = false;
            T.lhEnd = now;
        }
        return why;
    }
    inline std::string ac_token(const Ctx& C, const AcTx& T, double now)
    {
        if (T.id && *T.id == "lh") return T.lhOn && ac_on(C, "lh") ? *T.id + inum(T.seq) : std::string();
        if (!T.id || now - T.at >= C.f("pacwin", 2) || !ac_on(C, *T.id)) return std::string();
        return *T.id + inum(T.seq);
    }
    inline bool ac_tok_ok(const std::string& t)
    {
        if (t.size() < 3 || t.size() > 5 || !std::islower((unsigned char)t[0]) || !std::islower((unsigned char)t[1])) return false;
        for (size_t i = 2; i < t.size(); i++) if (t[i] < '0' || t[i] > '9') return false;
        return true;
    }
    struct AcRx
    {
        std::optional<std::string> last;
    };
    inline std::optional<std::string> ac_rx(const Ctx& C, AcRx& R, const std::optional<std::string>& tok)
    {
        if (!tok || !ac_tok_ok(*tok)) return std::nullopt;
        if (R.last && *R.last == *tok) return std::nullopt;
        R.last = tok;
        std::string id = tok->substr(0, 2);
        if (!ac_on(C, id)) return std::nullopt;
        return id;
    }
    inline std::optional<std::string> hit_local(const Ctx& C, std::optional<std::string>& tok)
    {
        std::optional<std::string> t = tok;
        tok.reset();
        if (!t || !ac_tok_ok(*t) || ((*t)[0] != 'g' && (*t)[0] != 'q')) return std::nullopt;
        std::string id = t->substr(0, 2);
        if (!ac_on(C, id)) return std::nullopt;
        return id;
    }
    struct LhRx
    {
        int ph = 0;
        std::string tok;
        double t0 = 0, until = 0;
    };
    inline const char* lh_st(int ph)
    {
        return ph == 1 ? "acli" : ph == 2 ? "aclh" : ph == 3 ? "aclo" : nullptr;
    }
    inline bool lh_is(const std::string& st) { return st == "acli" || st == "aclh" || st == "aclo"; }
    inline bool ac_loop(const std::string& st) { return st == "aclh"; }
    inline double lh_len(const Ctx& C, double len)
    {
        double bo = C.f("pablendout", 0.25);
        return C.f("pablendearly", 1) == 1 && len > 3.0 * bo ? len - bo : len;
    }
    inline void lh_go(LhRx& R, const std::string& tok, double now, double inLen)
    {
        R.ph = 1;
        R.tok = tok;
        R.t0 = now;
        R.until = now + inLen;
    }
    inline const char* lh_rx(const Ctx& C, LhRx& R, const std::optional<std::string>& cur, double age, double now, double outLen)
    {
        if (!R.ph) return nullptr;
        if (R.ph == 3)
        {
            if (now < R.until) return nullptr;
            R.ph = 0;
            return "saida terminou";
        }
        bool stale = age >= C.f("palhstale", 2);
        bool held = cur && *cur == R.tok && !stale;
        if (!held || now - R.t0 >= C.f("palhmax", 300))
        {
            R.ph = 3;
            R.until = now + outLen;
            return stale ? "parceiro sem dados" : !held ? "janela fechada" : "limite de tempo";
        }
        if (R.ph == 1 && now >= R.until)
        {
            R.ph = 2;
            return "laco";
        }
        return nullptr;
    }
    inline bool pa_ac_live(const Ctx& C, const std::string& st, const std::string& fam, const std::string& base)
    {
        if (st.size() != 4 || base == "ladder" || (base == "fall" && st != "acvt")) return false;
        return ac_on(C, st.substr(2)) && ac_ent(st, fam);
    }
    inline std::string pa_use(const std::string& base, const std::optional<std::string>& eq, double eqUntil, const std::optional<std::string>& ac, double acUntil, double now)
    {
        if (ac && now < acUntil && base != "ladder" && (base != "fall" || *ac == "acvt")) return *ac;
        if (eq && now < eqUntil) return *eq;
        return base;
    }
    inline std::optional<ClipPick> pa_clip(const Ctx& C, std::string st, const std::string& fam, const std::function<bool(const PaEnt&)>& load)
    {
        if (st.size() == 4 && st.compare(0, 2, "ac") == 0)
        {
            const PaEnt* e = ac_on(C, st.substr(2)) ? ac_ent(st, fam) : nullptr;
            if (e && load(*e)) return ClipPick{e, st};
            return std::nullopt;
        }
        if (C.f("paladder", 1) == 1)
            for (const auto& s : kPaLadder)
                if (st == s.st && load(s)) return ClipPick{&s, st};
        if (C.f("paequip", 1) == 1)
            for (const auto& s : kPaEquip)
                if (st == s.st && load(s)) return ClipPick{&s, st};
        for (int i = 0; i < 5; i++)
        {
            if (!pa_row(st)) return std::nullopt;
            if (C.f("pacwalk", 0) == 1)
                for (const auto& s : kPaCWalk)
                    if (st == s.st && load(s)) return ClipPick{&s, st};
            if (C.f("paidlee", 0) == 1)
                for (const auto& s : kPaIdleE)
                    if (st == s.st && fam == s.fam && load(s)) return ClipPick{&s, st};
            if (C.f("paaimmv", 1) == 1)
                for (const auto& s : kPaAimMv)
                    if (st == s.st && load(s)) return ClipPick{&s, st};
            const PaEnt* e = nullptr;
            if (fam == "bh" && C.f("pabh", 0) == 1) e = pa_ent(st, "bhn");
            if (!e) e = pa_ent(st, fam.c_str());
            if (!e && fam == "pt" && C.f("papt", 1) == 1) e = pa_ent(st, "bhn");
            if (!e) e = pa_ent(st, "any");
            if (e && load(*e)) return ClipPick{e, st};
            const char* fb = pa_fb(st);
            if (!fb) return std::nullopt;
            st = fb;
        }
        return std::nullopt;
    }

    struct Mv
    {
        std::optional<std::string> c, a, f, l, act;
    };
    inline Mv mv_parse(const std::string& s)
    {
        Mv m;
        auto dig = [](char ch) { return ch >= '0' && ch <= '9'; };
        if (s.size() < 3 || !dig(s[0]) || s[1] != ',' || !dig(s[2])) return m;
        m.c = s.substr(0, 1);
        m.a = s.substr(2, 1);
        size_t i = 3;
        if (i < s.size() && s[i] == ',') i++;
        if (i < s.size() && dig(s[i])) m.f = s.substr(i, 1);
        else m.f = std::string();
        if (i + 2 < s.size() && dig(s[i]) && s[i + 1] == ',' && dig(s[i + 2])) m.l = s.substr(i + 2, 1);
        if (m.l && i + 4 < s.size() && s[i + 3] == ',')
        {
            std::string t = s.substr(i + 4);
            if (ac_tok_ok(t)) m.act = t;
        }
        return m;
    }
    inline int mv_lean(const Mv& m)
    {
        if (!m.l) return 0;
        if (*m.l == "2") return -1;
        if (*m.l == "3") return 1;
        return 0;
    }
    inline int mv_lcode(bool ladder, int lean)
    {
        if (ladder) return 1;
        if (lean < 0) return 2;
        if (lean > 0) return 3;
        return 0;
    }
    struct LRot
    {
        double p = 0, y = 0, r = 0;
    };
    inline void rot_axes(const LRot& R, double X[3], double Y[3], double Z[3])
    {
        const double k = 3.14159265358979323846 / 180.0;
        double sp = std::sin(R.p * k), cp = std::cos(R.p * k), sy = std::sin(R.y * k), cy = std::cos(R.y * k), sr = std::sin(R.r * k), cr = std::cos(R.r * k);
        X[0] = cp * cy; X[1] = cp * sy; X[2] = sp;
        Y[0] = sr * sp * cy - cr * sy; Y[1] = sr * sp * sy + cr * cy; Y[2] = -sr * cp;
        Z[0] = -(cr * sp * cy + sr * sy); Z[1] = cy * sr - cr * sp * sy; Z[2] = cr * cp;
    }
    inline LRot rot_of(const double X[3], const double Y[3], const double Z[3])
    {
        const double k = 180.0 / 3.14159265358979323846;
        LRot R;
        R.p = std::atan2(X[2], std::sqrt(X[0] * X[0] + X[1] * X[1])) * k;
        R.y = std::atan2(X[1], X[0]) * k;
        double sx[3], sy[3], sz[3];
        rot_axes(LRot{R.p, R.y, 0}, sx, sy, sz);
        R.r = std::atan2(Z[0] * sy[0] + Z[1] * sy[1] + Z[2] * sy[2], Y[0] * sy[0] + Y[1] * sy[1] + Y[2] * sy[2]) * k;
        return R;
    }
    inline LRot lean_rot(const LRot& base, double deg)
    {
        double X[3], Y[3], Z[3];
        rot_axes(base, X, Y, Z);
        double a = deg * 3.14159265358979323846 / 180.0, s = std::sin(a), c = std::cos(a);
        double* v[3] = {X, Y, Z};
        for (double* e : v)
        {
            double y = e[1] * c + e[2] * s, z = -e[1] * s + e[2] * c;
            e[1] = y;
            e[2] = z;
        }
        return rot_of(X, Y, Z);
    }
    inline double lean_step(double cur, double tgt, double dt, double rate)
    {
        double d = tgt - cur, m = std::max(0.0, rate * dt);
        if (std::fabs(d) <= m) return tgt;
        return cur + (d > 0 ? m : -m);
    }

    struct LnTf
    {
        double q[4];
        double t[4];
        double s[4];
    };
    static_assert(sizeof(LnTf) == 0x60, "FTransform double layout");
    struct LnStage
    {
        int pivot = -1;
        std::vector<int> bones;
    };
    inline void ln_axis(const LRot& base, double ax[3])
    {
        double X[3], Y[3], Z[3];
        rot_axes(base, X, Y, Z);
        ax[0] = X[0];
        ax[1] = Y[0];
        ax[2] = Z[0];
    }
    inline void ln_qmul(const double a[4], const double b[4], double o[4])
    {
        double x = a[3] * b[0] + a[0] * b[3] + a[1] * b[2] - a[2] * b[1];
        double y = a[3] * b[1] - a[0] * b[2] + a[1] * b[3] + a[2] * b[0];
        double z = a[3] * b[2] + a[0] * b[1] - a[1] * b[0] + a[2] * b[3];
        double w = a[3] * b[3] - a[0] * b[0] - a[1] * b[1] - a[2] * b[2];
        o[0] = x; o[1] = y; o[2] = z; o[3] = w;
    }
    inline void ln_qrot(const double q[4], const double v[3], double o[3])
    {
        double tx = 2 * (q[1] * v[2] - q[2] * v[1]), ty = 2 * (q[2] * v[0] - q[0] * v[2]), tz = 2 * (q[0] * v[1] - q[1] * v[0]);
        o[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
        o[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
        o[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
    }
    inline std::vector<LnStage> ln_stages(const std::vector<int>& parent, const std::vector<std::string>& names)
    {
        int n = (int)parent.size();
        auto idx = [&](const char* s) {
            for (int i = 0; i < n && i < (int)names.size(); i++)
                if (names[i] == s) return i;
            return -1;
        };
        int root = idx("jnt_root"), hips = idx("jnt_hips"), foot = idx("jnt_ik_foot_root");
        const char* sp[3] = {"jnt_spine_01", "jnt_spine_02", "jnt_spine_03"};
        std::vector<LnStage> out;
        if (root < 0 || hips < 0) return out;
        auto under = [&](int i, int a) {
            for (int k = 0; i >= 0 && k <= n; k++, i = parent[i])
                if (i == a) return true;
            return false;
        };
        std::vector<int> extra;
        for (int i = 0; i < n; i++)
        {
            if (i == root || under(i, hips) || (foot >= 0 && under(i, foot))) continue;
            if (under(i, root)) extra.push_back(i);
        }
        for (const char* s : sp)
        {
            int p = idx(s);
            if (p < 0 || !under(p, hips)) return {};
            LnStage st;
            st.pivot = p;
            for (int i = 0; i < n; i++)
                if (under(i, p)) st.bones.push_back(i);
            st.bones.insert(st.bones.end(), extra.begin(), extra.end());
            out.push_back(std::move(st));
        }
        return out;
    }
    inline void ln_apply(LnTf* b, int n, const std::vector<LnStage>& st, const double ax[3], double deg)
    {
        if (st.empty() || n <= 0) return;
        double a = -deg * 3.14159265358979323846 / 180.0 / (double)st.size();
        double s = std::sin(a / 2), c = std::cos(a / 2);
        double q[4] = {ax[0] * s, ax[1] * s, ax[2] * s, c};
        for (const LnStage& g : st)
        {
            if (g.pivot < 0 || g.pivot >= n) continue;
            double P[3] = {b[g.pivot].t[0], b[g.pivot].t[1], b[g.pivot].t[2]};
            for (int i : g.bones)
            {
                if (i < 0 || i >= n) continue;
                LnTf& x = b[i];
                double d[3] = {x.t[0] - P[0], x.t[1] - P[1], x.t[2] - P[2]}, r[3];
                ln_qrot(q, d, r);
                x.t[0] = P[0] + r[0];
                x.t[1] = P[1] + r[1];
                x.t[2] = P[2] + r[2];
                double o[4];
                ln_qmul(q, x.q, o);
                double m = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2] + o[3] * o[3]);
                if (m > 0)
                    for (int k = 0; k < 4; k++) x.q[k] = o[k] / m;
            }
        }
    }

    struct PaState
    {
        std::optional<double> ry, rt, recv;
        std::optional<std::string> turn;
        std::optional<double> turn_until;
        std::optional<std::string> cand;
        int cn = 0;
        std::optional<std::string> st, face;
    };
    inline void pa_turn_feed(const Ctx& C, PaState& Q, std::optional<double> ry, std::optional<double> rt, double recv, double now)
    {
        if (!ry || (Q.recv && *Q.recv == recv)) return;
        double t = rt ? *rt : recv;
        if (Q.ry && Q.rt && t > *Q.rt)
        {
            double w = ang_diff(*ry, *Q.ry) / std::max(0.05, t - *Q.rt);
            if (std::fabs(w) >= C.f("patrn", 45))
            {
                Q.turn = w > 0 ? "turnr" : "turnl";
                Q.turn_until = now + 0.4;
            }
        }
        Q.ry = ry;
        Q.rt = t;
        Q.recv = recv;
    }
    inline std::optional<std::string> pa_turn_now(const PaState& Q, double now)
    {
        if (Q.turn_until && now < *Q.turn_until) return Q.turn;
        return std::nullopt;
    }
    inline void pa_hold(const Ctx& C, PaState& Q, const std::string& st, const std::string& face)
    {
        std::string key = st + "|" + face;
        if (Q.cand && *Q.cand == key) Q.cn = Q.cn + 1;
        else { Q.cand = key; Q.cn = 1; }
        if (!Q.st || Q.cn >= C.f("pahold", 2)) { Q.st = st; Q.face = face; }
    }
    struct YawHold
    {
        std::optional<double> held;
        int dir = 0;
        double until = 0;
    };
    inline bool pa_still(const std::optional<std::string>& st)
    {
        return st && (*st == "idle" || *st == "cidle" || *st == "turnl" || *st == "turnr");
    }
    inline std::optional<double> yaw_hold(const Ctx& C, YawHold& H, bool still, std::optional<double> want, std::optional<double> cur, double now)
    {
        if (C.f("pupyawhold", 1) != 1 || !still || !want)
        {
            H.held.reset();
            H.dir = 0;
            return want;
        }
        if (H.dir != 0)
        {
            H.held = *want;
            if (now >= H.until && (!cur || std::fabs(ang_diff(*want, *cur)) <= C.f("pupyawdone", 5))) H.dir = 0;
            return want;
        }
        if (!H.held) H.held = cur ? *cur : *want;
        double d = ang_diff(*want, *H.held);
        if (std::fabs(d) >= C.f("pupyawmax", 45))
        {
            H.dir = d > 0 ? 1 : -1;
            H.until = now + C.f("pupyawt", 0.4);
            H.held = *want;
            return want;
        }
        return H.held;
    }
    inline std::optional<std::string> yaw_hold_turn(const Ctx& C, const YawHold& H, const std::optional<std::string>& feed)
    {
        if (C.f("pupyawhold", 1) != 1) return feed;
        if (H.dir > 0) return std::string("turnr");
        if (H.dir < 0) return std::string("turnl");
        return std::nullopt;
    }
    inline std::string pa_pistol_run(const Ctx& C, const std::string& st, const std::string& sec, double speed, const std::string& fam)
    {
        const char* ps = pa_sec(sec);
        if (C.f("papt", 1) == 1 && C.f("pabh", 0) != 1 && speed >= kWalkMax && ps && st == ps && fam == "pt") return "r" + st;
        return st;
    }
    inline double pa_rate(const Ctx& C, const PaEnt& e, double speed)
    {
        double rate = 1.0;
        if (e.has_nat && speed >= 30) rate = std::max(0.6, std::min(C.f("pamaxr", 2.5), speed / e.nat));
        return rate;
    }

    struct PfState
    {
        int n = 0;
        std::optional<double> hh, own;
    };
    inline double pf_z(const Ctx& C, double z, const std::optional<std::string>& fh, std::optional<double> own_read, std::optional<double>& pfH, PfState& F)
    {
        if (C.f("pupfeet", 0) != 1) return z;
        auto hh = tonum(fh ? *fh : std::string());
        if (!hh || *hh < 10 || *hh > 200) return z;
        std::optional<double> own = own_read;
        if (own && *own >= 10 && *own <= 200) pfH = own;
        else own = pfH;
        if (!own) return z;
        if (!F.hh || std::fabs(*F.hh - *hh) > 1 || std::fabs(*F.own - *own) > 1)
        {
            F.n = F.n + 1;
            F.hh = hh;
            F.own = own;
            if (F.n <= 30 || F.n % 50 == 0)
                C.say(fmt("NOLA pupfeet: remetente meia_altura=%.1f boneco meia_altura=%.1f -> z do boneco %+.1f cm vs centro (vez %d)", *hh, *own, *own - *hh, F.n));
        }
        return z - *hh + *own;
    }

    inline std::optional<int> tgt_mode(const Ctx& C, std::optional<int> mm, bool forced)
    {
        if (!mm || C.f("tgtnone", 1) == 1) return std::nullopt;
        if (C.f("tgtwalk", 1) == 1) return *mm == 2 ? std::optional<int>(1) : std::nullopt;
        return forced && *mm == 1 ? std::optional<int>(2) : std::nullopt;
    }

    inline std::optional<bool> tgt_cmc(const Ctx& C, std::optional<bool> active, bool ours)
    {
        if (!active) return std::nullopt;
        if (C.f("tgtnone", 1) == 1) return *active ? std::optional<bool>(false) : std::nullopt;
        return !*active && ours ? std::optional<bool>(true) : std::nullopt;
    }

    struct RSnap
    {
        uint64_t seq = 0;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0;
        std::optional<double> yaw;
        std::optional<double> t;
        double recv = 0;
    };

    struct RVel
    {
        bool init = false;
        int der = 0, sent = 0;
        double at = 0;
        std::optional<uint64_t> r;
        std::optional<double> t, x, y, vx, vy;
    };
    inline std::pair<double, double> r_vel(const Ctx& C, RVel& D, const RSnap& r, double now)
    {
        double sx = r.vx, sy = r.vy;
        if (!D.init) { D = RVel{}; D.init = true; D.at = now; }
        double t = r.t ? *r.t : r.recv;
        if (!D.r || *D.r != r.seq)
        {
            double vx = 0, vy = 0;
            if (D.t && t > *D.t)
            {
                double dt = t - *D.t;
                if (dt >= 0.03 && dt <= 1)
                {
                    vx = (r.x - *D.x) / dt;
                    vy = (r.y - *D.y) / dt;
                    double mx = C.f("nsvmax", 1500);
                    if (vx * vx + vy * vy > mx * mx) { vx = 0; vy = 0; }
                }
            }
            if (!(D.t && t <= *D.t)) { D.vx = vx; D.vy = vy; }
            D.r = r.seq;
            D.t = t;
            D.x = r.x;
            D.y = r.y;
            if (sx * sx + sy * sy < 25 && vx * vx + vy * vy >= 900) D.der = D.der + 1;
            else if (sx * sx + sy * sy >= 25) D.sent = D.sent + 1;
            if (now - D.at >= 10)
            {
                if (D.der > 0) C.say(fmt("NOLA rvel 10 s: velocidade enviada ~0 com o remoto andando em %d snapshots (rvder=%s; enviada ok em %d)", D.der, C.s("rvder").c_str(), D.sent));
                D.der = 0;
                D.sent = 0;
                D.at = now;
            }
        }
        if (C.f("rvder", 0) != 1 || sx * sx + sy * sy >= 25) return {sx, sy};
        double dx = D.vx.value_or(0), dy = D.vy.value_or(0);
        if (dx * dx + dy * dy >= 900) return {dx, dy};
        return {sx, sy};
    }

    struct HPt
    {
        double t, x, y, z;
    };
    struct Mover
    {
        bool has = false;
        double x = 0, y = 0, z = 0, vx = 0, vy = 0, vz = 0, st = 0;
        std::optional<double> ts, yaw;
        std::vector<HPt> h;
        std::optional<double> hAt, off, T, dx, dy, mvAt, at, nbvx, nbvy, rt;
        bool stopped = false;
        bool drive = false;
        bool fr = false;
        bool av = false;
        double jit = 0, svx = 0, svy = 0, svz = 0;
    };

    struct NB
    {
        bool init = false;
        double at = 0, lag = 0;
        int ip = 0, fim = 0, cut = 0, slow = 0;
    };
    inline NB& nb_st(const Ctx& C, NB& B, double now)
    {
        if (!B.init) { B = NB{}; B.init = true; B.at = now; }
        if (now - B.at >= 10)
        {
            if (B.ip > 0 || B.cut > 0 || B.slow > 0)
                C.say(fmt("NOLA nsip 10 s: passos_interpolados=%d fim_do_caminho=%d atraso_med=%.0f ms recuos_cortados=%d recuos_lentos=%d (nsip=%s nsnoback=%s nscap=%s)",
                          B.ip, B.fim, B.lag / std::max(1, B.ip) * 1000, B.cut, B.slow, C.s("nsip").c_str(), C.s("nsnoback").c_str(), C.s("nscap").c_str()));
            B.at = now;
            B.ip = 0;
            B.fim = 0;
            B.cut = 0;
            B.slow = 0;
            B.lag = 0;
        }
        return B;
    }

    inline void ns_hist(Mover& K, double x, double y, double z, std::optional<double> ts, double now, bool fresh, double xcap = 0)
    {
        K.fr = fresh;
        double t = ts ? *ts : now;
        auto& h = K.h;
        bool hasL = !h.empty();
        HPt L = hasL ? h.back() : HPt{0, 0, 0, 0};
        bool moved = !hasL || (x - L.x) * (x - L.x) + (y - L.y) * (y - L.y) + (z - L.z) * (z - L.z) >= 0.25;
        if (!moved && !(fresh && now - K.hAt.value_or(-10) >= 0.2)) return;
        if (hasL && t < L.t - 1) { h.clear(); K.off.reset(); hasL = false; }
        if (hasL && t <= L.t)
        {
            if (t == L.t && moved) { h.back().x = x; h.back().y = y; h.back().z = z; }
            return;
        }
        double o = now - t;
        if (!K.off || o < *K.off) K.off = o;
        else K.off = *K.off + (o - *K.off) * 0.02;
        {
            double j = std::max(0.0, o - *K.off);
            K.jit = j > K.jit ? K.jit + (j - K.jit) * 0.5 : K.jit * 0.97;
        }
        if (hasL)
        {
            double g = t - L.t, T = K.T.value_or(0.1);
            if (moved && g > std::max(2.5 * T, 0.4) && !(fresh && xcap > 0)) h.push_back({t - T, L.x, L.y, L.z});
            else if (g > 0.03 && g < 0.6) K.T = T * 0.7 + g * 0.3;
            if (moved) { K.dx = x - L.x; K.dy = y - L.y; K.mvAt = now; }
        }
        else K.mvAt = now;
        h.push_back({t, x, y, z});
        K.hAt = now;
        while (h.size() > 5) h.erase(h.begin());
    }

    struct PathPt
    {
        double x, y, z, g, dx, dy;
    };
    inline std::optional<PathPt> ns_path(const Ctx& C, NB& B, Mover& K, double now)
    {
        auto& h = K.h;
        size_t n = h.size();
        if (n == 0 || !K.off) return std::nullopt;
        double D = std::min(std::max(K.T.value_or(0.1), 0.05), C.f("nsipmax", 0.5)) + C.f("nsippad", 0.05);
        double xtra = 0;
        if (K.av)
        {
            D += std::min(K.jit * C.f("avjit", 1), C.f("avjitmax", 0.15));
            xtra = C.f("avxtra", 0.25);
        }
        double rt = now - *K.off - D;
        NB& S = nb_st(C, B, now);
        S.ip = S.ip + 1;
        S.lag = S.lag + D;
        const HPt& L = h[n - 1];
        if (n == 1 || rt >= L.t)
        {
            S.fim = S.fim + 1;
            if (n >= 2)
            {
                const HPt& P = h[n - 2];
                double xcap = C.f("nsxcap", 1.0);
                if (K.fr && xcap > 0 && L.t > P.t && now - K.st > C.f("nsxstale", 0.25))
                {
                    double u = std::min(rt - L.t, xcap) / (L.t - P.t);
                    return PathPt{L.x + (L.x - P.x) * u, L.y + (L.y - P.y) * u, L.z, 1, L.x - P.x, L.y - P.y};
                }
                double gp = L.t - P.t;
                bool rest = K.ts && *K.ts > L.t && (K.x - L.x) * (K.x - L.x) + (K.y - L.y) * (K.y - L.y) < 0.25;
                if (xtra > 0 && !rest && gp > 0.03 && gp < 0.6)
                {
                    double over = std::min(rt - L.t, xtra), u = over / gp;
                    double fade = 1 - over / (xtra + 0.05);
                    return PathPt{L.x + (L.x - P.x) * u * fade, L.y + (L.y - P.y) * u * fade, L.z, 1, L.x - P.x, L.y - P.y};
                }
                return PathPt{L.x, L.y, L.z, 0.9, L.x - P.x, L.y - P.y};
            }
            return PathPt{L.x, L.y, L.z, 0.9, 0, 0};
        }
        for (size_t i = n - 1; i >= 1; i--)
        {
            const HPt& P = h[i - 1];
            if (rt >= P.t || i == 1)
            {
                const HPt& Q = h[i];
                double u = std::max(0.0, (rt - P.t) / (Q.t - P.t));
                return PathPt{P.x + (Q.x - P.x) * u, P.y + (Q.y - P.y) * u, P.z + (Q.z - P.z) * u, 1, Q.x - P.x, Q.y - P.y};
            }
        }
        return std::nullopt;
    }

    inline std::pair<double, double> ns_noback(const Ctx& C, NB& B, const Mover& K, double vx, double vy, std::optional<double> dx, std::optional<double> dy, double now, std::optional<double> ox, std::optional<double> oy)
    {
        if (ox && *ox * *ox + *oy * *oy > 900)
        {
            auto v = ns_noback(C, B, K, vx, vy, dx, dy, now, std::nullopt, std::nullopt);
            vx = v.first;
            vy = v.second;
            double ddx = *ox, ddy = *oy;
            if (now - K.mvAt.value_or(-10) >= C.f("nsbhold", 0.5)) return {vx, vy};
            double d = std::sqrt(ddx * ddx + ddy * ddy);
            double a = (vx * ddx + vy * ddy) / d;
            double lim = -C.f("nsbmax", 60);
            if (a < lim)
            {
                NB& S = nb_st(C, B, now);
                S.slow = S.slow + 1;
                vx = vx + (lim - a) * ddx / d;
                vy = vy + (lim - a) * ddy / d;
            }
            return {vx, vy};
        }
        double d2 = dx.value_or(0) * dx.value_or(0) + dy.value_or(0) * dy.value_or(0);
        if (d2 < 1) return {vx, vy};
        double d = std::sqrt(d2);
        double ux = *dx / d, uy = *dy / d;
        double a = vx * ux + vy * uy;
        if (a >= 0) return {vx, vy};
        double lim = (now - K.mvAt.value_or(-10) < C.f("nsbhold", 0.5)) ? 0 : -C.f("nsbmax", 60);
        if (a >= lim) return {vx, vy};
        NB& S = nb_st(C, B, now);
        if (lim == 0) S.cut = S.cut + 1;
        else S.slow = S.slow + 1;
        return {vx + (lim - a) * ux, vy + (lim - a) * uy};
    }

    inline bool ns_sample(const Ctx& C, Mover& K, double x, double y, double z, std::optional<double> yaw, std::optional<double> ts, double now,
                          std::optional<double> vx, std::optional<double> vy, std::optional<double> vz, bool still)
    {
        ns_hist(K, x, y, z, ts, now, vx.has_value(), C.f("nsxcap", 1.0));
        if (yaw) K.yaw = yaw;
        if (vx)
        {
            K.has = true; K.x = x; K.y = y; K.z = z; K.vx = *vx; K.vy = vy.value_or(0); K.vz = vz.value_or(0); K.st = now; K.ts = ts;
            return true;
        }
        if (!K.has)
        {
            K.has = true; K.x = x; K.y = y; K.z = z; K.vx = 0; K.vy = 0; K.vz = 0; K.st = now; K.ts = ts;
            return true;
        }
        double dx = x - K.x, dy = y - K.y, dz = z - K.z;
        if (dx * dx + dy * dy + dz * dz < 0.25)
        {
            if (still) { K.vx = 0; K.vy = 0; K.vz = 0; K.st = now; }
            return false;
        }
        double dt = (K.ts && ts) ? (*ts - *K.ts) : 0;
        double vx2 = 0, vy2 = 0, vz2 = 0;
        if (dt > 0.03 && dt < 1) { vx2 = dx / dt; vy2 = dy / dt; vz2 = dz / dt; }
        double mx = C.f("nsvmax", 1500);
        if (vx2 * vx2 + vy2 * vy2 + vz2 * vz2 > mx * mx) { vx2 = 0; vy2 = 0; vz2 = 0; }
        K.x = x; K.y = y; K.z = z; K.vx = vx2; K.vy = vy2; K.vz = vz2; K.st = now; K.ts = ts;
        return true;
    }

    struct NsS
    {
        double at = 0;
        int n = 0, dr = 0, sn = 0, en = 0, mk = 0, fail = 0, pup = 0, smp = 0;
        double es = 0, emax = 0, ms = 0, msmax = 0;
    };
    inline NsS ns_s0(double now) { NsS s; s.at = now; return s; }

    struct DriveOut
    {
        bool ok = false;
        bool snap = false;
        double sx = 0, sy = 0, sz = 0;
        double vx = 0, vy = 0, vz = 0;
        bool rot = false;
        double yaw = 0;
    };
    inline DriveOut ns_drive(const Ctx& C, NB& B, NsS& S, Mover& K, std::optional<double> ax, double ay, double az, std::optional<double> cur_yaw, double now)
    {
        DriveOut o;
        if (!K.has) return o;
        if (!ax) return o;
        double prevAt = K.at.value_or(now - 0.1);
        bool wasStopped = K.stopped;
        double dtT = std::min(std::max(now - prevAt, 0.05), 0.3);
        K.at = now;
        K.stopped = false;
        double cap = C.f("nscap", 250) / 1000;
        double age = std::max(0.0, now - K.st);
        double ext = std::min(age, cap);
        double tx = K.x + K.vx * ext, ty = K.y + K.vy * ext, tz = K.z + K.vz * ext;
        std::optional<PathPt> ip;
        if (C.f("nsip", 0) == 1) ip = ns_path(C, B, K, now);
        if (ip) { tx = ip->x; ty = ip->y; tz = ip->z; }
        double ex = tx - *ax, ey = ty - ay, ez = tz - az;
        double e = std::sqrt(ex * ex + ey * ey + ez * ez);
        S.dr = S.dr + 1;
        S.es = S.es + e;
        S.en = S.en + 1;
        if (e > S.emax) S.emax = e;
        double snapAt = K.av && C.f("avsnap", 0) > 0 ? C.f("avsnap", 0) : C.f("nssnap", 300);
        if (e > snapAt)
        {
            o.snap = true; o.sx = tx; o.sy = ty; o.sz = tz;
            ex = 0; ey = 0; ez = 0;
            S.sn = S.sn + 1;
        }
        double f = std::max(0.0, std::min(1.0, (cap - age) / dtT));
        double k = std::min(C.f("nsgain", 7), 0.9 / dtT);
        double vx = K.vx * f + ex * k, vy = K.vy * f + ey * k, vz = K.vz * f + ez * k;
        if (ip) { vx = ex * ip->g / 0.1; vy = ey * ip->g / 0.1; vz = ez * ip->g / 0.1; }
        if (C.f("nsnoback", 0) == 1)
        {
            std::optional<double> ddx = ip ? std::optional<double>(ip->dx) : K.dx;
            std::optional<double> ddy = ip ? std::optional<double>(ip->dy) : K.dy;
            auto v = ns_noback(C, B, K, vx, vy, ddx, ddy, now, K.nbvx, K.nbvy);
            vx = v.first;
            vy = v.second;
            K.nbvx = vx;
            K.nbvy = vy;
        }
        double dz = C.f("nsdead", 3);
        if (K.vx * K.vx + K.vy * K.vy + K.vz * K.vz < 25 && ex * ex + ey * ey + ez * ez < dz * dz) { vx = 0; vy = 0; vz = 0; }
        double sp = std::sqrt(vx * vx + vy * vy + vz * vz), mx = C.f("nsvmax", 1500);
        if (sp > mx) { vx = vx * mx / sp; vy = vy * mx / sp; vz = vz * mx / sp; }
        double amax = K.av ? C.f("avacc", 0) : 0;
        if (amax > 0)
        {
            double rdt = std::min(std::max(now - prevAt, 0.001), 0.25);
            if (wasStopped || now - prevAt > 0.25 || o.snap) { K.svx = vx; K.svy = vy; K.svz = vz; }
            else
            {
                double dvx = vx - K.svx, dvy = vy - K.svy, dvz = vz - K.svz, dm = std::sqrt(dvx * dvx + dvy * dvy + dvz * dvz), lim = amax * rdt;
                if (dm > lim) { dvx *= lim / dm; dvy *= lim / dm; dvz *= lim / dm; }
                K.svx += dvx; K.svy += dvy; K.svz += dvz;
                vx = K.svx; vy = K.svy; vz = K.svz;
            }
        }
        o.vx = vx; o.vy = vy; o.vz = vz;
        if (K.yaw && cur_yaw)
        {
            double d = ang_diff(*K.yaw, *cur_yaw);
            if (std::fabs(d) > 0.3)
            {
                o.rot = true;
                o.yaw = *cur_yaw + d * (1 - std::exp(-dtT * C.f("nsyawgain", 10)));
            }
        }
        o.ok = true;
        return o;
    }

    struct PupPos
    {
        bool has = false;
        double x = 0, y = 0, z = 0;
    };
    struct PupStepIn
    {
        double now = 0, recv = 0, rx = 0, ry = 0;
        std::optional<double> ryaw;
        double vx = 0, vy = 0;
        double tz = 0;
        double offx = 0, offy = 0;
        std::optional<double> pa_yaw;
    };
    struct PupStepOut
    {
        double vx = 0, vy = 0, speed = 0, step = 0, dt = 0;
        bool jump = false;
        double jump_m = 0;
    };
    inline PupStepOut pup_step(const Ctx& C, PupPos& pos, std::optional<double>& yaw, std::optional<double>& lastT, const PupStepIn& in)
    {
        PupStepOut o;
        double now = in.now;
        double dt = std::min(std::max(now - lastT.value_or(now), 0.001), 0.25);
        lastT = now;
        o.dt = dt;
        double age = now - in.recv;
        double vx = in.vx, vy = in.vy;
        if (age > 0.6) { vx = 0; vy = 0; }
        double ex = std::min(age, C.f("smooth", 0) == 1 ? 0.6 : 0.3) + C.f("avlead", 200) / 1000;
        double tx = in.rx + vx * ex + C.f("avoffx", 0) + in.offx, ty = in.ry + vy * ex + in.offy, tz = in.tz;
        if (!pos.has) { pos.has = true; pos.x = tx; pos.y = ty; pos.z = tz; }
        double dx = tx - pos.x, dy = ty - pos.y;
        double k = 1 - std::exp(-dt * C.f("avgain", 30));
        if (dx * dx + dy * dy > 2000.0 * 2000.0)
        {
            k = 1;
            o.jump = true;
            o.jump_m = std::sqrt(dx * dx + dy * dy) / 100;
        }
        double ox = pos.x, oy = pos.y;
        if (C.f("smooth", 0) == 1 && k < 1)
        {
            double nx = pos.x + vx * dt, ny = pos.y + vy * dt;
            double ex2 = tx - nx, ey2 = ty - ny;
            double ss = C.f("smoothsnap", 300);
            double kc = (ex2 * ex2 + ey2 * ey2 > ss * ss) ? 1 : (1 - std::exp(-dt * C.f("smoothgain", 4)));
            double nz = pos.z + (tz - pos.z) * (1 - std::exp(-dt * 10));
            pos.x = nx + ex2 * kc;
            pos.y = ny + ey2 * kc;
            pos.z = nz;
        }
        else
        {
            double nz = pos.z + (tz - pos.z) * k;
            pos.x = pos.x + dx * k;
            pos.y = pos.y + dy * k;
            pos.z = nz;
        }
        o.step = std::sqrt((pos.x - ox) * (pos.x - ox) + (pos.y - oy) * (pos.y - oy));
        double speed = std::sqrt(vx * vx + vy * vy);
        double want;
        if (in.pa_yaw) want = *in.pa_yaw;
        else if (speed > 100) want = datan2(vy, vx);
        else if (in.ryaw) want = *in.ryaw;
        else if (yaw) want = *yaw;
        else want = 0;
        double base = yaw ? *yaw : want;
        yaw = base + ang_diff(want, base) * (1 - std::exp(-dt * 10));
        o.vx = vx;
        o.vy = vy;
        o.speed = speed;
        return o;
    }

    struct FarPos
    {
        bool has = false;
        double x = 0, y = 0, z = 0;
        std::optional<double> yaw, lastT;
    };
    struct FarOut
    {
        double x = 0, y = 0, z = 0, yaw = 0, speed = 0;
    };
    inline FarOut far_step(const Ctx& C, FarPos& F, double now, double recv, double rx, double ry, double rz, std::optional<double> ryaw, double vx, double vy)
    {
        double dt = std::min(std::max(now - F.lastT.value_or(now), 0.001), 0.25);
        F.lastT = now;
        double age = now - recv;
        if (age > 0.6) { vx = 0; vy = 0; }
        double ex = std::min(age, 0.3) + C.f("avlead", 200) / 1000;
        double tx = rx + vx * ex, ty = ry + vy * ex, tz = rz - 90;
        if (!F.has || (tx - F.x) * (tx - F.x) + (ty - F.y) * (ty - F.y) > 2000.0 * 2000.0) { F.has = true; F.x = tx; F.y = ty; F.z = tz; }
        double k = 1 - std::exp(-dt * C.f("avgain", 30));
        F.x = F.x + (tx - F.x) * k;
        F.y = F.y + (ty - F.y) * k;
        F.z = F.z + (tz - F.z) * k;
        double speed = std::sqrt(vx * vx + vy * vy);
        double want;
        if (speed > 100) want = datan2(vy, vx);
        else if (ryaw) want = *ryaw;
        else if (F.yaw) want = *F.yaw;
        else want = 0;
        double base = F.yaw ? *F.yaw : want;
        F.yaw = base + ang_diff(want, base) * (1 - std::exp(-dt * 10));
        FarOut o;
        o.x = F.x; o.y = F.y; o.z = F.z; o.yaw = *F.yaw; o.speed = speed;
        return o;
    }

    struct SmStat
    {
        bool init = false;
        double at = 0, max = 0;
        int st[6] = {0, 0, 0, 0, 0, 0};
        int iv[4] = {0, 0, 0, 0};
        std::optional<double> recv;
    };
    inline void sm_stat(const Ctx& C, SmStat& S, double recv, double now, double step)
    {
        if (C.f("smoothlog", 1) != 1) return;
        if (!S.init) { S = SmStat{}; S.init = true; S.at = now; }
        if (!S.recv || *S.recv != recv)
        {
            if (S.recv)
            {
                double ms = (recv - *S.recv) * 1000;
                int b = ms < 110 ? 0 : ms < 160 ? 1 : ms < 250 ? 2 : 3;
                S.iv[b]++;
            }
            S.recv = recv;
        }
        int b = step < 1 ? 0 : step < 5 ? 1 : step < 15 ? 2 : step < 30 ? 3 : step < 60 ? 4 : 5;
        S.st[b]++;
        if (step > S.max) S.max = step;
        if (now - S.at >= 10)
        {
            if (S.st[2] + S.st[3] + S.st[4] + S.st[5] > 0)
                C.say(fmt("NOLA smooth 10 s (%s): passos cm <1:%d 1-5:%d 5-15:%d 15-30:%d 30-60:%d >60:%d max %.0f | amostras ms <110:%d 110-160:%d 160-250:%d >250:%d",
                          C.f("smooth", 0) == 1 ? "suave" : "antigo", S.st[0], S.st[1], S.st[2], S.st[3], S.st[4], S.st[5], S.max, S.iv[0], S.iv[1], S.iv[2], S.iv[3]));
            S.at = now;
            for (int& x : S.st) x = 0;
            for (int& x : S.iv) x = 0;
            S.max = 0;
        }
    }

    struct PfxH
    {
        bool init = false;
        std::string path;
        bool has_path = false;
        std::optional<int64_t> uid;
        std::vector<double> t;
        bool cap = false;
    };
    enum class Pfx { keep, recycle };
    inline Pfx pfx_tick(const Ctx& C, PfxH& H, std::vector<double>& R, const std::optional<std::string>& path, std::optional<int64_t> uid, double f, double ax, double ay, double az, double speed, double now)
    {
        if (f <= C.f("pupfixcm", 150)) return Pfx::keep;
        bool same = H.init && H.has_path == path.has_value() && (!path || H.path == *path) && H.uid == uid;
        if (!same) { H = PfxH{}; H.init = true; H.has_path = path.has_value(); H.path = path.value_or(""); H.uid = uid; }
        std::vector<double> t;
        for (double x : H.t) if (now - x < 10) t.push_back(x);
        t.push_back(now);
        H.t = t;
        if ((double)t.size() < C.f("pupfixn", 3)) return Pfx::keep;
        std::vector<double> k;
        for (double x : R) if (now - x < 600) k.push_back(x);
        R = k;
        if ((double)k.size() >= C.f("pupfixmax", 3))
        {
            if (!H.cap)
            {
                H.cap = true;
                C.say(fmt("NOLA pupfix: boneco movido de volta de novo (%.0f cm) mas ja houve %d trocas em 600 s -> corpo mantido", f, (int)k.size()));
            }
            return Pfx::keep;
        }
        R.push_back(now);
        C.say(fmt("NOLA pupfix: boneco movido de volta por outro sistema (%.0f cm de onde o pusemos, %d vezes em 10 s; ancora %.0f,%.0f,%.0f; remoto v=%.0f) -> corpo trocado (%d/%d em 600 s)",
                  f, (int)t.size(), ax, ay, az, speed, (int)R.size(), (int)C.f("pupfixmax", 3)));
        return Pfx::recycle;
    }

    struct Shot
    {
        double at = 0, ox = 0, oy = 0, oz = 0, dx = 0, dy = 0, dz = 0;
    };
    struct Quiet
    {
        double x = 0, y = 0, r = 0, until = 0;
    };
    struct HitIn
    {
        double ref = 0, hp = 0, now = 0;
        double px = 0, py = 0, pz = 0;
        std::optional<double> jump_until;
        std::optional<Quiet> quiet;
        std::optional<Shot> shot;
    };
    inline std::optional<std::string> hit_why(const Ctx& C, const HitIn& in, double amount, std::optional<double>& realAt)
    {
        double now = in.now;
        if ((in.ref - in.hp) >= kTgtHp * 0.9) return std::string("golpe inteiro de uma vez (script do jogo, b120)");
        if (now < in.jump_until.value_or(0)) return std::string("salto");
        if (in.quiet && now < in.quiet->until && (in.px - in.quiet->x) * (in.px - in.quiet->x) + (in.py - in.quiet->y) * (in.py - in.quiet->y) < in.quiet->r * in.quiet->r)
            return std::string("anomalia espelhada");
        std::optional<std::string> why;
        if (in.shot && now - in.shot->at < 0.6)
        {
            const Shot& L = *in.shot;
            double t = (in.px - L.ox) * L.dx + (in.py - L.oy) * L.dy + (in.pz - L.oz) * L.dz;
            if (t > 0)
            {
                double hx = L.ox + L.dx * t, hy = L.oy + L.dy * t, hz = L.oz + L.dz * t;
                if (std::sqrt((hx - in.px) * (hx - in.px) + (hy - in.py) * (hy - in.py)) < 120 && std::fabs(hz - in.pz) < 140) why = std::string("tiro local");
            }
        }
        double dmin = C.f("dmgmin", 1);
        if (!why && dmin > 0)
        {
            if (amount >= dmin) realAt = now;
            else if (now - realAt.value_or(-10) > 2) why = "tique<" + C.s("dmgmin");
        }
        return why;
    }

    struct WPart
    {
        bool ok = false;
        std::string pth, mats, pname, psock;
    };
    inline bool w1_split(const std::string& x1, std::string& part, std::string& statics)
    {
        if (x1.compare(0, 3, "W1~") != 0) return false;
        size_t k = x1.find('~', 3);
        if (k == std::string::npos) return false;
        part = x1.substr(3, k - 3);
        statics = x1.substr(k + 1);
        return true;
    }
    inline std::optional<std::string> pa_equip_swap(const Ctx& C, const std::string& oldW, const std::string& newW)
    {
        std::string op, os, np, ns;
        if (w1_split(oldW, op, os) && w1_split(newW, np, ns) && op == np) return std::nullopt;
        return pa_equip(C, pa_famc(C, oldW), pa_famc(C, newW));
    }
    inline WPart w_part(const std::string& part)
    {
        WPart w;
        size_t a = part.find('@');
        if (a == std::string::npos) return w;
        size_t b = part.find('@', a + 1);
        if (b == std::string::npos) return w;
        w.pth = part.substr(0, a);
        w.mats = part.substr(a + 1, b - a - 1);
        std::string rest = part.substr(b + 1);
        size_t c = rest.find('@');
        if (c == std::string::npos) { w.pname = rest; w.psock = ""; }
        else { w.pname = rest.substr(0, c); w.psock = rest.substr(c + 1); }
        w.ok = true;
        return w;
    }
    inline bool w_rl_hand(const Ctx& C, const std::optional<std::string>& acSt, bool isAc, const std::string& fam)
    {
        double m = C.f("pwrlhand", 1);
        if (fam != "pt" || m < 1) return false;
        return m >= 2 || (isAc && acSt && *acSt == "acrl");
    }
    inline std::string pa_rl_base(const Ctx& C, const std::string& base, bool addLive)
    {
        if (!addLive || C.f("pawrlcb", 1) != 1) return base;
        if (base == "idle") return "aidle";
        if (base == "walk") return "awalk";
        return base;
    }
    inline bool w_rl_ar(const Ctx& C, const std::string& fam, bool clipLive)
    {
        return clipLive && !fam.empty() && fam != "pt" && C.f("pwrlar", 1) == 1;
    }
    inline bool w_rl_rifle_ms(const Ctx& C, const std::string& fam)
    {
        return !fam.empty() && fam != "pt" && C.f("pwrlms", 0) == 1;
    }
    inline bool w_rl_stock(const Ctx& C, bool clipLive, const std::string& fam)
    {
        int m = (int)C.f("pwrlstk", 1);
        return clipLive && (m >= 2 || (m == 1 && fam == "pt") || w_rl_rifle_ms(C, fam));
    }
    inline const char* w_cls(const Ctx& C)
    {
        return C.f("pwskel", 1) == 1 ? "/Script/Engine.SkeletalMeshComponent" : "/Script/Engine.PoseableMeshComponent";
    }
    inline bool ac_layer(const Ctx& C, int addType)
    {
        return C.f("paadd", 1) == 1 && addType > 0;
    }
    struct WrlEnt
    {
        const char* skel;
        const char* chr;
        const char* wpn;
        int add;
        double len;
        double rate;
    };
    inline const WrlEnt kWrl[] = {
#define X(a, b, c, d, e, f) {a, b, c, d, e, f},
#include "s2m_wrl_tab.inc"
#undef X
    };
    inline const WrlEnt* wrl_ent(const std::string& skel)
    {
        if (skel.empty()) return nullptr;
        for (const auto& e : kWrl)
            if (skel == e.skel) return &e;
        return nullptr;
    }
    inline const WrlEnt* wrl_pick(const Ctx& C, const std::string& id, const std::string& skel)
    {
        if (id != "rl" || C.f("pawrl", 1) != 1) return nullptr;
        return wrl_ent(skel);
    }
    inline const char* wrl_chr(const Ctx& C, const WrlEnt* e)
    {
        if (!e || e->add <= 0 || C.f("pawrlchr", 1) != 1) return nullptr;
        return e->chr;
    }
    inline const char* wrl_ms(const Ctx& C, const WrlEnt* e, const std::string& fam)
    {
        if (!e || !w_rl_rifle_ms(C, fam)) return nullptr;
        return e->chr;
    }
    inline const char* wrl_body(const Ctx& C, const WrlEnt* e, const std::string& fam)
    {
        if (!e || e->add > 0 || fam != "pt" || C.f("pwrlpchr", 1) != 1) return nullptr;
        return e->chr;
    }
    struct WshEnt
    {
        const char* skel;
        const char* chr;
        int add;
        double len;
        double rate;
        const char* wpn;
        double bi;
        double bo;
    };
    inline const WshEnt kWsh[] = {
#define X(a, b, c, d, e, f, g, h) {a, b, c, d, e, f, g, h},
#include "s2m_wsh_tab.inc"
#undef X
    };
    inline const WshEnt* wsh_ent(const std::string& skel)
    {
        if (skel.empty()) return nullptr;
        for (const auto& e : kWsh)
            if (skel == e.skel) return &e;
        return nullptr;
    }
    inline const WshEnt* wsh_pick(const Ctx& C, const std::string& skel, const std::string& fam)
    {
        if (C.f("pashoot", 1) != 1 || (fam != "pt" && fam != "ar")) return nullptr;
        if (const WshEnt* e = wsh_ent(skel)) return e;
        if (C.f("pashootfb", 1) != 1) return nullptr;
        return wsh_ent(fam == "pt" ? "SK_pm_Skeleton" : "SK_AK74_Skeleton");
    }
    inline const char* wsh_gun(const Ctx& C, const WshEnt* e, const std::string& skel)
    {
        if (!e || C.f("pashgun", 1) != 1 || !e->wpn || !*e->wpn || e != wsh_ent(skel)) return nullptr;
        return e->wpn;
    }
    inline double wsh_blend(const Ctx& C, const char* flag, double stock)
    {
        double v = C.f(flag, -1);
        return v >= 0 ? v : stock;
    }
    inline bool wsh_new(int& seen, int n)
    {
        if (seen < 0 || n < seen) { seen = n; return false; }
        if (n == seen) return false;
        seen = n;
        return true;
    }
    inline bool wsh_go(double now, double wrlUntil, bool acLive, bool lad)
    {
        return !lad && !acLive && now >= wrlUntil;
    }
    inline bool w_rl_cancel(const Ctx& C, const std::string& fam, bool hand)
    {
        return hand && fam == "pt" && C.f("pwrlcan", 1) == 1;
    }
    inline bool w_same_part(const std::string& oldW, const std::string& newW)
    {
        if (oldW.compare(0, 3, "W1~") != 0 || newW.compare(0, 3, "W1~") != 0) return false;
        size_t a = oldW.find('~', 3), b = newW.find('~', 3);
        if (a == std::string::npos || b == std::string::npos || a == 3) return false;
        return oldW.compare(3, a - 3, newW, 3, b - 3) == 0;
    }
    inline bool wrl_hold(const Ctx& C, const std::string& oldW, const std::string& newW, bool clipLive)
    {
        return clipLive && C.f("pawrlhold", 1) == 1 && w_same_part(oldW, newW);
    }
    inline bool w_det_hand(const Ctx& C, bool det, const std::string& fam)
    {
        return det && fam == "pt" && C.f("padetg", 1) == 1 && C.f("pwrlhand", 1) >= 1;
    }
    inline bool w_bolt(const Ctx& C, const std::string& pth)
    {
        return C.f("pabolt", 1) == 1 && det_lower(pth).find("/weapons/bolt/") != std::string::npos;
    }
    inline bool w_knife(const Ctx& C, const std::string& pth)
    {
        return C.f("paknife", 1) == 1 && C.f("pwhand", 1) == 1 && det_lower(pth).find("/weapons/knifes/") != std::string::npos;
    }
    inline double ac_dur(const Ctx& C, const std::string& id, bool knife, double d)
    {
        double k = C.f("paknml", 1.5);
        return (id == "ml" && knife && k > 0) ? std::min(d, k) : d;
    }
    inline std::string w_sock(const Ctx& C, const std::string& pth, const std::string& psock)
    {
        std::string p = pth;
        for (char& ch : p) ch = (char)std::tolower((unsigned char)ch);
        if (w_knife(C, pth)) return "jnt_r_hand";
        if (C.f("pwhand", 1) == 1 && p.find("/weapons/knifes/") != std::string::npos) return "jnt_r_weapon";
        if (w_bolt(C, pth)) return C.f("pabarm", 1) == 1 ? "jnt_r_hand" : "jnt_r_weapon";
        return psock.empty() ? std::string("GripPointTPP") : psock;
    }
    struct WStatic
    {
        bool ok = false;
        std::string nm, par, sock, path;
        std::vector<std::optional<double>> v;
    };
    inline WStatic w_static(const std::string& ent)
    {
        WStatic s;
        size_t p[4];
        size_t from = 0;
        for (int i = 0; i < 4; i++)
        {
            p[i] = ent.find('>', from);
            if (p[i] == std::string::npos) return s;
            from = p[i] + 1;
        }
        s.nm = ent.substr(0, p[0]);
        s.par = ent.substr(p[0] + 1, p[1] - p[0] - 1);
        s.sock = ent.substr(p[1] + 1, p[2] - p[1] - 1);
        s.path = ent.substr(p[2] + 1, p[3] - p[2] - 1);
        std::string tr = ent.substr(p[3] + 1);
        size_t i = 0;
        while (i < tr.size())
        {
            if (tr[i] == ',') { i++; continue; }
            size_t j = tr.find(',', i);
            if (j == std::string::npos) j = tr.size();
            s.v.push_back(tonum(tr.substr(i, j - i)));
            i = j;
        }
        s.ok = true;
        return s;
    }
    inline std::vector<std::string> split_nonempty(const std::string& s, char sep)
    {
        std::vector<std::string> out;
        size_t i = 0;
        while (i < s.size())
        {
            if (s[i] == sep) { i++; continue; }
            size_t j = s.find(sep, i);
            if (j == std::string::npos) j = s.size();
            out.push_back(s.substr(i, j - i));
            i = j;
        }
        return out;
    }

    inline int offt_want(double k) { return (int)std::ceil(k * 4 / 3); }

    inline std::optional<std::string> remote_life(const std::optional<std::string>& part)
    {
        if (!part) return std::nullopt;
        const std::string& s = *part;
        if (s.size() < 2 || !std::isalpha((unsigned char)s[0]) || s[1] != ',') return std::nullopt;
        auto numrun = [&](size_t i) {
            size_t j = i;
            while (j < s.size() && (s[j] == '-' || s[j] == '.' || (s[j] >= '0' && s[j] <= '9'))) j++;
            return j;
        };
        size_t j = numrun(2);
        if (j == 2 || j >= s.size() || s[j] != ',') return std::nullopt;
        size_t k = numrun(j + 1);
        if (k == j + 1) return std::nullopt;
        return s.substr(0, 1);
    }

    struct Hold
    {
        bool held = false;
        bool logged = false;
        std::optional<double> at;
    };
    inline void hold_arm(Hold& h, bool on, double now)
    {
        h.held = on;
        h.logged = false;
        if (on) h.at = now;
        else h.at.reset();
    }
    inline std::string hold_step(Hold& h, bool on, bool stable, int q, double now, double maxs)
    {
        if (h.held && !on) h.held = false;
        if (!h.held || !stable) return std::string();
        double w = now - h.at.value_or(now);
        if (q <= 0)
        {
            h.held = false;
            return fmt("NOLA corpos: fila nativa de nos de missao vazia (%d) %.1f s depois do pawn novo -> mundo solto", q, w);
        }
        if (w > maxs)
        {
            h.held = false;
            return fmt("NOLA corpos: ainda %d nos de missao na fila nativa %.0f s depois do pawn novo (qholdmax) -> mundo solto", q, w);
        }
        if (!h.logged)
        {
            h.logged = true;
            return fmt("NOLA corpos: o jogo ainda segura %d nos de missao depois da carga (mundo parado): corpos e spawns esperam", q);
        }
        return std::string();
    }
    inline const char* view_why(bool multi, int v, int views, bool snapOk, bool left, bool bye)
    {
        if (!multi) return "sem hub";
        if (v >= views) return "vista fechada";
        if (!snapOk) return "sem snapshot";
        if (left) return bye ? "fechou o jogo" : "saiu";
        return nullptr;
    }

    struct WdWatch
    {
        double at = -1;
        int n = 0;
        bool woke = false;
    };
    inline void wd_arm(WdWatch& w, double now)
    {
        w.at = now;
        w.n = 0;
        w.woke = false;
    }
    inline std::string wd_watch(WdWatch& w, int st, double hp, double now, double wakeHp)
    {
        if (w.at < 0 || w.woke) return std::string();
        double t = now - w.at;
        if (st == 0 || st == 3)
        {
            w.woke = true;
            return fmt("NOLA puppet: o jogo levantou o boneco caido %.1f s depois do FallWounded (estado %d, hp %.0f)%s", t, st, hp,
                       hp >= wakeHp ? ": HP de volta acima do limite de 35 do jogo, RegenHP do Nola_RemotePlayer no pak do avatar nao e 0 (pak antigo)" : "");
        }
        static const double marks[] = {2, 10, 25};
        if (w.n < 3 && t >= marks[w.n])
        {
            w.n++;
            return fmt("NOLA puppet: caido ha %.0f s: estado %d hp %.0f", marks[w.n - 1], st, hp);
        }
        return std::string();
    }

    inline bool pend_hit(bool needNew, bool inBefore, double mh, double lo, double hi, double dx, double dy, double r)
    {
        if (needNew && inBefore) return false;
        if (mh < lo || mh > hi) return false;
        return dx * dx + dy * dy < r * r;
    }

    inline bool spawn_turn(int me, const bool* spawning, int n)
    {
        for (int v = 0; v < n; ++v)
            if (v != me && spawning[v]) return false;
        return true;
    }
}
