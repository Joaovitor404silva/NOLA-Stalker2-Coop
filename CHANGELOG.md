# NOLA: Changelog

[← Back to the overview](README.md)

## v1.4.2 (2026-11-11)

A fix update. Same game build as v1.4.1 (Update 2.1, Steam build 26123434). Every player must install the same version.

### Fixed
- **Steam join:** a guest who accepted a Steam invite (or used Join co-op) waited about 20 seconds on the loading screen and went back to the main menu. The guest now connects.
- **Hamster in Zalissya:** guests can talk to him and buy from him again ([issue #32](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/32)). The same fix applies to other calm NPCs whose dialogue did not open for the guest.
- **Guest crash** when a different campaign was loaded while already in the world.
- **Faceless player copies after a teleport:** after a downed player moved beside a teammate (HOME) or any long teleport, one or two faceless copies of a player could stay standing at the destination. They are now removed, for the host and the guests.

### Known issues
- A vendor can stand a few metres from where the host sees them, and can be out of place after the guest reloads a save in the middle of a session.

## v1.4.1 (2026-11-11)

Smoother NPCs for guests, a report key, and old co-op saves back after an update. Same game build as v1.3.2 (Update 2.1, Steam build 26123434). Every player must install the same version. Includes everything from the v1.4.0-beta.1 test build ([pull request #39](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/pull/39) by dreadnaughtgaming).

### Fixed
- **NPCs freezing, sliding and teleporting for guests:** the host sent each world update as one large packet that some routers, VPNs and mobile connections drop. Updates now travel as small packets and a lost one is repaired without a resend, every received update is used, and the smoothing of NPC movement adapts to the connection. Tested at 120-200 ms with packet loss.
- **NPCs invisible for a guest when the players are far apart:** far-away NPCs no longer block the ones near you from appearing.
- **An NPC the guest's game could not move was teleported many times a second:** it is retried at longer intervals instead.
- **Old co-op saves missing after an update:** since v1.3.1 co-op games are saved in their own folder (`SaveGamesNOLA`), so campaigns saved with older versions did not show in co-op. The first time you open co-op and its save folder is empty, NOLA copies your saves into it once. Your normal saves are not changed. `csaveseed=0` turns this off.
- **Revive medkit:** it is taken only when the revive request goes out, holding F no longer uses a second one, and a rejected revive gives the medkit back.
- **Trade screen for guests:** guests can open the trade screen at vendors and technicians in more cases, and a failed trade request brings the dialogue answers back (related to [issue #32](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/32)).
- Story NPCs whose interaction was paused get it back even when the first try fails; a door lock the game refuses is retried; broken, oversized or old network packets are dropped safely; leftover main menu refreshes no longer run in the world.

### New
- **Report key Ctrl+F10:** when something goes wrong, press Ctrl+F10. NOLA writes one file to `C:\nola_bridge\reports\`, shows its name on screen, and adds what happens in the next 20 seconds. Send us that file.
- **Session log:** NOLA keeps a log of each session in `C:\nola_bridge\logs\` (the last 5 sessions). After a crash, send the newest one before you start the game again.
- **Privacy:** IP addresses, Steam IDs, your Windows user folder, PC names and player names are masked in both files. Nothing is sent anywhere by itself.
- **No bleed-out timer:** a downed player stays down until a teammate revives them with F + a medkit. If every player is down, the team restarts from the host's last save. The host can bring back a timer with `downsecs` (for example `downsecs=30`).
- **Hold HOME for 5 seconds while down** to move beside the nearest standing teammate (you still need a revive).
- **"<name> is down!"** is shown to the other players.

### Known issues
- A vendor can stand a few metres from where the host sees them, and can be out of place after the guest reloads a save in the middle of a session.

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
