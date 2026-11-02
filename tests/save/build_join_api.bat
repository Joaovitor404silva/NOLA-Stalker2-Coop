@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out\japi mkdir out\japi
cl /nologo /std:c++20 /EHsc /W3 /DS2N_JOIN_TEST /DS2N_SAVE_TEST /I..\..\src /Foout\japi\ /Feout\test_join_api.exe test_join_api.cpp ..\..\src\s2n_join.cpp ..\..\src\s2n_save.cpp ..\..\src\s2m_wpkg.cpp ..\..\src\s2m_savefile.cpp
exit /b %errorlevel%
