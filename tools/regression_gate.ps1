################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

[CmdletBinding()]
param(
    [switch]$NoConfigure
)

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot

if (-not [string]::IsNullOrEmpty(
        $env:QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE) -or
    -not [string]::IsNullOrEmpty(
        $env:QUADER_COMMAND_BEHAVIOR_CHANGE_REASON)) {
    throw 'The regression gate is read-only. Clear command behavior baseline update variables first.'
}

function Invoke-RegressionStep {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [scriptblock]$Action
    )

    Write-Host "[regression-gate] $Name"
    & $Action
    if ($LASTEXITCODE -ne 0) {
        throw "Regression gate step failed ($LASTEXITCODE): $Name"
    }
}

Push-Location $repositoryRoot
try {
    if (-not $NoConfigure) {
        Invoke-RegressionStep 'Configure vs2026' {
            & cmake --preset vs2026
        }
    }

    Invoke-RegressionStep 'Build all behavior and architecture test targets' {
        & cmake --build --preset vs2026-debug --target `
            quader_guardrail_tests `
            quader_tests `
            quader_modeling_domain_tests `
            quader_modeling_sdk_consumer_tests `
            quader_modeling_sdk_parity_tests
    }

    Invoke-RegressionStep 'Run the complete registered CTest suite' {
        & ctest --preset vs2026-debug --output-on-failure
    }

    Invoke-RegressionStep 'Build the shipping native executable' {
        & cmake --build --preset vs2026-debug --target quader
    }

    Write-Host '[regression-gate] PASS'
}
finally {
    Pop-Location
}
