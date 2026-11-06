# Stability 0.5: revive payments

Base: custom stability 0.4 merged with upstream v1.3.2.
Status: experimental Windows build compiled on 2026-11-06; native regression tests passed. In-game multiplayer validation remains pending.

## Changes implemented

- Start the medkit removal node directly instead of treating insertion into the generic item queue as a successful debit.
- Verify the inventory count fell by exactly one before publishing the revive request. Observe delayed completion without issuing another removal. After ten seconds, an unconfirmed debit is unresolved and cannot become a late revive request.
- Keep one local medkit transaction outstanding when the recipient advertises receipt support. Suppress further F attempts while awaiting its result.
- Add RA receipt and RT request-identity tags. Match recipient view, peer identity/session, request counter, target player and a process-lifetime nonce before acting on a receipt. Repeated or mismatched receipts cannot grant another medkit.
- Refund the original medkit type once after an explicit rejection, then verify the inventory increase. Never retry a possibly successful grant automatically.
- Reject competing rescuers without replacing the first rescuer or resetting the application timer. Retain per-view terminal receipts for retransmission.
- Return rejection for requests received while standing, dead, without a pawn, with the life guard disabled, or during the all-down wipe. Session changes invalidate pending requests.
- Keep unlimited downed time, all-down wipe, five-second HOME rescue and existing downed notices.

## Limits and compatibility

Both players need this code for receipt-based refunds. Older builds retain the legacy path (verified debit, but no refund acknowledgements). Capability detection is based on received RA metadata; a request made before capability metadata arrives can use the legacy path.

Missing replies, session changes, pawn changes, unconfirmed removal and unconfirmed refunds are unresolved, not proven failures. They block further medkit transactions for the current module lifetime and show an explanatory message. There is no timeout refund. There is no durable journal or automatic recovery across reloads, crashes or save rollback. Do not describe this checkpoint as crash-atomic or fully disconnect-recoverable. Restarting discards in-memory transaction state.

Inventory counts are observations, not an engine transaction identifier. Concurrent manual consumption, looting, inventory edits or save loads can interfere with those observations. Real game validation must cover these paths. A successful receipt follows the existing revive application path; it is not a new independent verification of every Unreal healing/camera operation.

The native removal/refund adapter has compiled on Windows. It has not been validated inside STALKER 2. Keep the previous installer available for rollback.

## Validation

- Actual production damage module compiled on Linux with Unreal relation hooks stubbed as in the existing portable harness; AddressSanitizer and UndefinedBehaviorSanitizer enabled. Leak detection disabled because this runtime prevents LeakSanitizer from inspecting /proc tasks.
- Existing downed-state, all-down, HOME rescue and revive scenarios passed.
- Added scenarios: delayed/unconfirmed debit, explicit rejection/refund, accepted receipt, duplicate receipt, wrong view/target/sequence/nonce, changed session, failed refund, competing rescuers, and legacy two-player topology.
- Existing network hardening and issue regression tests passed (roster/forwarding, door retries, menu transitions, compass, guest vendor scenarios).
- See validation/next-revive/RESULTS.log, scenario logs and issue-regressions.log.

Reproduce portable coverage:

    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_portable.py
    ASAN_OPTIONS=detect_leaks=0 python3 tests/hardening/run_issue_tests.py

## Still unfinished

- Look-at-player / INSERT money-transfer interface and transfer backend.
- Durable transaction reconciliation after disconnect, reload or crash.
- In-game verification of the new revive adapter and refund behavior.
- Remaining guest dialogue/trader lock cases need a reproducible NPC/location and host/guest logs; existing source fixes must not be represented as a universal solution.
- Unreproduced join crash, moving anomalies, quest and radiation-load reports remain open.
