@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\rp mkdir out\rp
if not exist out\sg mkdir out\sg
rc /nologo /fo out\s2x_data.res ..\..\src\s2x_data.rc || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DFUZZ_REPLAY /I..\..\src /Foout\rp\ /Feout\fuzz_replay.exe fuzz_net.cpp ..\x0\stubs.cpp ..\..\src\s2x_flags.cpp ..\..\src\s2x_bridge.cpp ..\..\src\s2x_io.cpp ..\..\src\s2x_sched.cpp out\s2x_data.res ws2_32.lib user32.lib || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /I..\..\src /Foout\sg\ /Feout\seedgen.exe seedgen.cpp ws2_32.lib || exit /b 1
out\seedgen.exe ..\net\fixtures out\seeds || exit /b 1
out\fuzz_replay.exe --selftest || exit /b 1
out\fuzz_replay.exe out\seeds seeds regress || exit /b 1
exit /b 0
