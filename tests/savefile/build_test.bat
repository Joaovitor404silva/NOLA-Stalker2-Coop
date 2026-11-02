@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /Foout\ /Feout\test_savefile.exe test_savefile.cpp ..\..\src\s2m_savefile.cpp || exit /b 1
set PY=C:\Users\USER\AppData\Roaming\uv\python\cpython-3.14.8-windows-x86_64-none\python.exe
set SPEC=D:\modding\offline\s1\reA
set ORC=D:\modding\offline\s1\s1c_oracle
"%PY%" -I oracle.py %SPEC% %SPEC%\dec %ORC% pairs.txt || exit /b 1
out\test_savefile.exe %SPEC%\dec %ORC% pairs.txt %*
exit /b %errorlevel%
