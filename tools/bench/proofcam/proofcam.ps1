param([Parameter(Position = 0)][string]$Mode = '', [Parameter(Position = 1)][string]$A1, [Parameter(Position = 2)][string]$A2, [string]$Texts = 'D:\modding\cpp\wt\ps1\data\nola_texts.tsv')
Add-Type -AssemblyName System.Drawing
if (-not ('PCap' -as [type])) {
  Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class PCap {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [StructLayout(LayoutKind.Sequential)] public struct PT { public int X, Y; }
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref PT p);
  [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr h);
}
"@
}

function PC-NowMs { [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() }

function PC-Md5($p) { (Get-FileHash -LiteralPath $p -Algorithm MD5).Hash.ToLower() }

function PC-Dark([System.Drawing.Bitmap]$b) {
  $n = 0; $dark = 0
  for ($y = 8; $y -lt $b.Height; $y += [Math]::Max(1, [int]($b.Height / 24))) {
    for ($x = 8; $x -lt $b.Width; $x += [Math]::Max(1, [int]($b.Width / 32))) {
      $c = $b.GetPixel($x, $y); $n++; if (($c.R + $c.G + $c.B) -lt 12) { $dark++ }
    }
  }
  if ($n -eq 0) { return 1.0 }
  $dark / $n
}

function PC-Grab([IntPtr]$h, [string]$out) {
  $r = New-Object PCap+RECT
  if (-not [PCap]::GetClientRect($h, [ref]$r) -or $r.R -le 0 -or $r.B -le 0) { return @{ Ok = $false; Err = 'no client rect' } }
  if ([PCap]::IsIconic($h)) { return @{ Ok = $false; Err = 'window minimized' } }
  $wr = New-Object PCap+RECT; [void][PCap]::GetWindowRect($h, [ref]$wr)
  $p = New-Object PCap+PT; [void][PCap]::ClientToScreen($h, [ref]$p)
  $ox = $p.X - $wr.L; $oy = $p.Y - $wr.T
  $ww = $wr.R - $wr.L; $wh = $wr.B - $wr.T
  $full = New-Object System.Drawing.Bitmap $ww, $wh
  $g = [System.Drawing.Graphics]::FromImage($full)
  $t0 = PC-NowMs
  $hdc = $g.GetHdc(); $ok = [PCap]::PrintWindow($h, $hdc, 2); $g.ReleaseHdc($hdc)
  $t1 = PC-NowMs
  $g.Dispose()
  $b = $full.Clone((New-Object System.Drawing.Rectangle $ox, $oy, $r.R, $r.B), $full.PixelFormat); $full.Dispose()
  $dk = PC-Dark $b
  if ($ok -and $dk -lt 0.97) { $b.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose(); return @{ Ok = $true; Method = 'PrintWindow'; Ms = [long](($t0 + $t1) / 2); Took = ($t1 - $t0); W = $r.R; H = $r.B; Dark = $dk } }
  $b.Dispose()
  $ff = @("$env:LOCALAPPDATA\ffmpeg\bin\ffmpeg.exe", (Get-Command ffmpeg -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1)) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
  if (-not $ff) { return @{ Ok = $false; Err = "PrintWindow black (dark=$([Math]::Round($dk,2))) and no ffmpeg for gfxcapture" } }
  $t0 = PC-NowMs
  $eap = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
  $o = & $ff -hide_banner -loglevel error -y -f lavfi -i "gfxcapture=hwnd=$([int64]$h):capture_cursor=0:max_framerate=60,hwdownload,format=bgra" -frames:v 1 $out 2>&1 | Out-String
  $ErrorActionPreference = $eap
  $t1 = PC-NowMs
  if (-not (Test-Path -LiteralPath $out)) { return @{ Ok = $false; Err = "gfxcapture failed: $o" } }
  $bb = [System.Drawing.Bitmap]::FromFile($out); $w = $bb.Width; $hh = $bb.Height; $bb.Dispose()
  @{ Ok = $true; Method = 'gfxcapture'; Ms = $t1; Took = ($t1 - $t0); W = $w; H = $hh; Dark = $dk }
}

function PC-Montage([object[]]$items, [string]$out, [int]$cellW = 683) {
  $cells = @()
  foreach ($it in $items) {
    $src = [System.Drawing.Bitmap]::FromFile($it.Png)
    $sc = $cellW / $src.Width; $ch = [int]($src.Height * $sc)
    $cells += @{ It = $it; Bmp = $src; Sc = $sc; H = $ch }
  }
  $maxH = ($cells | ForEach-Object { $_.H } | Measure-Object -Maximum).Maximum
  $lab = 54
  $m = New-Object System.Drawing.Bitmap ($cellW * $cells.Count), ([int]$maxH + $lab)
  $g = [System.Drawing.Graphics]::FromImage($m)
  $g.Clear([System.Drawing.Color]::FromArgb(20, 20, 20))
  $g.InterpolationMode = 'HighQualityBicubic'
  $font = New-Object System.Drawing.Font 'Consolas', 9
  $pen = New-Object System.Drawing.Pen ([System.Drawing.Color]::Lime), 2
  $i = 0
  foreach ($c in $cells) {
    $x0 = $i * $cellW
    $g.DrawImage($c.Bmp, $x0, $lab, $cellW, $c.H)
    $sc = $c.It.Sidecar
    if ($sc -and $sc.box -and $sc.onscreen) {
      $vw = [double]$sc.viewport[0]; $kx = if ($vw -gt 0) { $cellW / $vw } else { $c.Sc }
      $bx = $sc.box
      $rx = [Math]::Max(0, [double]$bx[0] * $kx); $ry = [Math]::Max(0, [double]$bx[1] * $kx)
      $rw = [Math]::Min($cellW, [double]$bx[2] * $kx) - $rx; $rh = [Math]::Min($c.H, [double]$bx[3] * $kx) - $ry
      if ($rw -gt 0 -and $rh -gt 0) { $g.DrawRectangle($pen, [single]($x0 + $rx), [single]($lab + $ry), [single]$rw, [single]$rh) }
    }
    $t = "$($c.It.Label)`n" + $(if ($sc) { "key=$($sc.key) dist=$([Math]::Round([double]$sc.dist/100,1))m vis=$($sc.vis) rendered=$($sc.rendered)" } else { 'no census box' }) + "`n$($c.It.Extra)"
    $g.DrawString($t, $font, [System.Drawing.Brushes]::White, [single]($x0 + 4), [single]2)
    $c.Bmp.Dispose(); $i++
  }
  $g.Dispose()
  $m.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $m.Dispose()
  $out
}

function PC-Await($op, [Type]$t) {
  if (-not $script:PCAsTask) {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $script:PCAsTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
  }
  $task = $script:PCAsTask.MakeGenericMethod($t).Invoke($null, @($op))
  [void]$task.Wait(-1)
  $task.Result
}

function PC-Ocr([string]$png, [double]$scale = 2.0) {
  $null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
  $null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Graphics, ContentType = WindowsRuntime]
  $null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
  $src = [System.Drawing.Bitmap]::FromFile($png)
  $up = New-Object System.Drawing.Bitmap ([int]($src.Width * $scale)), ([int]($src.Height * $scale))
  $g = [System.Drawing.Graphics]::FromImage($up); $g.InterpolationMode = 'HighQualityBicubic'; $g.DrawImage($src, 0, 0, $up.Width, $up.Height); $g.Dispose(); $src.Dispose()
  $tmp = Join-Path $env:TEMP ("pcocr_" + [guid]::NewGuid().ToString('N') + '.png')
  $up.Save($tmp, [System.Drawing.Imaging.ImageFormat]::Png); $up.Dispose()
  try {
    $file = PC-Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($tmp)) ([Windows.Storage.StorageFile])
    $stream = PC-Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
    $dec = PC-Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
    $bmp = PC-Await ($dec.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
    $eng = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
    if (-not $eng) { return @() }
    $res = PC-Await ($eng.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])
    $stream.Dispose()
    @($res.Lines | ForEach-Object { $_.Text })
  } finally { Remove-Item -LiteralPath $tmp -Force -ErrorAction SilentlyContinue }
}

function PC-Norm([string]$s) { (($s.ToLowerInvariant() -replace '[^\p{L}\p{N} ]', ' ') -replace '\s+', ' ').Trim() }

function PC-Lev([string]$a, [string]$b) {
  $n = $a.Length; $m = $b.Length
  if ($n -eq 0) { return $m }; if ($m -eq 0) { return $n }
  $p = New-Object int[] ($m + 1); $c = New-Object int[] ($m + 1)
  for ($j = 0; $j -le $m; $j++) { $p[$j] = $j }
  for ($i = 1; $i -le $n; $i++) {
    $c[0] = $i
    for ($j = 1; $j -le $m; $j++) {
      $cost = if ($a[$i - 1] -eq $b[$j - 1]) { 0 } else { 1 }
      $c[$j] = [Math]::Min([Math]::Min($c[$j - 1] + 1, $p[$j] + 1), $p[$j - 1] + $cost)
    }
    $t = $p; $p = $c; $c = $t
  }
  $p[$m]
}

function PC-LoadTexts([string]$tsv) {
  $out = @()
  if (-not (Test-Path -LiteralPath $tsv)) { return $out }
  $lines = [IO.File]::ReadAllLines($tsv)
  for ($i = 1; $i -lt $lines.Count; $i++) {
    $f = $lines[$i] -split "`t"
    if ($f.Count -lt 2) { continue }
    for ($k = 1; $k -lt $f.Count; $k++) {
      $t = ($f[$k] -replace '\{[^}]*\}', ' ' -replace '%[sd]', ' ')
      $n = PC-Norm $t
      if ($n.Length -ge 6) { $out += [pscustomobject]@{ Id = $f[0]; Lang = @('', 'en', 'pt', 'es')[[Math]::Min($k, 3)]; Text = $f[$k]; Norm = $n } }
    }
  }
  $out
}

function PC-MatchTexts([string[]]$ocr, $texts, [double]$maxRatio = 0.25) {
  $hits = @()
  $lines = @($ocr | ForEach-Object { PC-Norm $_ } | Where-Object { $_.Length -ge 4 })
  $blob = ($lines -join ' ')
  foreach ($t in $texts) {
    $best = 999; $bestLine = ''
    if ($blob.Contains($t.Norm)) { $best = 0; $bestLine = ($lines | Where-Object { $_.Contains($t.Norm) } | Select-Object -First 1); if (-not $bestLine) { $bestLine = $t.Norm } }
    else {
      foreach ($l in $lines) {
        if ([Math]::Abs($l.Length - $t.Norm.Length) -gt [Math]::Max(3, $t.Norm.Length * $maxRatio) -and -not $l.Contains($t.Norm.Substring(0, [Math]::Min(6, $t.Norm.Length)))) { continue }
        $cand = if ($l.Length -gt $t.Norm.Length + 4) { $s = $l.IndexOf($t.Norm.Substring(0, [Math]::Min(4, $t.Norm.Length))); if ($s -ge 0) { $l.Substring($s, [Math]::Min($l.Length - $s, $t.Norm.Length)) } else { $l } } else { $l }
        $d = PC-Lev $cand $t.Norm
        if ($d -lt $best) { $best = $d; $bestLine = $l }
      }
    }
    if ($best -le [Math]::Max(1, [int]($t.Norm.Length * $maxRatio))) { $hits += [pscustomobject]@{ Id = $t.Id; Lang = $t.Lang; Text = $t.Text; Ocr = $bestLine; Dist = $best } }
  }
  $hits
}

function PC-SelfTest {
  $fail = 0; $ok = 0
  function T($c, $w) { if ($c) { $script:pcok++ } else { $script:pcfail++; Write-Host "FAIL $w" } }
  $script:pcok = 0; $script:pcfail = 0
  T ((PC-Lev 'kitten' 'sitting') -eq 3) 'levenshtein'
  T ((PC-Norm '  Host  CO-OP (ON)!') -eq 'host co op on') 'normalize'
  $tx = @([pscustomobject]@{ Id = 'menu_host'; Lang = 'en'; Text = 'Host co-op'; Norm = (PC-Norm 'Host co-op') }, [pscustomobject]@{ Id = 'x'; Lang = 'en'; Text = 'Revive your partner now'; Norm = (PC-Norm 'Revive your partner now') })
  $h = @(PC-MatchTexts @('MAIN MENU', 'Host co-op', 'Settings') $tx)
  T ($h.Count -eq 1 -and $h[0].Id -eq 'menu_host') 'exact line match'
  $h = @(PC-MatchTexts @('Revlve your partncr now') $tx)
  T ($h.Count -eq 1 -and $h[0].Id -eq 'x' -and $h[0].Dist -le 3) 'fuzzy match with OCR noise'
  $h = @(PC-MatchTexts @('nothing to see here at all') $tx)
  T ($h.Count -eq 0) 'no false match'
  $all = PC-LoadTexts $Texts
  T ($all.Count -gt 20) "nola_texts.tsv loads ($($all.Count) strings)"
  $d = Join-Path $env:TEMP ('pcself_' + [guid]::NewGuid().ToString('N')); New-Item -ItemType Directory -Force $d | Out-Null
  $b = New-Object System.Drawing.Bitmap 960, 540; $g = [System.Drawing.Graphics]::FromImage($b); $g.Clear([System.Drawing.Color]::FromArgb(60, 70, 80))
  $g.FillRectangle([System.Drawing.Brushes]::Orange, 400, 200, 80, 160)
  $g.DrawString('Host co-op', (New-Object System.Drawing.Font 'Arial', 28), [System.Drawing.Brushes]::White, 40, 40); $g.Dispose()
  $png = Join-Path $d 'A_test.png'; $b.Save($png, [System.Drawing.Imaging.ImageFormat]::Png); $b.Dispose()
  $tb = [System.Drawing.Bitmap]::FromFile($png); $dv = PC-Dark $tb; $tb.Dispose(); T ($dv -lt 0.1) 'dark check on a lit image'
  $sc = [pscustomobject]@{ key = 'u1'; dist = 600; vis = 1; rendered = 'true'; onscreen = $true; viewport = @(960, 540); box = @(400, 200, 480, 360) }
  $mo = PC-Montage @(@{ Png = $png; Sidecar = $sc; Label = 'C host'; Extra = 'skew 0 ms' }, @{ Png = $png; Sidecar = $null; Label = 'D client'; Extra = '' }) (Join-Path $d 'montage.png')
  $mb = [System.Drawing.Bitmap]::FromFile($mo)
  $k = 683 / 960
  $px = $mb.GetPixel([int](400 * $k), [int](54 + 280 * $k))
  T ($mb.Width -eq 1366 -and $px.G -gt 200 -and $px.R -lt 80) "montage size and green box edge (got $($mb.Width) px=$px)"
  $mb.Dispose()
  try { $o = PC-Ocr $png; T (($o -join ' ') -match 'Host') "windows ocr reads a rendered label (got: $($o -join ' | '))" } catch { T $false "windows ocr threw: $($_.Exception.Message)" }
  Remove-Item -LiteralPath $d -Recurse -Force
  Write-Host "proofcam selftest ok=$script:pcok fail=$script:pcfail"
  if ($script:pcfail) { exit 1 } else { exit 0 }
}

if ($MyInvocation.InvocationName -ne '.') {
  switch ($Mode) {
    'selftest' { PC-SelfTest }
    'ocr' { $o = PC-Ocr $A1; $o | ForEach-Object { "OCR $_" }; PC-MatchTexts $o (PC-LoadTexts $Texts) | ForEach-Object { "MATCH $($_.Id) [$($_.Lang)] d=$($_.Dist) '$($_.Text)' <- '$($_.Ocr)'" } }
    'grab' { $p = Get-Process -Id ([int]$A1); $r = PC-Grab $p.MainWindowHandle $A2; $r | ConvertTo-Json -Compress }
    '' { }
    default { Write-Host 'proofcam.ps1 selftest | ocr <png> | grab <pid> <out.png>'; exit 2 }
  }
}
