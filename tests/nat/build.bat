@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d %~dp0
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W3 /O1 /Fo:build\ /Fe:build\test_nat_mc.exe test_nat_mc.cpp || exit /b 1
build\test_nat_mc.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /O1 /Fo:build\ /Fe:build\test_nat_gait.exe test_nat_gait.cpp || exit /b 1
build\test_nat_gait.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /O1 /Fo:build\ /Fe:build\test_nat_mab.exe test_nat_mab.cpp || exit /b 1
build\test_nat_mab.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /O1 /Fo:build\ /Fe:build\test_nat_dsdrop.exe test_nat_dsdrop.cpp || exit /b 1
build\test_nat_dsdrop.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /O1 /Fo:build\ /Fe:build\test_nat_look.exe test_nat_look.cpp || exit /b 1
build\test_nat_look.exe || exit /b 1
cl /nologo /std:c++17 /EHsc /W3 /O1 /Fo:build\ /Fe:build\test_nat_qobj.exe test_nat_qobj.cpp || exit /b 1
build\test_nat_qobj.exe
