@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d %~dp0..\..\src
set PHR=
if exist ..\cppbuild\Output\_deps\polyhook2-src set PHR=..\cppbuild\Output\_deps
if not defined PHR for /d %%k in (D:\modding\cpp\ue4ss_cache\*) do if not defined PHR if exist "%%k\Output\_deps\polyhook2-src" set PHR=%%k\Output\_deps
if not defined PHR (echo SKIP: polyhook sources not found & exit /b 77)
set PH=/I"%PHR%\polyhook2-src" /I"%PHR%\polyhook2-src\asmjit\src" /I"%PHR%\polyhook2-src\asmtk\src" /I"%PHR%\zydis-src\include" /I"%PHR%\zydis-src\dependencies\zycore\include" /DZYDIS_STATIC_BUILD /DZYCORE_STATIC_BUILD /DASMJIT_STATIC
cl /nologo /std:c++20 /EHsc /W4 /c /I. %PH% /Fo%TEMP%\ s2m_bodies.cpp
