@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\fm mkdir out\fm
if not exist out\msvc mkdir out\msvc
rc /nologo /fo out\s2x_data.res ..\..\src\s2x_data.rc || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /Zi /MD /DNOMINMAX /DWIN32_LEAN_AND_MEAN /fsanitize=address /fsanitize=fuzzer /I..\..\src /Foout\fm\ /Feout\msvc\fuzz_net.exe fuzz_net.cpp ..\x0\stubs.cpp ..\..\src\s2x_flags.cpp ..\..\src\s2x_bridge.cpp ..\..\src\s2x_io.cpp ..\..\src\s2x_sched.cpp out\s2x_data.res ws2_32.lib user32.lib || exit /b 1
for /f "delims=" %%p in ('where clang_rt.asan_dynamic-x86_64.dll') do copy /y "%%p" out\msvc\ >nul
exit /b 0
