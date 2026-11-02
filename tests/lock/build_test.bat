@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /utf-8 /std:c++20 /EHsc /O1 /W3 /D S2M_LOCK_TEST /I..\..\src /Fo:out\ /Fe:out\test_lock.exe test_lock.cpp ..\..\src\s2m_lock.cpp ..\..\src\s2m_pop.cpp
