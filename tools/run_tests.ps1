param([Parameter(ValueFromRemainingArguments = $true)]$Rest)
$ErrorActionPreference = 'Stop'
$script = Join-Path (Split-Path -Parent $PSScriptRoot) 'tests\run_tests.py'
& uv run --no-project --with lupa python -I $script @Rest
exit $LASTEXITCODE
