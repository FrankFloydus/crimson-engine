[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QuaderRoot,
    [string]$BuildRoot = (Join-Path $PSScriptRoot '../build/quader-reader-current'),
    [string]$Destination = (Join-Path $PSScriptRoot '../third_party/quader_reader')
)
$ErrorActionPreference = 'Stop'
$nativeRoot = (Resolve-Path -LiteralPath $QuaderRoot).Path
$binaryRoot = (Resolve-Path -LiteralPath $BuildRoot).Path
$payloadRoot = [IO.Path]::GetFullPath($Destination)
$overrideRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../third_party/quader_reader/source')).Path
$sourceHeader = Join-Path $overrideRoot 'quader_bridge_reader.hpp'
$sourceImplementation = Join-Path $overrideRoot 'quader_bridge_reader.cpp'
$sourceReader = Join-Path $binaryRoot 'Release/quader_bridge_reader.dll'
$cacheFile = Join-Path $binaryRoot 'CMakeCache.txt'
foreach ($required in @($sourceHeader, $sourceImplementation, $sourceReader, $cacheFile)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Missing reader staging input: $required" }
}
$cache = @{}
foreach ($line in Get-Content -LiteralPath $cacheFile) {
    if ($line -match '^([^#/:][^:]*):[^=]*=(.*)$') { $cache[$Matches[1]] = $Matches[2] }
}
if (-not $cache.ContainsKey('CMAKE_HOME_DIRECTORY') -or
    -not $nativeRoot.Equals((Resolve-Path -LiteralPath $cache['CMAKE_HOME_DIRECTORY']).Path, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The reader build was configured from a different native source checkout.'
}
if ($cache['VCPKG_TARGET_TRIPLET'] -ne 'x64-windows') { throw 'Reader staging requires the x64-windows Release dependency closure.' }
if (-not $cache.ContainsKey('CC_QUADER_READER_OVERRIDE_ROOT') -or
    -not $overrideRoot.Equals((Resolve-Path -LiteralPath $cache['CC_QUADER_READER_OVERRIDE_ROOT']).Path, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The reader must be built with Crimson source overrides through Build-QuaderReader.ps1.'
}
$installedRoot = $cache['VCPKG_INSTALLED_DIR']
$releaseBin = Join-Path $installedRoot 'x64-windows/bin'
if (-not (Test-Path -LiteralPath $releaseBin -PathType Container)) { throw 'The configured reader dependency cache is missing.' }
$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($dumpbin) { $dumpbinPath = $dumpbin.Source } else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $vsRoot) { throw 'Visual Studio x64 tools are required to verify the reader closure.' }
    $msvc = Get-ChildItem -LiteralPath (Join-Path $vsRoot 'VC/Tools/MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $dumpbinPath = Join-Path $msvc.FullName 'bin/Hostx64/x64/dumpbin.exe'
}
$headerText = [IO.File]::ReadAllText($sourceHeader)
if ($headerText -notmatch '#define QUADER_BRIDGE_ABI_VERSION ([0-9]+)u') { throw 'Cannot identify the native reader ABI.' }
$abi = [int]$Matches[1]
$manifestText = [IO.File]::ReadAllText((Join-Path $nativeRoot 'src/io/project/project_manifest.hpp'))
if ($manifestText -notmatch 'kQuaderProjectManifestVersion = ([0-9]+)u') { throw 'Cannot identify the supported project manifest version.' }
$manifestVersion = [int]$Matches[1]
$previousFiles = @()
$previousProvenance = Join-Path $payloadRoot 'reader-provenance.json'
if (Test-Path -LiteralPath $previousProvenance -PathType Leaf) {
    $previousFiles = @((Get-Content -LiteralPath $previousProvenance -Raw | ConvertFrom-Json).files | ForEach-Object { $_.file })
}
New-Item -ItemType Directory -Force -Path $payloadRoot,(Join-Path $payloadRoot 'include') | Out-Null
Copy-Item -LiteralPath $sourceHeader -Destination (Join-Path $payloadRoot 'include/quader_bridge_reader.hpp') -Force
Copy-Item -LiteralPath $sourceReader -Destination (Join-Path $payloadRoot 'quader_bridge_reader.dll') -Force
$queue = [Collections.Generic.Queue[string]]::new()
$queue.Enqueue((Join-Path $payloadRoot 'quader_bridge_reader.dll'))
$files = @{}
while ($queue.Count -gt 0) {
    $file = $queue.Dequeue()
    $name = [IO.Path]::GetFileName($file)
    if ($files.ContainsKey($name)) { continue }
    $files[$name] = $file
    $imports = & $dumpbinPath /NOLOGO /DEPENDENTS $file
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect reader dependencies: $name" }
    foreach ($line in $imports) {
        if ($line -notmatch '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { continue }
        $dependency = $Matches[1]
        $source = Join-Path $releaseBin $dependency
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            $target = Join-Path $payloadRoot $dependency
            Copy-Item -LiteralPath $source -Destination $target -Force
            $queue.Enqueue($target)
        } elseif ($dependency -notmatch '^(api-ms-win-|ext-ms-win-)' -and -not (Test-Path -LiteralPath (Join-Path $env:WINDIR "System32/$dependency"))) {
            throw "Unresolved reader dependency: $dependency (from $name)"
        }
    }
}
foreach ($name in $previousFiles) {
    if ($files.ContainsKey($name)) { continue }
    if ([IO.Path]::GetFileName($name) -ne $name -or [IO.Path]::GetExtension($name) -ne '.dll') { throw 'Previous reader closure contains an invalid DLL filename.' }
    $obsolete = [IO.Path]::GetFullPath((Join-Path $payloadRoot $name))
    if ([IO.Path]::GetDirectoryName($obsolete) -ne $payloadRoot) { throw 'Previous reader closure escapes its payload directory.' }
    if (Test-Path -LiteralPath $obsolete -PathType Leaf) { Remove-Item -LiteralPath $obsolete }
}
$licenses = Join-Path $payloadRoot 'licenses'
$nativeLicenses = Join-Path $licenses 'quader'
New-Item -ItemType Directory -Force -Path $nativeLicenses | Out-Null
Copy-Item -LiteralPath (Join-Path $nativeRoot 'thirdparty/THIRD_PARTY_MANIFEST.md') -Destination $nativeLicenses -Force
Copy-Item -LiteralPath (Join-Path $nativeRoot 'thirdparty/licenses') -Destination $nativeLicenses -Recurse -Force
$owners = @{}
foreach ($list in Get-ChildItem -LiteralPath (Join-Path $installedRoot 'vcpkg/info') -Filter '*_x64-windows.list') {
    $parts = $list.BaseName -split '_',3
    foreach ($line in Get-Content -LiteralPath $list.FullName) {
        if ($line -match '^x64-windows/bin/([^/]+\.dll)$') { $owners[$Matches[1]] = @{ name = $parts[0]; version = $parts[1] } }
    }
}
$records = @()
foreach ($name in @($files.Keys | Sort-Object)) {
    $record = [ordered]@{ file = $name; sha256 = (Get-FileHash -LiteralPath $files[$name] -Algorithm SHA256).Hash.ToLowerInvariant() }
    if ($name -ne 'quader_bridge_reader.dll') {
        if (-not $owners.ContainsKey($name)) { throw "No dependency/license owner is recorded for $name" }
        $owner = $owners[$name]
        $record['package'] = $owner.name
        $record['version'] = $owner.version
        $packageLicenses = Join-Path $licenses $owner.name
        New-Item -ItemType Directory -Force -Path $packageLicenses | Out-Null
        Copy-Item -LiteralPath (Join-Path $installedRoot "x64-windows/share/$($owner.name)/copyright") -Destination $packageLicenses -Force
    }
    $records += $record
}
$commit = & git -C $nativeRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the native source commit.' }
$changes = @(& git -C $nativeRoot status --porcelain=v1)
if ($LASTEXITCODE -ne 0) { throw 'Cannot read the native source state.' }
$tracked = @(& git -C $nativeRoot ls-files -- src cmake CMakeLists.txt VERSION vcpkg.json vcpkg-configuration.json)
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate native reader source inputs.' }
$untracked = @(& git -C $nativeRoot ls-files --others --exclude-standard -- src cmake)
if ($LASTEXITCODE -ne 0) { throw 'Cannot enumerate untracked native source inputs.' }
$usedInputs = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
if ($untracked.Count -gt 0) {
    $dependencyLogs = @(Get-ChildItem -LiteralPath $binaryRoot -Recurse -Filter 'CL.read.1.tlog' -File)
    $cmakeDependencies = Join-Path $binaryRoot 'CMakeFiles/generate.stamp.depend'
    if (Test-Path -LiteralPath $cmakeDependencies) { $dependencyLogs += Get-Item -LiteralPath $cmakeDependencies }
    $nativePrefix = $nativeRoot.TrimEnd('\', '/') + '\'
    foreach ($log in $dependencyLogs) {
        foreach ($line in Get-Content -LiteralPath $log.FullName) {
            $inputPath = $line.Replace('/', '\')
            if ($inputPath.StartsWith($nativePrefix, [StringComparison]::OrdinalIgnoreCase)) {
                [void]$usedInputs.Add($inputPath.Substring($nativePrefix.Length).Replace('\', '/'))
            }
        }
    }
}
$usedUntracked = @($untracked | Where-Object { $usedInputs.Contains($_) })
$sourceHash = [Security.Cryptography.IncrementalHash]::CreateHash([Security.Cryptography.HashAlgorithmName]::SHA256)
try {
    foreach ($relative in @(@($tracked) + @($usedUntracked) | Sort-Object -Unique)) {
        $nameBytes = [Text.Encoding]::UTF8.GetBytes($relative)
        $content = [IO.File]::ReadAllBytes((Join-Path $nativeRoot $relative))
        $sourceHash.AppendData([BitConverter]::GetBytes([long]$nameBytes.Length))
        $sourceHash.AppendData($nameBytes)
        $sourceHash.AppendData([BitConverter]::GetBytes([long]$content.Length))
        $sourceHash.AppendData($content)
    }
    $sourceDigest = [BitConverter]::ToString($sourceHash.GetHashAndReset()).Replace('-', '').ToLowerInvariant()
} finally { $sourceHash.Dispose() }
$diffPath = Join-Path $binaryRoot 'native-source-changes.patch'
& git -C $nativeRoot diff HEAD --binary "--output=$diffPath"
if ($LASTEXITCODE -ne 0) { throw 'Cannot fingerprint the native worktree changes.' }
$provenance = [ordered]@{
    reader_abi = $abi
    supported_project_manifest_version = $manifestVersion
    configuration = 'Release'
    native_commit = $commit.Trim()
    native_worktree_modified = ($changes.Count -gt 0)
    native_worktree_status = $changes
    native_worktree_diff_sha256 = (Get-FileHash -LiteralPath $diffPath -Algorithm SHA256).Hash.ToLowerInvariant()
    native_source_inputs_sha256 = $sourceDigest
    native_untracked_build_inputs = $usedUntracked
    crimson_reader_source = 'third_party/quader_reader/source/quader_bridge_reader.cpp'
    crimson_reader_source_sha256 = (Get-FileHash -LiteralPath $sourceImplementation -Algorithm SHA256).Hash.ToLowerInvariant()
    header_sha256 = (Get-FileHash -LiteralPath $sourceHeader -Algorithm SHA256).Hash.ToLowerInvariant()
    files = $records
}
$provenance | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $payloadRoot 'reader-provenance.json') -Encoding utf8
Write-Output "Staged Quader reader ABI $abi, project manifest v$manifestVersion, source $($commit.Trim())."
