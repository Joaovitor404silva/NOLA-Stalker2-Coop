@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
rc /nologo /fo out\s2x_data.res ..\..\src\s2x_data.rc
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /wd4100 /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\..\src /Foout\ /Feout\test_x0.exe test_x0.cpp stubs.cpp ..\..\src\s2x_flags.cpp ..\..\src\s2x_bridge.cpp ..\..\src\s2x_io.cpp ..\..\src\s2x_sched.cpp out\s2x_data.res user32.lib
if errorlevel 1 exit /b 1
if not exist out\rel mkdir out\rel
cl /nologo /std:c++20 /EHsc /W4 /wd4100 /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /DS2X_RELEASE /I..\..\src /Foout\rel\ /Feout\test_rel.exe test_rel.cpp stubs.cpp ..\..\src\s2x_flags.cpp ..\..\src\s2x_bridge.cpp ..\..\src\s2x_io.cpp ..\..\src\s2x_sched.cpp out\s2x_data.res user32.lib
exit /b %errorlevel%
