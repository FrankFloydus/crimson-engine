################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
[CmdletBinding()]
param(
    [string]$Destination,
    [switch]$PlanOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'native_build_contract.ps1')

function Normalize-PackageDirectoryPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    $absolute = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($absolute)
    $trimmed = $absolute.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $trimmedRoot = $root.TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    if ($trimmed.Length -gt $trimmedRoot.Length) {
        return $trimmed
    }
    return $absolute
}

$repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$sourceDirectory = (Resolve-Path -LiteralPath (Join-Path $repositoryRoot 'build/vs2026/Release')).Path
$publicExecutableName = 'quader-0.1.exe'
if ([string]::IsNullOrWhiteSpace($Destination)) {
    $Destination = Join-Path $repositoryRoot 'build/distributions/quader-0.1'
}
$destinationDirectory = Normalize-PackageDirectoryPath $Destination
if (Test-Path -LiteralPath $destinationDirectory) {
    throw "The public package destination already exists; select a new directory: $destinationDirectory"
}
if ($destinationDirectory.StartsWith($sourceDirectory + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The public package must be separate from the canonical Release directory.'
}

# Validate the canonical Public artifact and its Release dependency identities.
# This intentionally has no alternate source or skip-validation switch.
& (Join-Path $PSScriptRoot 'verify_native_build.ps1') -Profile Public -Configuration Release
$files = [Collections.Generic.SortedSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$sourcePaths = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
$pendingBinaries = [Collections.Generic.Queue[string]]::new()
$dumpbin = Resolve-DumpbinPath

function Add-PackageFile {
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    $relative = $RelativePath.Replace('/', '\')
    $source = [IO.Path]::GetFullPath((Join-Path $sourceDirectory $relative))
    if (-not $source.StartsWith($sourceDirectory + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Package source escapes the canonical Release directory: $relative"
    }
    $item = Get-Item -LiteralPath $source
    if ($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Package source must be a regular file: $source"
    }
    if ($relative -match '(?i)(^|\\)(quader\.exe|gtest[^\\]*|tracy[^\\]*|sentry[^\\]*|quader_bridge_reader[^\\]*)$' -or
        [IO.Path]::GetExtension($relative) -match '(?i)^\.(pdb|lib|exp|obj|log)$') {
        throw "Developer artifact is not permitted in a public package: $relative"
    }
    $sourcePaths[$relative] = $source
    if ($files.Add($relative) -and [IO.Path]::GetExtension($relative) -in @('.exe', '.dll')) {
        $pendingBinaries.Enqueue($relative)
    }
}

Add-PackageFile $publicExecutableName
Add-PackageFile 'quader_fatal_diagnostics.exe'
foreach ($runtime in @(Get-NativeRuntimeDeploymentFiles)) {
    Add-PackageFile $runtime
}
# Qt dynamically loads these graphics components; they need not appear in PE imports.
foreach ($runtime in @('d3dcompiler_47.dll', 'dxcompiler.dll', 'dxil.dll')) {
    if (Test-Path -LiteralPath (Join-Path $sourceDirectory $runtime) -PathType Leaf) {
        Add-PackageFile $runtime
    }
}

while ($pendingBinaries.Count -gt 0) {
    $binary = $pendingBinaries.Dequeue()
    $binaryPath = Join-Path $sourceDirectory $binary
    $imports = @(& $dumpbin /nologo /dependents $binaryPath)
    if ($LASTEXITCODE -ne 0) { throw "Cannot enumerate runtime imports: $binaryPath" }
    foreach ($line in $imports) {
        if ($line -notmatch '^\s+([^\s]+\.dll)\s*$') { continue }
        $dependency = $Matches[1]
        if ($dependency -match '^(api-ms-win|ext-ms-win)-') { continue }
        $relativeCandidates = @($dependency)
        $binaryDirectory = Split-Path -Parent $binary
        if (-not [string]::IsNullOrEmpty($binaryDirectory)) {
            $relativeCandidates = @((Join-Path $binaryDirectory $dependency), $dependency)
        }
        $localDependency = @($relativeCandidates | Where-Object {
            Test-Path -LiteralPath (Join-Path $sourceDirectory $_) -PathType Leaf
        } | Select-Object -First 1)
        if ($localDependency.Count -gt 0) {
            Add-PackageFile $localDependency[0]
        } elseif (-not (Test-Path -LiteralPath (Join-Path $env:WINDIR "System32/$dependency") -PathType Leaf)) {
            throw "Missing runtime dependency: $binary -> $dependency"
        }
    }
}
# Distribute Qt's deployed installer for clean systems without the MSVC runtime.
Add-PackageFile 'vc_redist.x64.exe'

$resourceTypes = @{
    'ibl' = @('.ktx', '.txt', '.md')
    'icons' = @('.png')
    'licenses' = @('.txt', '.md')
    'locales' = @('.lang')
    'materials' = @('.filamat', '.qmat', '.png')
    'textures' = @('.png')
}
foreach ($directory in $resourceTypes.Keys) {
    $resourceDirectory = Join-Path $sourceDirectory $directory
    if (-not (Test-Path -LiteralPath $resourceDirectory -PathType Container)) {
        throw "Required public runtime resources are missing: $resourceDirectory"
    }
    foreach ($resource in @(Get-ChildItem -LiteralPath $resourceDirectory -Recurse -File)) {
        if ($resource.Extension.ToLowerInvariant() -in $resourceTypes[$directory]) {
            Add-PackageFile $resource.FullName.Substring($sourceDirectory.Length + 1)
        }
    }
}

# Use current repository notices, never stale files from a previous dependency
# deployment. Test, profiling and disabled crash-reporting packages are absent.
$noticeDirectory = Join-Path $repositoryRoot 'thirdparty/licenses'
foreach ($notice in @(Get-ChildItem -LiteralPath $noticeDirectory -File -Filter '*.txt')) {
    if ($notice.Name -match '^(gtest|tracy|sentry-native)-') { continue }
    if ($notice.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        throw "Dependency notice must be a regular file: $($notice.FullName)"
    }
    $relative = Join-Path 'licenses' $notice.Name
    [void]$files.Add($relative)
    $sourcePaths[$relative] = $notice.FullName
}

Write-Host "Public package: $destinationDirectory"
Write-Host "Allowlisted files: $($files.Count)"
if ($PlanOnly) {
    $files | ForEach-Object { Write-Output $_ }
    return
}

# The destination must be new. Never remove, merge into, or overwrite a package.
New-Item -ItemType Directory -Path $destinationDirectory -ErrorAction Stop | Out-Null
$manifestFiles = foreach ($relative in $files) {
    $source = $sourcePaths[$relative]
    $destinationFile = Join-Path $destinationDirectory $relative
    [void][IO.Directory]::CreateDirectory((Split-Path -Parent $destinationFile))
    [IO.File]::Copy($source, $destinationFile, $false)
    $sourceHash = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()
    $destinationHash = (Get-FileHash -LiteralPath $destinationFile -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($sourceHash -ne $destinationHash) { throw "Package copy hash mismatch: $relative" }
    [ordered]@{ path = $relative.Replace('\', '/'); sha256 = $destinationHash }
}
$unexpected = @(Get-ChildItem -LiteralPath $destinationDirectory -Recurse -File | Where-Object {
    -not $files.Contains($_.FullName.Substring($destinationDirectory.Length + 1))
})
if ($unexpected.Count -ne 0) { throw 'The package contains files outside its allowlist.' }
Assert-RuntimeDependencyClosure -ExecutablePath (Join-Path $destinationDirectory $publicExecutableName)
[ordered]@{
    productRelease = '0.1'
    buildVersion = (Get-Content -LiteralPath (Join-Path $repositoryRoot 'VERSION') -Raw).Trim()
    files = @($manifestFiles)
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $destinationDirectory 'package-manifest.json') -Encoding utf8
Write-Host "Verified public package: $destinationDirectory"
