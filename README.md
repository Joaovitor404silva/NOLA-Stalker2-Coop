# NOLA - No One Left Alone

**Co-op for S.T.A.L.K.E.R. 2: Heart of Chornobyl. Walk the Zone with friends, in the same world, each with your own character.**

> Status: **V1 release candidate 1** (1.0-rc1). It works and has been played a lot in testing, but it is not finished and it will have bugs. Please read [Known issues](#known-issues) before you start.
>
> Download: [Download: Nexus link]

---

## What NOLA is

NOLA is a fan-made co-op mod for S.T.A.L.K.E.R. 2: Heart of Chornobyl. One player hosts and runs the Zone. Up to five players (the host plus four friends) share that world: you see each other, fight the same enemies, loot the same bodies, follow the same story, and pick each other up when you go down.

The host's world is the real one: the host decides what the A-Life, the NPCs, the weather and the quests do. Everyone else joins that world and keeps their own character (gear, condition, attachments and health). If you do not press the co-op buttons, the game plays like normal solo STALKER 2.

NOLA is a non-commercial hobby project. It is not made, endorsed or supported by GSC Game World.

---

## Features and how well they are tested

Every row says honestly how far it has been checked.

| Status | Meaning |
|---|---|
| ✅ | Tested in game (live, with real game windows talking to each other) |
| 🧪 | Built, partly tested (some parts checked, or checked only on an earlier test build) |
| 🔧 | Built, not tested yet |
| ❌ | Off in this release |

"Tested" means tested by the author on test PCs, mostly with two players and a few shorter runs with three. It does not mean "proven for every mission in the game".

### Connecting

| Feature | What it does | Status |
|---|---|---|
| Host co-op / Join co-op in the main menu | Two new entries in the main menu. The host picks "Host co-op" and loads a world, the others pick "Join co-op". | ✅ |
| Connect by IP address (LAN or VPN) | Join a host on your local network, or through a VPN such as Radmin. This is what almost all testing used. | ✅ |
| Connect through Steam friends | The host opens a Steam lobby and invites a friend (overlay invite, Home key in the world). | 🧪 A Steam lobby join, leave and rejoin worked in a live test with a test client. A real session between two different game owners, and the friends-list invite itself, have not been tested yet. |
| Up to 5 players | The host plus up to four friends. | 🧪 Two players tested a lot, three players tested in shorter runs (join, leave, rejoin, damage). Four and five players: not tested yet. |
| Join and land beside the host | A joining player is teleported next to the host as soon as the area around the host has loaded (about a second in the test, it used to take far longer). | ✅ |
| Leave and rejoin | When someone leaves, their body disappears for the others. When they come back there is still one body, not two. | ✅ |
| Switch between Host and Join in one session | You can go back to the menu and pick the other role without restarting the game. | ✅ |
| Solo play unchanged | Without pressing Host or Join, the game plays as normal. | 🧪 Checked on earlier test builds. |

### Shared world

| Feature | What it does | Status |
|---|---|---|
| NPCs and enemies from the host | The host runs the A-Life and the NPCs. Other players see and fight copies that follow the host's characters. | ✅ Cover animations on the copies were not perfect when last checked. |
| No invisible enemies for joiners | A joiner can no longer be attacked by enemies that exist only on their own PC and cannot be seen. | ✅ Verified in game logs (a screenshot proof was not possible). |
| NPC copies stop staring at dead targets | NPC copies on a joiner's PC no longer keep aiming at a body that is already dead. | ✅ Verified in game logs. |
| Mutants move and attack in step | Mutant speed, attack animations and the bloodsucker's cloak follow the host. | 🧪 Bloodsucker speed and the attack and cloak timing matched the host in measurements. Other mutant types, and how it looks, were not fully checked. |
| Time and weather follow the host | The joiner sees the same time of day and sky as the host. | 🧪 Worked in a live test on an earlier build. |
| Sleep together, no pause, emissions together | Sleeping needs everybody, one player cannot pause the game, emissions run for all. | 🧪 Checked on earlier test builds. Not re-checked on this release candidate. |
| Doors, levers, breakable objects, anomalies | State changes made by one player are copied to the others. | 🧪 Checked on earlier test builds. Not re-checked on this release candidate. |
| Padlocks and locked doors | A padlock one player breaks is broken for the other too, and the door behind it is no longer "locked" for them. | ✅ |
| Dropped items | An item one player drops lies on the ground for everybody, at the same place. Whoever picks it up first gets it, and it is gone for the others. | ✅ The case where two players grab it in the same instant was only tested offline. |
| Dead bodies and loot | One body per death, in the same place for everybody. Loot it from any player. A looted body stays where it is, with nothing left to take. | ✅ |
| Stashes and containers | Stashes open normally for every player (a bug that closed the box by itself was fixed). | ✅ |
| Traders | In V1 each player sees their own trader stock. | 🧪 By design for now. Shared stock is planned. |

### Story and quests

| Feature | What it does | Status |
|---|---|---|
| Quest progress follows the host | Everybody's journal shows the same mission and objectives as the host's. | ✅ Checked in the opening story missions, not through the whole game. |
| Quest items for everybody | A quest item picked up by one player is given to the other players too, once, with no duplicates after reloading or reconnecting. | ✅ |
| Host chooses story branches | In decisive dialogue, only the host picks the answer. A joiner who clicks one gets a short notice and then follows the host's choice, so nobody ends up on a different path. | 🧪 Checked on one radio conversation and one random branch. The large table of other branches is built but was not checked one by one. |
| Catch-up for a lagging player | If one player is behind in a mission, the missing steps are completed for them after a short wait. | 🧪 Tested for a joiner who was behind the host. |
| Cutscenes and scenes together | Everybody is brought to the scene together. | 🧪 Worked in a live test on an earlier build. |
| Fast travel together | Guide travel is meant to take everybody along. | 🔧 |

### Your character

| Feature | What it does | Status |
|---|---|---|
| Each player keeps their own character | A joiner keeps their own gear, condition, attachments, quick slots and health. The host's world is merged with the joiner's character when they join. | ✅ |
| Co-op has its own save slots | Co-op games are saved in a separate place, so your normal solo saves are not touched. The co-op slots start empty. | ✅ |
| Weapon upgrades | Weapon upgrades on your items after a join or reload. | 🔧 Not verified. See Known issues. |

### Combat and survival

| Feature | What it does | Status |
|---|---|---|
| Joiner damages enemies | A joiner's bullets and knife hurt the host's enemies, once per hit. | ✅ |
| Joiner grenades | A joiner's grenade damages the host's enemies. | 🧪 Worked in a live test, checked in game logs only. |
| Enemies hurt every player | Enemy damage reaches joiners as well as the host. | ✅ |
| Friendly fire | Players can hurt each other, in both directions (host to joiner and joiner to host). On in V1. | ✅ |
| Downed, bleed-out and revive | At low health you drop to the ground with the camera low and no weapon in your hands, and you bleed out in about 30 seconds. A teammate stands close and holds F with a medkit to get you up, and the medkit's healing is applied to you. | ✅ |
| Everybody down means game over | If all players are downed or dead, it is game over and the game reloads, like in the stock game. | ✅ |

### Loading and sessions

| Feature | What it does | Status |
|---|---|---|
| Load together | A joiner waits on the loading screen until the host's world is up. When the host loads, reloads, or reloads after a death, the joiner follows. | ✅ |
| "Corrupt save" popup fixed | Starting Host co-op and then Continue no longer shows a false "file is corrupted" message. The host popup also tells you to pick Load or New Game when the co-op slots are empty. | ✅ |
| Session messages | A small box in the game's own tutorial style tells you when somebody joins, leaves or loses the connection. English, Portuguese and Spanish, following the game's text language. | ✅ |
| Back to the main menu when the host goes silent | If the host disappears for long enough, the joiner is taken back to the main menu. | 🔧 Not tested in game. |

### Menu and look

| Feature | What it does | Status |
|---|---|---|
| NOLA logo in the main menu | A small NOLA logo sits next to the game's logo, in the bottom corner beside the Cost of Hope badge. | ✅ |
| Menu texts in three languages | Host / Join entries and popups in English, Portuguese and Spanish. | ✅ |

### Turned off in this release

| Feature | What it would do | Status |
|---|---|---|
| NPC copy parking | Park distant NPC copies out of the way to save performance. | ❌ It failed in testing. A fix exists but has not been tested, so it is left off. |

---

## Known issues

- **Items a joiner picked up after the host's last save** come back as fresh items without their upgrades when the host reloads.
- **Weapon upgrades** have not been verified after a join or reload.
- **One huge hit** (for example a grenade at close range) can kill a joiner outright, without the downed state.
- **The host going silent** is not tested in game, so what a joiner sees then may not be right yet.
- **Mutants:** copies of mutants on a joiner's PC can look slightly off, for example feet still moving a moment after the mutant has stopped.
- **An extra body** of another player can sometimes be seen after a very long jump (a long teleport).
- **Messages:** the NOLA message box borrows the game's tutorial box, so a game tutorial hint can replace a NOLA message.
- **Quitting the game while Steam is running** used to crash the game on exit (no data is lost). A fix is included in this release but has not been confirmed in game yet.
- **Dead bodies:** a body that was already emptied before the host reloaded can become lootable again after the reload.
- **Cover:** NPC copies did not always show the right cover animation when it was last checked.
- **Four or five players, a real Steam session and long play sessions** have not had proper testing yet.

If something breaks, please say what you were doing. Back up your saves before you try it.

---

## Roadmap

### V1 (now: release candidate 1)
Host and join from the main menu, Steam or IP, up to 5 players, a shared world with host-run NPCs, shared drops, bodies, locks and quest items, a story that follows the host, your own character kept, downed and revive, friendly fire, loading together, three languages.

### V2 (planned)
- The joiner is placed beside the host before the world loads, so there is no jump at all.
- A fix for the occasional extra body.
- A test of swapping host and joiner roles with a joiner's own save.
- A fix for NPC copy parking.
- Proper 3, 4 and 5 player live tests.
- A real two-owner Steam test.
- Keep weapon upgrades on restored items.
- Shared trader stock.

### Ideas (studied, not promised)
- Voice chat on one key (close range and radio).
- Harder enemies when more players are in the Zone.
- Shared map waypoint and teammate markers on the PDA map.
- A settings page in the game and a connection indicator.
- Travelling together and regrouping.
- Companions that take orders.
- Moving the A-Life around when players are far apart.

---

## Requirements and how to play

**You need**
- The Steam version of S.T.A.L.K.E.R. 2: Heart of Chornobyl on Windows 10 or 11, Steam build 25382007 (with the Cost of Hope DLC). Other stores and other builds are not supported. A game update can break NOLA until it is updated.
- Every player on the same NOLA version.
- For Steam play: every player is a Steam friend of the host, with the Steam overlay on. For IP play: the PCs can reach each other on the same network or through a VPN.

**Quick start**
1. Close the game. Run the NOLA installer on every PC (it asks for administrator rights). Choose Steam friends or IP address.
2. Start the game. The main menu now has **Host co-op** and **Join co-op**.
3. The host picks Host co-op and then loads a world (Continue, Load or New Game).
4. The others pick Join co-op. With Steam, the host presses the Home key in the world to open the invite window. With IP, the joiner connects to the address chosen at install.
5. To play solo, ignore both entries.

The installer keeps a backup of every file it replaces. Running it again lets you uninstall and puts everything back. It never touches your saves. The download contains full install notes.

---

## FAQ

**Is it safe for my saves?**
Co-op games use their own save slots and the installer does not modify your saves. Still, this is a release candidate: back up your saves first.

**Do all players need the DLC?**
The mod was built and tested on the build that includes the Cost of Hope DLC. Setups without it have not been tested.

**Can I use it with other mods?**
NOLA ships its own copy of UE4SS. If you already use UE4SS mods, your own settings and mod list are kept and only the NOLA line is added. Other mods that change the same game data may clash, and this has not been tested.

**Why only the Steam version?**
Only the Steam build was used for development and testing.

**Will there be a final V1?**
Yes, after more testing. The V2 list above is what comes after that.

---

## Credits, licence and disclaimer

- **Jaumn**: creator of NOLA. Design, programming, testing and many nights in the Zone. NOLA started with one simple wish: to walk the Zone with friends instead of alone.
- **The UE4SS team**: the mod loader that lets mods like this run inside Unreal Engine games. NOLA would not exist without it. UE4SS (UE4SS-RE project) is released under the MIT licence, and the licence file is shipped with NOLA.
- **GSC Game World**: for creating the Zone, keeping the spirit of the series alive, and the official modding tools that help the community understand the game.
- **Testers and friends**: thanks to everyone who jumped into broken builds and reported every bug. Every fix exists because someone tested it first.
- **The STALKER community**: to every modder who shared knowledge and every player who ever asked "can we play this together?". This one is for you.
- S.T.A.L.K.E.R. 2: Heart of Chornobyl is the work of GSC Game World. NOLA is an unofficial, non-commercial fan project. It is not affiliated with, endorsed by or supported by GSC Game World or its publishers. All game names, art and trademarks belong to their owners. NOLA does not contain or redistribute the game, and you need your own copy.
- NOLA is a free hobby and study project. It is provided as it is, with no warranty. Use it at your own risk.
- **AI disclosure:** NOLA was developed with heavy help from AI coding assistants (Claude, by Anthropic) under the author's direction, including code, tests and documentation. The author set the goals, ran the game tests and decided what shipped.
