# Developing NOLA

[← Back to the overview](README.md)

## Requirements

- Windows 10/11 x64 and your own copy of S.T.A.L.K.E.R. 2 on Steam (game build 26123434 for v1.3.x).
- Visual Studio 2022 Build Tools (MSVC, CMake, Ninja) and Rust stable (MSVC toolchain), both needed by RE-UE4SS.
- A [RE-UE4SS](https://github.com/UE4SS-RE/RE-UE4SS) checkout (tested at `5e62799`).
- [uv](https://docs.astral.sh/uv/) for the Python tools and tests, Node.js 20+ for the RE helper scripts.

## Folder setup

Several scripts still use the original dev machine's absolute paths. The easiest fix is to create these junctions (run in an admin `cmd`), or change the paths in the scripts:

| Expected path | Point it at |
|---|---|
| `D:\modding\cpp\S2CoopPort` | this checkout |
| `D:\modding\cpp\RE-UE4SS` | your RE-UE4SS checkout (used by `cppbuild/CMakeLists.txt`) |
| `<game>\ModWork` | `modwork\` in this checkout |

```
mklink /J D:\modding\cpp\S2CoopPort <this checkout>
mklink /J D:\modding\cpp\RE-UE4SS <your RE-UE4SS checkout>
mklink /J "<game>\ModWork" <this checkout>\modwork
```

Pull requests that replace a hard-coded path with a relative one are welcome.

## Build the DLL

```
cppbuild\build.bat
```

The output is `cppbuild\Output\...\Game__Shipping__Win64\` (`main.dll`). `tools\nola_build_fast.bat` and `tools\nola_build_cached.bat` do incremental builds, and `tools\build_rel.ps1` makes a release build.

The DLL embeds the tables in `data/`. If you change a generator's input, regenerate its table first: `uv run --no-project python -I tools\gen_<table>.py`. How runtime tables work is in [docs/RELEASE.md](docs/RELEASE.md).

## Run the tests

```
powershell -File tools\run_tests.ps1 --all --jobs 4
```

The runner reads `tests/suites.tsv` (one row per suite: steps, needed files, the source globs that trigger it) and compares the result with `tests/baseline.tsv`. A suite whose `needs` files are missing prints `SKIP`. CI runs the same command on every pull request.

Some suites need things you make yourself and never commit: a save from your own game, the game exe, a local decompile, or the quest node sources unpacked from your game's paks. They `SKIP` until you have them.

## Paks and the release package

- `modwork\s2coop\tools\build_paks.py` builds the `zzzzzz_*` override paks from `modwork\s2coop\paksrc\`. That folder holds files that start as copies of the game's own `.cfg` files, so it is **not** in this repository. Unpack the files you need from your own game, and only commit the scripts.
- `modwork\s2coop\tools\build_release_cpp.py` assembles the release payload (UE4SS, the DLL, the paks and `flags.txt`) and the installer in `modwork\s2coop\tools\installer\`.

## Install a dev build

Copy the payload into `Stalker2\Binaries\Win64\` and the pak into `Stalker2\Content\Paks\~mods\`, like the [manual install](INSTALL.md). `tools\hotreload.ps1` swaps a new DLL into a running game.

For live debugging, [Game Crawler](https://github.com/Joaovitor404silva/Game-Crawler) (an in-process debugger with an MCP server) and its [Anomaly Scanner](https://github.com/Joaovitor404silva/Anomaly-Scanner) plugin work with NOLA.

## Reverse engineering

The repo has no decompile and no game symbols. Build them from your own copy of the game:

1. Copy `Stalker2\Binaries\Win64\Stalker2-Win64-Shipping.exe` somewhere else. Never work on the Steam file.
2. Import it into Ghidra 12.1.x (JDK 21, about 32 GB RAM) headless, with `re\ghidra_scripts\FastOpts.java` as a pre-script. Analysis takes hours.
3. Apply engine types from a live reflection dump with `ApplySdk.java`.
4. `node re\s2names\build_names.js` builds your local `names.tsv`; `re\ghidra_scripts\apply_names.ps1` applies it. Keep `names.tsv` local.
5. `DecompDump.java` dumps the decompile, and `node re\s2decomp\build_index.js` indexes it. Keep it local.

Use what you learn to write your own code. Don't commit decompiler output (see [CONTRIBUTING.md](CONTRIBUTING.md)).

## Design notes

- [docs/RELEASE.md](docs/RELEASE.md): what a release contains and how runtime tables are embedded.
- [docs/STEAM_DESIGN.md](docs/STEAM_DESIGN.md): how the Steam connection works.

---

[← Back to the overview](README.md)
