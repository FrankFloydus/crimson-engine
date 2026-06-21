param(
    [Parameter(Mandatory = $true)]
    [string]$Exe,

    [Parameter(Mandatory = $true)]
    [string]$ScreenshotPath,

    [Parameter(Mandatory = $true)]
    [string]$LogCopyPath,

    [int]$TimeoutSeconds = 30
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

$screenshotDir = Split-Path -Parent $ScreenshotPath
$logDir = Split-Path -Parent $LogCopyPath
New-Item -ItemType Directory -Force -Path $screenshotDir | Out-Null
New-Item -ItemType Directory -Force -Path $logDir | Out-Null

$exeFullPath = (Resolve-Path -LiteralPath $Exe).Path
$exeDir = Split-Path -Parent $exeFullPath
$process = Start-Process -FilePath $exeFullPath -ArgumentList "--borderless-window" -WorkingDirectory $exeDir -PassThru -WindowStyle Hidden

try {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $windowHandle = [IntPtr]::Zero

    while ((Get-Date) -lt $deadline) {
        if ($process.HasExited) {
            throw "Quader exited early with code $($process.ExitCode)."
        }

        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $windowHandle = $process.MainWindowHandle
            break
        }

        Start-Sleep -Milliseconds 250
    }

    if ($windowHandle -eq [IntPtr]::Zero) {
        throw "Timed out waiting for the Quader window."
    }

    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class NativeWindowCapture {
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
}
"@

    $rect = New-Object NativeWindowCapture+RECT
    if (-not [NativeWindowCapture]::GetWindowRect($windowHandle, [ref]$rect)) {
        throw "Could not read Quader window rectangle."
    }

    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $bitmap.Save($ScreenshotPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose()
    $bitmap.Dispose()

    $screenshot = Get-Item -LiteralPath $ScreenshotPath
    if ($screenshot.Length -le 1024) {
        throw "Screenshot is empty or too small: $ScreenshotPath"
    }

    $logCandidates = @(
        (Join-Path $exeDir "quader.log"),
        (Join-Path $env:TEMP "quader.log"),
        "C:\tmp\quader.log"
    )

    foreach ($candidate in $logCandidates) {
        if (Test-Path -LiteralPath $candidate) {
            Copy-Item -LiteralPath $candidate -Destination $LogCopyPath -Force
            break
        }
    }

    if (Test-Path -LiteralPath $LogCopyPath) {
        $logText = Get-Content -LiteralPath $LogCopyPath -Raw
        if ($logText -match "CRITICAL|Fatal error|fatal error") {
            throw "Critical or fatal log entry found in $LogCopyPath"
        }
    }
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        $process.WaitForExit()
    }
}
