# Offline tests

One command builds and runs every suite and prints one table:

    powershell -NoProfile -File tools\run_tests.ps1

It exits 0 when there is no new failure and 1 when there is at least one. A failure that is listed in
`tests\baseline.tsv` is shown as "known" and does not fail the run.

## Options

| option | meaning |
|---|---|
| `--only a,b` | run only these suites |
| `--skip a,b` | skip these suites |
| `--list` | print the manifest and exit |
| `--full` | also run the slow `full` group (`strip_full`, all strip files instead of 30) |
| `--jobs N` | parallel suites, default min(8, cores) |
| `--timeout S` | per step timeout in seconds, default 1200 |
| `--strict` | exit 2 when a baseline row no longer fails (stale row) |

Each suite log is written to `tests\.run\<suite>.log`. Suites that need an unavailable input (the offline
oracle dirs under `D:\modding\offline\s1`, the polyhook sources of a built worktree) are shown as SKIP.
The `excl` group (`net_udp`) runs alone after the parallel suites because it uses fixed UDP ports and timing.

## Files

`suites.tsv`: suite, group (`par`, `excl`, `full`), weight (longest first), needs (comma separated paths,
`{repo}` is the worktree), steps (separated by `;;`: `bat`, `exe` or `py` followed by a path under `tests\`).

`baseline.tsv`: suite, case, reason, since. `case` is a prefix of the failure text printed by the suite.
When a baselined case passes again the runner prints "FIXED (remove from baseline.tsv)".
Adding a row is only for a failure whose cause is understood and written in `reason`.

Fixtures live in the repo (`tests\net\fixtures`, `tests\wpkg\fixtures`). Nothing reads `C:\s2coop_inst`.

## Native layer suite

`natlayer` compiles `src\s2n_nat.cpp` itself into a test with the defines, includes and libs of the
`NolaNative.vcxproj` (so the worktree must be built once). `GetModuleHandleW(nullptr)` is mapped to a fake
exe image, a stub `UE4SS.dll` is generated from the imports, and the unresolved game hooks are stubbed. It checks
the config switches, the feed buffer, the owner set, and the aggro-scorer installer (`ag_install`): refuses
other bytes, patches the three sites with near jumps to the emitted stub. It never touches a game process.

## Checklist for every new feature

Every feature needs a test (or a scenario line) for each of these four cases before it is called done:

1. Peer absent: the other side never answers (no join, no packet, no file). The feature must not hang, spam
   the log, or change local play.
2. Flag off: the flag set to 0 at boot and flipped to 0 in the middle of a session. Behaviour must be exactly
   the pre-feature behaviour, and in-flight state must be released.
3. Stale file: a bridge, save, or data file left from a previous run or an older build is present at boot.
   The feature must ignore or replace it, never trust it.
4. Second client: two clients (and a client with a different pid) at the same time. State must be per client,
   frames must carry the pid, and one client leaving must not affect the other.

Add the test to the suite of the module (or a new row in `suites.tsv`), run `tools\run_tests.ps1 --only <suite>`,
and commit the golden files with the code change.
