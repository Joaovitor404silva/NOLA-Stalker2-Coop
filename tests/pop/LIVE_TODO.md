# M6 population live checks (cpp_pop=1), nothing here has run on a game

Pair 1, client B with alifeoff=1, host A Lua or C++ (wire is unchanged).

1. AI feature call: XDeactivateAIFeature takes an EnumProperty `Feature`. Lua wrote 203647 into it. x0::Call.i refuses a size mismatch, so this is the first thing to read: log `NOLA alife: DESLIGADO ... ok=true,true` must appear; `chamadas falharam (ok=false,...)` means the enum width differs (probe: s2dev props on CustomConsoleManagerAM XDeactivateAIFeature, then pass the right width).
2. C5: B census. After DESLIGADO count hidden live non-own pawns (S2Dev find BP_AI_Human_C, bHidden) vs the host list (A out.json npcs); hubs and trade places stay visible and usable (talk to a trader in Rostok, F key).
3. C5: hidnpc. Spawn a dog pack near B (spawn3.lua on A is host only; on B use a hidden dog seen in threat) and read `NOLA hidnpc: NPC local escondido ... XKillNpcByUID(uid) ok=true`; second try after 5 s uses XDestroyModel; a natnpc-listed uid must log `esta na lista natnpc do host`.
4. C12: vendors local on both sides (D3 recipe), vendmir=1 vendone=1: host log `NOLA vend: NPC em x,y,z sem espelho`, client keeps the same single NPC visible; vendor talk opens trade on both.
5. D14: orphsweep=1 orphrm=3 after a client relaunch with an old S2Coop_RemoteTarget in the save: `NOLA orfao: ... -> escondido=true ...` and the adopted body untouched. Dry run first with orphsweep=2.
6. qfall: drop a quest item outdoors with the inventory open, `NOLA qfall: ... de volta em z` appears once; needs the trace helper (item below) or the log reads `pes do jogador (trace falhou)`.
7. Fight bench with cpp_pop=1 on B: every row that was PASS stays PASS; heapchk plugin on B HITS=0; natpelast empty.

Unmeasured by me (offline): worldKey is the local pawn Id (Lua used ST.wg.key); GetFocusedEnemy / GetGUID / GetPrototypeID param names come from the object dump (Obj, Actor, Actor).
