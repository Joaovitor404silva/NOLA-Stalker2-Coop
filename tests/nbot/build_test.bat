@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /W4 /O2 /I..\..\src /I..\..\tools\bench\nbot /Foout\ /Feout\test_nbot_scene.exe test_nbot_scene.cpp
if errorlevel 1 exit /b 1
out\test_nbot_scene.exe ..\..\tools\bench\scenes
exit /b %errorlevel%
