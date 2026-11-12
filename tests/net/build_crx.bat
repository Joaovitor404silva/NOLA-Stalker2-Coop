@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /Foout\ /Feout\test_crx.exe test_crx.cpp ws2_32.lib || exit /b 1
out\test_crx.exe
exit /b %errorlevel%
