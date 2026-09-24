################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
# Read-only; validate Release by default. Debug/Profile is explicitly selected when requested.
[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [ValidateSet('Debug', 'Release')]
    [string[]]$Configuration = @('Release'),

    [ValidateNotNullOrEmpty()]
    [ValidateSet('Full', 'Public')]
    [string[]]$Profile = @('Full')
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native_build_contract.ps1')
$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildDirectory = Join-Path $repositoryRoot 'build/vs2026'
$cachePath = Resolve-RequiredFile (Join-Path $buildDirectory 'CMakeCache.txt') 'Canonical CMake cache'
Assert-TracyBuild -CachePath $cachePath
$cache = Get-Content -LiteralPath $cachePath -Raw
foreach ($required in @('QUADER_BUILD_NATIVE_APP:BOOL=ON', 'QUADER_BUILD_TESTS:BOOL=ON',
        'CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026', 'VCPKG_APPLOCAL_DEPS:BOOL=OFF')) {
    if (-not $cache.Contains($required)) { throw "Canonical native cache is missing $required" }
}
Assert-ProfilingBuildProject -ProjectPath (Join-Path $buildDirectory 'quader_modeling.vcxproj')
Assert-FatalDiagnosticsBuildProject -ProjectPath (Join-Path $buildDirectory 'quader_fatal_diagnostics.vcxproj')
foreach ($projectName in @('quader_app', 'quader_platform', 'quader_modeling', 'quader_render',
        'quader_fatal_diagnostics')) {
    $projectPath = Join-Path $buildDirectory "$projectName.vcxproj"
    Assert-ReleaseBuildProject -ProjectPath $projectPath
}
foreach ($profileName in ($Profile | Select-Object -Unique)) {
    $targetName = if ($profileName -eq 'Public') { 'quader_public_app' } else { 'quader' }
    $executableName = if ($profileName -eq 'Public') { 'quader-0.1.exe' } else { 'quader.exe' }
    $projectPath = Join-Path $buildDirectory "$targetName.vcxproj"
    Assert-NativeExecutableLinkContract -ProjectPath $projectPath
    Assert-ReleaseBuildProject -ProjectPath $projectPath
    foreach ($configurationName in $Configuration) {
        $executable = Resolve-RequiredFile (Join-Path $buildDirectory "$configurationName/$executableName") "$profileName $configurationName executable"
        Assert-NativeRuntimeDeployment -ExecutablePath $executable
        Assert-RuntimeDependencyClosure -ExecutablePath $executable
        Assert-ExecutableDebugDirectory -ExecutablePath $executable -ExpectSymbols ($configurationName -eq 'Debug')
        $fatalDiagnostics = Resolve-NativeFatalDiagnosticsExecutable -ExecutablePath $executable
        Assert-ExecutableDebugDirectory -ExecutablePath $fatalDiagnostics -ExpectSymbols ($configurationName -eq 'Debug')
        $hash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Host "Verified $profileName $configurationName`: $executable (SHA-256 $hash)"
        $companionHash = (Get-FileHash -LiteralPath $fatalDiagnostics -Algorithm SHA256).Hash.ToLowerInvariant()
        Write-Host "Verified $configurationName fatal diagnostics: $fatalDiagnostics (SHA-256 $companionHash)"
    }
}
