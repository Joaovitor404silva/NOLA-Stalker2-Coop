# Current package: stability 0.4 on upstream v1.3.2 — SOURCE ONLY

Read MERGE_0.4.md for the current behaviour and validation. Earlier notes below
are historical; old Windows build results do not validate the current source.

# Current package: stability 0.3 SOURCE ONLY

See GUEST_TRADING_0.3.md for the new guest trader behaviour and validation.
Everything below describes the preserved earlier versions.

# Current package: stability 0.2 SOURCE ONLY

See ISSUE_FIXES_0.2.md for the new fixes and validation limits. The notes below
describe the preserved 0.1 foundation; their Windows build results do not apply
to the new 0.2 changes. No new DLL is included.

# NOLA stability patch 0.1 — experimental Windows build

Based on upstream Joaovitor404silva/NOLA-Stalker2-Coop commit
802c5b54c94f3e715b74230d7d7eb7804b99f7cd (upstream v1.3.1).
Independent experimental modification, not an official upstream release.

## HOME rescue update

While downed, hold HOME continuously for 5 seconds to teleport beside the nearest
standing teammate (3D distance). Releasing early cancels the hold. Only fresh
positions from connected, living teammates qualify. The action stays disabled
while standing, during a team wipe, and during blocked cinematics.

The rescue tries eight positions 1.2 m around that teammate, at their reported
height, through K2_TeleportTo. If every candidate is blocked it leaves the player
in place. No unchecked SetActorLocation fallback is used. It does not restore
health, spend a medkit, or revive the player. Release HOME before another attempt;
a successful rescue has a 10-second cooldown. The game's window must have focus.

Collision handling and multiplayer replication require live game validation.
The engine teleport collision check does NOT detect anomaly volumes, safe floors,
or unloaded destinations; the destination cannot be guaranteed anomaly-free.
An on-screen countdown says the player will move to the nearest standing player.
Releasing HOME clears the countdown. Completion, unavailable targets, blocked
positions and cooldowns have on-screen messages. These use the existing NOLA UI
(enabled by default through uimsg), with rescue messages given higher priority.
Outcomes are also logged as NOLA RESCUE.
The Steam invitation HOME shortcut is suppressed while downed so it does not
interrupt the rescue hold; it remains available while standing.

Offline regression cases cover the full hold, early release, nearest eligible
teammate, ignoring a closer downed teammate, blocked destinations, stale/silent
teammates, rejecting use while standing, preserving downed health/state, and
preventing repeats while the key remains held. Native teleport calls are mocked
in these tests; no claim is made that in-game teleportation has been tested.

## Implemented

- Native downed players no longer die OR automatically stand up after downsecs.
  They remain protected and revivable until revived or the existing all-down
  condition fires. Existing revive range, medkit requirements and team-wipe
  settings remain in effect. Default host/client profiles enable all-down.
  Logs no longer announce timed death. downsecs/downdie remain accepted legacy
  configuration keys but do not control a native bleed-out timer in this patch.
- Direct/non-hub UDP rejects gameplay/control/save packets from endpoints other
  than the configured peer. Discovery remains separate. This is endpoint
  validation, NOT cryptographic authentication. Hub admission is unchanged.
- Retired session headers and NPC updates cannot overwrite the accepted session.
  History is bounded to 256 transitions; restarting the relay resets it.
  Unknown NPC sessions cannot replace current NPC records.
- Sender delta caches reset immediately when the local session changes.
- Receive work is limited to 256 packets or 4 ms between packets per iteration.
  One packet may still take longer; this is not a hard real-time deadline.
- Network JSON expansion is capped at 1 MiB. All decompressor output paths now
  enforce their caller's budget, including literal bytes and stored blocks.
  Generic non-network decoding keeps the previous 64 MiB default.
- CMake accepts UE4SS_ROOT instead of requiring the author's D: drive.
- Fixed a literal carriage return and Windows-only data paths in the existing
  damage test harness.

## Validation and limits

Run `ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_portable.py` on Linux
with g++. It compiles production codec/sender/assembler function bodies and
production damage logic in temporary translation units. Only the native Unreal
relation hooks are stubbed for damage tests; no in-game behavior is simulated
by those stubs. This is NOT compilation of the Windows transport or DLL.

Regression cases cover decoder boundaries and over-limit compressed messages;
old-session replay and NPC isolation; first NPC resend on reconnect; remaining
downed at 31 seconds and 10 minutes; revival after 10 minutes; disabling the old
auto-stand mode; two-player host/client paths and a three-player all-down wipe.
AddressSanitizer and UndefinedBehaviorSanitizer are used. Leak detection is
explicitly disabled because this container cannot support LeakSanitizer.

Windows x64 DLL compiled successfully on 2026-11-03 with MSVC 19.44.35229,
UE4SS revision 5e627997, UEPseudo revision 38e7171e, S2X_RELEASE=ON and
Game__Shipping__Win64 configuration. Native network and damage regression
tests passed on Windows. All 132 imported UE4SS symbols are present in the
original NOLA 1.3.1 runtime. This checks symbol availability, not all runtime ABI
or in-game behavior. No in-game testing has occurred. UDP endpoint checks and
receive budgets still require live Windows transport integration tests.
The native C++ build is the target; historical Lua fallback code is unchanged.
Upstream's exceptional instant-death limitation still applies: this patch
removes the downed timer, not every path by which the engine can kill a player.
Actual-death propagation and the existing all-down settings are unchanged.

## Build on Windows

Requirements: Windows x64; Visual Studio 2022 C++ build tools; CMake in PATH;
Python in PATH; Rust stable MSVC; RE-UE4SS with submodules initialized (upstream
specifies tested revision 5e62799). Consult DEVELOPING.md for upstream details.

From PowerShell in this source directory:

    .\BUILD_PATCH.ps1 -UE4SSRoot 'C:\modding\RE-UE4SS'

The helper first builds native network/damage tests and checks their results,
then configures and builds NolaNative with S2X_RELEASE=ON. Output is under
build-patch/native. Do not copy source files into the game as if they were a DLL.
A built DLL still requires compatibility checks against the installed UE4SS
runtime and the correct upstream v1.3.1 payload/game build 26123434.
Keep original DLL/saves available for rollback and use the same patched version
on every participant. Test two-player direct UDP and Steam before longer sessions.

## Still pending from the proposed version

Packet fragmentation/reassembly and negotiated protocol changes; authenticated
session handshakes; adaptive smoothing; event acknowledgements/exactly-once
application; world reconciliation and late-join snapshots; item/upgrade/save
persistence repair; NPC lifecycle redesign; traffic prioritization; connection
UI; four/five-player and long-session game tests. These are NOT implemented or
claimed fixed by this build. The Windows build is complete; those broader
proposals remain future work.

Original authorship and GPL-3.0 LICENSE are preserved.
