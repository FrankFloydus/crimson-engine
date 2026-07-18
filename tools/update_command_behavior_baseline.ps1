################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [ValidateScript({ -not [string]::IsNullOrWhiteSpace($_) })]
    [string]$Reason
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testExecutable = Join-Path $repositoryRoot 'build/vs2026/Debug/quader_tests.exe'

Push-Location $repositoryRoot
try {
    & cmake --build --preset vs2026-debug --target quader_tests
    if ($LASTEXITCODE -ne 0) {
        throw "Could not build quader_tests ($LASTEXITCODE)."
    }

    $previousUpdate = $env:QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE
    $previousReason = $env:QUADER_COMMAND_BEHAVIOR_CHANGE_REASON
    try {
        $env:QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE = '1'
        $env:QUADER_COMMAND_BEHAVIOR_CHANGE_REASON = $Reason
        & $testExecutable `
            '--gtest_filter=CommandBehaviorContractTests.ApprovedFingerprintCoversEveryCommand'
        if ($LASTEXITCODE -ne 0) {
            throw "The command behavior baseline was not approved ($LASTEXITCODE)."
        }
    }
    finally {
        $env:QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE = $previousUpdate
        $env:QUADER_COMMAND_BEHAVIOR_CHANGE_REASON = $previousReason
    }

    Write-Host 'Updated tests/baselines/command_behavior.json.'
    Write-Host 'Review the complete baseline diff before committing it.'
}
finally {
    Pop-Location
}
