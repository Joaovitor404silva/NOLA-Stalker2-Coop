# M2 damage: live checks still needed

Nothing below has run in a game. Offline coverage is in run_parity.py: 11 scenarios match Lua line for line, plus 1 C++-only golden.

## Setup
- Both clients: flags.txt deduped, `dmgin=1` on B (release flags_client.txt line 20), `lifeguard=1`, `shotdetect=1`, `cine=1`, `tgtgod=1`.
- C++ run: `cpp_io=1 cpp_bodies=1 cpp_damage=1` on both sides. Apply tests\damage\lua_guard.patch first.
- Lua run: the same, but with `cpp_damage=0`.
- Fight: `fightbench.py --spawn --lines 3 --tag rel` (spawn3), the client standing next to the host.

## The six root-cause counters, both routes
Run `natdmg=0` (route D) and `natdmg=1` (route ND, only after X0 has merged b206).
1. Effective B dmgin: the client line `NOLA dmg cadeia 10s (cliente): ... dmgin=1`, with `desligado=0`.
2. NPCs pick the target:
   - `foco_no_alvo` > 0 in `NOLA dmg cadeia 10s (host)`;
   - the native aggrop line `alvo_como_jogador` > 0.
3. The target takes hits: `eventos` > 0 on the host line (route D), or `rota_nd` > 0 (route ND). Also M1's `NOLA target: DANO` lines.
4. #D leaves the host: `enviados` and `seq_max` rise. In A out.json, x1 carries `#D:`.
5. #D reaches B:
   - `recebidos` > 0, `novos` > 0, `perdidos` ~ 0;
   - B in.json carries the same seq.
6. B applies it:
   - route D: `NOLA dmgin 10s: aplicados=N` > 0, and the cuts `cortados_pelo_teto` / `morto` / `protecao` / `caido` read sensibly;
   - route ND: `nd_aplicados` > 0, plus the b206 `NOLA b206 natdmg 10 s (cliente)` line;
   - either way, `hp_perdido` > 0 and the client hp visibly drops.
- No double count: with natdmg=1, the host line shows `enviados=0` and `rota_nd` > 0. The client shows `aplicados=0` from #D.

## Gate rows (PORT_PLAN M2, rows C6 / C7 / C19)
- The client hp drops under host-NPC fire (both routes). Report which route is simpler. Prefer it if both pass.
- The host hp drops (native).
- A client kill on an NPC counts on the host:
  - `SHOT(core) ... hp X -> 0.0`;
  - `NOLA rep: ... MORTO` when repsync=1.
- A host kill on an NPC counts on the host.
- C6: real client shots. `SHOT real:` on B, then `SHOT(core) seq=` on A with the same seq. Head and leg hits read `osso=jnt_head` / `jnt_l_leg`.
- C7: the client goes down and gets up.
  - `NOLA LIFE: CAIDO`, then `DE PE (reanimado pelo outro jogador)` when the host stands within 3 m for 3 s;
  - otherwise `DE PE (tempo)` after 20 s;
  - while down, `#L:w` reaches the host and no #D hit applies (`caido` count).
- C19: the hidden target is parked or back. During tgtPark, `relmir` and `repfoe` skip (`alvo=estacionado` on the host chain line).

## Checks with no offline cover
- X0 Call with dotted struct paths: `BriefDamageData.Damage` and `BriefDamageData.AttackerUID.UID`. A refused call logs `ok=false chamada recusada`.
- `ForceSetHP` on an int param, `SetMovementMode` on byte params, `DisableMovement` on CharacterMovement.
- `x0::own_tag("@shots")` reaches out.json `"shots"`. The host `x0::remote().shots` carries it back.
- The OnShotSoundStart hook: `GetOwner` on the queued self, and the camera ray from `PlayerCameraManager`.
- `s2rel::get/set` (relfoe=1 / relmir=1 on A): `NOLA relfoe:` and `NOLA relmir:` lines.
- Down message: X0 screen_text added by U1 (s2x_ui.cpp, S2X_HAS_SCREEN_TEXT on); live screenshot proof by U1.
- Peer relaunch: after `PEER SESSION`, a restarted seq 1 is applied again (the seen sets were cleared).

## Mixed-mode caveats
- cpp_damage=1 with cpp_travel=0: no host-cinematic gate. The Lua cv state is not visible, so #D is not cleared and shots are not dropped during a host cinematic.
- cpp_damage=1 with cpp_lock=0: s2m_lock::shot_deny is inert, so the N3 story-NPC shot deny is off.
- links().nd_dead is always empty (nodeath is DELETE, release nodeath=0), so the batch-13 dead state never stops lifeGuard. This matches the release.
- cpp_damage=1 with cpp_bodies=0: m01::dmg() is fed only by C++ M1, so no #D events are produced.
