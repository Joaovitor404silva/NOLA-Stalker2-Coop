# A2 copy sliding: paths, root, design, probes (worker A2, offline, 2026-10-17)

Symptom: client NPC copies sometimes slide (body translates, legs idle) instead of walk/run.

## 1. Copy movement paths on the client

| Path | Who | Placement | CMC | Leg source | Legs? |
|---|---|---|---|---|---|
| natkin (b182) | s2n_nat.cpp kin_frame 4240-4314 from tick_cb 4315 | K2_SetActorLocationAndRotation at host sample now-natkind(100ms) | tick ON; RequestedVelocity +0x578 + bit +0x54C (bit0 bHasRequestedVelocity, bit1 bRequestedMoveWithMaxSpeed) when speed>20 (4286-4303); MaxWalkSpeed +0x268 = speed*1.1 | PoseSearchLocomotion motion matching: trajectory from RequestedVelocity, database/tag from AI move tag U+0x6c8 (U=[agent+0xac8], FUN_142508b2e 422-547, "%s.MoveBehaviourTransition") | yes while AI MoveTo goal alive with gait 2/3/4 (natkinord=1, natrun 230, natsprint 450) |
| npcsync mover (b122/b125, npsmode 2) | s2m_npcsync.cpp np_take 833-904, np_drive 922-1004 | PMC mover (m01::mover_seed/sample/drive) | brain StopLogic, TogglePathFollowingComponent(false), StopMovementImmediately, CMC SetComponentTickEnabled(false); Velocity CMC+0xC0 / Acceleration +0x318 written; re-check every 1 s that CMC stays off | none (no clip, Montage_Stop on take) | NO: VERIFIED NOT-WORKING (CMC tick off + Velocity writes = legs idle, foot slip 0.29-0.78; b150 v2 anim_movimento 0/83) |
| natnpc non-kin (soft / vend) | s2n_nat.cpp drive 3692-3830 | snap/nudge, MaxWalkSpeed, face | on | AI MoveTo | yes (not the slide source in logs: empurroes=0 under kin) |

Other CMC writers checked (s2m_bodies puppet only, s2m_damage player, s2m_travel/save): none touch natnpc copies.

## 2. Root (proven from logs + code)

R1: two owners for one body. natnpc and npcsync hand the same client body back and forth; while npcsync owns it
the CMC tick is off and the PMC mover slides it.

Evidence, client UE4SS.log 17:25-17:41 (L1 session), host uid 1486 (_2147479889), body BP_AI_Human_C_2147448468, npcsync takes #14-#19:
- natnpc release (natlost 3 s, s2n_nat.cpp 3965-3975) -> npcsync take after 0.1-0.9 s:
  17:33:42.62->43.33, 36:24.03->24.13, 36:43.20->43.62, 37:01.22->02.12, 39:03.62->04.22, 39:22.72->22.93.
- natnpc re-adopt -> npcsync release after 0.03-0.93 s (CMC still off while natkin already places the body):
  34:05.10->.13, 36:26.13->.23, 36:44.41->.63, 37:03.52->04.12, 39:05.72->06.32, 39:25.20->26.13.
- Visible how-4 releases ("fica visivel onde esta, controle devolvido (b169)") then driven by npcsync for minutes:
  2147451765 (host 76991) from 17:29:45.83 (take stopped MG_tp_ar_cover_high); 2147443065 (host 76573)
  17:30:04.98-17:33:29.14 (~3.4 min); 2147448468 17:33:43-17:34:05 (21.8 s).
- Client npcsync 10 s lines: dirigidos=1-2 continuously 17:28:14-17:33:23.
- Races: 17:30:04.98 npcsync took BP_AI_Human_C_2147442348, natnpc CRIADA on the same body 25 ms later;
  17:29:33 natnpc spawned a copy while npcsync drove another kept body for the same host NPC 1486 (two bodies);
  17:28:12 2147474792 (host 75489) on npcsync, natnpc spawned its own copy 17:31:01.

Why the ownership flaps (code):
- Both roles run npcsync=1 (flags_role_client.tsv:62, flags_role_host.tsv:59) with natnpc=1.
- natnpc host list = nearest natmax 8 within natr 6000 of the CLIENT, no hysteresis (host_rebuild 1590-1644).
  npcsync host = within npsr 15000 of EITHER player, budget npsbud 24 / npsmax 16. Host na_lista 3-8 vs npcsync 7-10.
- npcsync exclusion of natnpc bodies is s2nat::listed_addr, checked only on its 1 s pass (np_pair 723-831), so
  a natnpc release is picked up by npcsync at the next pass, and a natnpc re-adopt keeps CMC off until npcsync's
  next pass releases it (np_rel 666-711).
- npcsync hid() = s2m_pop::isHiddenLocal (452); it does not see natnpc set_hidden.
- OFFLINE_REPORT §182 audit item 2 already required npcsync disabled for natnpc-listed NPCs; it is only partly done.

R2 (code-level, not measured): inside natkin the legs need CMC tick on, MovementMode Walking/NavWalking, a live AI
move goal with gait (U+0x6c8 database), RequestedVelocity each frame, and no root-locked montage. Candidate
windows: order start latency (ordem->movimento med 0.21-0.33 s, max 0.40), natkinpin hold goal (MoveTo own point,
3619-3626) while the host moves, order completed before the next one, natgad inert under kin (e.csp = kcsp),
cover montages (A1's area). Log gait stats are consistent (walk host ~110, run ~290; gait-4 rows 1368-1889 are host
velocity spikes). P1 decides whether R2 is real.

Not the cause: natnpc nudges (off under kin, empurroes=0); mirror drops for npcsync NPCs; offanim (ported, s2m_npc.cpp 1557-1616).

Unknown: why host dropped uid 1486 from the natnpc list (natr edge vs other filter) -> P2. Where PerformMovement
consumes +0x54C/+0x590 (no access in the 0x1ddec4 decompile) -> P3 watch.

## 3. Root design (one owner per body, no per-frame override)

D1 natnpc is the only client driver of human copies when natnpc=1:
- cfg npsnat (client, default 0): npcsync client never takes a pawn of class BP_AI_Human* while natnpc=1; host stops
  sending #NP rows for humans. npcsync keeps mutants/others.
- natnpc host list covers every live human within the old npcsync reach the client can see; natmax applies to
  spawning new copies only (cfg natlistall, default 0).
- list hysteresis: keep a listed NPC until natr*1.2 / natlost (cfg natrhys, default 0 = off).
D2 same-frame hand-off when D1 is off or for transitional bodies:
- natnpc take calls the npcsync release for that body in the same frame (CMC tick on, PMC mover_release,
  path following back) via an exported s2m_npcsync release-by-addr (cfg natnpsrel, default 0).
- npcsync np_pair refuses a body listed by natnpc or released by natnpc in the last natlost+2 s (checked at take,
  not only on the 1 s pass).
D3 (only if P1 proves R2): keep the AI move goal alive with the right gait while the host moves; natkinpin hold
goal only when the host speed < 20; re-issue MoveTo before completion (cfg natkinkeep, default 0).

Supervisor 2026-10-17: D1 + D2 approved, D3 only if P1 proves R2. Once D1/D2 are proven live, the switches default
to 1 in the release role TSVs. Order: A1 cover build + live proof first; then "live ok" + "edit ok" together, rebase
on A1's s2m_npcsync.cpp / s2n_nat.cpp, run P1-P4, implement, fight bench. hw slots 2-3 only.

All switches live cfg, default 0 during A/B on client B. No module writes CMC/anim fields every frame to fight
the game; kin_frame's RequestedVelocity is the game's own input path.

File ownership: s2m_npcsync.cpp is an npc module owned by A1, s2n_nat.cpp host list/take are outside A1's cover part
but the file is shared; edits only after "live ok" and agreement with A1/supervisor.

## 4. Live probes (after "live ok"; read-only first)

P1 client B, read-only per-frame sampler (plugin A2_slide), every copy within 30 m:
- ownership: natnpc kOn, npcsync dr owner, PMC IsActive;
- CMC: tick enabled, MovementMode/Custom +0x221/+0x222, Velocity +0xC0, Acceleration +0x318,
  RequestedVelocity +0x578, bits +0x54C, LastUpdateRequestedVelocity +0x590, MaxWalkSpeed +0x268;
- anim: PSLD anim+0xCF0 Tags (+0x10) FNames, U+0x6c8 FName, bMoving/bWalking, active montage + IsPlayingRootMotion,
  RootMotionMode;
- slip: jnt_l_foot/jnt_r_foot socket speed vs body displacement.
Slide frame = body > 60 cm/s and slip > 0.5 for > 0.3 s; log the owner and the paired host NPC velocity, mode,
montage and tags at that moment.
P2 host A, read-only per 1 s: for every human near natr, distance to the client and the host_rebuild filter result
(explains the uid 1486 drops).
P3 hw watch on B, slots 2-3 only: CMC+0x54C on a sliding copy (writers other than kin_frame); U+0x6c8 (move-tag
writer, coordinate with A1's P2); CMC+0x221 if P1 shows a non-walking mode.
P4 client B, state-changing: A/B npcsync client off vs on, natkinpin 0/1, count slide frames per 5 min; then the
fight bench.
Pass: slide frames on copies == 0 with D1 on, with no new desync in the fight bench.
