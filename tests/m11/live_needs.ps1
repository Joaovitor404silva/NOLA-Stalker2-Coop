param([string]$Bridge = 'C:\nola_inst\D', [int]$Port = 27062, [string]$Log = 'C:\S2DevRoot2\Stalker2\Binaries\Win64\ue4ss\UE4SS.log', [switch]$NoRed)
$ErrorActionPreference = 'Stop'
$flags = Join-Path $Bridge 'flags.txt'

function Need([string]$fn) {
    $env:CRAWLER_PORT = "$Port"
    $o = (& node 'D:\modding\Game-Crawler\crawler.js' call pawn $fn 2>&1 | Out-String)
    $m = [regex]::Match($o, 'ReturnValue = (-?[\d\.]+)')
    if (-not $m.Success) { throw "no $fn answer: $o" }
    [double]$m.Groups[1].Value
}
function Needs { @{ H = Need 'GetCurrentHungerPoints'; S = Need 'GetCurrentSleepinessPoints' } }
function SetFlag([string]$k, [string]$v) {
    $t = @(Get-Content $flags)
    if ($t -match "^$k=") { $t = $t -replace "^$k=.*", "$k=$v" } else { $t += "$k=$v" }
    $t | Set-Content $flags -Encoding ascii
}
function Lines([datetime]$since, [string]$re) {
    Get-Content $Log -Tail 3000 | Where-Object { $_ -match $re -and $_ -match '^\[(\d{4}-\d\d-\d\d \d\d:\d\d:\d\d)' -and [datetime]$Matches[1] -ge $since }
}

$fail = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "PASS $what" } else { "FAIL $what"; $script:fail++ } }
try {
    SetFlag syncback 0
    SetFlag synctimetest -1
    $t0 = Get-Date; $a = Needs
    Start-Sleep 40
    $b = Needs
    "green: hunger $($a.H) -> $($b.H), sleepiness $($a.S) -> $($b.S)"
    Check ((Lines $t0 'TIME: sync gt ignorado').Count -ge 1) 'new code: client 3 h ahead, sync ignored'
    Check ((Lines $t0 'TIME: XSetGameTime\(.*sync gt').Count -eq 0) 'new code: no XSetGameTime from the sync'
    Check (($b.S - $a.S) -lt 1.5 -and ($b.H - $a.H) -lt 3) 'new code: needs only drift normally in 40 s'
    if (-not $NoRed) {
        SetFlag syncback 1
        $t1 = Get-Date; $c = Needs
        Start-Sleep 40
        $d = Needs
        "red: hunger $($c.H) -> $($d.H), sleepiness $($c.S) -> $($d.S)"
        Check ((Lines $t1 'TIME: XSetGameTime\(.*sync gt').Count -ge 1) 'old code (syncback=1): sync sets the clock back'
        Check (($d.S - $c.S) -ge 3 -or ($d.H - $c.H) -ge 3) 'old code: needs jump (a game day)'
    }
}
finally { SetFlag syncback 0; SetFlag synctimetest 0 }
if ($fail) { "VERDICT FAIL ($fail)"; exit 1 } else { 'VERDICT PASS'; exit 0 }
