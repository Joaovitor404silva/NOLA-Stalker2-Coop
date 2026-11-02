@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /Foout\ /Feout\net_tool.exe net_tool.cpp ..\..\src\s2r_net.cpp ws2_32.lib || exit /b 1
set PH=/I..\..\cppbuild\Output\_deps\polyhook2-src /I..\..\cppbuild\Output\_deps\polyhook2-src\asmjit\src /I..\..\cppbuild\Output\_deps\polyhook2-src\asmtk\src /I..\..\cppbuild\Output\_deps\zydis-src\include /I..\..\cppbuild\Output\_deps\zydis-src\dependencies\zycore\include /DZYDIS_STATIC_BUILD /DZYCORE_STATIC_BUILD /DASMJIT_STATIC
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /I..\..\src %PH% /Foout\ /Feout\glue_test.exe glue_test.cpp ..\..\src\s2r_x0.cpp ..\..\src\s2r_net.cpp ws2_32.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\PolyHook_2.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\asmtk.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\asmjit.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\Zydis.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\Zycore.lib || exit /b 1
