@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /utf-8 /std:c++20 /EHsc /O1 /W4 /I..\..\src /Fo:out\ /Fe:out\test_damage.exe test_damage.cpp ..\..\src\s2m_damage.cpp
