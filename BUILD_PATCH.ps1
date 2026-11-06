param([Parameter(Mandatory=$true)][string]$UE4SSRoot)
$ErrorActionPreference = 'Stop'
$repo = $PSScriptRoot
$UE4SSRoot = (Resolve-Path $UE4SSRoot).Path
if (!(Test-Path "$UE4SSRoot/CMakeLists.txt")) { throw 'Invalid RE-UE4SS checkout' }
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio 2022 C++ build tools first' }
$vcvars = Join-Path $vs 'VC\Auxiliary\Build\vcvars64.bat'
$out = Join-Path $repo 'build-patch'
New-Item -ItemType Directory -Force $out | Out-Null
$bat = Join-Path $out 'compile-tests.bat'
@"
@echo off
call "$vcvars" || exit /b 1
cd /d "$repo"
cl /nologo /utf-8 /std:c++20 /EHsc /O2 /Isrc /Fo"$out\\" /Fe"$out\test_net.exe" tests\hardening\test_net.cpp src\s2r_net.cpp ws2_32.lib || exit /b 1
"$out\test_net.exe" || exit /b 1
cl /nologo /utf-8 /std:c++20 /EHsc /O1 /Isrc /Fo"$out\\" /Fe"$out\test_damage.exe" tests\damage\test_damage.cpp src\s2m_damage.cpp || exit /b 1
for %%S in (tests\hardening\*.txt) do (
  "$out\test_damage.exe" data "%%S" > "$out\%%~nS.log" || exit /b 1
)
"@ | Set-Content $bat -Encoding ascii
& cmd.exe /d /c $bat
if ($LASTEXITCODE) { throw 'Regression test compilation/execution failed' }
& python "$repo/tests/hardening/check_damage.py" "$out"
if ($LASTEXITCODE) { throw 'Downed-state assertions failed' }
& cmd.exe /d /c cmake -S "$repo/cppbuild" -B "$out/native" -G 'Visual Studio 17 2022' -A x64 "-DUE4SS_ROOT=$UE4SSRoot" -DS2X_RELEASE=ON '2>&1'
if ($LASTEXITCODE) { throw 'CMake configuration failed' }
& cmd.exe /d /c cmake --build "$out/native" --config Game__Shipping__Win64 --target NolaNative -- /m '2>&1'
if ($LASTEXITCODE) { throw 'DLL build failed' }
Get-ChildItem "$out/native" -Recurse -Filter '*.dll' | Select-Object FullName
Write-Host 'Build finished. Preserve your installed DLL before replacing it. Use the same patched build on all players.'
