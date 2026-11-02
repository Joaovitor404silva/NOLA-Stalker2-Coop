@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d %~dp0
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W3 /Fo:build\ /Fe:build\test_world.exe test_world.cpp ..\..\src\s2m_world.cpp || exit /b 1
