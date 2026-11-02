@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W3 /O2 /I..\..\src /Fo:out\ /Fe:out\dr_sim.exe dr_sim.cpp
