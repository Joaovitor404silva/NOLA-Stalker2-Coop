@echo off
rem NOLA - No One Left Alone: uninstall.
rem This only starts nola_install.ps1 (next to this file) with PowerShell.
rem It puts back every file the install replaced, from the backup it kept,
rem and removes the NOLA files. Your saves are not touched.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0nola_install.ps1" -Action uninstall %*
echo.
pause
