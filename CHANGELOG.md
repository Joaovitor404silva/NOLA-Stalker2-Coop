# NOLA: Changelog

[← Back to the overview](README.md)

## v1.1.1 (2026-10-21)

### Fixed
- **Crash about 10 seconds after launch on a fresh install** ("Fatal Error! Crashdump written to ... ue4ss"): the UE4SS settings file in the package had no engine version. The package now sets it, and the installer also fixes an existing settings file (#4).

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
