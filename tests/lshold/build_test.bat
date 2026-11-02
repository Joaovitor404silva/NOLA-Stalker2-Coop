@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /wd4100 /O2 /I..\..\src /Foout\ /Feout\test_lshold.exe test_lshold.cpp ..\..\src\s2m_wpkg.cpp ..\..\src\s2m_savefile.cpp
if errorlevel 1 exit /b 1
out\test_lshold.exe
exit /b %errorlevel%
