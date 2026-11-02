@echo off
call %~dp0build_test.bat || exit /b 1
call %~dp0build_prod_check.bat || exit /b 1
uv run --no-project --with lupa python -I %~dp0run_parity.py
