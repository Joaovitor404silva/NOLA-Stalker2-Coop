# Contributing to NOLA

Thanks for helping! Bug reports, test reports, translations, docs and code are all welcome. For reports and ideas, see [How to contribute](README.md#how-to-contribute). This page is about pull requests.

## The hard rules

A pull request that breaks one of these is closed. CI checks the first two.

1. **No game files.** Never commit anything from the game or extracted from it: `.pak`, `.utoc`, `.ucas`, `.uasset`, `.uexp`, the exe or its DLLs, `.cfg` dumps from the game's paks, game text, audio, textures or models.
2. **No binaries and no saves.** No `.dll`, `.exe`, `.lib`, `.sav`, `.zip` or `.7z`. Releases are built from source.
3. **No decompiled game code.** You may read your own decompile to understand the game. Write the mod's code yourself; never paste decompiler output, and don't commit the decompile or the name database you build from it.
4. **No third-party code without its licence.** Code you copy must have a GPL-3.0 compatible licence, and you keep its notice.
5. **No personal data.** No IPs, Steam IDs, user names or paths with your name in code, tests or logs.
6. **No cheats for other people's games, no paywalls, no telemetry.**

By opening a pull request you agree that your change is released under the [GPL-3.0 licence](LICENSE).

## How a change gets in

1. Fork the repo and branch from `main`.
2. One fix or feature per pull request. Keep it small.
3. Add or update an offline test in `tests/` that fails before your change and passes after it.
4. Run the tests (see [DEVELOPING.md](DEVELOPING.md)).
5. If you changed a table generator, regenerate the table in `data/` and commit both.
6. If players will notice the change, say so in the PR so it can go into the [Changelog](CHANGELOG.md).
7. Open the pull request and fill in the template.

`main` is protected: every change goes through a pull request, CI must pass, and the maintainer reviews and merges it. Nobody pushes to `main` directly.

## What CI checks

The `ci` workflow runs on every pull request:

- **Repo rules**: no game files, binaries, saves, or files over 5 MB.
- **Offline tests**: `tools\run_tests.ps1 --all` builds the C++ test programs with MSVC and runs every suite against `tests/baseline.tsv`. Suites that need the game, a save or a local decompile are reported as `SKIP`; that is fine.

Live tests in the real game are run by the maintainer before a release. If your change affects what players see or do, describe how you tested it in game (two PCs, or two game copies on one PC) and add screenshots if you can.

## Style

- C++ in `src/` follows the code around it: short names, one module per `s2*_` prefix.
- No commented-out code.
- Python tools run with `uv run --no-project python -I`.
- Text files keep their current line endings (the repo stores them as they are).
