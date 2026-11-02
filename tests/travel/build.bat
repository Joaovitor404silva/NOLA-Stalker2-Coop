@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set OUT=%~dp0out
if not exist %OUT% mkdir %OUT%
cd /d %~dp0
cl /nologo /std:c++20 /EHsc /W4 /permissive- /I..\..\src /Fo%OUT%\ /Fe%OUT%\test_travel.exe test_travel.cpp ..\..\src\s2m_travel.cpp || exit /b 1
%OUT%\test_travel.exe
