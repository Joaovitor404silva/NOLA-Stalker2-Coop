@echo off
call "%~dp0ue4ss_env.bat" || exit /b 1
cd /d "%~1" || exit /b 1
cmake -S src -B Output -G "Visual Studio 17 2022" -A x64 %RUSTC_ARG% || exit /b 1
cmake --build Output --config Game__Shipping__Win64 --target UE4SS -- /m || exit /b 1
