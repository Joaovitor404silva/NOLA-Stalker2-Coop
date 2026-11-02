# M8 story lock live checks (cpp_lock=1), nothing here has run on a game

Pair 1: host A and client B, flags lead=1 ldblk=1 choice=1 dmir=0 vendtrade=0 (release set), A or B on C++ (wire is unchanged: tag #LK, same log text).

1. C9 / b109: A opens a story dialog (any non-vendor NPC with quest lines). Log on A: `NOLA lead: trava #N tomada (dlg em x,y,z)`; out.json carries `#LK:h,...,dlg,...`. B walks to the same NPC: B log `NOLA lead: NPCs de historia sem interacao aqui (total n): o outro lado lidera`, the NPC prompt is off on B (no F prompt), no dialog opens.
2. b127: A closes the dialog. B log `NOLA lead: NPCs de historia liberados (o outro lado soltou a trava)`, prompt back within 1-2 s. A dialog lock left open must also end by itself after ldwatchs (600 s): log `watchdog b127`.
3. Held fallback: B opens a dialog in the same second A does (lock event not yet arrived). B log `dialogo aberto com o outro lado liderando (dlg #N): SEGURADO (respostas escondidas)` then `respostas escondidas`, answers hidden, `dialogo segurado fechado (UIDialogClose) ok=true` after ldclose (4 s). When A releases first: `respostas devolvidas (o outro lado soltou a trava)` and B takes the lock.
4. Tie: both open at once. Client log `empate: os dois tomaram a trava ... o HOST vence, o cliente segue`.
5. Vendor free: B talks to a trader (Rostok bartender, or the Zalissya trader) while A holds a lock. B log `dialogo com vendedor: livre, sem trava (vendtrade/ldblk)`, normal dialog and trade work, no NPC of a vendor within vendtr 1500 cm is blocked.
6. Trigger lock: A walks into a branch trigger (s2u_choice trig tier 1). Log `trava #N tomada (trig ...)` and the lock ends by itself after ldtrig 3 s (`solta: tempo`).
7. N3 choice: B shoots a branch NPC while A leads: A log `tiro em NPC de historia <guid> do cliente NAO aplicado`. B shoots with no lock: `tiro do cliente ... aplicado: o cliente lidera 3 s`. B pulls a lever from the use table: A log `uso ... do cliente NAO aplicado`; B prompts of branch usables near B are off (`NOLA choice: ... bloqueado no cliente`).
8. ldsave: after load, `NOLA ldsave: pawn #1 +8 s: ...` and `+33 s` lines, registro N/U counts; ledger file bridge\s2_blk_ledger.txt written.
9. No widget walks: heapchk plugin and the dm70 crash class (access violation 0x18 around W_DialogueView_C) must stay silent for a 30 min session; the module only reads IsInViewport / IsVisible, AnswerContainer GetVisibility / SetVisibility (collapsed = 1), UIDialogClose, UIDialogOpenTrade / UIDialogOpenUpgrade, ButtonHintTrade GetVisibility.
10. Fight bench with cpp_lock=1 on both: every row that was PASS stays PASS.

Needs X0 before row 7 can pass: Call::raw (see PORT_LOG, M8 integrate block). Without it the host cannot resolve a branch NPC from its placeholder guid, so shots are never denied (the C++ build compiles without S2X_CALL_RAW and returns no actor).

Unmeasured by me (offline): CppMediator GetGUID / GetActorByGuid param names (Actor, TargetGUID) come from the object dump; whole ProdWorld (x0::Call / prop reads on BP_AI_Human_C, InteractableObject, InteractionComponent, BedView, W_DialogueView_C) has only been compiled, never run.
