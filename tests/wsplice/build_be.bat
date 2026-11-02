@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out_be mkdir out_be
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /Foout_be\ /Feout_be\test_wsplice_be.exe test_wsplice_be.cpp ..\..\src\s2m_wsplice.cpp ..\..\src\s2m_strip.cpp ..\..\src\s2m_savefile.cpp || exit /b 1
out_be\test_wsplice_be.exe D:\modding\tools\repak\oo2core_9_win64.dll D:\modding\offline\testsave\be_sav_26123434 c_33E3AF57.sav h_323723C1.sav h_BD2F5F99.sav
exit /b %errorlevel%
