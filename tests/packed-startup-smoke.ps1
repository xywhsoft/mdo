[CmdletBinding()]
param(
    [string]$Xsw,
    [string]$App,
    [ValidateRange(5, 60)]
    [int]$ObserveSeconds = 20,
    [switch]$KeepArtifacts
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $Xsw) {
    $workspaceRoot = Split-Path -Parent $repoRoot
    $Xsw = Join-Path $workspaceRoot 'xserver-mdo-refactor\release\xsw.exe'
}
if (-not $App) {
    $App = Join-Path $PSScriptRoot 'fixtures\packed-startup-app'
}

$xswPath = (Resolve-Path -LiteralPath $Xsw).Path
$appPath = (Resolve-Path -LiteralPath $App).Path
$runRoot = Join-Path $env:TEMP ('mdo-packed-startup-' + [guid]::NewGuid().ToString('N'))
$packedPath = Join-Path $runRoot 'mdo-packed-startup.exe'

function Format-ProcessArgument([string]$Value) {
    return '"' + $Value.Replace('"', '\"') + '"'
}

function Remove-TestDirectory([string]$Path) {
    $resolvedTemp = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolvedPath = [IO.Path]::GetFullPath($Path)
    $leaf = Split-Path -Leaf $resolvedPath
    if (-not $resolvedPath.StartsWith($resolvedTemp, [StringComparison]::OrdinalIgnoreCase) -or
        -not $leaf.StartsWith('mdo-packed-startup-', [StringComparison]::Ordinal)) {
        throw "refusing to remove unexpected test directory: $resolvedPath"
    }
    Remove-Item -LiteralPath $resolvedPath -Recurse -Force
}

New-Item -ItemType Directory -Path $runRoot | Out-Null
$succeeded = $false
try {
    $packArgs = @(
        'pack',
        (Format-ProcessArgument $appPath),
        '-o',
        (Format-ProcessArgument $packedPath)
    )
    $pack = Start-Process -FilePath $xswPath -ArgumentList $packArgs `
        -WorkingDirectory $repoRoot -WindowStyle Hidden -PassThru
    if (-not $pack.WaitForExit(30000)) {
        Stop-Process -Id $pack.Id -Force
        throw 'packing did not finish within 30 seconds'
    }
    if ($pack.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $packedPath -PathType Leaf)) {
        throw "packing failed: exit=$($pack.ExitCode) output=$packedPath"
    }

    $run = Start-Process -FilePath $packedPath -WorkingDirectory $runRoot `
        -WindowStyle Hidden -PassThru
    if ($run.WaitForExit($ObserveSeconds * 1000)) {
        throw "packed process exited during the observation window: exit=$($run.ExitCode)"
    }

    Stop-Process -Id $run.Id -Force
    $run.WaitForExit()

    $crashes = @(Get-ChildItem -LiteralPath $runRoot -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -like 'crash_*.txt' -or $_.Extension -eq '.dmp' })
    if ($crashes.Count -ne 0) {
        throw "packed process produced crash artifacts: $($crashes.Name -join ', ')"
    }

    $markerPath = Join-Path $runRoot 'startup-ready.marker'
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf) -or
        (Get-Content -LiteralPath $markerPath -Raw) -ne "service-initialized`n") {
        throw 'packed service did not publish its startup marker'
    }
    if (Test-Path -LiteralPath (Join-Path $runRoot 'xsw.log')) {
        throw 'packed GUI process created the deprecated xsw.log side file'
    }

    Write-Host "PASS: packed service initialized and survived ${ObserveSeconds}s"
    Write-Host "host: $xswPath"
    if ($KeepArtifacts) {
        Write-Host "artifacts: $runRoot"
    } else {
        Write-Host 'artifacts: cleaned'
    }
    $succeeded = $true
}
finally {
    if ($succeeded -and -not $KeepArtifacts -and (Test-Path -LiteralPath $runRoot)) {
        Remove-TestDirectory $runRoot
    } elseif (-not $succeeded) {
        Write-Warning "failed-run artifacts preserved: $runRoot"
    }
}
