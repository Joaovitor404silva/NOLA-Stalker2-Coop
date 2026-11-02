param([string]$HostBridge = 'C:\nola_inst\C', [string]$ClientBridge = 'C:\nola_inst\D', [int]$Port = 7799, [string]$Out = "$env:TEMP\nola_m4_five")
$ErrorActionPreference = 'Stop'
$fp = Join-Path $PSScriptRoot '..\..\modwork\s2coop\tools\s2fake\fakepeer.py'
New-Item -ItemType Directory -Force $Out | Out-Null
Get-ChildItem $Out -File | Remove-Item

function Dev([string]$bridge, [string]$cmd) {
    $o = Join-Path $bridge 's2cmd_out.txt'
    $n = 0; if (Test-Path $o) { $n = (Get-Item $o).Length }
    Set-Content (Join-Path $bridge 's2cmd.txt') $cmd -Encoding ascii
    for ($i = 0; $i -lt 20; $i++) {
        Start-Sleep -Milliseconds 500
        if ((Test-Path $o) -and (Get-Item $o).Length -gt $n) { return [IO.File]::ReadAllText($o).Substring([int]$n) }
    }
    throw "no devcmd answer from $bridge"
}
function Fake([string]$name, [int]$secs) {
    Start-Process -WindowStyle Hidden uv -ArgumentList @('run', '--no-project', 'python', '-I', $fp, '--peer', "127.0.0.1:$Port", '--listen', '0', '--name', $name, '--seconds', $secs, '--log', "$Out\$name.log", '--summary', "$Out\$name.json")
}

$fail = 0
function Check([bool]$ok, [string]$what) { if ($ok) { "PASS $what" } else { "FAIL $what"; $script:fail++ } }

1..3 | ForEach-Object { Fake "fake$_" 90 }
Start-Sleep 20
Fake 'fake4' 30
Start-Sleep 8
$h = Dev $HostBridge 'status x0'
$c = Dev $ClientBridge 'status x0'
Check ($h -match 'hub clients=4/4') 'host hub has 4 clients (real client + 3 fakes = 5 players)'
Check ($c -match 'net on .*rx_age=[0-4]\.') 'real client still receiving'
1..3 | ForEach-Object { Check ((Get-Content "$Out\fake$_.log" -Raw) -match 'PEER UP') "fake$_ PEER UP" }
Check ((Get-Content "$Out\fake4.log" -Raw) -match 'HOST FULL: max 4') '6th player refused (HOST FULL)'
Start-Sleep 60
$back = $false
for ($i = 0; $i -lt 24 -and -not $back; $i++) { $back = (Dev $HostBridge 'status x0') -match 'hub clients=1/4'; if (-not $back) { Start-Sleep 5 } }
Check $back 'fakes left (no bye, host drops them on silence): host back to 1 client within 120 s'
Check ((Dev $ClientBridge 'status x0') -match 'net on .*rx_age=[0-4]\.') 'real client still receiving after the fakes left'
if ($fail) { "VERDICT FAIL ($fail)"; exit 1 } else { 'VERDICT PASS'; exit 0 }
