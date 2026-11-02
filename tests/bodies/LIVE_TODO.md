# M1 bodies live checks (cpp_bodies=1), nothing here has run on a game

Setup: lua_guard.patch applied on both sides (not applied yet), release flags (farbody=1, pupmove=2, pupanim=1, nsmooth=1, pupcmoff=1, pupfix=1, pupfeet=1, tgtgod=1, kzsafe=1, abpguard=2, pupnoai=1, offtick=60, tgtnoint=1). cpp_bodies alone breaks the Lua readers listed in PORT_LOG "M1 bodies" X0 need 5: run these with M2/M3/M4/M6 ported too, or with the X0 Lua bridge.

PORT_PLAN M1 rows:
1. Fight bench, all rows: every row that was PASS stays PASS. Rows C3 (puppet visible, moves, animates), C4 (hidden target takes hits: `NOLA target: DANO seq=`), C19.
2. fakehost 09 (gap + leash).
3. 1.4 km teleport round trips x3 (b77tp repro): `NOLA corpos: jogador local saltou ... (teleporte) -> referencias ... largadas sem tocar`, then `corpos liberados` after bodywait, a new puppet adopted, no crash.
4. Host emission with the client beside it (D10 repro): no SetAnimationMode anywhere; `abpguard: censo` lines show sem controlador=0 for our bodies.
5. Client relaunch, then the re-adopted puppet has an AnimInstanceHuman (D6): `NOLA puppet: AnimBP do jogo mantido ... humano=true`.
6. heapchk plugin (b201) on B during the bench: HITS=0.

Probes for what I could not measure offline:
7. Snap.recv clock: x0::remote().recv must be on the same clock as x0::now() (Lua: r.recv vs os.clock). If not, age > 5 hides the puppet for good: `NOLA puppet: escondido=true` that never flips back.
8. T0 transform: AddComponentByClass without a RelativeTransform param, then relative 0/0/1. Check the weapon sits in the hand (`NOLA puppet: arma remota ... existe=true`) and the far body meshes are not offset.
9. abpGuard raw read: Agent+0xAC8 / +0xAD0 on a live BP_AI_Human_C (s2dev rd) must be the two HumanAnimController pointers Lua read through the registered properties; nonzero on a healthy NPC.
10. Far body: needs s2x_spawn_actor (X0). Without it, farbody=1 hides the NPC puppet at > pupfar and shows nothing. With it: `NOLA farbody: corpo distante criado (N malhas)` at 60+ m, walk/run anim plays, back to the NPC puppet at < 50 m.
11. Clip loads: needs s2x_load_asset (X0) for clips not already in memory; else `NOLA pupanim: clipe nao carregou ...` per missing clip and the AnimBP alone drives the legs.
12. Flag text in logs: Val::str for floats (offtick, dmgmin, pupwin) must print like Lua tostring ("60", "0.5"); compare one `offtick: valores do jogo guardados` line with Lua's.
13. remoteIn shared state: a `kzsafe: remoto FORA DO MUNDO` line may now show twice (M1 copy + Lua callers). Expected, not a bug.
14. Tick placement: Lua ran bodyGate before the other tick100 work and the bodies after it; C++ runs them together on the bodies100 row. Watch for a gate open one tick earlier or later than the Lua log.
