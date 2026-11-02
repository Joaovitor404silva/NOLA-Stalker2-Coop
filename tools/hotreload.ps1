param(
    [string]$Dll = '',
    [string[]]$Sides = @('A', 'B'),
    [int]$TimeoutSec = 30,
    [switch]$Clean,
    [switch]$Status,
    [int]$Pair = 2
)
$ErrorActionPreference = 'Stop'

$Map = @{
    A = @{ Bridge = 'C:\nola_inst\C'; Root = 'C:\S2DevRootHost2' }
    B = @{ Bridge = 'C:\nola_inst\D'; Root = 'C:\S2DevRoot2' }
    E = @{ Bridge = 'C:\nola_inst\E'; Root = 'C:\S2DevRoot3' }
}
if ($Pair -eq 3) {
    $Map.A = @{ Bridge = 'C:\nola_inst\P3A'; Root = 'C:\S2DevRootHostP3' }
    $Map.B = @{ Bridge = 'C:\nola_inst\P3B'; Root = 'C:\S2DevRootP3' }
    $Map.Remove('E')
    $Sides = @($Sides | Where-Object { $_ -ne 'E' })
} elseif ($Pair -ne 2) { throw 'hotreload: -Pair must be 2 or 3' }

function Say($m) { Write-Host ('[' + (Get-Date -Format 'HH:mm:ss.fff') + '] ' + $m) }
function LogOf($k) { Join-Path $Map[$k].Root 'Stalker2\Binaries\Win64\ue4ss\UE4SS.log' }
function DllsOf($k) {
    $ini = Join-Path $Map[$k].Root 'Stalker2\Binaries\Win64\ue4ss\UE4SS-settings.ini'
    $l = Get-Content -LiteralPath $ini | Where-Object { $_ -match '^ModsFolderPath\s*=' } | Select-Object -First 1
    if (-not $l) { throw "no ModsFolderPath in $ini" }
    Join-Path (($l -replace '^ModsFolderPath\s*=\s*', '').Trim()) 'NolaNative\dlls'
}
function LogSize($p) { if (Test-Path -LiteralPath $p) { (Get-Item -LiteralPath $p).Length } else { 0 } }
function LogFrom($p, [long]$off) {
    if (-not (Test-Path -LiteralPath $p)) { return '' }
    $fs = [IO.File]::Open($p, 'Open', 'Read', 'ReadWrite,Delete')
    try {
        if ($off -gt $fs.Length) { $off = 0 }
        [void]$fs.Seek($off, 'Begin')
        $b = New-Object byte[] ($fs.Length - $off)
        $n = $fs.Read($b, 0, $b.Length)
        [Text.Encoding]::UTF8.GetString($b, 0, $n)
    } finally { $fs.Dispose() }
}
function Running($k) {
    $exe = Join-Path $Map[$k].Root 'Stalker2\Binaries\Win64\Stalker2-Win64-Shipping.exe'
    @(Get-Process -Name 'Stalker2-Win64-Shipping' -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $exe }).Count -gt 0
}

$Sides = @($Sides | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim().ToUpper() } | Where-Object { $_ })
foreach ($k in $Sides) { if (-not $Map.ContainsKey($k)) { throw "unknown side $k (A, B, E)" } }

if ($Status) {
    foreach ($k in $Sides) {
        $l = Select-String -LiteralPath (LogOf $k) -Pattern 'NOLA hot reload' -ErrorAction SilentlyContinue | Select-Object -Last 3
        Say "$k running=$(Running $k)"
        foreach ($x in $l) { Say "  $($x.Line)" }
    }
    exit 0
}

if ($Clean) {
    foreach ($k in $Sides) {
        if (Running $k) { Say "$k runs: its loaded copies stay (the stub removes stale ones at the next boot)" }
        $d = DllsOf $k
        foreach ($f in @(Get-ChildItem -LiteralPath $d -Filter 'hot_*.dll' -ErrorAction SilentlyContinue)) {
            try { Remove-Item -LiteralPath $f.FullName -Force; Say "$k removed $($f.FullName)" } catch { Say "$k kept $($f.FullName) (in use)" }
        }
        $h = Join-Path $Map[$k].Bridge 'hot'
        if ((Test-Path -LiteralPath $h) -and -not (Running $k)) { Remove-Item -LiteralPath $h -Recurse -Force; Say "$k removed $h" }
    }
    exit 0
}

if (-not $Dll) { throw '-Dll <dev NolaNative.dll> is required' }
if (-not (Test-Path -LiteralPath $Dll)) { throw "missing $Dll" }
$md5 = (Get-FileHash -LiteralPath $Dll -Algorithm MD5).Hash.ToLower()
$token = (Get-Date -Format 'yyyyMMddHHmmssfff') + '-' + $md5.Substring(0, 8)
$t0 = Get-Date
$off = @{}
foreach ($k in $Sides) {
    if (-not (Running $k)) { throw "$k is not running" }
    $off[$k] = LogSize (LogOf $k)
    $h = Join-Path $Map[$k].Bridge 'hot'
    New-Item -ItemType Directory -Force $h | Out-Null
    Copy-Item -LiteralPath $Dll -Destination (Join-Path $h 'next.dll') -Force
    [IO.File]::WriteAllText((Join-Path $h 'go'), $token)
}
Say "token $token (md5 $md5) sent to $($Sides -join ', ')"
$left = [System.Collections.Generic.List[string]]::new(); foreach ($k in $Sides) { $left.Add($k) }
$bad = 0
while ($left.Count -and ((Get-Date) - $t0).TotalSeconds -lt $TimeoutSec) {
    foreach ($k in @($left)) {
        $txt = LogFrom (LogOf $k) $off[$k]
        $m = [regex]::Match($txt, "NOLA hot reload: (gen \d+ -> \d+ token=$token ok[^\r\n]*|FAILED token=$token[^\r\n]*)")
        if ($m.Success) {
            $s = ((Get-Date) - $t0).TotalSeconds
            Say ("{0} {1:N1} s: {2}" -f $k, $s, $m.Groups[1].Value)
            if ($m.Groups[1].Value -like 'FAILED*') { $bad++ }
            [void]$left.Remove($k)
        } elseif (-not (Running $k)) {
            Say "$k process is gone (crash?)"; $bad++; [void]$left.Remove($k)
        }
    }
    if ($left.Count) { Start-Sleep -Milliseconds 200 }
}
foreach ($k in $left) { Say "$k no reload line in $TimeoutSec s"; $bad++ }
Say ("done in {0:N1} s, {1} failed" -f ((Get-Date) - $t0).TotalSeconds, $bad)
exit $(if ($bad) { 1 } else { 0 })
