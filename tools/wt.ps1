param(
    [Parameter(Position = 0, Mandatory = $true)][ValidateSet('new', 'build', 'diff', 'merge', 'rm', 'list')][string]$Cmd,
    [Parameter(Position = 1)][string]$Worker,
    [switch]$Release,
    [switch]$Fast,
    [string]$NoRegression = ''
)

$ErrorActionPreference = 'Stop'
$Main = if ($env:WT_MAIN) { $env:WT_MAIN } else { 'D:\modding\cpp\S2CoopPort' }
$Root = if ($env:WT_ROOT) { $env:WT_ROOT } else { 'D:\modding\cpp\wt' }

function MGit { & git.exe -C $Main @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed ($LASTEXITCODE)" } }

if ($Cmd -ne 'list') {
    if (-not $Worker -or $Worker -notmatch '^[A-Za-z0-9_-]+$') { throw 'worker name required: letters, digits, _ or -' }
}
$UE4SS = if ($env:WT_UE4SS) { $env:WT_UE4SS } else { 'D:\modding\cpp\RE-UE4SS' }
$Cache = if ($env:WT_CACHE) { $env:WT_CACHE } else { 'D:\modding\cpp\ue4ss_cache' }
$VsRoot = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$Rustc = "$env:USERPROFILE\.rustup\toolchains\stable-x86_64-pc-windows-msvc\bin\rustc.exe"
$ToolsDir = $PSScriptRoot
$Sccache = if ($env:WT_SCCACHE) { $env:WT_SCCACHE } else { 'D:\modding\tools\sccache\sccache.exe' }
$SccacheDir = if ($env:WT_SCCACHE_DIR) { $env:WT_SCCACHE_DIR } else { 'D:\modding\cpp\sccache' }
$FastLinker = if ($env:WT_FAST_LINKER) { $env:WT_FAST_LINKER } else { 'D:\modding\tools\llvm\clang+llvm-23.1.3-x86_64-pc-windows-msvc\bin\lld-link.exe' }
$FastLink = if ($env:WT_FAST_LINK) { $env:WT_FAST_LINK } else { '/DEBUG:GHASH;/OPT:NOREF;/OPT:NOICF;/INCREMENTAL:NO' }

function Get-Sha([string]$text) {
    $sha = [Security.Cryptography.SHA256]::Create()
    ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text))) -replace '-', '').ToLower()
}

function Get-TopLines([string]$cppbuild) {
    (Get-Content "$cppbuild\CMakeLists.txt") | Where-Object { $_ -notmatch 'S2_SRC' }
}

function Get-CacheInfo([string]$cppbuild) {
    try {
        $ugit = @('-c', 'safe.directory=*', '-C', $UE4SS)
        $head = (& git.exe @ugit rev-parse HEAD).Trim()
        $subs = (& git.exe @ugit submodule status --recursive) -join "`n"
        $dirty = (& git.exe @ugit status --porcelain --ignore-submodules=none) -join "`n"
        $diff = (& git.exe @ugit diff HEAD) -join "`n"
        $cl = (Get-Content "$VsRoot\VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt" -Raw).Trim()
        $sdk = (Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\Include' -Directory | Sort-Object Name | Select-Object -Last 1).Name
        $cm = (& "$VsRoot\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --version | Select-Object -First 1)
        $rc = (& $Rustc --version)
        $top = (Get-TopLines $cppbuild) -join "`n"
        $gen = 'gen=Visual Studio 17 2022;arch=x64;config=Game__Shipping__Win64;target=UE4SS;script=1'
        $lines = @("ue4ss=$head", "submodules=$(Get-Sha $subs)", "dirty=$(Get-Sha ($dirty + $diff))", "msvc=$cl", "sdk=$sdk", "cmake=$cm", "rustc=$rc", "toplevel=$(Get-Sha $top)", $gen)
        @{ Key = (Get-Sha ($lines -join "`n")).Substring(0, 16); Lines = $lines; Top = $top }
    } catch { $null }
}

function Ensure-Cache($info) {
    $kd = Join-Path $Cache $info.Key
    if (Test-Path "$kd\ok") { return $true }
    $lock = "$kd.lock"
    New-Item -ItemType Directory -Force $Cache | Out-Null
    $deadline = (Get-Date).AddMinutes(45)
    while ($true) {
        try { New-Item -ItemType Directory $lock -ErrorAction Stop | Out-Null; break } catch { }
        if (Test-Path "$kd\ok") { return $true }
        $holder = Get-Content "$lock\pid" -ErrorAction SilentlyContinue
        if ($holder -and -not (Get-Process -Id ([int]$holder) -ErrorAction SilentlyContinue)) { Remove-Item -Recurse -Force $lock -ErrorAction SilentlyContinue; continue }
        if ((Get-Date) -gt $deadline) { return $false }
        Start-Sleep -Seconds 5
    }
    try {
        Set-Content "$lock\pid" $PID
        if (Test-Path "$kd\ok") { return $true }
        New-Item -ItemType Directory -Force "$kd\src" | Out-Null
        Set-Content "$kd\src\CMakeLists.txt" $info.Top
        Add-Content "$kd\src\CMakeLists.txt" ''
        Set-Content "$kd\inputs.txt" $info.Lines
        $env:MSBUILDDISABLENODEREUSE = '1'
        for ($try = 1; $try -le 2; $try++) {
            & cmd.exe /c "`"$ToolsDir\ue4ss_cache_build.bat`" `"$kd`" > `"$kd\cache_build_$try.log`" 2>&1"
            if ($LASTEXITCODE -eq 0) { break }
        }
        if ($LASTEXITCODE -ne 0) { return $false }
        if (-not (Test-Path "$kd\Output\Game__Shipping__Win64\lib\UE4SS.lib")) { return $false }
        Set-Content "$kd\ok" (Get-Date).ToString('s')
        return $true
    } finally { Remove-Item -Recurse -Force $lock -ErrorAction SilentlyContinue }
}

function Get-CachedArgs([string]$kd) {
    $a = @()
    Get-ChildItem "$kd\Output\_deps" -Directory -Filter '*-src' -ErrorAction SilentlyContinue | ForEach-Object {
        $n = $_.Name.Substring(0, $_.Name.Length - 4).ToUpper()
        $a += "-DFETCHCONTENT_SOURCE_DIR_$n=$($_.FullName.Replace([string][char]92, '/'))"
    }
    $a -join ' '
}
$Branch = "w/$Worker"
$Dir = Join-Path $Root $Worker

switch ($Cmd) {
    'list' {
        MGit worktree list
    }
    'new' {
        if (Test-Path $Dir) { throw "$Dir already exists" }
        New-Item -ItemType Directory -Force $Root | Out-Null
        $exists = & git.exe -C $Main branch --list $Branch
        if ($exists) { MGit worktree add $Dir $Branch } else { MGit worktree add -b $Branch $Dir main }
        Write-Output "branch $Branch"
        Write-Output "worktree $Dir"
        Write-Output "base $(& git.exe -C $Dir rev-parse --short HEAD)"
    }
    'build' {
        if (-not (Test-Path "$Dir\cppbuild\build.bat")) { throw "no worktree at $Dir" }
        $marks = & git -C $Dir grep -n -E '^(<<<<<<< |>>>>>>> )' -- src data tools tests
        if ($marks) { throw "merge conflict markers left in the tree:`n$($marks -join "`n")" }
        $log = "$Dir\cppbuild\build_wt.log"
        $env:MSBUILDDISABLENODEREUSE = '1'
        $sw = [Diagnostics.Stopwatch]::StartNew()
        if ($Fast) {
            if ($Release) { throw '-Fast is a dev-only build; build the release with -Release (or tools\build_rel.ps1)' }
            $log = "$Dir\cppbuild\build_fast.log"
            $info = Get-CacheInfo "$Dir\cppbuild"
            if (-not $info -or -not (Ensure-Cache $info)) { throw 'UE4SS cache not available; -Fast links against the cached UE4SS libs' }
            $kd = Join-Path $Cache $info.Key
            $env:NOLA_CMAKE_ARGS = Get-CachedArgs $kd
            $env:NOLA_SCCACHE = $Sccache.Replace([string][char]92, '/')
            $env:NOLA_SCCACHE_DIR = $SccacheDir
            $env:NOLA_LINKER = $FastLinker.Replace([string][char]92, '/')
            $env:NOLA_FAST_LINK = $FastLink
            $bases = @($Main) + @(Get-ChildItem $Root -Directory -ErrorAction SilentlyContinue | ForEach-Object { $_.FullName })
            $env:SCCACHE_BASEDIRS = ($bases | Sort-Object -Unique) -join ';'
            New-Item -ItemType Directory -Force $SccacheDir | Out-Null
            $bdFile = "$SccacheDir\basedirs.txt"
            $bdOld = if (Test-Path $bdFile) { (Get-Content $bdFile -Raw).Trim() } else { '' }
            if ($bdOld -ne $env:SCCACHE_BASEDIRS) {
                & cmd.exe /c "`"$Sccache`" --stop-server >nul 2>&1"
                Set-Content $bdFile $env:SCCACHE_BASEDIRS
            }
            & cmd.exe /c "`"$ToolsDir\nola_build_fast.bat`" `"$Dir\cppbuild`" `"$($kd.Replace([string][char]92, '/'))`" > `"$log`" 2>&1"
            $rc = $LASTEXITCODE
            $sw.Stop()
            if ($rc -ne 0) {
                Get-Content $log | Select-String -Pattern ' error |error [A-Z]+[0-9]+|fatal|FAILED' | Select-Object -First 30 | ForEach-Object { $_.Line }
                throw "fast build failed ($rc), log $log"
            }
            $dll = "$Dir\cppbuild\OutFast\NolaNative\NolaNative.dll"
            $f = Get-Item $dll
            Write-Output "branch $Branch $(& git.exe -C $Dir rev-parse --short HEAD)"
            Write-Output "dll $dll"
            Write-Output "md5 $((Get-FileHash $dll -Algorithm MD5).Hash.ToLower())"
            Write-Output "size $($f.Length) built $($f.LastWriteTime.ToString('s')) in $([int]$sw.Elapsed.TotalSeconds)s"
            Write-Output "log $log"
            Write-Output "fast dev build (Ninja, /Z7, sccache, $([IO.Path]::GetFileName($FastLinker)) $FastLink), cache $($info.Key)"
            break
        }
        $cacheNote = 'cache off'
        $useCache = $false
        $marker = "$Dir\cppbuild\Output\ue4ss_cache_key.txt"
        $libs = "$Dir\cppbuild\Output\Game__Shipping__Win64\lib\UE4SS.lib"
        if (-not $env:WT_NOCACHE) {
            $info = Get-CacheInfo "$Dir\cppbuild"
            if ($info) {
                $have = if (Test-Path $marker) { (Get-Content $marker -Raw).Trim() } else { '' }
                if ($have -eq $info.Key) { $useCache = $true }
                elseif (-not $have -and -not (Test-Path $libs)) { $useCache = Ensure-Cache $info }
                if ($useCache) {
                    $kd = Join-Path $Cache $info.Key
                    $env:NOLA_CMAKE_ARGS = Get-CachedArgs $kd
                    $cacheNote = "cache hit $($info.Key)"
                } else { $cacheNote = "cache skipped $($info.Key)" }
            }
        }
        $relArg = if ($Release) { '-DS2X_RELEASE=ON' } else { '-DS2X_RELEASE=OFF' }
        $env:NOLA_CMAKE_ARGS = ("$env:NOLA_CMAKE_ARGS $relArg").Trim()
        if ($useCache) {
            New-Item -ItemType Directory -Force "$Dir\cppbuild\Output" | Out-Null
            Set-Content $marker $info.Key
            & cmd.exe /c "`"$ToolsDir\nola_build_cached.bat`" `"$Dir\cppbuild`" `"$kd`" > $log 2>&1"
        } else {
            & cmd.exe /c "$Dir\cppbuild\build.bat > $log 2>&1"
        }
        $rc = $LASTEXITCODE
        $sw.Stop()
        if ($rc -ne 0) {
            Get-Content $log | Select-String -Pattern ' error |error [A-Z]+[0-9]+|fatal' | Select-Object -First 30 | ForEach-Object { $_.Line }
            throw "build failed ($rc), log $log"
        }
        $dll = "$Dir\cppbuild\Output\NolaNative\Game__Shipping__Win64\NolaNative.dll"
        if ($Release) {
            New-Item -ItemType Directory -Force "$Dir\cppbuild\Release" | Out-Null
            Copy-Item $dll "$Dir\cppbuild\Release\NolaNative.dll" -Force
            Copy-Item ([IO.Path]::ChangeExtension($dll, '.pdb')) "$Dir\cppbuild\Release\NolaNative.pdb" -Force -ErrorAction SilentlyContinue
            $dll = "$Dir\cppbuild\Release\NolaNative.dll"
            Write-Output 'release build (S2X_RELEASE=ON); cppbuild\Output now holds the release DLL until the next build without -Release'
        }
        $f = Get-Item $dll
        Write-Output "branch $Branch $(& git.exe -C $Dir rev-parse --short HEAD)"
        Write-Output "dll $dll"
        Write-Output "md5 $((Get-FileHash $dll -Algorithm MD5).Hash.ToLower())"
        Write-Output "size $($f.Length) built $($f.LastWriteTime.ToString('s')) in $([int]$sw.Elapsed.TotalSeconds)s"
        Write-Output "log $log"
        Write-Output $cacheNote
    }
    'diff' {
        if (Test-Path $Dir) {
            $base = & git.exe -C $Dir merge-base main HEAD
            & git.exe -C $Dir diff $base
            $st = & git.exe -C $Dir status --short
            if ($st) { Write-Output '--- worktree status:'; $st }
        } else {
            MGit diff "main...$Branch"
        }
    }
    'merge' {
        $cur = & git.exe -C $Main rev-parse --abbrev-ref HEAD
        if ($cur -ne 'main') { throw "main tree is on $cur, not main" }
        if (Test-Path $Dir) {
            $st = & git.exe -C $Dir status --short
            if ($st) { Write-Output $st; throw "worktree $Dir has uncommitted changes; commit them on $Branch first" }
        }
        if ($NoRegression) { Write-Output "REGRESSION GATE SKIPPED: $NoRegression" }
        elseif (Test-Path "$Dir\tools\run_tests.ps1") {
            $rlog = "$Dir\regression_gate.log"
            & powershell -NoProfile -ExecutionPolicy Bypass -File "$Dir\tools\run_tests.ps1" --changed main *> $rlog
            $rc = $LASTEXITCODE
            Get-Content $rlog | Select-String 'new failures|^NEW|FAIL ' | Select-Object -Last 12 | ForEach-Object { Write-Output $_.Line }
            & git.exe -C $Dir checkout -- tests 2>$null
            if ($rc -eq 1) { throw "REGRESSION GATE: new test failures vs tests\baseline.tsv (log $rlog). Fix them, or merge with -NoRegression '<why>'" }
            if ($rc -ne 0) { throw "REGRESSION GATE: test runner exit $rc (log $rlog)" }
            Remove-Item $rlog -ErrorAction SilentlyContinue
        } else { throw "REGRESSION GATE: no tools\run_tests.ps1 in $Dir" }
        & git.exe -C $Main merge --no-ff --no-edit $Branch
        if ($LASTEXITCODE -ne 0) {
            $u = & git.exe -C $Main diff --name-only --diff-filter=U
            Write-Output 'MERGE CONFLICT, merge left in progress in the main tree. Conflicted files:'
            $u | ForEach-Object { Write-Output "  $_" }
            Write-Output "resolve and commit, or: git -C $Main merge --abort"
            exit 1
        }
        Write-Output "merged $Branch into main: $(& git.exe -C $Main rev-parse --short HEAD)"
    }
    'rm' {
        if (-not (Test-Path $Dir)) { throw "no worktree at $Dir" }
        $st = & git.exe -C $Dir status --short
        if ($st) { Write-Output $st; throw "worktree $Dir has uncommitted changes" }
        & git.exe -C $Main worktree remove --force $Dir
        for ($i = 0; (Test-Path $Dir) -and $i -lt 10; $i++) {
            Start-Sleep -Seconds 3
            try { Remove-Item -Recurse -Force $Dir -Confirm:$false -ErrorAction Stop } catch { }
        }
        if (Test-Path $Dir) { throw "$Dir still locked; close the process holding it and run rm again" }
        MGit worktree prune
        Write-Output "removed $Dir (branch $Branch kept)"
    }
}
