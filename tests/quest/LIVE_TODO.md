# M7 quests: live checks still needed

Nothing below has run in a game. Offline coverage: parity.py, 15 scenarios match the real s2core ranges (md5 0b56e1b5) line for line, file for file.

## Setup
- C++ run: `cpp_io=1 cpp_quest=1` on both sides, tests\quest\lua_guard.patch applied. X0 must register the `s2q` console handler first (PORT_LOG M7 X0 need 1), or s2q commands do nothing.
- Lua run: the same with `cpp_quest=0`. Compare the `NOLA quest`, `qitem`, `qpick`, `qdrop`, `qdiv`, `trig`, `qreward`, `rb` log lines and the x1 tags QN W J QD S B QI QP in out.json.

## Fakehost scenarios (s2coop\tools\fakehost_scenarios), C++ vs Lua equal
- 01_catchup_first_join: #S catch-up digest, client replays once, cuDone set.
- 02_live_steps_then_client_load: live #J steps, then a client load: the quest log cut and the replay after the reload.
- 07_host_rollback_client_ahead: #B host gen bump, client rbWait, loadLastSave, rb_done.
- 10_host_relaunch_older_save: campaign id / rbGen after a host relaunch, quest_log rewrite.
- 11_client_load_no_leave_save: questRollback on the client, qlOk.

## Rows
- C8 quest progress sync: one quest step on A shows on B (journal and markers); quest item given on A lands on B too (b164); a trigger walked by B fires on the host once (b98 trigself, one `QN` per seq).
- C17 desync detection: qdiv A. Let B miss one host journal step (drop its #J for one step, e.g. B paused through it), the detector logs the divergence and repairs it (qdFix) or rolls (qdRoll) as Lua does.

## Native behaviour only a game can show
- Hold-D drop: hold D over a quest item in the inventory (key_hold.ps1). The qdrop hold cycle must call OnDropItemHoldTriggered once. Compare x0::key_down with PlayerController:IsInputKeyDown(FKey D) (X0 need 3).
- qpick: lead dialog on A, B pick waits (lead_dlg / host_dlg_open from M8).
- qdiv journal read through CppMediator (qdivj): the counts match the journal UI.
- GameTimeSeconds read live (Env::game_secs) vs the main.lua cached S2_gameSecs: the qreward and qdiv timings line up.
- XCreateItemInInventoryByID reward items (qreward #W) arrive once on the receiver, no duplicate after a reload.
