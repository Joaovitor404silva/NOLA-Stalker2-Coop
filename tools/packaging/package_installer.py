"""Package the patched DLL with the unmodified upstream runtime/game payload."""
from pathlib import Path
import argparse, hashlib, shutil, zipfile

p = argparse.ArgumentParser()
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--dll', type=Path, required=True)
p.add_argument('--source', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
if a.dll.read_bytes()[:2] != b'MZ':
    raise SystemExit('Not a Windows DLL')
root = a.out / 'NOLA-stability-0.5-v1.3.2-Installer'
if root.exists():
    raise SystemExit('Output already exists; choose a new staging directory')
shutil.copytree(a.baseline, root)
rel = 'game/Stalker2/Binaries/Win64/ue4ss/Mods/NolaNative/dlls/main.dll'
shutil.copy2(a.dll, root / 'files' / rel)
checks = root / 'files/CHECKSUMS.txt'
lines = []
for line in checks.read_text().splitlines():
    old_hash, name = line.split('  ', 1)
    data = (root / 'files' / name).read_bytes()
    digest = hashlib.md5(data).hexdigest()
    if name != rel and digest != old_hash:
        raise SystemExit('Upstream payload mismatch: ' + name)
    lines.append(digest + '  ' + name)
checks.write_text('\n'.join(lines) + '\n', encoding='utf-8')
script = root / 'nola_install.ps1'
s = script.read_text(encoding='utf-8-sig')
assert "$NolaVersion = '1.3.2'" in s
s = s.replace("$NolaVersion = '1.3.2'", "$NolaVersion = '1.3.2 + stability 0.5 (unofficial experimental)'", 1)
s = s.replace('the host presses Home in game', 'the standing host presses Home in game')
script.write_text(s, encoding='utf-8-sig')
shutil.copy2(a.source / 'LICENSE', root / 'LICENSE')
shutil.copy2(a.source / 'CREDITS.md', root / 'CREDITS.md')
shutil.copy2(a.source / 'NEXT_REVIVE.md', root / 'REVIVE_0.5.md')
merge_notes = (a.source / 'MERGE_0.4.md').read_text(encoding='utf-8')
merge_notes = merge_notes.replace('Experimental SOURCE ONLY. No new DLL/installer is included or claimed tested.', 'Experimental Windows release. See START_HERE.txt for installation instructions.')
merge_notes = merge_notes.replace('This is not a full Windows or live-game test.', 'The portable tests above are supplemented by the Windows build; live-game testing remains required.')
(root / 'MERGE_0.4.md').write_text(merge_notes, encoding='utf-8')
readme = '''NOLA stability 0.5 - unofficial experimental installer
Based on NOLA 1.3.2 by Joaovitor404silva and its contributors.
For STALKER 2 Steam build 26123434 (Windows x64).

INSTALL
1. Close STALKER 2 and extract the WHOLE ZIP.
2. Double-click install.bat. Keep the files folder beside it.
3. Select Steam friends or IP, then confirm the game folder and installation.
4. Every player must install this same patched package.
5. Keep this extracted folder: uninstall.bat restores backed-up files.
The installer has not been run against your actual game automatically.

CHANGES
- Confirm medkit removal before sending a revive request.
- Explicitly rejected revives return one medkit when both peers support receipts.
- Prevent competing rescuers from overwriting an active request.
- Duplicate/mismatched acknowledgements cannot trigger repeated refunds.
- Includes upstream 1.3.2 door, tripwire-disarm, compass and menu fixes.
- Teammate-down alerts and revive prompts; prior network and guest-trader changes.
- No timed bleed-out or automatic stand-up while downed. Revive normally,
  or the existing all-players-down team wipe applies.
- While downed, hold HOME continuously for FIVE seconds. A countdown says
  you will move beside the nearest standing teammate. Release to cancel.
- Rescue moves you; it does not revive you. After success, release HOME;
  the cooldown is 10 seconds. Stale/disconnected/downed targets are excluded.
- HOME opens the Steam invitation window only when you are not downed.
- Bounded network decompression/receive work, stale-session protection,
  endpoint filtering and reconnect delta-cache fixes.

NOTES
Uncertain revive transactions block further medkit use for this module lifetime.
There is no automatic recovery after disconnect, reload or crash. Never treat
missing replies as rejection. Both players need this version for refunds.
Money transfer / INSERT interface is NOT included. See REVIVE_0.5.md.
This is a compiled experimental build, not an official NOLA release.
Offline native tests do not replace in-game multiplayer testing.
Rescue checks collision at eight nearby positions, but cannot guarantee
an anomaly-free spot. Try with two players before a long session.
The original UE4SS runtime and game-data pak are retained from NOLA 1.3.2.
See INSTALL.md for connection setup and MERGE_0.4.md for source merge details and remaining limitations.
Corresponding modified NOLA source is supplied alongside this installer as
NOLA-stability-0.5-v1.3.2-SOURCE.zip. Original project:
https://github.com/Joaovitor404silva/NOLA-Stalker2-Coop
'''
(root / 'START_HERE.txt').write_text(readme, encoding='utf-8')
install = root / 'INSTALL.md'
s = install.read_text(encoding='utf-8-sig').replace('Version: 1.3.2', 'Version: 1.3.2 + unofficial stability 0.5\n\nRead START_HERE.txt for patched controls and limitations.')
s = s.replace('NOLA-Auto.zip', 'NOLA-stability-0.5-v1.3.2-Installer.zip').replace('press Home in the world', 'while standing, press Home in the world')
install.write_text(s, encoding='utf-8')
archive = a.out / (root.name + '.zip')
with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
    for f in sorted(root.rglob('*')):
        if f.is_file(): z.write(f, f.relative_to(a.out))
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None
print(archive)
print('SHA256', hashlib.sha256(archive.read_bytes()).hexdigest())
