@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0..\.."
cl /nologo /utf-8 /std:c++20 /EHsc /W3 /Zs /I src src\s2m_lock.cpp
