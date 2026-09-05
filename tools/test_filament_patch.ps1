################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
$ErrorActionPreference = 'Stop'
$temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/')
$fixtureRoot = Join-Path $temporaryRoot ("quader-filament-patch-test-" + [Guid]::NewGuid().ToString('N'))
$sourceRoot = Join-Path $fixtureRoot 'build/source'
$patchPath = Join-Path $fixtureRoot 'fixture.patch'
$savedCeiling = $env:GIT_CEILING_DIRECTORIES
try {
    New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
    & git init --quiet $fixtureRoot
    if ($LASTEXITCODE -ne 0) { throw 'Cannot initialize fixture repository' }
    $targetPath = Join-Path $sourceRoot 'fixture.txt'
    [IO.File]::WriteAllText($targetPath, "before`n", [Text.UTF8Encoding]::new($false))
    $patch = "diff --git a/fixture.txt b/fixture.txt`n--- a/fixture.txt`n+++ b/fixture.txt`n@@ -1 +1 @@`n-before`n+after`n"
    [IO.File]::WriteAllText($patchPath, $patch, [Text.UTF8Encoding]::new($false))

    # Load only the production patch functions; never execute provisioning/build steps.
    $tokens = $null
    $errors = $null
    $script = [Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $PSScriptRoot 'build_filament_sdk.ps1'), [ref]$tokens, [ref]$errors)
    if ($errors.Count -ne 0) { throw "Filament builder syntax error: $errors" }
    $functions = @($script.FindAll({ param($node)
        $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -in @('Invoke-FilamentPatch', 'Test-PatchApplication')
    }, $true))
    if ($functions.Count -ne 2) { throw 'Cannot locate production patch functions' }
    foreach ($function in $functions) { Invoke-Expression $function.Extent.Text }

    if (-not (Test-PatchApplication)) { throw 'Pristine fixture must accept patch' }
    if ((Invoke-FilamentPatch -Arguments @($patchPath)) -ne 0) { throw 'Patch failed' }
    if ([IO.File]::ReadAllText($targetPath) -cne "after`n") {
        throw 'Git silently skipped the nested source patch'
    }
    if (Test-PatchApplication) { throw 'Applied fixture must reject another forward application' }
    if (-not (Test-PatchApplication -Reverse)) { throw 'Applied fixture must accept reverse check' }
    if ($env:GIT_CEILING_DIRECTORIES -cne $savedCeiling) { throw 'Patch command leaked Git environment' }
    Write-Host 'Filament patch applies inside an ancestor worktree and preserves its environment.'
}
finally {
    $env:GIT_CEILING_DIRECTORIES = $savedCeiling
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    if ([IO.Path]::GetDirectoryName($resolvedFixture) -ne $temporaryRoot -or
        -not [IO.Path]::GetFileName($resolvedFixture).StartsWith('quader-filament-patch-test-')) {
        throw "Refusing to remove a path outside the generated fixture: $resolvedFixture"
    }
    if (Test-Path -LiteralPath $resolvedFixture) { Remove-Item -LiteralPath $resolvedFixture -Recurse -Force }
}
