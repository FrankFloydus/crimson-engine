param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [switch]$RunTests
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$BuildPreset = $Configuration.ToLowerInvariant()

Push-Location $ProjectRoot
try {
    cmake --preset vs2026-x64
    cmake --build --preset $BuildPreset

    if ($RunTests) {
        ctest --test-dir ".\out\build\vs2026-x64" -C $Configuration --output-on-failure
    }
} finally {
    Pop-Location
}
