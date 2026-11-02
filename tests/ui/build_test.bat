@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /permissive- /DNOMINMAX /I..\..\src /Foout\ /Feout\test_uiq.exe test_uiq.cpp ..\..\src\s2x_uiq.cpp || exit /b 1
out\test_uiq.exe || exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /permissive- /DNOMINMAX /I..\..\src /Foout\ /Feout\test_tutl.exe test_tutl.cpp ..\..\src\s2x_uiq.cpp || exit /b 1
out\test_tutl.exe
