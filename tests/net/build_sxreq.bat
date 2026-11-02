@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /I..\..\src /Foout\ /Feout\test_sxreq.exe test_sxreq.cpp ..\..\src\s2r_net.cpp ws2_32.lib || exit /b 1
out\test_sxreq.exe
exit /b %errorlevel%
