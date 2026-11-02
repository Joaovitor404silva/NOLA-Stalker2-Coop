# RELEASE IS SELF-CONTAINED

The release ships `main.dll`, the pak, UE4SS and one `flags.txt` in `C:\nola_bridge`. Nothing else. Every table the DLL reads at runtime must be inside the DLL. A table that only exists in a dev bridge folder (`C:\s2coop_inst\*`, `C:\nola_inst\*`) works in every dev test and is empty for every player (RC1: no container tables, the stash window closed itself, ST3 §352; no start kit, equipment, artifact or trader tables, RT4).

## The list: `data/nola_runtime.tsv`

One row for every file the DLL opens and every dev-only flag.

| column | meaning |
|---|---|
| kind | `file` or `flag` |
| name | file name in the bridge folder (`*` allowed for generated names) or flag key |
| class | `a` table that must ship (embedded), `b` state the mod or the installer writes, `c` dev or debug only (never in a release DLL) |
| where | reader (file and function) |
| rows | always empty (kept for older checkers); counts are taken from the table itself at check time |
| marker | class c only: strings that must not be in a release DLL, `w:` UTF-16, `a:` ASCII |
| note | what breaks without it |

## Adding a runtime-read table

1. Put the file in `data/`.
2. Add a `file <name> a <reader>` row to `data/nola_runtime.tsv`.
3. Run `uv run --no-project python -I tools/gen_x0_data.py --rc`. It checks the list and rewrites `src/s2x_data.rc` (RCDATA, one entry per class a row).
4. Read it with a bridge-first reader: `x0::table(name, out)` (bridge file, then the embedded copy), or the module `readText` (items, save), `readFile` (pop), `data` (quest, world) or `x0::data` / `x0::data_tsv` (embedded first). Never open a table with a plain bridge path.
5. Rebuild and run the x0 suite.

Order of `x0::table`: the bridge file wins when it exists (dev override, per-PC copy); otherwise the embedded copy (flag `taball`, default 1; 0 = bridge only, the RC1 behaviour). `tabText` in s2m_items is the same order for the container tables (flag `contall`).

## Dev-only inputs and flags (class c)

Dev inputs (`role.txt`, `state.json`, `s2cmd.txt`, `relay_record.txt`, the `s2x_data\` loose override) are compiled only when `S2X_RELEASE` is off. A release build (`S2X_RELEASE=ON`) locks every class c flag to its default: flags.txt, `fset` and the host cannot turn them on. A new dev input gets a class c row with a marker and an `#ifndef S2X_RELEASE` guard.

## Tests

- x0 suite, `test_x0.exe`: every class a table loads with at least one row from the embedded copy with an EMPTY bridge folder; every embedded resource is listed; the bridge copy wins; `taball=0` reads the bridge only.
- x0 suite, `test_rel.exe` (built with `/DS2X_RELEASE`): the same table check, dev flags locked, the loose override ignored.

## Builder check

`ModWork\s2coop\tools\build_release_cpp.py` runs `check_release_tables.py` on the release DLL (also in `--check`). The build FAILS and names the file and this list when:

- a file name the source reads is not in `data/nola_runtime.tsv`;
- a class a table is missing from `data/`, from `src/s2x_data.rc`, or is not byte-identical in the DLL (and is not the shipped `flags.txt`);
- the DLL is not an `S2X_RELEASE` build, or a class c marker is in the DLL;
- a test/diagnostic flag of the builder is not a class c flag row.

Standalone: `uv run --no-project python -I <ModWork>\s2coop\tools\check_release_tables.py --dll <dll> --repo <repo>`.

## Fix checks run the release build

Build the release DLL with `tools\wt.ps1 build <name> -Release` (`S2X_RELEASE=ON`, copied to `cppbuild\Release\NolaNative.dll`). The next build without `-Release` switches `cppbuild\Output` back to the dev DLL. A fix that touches a table, the bridge or a dev input is checked offline with the release DLL, `check_release_tables.py` and an empty bridge folder, not only with a dev bridge.
