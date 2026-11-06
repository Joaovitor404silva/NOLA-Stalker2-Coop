# NOLA stability 0.3 — guest trading source update

Includes all stability 0.2 fixes and the original unlimited downed time and
five-second HOME rescue. This is experimental SOURCE ONLY, not an installable mod.

## New behaviour

- The client role now enables vendtrade=1 by default. When a guest interacts with
  a recognised local trader, the existing native trade-opening path runs; for a
  technician it opens upgrades. The host keeps its normal dialogue behaviour.
- Dialogue-lock exemption and trade-opening use the interacted actor's vendor
  metadata from the population module. They no longer depend on that actor being
  within the narrower vendtr radius of its reference position. Vendor recognition
  still relies on the mod's vendor table and nearest-actor matching; this is not
  a new engine-provided trader ID system.
- Guest trade opening waits up to two seconds for vendor identification, including
  when the actor itself is already available. Closing the dialogue resets that wait.
- A host-controlled story scene cannot trigger automatic guest trade opening.
- If the native trade call fails, hidden dialogue answers are restored immediately.

The failed NPC-block restore retry and late vendor-unblocking fixes from 0.2
remain in place. Story NPCs are not broadly enabled and host quest authority is
preserved. Guest trade does not grant host quest-dialogue control.

## Validation

The issue regression runner passes all previous cases and seven new scenarios:
guest trader, guest technician, ordinary story NPC exclusion, host scene exclusion,
late vendor recognition, failed trade-call recovery and unchanged host behaviour.
The population suite also passes, including vendor metadata and rejection of
unselected/invalid actors. Tests exercise production logic with mocked game calls;
they do not establish that every reported guest interaction failure is fixed.

Commands (Python 3 and C++20 g++):

    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_issue_tests.py
    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_portable.py

The unchanged damage/network hardening suite last passed for 0.2. New trader and
population results are included in validation/. No new Windows DLL or installer
has been built for 0.3, and this version has not been tested in-game.

## Remaining limitations and live check

The native NPC interaction still needs to open a dialogue view. A guest with no
prompt or no response may have a different interaction/actor problem; this patch
cannot bypass an absent actor or diagnose that case without the guest log.
Only recognised vendors are supported. This does not add host-authoritative
shared merchant stock or transactional inventory replication.

After building, use matching versions for all players. Test a guest at a trader
and technician, close/reopen each screen, verify money/items and save/reload,
and verify that story scenes cannot be taken over. If interaction still does
nothing, capture the GUEST UE4SS.log immediately after the attempt and name the
trader and whether an interaction prompt appeared.

An existing explicit vendtrade=0 configuration overrides the guest-role default.
The original 0.1 packaging templates are retained and must be updated before
packaging any new 0.3 DLL. Do not use this source ZIP as a game install.
