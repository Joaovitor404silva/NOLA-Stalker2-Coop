@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /Foout\ /Feout\test_wsplice.exe test_wsplice.cpp ..\..\src\s2m_wsplice.cpp ..\..\src\s2m_strip.cpp ..\..\src\s2m_savefile.cpp || exit /b 1
out\test_wsplice.exe D:\modding\offline\s1\reA\dec D:\modding\offline\s1\s1c_oracle pairs.txt %*
exit /b %errorlevel%
