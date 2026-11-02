@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /MD /I..\..\src /Foout\ /Feout\test_text.exe test_text.cpp
if errorlevel 1 exit /b 1
out\test_text.exe
exit /b %errorlevel%
