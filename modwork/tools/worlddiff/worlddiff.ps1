$ErrorActionPreference = 'Stop'
& uv run --no-project python -I (Join-Path $PSScriptRoot 'worlddiff.py') @args
exit $LASTEXITCODE
