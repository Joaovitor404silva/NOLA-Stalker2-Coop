# Idempotent: (re)apply static names + C:\modding\s2names\names.tsv (if present) to the WRITABLE named project C:\modding\ghidra_26123434.
# Fails if the named project is busy (decompile run holds it).
$env:JAVA_HOME = 'C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot'
$env:GHIDRA_HEADLESS_MAXMEM = '24G'
$log = 'C:\modding\s2decomp\apply_names.log'
& 'C:\modding\tools\ghidra\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat' 'C:\modding\ghidra_26123434' s2 `
    -process s2.exe -noanalysis -scriptPath 'C:\modding\ghidra_scripts' -postScript ApplyNames.java `
    'C:\modding\s2decomp\apply_names_summary.txt' 'C:\modding\s2decomp\static_names.tsv' 'C:\modding\s2names\names.tsv' *> $log
Select-String -Path $log -Pattern 'ApplyNames.java>|ERROR|Exception' | ForEach-Object { $_.Line }
