# NOLA: Features

[← Back to the overview](README.md)

Every row says honestly how far it has been checked.

| Status | Meaning |
|---|---|
| ✅ | Tested in game (live, with real game windows talking to each other) |
| 🧪 | Built, partly tested (some parts checked, or checked only on an earlier test build) |
| 🔧 | Built, not tested yet |
| ❌ | Off in this release |

"Tested" means tested by the author on test PCs, mostly with two players and a few shorter runs with three. It does not mean "proven for every mission in the game".

## Connecting

| Feature | What it does | Status |
|---|---|---|
| Host co-op / Join co-op in the main menu | Two new entries in the main menu. The host picks "Host co-op" and loads a world, the others pick "Join co-op". | ✅ |
| Connect by IP address (LAN or VPN) | Join a host on your local network, or through a VPN such as Radmin. This is what almost all testing used. | ✅ |
| Connect through Steam friends | The host opens a Steam lobby and invites a friend (overlay invite, Home key in the world). | 🧪 A Steam lobby join, leave and rejoin worked in a live test. |
| Up to 5 players | The host plus up to four friends. | 🧪 Two players tested a lot, three players tested in shorter runs (join, leave, rejoin, damage). Four and five players: not tested yet. |
| Join and land beside the host | A joining player is teleported next to the host as soon as the area around the host has loaded (about a second in the test, it used to take far longer). | ✅ |
| Leave and rejoin | When someone leaves, their body disappears for the others. When they come back there is still one body, not two. | ✅ |
| Switch between Host and Join in one session | You can go back to the menu and pick the other role without restarting the game. | ✅ |
| Solo play unchanged | Without pressing Host or Join, the game plays as normal. | 🧪 Checked on earlier test builds. |

## Shared world

| Feature | What it does | Status |
|---|---|---|
| NPCs and enemies from the host | The host runs the A-Life and the NPCs. Other players see and fight copies that follow the host's characters. | ✅ Cover animations on the copies were not perfect when last checked. |
| No invisible enemies for joiners | A joiner can no longer be attacked by enemies that exist only on their own PC and cannot be seen. | ✅ Verified in game logs (a screenshot proof was not possible). |
| NPC copies stop staring at dead targets | NPC copies on a joiner's PC no longer keep aiming at a body that is already dead. | ✅ Verified in game logs. |
| Mutants move and attack in step | Mutant speed, attack animations and the bloodsucker's cloak follow the host. | 🧪 Bloodsucker speed and the attack and cloak timing matched the host in measurements. Other mutant types, and how it looks, were not fully checked. |
| Time and weather follow the host | The joiner sees the same time of day and sky as the host. | 🧪 Worked in a live test on an earlier build. |
| Sleep together, no pause, emissions together | Sleeping needs everybody, one player cannot pause the game, emissions run for all. | 🧪 Checked on earlier test builds. Not re-checked in v1.2.0. |
| Doors, levers, breakable objects, anomalies | State changes made by one player are copied to the others. | 🧪 Checked on earlier test builds. Not re-checked in v1.2.0. |
| Padlocks and locked doors | A padlock one player breaks is broken for the other too, and the door behind it is no longer "locked" for them. | ✅ |
| Dropped items | An item one player drops lies on the ground for everybody, at the same place. Whoever picks it up first gets it, and it is gone for the others. | ✅ The case where two players grab it in the same instant was only tested offline. |
| Dead bodies and loot | One body per death, in the same place for everybody. Loot it from any player. A looted body stays where it is, with nothing left to take. | ✅ |
| Stashes and containers | Stashes open normally for every player (a bug that closed the box by itself was fixed). | ✅ |
| Traders | In V1 each player sees their own trader stock. | 🧪 By design for now. Shared stock is planned. |

## Story and quests

| Feature | What it does | Status |
|---|---|---|
| Quest progress follows the host | Everybody's journal shows the same mission and objectives as the host's. | ✅ Checked in the opening story missions, not through the whole game. |
| Quest items for everybody | A quest item picked up by one player is given to the other players too, once, with no duplicates after reloading or reconnecting. | ✅ |
| Host chooses story branches | In decisive dialogue, only the host picks the answer. A joiner who clicks one gets a short notice and then follows the host's choice, so nobody ends up on a different path. | 🧪 Checked on one radio conversation and one random branch. The large table of other branches is built but was not checked one by one. |
| Catch-up for a lagging player | If one player is behind in a mission, the missing steps are completed for them after a short wait. | 🧪 Tested for a joiner who was behind the host. |
| Cutscenes and scenes together | Everybody is brought to the scene together. | 🧪 Worked in a live test on an earlier build. |
| Fast travel together | Guide travel is meant to take everybody along. | 🔧 |

## Your character

| Feature | What it does | Status |
|---|---|---|
| Each player keeps their own character | A joiner keeps their own gear, condition, attachments, quick slots and health. The host's world is merged with the joiner's character when they join. | ✅ |
| Co-op has its own save slots | Co-op games are saved in a separate place, so your normal solo saves are not touched. The co-op slots start empty. | ✅ |
| Weapon upgrades | Weapon upgrades on your items after a join or reload. | 🔧 Not verified. See Known issues. |
| Worn gear on rejoin | A joiner's game, with their worn gear, is saved when they exit or disconnect, so it comes back when they join again. | ✅ |

## Your partner's character (v1.2)

| Feature | What it does | Status |
|---|---|---|
| Friends on the compass | Each other player shows as a marker on your HUD compass at any distance and follows them. | ✅ |
| Aim, lower, safe zones | Aiming pose for rifles and pistols, H lowers the weapon, weapon hidden in safe zones. | ✅ |
| Shooting | The game's own fire animation per weapon, on body and gun; the Mosin cycles its bolt. | ✅ |
| Reloads | Rifles, shotguns, SVD, PKP and pistols reload with the weapon in the hands. | ✅ |
| Pistol, knife, bolt, detector | Proper grips; detector in the left hand, bolt in the right. | ✅ |
| Movement | Lean, crouch transitions, vaulting, ladder climbing, feet planted when standing still. | ✅ No crouch-walk animation (the game has none for other characters). |
| Stashes and bodies | Crouches and searches for as long as a stash is open; body search animation. | ✅ |
| Grenade throw on the partner | The throw motion seen on the partner. | 🔧 Not yet tested in a live session. |

## Combat and survival

| Feature | What it does | Status |
|---|---|---|
| Joiner damages enemies | A joiner's bullets and knife hurt the host's enemies, once per hit. | ✅ |
| Joiner grenades | A joiner's grenade damages the host's enemies. | 🧪 Worked in a live test, checked in game logs only. |
| Enemies hurt every player | Enemy damage reaches joiners as well as the host. | ✅ |
| Friendly fire | Players can hurt each other, in both directions (host to joiner and joiner to host). On in V1. | ✅ |
| Downed, bleed-out and revive | At low health you drop to the ground with the camera low and no weapon in your hands, and you bleed out in about 30 seconds. A teammate stands close and holds F with a medkit to get you up, and the medkit's healing is applied to you. | ✅ |
| Everybody down means game over | If all players are downed or dead, it is game over and the game reloads, like in the stock game. | ✅ |
| Real cause on the death screen | A co-op death shows the real cause of death (or the game's default text) instead of always "shot". New in v1.2.0. | ✅ |

## Loading and sessions

| Feature | What it does | Status |
|---|---|---|
| Load together | A joiner waits on the loading screen until the host's world is up. When the host loads, reloads, or reloads after a death, the joiner follows. | ✅ |
| "Corrupt save" popup fixed | Starting Host co-op and then Continue no longer shows a false "file is corrupted" message. The host popup also tells you to pick Load or New Game when the co-op slots are empty. | ✅ |
| Session messages | A small box in the game's own tutorial style tells you when somebody joins, leaves or loses the connection. English, Portuguese and Spanish, following the game's text language. | ✅ |
| Back to the main menu when the host goes silent | If the host disappears for long enough, the joiner is taken back to the main menu. | 🔧 Not tested in game. |

## Menu and look

| Feature | What it does | Status |
|---|---|---|
| NOLA logo in the main menu | A small NOLA logo sits next to the game's logo, in the bottom corner beside the Cost of Hope badge. | ✅ |
| Menu texts in three languages | Host / Join entries and popups in English, Portuguese and Spanish. | ✅ |

## Turned off in this release

| Feature | What it would do | Status |
|---|---|---|
| NPC copy parking | Park distant NPC copies out of the way to save performance. | ❌ It failed in testing. A fix exists but has not been tested, so it is left off. |

---

[← Back to the overview](README.md)
