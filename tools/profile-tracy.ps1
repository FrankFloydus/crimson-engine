param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [string]$TracyProfilerExe = "",

    [int]$AutoCloseMs = 0
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPreset = $Configuration.ToLowerInvariant()
$EngineExe = Join-Path $ProjectRoot "out\build\vs2026-x64\$Configuration\quader_native.exe"

function Find-TracyProfiler {
    param([string]$ExplicitPath)

    if ($ExplicitPath -and (Test-Path $ExplicitPath)) {
        return (Resolve-Path $ExplicitPath).Path
    }

    $OnPath = Get-Command "tracy-profiler.exe" -ErrorAction SilentlyContinue
    if ($OnPath) {
        return $OnPath.Source
    }

    $Candidates = @(
        (Join-Path $ProjectRoot "tools\tracy-profiler.exe"),
        (Join-Path $ProjectRoot "third-party\tracy\tracy-profiler.exe"),
        "C:\Tools\tracy\tracy-profiler.exe"
    )

    foreach ($Candidate in $Candidates) {
        if (Test-Path $Candidate) {
            return (Resolve-Path $Candidate).Path
        }
    }

    return ""
}

Push-Location $ProjectRoot
try {
    cmake --preset vs2026-x64 -DQUADER_ENABLE_TRACY=ON
    cmake --build --preset $BuildPreset

    $ProfilerExe = Find-TracyProfiler -ExplicitPath $TracyProfilerExe
    if ($ProfilerExe) {
        Start-Process -FilePath $ProfilerExe
        Start-Sleep -Seconds 1
    } else {
        Write-Warning "Could not find tracy-profiler.exe. The engine will launch with Tracy instrumentation, but the profiler GUI will not be opened."
        Write-Warning "Pass -TracyProfilerExe 'C:\Path\to\tracy-profiler.exe' or put tracy-profiler.exe on PATH."
    }

    if ($AutoCloseMs -gt 0) {
        $env:QUADER_AUTOCLOSE_MS = [string]$AutoCloseMs
    } else {
        Remove-Item Env:\QUADER_AUTOCLOSE_MS -ErrorAction SilentlyContinue
    }

    Start-Process -FilePath $EngineExe -WorkingDirectory $ProjectRoot
} finally {
    Remove-Item Env:\QUADER_AUTOCLOSE_MS -ErrorAction SilentlyContinue
    Pop-Location
}
