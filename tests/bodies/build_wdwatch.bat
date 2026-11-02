@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /Fo:out\ /Fe:out\test_wdwatch.exe test_wdwatch.cpp
out\test_wdwatch.exe
