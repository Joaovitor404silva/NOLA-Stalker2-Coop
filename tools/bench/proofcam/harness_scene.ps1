$PROOFCAM = Join-Path $PSScriptRoot 'proofcam.ps1'
if (-not (Test-Path -LiteralPath $PROOFCAM)) { $PROOFCAM = "$G\ModWork\tools\proofcam\proofcam.ps1" }
. $PROOFCAM
$SCENEDIR = "$H\scenes"
$NOLATEXTS = 'D:\modding\cpp\wt\ps1\data\nola_texts.tsv'
foreach ($cand in @("$G\ModWork\s2coop\data\nola_texts.tsv", "$H\scenes\nola_texts.tsv")) { if (-not (Test-Path -LiteralPath $NOLATEXTS) -and (Test-Path -LiteralPath $cand)) { $NOLATEXTS = $cand } }

function Side-Of([string]$s) {
  switch ($s.ToUpper()) { 'C' { 'A' } 'A' { 'A' } 'D' { 'B' } 'B' { 'B' } 'E' { 'E' } default { Fail "unknown side $s (C|D|E or A|B|E)" } }
}
function Bridge-Letter($k) { switch ($k) { 'A' { 'C' } 'B' { 'D' } 'E' { 'E' } } }

function Invoke-Nbot($k, [string[]]$lines, [int]$TimeoutMs = 20000) {
  $io = "$H\bench\io\$($SIDES[$k].Port)"
  if (-not (Test-Path -LiteralPath $io)) { Fail "nbot io folder $io missing (is nbot loaded on $k?)" }
  $id = [guid]::NewGuid().ToString('N').Substring(0, 12)
  $tmp = "$io\req_$id.tmp"; $req = "$io\req_$id.txt"; $res = "$io\res_$id.txt"
  [IO.File]::WriteAllText($tmp, (($lines -join "`n") + "`n"), (New-Object Text.ASCIIEncoding))
  Move-Item -LiteralPath $tmp $req -Force
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.ElapsedMilliseconds -lt $TimeoutMs) {
    if (Test-Path -LiteralPath $res) { Start-Sleep -Milliseconds 30; $t = [IO.File]::ReadAllText($res); Remove-Item -LiteralPath $res -Force -ErrorAction SilentlyContinue; return $t.Trim() }
    Start-Sleep -Milliseconds 50
  }
  if (Test-Path -LiteralPath $req) { Remove-Item -LiteralPath $req -Force -ErrorAction SilentlyContinue; Fail "nbot on $k did not take the request in $TimeoutMs ms (plugin not loaded? s2dev plug reload nbot)" }
  Fail "nbot on $k took the request but wrote no answer in $TimeoutMs ms"
}

function Wait-SceneResult($k, [int]$TimeoutS) {
  $f = "$H\bench\io\$($SIDES[$k].Port)\scene_result.json"
  $sw = [Diagnostics.Stopwatch]::StartNew(); $last = $null
  while ($sw.Elapsed.TotalSeconds -lt $TimeoutS) {
    if (Test-Path -LiteralPath $f) {
      try { $j = [IO.File]::ReadAllText($f) | ConvertFrom-Json; $last = $j; if ($j.state -ne 'running') { return $j } } catch { }
    }
    if (-not (Get-SideProc $k)) { Fail "DEAD: $k exited while its scene ran" }
    Start-Sleep -Milliseconds 250
  }
  if ($last) { [void](Invoke-Nbot $k @('scene stop')); $last.state = 'timeout'; return $last }
  Fail "no scene_result.json from $k in $TimeoutS s"
}

function Read-Scene([string]$name, [string]$defSide) {
  $path = if (Test-Path -LiteralPath $name) { (Resolve-Path -LiteralPath $name).Path } else { Join-Path $SCENEDIR (($name -replace '\.scene$', '') + '.scene') }
  if (-not (Test-Path -LiteralPath $path)) { Fail "scene $name not found ($path)" }
  $lines = [IO.File]::ReadAllLines($path)
  $steps = @(); $title = [IO.Path]::GetFileNameWithoutExtension($path)
  for ($i = 0; $i -lt $lines.Count; $i++) {
    $l = $lines[$i].Trim(); if ($i -eq 0) { $l = $l.TrimStart([char]0xFEFF) }
    if (-not $l -or $l.StartsWith('#')) { continue }
    $side = $defSide
    if ($l -match '^@([A-Ea-e])\s+(.*)$') { $side = Side-Of $Matches[1]; $l = $Matches[2].Trim() }
    $op = ($l -split '\s+')[0]
    if ($op -eq 'scene') { $title = ($l -split '\s+', 2)[1]; continue }
    $steps += [pscustomobject]@{ Line = $i + 1; Side = $side; Op = $op; Text = $l; Harness = ($op -in 'key', 'shot') }
  }
  [pscustomobject]@{ Name = $title; Path = $path; Count = $lines.Count; Steps = $steps }
}

function Step-Timeout([string]$text) {
  $op = ($text -split '\s+')[0]
  $t = if ($text -match '\btimeout=([0-9.]+)') { [double]$Matches[1] } else { 20 }
  if ($op -eq 'wait' -and $text -match '^wait\s+([0-9.]+)\s*$') { $t = [double]$Matches[1] }
  $t + 25
}

function Log-Sizes { $z = @{}; foreach ($k in @('A', 'B', 'E')) { $p = LogPath $k; $z[$k] = if (Test-Path -LiteralPath $p) { (Get-Item -LiteralPath $p).Length } else { -1 } }; $z }
function Run-SceneSegment($sc, $seg, $runDir, [long]$logoff = -1) {
  $k = $seg[0].Side
  $arr = New-Object string[] $sc.Count
  foreach ($s in $seg) { $arr[$s.Line - 1] = $s.Text }
  New-Item -ItemType Directory -Force "$H\tmp" | Out-Null
  $tmp = "$H\tmp\scene_$($sc.Name)_$(Bridge-Letter $k)_$($seg[0].Line).scene"
  [IO.File]::WriteAllLines($tmp, [string[]]($arr | ForEach-Object { if ($_) { $_ } else { '' } }))
  $o = Invoke-Nbot $k @("scene run $tmp logoff=$logoff")
  if ($o -notmatch '^scene \S+ running') { return @([pscustomobject]@{ Line = $seg[0].Line; Side = $k; Op = $seg[0].Op; Text = $seg[0].Text; Result = 'ERR'; Ms = 0; Detail = "nbot: $o"; Data = $null }) }
  $budget = ($seg | ForEach-Object { Step-Timeout $_.Text } | Measure-Object -Sum).Sum
  $j = Wait-SceneResult $k ([int]$budget)
  $rows = @()
  foreach ($r in @($j.results)) { $rows += [pscustomobject]@{ Line = $r.line; Side = $k; Op = $r.op; Text = $r.text; Result = $(if ($r.result -eq 'RUN') { 'TIMEOUT' } else { $r.result }); Ms = $r.ms; Detail = $r.detail; Data = $r.data } }
  $done = @($rows | ForEach-Object { $_.Line })
  foreach ($s in $seg) { if ($done -notcontains $s.Line) { $rows += [pscustomobject]@{ Line = $s.Line; Side = $k; Op = $s.Op; Text = $s.Text; Result = 'SKIP'; Ms = 0; Detail = "scene state $($j.state)"; Data = $null } } }
  $rows
}

function Run-HarnessStep($s, $runDir) {
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $res = 'PASS'; $det = ''; $data = $null
  try {
    $t = $s.Text -split '\s+'
    if ($s.Op -eq 'key') { $det = Send-Input $s.Side 'hold' @{ Vk = (KeyHex $t[1]); Ms = [int]$t[2] } }
    elseif ($s.Op -eq 'shot') {
      $kk = 'peer'; foreach ($x in $t) { if ($x -match '^key=(.+)$') { $kk = $Matches[1] } }
      $shotKeys = if ($t -contains 'all=1') { @(@('A', 'B', 'E') | Where-Object { Get-SideProc $_ }) } else { @($s.Side) }
      $r = Invoke-ProofShot $shotKeys $t[1] $kk 3000 $runDir
      $det = "shot $($r.Dir) skew=$($r.Skew) ms"; $data = $r.Items
      if (-not $r.Ok) { $res = 'FAIL' }
    }
  } catch { $res = 'ERR'; $det = "$($_.Exception.Message)" }
  [pscustomobject]@{ Line = $s.Line; Side = $s.Side; Op = $s.Op; Text = $s.Text; Result = $res; Ms = $sw.ElapsedMilliseconds; Detail = $det; Data = $data }
}

function Write-SceneReport($sc, $rows, $dir, $t0) {
  $cnt = @{}; foreach ($r in $rows) { $cnt[$r.Result] = 1 + [int]$cnt[$r.Result] }
  $bad = [int]$cnt['FAIL'] + [int]$cnt['ERR'] + [int]$cnt['TIMEOUT'] + [int]$cnt['DEAD']
  $verdict = if ($bad -or -not $rows) { 'FAIL' } else { 'PASS' }
  $o = New-Object System.Collections.ArrayList
  [void]$o.Add("S2 harness scene report  scene=$($sc.Name)  file=$($sc.Path)")
  [void]$o.Add("start=$($t0.ToString('yyyy-MM-dd HH:mm:ss'))  secs=$([int]((Get-Date) - $t0).TotalSeconds)")
  [void]$o.Add('')
  [void]$o.Add(('{0,3}  {1,4}  {2,-7}  {3,-4}  {4,7}  {5}' -f '#', 'line', 'result', 'side', 'ms', 'step'))
  $n = 0
  foreach ($r in $rows) {
    $n++
    [void]$o.Add(('{0,3}  {1,4}  {2,-7}  {3,-4}  {4,7}  {5}' -f $n, $r.Line, $r.Result, (Bridge-Letter $r.Side), $r.Ms, $r.Text))
    if ($r.Detail) { [void]$o.Add('                                     -> ' + (Short $r.Detail 400)) }
  }
  [void]$o.Add('')
  [void]$o.Add(('TOTAL ' + (($cnt.Keys | Sort-Object | ForEach-Object { "$_=$($cnt[$_])" }) -join ' ') + "  VERDICT $verdict"))
  [IO.File]::WriteAllLines("$dir\report.txt", [string[]]$o)
  $json = [ordered]@{ scene = $sc.Name; path = $sc.Path; verdict = $verdict; start = $t0.ToString('o'); steps = @($rows | ForEach-Object { [ordered]@{ line = $_.Line; side = (Bridge-Letter $_.Side); op = $_.Op; text = $_.Text; result = $_.Result; ms = $_.Ms; detail = $_.Detail; data = $_.Data } }) }
  [IO.File]::WriteAllText("$dir\scene.json", ($json | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
  $verdict
}

function Cmd-Scene {
  if (-not $Spec) { Write-Host 'scene <name|file.scene> [-On C|D|E] [-DryRun]'; exit 2 }
  $def = if ($On) { Side-Of $On } else { 'A' }
  $sc = Read-Scene $Spec $def
  $errs = Check-SceneText $sc
  if ($errs) { $errs | ForEach-Object { Write-Host "ERROR $_" }; exit 1 }
  if ($DryRun) { $sc.Steps | ForEach-Object { Write-Host ("{0,4} {1} {2,-8} {3}" -f $_.Line, (Bridge-Letter $_.Side), $(if ($_.Harness) { 'harness' } else { 'nbot' }), $_.Text) }; Write-Host "steps=$($sc.Steps.Count)"; exit 0 }
  $run = Get-CurrentRun; if (-not $run) { $run = New-RunDir }
  $dir = Join-Path $run ("scene_$($sc.Name)_" + (Get-Date -Format 'HHmmss')); New-Item -ItemType Directory -Force $dir | Out-Null
  $t0 = Get-Date; $rows = @(); $i = 0; $stop = $false; $cur = Log-Sizes
  while ($i -lt $sc.Steps.Count) {
    $s = $sc.Steps[$i]; $prev = $cur; $cur = Log-Sizes
    if ($stop) { $rows += [pscustomobject]@{ Line = $s.Line; Side = $s.Side; Op = $s.Op; Text = $s.Text; Result = 'SKIP'; Ms = 0; Detail = 'an earlier step failed'; Data = $null }; $i++; continue }
    if (-not (Get-SideProc $s.Side)) { $rows += [pscustomobject]@{ Line = $s.Line; Side = $s.Side; Op = $s.Op; Text = $s.Text; Result = 'ERR'; Ms = 0; Detail = "$($s.Side) is not running"; Data = $null }; $stop = $true; $i++; continue }
    if ($s.Harness) { $r = Run-HarnessStep $s $dir; $rows += $r; Write-Host ("{0,4} {1} {2,-7} {3}  {4}" -f $r.Line, (Bridge-Letter $r.Side), $r.Result, $r.Text, (Short $r.Detail 140)); if ($r.Result -ne 'PASS') { $stop = $true }; $i++; continue }
    $seg = @($s); $j = $i + 1
    while ($j -lt $sc.Steps.Count -and -not $sc.Steps[$j].Harness -and $sc.Steps[$j].Side -eq $s.Side) { $seg += $sc.Steps[$j]; $j++ }
    try { $rr = @(Run-SceneSegment $sc $seg $dir ([long]$prev[$s.Side])) } catch { $rr = @([pscustomobject]@{ Line = $s.Line; Side = $s.Side; Op = $s.Op; Text = $s.Text; Result = $(if ("$($_.Exception.Message)" -like '*DEAD:*') { 'DEAD' } else { 'ERR' }); Ms = 0; Detail = "$($_.Exception.Message)"; Data = $null }) }
    foreach ($r in $rr) { Write-Host ("{0,4} {1} {2,-7} {3}  {4}" -f $r.Line, (Bridge-Letter $r.Side), $r.Result, $r.Text, (Short $r.Detail 140)) }
    $rows += $rr
    if (@($rr | Where-Object { $_.Result -ne 'PASS' }).Count) { $stop = $true }
    $i = $j
  }
  $v = Write-SceneReport $sc $rows $dir $t0
  Add-Content -LiteralPath (Join-Path $run 'scenes.txt') -Value "$(Get-Date -Format 'HH:mm:ss') scene $($sc.Name) $v $dir" -Encoding ascii
  Say "scene $($sc.Name): $dir\report.txt and scene.json  VERDICT $v"
  exit $(if ($v -eq 'PASS') { 0 } else { 1 })
}

function Check-SceneText($sc) {
  $errs = @()
  $known = @('note', 'closeui', 'hudclean', 'require', 'spawn', 'kill', 'faction', 'give', 'hp', 'god', 'time', 'wait', 'expect', 'tp', 'frame', 'box', 'bot', 'key', 'shot')
  foreach ($s in $sc.Steps) {
    if ($known -notcontains $s.Op) { $errs += "line $($s.Line): unknown step $($s.Op)" }
    if ($s.Op -eq 'key' -and $s.Text -notmatch '^key\s+\S+\s+\d+') { $errs += "line $($s.Line): key <KEY> <ms>" }
    if ($s.Op -eq 'shot' -and $s.Text -notmatch '^shot\s+\S+') { $errs += "line $($s.Line): shot <name> [key=<k>] [all=1]" }
  }
  $errs
}

function Cmd-Frame {
  $k = if ($On) { Side-Of $On } else { 'A' }
  $key = if ($Key) { $Key } elseif ($Spec) { $Spec } else { 'peer' }
  $line = "frame $key"
  if ($Dist -gt 0) { $line += " move=1 dist=$Dist" }
  if ($Fov) { $line += " fov=$Fov" }
  $o = Invoke-Nbot $k @("scene line $line")
  if ($o -notmatch '^scene \S+ running') { Fail "nbot: $o" }
  $j = Wait-SceneResult $k 60
  $r = @($j.results)[-1]
  Say "frame $key on $(Bridge-Letter $k): $($r.result) $($r.detail)"
  if ($r.data) { $r.data | ConvertTo-Json -Compress -Depth 5 | Write-Host }
  exit $(if ($r.result -eq 'PASS') { 0 } else { 1 })
}

function Invoke-ProofShot([string[]]$shotKeys, [string]$name, [string]$key, [long]$lead, [string]$outRoot) {
  if (-not $name) { $name = 'shot' }
  $dir = Join-Path $outRoot ("shot_${name}_" + (Get-Date -Format 'HHmmss')); New-Item -ItemType Directory -Force $dir | Out-Null
  $now = PC-NowMs
  $at = if ($lead -gt 1000000000000) { $lead } else { $now + [Math]::Max(1500, $lead) }
  $boxes = @{}; $wins = @{}
  foreach ($k in $shotKeys) {
    $p = Get-SideProc $k; if (-not $p) { Say "$k is not running, no shot"; continue }
    $gp = Get-Process -Id $p.ProcessId -ErrorAction SilentlyContinue
    $wins[$k] = @{ Pid = $p.ProcessId; H = $gp.MainWindowHandle }
    $bf = "$dir\$(Bridge-Letter $k)_box.json"; $boxes[$k] = $bf
    $o = Invoke-Nbot $k @("scene line box $key at=$at out=$bf timeout=30")
    if ($o -notmatch '^scene \S+ running') { Say "$k box request: $o" }
  }
  while ((PC-NowMs) -lt $at - 15) { Start-Sleep -Milliseconds 5 }
  while ((PC-NowMs) -lt $at) { }
  $caps = @{}
  foreach ($k in $wins.Keys) { $png = "$dir\$(Bridge-Letter $k)_${name}_tmp.png"; $caps[$k] = PC-Grab $wins[$k].H $png; $caps[$k].Png = $png }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  while ($sw.Elapsed.TotalSeconds -lt 20 -and @($boxes.Values | Where-Object { -not (Test-Path -LiteralPath $_) }).Count) { Start-Sleep -Milliseconds 200 }
  $texts = PC-LoadTexts $NOLATEXTS
  $items = @(); $ok = $true; $skews = @()
  foreach ($k in ($wins.Keys | Sort-Object)) {
    $c = $caps[$k]; $L = Bridge-Letter $k
    $box = $null; if (Test-Path -LiteralPath $boxes[$k]) { try { $box = [IO.File]::ReadAllText($boxes[$k]) | ConvertFrom-Json } catch { } }
    $res = $null; try { $res = (Get-Content -LiteralPath "$H\bench\io\$($SIDES[$k].Port)\scene_result.json" -Raw | ConvertFrom-Json) } catch { }
    if (-not $c.Ok) { $ok = $false; $items += [ordered]@{ side = $L; error = $c.Err; box = $box }; continue }
    $md5 = PC-Md5 $c.Png
    $final = "$dir\${L}_${name}_$($md5.Substring(0,8)).png"
    Move-Item -LiteralPath $c.Png $final -Force
    $ocr = @(); $match = @()
    try { $ocr = @(PC-Ocr $final); $match = @(PC-MatchTexts $ocr $texts | ForEach-Object { [ordered]@{ id = $_.Id; lang = $_.Lang; text = $_.Text; ocr = $_.Ocr; dist = $_.Dist } }) } catch { $ocr = @("ocr error: $($_.Exception.Message)") }
    $skew = [long]$c.Ms - $at; $skews += $skew
    $boxLate = if ($box) { [long]$box.wall_ms - $at } else { $null }
    $side = [ordered]@{
      side = $L; role = $SIDES[$k].Role; pid = $wins[$k].Pid; png = (Split-Path $final -Leaf); md5 = $md5; at_ms = $at
      capture = [ordered]@{ method = $c.Method; wall_ms = $c.Ms; took_ms = $c.Took; skew_ms = $skew; w = $c.W; h = $c.H; dark = [Math]::Round($c.Dark, 3) }
      census_box_late_ms = $boxLate; target = $box; box_step = $(if ($res) { @($res.results)[-1] } else { $null })
      ocr = [ordered]@{ advisory = $true; lines = $ocr; nola_texts = $match }
    }
    if (-not $box) { $ok = $false }
    [IO.File]::WriteAllText(($final -replace '\.png$', '.json'), ($side | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding $false))
    $items += $side
  }
  if (-not $items) { $ok = $false }
  $mitems = @($items | Where-Object { $_.png } | ForEach-Object { @{ Png = (Join-Path $dir $_.png); Sidecar = $_.target; Label = "$($_.side) $($_.role) md5 $($_.md5.Substring(0,8))"; Extra = "capture $($_.capture.method) skew $($_.capture.skew_ms) ms, census box $($_.census_box_late_ms) ms" } })
  $mont = $null; if ($mitems.Count) { $mont = PC-Montage $mitems "$dir\montage_$name.png" }
  $sk = if ($skews.Count) { ($skews | Measure-Object -Maximum).Maximum - ($skews | Measure-Object -Minimum).Minimum } else { -1 }
  $sum = [ordered]@{ name = $name; at_ms = $at; key = $key; sides = @($items | ForEach-Object { $_.side }); spread_ms = $sk; montage = $(if ($mont) { Split-Path $mont -Leaf } else { $null }); ok = $ok }
  [IO.File]::WriteAllText("$dir\shot.json", ($sum | ConvertTo-Json -Depth 4), (New-Object Text.UTF8Encoding $false))
  @{ Ok = $ok; Dir = $dir; Skew = $sk; Items = $items; Montage = $mont }
}

function Cmd-Shot {
  $shotKeys = if ($All) { @(@('A', 'B', 'E') | Where-Object { Get-SideProc $_ }) } elseif ($On) { @($On -split ',' | ForEach-Object { Side-Of $_ }) } else { @('A') }
  if (-not $shotKeys) { Fail 'no running side to shoot' }
  $lead = if ($At) { [long]$At } else { 4000 }
  $run = Get-CurrentRun; if (-not $run) { $run = New-RunDir }
  $r = Invoke-ProofShot $shotKeys $(if ($Spec) { $Spec } else { 'shot' }) $(if ($Key) { $Key } else { 'peer' }) $lead $run
  foreach ($it in $r.Items) {
    if ($it.error) { Say "$($it.side): capture failed: $($it.error)"; continue }
    $t = $it.target
    Say ("{0}: {1} md5 {2} {3} skew {4} ms | target {5} dist {6} m vis {7} rendered {8} | ocr hits {9}" -f $it.side, $it.png, $it.md5.Substring(0, 8), $it.capture.method, $it.capture.skew_ms, $(if ($t) { $t.key } else { 'none' }), $(if ($t) { [Math]::Round([double]$t.dist / 100, 1) } else { '-' }), $(if ($t) { $t.vis } else { '-' }), $(if ($t) { $t.rendered } else { '-' }), @($it.ocr.nola_texts).Count)
  }
  Say "shot: $($r.Dir) (montage $($r.Montage)) spread $($r.Skew) ms ok=$($r.Ok)"
  exit $(if ($r.Ok) { 0 } else { 1 })
}
