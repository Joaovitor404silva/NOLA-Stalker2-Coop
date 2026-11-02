param([int[]]$Ports = @(27063, 27062), [int]$TimeoutSec = 600)
$ErrorActionPreference = 'Stop'
$cli = 'C:\modding\s2dev\s2dev.js'
$bench = Split-Path -Parent $MyInvocation.MyCommand.Path
$py = "import sys; sys.path.insert(0, r'$bench'); import nbot; print(nbot.send(int(sys.argv[1]), 'ping', timeout=15))"
$deadline = (Get-Date).AddSeconds($TimeoutSec)
foreach ($port in $Ports) {
  $env:S2DEV_PORT = "$port"
  $ok = $false
  while ((Get-Date) -lt $deadline) {
    $r = (& node $cli plug reload nbot 2>&1 | Out-String).Trim()
    if ($r -match 'nbot: loaded') { $ok = $true; break }
    Start-Sleep -Seconds 5
  }
  if (-not $ok) { Write-Output "port ${port}: nbot NOT loaded ($r)"; continue }
  $p = (& uv run --no-project python -I -c $py $port 2>&1 | Out-String).Trim()
  Write-Output "port ${port}: $r; $p"
}
Write-Output 'Recorder ready: focus the game window, F7 = start/stop recording, F8 = mark the next tutorial segment.'
Write-Output 'Files: C:\modding\s2harness\bench\rec\27063 (host) and \27062 (client). Compile: uv run --no-project python -I tools\bench\reccompile.py <rec.jsonl>'
