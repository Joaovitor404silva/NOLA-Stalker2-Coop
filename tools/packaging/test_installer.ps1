$ErrorActionPreference = 'Stop'
$root = 'C:\Users\Jeffrey\NOLA-Build\installer-smoke'
if (Test-Path $root) { throw 'Test directory already exists; refusing to reuse it' }
$game = Join-Path $root 'Game'
$bridge = Join-Path $root 'Bridge'
$w64 = Join-Path $game 'Stalker2\Binaries\Win64'
$ue = Join-Path $w64 'ue4ss'
$paks = Join-Path $game 'Stalker2\Content\Paks\~mods'
$package = 'C:\Users\Jeffrey\NOLA-Build\release\NOLA-stability-0.1-Installer'
$setup = Join-Path $package 'nola_install.ps1'
New-Item -ItemType Directory -Force $w64,$bridge,"$ue\Mods",$paks | Out-Null
Set-Content "$w64\Stalker2-Win64-Shipping.exe" 'test placeholder, never executed'
Set-Content "$w64\dwmapi.dll" 'original loader'
Set-Content "$ue\UE4SS.dll" 'original runtime'
Set-Content "$ue\Mods\mods.txt" 'OtherMod : 1'
Set-Content "$ue\UE4SS-settings.ini" "[Overrides]`r`nModsFolderPath = custom`r`n[Hooks]`r`nHookEngineTick = 0`r`n[EngineVersionOverride]`r`nMajorVersion = 4`r`nMinorVersion = 27"
Set-Content "$paks\OtherMod.pak" 'unrelated mod'
Set-Content "$paks\zzzzzz_Nola_old.pak" 'old NOLA pak'
Set-Content "$bridge\flags.txt" 'original flags'
function Snapshot {
    $m = @{}
    Get-ChildItem $root -Recurse -File | ForEach-Object {
        $m[$_.FullName.Substring($root.Length)] = (Get-FileHash $_.FullName -Algorithm SHA256).Hash
    }
    return $m
}
$before = Snapshot
& powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action install -Game $game -Bridge $bridge -Net steam -NoFw -Yes
if ($LASTEXITCODE -ne 0) { throw 'Steam installer failed' }
$installed = "$ue\Mods\NolaNative\dlls\main.dll"
$payload = "$package\files\game\Stalker2\Binaries\Win64\ue4ss\Mods\NolaNative\dlls\main.dll"
if ((Get-FileHash $installed).Hash -ne (Get-FileHash $payload).Hash) { throw 'Wrong installed DLL' }
if ((Get-Content "$ue\Mods\mods.txt" -Raw) -notmatch 'OtherMod : 1') { throw 'Unrelated mod lost' }
if ((Get-Content "$bridge\flags.txt" -Raw) -notmatch '(?m)^net=steam\s*$') { throw 'Steam settings missing' }
Write-Output 'PASS: Steam installation and existing mod preservation'
& powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action install -Game $game -Bridge $bridge -Net ip -Peer '192.0.2.1' -NoFw -Yes
if ($LASTEXITCODE -ne 0) { throw 'IP reinstall failed' }
if ((Get-Content "$bridge\flags.txt" -Raw) -notmatch '(?m)^netpeer=192\.0\.2\.1\s*$') { throw 'IP settings missing' }
Write-Output 'PASS: IP reinstall and original backup preservation'
& powershell -NoProfile -ExecutionPolicy Bypass -File $setup -Action uninstall -Game $game -NoFw -Yes
if ($LASTEXITCODE -ne 0) { throw 'Uninstall failed' }
$after = Snapshot
if ($after.Count -ne $before.Count) { throw "File count changed: $($before.Count) -> $($after.Count)" }
foreach ($k in $before.Keys) {
    if ($after[$k] -ne $before[$k]) { throw "Restore mismatch: $k" }
}
Write-Output 'PASS: uninstall restored every original file byte-for-byte; no extra files remain'
