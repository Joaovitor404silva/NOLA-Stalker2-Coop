# A3 copy weapons: findings so far (stopped early by the coordinator, 2026-10-17)

User bug: "Some weapons is missin in npcs hands too". Client screenshot C:\Users\USER\Desktop\S2CoopDebug\bugposeCLient.png (file time 17:34): two copies with their arms raised and empty. The left one shows a muzzle flash at chest height and no gun. On the right one, a rifle hangs vertically at the right hip, away from the hands.

Offline only. No src edits, no live work, nothing written to s2rag. Status tags: PROVEN = read in code, decompile or log. NOT PROVEN = a hypothesis to test live.

## 1. Every weapon path, port status and flags

| Path | Code (S2CoopPort\src) | Ported | Release default |
|---|---|---|---|
| Host sends eq, combat bit 2 (bHasWeaponInHands), weapon mesh/class (wMesh/wCls -> look) | s2n_nat.cpp ~1711-1739 (host scan), s2x_nat.cpp host_out own_tag "NN" | yes | natnpc=1 (flags_x0), natcb=1 |
| Look weapon pairing b191h: copy gets the host gun through Obj::SetHandItemSkeletalMesh(mesh, AnimBP) | s2n_nat.cpp ~1491-1506 | yes | natlook=1, natgen=1 |
| 2 s redraw: ChangeMainHandWeapon(host eq) only when GetMainHandEquipType != host eq | s2n_nat.cpp combat_tick ~3571-3585 | yes | natcb=1, natwpnguard=1 |
| nathol: detour on FUN_140d3111a (AI holster tick) = no-op for our copies | s2n_nat.cpp ho_detour ~3073, wpn_install ~3146 | yes, log "holster tick detour on" | nathol=1 |
| natthrsrc: detour on FUN_14043628c, keeps the forced threat cache | th_detour ~3037 | yes, log "on" | 1 |
| montnull / natnoatk | mp_detour / ma_detour ~2357-2387 | yes, log "on" | 1 / 1 |
| Montage mirror stop (natmstop / natmgate) | combat_tick ~3614-3647 | yes | natmstop=1, natmgate=1 |
| pwsnap re-snap of the gun on GripPointTPP | s2m_bodies.cpp:1349 | yes, but AVATAR PUPPET ONLY (P.wcomps). Not for natnpc copies, same as in Lua | pwsnap=1 |
| offanim/offvb render relevance | s2m_npc.cpp:1568 | yes, HOST ONLY (`x0::is_host()`), same as Lua | 1 |

- Flags PROVEN equal: live A/B flags.txt (C:\s2coop_inst\A|B) against the release defaults (flags_s2core + main + main_extra + x0 + role TSV). The only differences are devcmd, luacmd, pupmvlog and the dlgend*/dmirpick* debug keys. No weapon key differs.
- The s2n_nat C defaults are PROVEN equal to the last Lua-era DLL source (offline\b191mont\cpp\src 0bbf8dda). The only differences are natcover (0 -> 1, A1's area) and aggrop (0 -> 1). In the Lua era, Lua set only natcb from a flag, so there was no hidden Lua-set weapon knob.
- Conclusion: no missing port and no flag-default difference explains the bug. It is a design gap that already existed before the port (compare the old §166 "weapon not showing but muzzle flash").

## 2. Evidence (client log C:\S2DevRoot\...\ue4ss\UE4SS.log, 17:25-17:47)
- PROVEN: from 17:30:21 to 17:32:12 the combat samples show `eq_igual` = 100 % but `arma_na_mao` (AnimHumanWeaponData.bHasWeaponInHands, anim+WeaponData+0) only 57-90 %. Examples: 17:31:22 am=93 arma=57 eq=93; 17:30:42 am=133 arma=114 eq=133. So copies kept the host equip type (rifle) while their AnimBP said "no weapon in hands", and the 2 s redraw did not run, because it only checks eq. After 17:32:23, arma = eq.
- PROVEN: in the whole session montnull nulos=0 and troca_sem_anim=0. No NULL weapon-anim montage was played and no redraw happened without an anim instance. nathol pulados was 0-877 per 10 s, so the copies' AI keeps entering holster state 2 and the detour blocks it. b191h arma_par (SetHandItemSkeletalMesh swaps) was 1-2 per 10 s while new copies were taken.
- NOT PROVEN: whether the screenshot copies were in the 17:30-17:32 window (only the file time 17:34 is known). The samples cover only copies whose host is in combat. The gun's visibility is never sampled: since b170 the look diff counts no weapon visibility.

## 3. Decompile facts (PROVEN, read in this task; not yet in s2rag, NOT verified live)
- ItemAppearanceComponent (Obj+0x7C8): +0xD0 WeaponMeshInHands (what GetWeaponInHandsMeshComponent 0x32b676 returns), +0xD8 WeaponMeshUnequipped, +0xE8 ShootingAttachMesh.
- FUN_140efe9ee is the hand-item builder. Callers include Obj::SetHandItemSkeletalMesh and Obj::vf_429.
  - For a non-local-player owner ([owner+0x650]+0x10 != DAT_149eec140), a NEW in-hands SkeletalMeshComponent is created HIDDEN: FUN_1406dbb0c(comp,0,1) = SetVisibility(false) and SetHiddenInGame(comp,1,1). It is attached to the owner Mesh at the socket *param_5.
  - The add path does NOT call vf_387, so WeaponData is not re-initialised by it.
  - With mesh==0 (remove), a non-player NPC keeps the comp at +0xD8 and calls FUN_1468603a4, which places it on the body using the look picks [[agent+0x650]+0x690]. Then vt+0xc18 is called with reason 5.
- Bit layout:
  - FUN_1406dbb0c = SceneComponent SetVisibility; bVisible = comp+0x190 bit 0x20.
  - SceneComponent::SetHiddenInGame 0x8dca04; bHiddenInGame = comp+0x191 bit 0x08.
- Agent::vf_387(r) (0xf5f8e0): for r in {1,2,5,6,7} it calls FUN_140f5f922(updater+0x440), updater = agent+0xac8.
  - FUN_140f5f922 sets WeaponData.WeaponMesh (anim+0x490) = the in-hands comp.
  - If FUN_14032b582 (the weapon mesh AnimInstanceBase) is NULL, it writes bHasWeaponInHands (anim+0x480) = 0 and stops.
  - Otherwise it async-loads the anim collection. Its delegate (vtable 0x148e9c3b0, slot +0x60 = FUN_140f5fe98) calls FUN_140f5ffa6, which recomputes bHasWeaponInHands.
- FUN_14123e904 is the draw/equip anim action (param_1[0x15] = AnimInstanceHuman). Its weapon steps run only if bHasWeaponInHands=1 and a body draw montage exists:
  - it plays the weapon montage;
  - it HIDES WeaponData.WeaponMesh;
  - it binds the delegate vtable 0x148e99b90, slot +0x60 = FUN_140f340b4 -> FUN_140f340c8 (SHOW: SetVisibility(1, propagate) + SetHiddenInGame(0, propagate) on anim+0x490, then the owner vf+0xd18);
  - then it plays the body montage (FUN_14060ee02).
  - If bHasWeaponInHands=0 or there is no body draw montage, the action just finishes and does NOT show the mesh.
- So the only show path found for an NPC's in-hands gun is FUN_140f340c8, run by a delegate of the draw action. When that delegate fires (montage notify, end, interrupt) is NOT PROVEN.

## 4. Candidate roots (NOT PROVEN, to test live)
- R1 hidden gun: the copy's in-hands mesh is built hidden (FUN_140efe9ee). The show delegate (FUN_140f340c8) never runs because:
  - (a) bHasWeaponInHands was 0 at draw time, so the action ended without a show; or
  - (b) our montage mirror stopped the body draw montage before the delegate fired (natmstop stops any copy montage the host does not play 2.5 s after OUR last eq change, but draws started by the game or the copy's AI are not covered by e.eqChg); or
  - (c) the b191h SetHandItemSkeletalMesh swap made a new hidden comp with no draw action and no vf_387, so WeaponData.WeaponMesh is stale and the gun stays hidden.
  - This fits the left copy: shots and muzzle FX come from a mesh that exists but is hidden.
- R2 bHasWeaponInHands=0 with eq equal (proven in the log above): the AnimBP weapon layer is off, giving the raised, empty-hand aim pose. The writer is FUN_140f5f922 or FUN_140f5ffa6 at a moment when the weapon AnimInstanceBase is NULL. Nothing re-inits later, because natthrsrc removed the threat flips and the redraw checks only eq.
- R3 loose rifle: either (a) the +0xD8 WeaponMeshUnequipped placed on the body by FUN_1468603a4 after a holster or remove, or (b) the in-hands mesh on GripPointTPP not following the hand bones (finding anim_weapon_attach_budgeted_mesh_child_transform; pwsnap covers only the puppet).

## 5. Live probe plan (read-only first; hw slots as the supervisor assigns)
- P1 paired dump, one host fighter and its copy (pair via s2n_nat_dump). On both sides read:
  - IAC = [a+0x7C8]: +0xD0, +0xD8, +0xE8, each with its name, AttachParent and AttachSocketName, +0x190 bit 0x20, +0x191 bit 0x08, SkeletalMesh, AnimScriptInstance and its class;
  - Mesh.AnimScriptInstance+0x480 (bHasWeaponInHands), +0x490 (WeaponMesh) == IAC+0xD0?;
  - GetMainHandEquipType, IsChangingWeapon;
  - the current body and weapon montage;
  - world distance from the in-hands comp to the hand_r bone and to the GripPointTPP socket;
  - screenshot pair.
- P2 count per copy, across a fight, on the copy only:
  - calls to FUN_140efe9ee (0xefe9ee) and FUN_140f340c8 (0xf340c8), FUN_14123e904 (0x123e904) and FUN_140f5f922 (0xf5f922), each with the return address;
  - Montage_Stop from our DLL on a draw montage (mont_stop caller = natmstop/natmgate).
- P3 hw write watch on a copy with a hidden gun:
  - on the in-hands comp +0x191 (1 byte) and +0x190 -> the last writer rip;
  - on anim+0x480 (1 byte) -> writer rip (expected 0xf5f9fa or FUN_140f5ffa6);
  - on IAC+0xD0 / +0xD8 (8 bytes) -> who swaps or holsters.
- P4 (state change on B only, after "live ok"): set natmstop 0 and look again (tests R1b). Look with natlook 0 for new copies (tests R1c).
- Root-fix direction once proven: keep the game's own draw action intact on our copies (do not stop draw montages; route the host weapon through ChangeMainHandWeapon/equip instead of a bare SetHandItemSkeletalMesh swap), so the native show delegate and vf_387 run. No per-frame re-equip, no redraw timers.

## Files
- Scratch scripts: C:\Users\USER\AppData\Local\Temp\claude\C--Users-USER\377b20f2-cf5e-4ecd-9724-1dbc2995ff02\scratchpad (fdiff.py = flag diff, vt.py / xref.py / lea.py = exe vtable reads).
- OFFLINE_REPORT §2xx was NOT appended (stopped by the coordinator).
