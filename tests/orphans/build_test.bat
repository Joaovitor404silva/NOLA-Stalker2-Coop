@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /DS2M_CONSOLE_PURE /I..\..\src /I. /Fo:out\ /Fe:out\test_orphans.exe test_orphans.cpp fake_or.cpp ..\..\src\s2m_wshot.cpp ..\..\src\s2m_aiwatch.cpp ..\..\src\s2m_console.cpp
if errorlevel 1 exit /b 1
if not exist out\ply mkdir out\ply
cl /nologo /std:c++20 /EHsc /W4 /O2 /DS2M_CONSOLE_PURE /I..\..\src /I. /Fo:out\ply\ /Fe:out\test_ply.exe test_ply.cpp fake_or.cpp ..\..\src\s2m_wshot.cpp
if errorlevel 1 exit /b 1
out\test_ply.exe
if errorlevel 1 exit /b 1
