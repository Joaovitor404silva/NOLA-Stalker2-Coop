# NPC sync audit + plan (worker A1, 2026-10-17)

Build under test: S2CoopPort DLL md5 6d2395a1fe0bd7aaf658b7e14c22c905 (host A port 27061, client B port 27060), pair 1.
Logs: client C:\S2DevRoot\Stalker2\Binaries\Win64\ue4ss\UE4SS.log, host C:\S2DevRootHost\...\UE4SS.log, 19:01-19:22.
Probes: D:\modding\offline\a1cover\probes (pairs.js, covts.js, stuck.js, audit.js) and their .txt outputs.
Rule: "unknown" / "not verified" where nothing was measured.

## 1. Who drives a client body today

| System | File | Bodies | How it drives | Release |
|---|---|---|---|---|
| natnpc | s2n_nat.cpp | nearest natmax=8 host humans (BP_AI_Human_C) within natr=6000 of the client, host_rebuild 1590-1644, no hysteresis | adopt the matching local body (how 4, AI on) or spawn (how 2); natkin kin_frame per-frame placement + CMC RequestedVelocity; drive() MoveTo/face; natcover cover block + mode 6/4 | host stops listing for natlost=3 s -> release(): how 4 hands the body back to its local AI, how 2 XDestroyModel |
| npcsync | s2m_npcsync.cpp | host sends every live NPC within npsr=15000 of either player (npsmax 16); client pairs by uid/proto | np_take 833-904: brain StopLogic, path off, CMC tick off, Montage_Stop, PMC mover | np_rel 666-711, RestartLogic; skips natnpc bodies only on its 1 s np_pair pass (listed_addr 730-736) |
| mirrors | s2m_npc.cpp | everything not natnpc-listed and not npcsync-paired (smirrors=1, smaxnpcs 8): mutants, spill-over humans | PoseableMesh copy of the host pose | MIRROR - on death/leave |
| hidnpc | s2m_pop | client-local NPCs the host does not drive that are in threat | XKillNpcByUID | - |

## 2. Behaviour table

| Behaviour | State | Evidence |
|---|---|---|
| Cover / low crouch body | WORKS (fixed this session, natcover=1) | covts2.txt, on-screen pair 0x1966a251a80/0x1c517145a40, 20 s at 30 ms: inC 99%, action 94%, mode 99%, SmartCoverType 99%, SCP 98%; earlier pair SCP 99%, SCT 100%. Before: SCT 0/5 (before_all.txt, L1_cover_evidence.md). Screenshots D:\modding\offline\a1cover\p6_pairs.png, p7_pairs.png (same NPC low cover, host left / client right) |
| Cover mode stuck at 2 | RARE, root unknown | 1 event in ~14 min of watching all client humans (stuck1.txt 19:09:16, copy 2147477358, cb+0x910=1 and mm=2 for 21 s); 0 events in the next 10 min (stuck2.txt) with hooks on PoseSearch CMC vf_195 (exe+0xdafc66) and base vf_195 (exe+0xdb023a) and a hw write watch on the CMC MovementMode: every write in that window came from our execSetMovementMode. During the stuck window the override saw no call for that CMC; the base hook and the hw watch were not armed on it then, so a non-virtual call or a direct write is not excluded. Parked (minor), re-arm both if it shows again |
| Off-screen copy pose | BY DESIGN | PoseSearch updater runs only while the mesh is rendered (anim+0xe01 watch: 0 hits in 5 s off screen, 124 hits in 3 s on screen); off-screen SCT/SCP and bones stale. Not visible to the player, no fix |
| Locomotion legs under natkin | WORKS | fightbench a1audit1 13/13 PASS, speed ratio 0.87-0.95; client line "vel copia/host=0.98 (n=231)", erro_med 5 cm, teleports 0 |
| Sliding | BROKEN (ownership) | A2_slide_design.md R1: while npcsync owns a human, CMC tick is off and the PMC mover slides it. Live now: client npcsync "pares=2 dirigidos=1" with host natnpc na_lista=7 and host npcsync enviados 6 -> both systems hold human bodies at the same time |
| Ownership churn | BROKEN | client log 19:01-19:12: natnpc ADOTADA 13, CRIADA 2, solta "o host nao manda mais" 9; npcsync tomado 6 / solto 5; MIRROR +14/-13; hidnpc kills 9. Pattern: natnpc release -> npcsync take 0.1-0.6 s later -> natnpc re-adopt ~4-5 s later -> npcsync release 0.03-1 s later (19:10:36-19:10:43, bodies 2147478549/2147478106/2147478853) |
| Duplicate / respawned copies | BROKEN (same root) | host 2147473919: natnpc release 19:03:18 -> hidnpc killed the local body 2147477160 (XKillNpcByUID 1754) -> MIRROR + -> natnpc CRIADA 19:03:22 -> release XDestroyModel 19:07:52 -> MIRROR + -> CRIADA again 19:08:38 |
| Cover montage cut | BROKEN (same root) | np_take does Montage_Stop; A2 log 17:29:45.83 take stopped MG_tp_ar_cover_high |
| Weapon in hands | PARTLY BROKEN | client b168 combat lines: arma_na_mao 72/90, 76/95, 88/90 in many 10 s windows with eq_igual equal -> host holds the weapon, copy bHasWeaponInHands=0 (A3 R2). Screenshots D:\modding\offline\bench_shots\a1audit1_Bcrop.png: arms raised, no gun. Writer not measured (A3 candidates FUN_140f5f922 / FUN_140f5ffa6) |
| Equipment equal | PARTLY | eq_igual 70-78/90-95 in windows 19:15-19:20, 90/90 later. Which slot differs: unknown |
| Look / aim layer | WORKS (counts) | olhar_igual 100% in every window; LookAtData owner state forced by natstsrc |
| Aim in cover vs host | not verified | needs a paired on-screen measurement of the aim offset in cover |
| Copy shots | WORKS | copia_tiro_proprio=0/90, natbullet replica disparados=109 projeteis=109 perdidos=0 |
| Hits / death / corpse | WORKS (counts) | "morreu no host -> copia morta (XKillNpcByUID)", mortes counted, corpos guardados=1, acertos_host=10, dano_bloqueado=44 |
| Dead host NPC vs copy | MISMATCH seen once | audit1.txt: host 2147474536 lying (head -44, sp 0) while its copy reports sp=115 (copy off screen, pose stale); not verified on screen |
| Mutants | WORKS via mirrors | MIRROR +BP_AI_Blinddog_C_* lines; natnpc cannot take mutants (LookAtData 0x20, PORT_LOG M3) |
| PMC release Deactivate | DONE | s2m_bodies.cpp 2375 (mover_release p.deact) |
| Distance / LOD | BROKEN edge | natnpc list = nearest 8 within 6000 with no hysteresis, natlost 3 s; host npcs_classe=14 -> NPCs near rank 8 or near 60 m flip in and out, which feeds the churn above |

## 3. Root causes
- R1 (proven, A2 + this audit): two client systems own human bodies. npcsync takes human bodies natnpc just released and keeps CMC off while natnpc re-adopts; it excludes natnpc only on a 1 s pass.
- R2 (proven by code + counts): the natnpc host list has no hysteresis, so humans near the rank/range edge are released and re-adopted every few seconds, and every release hands the body to its local AI (how 4) where hidnpc may kill it, then natnpc spawns a fresh copy.
- R3 (proven counts, writer unknown): copies lose bHasWeaponInHands while the host holds the weapon.
- R4 (user bug 2026-10-17, open): in cover the copy body is right but the upper body is wrong (looks to the other side, reload at an off angle). To measure on paired samples: actor yaw, control/aim rotation, AnimBP aim offset yaw/pitch and look target, cover direction/side in the cover block, LookAt owner state, active montage with slot and start time, our face/rotation writes on the copy.
- R5 (unknown): the one cover-mode-2 event.

## 4. Design: one owner per body
- D1 natnpc is the only owner of client human bodies when natnpc=1. cfg npsnat (client): npcsync never pairs a pawn of natcls while natnpc=1; mutants and other classes keep npcsync/mirrors. Release default 1 once proven.
- D2 list hysteresis on the host: an NPC already listed stays listed while within natr*nathys and while its rank is within natmax+2 (natmax still caps new entries). cfg nathys (1.0 = off, release 1.2 once proven).
- D3 weapon: measure the bHasWeaponInHands writer on a copy (hw write watch on anim+0x480 in slot 2 when arma_na_mao drops), then fix at the source per A3 (keep the game's own draw/equip path on copies). No per-frame re-equip.
- Cover stays as built (natcover=1, event-driven: set on host enter, cleared and mode restored on host leave or release; MoveTo/nudge off while in cover).
- Spill-over humans (beyond natmax / natr*nathys) go to mirrors, as today; npcsync no longer drives them.
- D4 one owner after a release (cfg natpark): when the host stops listing a human whose uid the host still sends (npcsync #NP rows, recv within 10 s), natnpc keeps the body parked (hidden, shooters off, unkillable, still in K.own) instead of handing it to the local AI; hidnpc parks a host-known human twin instead of killing it. A parked body is re-adopted in place by natnpc (no spawn); it goes back to the local AI only when the host stops sending its uid. Mutants unchanged.
- D5 upper body in cover: fix at the writer once R4 is measured.

## 5. Proof plan
- Before/after on the same session: client natnpc solta/ADOTADA/CRIADA counts, npcsync tomados/soltos/dirigidos for humans, MIRROR +/- for humans, hidnpc kills of host-listed NPCs, per 10 min.
- Duplicate bodies per host NPC = 0 (no natnpc CRIADA for a host uid whose local twin exists or was killed); kills of released or host-known bodies = 0 (hidnpc XKillNpcByUID uid cross-checked against host uids). Baseline 19:01-19:22: 1 of 12 hidnpc kills hit a released body (uid 1754) and was followed by 2 natnpc spawns of the same host NPC.
- Paired host/client screenshots of the same NPCs (cover, walking, cover reload/peek facing).
- fightbench --lines 6 PASS.
- Switches: npsnat, nathys live in s2n_nat_cfg / x0 flags; release role TSVs default 1 / 1.2 once proven.
