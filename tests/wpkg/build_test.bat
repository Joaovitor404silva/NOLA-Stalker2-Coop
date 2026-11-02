@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /wd4100 /O2 /I..\..\src /Foout\ /Feout\test_wpkg.exe test_wpkg.cpp ..\..\src\s2m_wpkg.cpp ..\..\src\s2m_savefile.cpp || exit /b 1
if "%~1"=="" (out\test_wpkg.exe "%~dp0fixtures\host_save.sav") else (out\test_wpkg.exe %1)
if errorlevel 1 exit /b 1
exit /b 0
