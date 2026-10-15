param(
    [Parameter(Mandatory = $true)][string]$Program,
    [Parameter(Mandatory = $true)][string]$Script,
    [string[]]$ScriptArgs = @(),
    [string]$ProjectDir = 'D:\ghidra-projects',
    [string]$Project = 'GTASA',
    [string]$GhidraDir = 'D:\ghidra_12.1.2_PUBLIC'
)
# Runs a GhidraScript read-only against an already analyzed program and prints only script output.
# analyzeHeadless takes its JDK (21 for Ghidra 12) from JAVA_HOME.
if (-not $env:JAVA_HOME -or -not (Test-Path (Join-Path $env:JAVA_HOME 'bin\java.exe'))) {
    Write-Error 'JAVA_HOME is not set or has no bin\java.exe: set it to a JDK 21 folder before running this script.'
    exit 1
}
$scriptPath = Split-Path -Parent $MyInvocation.MyCommand.Path
$output = & (Join-Path $GhidraDir 'support\analyzeHeadless.bat') $ProjectDir $Project -process $Program -noanalysis -readOnly `
    -scriptPath $scriptPath -postScript $Script @ScriptArgs 2>&1
# Script println output starts with "INFO  <Script>.java> "; multi-line messages continue on unprefixed lines.
$inScript = $false
foreach ($line in ($output | ForEach-Object { "$_" })) {
    if ($line -match '^(INFO|WARN|ERROR|DEBUG) ') {
        $inScript = $line -match '^INFO  \S+\.java> '
        if ($line -match '^ERROR') { $line; continue }
        if (-not $inScript) { continue }
        $line = $line -replace '^INFO  \S+\.java> ', ''
    }
    if ($inScript) { $line -replace ' \(GhidraScript\)\s*$', '' }
}
