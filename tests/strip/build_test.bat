@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /Foout\ /Feout\test_strip.exe test_strip.cpp ..\..\src\s2m_strip.cpp || exit /b 1
out\test_strip.exe D:\modding\offline\s1\port\oracle %*
exit /b %errorlevel%
