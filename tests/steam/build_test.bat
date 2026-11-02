@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
if not exist out\glue mkdir out\glue
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /LD /Foout\ /Feout\steam_api64.dll fake_steam_api.cpp || exit /b 1
for %%n in (b c d e) do copy /y out\steam_api64.dll out\fake_%%n.dll >nul || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /I..\..\src /Foout\ /Feout\steam_test.exe steam_test.cpp ..\..\src\s2r_steam.cpp ..\..\src\s2r_net.cpp /link out\steam_api64.lib delayimp.lib /DELAYLOAD:steam_api64.dll ws2_32.lib || exit /b 1
set PH=/I..\..\cppbuild\Output\_deps\polyhook2-src /I..\..\cppbuild\Output\_deps\polyhook2-src\asmjit\src /I..\..\cppbuild\Output\_deps\polyhook2-src\asmtk\src /I..\..\cppbuild\Output\_deps\zydis-src\include /I..\..\cppbuild\Output\_deps\zydis-src\dependencies\zycore\include /DZYDIS_STATIC_BUILD /DZYCORE_STATIC_BUILD /DASMJIT_STATIC
set PHL=..\..\cppbuild\Output\Game__Shipping__Win64\lib\PolyHook_2.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\asmtk.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\asmjit.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\Zydis.lib ..\..\cppbuild\Output\Game__Shipping__Win64\lib\Zycore.lib
cl /nologo /std:c++20 /EHsc /O2 /W3 /MD /I..\..\src %PH% /Foout\glue\ /Feout\glue_steam_test.exe glue_steam_test.cpp ..\..\src\s2r_x0.cpp ..\..\src\s2r_steam_x0.cpp ..\..\src\s2r_steam.cpp ..\..\src\s2r_net.cpp /link out\steam_api64.lib delayimp.lib /DELAYLOAD:steam_api64.dll ws2_32.lib %PHL% || exit /b 1
