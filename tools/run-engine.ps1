param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [int]$AutoCloseMs = 0
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$EngineExe = Join-Path $ProjectRoot "out\build\vs2026-x64\$Configuration\quader_native.exe"

if (-not (Test-Path $EngineExe)) {
    throw "Engine executable was not found at '$EngineExe'. Build it first with tools\build-engine.ps1."
}

Push-Location $ProjectRoot
try {
    if ($AutoCloseMs -gt 0) {
        $env:QUADER_AUTOCLOSE_MS = [string]$AutoCloseMs
    } else {
        Remove-Item Env:\QUADER_AUTOCLOSE_MS -ErrorAction SilentlyContinue
    }

    & $EngineExe
    exit $LASTEXITCODE
} finally {
    Remove-Item Env:\QUADER_AUTOCLOSE_MS -ErrorAction SilentlyContinue
    Pop-Location
}
