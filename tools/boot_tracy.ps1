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

function Assert-ProfileRuntimeDeployment {
    param(
        [Parameter(Mandatory = $true)]
        [string]$ExecutablePath
    )

    $runtimeDirectory = Split-Path -Parent $ExecutablePath
    $buildDirectory = Split-Path -Parent $runtimeDirectory
    $releaseBin = Join-Path $buildDirectory 'vcpkg_installed\x64-windows\bin'
    $releaseImageFormats = Join-Path $buildDirectory `
        'vcpkg_installed\x64-windows\Qt6\plugins\imageformats'
    foreach ($required in @(
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
            'tiff.dll')) {
        $requiredPath = Join-Path $runtimeDirectory $required
        if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
            throw "The Debug profiling runtime is missing Release Qt file: $requiredPath"
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
            throw "The Debug profiling runtime still contains slow Qt Debug file: $forbiddenPath"
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
            throw "The Debug profiling runtime is not the Release artifact for '$releaseRuntime': $deployedPath"
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
            throw "The Debug profiling runtime is not the Release image plugin '$releasePlugin': $deployedPath"
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
    $binaries = @((Get-Item -LiteralPath $ExecutablePath)) +
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
    Assert-ProfileRuntimeDeployment -ExecutablePath $quaderPath
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
