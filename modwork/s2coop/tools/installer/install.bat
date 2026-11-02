@echo off
rem NOLA - No One Left Alone: install.
rem This only starts nola_install.ps1 (next to this file) with PowerShell.
rem Open nola_install.ps1 in Notepad to read everything it does.
rem It installs the files from the "files" folder next to it and keeps a backup
rem of everything it replaces, so uninstall.bat can put it all back.
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0nola_install.ps1" -Action install %*
echo.
pause
