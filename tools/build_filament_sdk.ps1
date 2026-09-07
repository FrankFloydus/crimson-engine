################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
<#
.SYNOPSIS
Builds Quader's pinned Filament SDK with Vulkan VSync and light attenuation controls.
.DESCRIPTION
Requires CMake, Git, Python 3.10+, and Visual Studio 2026 C++ tools. Only -AllowDownload
permits network access. Source, build, and installed SDK stay outside this repository
by default. This builds the Release /MD dependency; it does not build native Quader.
Existing source must have this tool's provenance marker. Nothing is deleted.
.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build_filament_sdk.ps1 -AllowDownload
#>
[CmdletBinding()]
param(
    [string]$WorkRoot = "",
    [switch]$AllowDownload,
    [ValidateRange(1, 64)][int]$Jobs = 6,
    [switch]$PrepareOnly,
    [switch]$InstallOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$repoRoot = Split-Path $PSScriptRoot -Parent
$pinPath = Join-Path $repoRoot "thirdparty/filament/filament-v1.71.6-quader-vsync2.json"
$pin = Get-Content -LiteralPath $pinPath -Raw | ConvertFrom-Json
$patchPath = Join-Path (Split-Path $pinPath -Parent) $pin.patch_file
$patchSha = (Get-FileHash -LiteralPath $patchPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($WorkRoot)) {
    $WorkRoot = Join-Path (Split-Path $repoRoot -Parent) "_SOURCES"
}
$WorkRoot = [IO.Path]::GetFullPath($WorkRoot)
$stem = "filament-v$($pin.upstream_version)-quader-vsync$($pin.patch_version)"
$archive = Join-Path $WorkRoot "filament-$($pin.upstream_commit).tar.gz"
$sourceRoot = Join-Path $WorkRoot "$stem-source"
$buildRoot = Join-Path $WorkRoot "$stem-build"
$sdkRoot = Join-Path $WorkRoot $pin.artifact
$sourceMarker = Join-Path $sourceRoot ".quader-filament-source.json"

function Invoke-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE."
    }
}

function Invoke-FilamentPatch([string[]]$Arguments, [switch]$Quiet) {
    $savedPreference = $ErrorActionPreference
    $savedCeiling = $env:GIT_CEILING_DIRECTORIES
    try {
        # Source may be staged below the Quader worktree. Without this ceiling Git
        # discovers that parent repository and silently skips all patch paths.
        $env:GIT_CEILING_DIRECTORIES = Split-Path -Parent $sourceRoot
        $ErrorActionPreference = "Continue"
        $output = @(& git -c core.autocrlf=false -C $sourceRoot apply @Arguments 2>&1 |
            ForEach-Object { "$_" })
        $exitCode = $LASTEXITCODE
        if (-not $Quiet) { $output | Out-Host }
        return $exitCode
    } finally {
        $ErrorActionPreference = $savedPreference
        $env:GIT_CEILING_DIRECTORIES = $savedCeiling
    }
}

function Test-PatchApplication([switch]$Reverse) {
    $arguments = @("--check")
    if ($Reverse) { $arguments += "--reverse" }
    $arguments += $patchPath
    return (Invoke-FilamentPatch -Arguments $arguments -Quiet) -eq 0
}

New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    if (-not $AllowDownload) {
        throw "Pinned archive missing: $archive. Run again with -AllowDownload to fetch it."
    }
    $uri = "https://codeload.github.com/google/filament/tar.gz/$($pin.upstream_commit)"
    Write-Host "Downloading pinned Filament source: $uri"
    Invoke-WebRequest -Uri $uri -OutFile $archive -UseBasicParsing
}
$sourceSha = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($sourceSha -ne $pin.source_sha256) {
    throw "Filament source checksum mismatch: $sourceSha. Archive was not extracted."
}

if (-not (Test-Path -LiteralPath $sourceRoot)) {
    New-Item -ItemType Directory -Path $sourceRoot | Out-Null
    Invoke-Checked "python" @((Join-Path $PSScriptRoot "verify_filament_source.py"),
        "--archive", $archive, "--source", $sourceRoot, "--patch", $patchPath,
        "--commit", $pin.upstream_commit, "--extract")
    @{ upstream_commit = $pin.upstream_commit; source_sha256 = $sourceSha } |
        ConvertTo-Json | Set-Content -LiteralPath $sourceMarker -Encoding UTF8
} elseif (-not (Test-Path -LiteralPath $sourceMarker)) {
    throw "Existing source has no provenance marker: $sourceRoot. Choose a fresh -WorkRoot."
}
$existingSource = Get-Content -LiteralPath $sourceMarker -Raw | ConvertFrom-Json
if ($existingSource.upstream_commit -ne $pin.upstream_commit -or
    $existingSource.source_sha256 -ne $sourceSha) {
    throw "Existing Filament source has different provenance. Choose a fresh -WorkRoot."
}

# Accept precisely the unapplied patch or its already-applied reverse, never fuzzy editing.
if (Test-PatchApplication) {
    if ((Invoke-FilamentPatch -Arguments @($patchPath)) -ne 0) {
        throw "Failed to apply the pinned Filament patch."
    }
} else {
    if (-not (Test-PatchApplication -Reverse)) {
        throw "Filament source does not accept the pinned patch or its reverse. Choose a fresh -WorkRoot."
    }
}
Invoke-Checked "python" @((Join-Path $PSScriptRoot "verify_filament_source.py"),
    "--archive", $archive, "--source", $sourceRoot, "--patch", $patchPath,
    "--commit", $pin.upstream_commit)
if ($PrepareOnly) {
    Write-Host "Prepared pinned Filament source: $sourceRoot"
    return
}

# Disable nested /MP so --parallel is the single bounded job owner. Upstream adds /MD
# when USE_STATIC_CRT=OFF; both Quader configurations consume these Release libraries.
if (-not $InstallOnly) {
    Invoke-Checked "cmake" @(
        "-S", $sourceRoot, "-B", $buildRoot, "-G", "Visual Studio 18 2026", "-A", "x64",
        "-DCMAKE_INSTALL_PREFIX=$sdkRoot", "-DCMAKE_CONFIGURATION_TYPES=Release",
        "-DDIST_DIR=x86_64/md",
        "-DUSE_STATIC_CRT=OFF", "-DFILAMENT_SHORTEN_MSVC_COMPILATION=OFF",
        "-DFILAMENT_SKIP_SAMPLES=ON", "-DFILAMENT_SKIP_SDL2=ON",
        "-DFILAMENT_BUILD_TESTING=OFF", "-DFILAMENT_ENABLE_MATDBG=OFF",
        "-DFILAMENT_SUPPORTS_VULKAN=ON", "-DFILAMENT_SUPPORTS_OPENGL=ON",
        "-DFILAMENT_SUPPORTS_WEBGPU=OFF", "-DFILAMENT_BUILD_FILAMAT=ON"
    )
    Invoke-Checked "cmake" @("--build", $buildRoot, "--config", "Release",
        "--parallel", "$Jobs", "--", "/nodeReuse:false")
} else {
    $previousMarker = Join-Path $sdkRoot "quader-filament-sdk.json"
    if (-not (Test-Path -LiteralPath $previousMarker)) {
        throw "InstallOnly requires a previously completed Filament build and install."
    }
    $previousBuild = Get-Content -LiteralPath $previousMarker -Raw | ConvertFrom-Json
    if ($previousBuild.patch_sha256 -ne $patchSha -or $previousBuild.source_sha256 -ne $sourceSha -or
        $previousBuild.runtime -ne "MD" -or $previousBuild.configuration -ne "Release") {
        throw "InstallOnly cannot certify different compiled inputs. Run a full dependency build."
    }
    $cachePath = Join-Path $buildRoot "CMakeCache.txt"
    if (-not (Test-Path -LiteralPath $cachePath)) { throw "No configured Filament build to install." }
    $cache = Get-Content -LiteralPath $cachePath -Raw
    foreach ($requiredSetting in @("USE_STATIC_CRT:BOOL=OFF", "DIST_DIR:UNINITIALIZED=x86_64/md",
        "CMAKE_CONFIGURATION_TYPES:UNINITIALIZED=Release", "CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026")) {
        if (-not $cache.Contains($requiredSetting)) {
            throw "Existing Filament build violates the install contract: $requiredSetting"
        }
    }
}
Invoke-Checked "cmake" @("--install", $buildRoot, "--config", "Release")

# Upstream installs VulkanPlatform.h but omits BlueVK's transitive public headers.
# Install the complete matching header groups, including beta video declarations.
$platformHeaderFiles = @()
$bluevkInclude = Join-Path $sourceRoot "libs/bluevk/include"
foreach ($group in @("bluevk", "vulkan", "vk_video")) {
    $groupSource = Join-Path $bluevkInclude $group
    Copy-Item -LiteralPath $groupSource -Destination (Join-Path $sdkRoot "include") -Recurse -Force
    foreach ($header in (Get-ChildItem -LiteralPath $groupSource -Recurse -File)) {
        $relative = $header.FullName.Substring($bluevkInclude.Length + 1).Replace('\', '/')
        $platformHeaderFiles += "include/$relative"
    }
}

$requiredFiles = @(
    "include/backend/DriverEnums.h", "include/backend/platforms/VulkanPlatform.h",
    "include/backend/platforms/VulkanPlatformWindows.h",
    "include/filament/SwapChain.h", "include/filament/Engine.h",
    "include/filament/LightManager.h", "bin/matc.exe"
) + $platformHeaderFiles
$libraries = @("filament", "backend", "bluegl", "bluevk", "filabridge", "filaflat",
    "utils", "geometry", "smol-v", "ibl", "ktxreader", "image", "basis_transcoder",
    "abseil", "camutils", "shaders")
foreach ($library in $libraries) {
    $requiredFiles += "lib/x86_64/md/$library.lib"
}
$fileHashes = [ordered]@{}
foreach ($relativePath in $requiredFiles) {
    $path = Join-Path $sdkRoot $relativePath
    if (-not (Test-Path -LiteralPath $path)) { throw "Built SDK is incomplete: $path" }
    $fileHashes[$relativePath] = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($InstallOnly -and ($relativePath.StartsWith("lib/") -or $relativePath -eq "bin/matc.exe")) {
        if ($previousBuild.file_sha256.$relativePath -ne $fileHashes[$relativePath]) {
            throw "InstallOnly cannot certify changed compiled output: $relativePath. Run a full dependency build."
        }
    }
}
Copy-Item -LiteralPath (Join-Path $sourceRoot "LICENSE") -Destination (Join-Path $sdkRoot "LICENSE")
$installManifest = [ordered]@{
    artifact = $pin.artifact
    upstream_version = $pin.upstream_version
    upstream_commit = $pin.upstream_commit
    source_sha256 = $sourceSha
    patch_sha256 = $patchSha
    patch_version = $pin.patch_version
    runtime = "MD"
    configuration = "Release"
    file_sha256 = $fileHashes
} | ConvertTo-Json -Depth 4
[IO.File]::WriteAllText((Join-Path $sdkRoot "quader-filament-sdk.json"),
    $installManifest + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Host "Built Filament SDK: $sdkRoot"
