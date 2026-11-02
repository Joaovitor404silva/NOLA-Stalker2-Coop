@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out_be mkdir out_be
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /Foout_be\ /Feout_be\test_strip_be.exe test_strip_be.cpp ..\..\src\s2m_strip.cpp ..\..\src\s2m_savefile.cpp ..\..\src\s2m_wsplice.cpp || exit /b 1
out_be\test_strip_be.exe D:\modding\tools\repak\oo2core_9_win64.dll D:\modding\offline\testsave\be_26123434 D:\modding\offline\testsave\be_sav_26123434
exit /b %errorlevel%
