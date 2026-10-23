[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$PythonExecutable = 'python',
    [switch]$Offline,
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$cacheDirectory = Join-Path $projectDirectory 'build/profiling'
$toolsDirectory = Join-Path $projectDirectory "build/$Configuration/bin/profiling"
$installedManifest = Join-Path $toolsDirectory 'tracy-tools.json'
if ($Build -and (Test-Path -LiteralPath $installedManifest -PathType Leaf)) {
    try {
        $installed = Get-Content -LiteralPath $installedManifest -Raw | ConvertFrom-Json
        $sourceLock = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'profiling/sources.lock.json') -Raw | ConvertFrom-Json
        $valid = $installed.tracy_version -eq '0.14.1' -and $installed.source_sha512 -eq $sourceLock.sources[0].sha512 -and
            $installed.mcp_version -eq $sourceLock.mcp -and $installed.pybind11_version -eq $sourceLock.pybind11 -and
            (Test-Path -LiteralPath $installed.python -PathType Leaf) -and $null -ne $installed.preparation_inputs
        if ($valid) {
            foreach ($entry in $installed.preparation_inputs.PSObject.Properties) {
                $path = Join-Path $projectDirectory $entry.Name
                if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) { $valid = $false; break }
            }
        }
        if ($valid) {
            foreach ($entry in $installed.files.PSObject.Properties) {
                $path = Join-Path $toolsDirectory $entry.Name
                if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) { $valid = $false; break }
            }
        }
        if ($valid) { Write-Host "Paired Tracy 0.14.1 tools ready (warm): $toolsDirectory"; return }
    } catch { }
}
$python = (Get-Command $PythonExecutable -ErrorAction Stop).Source
$prepare = Join-Path $PSScriptRoot 'profiling/prepare.py'
$arguments = @($prepare,'prepare','--cache',$cacheDirectory)
if ($Offline) { $arguments += '--offline' }
& $python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Pinned Tracy source/wheel preparation failed.' }
if (-not $Build) { Write-Host 'Pinned sources and wheels are ready. Use -Build to compile the isolated profiling tools.'; return }
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $wrapper = Join-Path $PSScriptRoot 'profiling/Build-TracyTools.cmd'
    & $wrapper $Configuration $python
    if ($LASTEXITCODE -ne 0) { throw 'Isolated Tracy tools build failed.' }
    return
}
$plan = Get-Content -LiteralPath (Join-Path $cacheDirectory 'prepared.json') -Raw | ConvertFrom-Json
$venv = Join-Path $cacheDirectory 'venv'
$toolPython = Join-Path $venv 'Scripts/python.exe'
if (-not (Test-Path -LiteralPath $toolPython -PathType Leaf)) {
    & $python -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Cannot create the isolated profiling Python environment.' }
}
& $toolPython -m pip install --no-index --find-links $plan.wheelhouse --only-binary=:all: --require-hashes -r (Join-Path $PSScriptRoot 'profiling/python-requirements.lock')
if ($LASTEXITCODE -ne 0) { throw 'Offline locked profiling Python installation failed.' }
$pybindDirectory = Join-Path $venv 'Lib/site-packages/pybind11/share/cmake/pybind11'
$binaryDirectory = Join-Path $cacheDirectory 'native'
$configure = @('-S',(Join-Path $PSScriptRoot 'profiling'),'-B',$binaryDirectory,'-G','Ninja',
    '-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_INSTALL_PREFIX=$toolsDirectory",'-DCMAKE_POLICY_VERSION_MINIMUM=3.10',
    "-DCRIMSON_TRACY_SOURCE=$($plan.sources.tracy)","-DCRIMSON_CAPSTONE_SOURCE=$($plan.sources.capstone)",
    "-DCRIMSON_ZSTD_SOURCE=$($plan.sources.zstd)","-DCRIMSON_PPQSORT_SOURCE=$($plan.sources.ppqsort)",
    "-DPython_EXECUTABLE=$toolPython","-Dpybind11_DIR=$pybindDirectory",'-DFETCHCONTENT_FULLY_DISCONNECTED=ON')
& cmake @configure
if ($LASTEXITCODE -ne 0) { throw 'Isolated offline Tracy tools configuration failed.' }
& cmake --build $binaryDirectory --config Release --parallel 8
if ($LASTEXITCODE -ne 0) { throw 'Isolated Tracy tools compilation failed.' }
& cmake --install $binaryDirectory --config Release
if ($LASTEXITCODE -ne 0) { throw 'Tracy tools staging failed.' }
& $toolPython $prepare finish --cache $cacheDirectory --tools $toolsDirectory --configuration $Configuration --python $toolPython
if ($LASTEXITCODE -ne 0) { throw 'Tracy tools manifest validation failed.' }
Write-Host "Paired Tracy 0.14.1 tools ready: $toolsDirectory"
