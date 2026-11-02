@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cd /d "%~dp0"
if not exist out mkdir out
uv run --no-project python -I mkrsp.py || exit /b 1
cl /c @out\cl.rsp /Foout\ test_natlayer.cpp || exit /b 1
link /nologo /OUT:out\test_natlayer.exe out\test_natlayer.obj @out\link.rsp /FORCE:UNRESOLVED /IGNORE:4088 /MAP:out\test_natlayer.map || exit /b 1
dumpbin /imports out\test_natlayer.exe > out\imports.txt
uv run --no-project python -I mkstub.py || exit /b 1
cl /nologo /LD /Foout\ui_stub.obj out\ue4ss_stub.c /Fe:out\UE4SS.dll /link /DEF:out\ue4ss_stub.def /NOENTRY /IGNORE:4070 || exit /b 1
out\test_natlayer.exe
exit /b %errorlevel%
