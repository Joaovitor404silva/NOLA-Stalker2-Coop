# NOLA: Changelog

[← Back to the overview](README.md)

## Unofficial stability 0.5 — cumulative fork changes (2026-11-06)

This section describes **all cumulative custom changes in stability 0.5 compared with original NOLA v1.3.2**, not just changes since our previous installer. The upstream baseline is commit `9485e81a875e67981a8ecb92289c81eed47d3597`; the published custom build is commit `8694d062a32f2fd132f53792dc1108d357fccf52`, tagged `v1.3.2-stability.0.5`.

NOLA was created by **Joaovitor404silva (Jaumn)** and its contributors. This unofficial fork retains their authorship, history and GPL-3.0 licensing. Custom changes were prepared for dreadnaughtgaming with assistance from OpenAI Codex.

[Download the installer and matching source](https://github.com/dreadnaughtgaming/NOLA-Stalker2-Coop/releases/tag/v1.3.2-stability.0.5). All players should use the same patched build. **Windows compilation and automated tests passed; live co-op validation remains pending.**

### Downed players and notifications

- Removed native timed bleed-out and timed automatic stand-up. Downed players wait for revival or the existing all-players-down team wipe; normal revive range and medkit requirements remain.
- Legacy `downsecs` / `downdie` configuration keys no longer control a native downed timer. This does not prevent exceptional engine instant-death paths or change actual-death propagation.
- Replaced upstream's downed countdown with a persistent waiting-for-revival message and HOME rescue instructions.
- Added fresh-transition teammate-down alerts, including at a distance, without repeating every tick or after brief stale-update gaps. Labels are `HOST` / `PLAYER n`, not Steam display names.
- Retained upstream's nearby F/medkit prompt and revived-by message, displayed after the downed camera releases. Added or retained feedback for missing medkits, pending requests, stale teammate updates, confirmed refunds and unresolved transactions.

### HOME rescue / unstuck action

- While downed, hold HOME continuously for **five seconds** to move beside the nearest eligible standing teammate, selected by 3D distance.
- An on-screen countdown explains that the player will be moved. Releasing early cancels the action and clears the countdown. This rescue countdown remains; the death countdown does not.
- Excludes stale, disconnected and downed targets. Rescue is disabled while standing, during a team wipe and during blocked cinematics; the game window must have focus.
- Tries eight collision-checked positions approximately 1.2 metres around the target at the reported height. If all fail, the player stays in place; there is no unchecked position-setting fallback.
- Rescue preserves the downed state and does not heal, revive or spend a medkit. Release HOME to rearm; successful rescues have a ten-second cooldown. Result, blocked-target and cooldown messages explain the outcome.
- Suppresses the Steam invitation HOME shortcut while downed; it remains available while standing. Rescue messages receive higher UI priority through the existing NOLA message system.
- Collision checks do **not** guarantee an anomaly-free destination, safe floor or loaded destination area.

### Revive requests and medkit handling

- Holding F no longer spends another medkit every five seconds: a new key press requires release first.
- Pending requests can briefly wait for a fresh teammate update, with a bounded extra wait of three seconds. A changed rescuer session invalidates the request.
- Medkit removal is invoked directly rather than treating insertion into an item-removal queue as a successful debit. The inventory count must fall by exactly one before a revive is sent.
- Delayed removal is observed without issuing a second removal. After ten seconds an unconfirmed debit becomes unresolved and cannot turn into a late revive request.
- With receipt-capable peers, permits only one local medkit transaction at a time and blocks further F attempts until its result is resolved.
- Adds request identity (`RT`) and receipt (`RA`) tags. Receipt handling checks the recipient view, peer identity/session, request counter, target player and process-lifetime nonce. Duplicate or mismatched replies cannot trigger another refund.
- An **explicit rejection** can return one medkit of the original type; the inventory increase is checked. A possibly successful grant is never retried automatically.
- Competing rescuers no longer replace the first active request or reset its application timer. Terminal receipts are retained per view for retransmission.
- Sends rejection when the recipient is already standing, dead, without a pawn, has the life guard disabled, or is in the all-down wipe path.
- Missing acknowledgements are **not** treated as rejection. Session/pawn changes and unconfirmed debits/refunds leave the transaction unresolved, show a message and block further medkit transactions for the current module lifetime.
- Refunds require support on both sides. Older peers, or attempts before receipt capability metadata arrives, use the legacy path without refund acknowledgements. There is no durable journal or automatic recovery across disconnects, crashes, reloads or save rollback. Concurrent inventory changes can interfere with count-based verification.

### Networking and player synchronization

- Caps expanded network JSON at **1 MiB**. All decompressor output paths enforce the caller's budget, including literals and stored blocks; generic non-network decoding retains its previous 64 MiB default.
- Direct/non-hub UDP rejects gameplay, control and save packets from endpoints other than the configured peer. Discovery and hub admission remain separate. This is endpoint validation, not cryptographic authentication.
- Prevents retired session headers and NPC updates from replacing the accepted session; unknown NPC sessions cannot replace current NPC records. Retired-session history is bounded to 256 transitions and resets when the relay restarts.
- Resets sender delta caches when the local session changes, allowing the new session to resend needed state.
- Limits receive work to 256 packets or four milliseconds between packets per iteration. A single packet may take longer, so this is not a hard real-time deadline.
- Fixes the receiver's shadowed assigned-player-ID variable that could reject forwarded teammate snapshots and hide a third player. Invalid roster slots remain rejected; forwarding, self-exclusion and slot reassignment have regression coverage.

### Guest traders, technicians and dialogue locks

These are fixes to existing guest interaction paths, **not a claim that guest trading was previously impossible**.

- Failed NPC interaction restores remain pending for retry; failed disables are not recorded as successful.
- Previously blocked traders are restored when recognised later. Population vendor identity also exempts recognised traders from the dialogue lock, and held dialogue checks are refreshed as actor data arrives.
- Enables `vendtrade=1` by default for the guest role. An explicit existing `vendtrade=0` still overrides that default. Recognised traders use the existing native trade-opening path; technicians use upgrades. Host dialogue behaviour is preserved.
- Uses the interacted actor's population vendor metadata instead of relying only on the narrower vendor-reference-position radius. Matching still depends on the vendor table and nearest-actor recognition.
- Waits up to two seconds for late vendor identification; closing dialogue resets the wait.
- Prevents host-controlled story scenes from triggering automatic guest trade opening, and restores hidden dialogue answers when the native trade call fails.
- Does not broadly unlock story NPCs, give guests host quest authority, add shared merchant-stock transactions, or guarantee a fix when no interaction prompt/dialogue view appears.

### Doors and menu stability

- Failed native door lock/unlock operations remain pending with retry backoff. Only verified successful state is cached as applied. An unloaded/missing door is not marked applied when retries expire, allowing a later fresh update to retry.
- Adds generation checks to asynchronous menu transitions. Older callbacks are cancelled after a newer toggle, session end, world entry or view destruction, including save waits, animation waits and delayed popups.
- Combines those checks with upstream v1.3.2's toggle gate; cancelled current transitions and popup-free completions release the gate.

### Upstream v1.3.2 fixes included — credit to the original project

These are inherited baseline features, **not additional fixes authored by this fork**:

- Close-behind doors/areas wait for the group (upstream issue #29).
- Grenade-tripwire disarm uses deactivation instead of damage replay (#37).
- Synthetic per-view compass marker IDs and visibility/fade fixes (#22).
- Menu toggle gating and animation completion waits (#21).
- Downed/revive UI and revived-by feedback (#20), adapted here to remove the death countdown.

The upstream compass implementation replaced our earlier marker-removal-order fix. That earlier fix is not a separate feature in the current build. Our door-state retries and menu callback cancellation remain alongside upstream's fixes because they address different failure paths.

### Build, packaging and validation

- CMake accepts `UE4SS_ROOT` instead of requiring the original author's drive layout. `BUILD_PATCH.ps1` runs native network/damage tests and builds the release DLL against a supplied RE-UE4SS checkout.
- Corrected a literal carriage return and Windows-only data paths in the damage test harness; added production-logic regression scenarios for the changes above, plus upstream merge checks.
- Published a clearly labelled unofficial experimental installer and corresponding source. Retains the upstream v1.3.2 runtime/game-data payload and updates the patched DLL's installer checksum.
- Windows DLL build and native regression tests passed on 2026-11-06. Steam install, IP reinstall and uninstall passed in an isolated test folder; original files were restored byte-for-byte. This was not an automatic installation into the actual game.
- Portable tests cover network budgets/session handling, roster forwarding, downed state through ten minutes and late revival, all-down wipe, HOME rescue, revive receipts, vendor recovery, menu cancellation and door retries. AddressSanitizer/UndefinedBehaviorSanitizer were used where supported; game calls are mocked. Merge checks also cover close-behind logic, table/text consistency and trap-disarm replay. The optional real ZoneKit quest-file generator check was skipped because those external files were unavailable.
- These tests do not establish live transport behaviour, every NPC interaction, collision/anomaly safety, or four/five-player and long-session stability. See [Windows validation](validation/WINDOWS_0.5.log).

### Still unfinished / not claimed fixed

- Player-to-player money transfer, including looking at a player and pressing INSERT for an amount/Send interface.
- Durable revive transaction recovery after disconnect, crash, reload or save rollback.
- Remaining guest dialogue failures without a prompt; join crash (#31); moving electrical/fire anomalies (#30 and anomaly-related #25); quest reports (#27); radiation-on-load reports (#23).
- Proposed new fragmentation/reassembly and protocol negotiation, authenticated session handshakes, adaptive smoothing, general reliable-event acknowledgements, world reconciliation/late-join snapshots, further item/upgrade/save persistence repairs, NPC lifecycle redesign, traffic prioritisation and a new connection UI. These were discussed but were not added by this fork; this does not imply upstream lacks all related mechanisms.

### Custom version history

| Custom version | Main additions |
| --- | --- |
| 0.1 | Unlimited downed time, five-second HOME rescue, initial network hardening and portable build/test support, initially based on upstream v1.3.1. |
| 0.2 | Third-player forwarding fix; interaction restore, menu and door retries; revive feedback, held-F protection and stale/session checks. |
| 0.3 | Guest trader/technician default, metadata-based recognition, late recognition wait and failed trade-call recovery. |
| 0.4 | Merged upstream v1.3.2 while keeping unlimited downed time and HOME rescue; adapted notifications and reconciled overlapping fixes. |
| 0.5 | Verified revive medkit debit, explicit rejection receipts/refunds, duplicate protection and competing-rescuer handling; Windows installer/source release. |

Earlier detailed documents ([0.1](PATCH_NOTES.md), [0.2](ISSUE_FIXES_0.2.md), [0.3](GUEST_TRADING_0.3.md), [0.4](MERGE_0.4.md), [0.5](NEXT_REVIVE.md)) describe their original checkpoints. Their historical “source only,” “not yet built,” or missing-refund statements are superseded by this cumulative entry and the 0.5 validation record.

## Original upstream release history

The entries below retain the original author's changelog. In particular, the upstream v1.3.2 countdown description does **not** describe this fork's unlimited-downed behaviour.

## v1.3.2 (2026-11-04)

Fixes five reported bugs. Same game build as v1.3.1 (Update 2.1, Steam build 26123434). Every player must install the same version.

### Fixed
- **Guest locked out behind code doors:** when the host entered a door code (for example 2605 in Nestor's quest) and walked through, the door closed behind the host and the guest could not open it. Doors and areas that close behind the player now stay open until every player is inside, for every such door in the game. ([issue #29](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/29))
- **Guest dies after disarming the mill grenade trap:** disarming a tripwire grenade in the old mill set the grenade off on the host's side. Disarming now works the same for everyone. ([issue #37](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/37))
- **Partner icon disappears from the compass:** the partner icon no longer vanishes and comes back, near or far. ([issue #22](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/22))
- **Main menu stops responding after toggling co-op:** switching co-op on, off and on again quickly no longer freezes the menu. ([issue #21](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/21))
- **No information when a partner is down:** a downed player sees a countdown, the partner sees "Press F to revive (uses a medkit)", and everyone sees who revived whom. ([issue #20](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/20))

## v1.3.1 (2026-11-01)

Fixes [issue #18](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/18) and lost loot, and makes IP play easier for groups. Same game build as v1.3.0 (Update 2.1, Steam build 26123434). Every player must install the same version.

### Fixed
- **Joiner always hungry and sleepy:** when the joiner's game clock was ahead of the host's, the clock sync set it back, the game counted a whole day passing, and hunger and sleepiness filled up again right after eating or sleeping. The sync no longer moves the clock back. ([issue #18](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/18))
- **Items next to a picked-up item vanished:** picking up one item could hide an identical item lying close to it, for every player. Now only the item you took is removed. Fix by Sternab.

### New
- **IP play with several PCs:** the installer asks for the IP address of every PC that may host, separated by commas. Every player can type the same list, their own address included. Join co-op tries each address until a host answers; Host co-op ignores the list. See `netpeer` in [Flags](FLAGS.md).

### Removed
- The `devdlc` developer switch. NOLA never touches DLC ownership: every player needs their own copy of the game and of every DLC the host uses.

## v1.3.0 (2026-10-31)

This update is about the enemies, mutants and bodies a joiner sees, and about not losing things. It also fixes parts of [issue #16](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/16). The game build stays Update 2.1 (Steam build 26123434). Every player must install the same version.

### Fixed
#### Your character, saves and items
- **Rejoin keeps money and items:** A joiner who leaves and joins again now keeps their money and items on saves made by game patch 2.1. Before, every 2.1 save fell back to a basic restore that lost them. ([issue #16](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/16))
- **Items dropped by a joiner:** If the host loads an older save from before it picked up a joiner's drop, the drop comes back on the ground instead of being lost. Taking two stacked drops at once, or holding F to use one from the ground, now gives the right count. ([issue #16](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/16))

#### Loading and cutscenes
- **Host load freeze:** Loading a save on the host no longer hangs on a stuck loading step. The press-any-button screen after a load now closes by itself in about 30 seconds (it used to wait for a key, and joiners could be let into a frozen world).
- **Esc menu in cutscenes:** Opening the Esc menu during a cutscene no longer freezes the host's game time.

#### Enemies and mutants on a joiner's screen
- **Every nearby enemy is there:** A joiner now sees every enemy and mutant the host has near each player. Before, only the 8 nearest were sent: in a live check 7 of 15 bandits were missing, now none. ([issue #16](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/16))
- **Enemy health and death:** An enemy's health follows the host, and it dies right after the host's copy dies. Deaths now play the normal death instead of the burn animation. ([issue #16](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/16))
- **Bloodsuckers:** A bloodsucker that is cloaked for the host is cloaked for the joiner too, and visible when the host sees it. ([issue #16](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/16))
- **Enemy shots:** Joiners now see the bullets of enemies shooting, not only the muzzle flash.
- **Hits:** When a joiner shoots an enemy it flinches and bleeds like it does for the host.
- **Double enemies:** A real enemy and its copy no longer stand in the same place after a reload or a late stream-in.
- **No more rubber band:** Enemies that move on a joiner's screen are no longer pulled back to an old spot by the game.
- **Enemies around a far joiner:** When a joiner is far from the host, for example in a village, the enemies and NPCs around the joiner now appear for the joiner.
- **Leftover props:** Props left behind by a finished NPC action are removed on joiners.

#### Dead bodies
- **Missing corpses:** Bodies that were already lying near a player are now sent to every joiner. Before, some never showed up.
- **Corpses far away:** A body the game removed while a joiner was far away comes back when the joiner returns, in the same place and pose as the host's body.

#### Looks and animation
- **Floating weapons:** The weapon of a hidden NPC no longer floats in the air on a joiner's screen.
- **Enemy looks:** Vests, coats, mutant skins and other look parts now match the host's, and blood and damage marks on a body are copied.
- **Cover:** Enemies in cover keep the cover animation instead of standing up in the middle of it. Not perfect yet, see known issues.
- **Legs:** Walking and running enemies and dogs move their legs in step with the body more often. Fast running can still slide a little.

#### Story and missions
- **Host-only mission objects:** A joiner can no longer touch a mission object that the host's story runs (for example the scanner in the first mission). A short HUD text says why. The object's state (hidden, on, broken) is sent from the host to every joiner.
- **Trigger areas:** A joiner who stands inside a quest trigger area now counts for the host's quest, and the host stays inside while any joiner is in it.

### Also in this build
These changes are in v1.3.0 but were not checked in a live session yet:
- Story quests are run only by the host. Joiners get their effects, dialogs and events from the host.
- Enemy copies on a joiner's screen are neutral to everything, so they no longer fight each other or the local townsfolk.
- A wounded enemy on the host also lies wounded on the joiner, and gets up when the host's one does.
- The bar cutscene in the first story part waits for the host's dialog before the joiner moves on.

## v1.2.1 (2026-10-28)

This update makes NOLA work on the new STALKER 2 game patch (Update 2.1, Steam build 26123434). With v1.2.0 on the new patch the co-op entries were missing from the main menu. Every player must install the same version, and v1.2.1 needs the new game patch.

### Fixed
- **Game patch support:** every game address the mod uses was moved to the new patch, so all co-op features turn on again, including corpse sync and container loot sync.
- **UE4SS start crash ([issue #7](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/7)):** the two UE4SS signature files (`GUObjectArray.lua`, `ProcessLocalScriptFunction.lua`) were updated for the new patch. You no longer need to set `HookProcessLocalScriptFunction = 0`.
- **Quest sync:** the quest sync data was rebuilt from the new patch, so the quest steps the patch changed or added stay in sync between players.

## v1.2.0 (2026-10-25)

This update is about how your friends look and move in your game: the other players' characters now use far more of the game's own animations. It also fixes the problems reported in [issue #6](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/6). Every player must install the same version.

### Fixed (issue #6)
- **Movement delay and fast catch-up:** when updates from the other player arrive late, their character keeps moving smoothly instead of freezing and then racing to catch up.
- **Invisible tutorial monster:** the monster after the bunker in the tutorial could hurt and kill a joiner without being visible to them. A joiner now only takes damage from enemies the host also has.
- **Partner gunshots:** you now hear the other player's gunshots and see the muzzle flash and the shot on their character.
- **Connection drops:** a joiner no longer hangs on the loading screen when the host loads. A joiner's game is saved when they leave, and their own worn gear is restored when they join again, so they don't come back holding items they never looted.
- **Death screen:** a co-op death shows the real cause (or the game's default text) instead of always "shot".

### New
- **Friends on the compass:** each other player shows as a marker on your HUD compass at any distance, and it follows them as they move.

### Your partner's character
- **Aiming and lowering the weapon:** rifles and pistols are held up in the aiming pose; pressing H lowers the weapon into the relaxed pose. In a safe zone the weapon is hidden, as it is for you.
- **Shooting:** each shot plays the game's own fire animation for that weapon, on the body and on the gun. The Mosin shows its bolt cycle.
- **Reloads:** rifles, shotguns, the SVD and the PKP play the body and weapon reload together, and the pistol stays in the hand while reloading.
- **Pistol, knife and bolt grips:** proper pistol grip, the game's knife grip (standing, walking, crouched) and a visible bolt in hand. With a detector out, the detector is in the left hand and the bolt in the right.
- **Movement:** lean left and right, crouch and stand-up transitions, smoother blends, vaulting over obstacles instead of through them, and ladder climbing with hands on the rungs.
- **Standing still:** feet stay planted. Before, the legs kept shifting as if the ground height was changing.
- **Stashes and bodies:** a partner who opens a stash crouches and searches for as long as it is open, then stands up when it closes. Searching a body plays the search animation.

### Screenshots
Before and after shots of the partner fixes are in the [v1.2.0 folder](v1.2.0/README.md).

### Known limits
- No crouch-walk animation: the game has none for other characters, so a crouched partner slides along with still legs.
- Throwing the bolt uses the rifle grenade-throw motion.
- Not yet tested in a live session: a grenade throw seen on the partner.
- Movement smoothing over the Steam relay is in this build, but the remaining delay there was not measured (testing used LAN).
- Four and five player sessions are still lightly tested.

## v1.1 (2026-10-21)

### Fixed
- **Ghost enemies:** some enemies (the tutorial bandits, the bandits attacking Zhorik in Zalissya) showed up only for joiners, standing still with no health. They are gone now: joiners see the same enemies as the host.
- **Stash window closing:** after one player emptied a stash (for example the mobile lab stash), the next player's window kept closing. It now opens, empty.
- **Shared stashes and boxes:** items a player puts into a stash or box now show up for everyone. Taking an item removes it for everyone, and only the player who took it gets it.
- **One player per box:** while a player is looking at or using a box, the others can't open it, the same as items on the ground.
- **Items keep everything:** items put into a box or dropped on the ground keep their condition, upgrades, attachments, loaded ammo and ammo type for the other players.
- **Dead bodies:** bodies no longer vanish from a joiner's screen when the host walks far away. They stay while any player is near them.
- **Quest sync:** quests no longer drift apart between host and joiner, and the joiner no longer reloads two or three times in a row to catch up.
- **Zhorik event and story characters:** characters that a quest spawns (Zhorik and his attackers in Zalissya, and others like them) now appear correctly for joiners, so these events play out for everyone.
- **Self-contained install:** every data table the mod needs is now inside the mod itself, so nothing depends on extra files in the bridge folder.

### Known issues (planned for v1.2)
- Joiners see NPC animations reset now and then.
- On a joiner's screen, enemies that die sometimes play the anomaly death animation instead of the right one.
- The compass shows no direction marker for nearby enemies.
- Some enemy animations on a joiner's screen stutter or skip a frame.

## v1.0 (2026-10-21)
First release.

---

[← Back to the overview](README.md)
