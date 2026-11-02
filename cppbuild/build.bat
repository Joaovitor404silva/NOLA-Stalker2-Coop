@echo off
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%USERPROFILE%\.cargo\bin;%PATH%
set GIT_CONFIG_COUNT=1
set GIT_CONFIG_KEY_0=url.https://github.com/.insteadOf
set GIT_CONFIG_VALUE_0=git@github.com:
cd /d "%~dp0"
cmake -S . -B Output -G "Visual Studio 17 2022" -A x64 -DRust_COMPILER=%USERPROFILE%/.rustup/toolchains/stable-x86_64-pc-windows-msvc/bin/rustc.exe %NOLA_CMAKE_ARGS% || exit /b 1
cmake --build Output --config Game__Shipping__Win64 --target NolaNative -- /m || exit /b 1
