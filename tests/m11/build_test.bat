@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d %~dp0
cl /nologo /std:c++20 /EHsc /W3 /Fe:test_session.exe test_session.cpp ..\..\src\s2m_session.cpp || exit /b 1
"%CD%\test_session.exe" || exit /b 1
cl /nologo /c /std:c++20 /EHsc /W3 /Fo:x0_check.obj ..\..\src\s2m_session_x0.cpp || exit /b 1
del *.obj test_session.exe
