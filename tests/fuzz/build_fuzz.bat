@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>nul
if "%NOLA_LLVM%"=="" set NOLA_LLVM=D:\modding\tools\llvm\clang+llvm-23.1.3-x86_64-pc-windows-msvc
if not exist "%NOLA_LLVM%\bin\clang-cl.exe" (echo SKIP no clang-cl at %NOLA_LLVM% & exit /b 1)
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\fz mkdir out\fz
rc /nologo /fo out\s2x_data.res ..\..\src\s2x_data.rc || exit /b 1
"%NOLA_LLVM%\bin\clang-cl.exe" /nologo /std:c++20 /EHsc /Od /Zi /MT /D_DISABLE_STRING_ANNOTATION /D_DISABLE_VECTOR_ANNOTATION /DNOMINMAX /DWIN32_LEAN_AND_MEAN -fsanitize=fuzzer,address -Wno-everything /I..\..\src /Foout\fz\ /Feout\fuzz_net.exe fuzz_net.cpp ..\x0\stubs.cpp ..\..\src\s2x_flags.cpp ..\..\src\s2x_bridge.cpp ..\..\src\s2x_io.cpp ..\..\src\s2x_sched.cpp out\s2x_data.res ws2_32.lib user32.lib || exit /b 1
copy /y "%NOLA_LLVM%\lib\clang\23\lib\windows\clang_rt.asan_dynamic-x86_64.dll" out\ >nul 2>nul
exit /b 0
