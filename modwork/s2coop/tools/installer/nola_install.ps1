# NOLA - No One Left Alone: installer and uninstaller.
#
# This is a plain text script on purpose, so anyone can read what it does
# before running it. It downloads nothing and runs nothing hidden. Every file
# it installs sits next to it in the "files" folder, and every file it
# replaces or removes is first copied to a backup (the journal) so that
# uninstall.bat can put everything back exactly as it was.
#
# Normally you start it through install.bat or uninstall.bat. It can also run
# without questions, for example:
#   powershell -NoProfile -ExecutionPolicy Bypass -File nola_install.ps1 -Action install -Net steam -Yes
#   powershell -NoProfile -ExecutionPolicy Bypass -File nola_install.ps1 -Action install -Net ip -Peer 10.0.1.20 -Yes
#   powershell -NoProfile -ExecutionPolicy Bypass -File nola_install.ps1 -Action uninstall -Yes
#
# Parameters:
#   -Action   install, uninstall or find (find only prints the game folder). Without it, a menu asks.
#   -Game     the "S.T.A.L.K.E.R. 2 Heart of Chornobyl" folder. Without it, the Steam libraries are searched.
#   -Net      steam (Steam friends) or ip (LAN or VPN). Without it, a question asks (or steam with -Yes).
#   -Peer     IP mode only: the IP address of the OTHER PC.
#   -Bridge   the folder for the mod's settings file flags.txt. Default: the S2_BRIDGE
#             environment variable if it is set, else C:\nola_bridge (the same folder the mod reads).
#   -Steam    the Steam folder, if it is not found in the registry.
#   -FwName   a different name for the firewall rule.
#   -NoFw     never touch the Windows firewall (prints the command instead).
#   -Yes      answer yes to every question (no prompts).
#
# Administrator rights are asked for ONLY when they are needed: when the game
# folder or the settings folder cannot be written by your user, or when IP mode
# needs the Windows firewall rule. Steam friends mode on a normal Steam install
# does not ask.

param(
    [string]$Action = "",
    [string]$Game = "",
    [string]$Net = "",
    [string]$Peer = "",
    [string]$Bridge = "",
    [string]$Steam = "",
    [string]$FwName = "",
    [string]$Role = "",
    [switch]$NoFw,
    [switch]$Yes,
    # Used only when this script restarts itself with administrator rights.
    [switch]$Elevated,
    [switch]$Pause
)

$ErrorActionPreference = 'Stop'

# ---------------------------------------------------------------------------
# Fixed values
# ---------------------------------------------------------------------------
$NolaVersion = '{{VERSION}}'
$GameFolderName = 'S.T.A.L.K.E.R. 2 Heart of Chornobyl'
$ExeName = 'Stalker2-Win64-Shipping.exe'
$HostPort = 7779
$ClientPort = 7778
$DefaultBridge = 'C:\nola_bridge'

# The package layout: this script, and next to it the "files" folder that holds
# exactly what NOLA-Manual.zip holds (game\..., C\nola_bridge\flags.txt,
# if-missing\... and CHECKSUMS.txt).
$ScriptPath = $MyInvocation.MyCommand.Path
$FilesDir = Join-Path $PSScriptRoot 'files'
$Win64Rel = 'game/Stalker2/Binaries/Win64'
$Ue4ssRel = $Win64Rel + '/ue4ss'
$Sums = @{}

# ---------------------------------------------------------------------------
# Small helpers: printing, failing, asking
# ---------------------------------------------------------------------------
class NolaStop : System.Exception { NolaStop([string]$m) : base($m) {} }

function Say([string]$s) { Write-Host $s }

function Fail([string]$s) {
    Write-Host ''
    Write-Host ('ERROR: ' + $s)
    throw [NolaStop]::new($s)
}

function Ask([string]$prompt) {
    [Console]::Write($prompt)
    $r = [Console]::ReadLine()
    # No keyboard input at all (for example started without a console): stop instead of asking forever.
    if ($null -eq $r) { Fail 'no answer could be read (run it from install.bat, or pass the answers as parameters)' }
    return $r.Trim()
}

function Md5Bytes([byte[]]$b) {
    $m = [System.Security.Cryptography.MD5]::Create()
    try { return ([System.BitConverter]::ToString($m.ComputeHash($b)) -replace '-', '').ToLowerInvariant() }
    finally { $m.Dispose() }
}

# ---------------------------------------------------------------------------
# The payload: the files in the "files" folder, checked against CHECKSUMS.txt
# ---------------------------------------------------------------------------
function Load-Payload {
    $sumFile = Join-Path $FilesDir 'CHECKSUMS.txt'
    if (-not [System.IO.File]::Exists($sumFile)) {
        Fail ('the "files" folder next to this script is missing or incomplete (unzip the whole package first): ' + $FilesDir)
    }
    # Each line is "md5  path", the path relative to the "files" folder with forward slashes.
    foreach ($l in [System.IO.File]::ReadAllLines($sumFile)) {
        $t = $l.Trim()
        if ($t.Length -eq 0) { continue }
        $p = $t -split '  ', 2
        $Sums[$p[1]] = $p[0]
    }
}

function Payload([string]$rel) {
    $path = Join-Path $FilesDir ($rel -replace '/', '\')
    if (-not [System.IO.File]::Exists($path)) { Fail ('payload file missing: files\' + ($rel -replace '/', '\')) }
    $b = [System.IO.File]::ReadAllBytes($path)
    if ($Sums.ContainsKey($rel) -and (Md5Bytes $b) -ne $Sums[$rel]) { Fail ('payload file damaged: files\' + ($rel -replace '/', '\')) }
    return , $b
}

function PayloadFiles([string]$prefix) {
    return @($Sums.Keys | Where-Object { $_.StartsWith($prefix, [System.StringComparison]::Ordinal) } | Sort-Object)
}

# ---------------------------------------------------------------------------
# The backup journal
#   <game>\Stalker2\Binaries\Win64\nola_backup\state.txt lists, in order, every
#   change made:  F <path> <backup name or ->   a file written or deleted
#                 D <path>                      a folder created
#                 W <rule name>                 a firewall rule added
#                 M <key> <value>               notes (version, net, ...)
#   The original files are copied to nola_backup\files\00001, 00002, ...
#   Uninstall walks the list backwards and undoes each line.
#   (The format is the same one NOLA-Setup.exe used, so either can undo the other.)
# ---------------------------------------------------------------------------
$Journal = $null
$Utf8NoBom = New-Object System.Text.UTF8Encoding($false)

function Journal-Begin([string]$dir) {
    $script:Journal = @{ Dir = $dir; State = (Join-Path $dir 'state.txt'); Seen = @{}; Counter = 0 }
    [void][System.IO.Directory]::CreateDirectory((Join-Path $dir 'files'))
    if (-not [System.IO.File]::Exists($Journal.State)) {
        [System.IO.File]::WriteAllText($Journal.State, "NOLA-JOURNAL 1`n", [System.Text.Encoding]::ASCII)
    }
}

function Journal-Add([string]$line) {
    [System.IO.File]::AppendAllText($Journal.State, $line + "`n", $Utf8NoBom)
}

function Journal-Meta([string]$key, [string]$value) { Journal-Add ("M`t" + $key + "`t" + $value) }

# Called before a file is changed: keeps a copy of the original (once per file).
function Journal-Touch([string]$path) {
    if ($Journal.Seen.ContainsKey($path)) { return }
    $Journal.Seen[$path] = $true
    if ([System.IO.File]::Exists($path)) {
        $Journal.Counter++
        $name = '{0:D5}' -f $Journal.Counter
        [System.IO.File]::Copy($path, (Join-Path (Join-Path $Journal.Dir 'files') $name), $true)
        Journal-Add ("F`t" + $path + "`t" + $name)
    }
    else { Journal-Add ("F`t" + $path + "`t-") }
}

function Journal-Read([string]$statePath) {
    $rows = @()
    foreach ($l in [System.IO.File]::ReadAllLines($statePath, [System.Text.Encoding]::UTF8)) {
        if ($l.Length -eq 0 -or $l.StartsWith('NOLA-JOURNAL') -or $l.StartsWith('S2COOP-JOURNAL')) { continue }
        $rows += , ($l -split "`t")
    }
    return , $rows
}

# ---------------------------------------------------------------------------
# File operations, all recorded in the journal
# ---------------------------------------------------------------------------
function EnsureDir([string]$dir) {
    if ([System.IO.Directory]::Exists($dir)) { return }
    $parent = [System.IO.Path]::GetDirectoryName($dir)
    if ($parent) { EnsureDir $parent }
    [void][System.IO.Directory]::CreateDirectory($dir)
    Journal-Add ("D`t" + $dir)
}

# Write a file (backing up the old one first), then read it back to check it.
function Put([string]$path, [byte[]]$data) {
    EnsureDir ([System.IO.Path]::GetDirectoryName($path))
    Journal-Touch $path
    [System.IO.File]::WriteAllBytes($path, $data)
    if ((Md5Bytes ([System.IO.File]::ReadAllBytes($path))) -ne (Md5Bytes $data)) { Fail ('write verification failed: ' + $path) }
}

function PutPayload([string]$rel, [string]$path) { Put $path (Payload $rel) }

# Delete a file (backing it up first).
function Remove([string]$path) {
    if (-not [System.IO.File]::Exists($path)) { return }
    Journal-Touch $path
    [System.IO.File]::Delete($path)
}

function PruneEmpty([string]$dir) {
    foreach ($d in [System.IO.Directory]::GetDirectories($dir)) { PruneEmpty $d }
    if ([System.IO.Directory]::GetFileSystemEntries($dir).Length -eq 0) { [System.IO.Directory]::Delete($dir) }
}

# Delete a whole folder (every file backed up first).
function RemoveTree([string]$dir) {
    if (-not [System.IO.Directory]::Exists($dir)) { return }
    foreach ($f in [System.IO.Directory]::GetFiles($dir, '*', [System.IO.SearchOption]::AllDirectories)) { Remove $f }
    PruneEmpty $dir
}

# ---------------------------------------------------------------------------
# Administrator rights: checked, and asked for only when needed
# ---------------------------------------------------------------------------
function IsAdmin {
    $id = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    return (New-Object System.Security.Principal.WindowsPrincipal($id)).IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

# Can this user write into $dir? An existing folder: try to create and delete a
# small test file in it. A folder that does not exist yet: try to create and
# delete a test folder in the nearest parent that exists.
function Test-Writable([string]$dir) {
    $probe = '.nola_write_test_' + [System.Guid]::NewGuid().ToString('N').Substring(0, 8)
    try {
        if ([System.IO.Directory]::Exists($dir)) {
            $f = Join-Path $dir $probe
            [System.IO.File]::WriteAllBytes($f, [byte[]]@())
            [System.IO.File]::Delete($f)
            return $true
        }
        $parent = [System.IO.Path]::GetDirectoryName($dir)
        if (-not $parent) { return $false }
        while (-not [System.IO.Directory]::Exists($parent)) {
            $up = [System.IO.Path]::GetDirectoryName($parent)
            if (-not $up) { return $false }
            $parent = $up
        }
        $d = Join-Path $parent $probe
        [void][System.IO.Directory]::CreateDirectory($d)
        [System.IO.Directory]::Delete($d)
        return $true
    }
    catch { return $false }
}

# Start this same script again with administrator rights (Windows shows its
# usual "allow this app to make changes" question), wait for it and return
# its exit code. The new window gets every answer already given, so it asks nothing.
function Restart-Elevated([string[]]$argList, [string]$why) {
    if ($Elevated) { Fail ('administrator rights are still missing after the restart (' + $why + ')') }
    Say ''
    Say ('Administrator rights are needed: ' + $why + '.')
    Say 'Windows will ask once to allow PowerShell to make changes. The rest runs in a new window.'
    $all = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $ScriptPath + '"')) + $argList + @('-Yes', '-Elevated')
    if (-not $Yes) { $all += '-Pause' }
    try { $p = Start-Process -FilePath 'powershell.exe' -ArgumentList ($all -join ' ') -Verb RunAs -Wait -PassThru }
    catch { Fail 'administrator rights were not given, nothing changed' }
    Say ('The administrator window finished (exit code ' + $p.ExitCode + ').')
    return $p.ExitCode
}

function Quote([string]$s) { return '"' + $s + '"' }

# ---------------------------------------------------------------------------
# Finding the game
# ---------------------------------------------------------------------------
function SteamRoots {
    $roots = @()
    if ($Steam.Length -gt 0) { return , @($Steam) }
    try {
        $v = (Get-ItemProperty -Path 'HKCU:\Software\Valve\Steam' -Name 'SteamPath' -ErrorAction Stop).SteamPath
        if ($v) { $roots += ($v -replace '/', '\') }
    }
    catch { }
    try {
        $v = (Get-ItemProperty -Path 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -Name 'InstallPath' -ErrorAction Stop).InstallPath
        if ($v) { $roots += $v }
    }
    catch { }
    $roots += 'C:\Program Files (x86)\Steam'
    return , $roots
}

# Every Steam library is listed in steamapps\libraryfolders.vdf as "path" "E:\\SteamLibrary".
function FindGame {
    $libs = @()
    foreach ($r in (SteamRoots)) {
        $libs += $r
        $vdf = [System.IO.Path]::Combine($r, 'steamapps\libraryfolders.vdf')
        if (-not [System.IO.File]::Exists($vdf)) { continue }
        foreach ($m in [regex]::Matches([System.IO.File]::ReadAllText($vdf), '"path"\s+"([^"]+)"')) {
            $libs += $m.Groups[1].Value.Replace('\\', '\')
        }
    }
    foreach ($l in $libs) {
        $g = [System.IO.Path]::Combine($l, 'steamapps\common\' + $GameFolderName)
        if ([System.IO.File]::Exists([System.IO.Path]::Combine($g, 'Stalker2\Binaries\Win64\' + $ExeName))) { return $g }
    }
    return ''
}

function Win64([string]$game) { return Join-Path $game 'Stalker2\Binaries\Win64' }
function BackupDir([string]$game) { return Join-Path (Win64 $game) 'nola_backup' }
function OldBackupDir([string]$game) { return Join-Path (Win64 $game) 's2coop_backup' }

function PickGame {
    $g = $script:Game
    if ($g.Length -eq 0) { $g = FindGame }
    if ($g.Length -gt 0) { Say ('Game folder: ' + $g) }
    while (-not [System.IO.File]::Exists([System.IO.Path]::Combine($g, 'Stalker2\Binaries\Win64\' + $ExeName))) {
        if ($g.Length -gt 0) { Say ('Not a ' + $GameFolderName + ' folder: ' + $g) }
        if ($Yes) { Fail 'no game folder (pass -Game "<path of the game folder>")' }
        $g = (Ask ("Paste the full path of the '" + $GameFolderName + "' folder: ")).Trim('"').TrimEnd('\')
        if ($g.Length -eq 0) { Fail 'no game folder' }
    }
    return $g
}

# The game must be closed: its files are in use while it runs.
function CheckRunning([string]$game) {
    $want = [System.IO.Path]::GetFullPath((Join-Path (Win64 $game) $ExeName))
    foreach ($p in @(Get-Process -Name 'Stalker2-Win64-Shipping' -ErrorAction SilentlyContinue)) {
        $path = ''
        try { $path = $p.MainModule.FileName } catch { $path = '' }
        if (-not $path) { Fail 'S.T.A.L.K.E.R. 2 is running and its folder cannot be checked. Close the game and run this again.' }
        if ([string]::Equals([System.IO.Path]::GetFullPath($path), $want, [System.StringComparison]::OrdinalIgnoreCase)) {
            Fail 'S.T.A.L.K.E.R. 2 is running. Close the game and run this again.'
        }
    }
}

# ---------------------------------------------------------------------------
# The connection choice
# ---------------------------------------------------------------------------
function PickNet {
    $n = $script:Net.ToLowerInvariant()
    if ($n.Length -gt 0 -and $n -ne 'steam' -and $n -ne 'ip') { Fail '-Net must be steam or ip' }
    if ($n.Length -eq 0 -and $Yes) { $n = 'steam' }
    while ($n -ne 'steam' -and $n -ne 'ip') {
        Say 'How do the PCs connect? NOLA takes up to five players: the host plus four friends.'
        Say '1) Steam friends (default): the host invites friends in game; no IP, VPN or firewall rule'
        Say '2) IP address: LAN or VPN; you type the IP addresses of the PCs that may host'
        $c = (Ask 'Choose 1 or 2 (Enter = 1): ').ToLowerInvariant()
        if ($c.Length -eq 0 -or $c -eq '1' -or $c -eq 'steam') { $n = 'steam' }
        elseif ($c -eq '2' -or $c -eq 'ip') { $n = 'ip' }
    }
    if ($n -eq 'steam' -and $script:Peer.Length -gt 0) { Fail '-Peer is only used with -Net ip' }
    return $n
}

function PeerList([string]$s) {
    $r = @()
    foreach ($x in ($s -split '[,;\s]+')) {
        if ($x.Length -eq 0) { continue }
        if ($x -notmatch '^[0-9A-Za-z\.\-:]+$') { return '' }
        if ($r -notcontains $x) { $r += $x }
    }
    return ($r -join ',')
}

function PickPeer {
    $p = PeerList $script:Peer
    while ($p.Length -eq 0) {
        if ($Yes) { Fail 'IP mode needs the addresses of the PCs that may host (pass -Peer <IP>, or several: -Peer 10.0.1.20,26.1.2.3)' }
        Say 'Type the IP address of every PC that may host (LAN or VPN, for example 26.x.x.x for Radmin), separated by commas.'
        Say 'Every player can type the same list, your own address included. Host co-op ignores it; Join co-op tries every address until a host answers.'
        $p = PeerList (Ask 'IP addresses of the PCs that may host: ')
    }
    return $p
}

# ---------------------------------------------------------------------------
# The Windows firewall rule (IP mode only)
# ---------------------------------------------------------------------------
function RuleName([string]$role, [string]$port) {
    if ($FwName.Length -gt 0) { return $FwName }
    return 'NOLA ' + $role + ' UDP ' + $port
}

function Netsh([string[]]$a) {
    # netsh runs in this same window; its text is kept to show it when something fails.
    $ErrorActionPreference = 'Continue'
    $o = & netsh.exe @a 2>&1 | Out-String
    return @{ Code = $LASTEXITCODE; Out = $o.Trim() }
}

function FirewallAdd([string]$gameExe, [string]$role, [string]$port) {
    $name = RuleName $role $port
    $show = 'advfirewall firewall add rule name="' + $name + '" dir=in action=allow protocol=UDP localport=' + $port +
            ' program="' + $gameExe + '" profile=private enable=yes'
    if ($NoFw) { Say ('[--] firewall skipped (-NoFw). Rule it would add: netsh ' + $show); return }
    if (-not (IsAdmin)) {
        Say 'WARNING: not running as administrator, firewall rule not added. Run this as administrator, or run once in an administrator prompt:'
        Say ('  netsh ' + $show)
        return
    }
    # Remove older rules of the mod (and an earlier copy of this one), then add it.
    [void](Netsh @('advfirewall', 'firewall', 'delete', 'rule', 'name=S2Coop relay'))
    [void](Netsh @('advfirewall', 'firewall', 'delete', 'rule', ('name=S2Coop co-op UDP ' + $HostPort + ',' + $ClientPort)))
    [void](Netsh @('advfirewall', 'firewall', 'delete', 'rule', ('name=' + $name)))
    $r = Netsh @('advfirewall', 'firewall', 'add', 'rule', ('name=' + $name), 'dir=in', 'action=allow', 'protocol=UDP',
                 ('localport=' + $port), ('program=' + $gameExe), 'profile=private', 'enable=yes')
    if ($r.Code -ne 0) { Fail ('firewall rule not added: ' + $r.Out) }
    Journal-Add ("W`t" + $name)
    Say ('[ok] firewall: inbound UDP ' + $port + ' allowed for ' + $ExeName + " (Private profile), rule '" + $name + "'")
}

function FirewallRemove([string]$name) {
    if ($NoFw) { Say ("[--] firewall rule '" + $name + "' left (-NoFw)"); return }
    if (-not (IsAdmin)) { Say ("WARNING: not administrator, firewall rule '" + $name + "' not removed"); return }
    $r = Netsh @('advfirewall', 'firewall', 'delete', 'rule', ('name=' + $name))
    if ($r.Code -eq 0) { Say ("[ok] firewall rule '" + $name + "' removed") } else { Say ("[--] firewall rule '" + $name + "' not present") }
}

# ---------------------------------------------------------------------------
# The two config files the player may already have
# ---------------------------------------------------------------------------

# UE4SS-settings.ini: "ModsFolderPath =" must be empty (so UE4SS loads the mods
# from ue4ss\Mods) and "HookEngineTick = 1". Missing lines are added in their
# section. [EngineVersionOverride] MajorVersion = 5 / MinorVersion = 5 is forced too
# (UE4SS reads VTableLayout.ini before its version scan; empty values crash it at start).
# Every other line stays as it is. Written with Windows (CRLF) line endings.
function PatchIni([string]$iniPath) {
    $lines = New-Object System.Collections.Generic.List[string]
    $lines.AddRange([System.IO.File]::ReadAllLines($iniPath))
    $changed = $false
    $sawTick = $false
    $sawMods = $false
    $sawMaj = $false
    $sawMin = $false
    $sec = ''
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^\s*\[(.+?)\]') { $sec = $Matches[1].Trim().ToLowerInvariant() }
        if ($sec -eq 'engineversionoverride' -and $lines[$i] -match '^\s*(MajorVersion|MinorVersion)\s*=') {
            $k = $Matches[1]
            if ($k -eq 'MajorVersion') { $sawMaj = $true } else { $sawMin = $true }
            if ($lines[$i] -notmatch ('^\s*' + $k + '\s*=\s*5\s*$')) { $lines[$i] = $k + ' = 5'; $changed = $true }
        }
        elseif ($lines[$i] -match '^\s*HookEngineTick\s*=') {
            $sawTick = $true
            if ($lines[$i] -notmatch '^\s*HookEngineTick\s*=\s*1\s*$') { $lines[$i] = 'HookEngineTick = 1'; $changed = $true }
        }
        elseif ($lines[$i] -match '^\s*ModsFolderPath\s*=') {
            $sawMods = $true
            if ($lines[$i] -match '^\s*ModsFolderPath\s*=\s*\S') { $lines[$i] = 'ModsFolderPath ='; $changed = $true }
        }
    }
    foreach ($want in @(@{ Seen = $sawTick; Section = '[Hooks]'; Line = 'HookEngineTick = 1' },
                        @{ Seen = $sawMods; Section = '[Overrides]'; Line = 'ModsFolderPath =' },
                        @{ Seen = $sawMaj; Section = '[EngineVersionOverride]'; Line = 'MajorVersion = 5' },
                        @{ Seen = $sawMin; Section = '[EngineVersionOverride]'; Line = 'MinorVersion = 5' })) {
        if ($want.Seen) { continue }
        $at = -1
        for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -ieq $want.Section) { $at = $i; break } }
        if ($at -ge 0) { $lines.Insert($at + 1, $want.Line) }
        else { $lines.Add($want.Section); $lines.Add($want.Line) }
        Say ('[ok] UE4SS-settings.ini had no ' + ($want.Line -split ' ')[0] + ' line: added ' + $want.Line)
        $changed = $true
    }
    if ($changed) {
        Journal-Touch $iniPath
        [System.IO.File]::WriteAllLines($iniPath, $lines.ToArray(), $Utf8NoBom)
        Say '[ok] UE4SS-settings.ini: HookEngineTick = 1, ModsFolderPath empty, EngineVersionOverride 5.5 (original kept in the backup)'
    }
    else { Say '[ok] UE4SS-settings.ini already fine, left as it is' }
}

# mods.txt: drop the lines of the older versions of the mod, keep every other
# mod's line, and make sure there is exactly one "NolaNative : 1".
function MergeModsTxt([string]$path) {
    if (-not [System.IO.File]::Exists($path)) { PutPayload ('if-missing/' + $Ue4ssRel + '/Mods/mods.txt') $path; return }
    $keep = New-Object System.Collections.Generic.List[string]
    $had = $false
    foreach ($l in [System.IO.File]::ReadAllLines($path)) {
        if ($l -match '^\s*(S2CoopAlife|S2CoopNative|NolaAlife)\s*:') { continue }
        if ($l -match '^\s*NolaNative\s*:') { if (-not $had) { $keep.Add('NolaNative : 1') }; $had = $true; continue }
        $keep.Add($l)
    }
    if (-not $had) { $keep.Add('NolaNative : 1') }
    Journal-Touch $path
    [System.IO.File]::WriteAllLines($path, $keep.ToArray(), [System.Text.Encoding]::ASCII)
}

# flags.txt: the package copy, without any net= / netpeer= line, then the
# connection lines for the choice made. CRLF line endings.
function WriteFlags([string]$path, [string]$net, [string]$peer) {
    $text = [System.Text.Encoding]::ASCII.GetString((Payload 'C/nola_bridge/flags.txt'))
    $sb = New-Object System.Text.StringBuilder
    foreach ($l in $text.Split("`n")) {
        $t = $l.TrimEnd("`r")
        if ($t.Length -eq 0) { continue }
        if ($t -match '^\s*(net|netpeer)\s*=') { continue }
        [void]$sb.Append($t).Append("`r`n")
    }
    [void]$sb.Append('net=').Append($net).Append("`r`n")
    if ($net -eq 'ip') { [void]$sb.Append('netpeer=').Append($peer).Append("`r`n") }
    Put $path ([System.Text.Encoding]::ASCII.GetBytes($sb.ToString()))
}

# The settings folder: -Bridge, else the S2_BRIDGE environment variable (the mod
# reads the same variable), else C:\nola_bridge.
function PickBridge {
    if ($script:Bridge.Length -gt 0) { return $script:Bridge.TrimEnd('\') }
    if ($env:S2_BRIDGE) { return $env:S2_BRIDGE.TrimEnd('\') }
    return $DefaultBridge
}

function JournalHasRule([string]$state) {
    if (-not [System.IO.File]::Exists($state)) { return $false }
    foreach ($r in (Journal-Read $state)) { if ($r[0] -eq 'W') { return $true } }
    return $false
}

function JournalBridge([string]$state) {
    if (-not [System.IO.File]::Exists($state)) { return '' }
    foreach ($r in (Journal-Read $state)) { if ($r[0] -eq 'M' -and $r[1] -eq 'bridge') { return $r[2] } }
    return ''
}

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
function Install {
    $g = PickGame
    CheckRunning $g
    if ($Role.Length -gt 0) { Say "Note: -Role is no longer used. Every PC gets the same install; pick Host co-op or Join co-op in the game's main menu." }
    $netMode = PickNet
    $peerIp = ''
    if ($netMode -eq 'ip') { $peerIp = PickPeer }
    $br = PickBridge
    $w64 = Win64 $g
    $ue = Join-Path $w64 'ue4ss'
    $mods = Join-Path $g 'Stalker2\Content\Paks\~mods'

    Say ''
    if ($netMode -eq 'steam') { Say 'Connection: Steam friends' }
    else { Say ('Connection: IP   hosts to try when joining: ' + $peerIp +'   listening on UDP ' + $HostPort + ' (when hosting) and ' + $ClientPort + ' (when joining)') }
    if (-not $Yes -and (Ask 'Install now? (y/n): ').ToLowerInvariant() -ne 'y') { Say 'Cancelled, nothing changed.'; return 1 }

    # Administrator rights only when needed.
    $why = @()
    if (-not (IsAdmin)) {
        if (-not (Test-Writable $w64) -or -not (Test-Writable $mods)) { $why += 'the game folder cannot be written by your user' }
        if (-not (Test-Writable $br)) { $why += ('the settings folder ' + $br + ' cannot be written by your user') }
        if ($netMode -eq 'ip' -and -not $NoFw) { $why += 'IP mode adds a Windows firewall rule' }
        elseif (-not $NoFw -and ((JournalHasRule (Join-Path (BackupDir $g) 'state.txt')) -or (JournalHasRule (Join-Path (OldBackupDir $g) 'state.txt')))) {
            $why += 'the earlier install added a firewall rule that has to be removed'
        }
    }
    if ($why.Count -gt 0) {
        $a = @('-Action', 'install', '-Game', (Quote $g), '-Net', $netMode, '-Bridge', (Quote $br))
        if ($netMode -eq 'ip') { $a += @('-Peer', $peerIp) }
        if ($Steam.Length -gt 0) { $a += @('-Steam', (Quote $Steam)) }
        if ($FwName.Length -gt 0) { $a += @('-FwName', (Quote $FwName)) }
        if ($NoFw) { $a += '-NoFw' }
        return Restart-Elevated $a ($why -join '; ')
    }

    # An earlier install is undone first, so its backup keeps the true originals.
    if ([System.IO.File]::Exists((Join-Path (OldBackupDir $g) 'state.txt'))) {
        Say 'An install from before the NOLA rename was found: restoring the original files first.'
        [void](Uninstall-From (OldBackupDir $g))
    }
    if ([System.IO.File]::Exists((Join-Path (BackupDir $g) 'state.txt'))) {
        Say 'An earlier NOLA install was found: restoring the original files first.'
        [void](Uninstall-From (BackupDir $g))
    }

    Journal-Begin (BackupDir $g)
    Journal-Meta 'version' $NolaVersion
    Journal-Meta 'net' $netMode
    if ($netMode -eq 'ip') { Journal-Meta 'peer' $peerIp }
    Journal-Meta 'game' $g
    Journal-Meta 'bridge' $br

    # 1. The UE4SS mod loader: dwmapi.dll next to the game exe, and the ue4ss folder.
    #    The package copies always replace what is there (the old files go to the backup).
    PutPayload ($Win64Rel + '/dwmapi.dll') (Join-Path $w64 'dwmapi.dll')
    PutPayload ($Ue4ssRel + '/UE4SS.dll') (Join-Path $ue 'UE4SS.dll')
    PutPayload ($Ue4ssRel + '/LICENSE') (Join-Path $ue 'LICENSE')
    PutPayload ($Ue4ssRel + '/VTableLayout.ini') (Join-Path $ue 'VTableLayout.ini')
    foreach ($f in (PayloadFiles ($Ue4ssRel + '/UE4SS_Signatures/'))) {
        PutPayload $f (Join-Path $ue ($f.Substring($Ue4ssRel.Length + 1) -replace '/', '\'))
    }
    $ini = Join-Path $ue 'UE4SS-settings.ini'
    if ([System.IO.File]::Exists($ini)) { PatchIni $ini }
    else { PutPayload ('if-missing/' + $Ue4ssRel + '/UE4SS-settings.ini') $ini; Say '[ok] UE4SS-settings.ini (stock) installed' }
    Say '[ok] UE4SS loader installed'

    # 2. The mod itself: remove the older versions, add NolaNative, enable it in mods.txt.
    foreach ($oldMod in @('S2CoopAlife', 'S2CoopNative', 'NolaAlife')) {
        $oldDir = Join-Path $ue ('Mods\' + $oldMod)
        if ([System.IO.Directory]::Exists($oldDir)) { RemoveTree $oldDir; Say ('[ok] old ' + $oldMod + ' mod removed (kept in the backup)') }
    }
    PutPayload ($Ue4ssRel + '/Mods/NolaNative/dlls/main.dll') (Join-Path $ue 'Mods\NolaNative\dlls\main.dll')
    PutPayload ($Ue4ssRel + '/Mods/NolaNative/assets/nola_title.png') (Join-Path $ue 'Mods\NolaNative\assets\nola_title.png')
    MergeModsTxt (Join-Path $ue 'Mods\mods.txt')
    Say '[ok] NolaNative installed and enabled in mods.txt'

    # 3. The game data pak: older NOLA paks out, the new one in. Other mods' paks stay.
    $old = 0
    if ([System.IO.Directory]::Exists($mods)) {
        foreach ($pat in @('zzzzzz_S2Coop*', 'zzzzzz_Nola*')) {
            foreach ($f in [System.IO.Directory]::GetFiles($mods, $pat)) { Remove $f; $old++ }
        }
    }
    PutPayload 'game/Stalker2/Content/Paks/~mods/zzzzzz_Nola_P.pak' (Join-Path $mods 'zzzzzz_Nola_P.pak')
    Say ('[ok] pak installed (' + $old + ' old NOLA paks removed, kept in the backup)')

    # 4. The settings file flags.txt with the connection choice.
    $oldRole = Join-Path $br 'role.txt'
    if ([System.IO.File]::Exists($oldRole)) { Remove $oldRole; Say '[ok] old role.txt removed (the role is picked in the main menu now)' }
    WriteFlags (Join-Path $br 'flags.txt') $netMode $peerIp
    $extra = ''
    if ($netMode -eq 'ip') { $extra = ', netpeer=' + $peerIp }
    Say ('[ok] settings: ' + $br + '\flags.txt net=' + $netMode + $extra)

    # 5. The firewall rule, only for IP mode. Steam friends needs none.
    if ($netMode -eq 'ip') { FirewallAdd (Join-Path $w64 $ExeName) 'co-op' ([string]$HostPort + ',' + $ClientPort) }
    else { Say '[ok] firewall: no rule needed for Steam friends, none added' }

    Say ''
    Say "Done. Start the game. In the main menu pick Host co-op (then load your world) or Join co-op (the host's world follows)."
    if ($netMode -eq 'steam') {
        Say 'Steam friends: the host presses Home in game to invite up to four friends; each friend accepts the invite or uses Join Game in the Steam friends list.'
    }
    Say 'To remove everything and restore your files: run uninstall.bat.'
    return 0
}

# ---------------------------------------------------------------------------
# Uninstall: undo the journal, newest change first
# ---------------------------------------------------------------------------
function Uninstall-From([string]$bdir) {
    $state = Join-Path $bdir 'state.txt'
    if (-not [System.IO.File]::Exists($state)) { return 1 }
    $rows = Journal-Read $state
    $restored = 0
    $removed = 0
    for ($i = $rows.Count - 1; $i -ge 0; $i--) {
        $r = $rows[$i]
        if ($r[0] -eq 'F') {
            $target = $r[1]
            if ([System.IO.File]::Exists($target)) {
                [System.IO.File]::SetAttributes($target, [System.IO.FileAttributes]::Normal)
                [System.IO.File]::Delete($target)
                $removed++
            }
            if ($r[2] -ne '-') {
                $dir = [System.IO.Path]::GetDirectoryName($target)
                if (-not [System.IO.Directory]::Exists($dir)) { [void][System.IO.Directory]::CreateDirectory($dir) }
                [System.IO.File]::Copy((Join-Path (Join-Path $bdir 'files') $r[2]), $target, $true)
                $restored++
            }
        }
        elseif ($r[0] -eq 'D') {
            if ([System.IO.Directory]::Exists($r[1]) -and [System.IO.Directory]::GetFileSystemEntries($r[1]).Length -eq 0) { [System.IO.Directory]::Delete($r[1]) }
        }
        elseif ($r[0] -eq 'W') { FirewallRemove $r[1] }
    }
    [System.IO.Directory]::Delete($bdir, $true)
    Say ('[ok] uninstall done: ' + $restored + ' files restored, ' + $removed + ' files removed')
    return 0
}

function Uninstall {
    $g = PickGame
    CheckRunning $g
    $old = Join-Path (OldBackupDir $g) 'state.txt'
    $new = Join-Path (BackupDir $g) 'state.txt'
    if (-not [System.IO.File]::Exists($old) -and -not [System.IO.File]::Exists($new)) {
        Say ('No NOLA install journal found in ' + (Win64 $g) + ': nothing to uninstall.')
        return 1
    }

    # Administrator rights only when needed.
    $why = @()
    if (-not (IsAdmin)) {
        $mods = Join-Path $g 'Stalker2\Content\Paks\~mods'
        if (-not (Test-Writable (Win64 $g)) -or -not (Test-Writable $mods)) { $why += 'the game folder cannot be written by your user' }
        foreach ($b in @((JournalBridge $old), (JournalBridge $new))) {
            if ($b -and -not (Test-Writable $b)) { $why += ('the settings folder ' + $b + ' cannot be written by your user') }
        }
        if (-not $NoFw -and ((JournalHasRule $old) -or (JournalHasRule $new))) { $why += 'the firewall rule of the IP mode has to be removed' }
    }
    if ($why.Count -gt 0) {
        $a = @('-Action', 'uninstall', '-Game', (Quote $g))
        if ($NoFw) { $a += '-NoFw' }
        return Restart-Elevated $a ($why -join '; ')
    }

    $rc1 = 1
    $rc2 = 1
    if ([System.IO.File]::Exists($old)) { $rc1 = Uninstall-From (OldBackupDir $g) }
    if ([System.IO.File]::Exists($new)) { $rc2 = Uninstall-From (BackupDir $g) }
    if ($rc1 -ne 0 -and $rc2 -ne 0) { Say ('No NOLA install journal found in ' + (Win64 $g) + ': nothing to uninstall.'); return 1 }
    return 0
}

# ---------------------------------------------------------------------------
# Start
# ---------------------------------------------------------------------------
function Run {
    $script:Game = $script:Game.Trim('"').TrimEnd('\')
    $script:Steam = $Steam.TrimEnd('\')
    Load-Payload
    Say ('=== NOLA setup ' + $NolaVersion + ' ===')
    $verb = $Action.ToLowerInvariant()
    if ($verb -eq 'find') {
        $g = FindGame
        if ($g.Length -gt 0) { Say ('Game folder: ' + $g); return 0 }
        Say 'Game folder not found'
        return 1
    }
    if ($verb.Length -eq 0) {
        Say '1) Install'
        Say '2) Uninstall (restore the original files)'
        $c = Ask 'Choose 1 or 2: '
        if ($c -eq '2') { $verb = 'uninstall' } else { $verb = 'install' }
    }
    if ($verb -eq 'uninstall') { return Uninstall }
    if ($verb -eq 'install') { return Install }
    Fail ('unknown -Action: ' + $Action + ' (use install, uninstall or find)')
}

$rc = 1
try { $rc = Run }
catch [NolaStop] { $rc = 2 }
catch { Write-Host ('ERROR: ' + $_.Exception.Message); $rc = 3 }
if ($Pause) { Write-Host ''; [void](Read-Host 'Press Enter to close') }
exit $rc
