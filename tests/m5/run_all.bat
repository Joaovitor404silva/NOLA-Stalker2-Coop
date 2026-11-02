@echo off
call %~dp0build.bat || exit /b 1
call %~dp0build_adapter.bat || exit /b 1
cd /d %~dp0
uv run --no-project --with lupa python -I compare.py || exit /b 1
uv run --no-project --with lupa python -I tables_check.py || exit /b 1
uv run --no-project python -I flags_check.py || exit /b 1
