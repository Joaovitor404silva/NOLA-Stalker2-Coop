#pragma once
static const char* const kPaDir = "/Game/_STALKER2/Animations/Characters/AnimSequences/";
static const int kWalkMax = 230;
static const double kTgtHp = 5000.0;
struct PaEnt { const char* st; const char* fam; const char* rel; double nat; bool has_nat; };
static const PaEnt kPa[] = {
    {"aidle", "ar", "ar/combat/idle/AS_tp_ar_combat_idle", 0.0, false},
    {"aidle", "pt", "pt/combat/Idles/AS_tp_pt_combat_idle", 0.0, false},
    {"awalk", "ar", "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_fwd", 156.0, true},
    {"back", "any", "ar/combat/locomotion/strafe/stand/AS_tp_ar_strafe_walk_bwd", 126.0, true},
    {"back", "bhn", "bh/locomotion/stand/AS_tp_bh_walk_l_180", 0.0, false},
    {"bleft", "any", "ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_bl", 126.0, true},
    {"bleft", "bhn", "bh/locomotion/stand/AS_tp_bh_walk_l_135", 0.0, false},
    {"bright", "any", "ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_br", 126.0, true},
    {"bright", "bhn", "bh/locomotion/stand/AS_tp_bh_walk_r_135", 0.0, false},
    {"cidle", "pt", "pt/combat/Idles/AS_tp_pt_lowcrouch_idle", 0.0, false},
    {"cidle", "any", "ar/combat/idle/AS_tp_ar_crouch_idle", 0.0, false},
    {"cidle", "bhn", "bh/Contextual/stand/tp_bh_ca_squatting/AS_tp_bh_ca_squatting_idle", 0.0, false},
    {"fall", "any", "bossfight/Korshunov/AS_tp_ar_combat_korshunov_jump_cycle", 0.0, false},
    {"idle", "bh", "bh/Idles/AS_tp_bh_idle", 0.0, false},
    {"idle", "ar", "ar/relax/idle/AS_tp_ar_relax_stand_idle", 0.0, false},
    {"idle", "pt", "pt/relax/idle/AS_tp_pt_relax_stand_idle", 0.0, false},
    {"left", "any", "ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_l", 156.0, true},
    {"left", "bhn", "bh/locomotion/stand/AS_tp_bh_walk_l_90", 0.0, false},
    {"rback", "bhn", "bh/locomotion/stand/AS_tp_bh_run_l_180", 0.0, false},
    {"rbleft", "bhn", "bh/locomotion/stand/AS_tp_bh_run_l_135", 0.0, false},
    {"rbright", "bhn", "bh/locomotion/stand/AS_tp_bh_run_r_135", 0.0, false},
    {"right", "any", "ar/combat/locomotion/strafe/stand/root_rotate/AS_tp_ar_strafe_walk_r", 156.0, true},
    {"right", "bhn", "bh/locomotion/stand/AS_tp_bh_walk_r_90", 0.0, false},
    {"rleft", "bhn", "bh/locomotion/stand/AS_tp_bh_run_l_90", 0.0, false},
    {"rright", "bhn", "bh/locomotion/stand/AS_tp_bh_run_r_90", 0.0, false},
    {"run", "bh", "bh/locomotion/stand/AS_tp_bh_run_fwd", 285.0, true},
    {"run", "ar", "ar/relax/locomotion/stand/AS_tp_ar_relax_run_fwd", 285.0, true},
    {"run", "pt", "bh/locomotion/stand/AS_tp_bh_run_fwd", 285.0, true},
    {"sprint", "ar", "ar/combat/locomotion/base/stand/AS_tp_ar_combat_sprint_fwd", 414.0, true},
    {"turnl", "ar", "ar/relax/turn_in_place/stand/AS_tp_ar_relax_turn_l_90", 0.0, false},
    {"turnl", "any", "bh/turn_in_place/stand/AS_tp_bh_turn_l_90", 0.0, false},
    {"turnr", "ar", "ar/relax/turn_in_place/stand/AS_tp_ar_relax_turn_r_90", 0.0, false},
    {"turnr", "any", "bh/turn_in_place/stand/AS_tp_bh_turn_r_90", 0.0, false},
    {"walk", "bh", "bh/locomotion/stand/AS_tp_bh_walk_fwd", 114.0, true},
    {"walk", "ar", "ar/relax/locomotion/stand/AS_tp_ar_relax_walk_fwd", 114.0, true},
    {"walk", "pt", "bh/locomotion/stand/AS_tp_bh_walk_fwd", 114.0, true},
};
struct PaFb { const char* st; const char* fb; };
static const PaFb kPaFb[] = {
    {"aidle", "idle"},
    {"awalk", "walk"},
    {"back", "walk"},
    {"bleft", "back"},
    {"bright", "back"},
    {"cidle", "idle"},
    {"cwalk", "cidle"},
    {"fall", "idle"},
    {"left", "walk"},
    {"rback", "back"},
    {"rbleft", "bleft"},
    {"rbright", "bright"},
    {"right", "walk"},
    {"rleft", "left"},
    {"rright", "right"},
    {"sprint", "run"},
    {"turnl", "idle"},
    {"turnr", "idle"},
};
struct PaSec { const char* sec; const char* st; };
static const PaSec kPaSec[] = {
    {"b", "back"},
    {"bl", "bleft"},
    {"br", "bright"},
    {"l", "left"},
    {"r", "right"},
};
struct PupAnim { const char* st; const char* path; };
static const PupAnim kPupAnim[] = {
    {"idle", "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/idles/AS_tp_bh_idle.AS_tp_bh_idle"},
    {"run", "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/locomotion/stand/AS_tp_bh_run_fwd.AS_tp_bh_run_fwd"},
    {"walk", "/Game/_STALKER2/Animations/Characters/AnimSequences/bh/locomotion/stand/AS_tp_bh_walk_fwd.AS_tp_bh_walk_fwd"},
};
struct PupNat { const char* st; double nat; };
static const PupNat kPupNat[] = {
    {"run", 285.0},
    {"walk", 114.0},
};
struct OfftCv { const char* name; double def; };
static const OfftCv kOfftCv[] = {
    {"gsc.MidDistanceComponentsMaxTickRate", 30.0},
    {"gsc.FarDistanceComponentsMaxTickRate", 10.0},
};
