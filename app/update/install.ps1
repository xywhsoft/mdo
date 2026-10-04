param([Parameter(Mandatory=$true)][string]$Parameters)
$ErrorActionPreference = 'Stop'
$folder = Split-Path -LiteralPath $Parameters
$resultPath = Join-Path $folder 'install-result.json'
$utf8 = New-Object System.Text.UTF8Encoding($false)
$targetPath = $null
$originalSaved = $false
$parentExited = $false
function Write-Result([string]$state, [string]$message) {
    $text = @{status=$state; message=$message} | ConvertTo-Json -Compress
    [IO.File]::WriteAllText($resultPath + '.tmp', $text, $utf8)
    Move-Item -LiteralPath ($resultPath + '.tmp') -Destination $resultPath -Force
}
function Quote-Argument([string]$argument) {
    # Windows CRT argument rules, including quotes and trailing backslashes.
    if ($argument.Length -gt 0 -and $argument -notmatch '[\s"]') { return $argument }
    $value = [regex]::Replace($argument, '(\\*)"', '$1$1\"')
    $value = [regex]::Replace($value, '(\\+)$', '$1$1')
    return '"' + $value + '"'
}
function Restart-App {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = $targetPath
    $start.WorkingDirectory = [string]$parametersData.work_dir
    $start.UseShellExecute = $false
    $start.Arguments = (($parametersData.args | ForEach-Object { Quote-Argument ([string]$_) }) -join ' ')
    $process = [Diagnostics.Process]::Start($start)
    if ($null -eq $process) { throw 'Unable to restart mdo' }
    $process.Dispose()
}
function File-Hash([string]$path) {
    $stream = [IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}
try {
    $parametersData = Get-Content -LiteralPath $Parameters -Raw -Encoding UTF8 | ConvertFrom-Json
    $targetPath = [IO.Path]::GetFullPath([string]$parametersData.target)
    $newPath = Join-Path $folder 'new.exe'
    $oldPath = Join-Path $folder 'old.exe'
    $expected = [string]$parametersData.sha256
    if ($expected -cnotmatch '^[0-9a-f]{64}$' -or -not (Test-Path -LiteralPath $targetPath -PathType Leaf)) {
        throw 'Invalid install parameters or missing running executable'
    }
    if ((File-Hash $newPath) -cne $expected) {
        throw 'Downloaded package hash changed before installation'
    }
    $parent = [Diagnostics.Process]::GetProcessById([int]$parametersData.pid)
    if ([IO.Path]::GetFullPath($parent.MainModule.FileName) -ine $targetPath) { throw 'Original executable path mismatch' }
    if ($parent.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -cne [string]$parametersData.identity) {
        throw 'The original process identity no longer matches'
    }
    [IO.File]::WriteAllText((Join-Path $folder 'install.ready'),$expected,$utf8)
    $goPath = Join-Path $folder 'install.go'
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    while (-not (Test-Path -LiteralPath $goPath -PathType Leaf)) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'Application did not authorize installation' }
        Start-Sleep -Milliseconds 100
    }
    if ((Get-Content -LiteralPath $goPath -Raw -Encoding UTF8) -cne $expected) { throw 'Install authorization mismatch' }
    if (-not $parent.WaitForExit(120000)) { throw 'mdo is still running; no files were replaced' }
    $parentExited = $true
    $parent.Dispose()
    # Recheck after waiting. Keep the original until the replacement is ready.
    if ((File-Hash $newPath) -cne $expected) {
        throw 'Downloaded package changed while waiting for shutdown'
    }
    Copy-Item -LiteralPath $targetPath -Destination $oldPath -Force
    $originalSaved = $true
    # Same-directory atomic replacement prevents a truncated root executable.
    $swapPath = $targetPath + '.mdo-update'
    Copy-Item -LiteralPath $newPath -Destination $swapPath -Force
    if ((File-Hash $swapPath) -cne $expected) {
        throw 'Replacement copy checksum failed'
    }
    [IO.File]::Replace($swapPath,$targetPath,[NullString]::Value)
    Write-Result 'success' ''
    Restart-App
    foreach ($name in @('new.exe','install.ready','install.go','install.json')) {
        Remove-Item -LiteralPath (Join-Path $folder $name) -Force -ErrorAction SilentlyContinue
    }
} catch {
    $failure = $_.Exception.Message
    if ($swapPath) { Remove-Item -LiteralPath $swapPath -Force -ErrorAction SilentlyContinue }
    if ($originalSaved -and $parentExited) {
        try { Copy-Item -LiteralPath $oldPath -Destination $targetPath -Force } catch {}
    }
    try { Write-Result 'failed' $failure } catch {}
    if ($parentExited -and $targetPath) { try { Restart-App } catch {} }
    exit 1
}
