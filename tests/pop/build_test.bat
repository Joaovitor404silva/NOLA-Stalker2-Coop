@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0
if not exist out mkdir out
cl /nologo /std:c++17 /EHsc /W4 /Fo:out\ /Fe:out\pop_test.exe test_pop.cpp ..\..\src\s2m_pop.cpp || exit /b 1
out\pop_test.exe
