@echo off
call "%~dp0ue4ss_env.bat" || exit /b 1
cd /d "%~1" || exit /b 1
cmake -S . -B Output -G "Visual Studio 17 2022" -A x64 %RUSTC_ARG% %NOLA_CMAKE_ARGS% || exit /b 1
if not exist "Output\Game__Shipping__Win64\lib\UE4SS.lib" robocopy "%~2\Output\Game__Shipping__Win64" "Output\Game__Shipping__Win64" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 exit /b 1
cmake --build Output --config Game__Shipping__Win64 --target NolaNative -- /m /p:BuildProjectReferences=false || exit /b 1
