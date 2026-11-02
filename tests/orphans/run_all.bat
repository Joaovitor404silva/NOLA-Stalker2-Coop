@echo off
setlocal
cd /d %~dp0
set F=0
uv run --no-project --with lupa python -I gen_wshot_tsv.py --check || set F=1
call %~dp0build_test.bat || set F=1
cd /d %~dp0
uv run --no-project --with lupa python -I lua_ref.py || set F=1
if %F%==1 (echo ALL FAIL) else (echo ALL OK)
exit /b %F%
