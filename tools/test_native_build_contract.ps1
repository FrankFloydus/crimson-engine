################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
# Synthetic fixtures exercise rejection paths without changing configured build artifacts.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native_build_contract.ps1')

function Assert-Rejected {
    param([scriptblock]$Action, [string]$Expected)
    $failure = $null
    try { & $Action } catch { $failure = $_.Exception.Message }
    if ($null -eq $failure -or -not $failure.Contains($Expected)) {
        throw "Expected rejection containing '$Expected', got: $failure"
    }
}

$projectPath = [IO.Path]::GetTempFileName()
$executablePath = [IO.Path]::GetTempFileName()
$runtimeFixtureDirectory = Join-Path ([IO.Path]::GetTempPath()) `
    ('quader-native-contract-' + [Guid]::NewGuid().ToString('N'))
$runtimeExecutable = Join-Path $runtimeFixtureDirectory 'quader.exe'
$runtimeCompanion = Join-Path $runtimeFixtureDirectory 'quader_fatal_diagnostics.exe'
try {
    [void][IO.Directory]::CreateDirectory($runtimeFixtureDirectory)
    $project = @'
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <PropertyGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <WholeProgramOptimization>true</WholeProgramOptimization>
  </PropertyGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile>
      <Optimization>MaxSpeed</Optimization>
      <RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>
      <PreprocessorDefinitions>NDEBUG</PreprocessorDefinitions>
      <DebugInformationFormat></DebugInformationFormat>
    </ClCompile>
    <Link><GenerateDebugInformation>false</GenerateDebugInformation></Link>
  </ItemDefinitionGroup>
</Project>
'@
    [IO.File]::WriteAllText($projectPath, $project)
    Assert-ReleaseBuildProject -ProjectPath $projectPath
    foreach ($token in @('TRACY_ENABLE=1', 'TRACY_ON_DEMAND=1', 'QUADER_ENABLE_SENTRY=1')) {
        [IO.File]::WriteAllText($projectPath, $project.Replace('NDEBUG', "NDEBUG;$token"))
        Assert-Rejected { Assert-ReleaseBuildProject -ProjectPath $projectPath } 'forbidden instrumentation/debug token'
    }
    [IO.File]::WriteAllText($projectPath, $project.Replace('MultiThreadedDLL', 'MultiThreadedDebugDLL'))
    Assert-Rejected { Assert-ReleaseBuildProject -ProjectPath $projectPath } 'forbidden instrumentation/debug token'
    [IO.File]::WriteAllText($projectPath, $project.Replace('<WholeProgramOptimization>true', '<WholeProgramOptimization>false'))
    Assert-Rejected { Assert-ReleaseBuildProject -ProjectPath $projectPath } 'whole-program optimization is missing'
    [IO.File]::WriteAllText($projectPath, $project.Replace('<GenerateDebugInformation>false', '<GenerateDebugInformation>true'))
    Assert-Rejected { Assert-ReleaseBuildProject -ProjectPath $projectPath } 'forbidden instrumentation/debug token'

    $helperProject = @'
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ClCompile><Optimization>MaxSpeed</Optimization><RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary><PreprocessorDefinitions>NDEBUG</PreprocessorDefinitions></ClCompile>
    <Link><AdditionalDependencies>Dbghelp.lib;kernel32.lib</AdditionalDependencies></Link>
  </ItemDefinitionGroup>
  <ItemDefinitionGroup Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ClCompile><Optimization>MaxSpeed</Optimization><RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary><PreprocessorDefinitions>NDEBUG</PreprocessorDefinitions></ClCompile>
    <Link><AdditionalDependencies>Dbghelp.lib;kernel32.lib</AdditionalDependencies></Link>
  </ItemDefinitionGroup>
</Project>
'@
    [IO.File]::WriteAllText($projectPath, $helperProject)
    Assert-FatalDiagnosticsBuildProject -ProjectPath $projectPath
    foreach ($token in @('TRACY_ENABLE=1', 'QUADER_ENABLE_SENTRY=1')) {
        [IO.File]::WriteAllText($projectPath, $helperProject.Replace('NDEBUG', "NDEBUG;$token"))
        Assert-Rejected { Assert-FatalDiagnosticsBuildProject -ProjectPath $projectPath } 'forbidden dependency or instrumentation'
    }
    foreach ($dependency in @('TracyClient.lib', 'sentry.lib', 'Qt6Core.lib', 'quader_platform.lib')) {
        [IO.File]::WriteAllText($projectPath, $helperProject.Replace('Dbghelp.lib;', "Dbghelp.lib;$dependency;"))
        Assert-Rejected { Assert-FatalDiagnosticsBuildProject -ProjectPath $projectPath } 'forbidden dependency or instrumentation'
    }
    [IO.File]::WriteAllText($projectPath, $helperProject.Replace('Dbghelp.lib;', ''))
    Assert-Rejected { Assert-FatalDiagnosticsBuildProject -ProjectPath $projectPath } 'must link Dbghelp'

    # A valid app deployment is incomplete without its separately launched helper.
    [IO.File]::WriteAllBytes($runtimeExecutable, [byte[]]::new(1))
    Assert-Rejected { Assert-NativeRuntimeDeployment -ExecutablePath $runtimeExecutable } 'Standalone fatal diagnostics companion not found'
    [IO.File]::WriteAllBytes($runtimeCompanion, [byte[]]::new(1))
    if ((Resolve-NativeFatalDiagnosticsExecutable -ExecutablePath $runtimeExecutable) -ne $runtimeCompanion) {
        throw 'Fatal diagnostics companion must resolve beside the app executable.'
    }

    # Stub only PE import enumeration; exercise the production dependency walker.
    function Resolve-DumpbinPath { return 'Invoke-FixtureDumpbin' }
    function Invoke-FixtureDumpbin {
        param([string]$NoLogo, [string]$Dependents, [string]$BinaryPath)
        $global:LASTEXITCODE = 0
        Write-Output '    api-ms-win-core-file-l1-1-0.dll'
        if ((Split-Path -Leaf $BinaryPath) -eq 'quader_fatal_diagnostics.exe' -and
            -not [string]::IsNullOrEmpty($script:fixtureHelperImport)) {
            Write-Output "    $script:fixtureHelperImport"
        }
    }
    $script:fixtureHelperImport = ''
    Assert-RuntimeDependencyClosure -ExecutablePath $runtimeExecutable
    $script:fixtureHelperImport = 'quader_fixture_missing_dependency_96179d5f.dll'
    Assert-Rejected { Assert-RuntimeDependencyClosure -ExecutablePath $runtimeExecutable } `
        'quader_fatal_diagnostics.exe -> quader_fixture_missing_dependency_96179d5f.dll'
    foreach ($dependency in @('TracyClient.dll', 'sentry.dll', 'Qt6Core.dll')) {
        $script:fixtureHelperImport = $dependency
        Assert-Rejected { Assert-RuntimeDependencyClosure -ExecutablePath $runtimeExecutable } 'imports forbidden runtime'
    }

    # One x64 PE section containing one debug-directory record.
    $bytes = [byte[]]::new(1024)
    function Set-UInt16([int]$Offset, [uint16]$Value) { [BitConverter]::GetBytes($Value).CopyTo($bytes, $Offset) }
    function Set-UInt32([int]$Offset, [uint32]$Value) { [BitConverter]::GetBytes($Value).CopyTo($bytes, $Offset) }
    Set-UInt16 0 0x5a4d
    Set-UInt32 0x3c 0x80
    Set-UInt32 0x80 0x4550
    Set-UInt16 0x86 1
    Set-UInt16 0x94 0xf0
    Set-UInt16 0x98 0x20b
    Set-UInt32 (0x98 + 108) 16
    Set-UInt32 (0x98 + 112 + 6 * 8) 0x1000
    Set-UInt32 (0x98 + 112 + 6 * 8 + 4) 28
    Set-UInt32 (0x98 + 0xf0 + 8) 0x100
    Set-UInt32 (0x98 + 0xf0 + 12) 0x1000
    Set-UInt32 (0x98 + 0xf0 + 16) 0x100
    Set-UInt32 (0x98 + 0xf0 + 20) 0x200
    Set-UInt32 (0x200 + 12) 13 # LTCG COFFGRP metadata is allowed in Release.
    [IO.File]::WriteAllBytes($executablePath, $bytes)
    Assert-ExecutableDebugDirectory -ExecutablePath $executablePath -ExpectSymbols $false
    Assert-Rejected { Assert-ExecutableDebugDirectory -ExecutablePath $executablePath -ExpectSymbols $true } 'expected symbols: True'
    Set-UInt32 (0x200 + 12) 2 # CodeView symbol directory.
    [IO.File]::WriteAllBytes($executablePath, $bytes)
    Assert-ExecutableDebugDirectory -ExecutablePath $executablePath -ExpectSymbols $true
    Assert-Rejected { Assert-ExecutableDebugDirectory -ExecutablePath $executablePath -ExpectSymbols $false } 'expected symbols: False'
    Set-UInt16 0x98 0x10b
    [IO.File]::WriteAllBytes($executablePath, $bytes)
    Assert-Rejected { Assert-ExecutableDebugDirectory -ExecutablePath $executablePath -ExpectSymbols $false } 'Expected x64 PE32+'
    Write-Host 'Native contract positive and negative fixtures passed.'
}
finally {
    Remove-Item -LiteralPath $projectPath, $executablePath -Force
    foreach ($fixtureFile in @($runtimeExecutable, $runtimeCompanion)) {
        if (Test-Path -LiteralPath $fixtureFile -PathType Leaf) {
            Remove-Item -LiteralPath $fixtureFile -Force
        }
    }
    if (Test-Path -LiteralPath $runtimeFixtureDirectory -PathType Container) {
        # Only remove the now-empty directory; never recursively clean temp paths.
        [IO.Directory]::Delete($runtimeFixtureDirectory, $false)
    }
}
