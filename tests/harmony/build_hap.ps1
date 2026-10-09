param(
    [string]$DevEcoDir = 'D:\runtimes\harmony\DevEcoStudio',
    [string]$CacheDir = 'D:\runtimes\harmony\cache',
    [string]$TempDir = 'D:\runtimes\harmony\temp'
)
$ErrorActionPreference = 'Stop'
$node = Join-Path $DevEcoDir 'tools\node\node.exe'
$javaDir = Join-Path $DevEcoDir 'jbr'
$ohpm = Join-Path $DevEcoDir 'tools\ohpm\bin\pm-cli.js'
$hvigor = Join-Path $DevEcoDir 'tools\hvigor\bin\hvigorw.js'
foreach ($file in @($node, $ohpm, $hvigor, (Join-Path $javaDir 'bin\java.exe'))) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Missing official tool: $file" }
}
$settings = @{
    DEVECO_SDK_HOME = Join-Path $DevEcoDir 'sdk'
    NODE_HOME = Join-Path $DevEcoDir 'tools\node'
    NODE_PATH = Join-Path $DevEcoDir 'tools\hvigor'
    JAVA_HOME = $javaDir
    HVIGOR_USER_HOME = Join-Path $CacheDir 'hvigor'
    npm_config_cache = Join-Path $CacheDir 'npm'
    TEMP = $TempDir
    TMP = $TempDir
    PATH = "$(Join-Path $javaDir 'bin');$env:PATH"
}
$saved = @{}
try {
    foreach ($name in $settings.Keys) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
        [Environment]::SetEnvironmentVariable($name, $settings[$name], 'Process')
    }
    New-Item -ItemType Directory -Path $CacheDir, $TempDir -Force | Out-Null
    Push-Location (Join-Path $PSScriptRoot 'hap')
    try {
        & $node $ohpm install --all --cache (Join-Path $CacheDir 'ohpm')
        if ($LASTEXITCODE -ne 0) { throw "ohpm failed: $LASTEXITCODE" }
        & $node $hvigor --mode module -p product=default -p module=entry@default assembleHap --no-daemon --no-parallel
        if ($LASTEXITCODE -ne 0) { throw "HAP build failed: $LASTEXITCODE" }
    } finally { Pop-Location }
} finally {
    foreach ($name in $saved.Keys) {
        [Environment]::SetEnvironmentVariable($name, $saved[$name], 'Process')
    }
}
