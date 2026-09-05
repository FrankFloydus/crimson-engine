################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

[CmdletBinding()]
param(
    [string]$TracyProfiler,

    [string[]]$TracyArguments = @(),

    [string[]]$QuaderArguments = @(),

    [ValidateRange(1, 300)]
    [int]$StartupTimeoutSeconds = 30,

    [switch]$ValidateOnly,

    [switch]$NoPauseOnError
)

$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'native_build_contract.ps1')

function Invoke-CMakeStep {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$CMakeArguments,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    Write-Host "[boot-tracy] $Description"
    & cmake @CMakeArguments
    if ($LASTEXITCODE -ne 0) {
        throw "cmake failed with exit code $LASTEXITCODE while $Description"
    }
}

function Start-DeveloperProcess {
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$Arguments = @()
    )

    $startParameters = @{
        FilePath = $FilePath
        WorkingDirectory = Split-Path -Parent $FilePath
        PassThru = $true
    }

    if ($Arguments.Count -gt 0) {
        $startParameters.ArgumentList = $Arguments
    }

    return Start-Process @startParameters
}

function Wait-ForMainWindow {
    param(
        [Parameter(Mandatory = $true)]
        [System.Diagnostics.Process]$Process,

        [Parameter(Mandatory = $true)]
        [string]$Description,

        [Parameter(Mandatory = $true)]
        [int]$TimeoutSeconds
    )

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            throw "$Description exited during startup with code $($Process.ExitCode)."
        }

        $Process.Refresh()
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return
        }

        Start-Sleep -Milliseconds 100
    }

    throw "Timed out waiting for the $Description window after $TimeoutSeconds seconds."
}

try {
    $repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    if ([string]::IsNullOrWhiteSpace($TracyProfiler)) {
        $TracyProfiler = Join-Path $PSScriptRoot '..\..\_SOURCES\windows-0.13.1\tracy-profiler.exe'
    }

    $QuaderExe = Join-Path $repositoryRoot 'build\vs2026\Debug\quader.exe'
    $CMakeCache = Join-Path $repositoryRoot 'build\vs2026\CMakeCache.txt'
    $ProfileProject = Join-Path $repositoryRoot 'build\vs2026\quader_modeling.vcxproj'
    $NativeProject = Join-Path $repositoryRoot 'build\vs2026\quader.vcxproj'

    $profilerPath = Resolve-RequiredFile -Path $TracyProfiler -Description 'Tracy profiler'

    if (-not $ValidateOnly) {
        Push-Location $repositoryRoot
        try {
            Invoke-CMakeStep -Description 'Configuring the canonical Visual Studio tree...' `
                -CMakeArguments @('--preset', 'vs2026')
            Invoke-CMakeStep -Description 'Building the Tracy-enabled Debug executable...' `
                -CMakeArguments @('--build', '--preset', 'vs2026-debug', '--target', 'quader')
        }
        finally {
            Pop-Location
        }
    }

    $quaderPath = Resolve-RequiredFile -Path $QuaderExe -Description 'Quader executable'
    $cachePath = Resolve-RequiredFile -Path $CMakeCache -Description 'CMake cache'
    $profileProjectPath = Resolve-RequiredFile -Path $ProfileProject -Description 'Quader profiling project'
    $nativeProjectPath = Resolve-RequiredFile -Path $NativeProject -Description 'Quader executable project'
    Assert-TracyBuild -CachePath $cachePath
    Assert-ProfilingBuildProject -ProjectPath $profileProjectPath
    Assert-NativeExecutableLinkContract -ProjectPath $nativeProjectPath
    Assert-NativeRuntimeDeployment -ExecutablePath $quaderPath
    Assert-RuntimeDependencyClosure -ExecutablePath $quaderPath

    if ($ValidateOnly) {
        Write-Host '[boot-tracy] Ready'
        Write-Host "[boot-tracy] Tracy: $profilerPath"
        Write-Host "[boot-tracy] Quader: $quaderPath"
        Write-Host '[boot-tracy] Config: Debug profile (Release-speed runtime/dependencies, symbols ON, Tracy ON)'
        Write-Host "[boot-tracy] Cache:  $cachePath"
        return
    }

    Write-Host '[boot-tracy] Starting Tracy profiler...'
    $tracyProcess = Start-DeveloperProcess -FilePath $profilerPath -Arguments $TracyArguments
    Wait-ForMainWindow -Process $tracyProcess -Description 'Tracy profiler' -TimeoutSeconds $StartupTimeoutSeconds

    Write-Host '[boot-tracy] Starting Tracy-enabled Quader...'
    $quaderProcess = Start-DeveloperProcess -FilePath $quaderPath -Arguments $QuaderArguments
    Wait-ForMainWindow -Process $quaderProcess -Description 'Quader' -TimeoutSeconds $StartupTimeoutSeconds

    Write-Host "[boot-tracy] Ready (Tracy PID $($tracyProcess.Id), Quader PID $($quaderProcess.Id))."
}
catch {
    Write-Host ''
    Write-Host "[boot-tracy] ERROR: $($_.Exception.Message)" -ForegroundColor Red

    if ($_.InvocationInfo.PositionMessage) {
        Write-Host $_.InvocationInfo.PositionMessage -ForegroundColor DarkRed
    }

    if (-not $NoPauseOnError) {
        Write-Host ''
        Write-Host '[boot-tracy] The launcher stopped before startup completed.' -ForegroundColor Yellow
        [void](Read-Host 'Press Enter to close this window')
    }

    exit 1
}
