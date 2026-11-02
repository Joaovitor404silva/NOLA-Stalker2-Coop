@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /wd4100 /O2 /DS2M_SAVE_TEST /I..\..\src /Foout\ /Feout\test_save.exe test_save.cpp ..\..\src\s2m_save.cpp
exit /b %errorlevel%
