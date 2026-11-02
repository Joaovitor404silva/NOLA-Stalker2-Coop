@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /wd4100 /O2 /I..\..\src /Foout\ /Feout\test_pid.exe test_pid.cpp ..\..\src\s2m_pid.cpp || exit /b 1
out\test_pid.exe || exit /b 1
exit /b 0
