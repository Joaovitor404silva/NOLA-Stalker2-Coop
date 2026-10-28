# NOLA: Changelog

[← Back to the overview](README.md)

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
