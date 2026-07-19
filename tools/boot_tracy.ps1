################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

[CmdletBinding()]
param(
    [string]$TracyProfiler,

    [string]$QuaderExe,

    [string]$CMakeCache,

    [string[]]$TracyArguments = @(),

    [string[]]$QuaderArguments = @(),

    [ValidateRange(1, 300)]
    [int]$StartupTimeoutSeconds = 30,

    [switch]$ValidateOnly,

    [switch]$NoPauseOnError
)

$ErrorActionPreference = 'Stop'

function Resolve-RequiredFile {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description not found: $Path"
    }

    return (Resolve-Path -LiteralPath $Path).Path
}

function Assert-TracyBuild {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CachePath
    )

    $cacheText = Get-Content -LiteralPath $CachePath -Raw
    if ($cacheText -notmatch '(?m)^QUADER_ENABLE_TRACY:BOOL=ON\s*$') {
        throw "Quader was not configured with QUADER_ENABLE_TRACY=ON: $CachePath"
    }

    if ($cacheText -notmatch '(?m)^QUADER_TRACY_ON_DEMAND:BOOL=ON\s*$') {
        throw "Quader was not configured with QUADER_TRACY_ON_DEMAND=ON: $CachePath"
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
    if ([string]::IsNullOrWhiteSpace($TracyProfiler)) {
        $TracyProfiler = Join-Path $PSScriptRoot '..\..\_SOURCES\windows-0.13.1\tracy-profiler.exe'
    }

    if ([string]::IsNullOrWhiteSpace($QuaderExe)) {
        $QuaderExe = Join-Path $PSScriptRoot '..\build\vs2026\Debug\quader.exe'
    }

    if ([string]::IsNullOrWhiteSpace($CMakeCache)) {
        $CMakeCache = Join-Path $PSScriptRoot '..\build\vs2026\CMakeCache.txt'
    }

    $profilerPath = Resolve-RequiredFile -Path $TracyProfiler -Description 'Tracy profiler'
    $quaderPath = Resolve-RequiredFile -Path $QuaderExe -Description 'Quader executable'
    $cachePath = Resolve-RequiredFile -Path $CMakeCache -Description 'CMake cache'
    Assert-TracyBuild -CachePath $cachePath

    if ($ValidateOnly) {
        Write-Host '[boot-tracy] Ready'
        Write-Host "[boot-tracy] Tracy: $profilerPath"
        Write-Host "[boot-tracy] Quader: $quaderPath"
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
