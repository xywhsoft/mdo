[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$PackedPath,
    [string]$SeedHome,
    [ValidateRange(2, 20)]
    [int]$CaptureAfterSeconds = 5
)

# A bounded, manual visual check of the real Windows WebView2 window. The
# screenshot is evidence to inspect, not a pixel-based pass/fail oracle.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class MdoWindowSnapshot {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr window, IntPtr target, uint flags);
}
'@

$repoRoot = Split-Path -Parent $PSScriptRoot
$sourceExe = (Resolve-Path -LiteralPath $PackedPath).Path
$runRoot = Join-Path $repoRoot ('.build\native-webview-qa-' + [guid]::NewGuid().ToString('N'))
$targetExe = Join-Path $runRoot 'mdo-native-qa.exe'
$screenshot = Join-Path $runRoot 'window.png'
New-Item -ItemType Directory -Path $runRoot | Out-Null
Copy-Item -LiteralPath $sourceExe -Destination $targetExe
if ($SeedHome) {
    $sourceHome = (Resolve-Path -LiteralPath $SeedHome).Path
    if (-not (Test-Path -LiteralPath $sourceHome -PathType Container)) {
        throw "SeedHome is not a directory: $sourceHome"
    }
    Copy-Item -LiteralPath $sourceHome -Destination (Join-Path $runRoot 'mdo-home') -Recurse
}

$startup = [System.Diagnostics.ProcessStartInfo]::new()
$startup.FileName = $targetExe
$startup.WorkingDirectory = $runRoot
$startup.UseShellExecute = $false
$startup.EnvironmentVariables.Remove('MDO_HOME')
$startup.EnvironmentVariables['XS_APP_AUTOCLOSE_MS'] = [string](($CaptureAfterSeconds + 10) * 1000)
$process = [System.Diagnostics.Process]::Start($startup)
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $process.Refresh()
        if ($process.HasExited) {
            throw "packed app exited before opening its window: $($process.ExitCode)"
        }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 200
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($process.MainWindowHandle -eq [IntPtr]::Zero) {
        throw 'packed app did not open a native window within 10 seconds'
    }
    Start-Sleep -Seconds $CaptureAfterSeconds
    $process.Refresh()
    if ($process.HasExited) { throw 'packed app exited before the snapshot' }

    $rect = [MdoWindowSnapshot+Rect]::new()
    if (-not [MdoWindowSnapshot]::GetWindowRect($process.MainWindowHandle, [ref]$rect)) {
        throw 'cannot read native window bounds'
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 360 -or $height -lt 320) { throw "invalid native window bounds: ${width}x${height}" }

    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try {
            $hdc = $graphics.GetHdc()
            try { $printed = [MdoWindowSnapshot]::PrintWindow($process.MainWindowHandle, $hdc, 2) }
            finally { $graphics.ReleaseHdc($hdc) }
        } finally { $graphics.Dispose() }
        if (-not $printed) { throw 'PrintWindow could not capture WebView2' }
        $bitmap.Save($screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }

    Write-Host "SNAPSHOT: $screenshot"
    Write-Host "WINDOW: $($process.MainWindowTitle) ${width}x${height}"
    Write-Host "HOME: $(Join-Path $runRoot 'mdo-home')"
    Write-Host "PACKED_SHA256: $((Get-FileHash -LiteralPath $targetExe -Algorithm SHA256).Hash)"
} finally {
    if (-not $process.HasExited) {
        $null = $process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            $process.Kill()
            $null = $process.WaitForExit(5000)
        }
    }
    $process.Dispose()
}
