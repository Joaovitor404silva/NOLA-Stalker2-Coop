@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
cd /d "%~dp0"
set O=%TEMP%\nbot_play_test
if not exist %O% mkdir %O%
cl /nologo /std:c++17 /EHsc /W4 /O2 /Fo%O%\ /Fe%O%\test_nbot_play.exe test_nbot_play.cpp
if errorlevel 1 exit /b 1
%O%\test_nbot_play.exe quest_goals.tsv
if errorlevel 1 exit /b 1
cl /nologo /O2 /MT /EHsc /std:c++17 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /IC:\modding\s2dev\plugins /IC:\modding\s2dev\plugins\nbot /LD nbot.cpp /Fo%O%\ /Fe%O%\nbot_check.dll
exit /b %errorlevel%
