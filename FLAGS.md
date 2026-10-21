# NOLA: Settings (flags)

[← Back to the overview](README.md)

NOLA keeps its settings in one small text file. You can open it, switch features off, or change a few numbers (like how long you can bleed out). This page explains the settings that are safe and useful for players.

You never have to touch this file to play. The defaults are the ones NOLA was tested with.

## The settings file

- **Where it is:** `C:\nola_bridge\flags.txt` (this is the default install location).
- **How to edit it:**
  1. Close the game.
  2. Open `flags.txt` in Notepad.
  3. Change the value after the `=`, for example `friendlyfire=1` becomes `friendlyfire=0`.
  4. Save the file and start the game.
- **Format:** one `key=value` per line, no spaces and no quotes.
- **Values:** most settings are **1 = on** and **0 = off**. Some are numbers (seconds, a scale, a distance) or a word (like `steam` or `ip`).
- **Make a backup first.** Copy `flags.txt` to something like `flags_backup.txt` before you change anything. If something goes wrong you can copy it back.
- **Only change the lines on this page.** The file has many more lines. They are tuning values that the mod needs exactly as they are, so please leave them alone.
- **The installer rewrites the file.** If you run `NOLA-Setup.exe` again (reinstall or update), your changes are replaced by the defaults, and the connection lines (`net` and `netpeer`) are written again from your answers. Keep your backup and copy your changes back afterwards.

## Do all players need the same settings?

Mostly it does not matter, because **the host decides**. These settings are sent from the host to everyone who joins, and they **follow the host automatically** while you are in the host's game. They are marked with 🔗 in the tables below. Your own value comes back when you leave the session. So for a 🔗 setting you only need to change it on the **host's** PC.

For the settings without 🔗, each PC uses its own file. Most of them only matter on one side (for example the connection lines are for the player who joins by IP). If you are not sure, the simplest safe rule is: **everybody uses the same file and the same NOLA version.**

For this to work, `hostcfg` must stay on (see "Don't change" below).

## Connecting

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `net` | `steam` | How the PCs connect: `steam` (Steam friends) or `ip` (LAN, or a VPN such as Radmin). The installer writes this line for you. | Not an on/off switch. Change it to `ip` to play by IP address, or back to `steam` for Steam friends. |
| `netpeer` | your answer in the installer | IP play only: the IP address of the other PC, for example `192.168.1.20`. A joiner needs it to find the host. Not used with Steam. The installer writes this line for you. | Not an on/off switch. Type the address of the PC you want to join. |
| `maxclients` | `4` | How many other players the host accepts. The host plus 4 joiners is 5 players in total. | Use a smaller number for a smaller session. For example `2` means the host plus 2 friends. 4 is the most. |
| `steaminvite` | `1` | Steam only: when you start hosting, the Steam invite window opens by itself so you can pick friends. | You open the invite window yourself by pressing **Home** in the world. |
| `steamjoin` | `1` | Steam only: "Join co-op" looks for your Steam friends' hosted games. | "Join co-op" no longer finds Steam hosts. Leave this on if you use Steam. |
| `joinport` | `1` | When you join, you are moved next to the host. | You stay where your own game loaded you, maybe far from the host. |
| `joinm` | `10` | Distance in metres. If you are already closer to the host than this when you join, you are not moved. | A bigger number means you are moved less often. A smaller number moves you even when you are quite close. |
| `cpp_port`, `cpp_net`, `cpp_session`, `cpp_world`, `cpp_npc`, `cpp_pop`, `cpp_items`, `cpp_bodies`, `cpp_damage`, `cpp_lock`, `cpp_quest`, `cpp_save`, `cpp_travel` | `1` | ⚠️ **Don't change.** These switch on the parts of the mod that do the real work (the connection, the shared world, NPCs, items, damage, quests, saves, travel). | Co-op stops working or breaks in strange ways. |
| `natnpc` | `1` | ⚠️ **Don't change.** This is how the host's NPCs and enemies appear and move on the joiners' PCs. | Joiners no longer see the host's NPCs and enemies properly. |
| `hostcfg` | `1` | ⚠️ **Don't change.** This is the thing that sends the host's settings (the 🔗 ones) to the joiners. | Joiners keep their own values, so the game can play differently for each player. |
| `joinmenu` | `1` | ⚠️ **Don't change.** The "Host co-op" and "Join co-op" entries in the main menu. | You can no longer start or join a co-op game from the menu. |
| `menurole` | `1` | ⚠️ **Don't change.** Makes the game remember if you picked Host or Join, so it behaves the right way for that role. | The game can behave as the wrong role, and co-op breaks. |

## Shared world

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `doors` 🔗 | `1` | When one player opens or closes a door, it moves for the others too. | Doors are no longer copied. Each player sees their own door state. |
| `lockbrk` 🔗 | `1` | A padlock that one player breaks is broken for the others too. | Each player has to break the padlock themselves. |
| `dobj` 🔗 | `1` | Breakable objects (props you can smash) that one player breaks are broken for the others too. | Smashed objects are not copied. Each player sees them broken only on the PC where they were smashed. (This works together with `dobjgen`, which also needs to stay `1`.) |
| `ien` 🔗 | `1` | Switches, levers and other things you can use are copied between players. | They are no longer copied, so one player can switch something and the others do not see it. |
| `anomsync` 🔗 | `1` | When a player triggers an anomaly, it also goes off for the other player. | Anomalies only react to the players on the PC where they are. |
| `idrop` 🔗 | `1` | An item that one player drops on the ground lies on the ground for everybody. | Dropped items stay only on your own PC. |
| `ground` 🔗 | `1` | When one player picks up an item lying on the ground, it disappears for the other players. | The same item can be picked up again by the other player. |
| `lootshare` 🔗 | `1` | A dead body has one shared list of loot, so every player sees the same items on it. | Each player can see a different list on the same body. |
| `emsync` 🔗 | `1` | Emissions follow the host. | Each PC runs its own emission schedule. |
| `wxfollow` | `1` | Joiners: when the host's weather changes, yours changes to match. | Your weather can be different from the host's. |
| `nopause` 🔗 | `1` | Opening the pause menu does not freeze the game, so nobody can stop the world for everybody else. | The game pauses on your PC when you open the menu, like in solo play. |
| `sleepsync` 🔗 | `1` | When one player goes to sleep, the other goes along. If the other player refuses or does not answer, the sleeper is put back. | Each player sleeps on their own. |
| `sleepmode` 🔗 | `1` | How the other player goes to sleep. `1` = at once. `2` = they must lie down in a bed themselves (they get a short time to do it). | Not an on/off switch. Only `1` and `2` mean something. |
| `wshot` | `1` | The host sends NPC gunshots to the joiners. | Joiners do not get those shots. |
| `wvoice` | `1` | NPC voice lines are sent to the joiners and played there. | Joiners do not hear those voices. |
| `wsfx` | `1` | Joiners: the relayed NPC shots and voices are played on your PC, with the gun sound and a muzzle flash. `2` also adds the bullet trace. | You do not hear or see them. |

## Story and quests

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `qsync` 🔗 | `1` | ⚠️ Quest progress follows the host. Everybody's journal shows the host's mission. Other quest settings depend on this one. | Each player's quests go their own way, and the other quest features stop making sense. I would not turn it off. |
| `choice` 🔗 | `1` | Story choices stay with the host. A joiner who tries one is stopped and follows the host's choice, so nobody ends up on another path. | Joiners can make story choices that the host did not pick, and the players can split onto different paths. |
| `lead` 🔗 | `1` | ⚠️ When one player starts a story conversation or scene, the other player follows it and cannot start a different story conversation at the same time. | Both players can start story conversations at the same time, which can leave the story in different places. |
| `ldblk` 🔗 | `1` | While one player is in a story conversation, the other player cannot talk to people nearby. Traders stay free. Needs `lead=1`. | The other player can talk to other people during your conversation. |
| `qcatch` | `1` | If one player is behind in a mission, the missing steps are completed for them after a short wait. | A player who is behind stays behind. |
| `qshare` 🔗 | `1` | A quest item picked up by one player is given to the other players too (once, no copies). | Only the player who picked it up gets the quest item. |
| `qdrop` | `1` | Drop a quest item by holding **D** on it in the inventory. | You cannot drop quest items. |
| `cine` 🔗 | `1` | The host's cutscenes and scenes are announced to everyone. Friendly fire is also paused during a cutscene. | Joiners are not told about the host's scenes. |
| `cjoin` 🔗 | `1` | Before a story scene starts, a player who is far away is moved next to the other player (far means more than about 15 m). | Players who are far apart stay where they are when the scene starts. |
| `ftfollow` 🔗 | `1` | When one player uses a guide's fast travel, the other is taken along. **Not tested in game yet**, see [Features](FEATURES.md). | Each player fast travels on their own. |

## Your character

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `coopsaves` | `1` | ⚠️ Co-op games are saved in their own place, so your normal solo saves are not touched. | Co-op would use your normal save slots, so a co-op game can end up mixed with your solo saves. Please don't. |
| `csave`, `csmenu`, `csexit`, `cswc` | `1` | ⚠️ **Don't change.** Part of the co-op save system, and the fix for the false "corrupt save" popup. | Saving in co-op can go wrong, and the false "file is corrupted" popup can come back. |
| `n7` 🔗 | `1` | ⚠️ **Don't change.** This is how a joiner keeps their own character (gear, health, quick slots) in the host's world. | Joiners may not keep their own character. |
| `sxjoin`, `wsplice`, `wspair`, `wstrip`, `wspos`, `wpkg` | `1` | ⚠️ **Don't change.** These put the joiner's character into the host's world when they join. | Joining can fail or leave you with the wrong character. |

## Combat and survival

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `friendlyfire` 🔗 | `1` | Players can hurt each other, both ways (host to joiner and joiner to host). | Players cannot hurt each other. |
| `ffscale` 🔗 | `1.0` | How strong friendly fire is. `1.0` = full damage, `0.5` = half, `2.0` = double. | `0` = no damage between players (the same as `friendlyfire=0`). |
| `dmgin` | `1` | A joiner's bullets hurt the host's enemies. | A joiner's bullets stop hurting enemies. |
| `meleesync` | `1` | A joiner's knife and melee hits hurt the host's enemies. | A joiner's melee hits stop hurting enemies. |
| `lifeguard` 🔗 | `1` | ⚠️ The "downed" system. At low health you drop to the ground instead of dying, and a teammate can pick you up. | There is no downed state, you die in the normal way. Friendly fire and the other downed settings below stop working. |
| `downhp` 🔗 | `15` | The health level at which you go down. | Not a switch. A lower number means you have to be closer to death before you go down. |
| `downsecs` 🔗 | `30` | How many seconds you can stay down (the bleed-out time). | Not a switch. A bigger number gives your friends more time. |
| `downdie` 🔗 | `1` | When `downsecs` runs out, you die. | With `0`, you stand up on your own after `downsecs` instead of dying. |
| `reviveitem` 🔗 | `1` | A teammate stands close, holds **F** with a medkit, and you get up. | With `0`, a teammate only has to stand close to you for `revivesecs` seconds, with no medkit needed. |
| `reviveradius` 🔗 | `300` | How close the helper must be, in centimetres. `300` is about 3 metres. | Not a switch. A bigger number lets the helper stand farther away. |
| `revivesecs` 🔗 | `3` | Used only when `reviveitem=0`: how many seconds the helper must stand close. | Does nothing while `reviveitem=1`. |
| `revivehp` 🔗 | `30` | The health you get back when you stand up. | Not a switch. A bigger number means you stand up healthier. (If `reviveheal=1`, the medkit's own healing is used instead.) |
| `reviveheal` 🔗 | `1` | The healing of the medkit your friend used is applied to you when you get up. | You get up with `revivehp` health instead. |
| `downnoweap` | `1` | While you are down, you have no weapon in your hands. | You keep your weapon in your hands while down. |
| `downcam` | `1` | While you are down, the camera is low, near the ground. | The camera stays at the normal height. |
| `bothdie` 🔗 | `1` | ⚠️ If one player dies, or all players are down, the others die too and the game goes back to the last save. This is the "everybody down means game over" rule. | A death does not drag the other players down with it. |
| `bothdown` 🔗 | `1` | When all players are down at the same time, it is game over at once (without waiting for the bleed-out). Needs `bothdie=1`. | Downed players keep bleeding until their own time runs out. |
| `dieload` 🔗 | `1` | Joiners: if you die, the "load save" buttons on the death screen are blocked. Only the host loads, and everybody follows. | Joiners can click "load" on their own death screen. |

## Loading and sessions

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `lsgate`, `lshold` | `1` | ⚠️ **Don't change.** A joiner waits on the loading screen until the host's world is ready. | The joiner no longer waits for the host's world, and joining can go wrong. |
| `uitut` | `1` | NOLA's session messages (someone joined, someone left, connection lost) show in the game's tutorial-style box. | The messages stop using that box. |

## Menu and look

| Setting | Default | What it does | What happens if you turn it off |
|---|---|---|---|
| `nolatitle` | `1` | The small NOLA logo in the main menu. | The logo is hidden. |
| `nolatitleimg` | `1` | The picture of the logo. | The logo is hidden (same as `nolatitle=0`). |

## If something goes wrong

- **Put your backup back.** Close the game and copy your `flags_backup.txt` over `flags.txt`.
- **Or reinstall.** Run `NOLA-Setup.exe` again. It writes a fresh `flags.txt` with the defaults. Your saves are not touched.
- **Still stuck?** [Open an issue](https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/issues/new) and tell us which line you changed. Please see the [README](README.md) for what to include.

---

[← Back to the overview](README.md)
