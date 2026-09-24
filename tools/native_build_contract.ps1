################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################

# Shared read-only validation for the profiler launcher and local native builds.

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

    if ($cacheText -notmatch '(?m)^QUADER_ENABLE_SENTRY:BOOL=OFF\s*$') {
        throw "The canonical executables must be configured with QUADER_ENABLE_SENTRY=OFF: $CachePath"
    }
}

function Get-ProjectConfigurationBlock {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath,

        [Parameter(Mandatory = $true)]
        [string]$Configuration
    )

    [xml]$project = Get-Content -LiteralPath $ProjectPath -Raw
    $namespace = [System.Xml.XmlNamespaceManager]::new($project.NameTable)
    $namespace.AddNamespace('msb', $project.Project.NamespaceURI)
    $node = $project.SelectSingleNode(
        "//msb:ItemDefinitionGroup[contains(@Condition, '$Configuration|x64')]",
        $namespace)
    if ($null -eq $node) {
        throw "The $Configuration x64 configuration was not found in: $ProjectPath"
    }

    return $node.OuterXml
}

function Assert-ProfilingBuildProject {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    $debugBlock = Get-ProjectConfigurationBlock -ProjectPath $ProjectPath -Configuration 'Debug'
    $releaseBlock = Get-ProjectConfigurationBlock -ProjectPath $ProjectPath -Configuration 'Release'

    foreach ($required in @(
            'QUADER_ENABLE_TRACY=1',
            'TRACY_ENABLE=1',
            'TRACY_ON_DEMAND=1',
            'NDEBUG',
            'QT_NO_DEBUG',
            '_ITERATOR_DEBUG_LEVEL=0',
            '<Optimization>MaxSpeed</Optimization>',
            '<InlineFunctionExpansion>AnySuitable</InlineFunctionExpansion>',
            '<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>')) {
        if (-not $debugBlock.Contains($required)) {
            throw "The Debug profiling contract is missing '$required' in: $ProjectPath"
        }
    }

    foreach ($forbidden in @('MultiThreadedDebugDLL', '<BasicRuntimeChecks>EnableFastChecks</BasicRuntimeChecks>')) {
        if ($debugBlock.Contains($forbidden)) {
            throw "The Debug profiling contract contains slow runtime token '$forbidden' in: $ProjectPath"
        }
    }

    foreach ($forbidden in @('QUADER_ENABLE_TRACY', 'TRACY_ENABLE', 'TRACY_ON_DEMAND')) {
        if ($releaseBlock.Contains($forbidden)) {
            throw "Release unexpectedly contains Tracy token '$forbidden' in: $ProjectPath"
        }
    }
}

function Assert-NativeExecutableLinkContract {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ProjectPath
    )

    $debugBlock = Get-ProjectConfigurationBlock -ProjectPath $ProjectPath -Configuration 'Debug'
    $releaseBlock = Get-ProjectConfigurationBlock -ProjectPath $ProjectPath -Configuration 'Release'
    $normalizedDebugBlock = $debugBlock.Replace('\', '/')
    $normalizedReleaseBlock = $releaseBlock.Replace('\', '/')

    foreach ($required in @(
            'TracyClient',
            '<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>',
            'NDEBUG',
            'QT_NO_DEBUG',
            '_ITERATOR_DEBUG_LEVEL=0',
            '/lib/x86_64/md/filament.lib',
            '/lib/Qt6Widgets.lib',
            '/lib/Qt6Gui.lib',
            '/lib/Qt6Core.lib')) {
        if (-not $normalizedDebugBlock.Contains($required)) {
            throw "The Debug profiling executable is missing Release-speed token '$required' in: $ProjectPath"
        }
    }

    foreach ($forbidden in @(
            'MultiThreadedDebugDLL',
            '/mdd/',
            'matdbg.lib',
            '/debug/lib/',
            '/debug/bin',
            'Qt6Widgetsd.lib',
            'Qt6Guid.lib',
            'Qt6Cored.lib',
            'sentry.lib')) {
        if ($normalizedDebugBlock.Contains($forbidden)) {
            throw "The Debug profiling executable contains slow or optional token '$forbidden' in: $ProjectPath"
        }
    }

    foreach ($forbidden in @('TracyClient', 'sentry.lib')) {
        if ($normalizedReleaseBlock.Contains($forbidden)) {
            throw "Release unexpectedly links '$forbidden' in: $ProjectPath"
        }
    }
}

function Get-NativeRuntimeDeploymentFiles {
    return @(
            'Qt6Core.dll',
            'Qt6Gui.dll',
            'Qt6Widgets.dll',
            'double-conversion.dll',
            'fmt.dll',
            'icuuc.dll',
            'jpeg62.dll',
            'libsharpyuv.dll',
            'libpng16.dll',
            'libwebp.dll',
            'libwebpdemux.dll',
            'libwebpmux.dll',
            'md4c.dll',
            'pcre2-16.dll',
            'spdlog.dll',
            'z.dll',
            'zstd.dll',
            'platforms\qwindows.dll',
            'imageformats\qgif.dll',
            'imageformats\qjpeg.dll',
            'imageformats\qtga.dll',
            'imageformats\qtiff.dll',
            'imageformats\qwebp.dll',
            'tiff.dll')
}

function Assert-NativeRuntimeDeployment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath
    )

    [void](Resolve-NativeFatalDiagnosticsExecutable -ExecutablePath $ExecutablePath)
    $runtimeDirectory = Split-Path -Parent $ExecutablePath
    $buildDirectory = Split-Path -Parent $runtimeDirectory
    $releaseBin = Join-Path $buildDirectory 'vcpkg_installed\x64-windows\bin'
    $releaseImageFormats = Join-Path $buildDirectory `
        'vcpkg_installed\x64-windows\Qt6\plugins\imageformats'
    foreach ($required in @(Get-NativeRuntimeDeploymentFiles)) {
        $requiredPath = Join-Path $runtimeDirectory $required
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "The native runtime is missing Release Qt file: $requiredPath"
        }
    }

    foreach ($forbidden in @(
            'Qt6Cored.dll',
            'Qt6Guid.dll',
            'Qt6Widgetsd.dll',
            'fmtd.dll',
            'spdlogd.dll',
            'concrt140d.dll',
            'msvcp140d.dll',
            'msvcp140_1d.dll',
            'msvcp140_2d.dll',
            'vccorlib140d.dll',
            'vcruntime140d.dll',
            'vcruntime140_1d.dll',
            'vcruntime140_threadsd.dll',
            'sentry.dll',
            'crashpad_handler.exe',
            'platforms\qwindowsd.dll',
            'imageformats\qgifd.dll',
            'imageformats\qicod.dll',
            'imageformats\qjpegd.dll',
            'imageformats\qtgad.dll',
            'imageformats\qtiffd.dll',
            'imageformats\qwebpd.dll')) {
        $forbiddenPath = Join-Path $runtimeDirectory $forbidden
        if (Test-Path -LiteralPath $forbiddenPath -PathType Leaf) {
            throw "The native runtime still contains slow Qt Debug file: $forbiddenPath"
        }
    }

    foreach ($releaseRuntime in @(
            'Qt6Core.dll',
            'Qt6Gui.dll',
            'Qt6Widgets.dll',
            'double-conversion.dll',
            'fmt.dll',
            'jpeg62.dll',
            'libsharpyuv.dll',
            'libpng16.dll',
            'libwebp.dll',
            'libwebpdemux.dll',
            'libwebpmux.dll',
            'md4c.dll',
            'pcre2-16.dll',
            'spdlog.dll',
            'tiff.dll',
            'z.dll',
            'zstd.dll')) {
        $deployedPath = Join-Path $runtimeDirectory $releaseRuntime
        $releasePath = Join-Path $releaseBin $releaseRuntime
        if (-not (Test-Path -LiteralPath $releasePath -PathType Leaf)) {
            throw "The configured Release runtime source is missing: $releasePath"
        }
        $deployedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $deployedPath).Hash
        $releaseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $releasePath).Hash
        if ($deployedHash -ne $releaseHash) {
            throw "The native runtime is not the Release artifact for '$releaseRuntime': $deployedPath"
        }
    }

    foreach ($releasePlugin in @(
            'qgif.dll',
            'qjpeg.dll',
            'qtga.dll',
            'qtiff.dll',
            'qwebp.dll')) {
        $deployedPath = Join-Path $runtimeDirectory "imageformats\$releasePlugin"
        $releasePath = Join-Path $releaseImageFormats $releasePlugin
        if (-not (Test-Path -LiteralPath $releasePath -PathType Leaf)) {
            throw "The configured Release image plugin source is missing: $releasePath"
        }
        $deployedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $deployedPath).Hash
        $releaseHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $releasePath).Hash
        if ($deployedHash -ne $releaseHash) {
            throw "The native runtime is not the Release image plugin '$releasePlugin': $deployedPath"
        }
    }
}

function Resolve-NativeFatalDiagnosticsExecutable {
    param([Parameter(Mandatory = $true)][string]$ExecutablePath)

    $companionPath = Join-Path (Split-Path -Parent $ExecutablePath) `
        'quader_fatal_diagnostics.exe'
    return Resolve-RequiredFile -Path $companionPath `
        -Description 'Standalone fatal diagnostics companion'
}

function Assert-FatalDiagnosticsBuildProject {
    param([Parameter(Mandatory = $true)][string]$ProjectPath)

    foreach ($configuration in @('Debug', 'Release')) {
        $block = Get-ProjectConfigurationBlock -ProjectPath $ProjectPath `
            -Configuration $configuration
        foreach ($required in @('<Optimization>MaxSpeed</Optimization>',
                '<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>', 'NDEBUG')) {
            if (-not $block.Contains($required)) {
                throw "The $configuration fatal diagnostics helper is missing '$required': $ProjectPath"
            }
        }
        if ($block -notmatch '(?i)\bdbghelp\.lib\b') {
            throw "The $configuration fatal diagnostics helper must link Dbghelp: $ProjectPath"
        }
        foreach ($forbidden in @('QUADER_ENABLE_TRACY', 'TRACY_ENABLE',
                'TRACY_ON_DEMAND', 'TracyClient', 'QUADER_ENABLE_SENTRY',
                'sentry.lib', 'Qt6', 'filament.lib', 'quader_runtime.lib',
                'quader_platform.lib', 'quader_app.lib', 'MultiThreadedDebugDLL')) {
            if ($block.IndexOf($forbidden, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                throw "The $configuration fatal diagnostics helper contains forbidden dependency or instrumentation '$forbidden': $ProjectPath"
            }
        }
    }
}

function Resolve-DumpbinPath {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($null -ne $command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw "Visual Studio locator was not found: $vswhere"
    }
    $installation = @(& $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
            -property installationPath) | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($installation)) {
        throw 'Visual Studio C++ tools were not found.'
    }

    $candidates = @(Get-ChildItem `
            -LiteralPath (Join-Path $installation 'VC\Tools\MSVC') `
            -Filter dumpbin.exe -Recurse -File | `
        Where-Object { $_.FullName -match 'Hostx64\\x64\\dumpbin\.exe$' } | `
        Sort-Object FullName -Descending)
    if ($candidates.Count -eq 0) {
        throw "dumpbin.exe was not found under: $installation"
    }
    return $candidates[0].FullName
}

function Assert-RuntimeDependencyClosure {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath
    )

    $dumpbin = Resolve-DumpbinPath
    $runtimeDirectory = Split-Path -Parent $ExecutablePath
    $fatalDiagnostics = Resolve-NativeFatalDiagnosticsExecutable -ExecutablePath $ExecutablePath
    $binaries = @((Get-Item -LiteralPath $ExecutablePath),
        (Get-Item -LiteralPath $fatalDiagnostics)) +
        @(Get-ChildItem -LiteralPath $runtimeDirectory -Recurse -File `
            -Filter '*.dll')
    $missing = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)
    foreach ($binary in $binaries) {
        $dependencyOutput = @(& $dumpbin /nologo /dependents $binary.FullName)
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin failed for runtime binary: $($binary.FullName)"
        }
        foreach ($line in $dependencyOutput) {
            if ($line -notmatch '^\s+([^\s]+\.dll)\s*$') {
                continue
            }
            $dependency = $Matches[1]
            if ($binary.FullName -eq $fatalDiagnostics -and
                $dependency -match '(?i)(tracy|sentry|crashpad|^Qt6|filament)') {
                throw "The standalone fatal diagnostics helper imports forbidden runtime '$dependency': $fatalDiagnostics"
            }
            if ($dependency -match '^(api-ms-win|ext-ms-win)-') {
                continue
            }
            $candidates = @(
                (Join-Path $binary.DirectoryName $dependency),
                (Join-Path $runtimeDirectory $dependency),
                (Join-Path $env:WINDIR "System32\$dependency"),
                (Join-Path $env:WINDIR "SysWOW64\$dependency"))
            if (-not ($candidates | Where-Object {
                        Test-Path -LiteralPath $_ -PathType Leaf
                    } | Select-Object -First 1)) {
                [void]$missing.Add("$($binary.Name) -> $dependency")
            }
        }
    }
    if ($missing.Count -ne 0) {
        throw "Runtime dependency closure is incomplete:`n$($missing -join "`n")"
    }
}


function Assert-ReleaseBuildProject {
    param([Parameter(Mandatory = $true)][string]$ProjectPath)
    $releaseBlock = Get-ProjectConfigurationBlock -ProjectPath $ProjectPath -Configuration 'Release'
    foreach ($forbidden in @('QUADER_ENABLE_TRACY', 'TRACY_ENABLE', 'TRACY_ON_DEMAND',
            'QUADER_ENABLE_SENTRY', 'TracyClient', 'sentry.lib', 'MultiThreadedDebugDLL',
            '<DebugInformationFormat>OldStyle</DebugInformationFormat>',
            '<DebugInformationFormat>ProgramDatabase</DebugInformationFormat>',
            '<GenerateDebugInformation>true</GenerateDebugInformation>')) {
        if ($releaseBlock.Contains($forbidden)) {
            throw "Release contains forbidden instrumentation/debug token '$forbidden': $ProjectPath"
        }
    }
    [xml]$project = Get-Content -LiteralPath $ProjectPath -Raw
    $namespace = [System.Xml.XmlNamespaceManager]::new($project.NameTable)
    $namespace.AddNamespace('msb', $project.Project.NamespaceURI)
    $wholeProgram = $project.SelectSingleNode(
        "//msb:PropertyGroup[contains(@Condition, 'Release|x64')]/msb:WholeProgramOptimization", $namespace)
    if ($null -eq $wholeProgram -or $wholeProgram.InnerText -ne 'true') {
        throw "Release whole-program optimization is missing: $ProjectPath"
    }
    foreach ($required in @('<Optimization>MaxSpeed</Optimization>',
            '<RuntimeLibrary>MultiThreadedDLL</RuntimeLibrary>')) {
        if (-not $releaseBlock.Contains($required)) {
            throw "Release build contract is missing '$required': $ProjectPath"
        }
    }
}

function Assert-ExecutableDebugDirectory {
    param(
        [Parameter(Mandatory = $true)][string]$ExecutablePath,
        [Parameter(Mandatory = $true)][bool]$ExpectSymbols
    )
    # Read the PE data directory directly, independent of dumpbin's UI language.
    $stream = [IO.File]::OpenRead($ExecutablePath)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Not a Windows executable: $ExecutablePath" }
        $stream.Position = 0x3c
        $peOffset = $reader.ReadUInt32()
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $ExecutablePath" }
        $stream.Position = $peOffset + 6
        $sectionCount = $reader.ReadUInt16()
        $stream.Position = $peOffset + 20
        $optionalHeaderSize = $reader.ReadUInt16()
        $optionalHeader = $peOffset + 24
        $stream.Position = $optionalHeader
        if ($reader.ReadUInt16() -ne 0x20b) { throw "Expected x64 PE32+ executable: $ExecutablePath" }
        $stream.Position = $optionalHeader + 108
        if ($reader.ReadUInt32() -lt 7) { throw "Missing PE data directories: $ExecutablePath" }
        $stream.Position = $optionalHeader + 112 + 6 * 8
        $debugAddress = $reader.ReadUInt32()
        $debugSize = $reader.ReadUInt32()
        $hasSymbols = $false
        if ($debugAddress -ne 0 -and $debugSize -ne 0) {
            $debugFileOffset = $null
            for ($section = 0; $section -lt $sectionCount; ++$section) {
                $stream.Position = $optionalHeader + $optionalHeaderSize + $section * 40 + 8
                $virtualSize = $reader.ReadUInt32()
                $virtualAddress = $reader.ReadUInt32()
                $rawSize = $reader.ReadUInt32()
                $rawPointer = $reader.ReadUInt32()
                if ($debugAddress -ge $virtualAddress -and
                    $debugAddress + $debugSize -le $virtualAddress + [Math]::Max($virtualSize, $rawSize)) {
                    $debugFileOffset = $rawPointer + $debugAddress - $virtualAddress
                    break
                }
            }
            if ($null -eq $debugFileOffset -or $debugSize % 28 -ne 0) {
                throw "Invalid PE debug directory: $ExecutablePath"
            }
            for ($entry = 0; $entry -lt $debugSize / 28; ++$entry) {
                $stream.Position = $debugFileOffset + $entry * 28 + 12
                $debugType = $reader.ReadUInt32()
                # LTCG's COFFGRP/ILTCG metadata is not source-level debug information.
                if ($debugType -eq 1 -or $debugType -eq 2) { $hasSymbols = $true }
            }
        }
        if ($hasSymbols -ne $ExpectSymbols) {
            throw "PE debug-directory contract failed (expected symbols: $ExpectSymbols): $ExecutablePath"
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}
