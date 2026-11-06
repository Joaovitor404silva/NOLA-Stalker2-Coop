# NOLA stability 0.2 — experimental source update

Based on upstream v1.3.1, commit 802c5b54c94f3e715b74230d7d7eb7804b99f7cd.
Built on our stability 0.1 source; previous changes were compared before editing.
This archive is SOURCE ONLY. It contains no new DLL or installer. The previously
built 0.1 installer does not include these new fixes. No upstream issues were
closed and no changes were pushed to GitHub.

## Implemented and tested

| Issue | Change | Limit of the claim |
|---|---|---|
| #17 Missing third player | Fixed a local `you` variable shadowing the receiver's assigned player ID. Forwarded teammate snapshots can now pass the receiver's valid-slot check. Invalid roster slots are rejected. | Actual receiver logic tested for slots 1, 2 and 4, forwarding, self exclusion and reassignment. Three-player live validation remains required. |
| #32 Guest vendors/dialogue lock | Failed NPC interaction restores remain pending for retry. Failed disables are not tracked as successful. Already-blocked traders are restored when identified later; population vendor identity also exempts them. A held dialogue rechecks vendor proximity when actor data arrives. | Addresses concrete lock failures; does not establish why every trader fails on the reported guest. Requires guest log and live testing. Does not indiscriminately unlock story NPCs. |
| #22 Compass disappearance | Remove all old UID markers before adding new markers when peer slots move. | Fixes slot swaps/compaction; intermittent two-player disappearance could have another cause. |
| #21 Menu toggling | Generation tokens cancel obsolete asynchronous menu-settle callbacks after newer toggles, session end, world entry or view destruction. | Prevents stale callbacks; does not prove all menu freezes resolved. |
| #20 Revive instructions | Contextual F/medkit prompt; feedback for missing medkit, failed item use, request sent, stale teammate data and recipient revival. | Uses existing NOLA UI settings. Actual engine-dead players are not made revivable. |
| #25 Revive reliability | Holding F no longer consumes another medkit every five seconds. Requests can briefly wait for fresh teammate data; a changed rescuer session invalidates the pending request. | Partial mitigation. There is still no acceptance/refund transaction; disconnects and rejected requests can still cost a medkit. Fire-anomaly behaviour is unchanged. |
| #29 Door state | Failed native lock/unlock applications stay pending with existing retry backoff. Only verified state is cached as applied. A missing door is not marked applied when retries expire, so a later fresh update can retry. | Mocked rejected-unlock case fails before and passes after. Scripted code-door/quest/travel behaviour still needs the affected save. |

## Preserved from 0.1

Unlimited downed time until revival or the existing all-down team wipe; HOME held
for five seconds with progress text, cancellation and nearest-standing-player
rescue; existing network input/decompression limits, old-session filtering,
reconnect cache reset and receive budget. All players should use the same build.
Rescue collision checks do not guarantee an anomaly-free destination.

## Remaining reports

- #31 join crash: needs the guest crash folder/minidump, UE4SS log and game version.
  The module-only access violation is insufficient to identify a safe root fix.
- #30 moving electrical anomaly and the anomaly portion of #25: existing event
  sync does not provide movement/target authority. Need exact location/actor and
  host/guest observations; blindly enabling anomaly sync is not a solution.
- #27 quests: retest on current upstream base, then compare host quest state and
  guest objective UI using the affected save. No blanket quest completion added.
- #23 radiation on load: needs affected save/join logs. Legitimate radiation is
  not cleared unconditionally.

## Validation

Run from the source root with Python 3 and g++ supporting C++20:

    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_portable.py
    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_issue_tests.py

Both passed on 2026-11-04. Codec, receiver and damage tests use AddressSanitizer
and UndefinedBehaviorSanitizer. Native engine calls are mocked. The issue runner
extracts the actual production receiver, menu-settle and door-lock functions;
lock and compass tests compile the production logic. Test logs are in validation/.
The earlier interaction/compass review also compared 44 existing lock scenarios.

The existing Windows BUILD_PATCH.ps1 builds native network/damage tests and the
DLL against a supplied RE-UE4SS checkout. It does not run the Linux issue runner.
The new full Windows DLL has NOT been compiled or exercised in-game: the build
PC was offline. Do not install this source ZIP as a game mod.

## Next live checks

Use a backed-up save and matching builds on every player. Test three players on
both direct and Steam connections, guest trader dialogue and trade, compass after
reconnect, rapid menu toggles, revive with held F and delayed updates, five-second
HOME rescue, team wipe and the reported code door. Capture the guest UE4SS.log
immediately after a failed vendor attempt, and both logs for the door/travel case.

The tools/packaging templates are preserved from 0.1 and still name that version.
They must be updated when packaging a newly built 0.2 DLL; they are not a 0.2 installer.
