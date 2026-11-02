@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d D:\modding\cpp\S2CoopPort\tests\save\out
cl /nologo /c /std:c++20 /EHsc /W4 /wd4100 /I..\..\..\src ..\..\..\src\s2m_save_x0.cpp /Fo:s2m_save_x0.obj
