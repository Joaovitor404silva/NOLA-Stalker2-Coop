# NOLA stability 0.4 — v1.3.2 merged with custom changes

Experimental SOURCE ONLY. No new DLL/installer is included or claimed tested.
Upstream release: https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop/releases/tag/v1.3.2
Pinned commit: 9485e81a875e67981a8ecb92289c81eed47d3597.
Merged with our stability 0.3 source; prior source archives remain unchanged.

## Included upstream changes

- Door/area close-behind logic waits for the group (#29), with its new module,
  generated trigger table, native hooks, flags, build checks and build wiring.
- Grenade-tripwire disarm uses the deactivation path rather than damage replay (#37).
- Synthetic per-view compass markers and fade/visibility fixes (#22).
- Menu toggle gating and waiting for button animations before rebuilding (#21).
- Downed UI, revive prompt and revived-by feedback (#20), adapted below.
- Other files and changes from the v1.3.2 tag are included, not just release-note fixes.

## Downed behaviour — deliberately different from upstream

There is NO bleed-out countdown, timed death or timed automatic stand-up.
The player remains down until revived or the existing all-down team wipe fires.
The persistent message says the player is waiting for revival and explains HOME.
Connected teammates receive a HOST/PLAYER-number-is-down notification on a fresh
state transition, including at a distance. It does not repeat every tick or after
brief stale-data gaps. These are the mod's existing player labels, not Steam names.
The nearby rescuer gets the upstream F/medkit prompt. The revived player receives
upstream's revived-by message after the downed camera releases.

Holding HOME for FIVE seconds still shows rescue progress and moves the downed
player beside the nearest standing teammate, with release cancellation and the
existing collision checks/cooldown. This rescue countdown is intentionally kept;
only the downed death/automatic-stand countdown is removed.

## Overlap decisions

- Upstream's compass design replaces our older marker-removal-order patch: markers
  now have distinct per-view synthetic IDs, so the previous UID collision no longer
  applies. The upstream compass tests replace our obsolete UID tests.
- Upstream's menu animation/toggle fix is combined with our callback generations.
  Generation checks cover save waiting, animation waiting and delayed popups.
  Current cancelled transitions and popup-free completions release the toggle gate.
- Upstream revive UI replaces our duplicate nearby prompt and generic success text.
  Our pending-request, missing-medkit and item-use-failure feedback remains, along
  with held-F protection, stale-data retry and rescuer-session checks.
- Our verified door-state retry remains alongside upstream close-behind logic:
  they address different failure paths.
- Network hardening, the third-player forwarding fix, vendor interaction restore
  retries and the 0.3 guest trader changes remain included.

## Validation

Run from the source root (Python 3, g++ with C++20):

    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_portable.py
    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_issue_tests.py
    python3 tests/hardening/run_upstream_merge_tests.py

Portable production-logic tests cover networking, unlimited downed time through
600 seconds, late revival, all-down wipe, five-second HOME rescue, named slot
notifications without spam, revive feedback, menu callback cancellation, vendor
logic and compass planning. Upstream checks cover 47 close-behind cases, table
generation, text consistency/translations and the grenade-disarm replay scenario.
Native game calls are mocked. The generator's optional real quest-file check is
skipped because external ZoneKit data is unavailable. Logs are in validation/.

This is not a full Windows or live-game test. BUILD_PATCH.ps1 still builds the
Windows native network/damage tests and DLL against a supplied RE-UE4SS checkout;
the three Python runners above cover additional portable merge tests.
Unmodified upstream snapshot files can still describe stock timed-downed behaviour;
use the custom hardening assertions for the intended unlimited-downed behaviour.

The packaging templates under tools/packaging are legacy 0.1 templates and must
be updated before making a new installer. Do not install this source ZIP directly.
All players must eventually run the same newly compiled custom build.

## Still outstanding

Guest dialogue failures without an interaction prompt are not proven solved.
Crash, anomaly movement, quest and radiation investigations remain outstanding.
Revive failures still lack a transactional medkit refund. Player-to-player money
transfer and the INSERT interface were discussed but have NOT been implemented.
No upstream issue was closed and nothing was pushed to GitHub.
