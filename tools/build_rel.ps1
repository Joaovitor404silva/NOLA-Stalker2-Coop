param([string]$Repo = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
$cb = Join-Path $Repo 'cppbuild'
$marker = Join-Path $cb 'Output\ue4ss_cache_key.txt'
if (-not (Test-Path $marker)) { throw "no UE4SS cache key in ${marker}, run wt.ps1 build first" }
$Cache = if ($env:WT_CACHE) { $env:WT_CACHE } else { 'D:\modding\cpp\ue4ss_cache' }
$kd = Join-Path $Cache ((Get-Content $marker -Raw).Trim())
if (-not (Test-Path "$kd\ok")) { throw "UE4SS cache $kd is not ready" }
$a = @()
Get-ChildItem "$kd\Output\_deps" -Directory -Filter '*-src' -ErrorAction SilentlyContinue | ForEach-Object {
    $n = $_.Name.Substring(0, $_.Name.Length - 4).ToUpper()
    $a += "-DFETCHCONTENT_SOURCE_DIR_$n=$($_.FullName.Replace([string][char]92, '/'))"
}
$env:NOLA_CMAKE_ARGS = ($a -join ' ')
$log = Join-Path $cb 'build_rel.log'
$bat = Join-Path $env:TEMP "nola_build_rel_$PID.bat"
@"
@echo off
call "$PSScriptRoot\ue4ss_env.bat" || exit /b 1
cd /d "$cb" || exit /b 1
cmake -S . -B OutputRel -G "Visual Studio 17 2022" -A x64 %RUSTC_ARG% %NOLA_CMAKE_ARGS% -DS2X_RELEASE=ON || exit /b 1
if not exist "OutputRel\Game__Shipping__Win64\lib\UE4SS.lib" robocopy "$kd\Output\Game__Shipping__Win64" "OutputRel\Game__Shipping__Win64" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
cmake --build OutputRel --config Game__Shipping__Win64 --target NolaNative -- /m /p:BuildProjectReferences=false || exit /b 1
"@ | Set-Content -Encoding ascii $bat
$env:MSBUILDDISABLENODEREUSE = '1'
& cmd.exe /c "`"$bat`" > `"$log`" 2>&1"
$rc = $LASTEXITCODE
Remove-Item $bat -ErrorAction SilentlyContinue
if ($rc -ne 0) {
    Get-Content $log | Select-String -Pattern ' error |error [A-Z]+[0-9]+|fatal' | Select-Object -First 30 | ForEach-Object { $_.Line }
    throw "release build failed ($rc), log $log"
}
$dll = Join-Path $cb 'OutputRel\NolaNative\Game__Shipping__Win64\NolaNative.dll'
Write-Output "dll $dll"
Write-Output "md5 $((Get-FileHash $dll -Algorithm MD5).Hash.ToLower())"
