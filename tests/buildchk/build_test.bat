@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /I..\..\src /Foout\ /Feout\test_buildchk.exe test_buildchk.cpp ..\..\src\s2x_buildchk.cpp
exit /b %errorlevel%
