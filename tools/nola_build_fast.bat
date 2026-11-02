@echo off
call "%~dp0ue4ss_env.bat" || exit /b 1
cd /d "%~1" || exit /b 1
set SCCACHE_IGNORE_SERVER_IO_ERROR=1
set SCCACHE_DIR=%NOLA_SCCACHE_DIR%
set NOLA_CXXF=/DWIN32 /D_WINDOWS /EHsc
set NOLA_CFGF=/W3 /wd4005 /wd4251 /wd4068 /Zc:inline /Zc:strictStrings /Gy /D_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR /Zc:preprocessor /O2 /Ob2 /DNDEBUG
if exist OutFast\build.ninja goto build
cmake -S fast -B OutFast -G Ninja -DCMAKE_BUILD_TYPE=Game__Shipping__Win64 -DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded "-DCMAKE_CXX_COMPILER_LAUNCHER=%NOLA_SCCACHE%" "-DCMAKE_LINKER=%NOLA_LINKER%" "-DNOLA_UE4SS_LIBDIR=%~2/Output/Game__Shipping__Win64/lib" "-DCMAKE_CXX_FLAGS=%NOLA_CXXF%" "-DCMAKE_CXX_FLAGS_GAME__SHIPPING__WIN64=%NOLA_CFGF%" "-DCMAKE_C_FLAGS_GAME__SHIPPING__WIN64=%NOLA_CFGF%" "-DCMAKE_SHARED_LINKER_FLAGS=/machine:x64" "-DCMAKE_EXE_LINKER_FLAGS=/machine:x64" "-DNOLA_FAST_LINK=%NOLA_FAST_LINK%" %RUSTC_ARG% %NOLA_CMAKE_ARGS% || exit /b 1
:build
"%NOLA_SCCACHE%" --start-server >nul 2>nul
cmake --build OutFast --target NolaNative || exit /b 1
