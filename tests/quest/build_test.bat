@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /DS2M_QUEST_STANDALONE /I..\..\src /Foout\ /Feout\test_quest.exe test_quest.cpp ..\..\src\s2m_quest.cpp
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /I..\..\src /c ..\..\src\s2m_quest.cpp /Foout\s2m_quest_x0.obj
exit /b %errorlevel%
