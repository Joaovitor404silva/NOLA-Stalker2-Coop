@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0..\..\src
cl /nologo /std:c++20 /EHsc /W4 /c /I. /Fo%TEMP%\ s2m_pop_x0.cpp s2m_pop.cpp
