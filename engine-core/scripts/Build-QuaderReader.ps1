[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$QuaderRoot,
    [string]$BuildRoot = (Join-Path $PSScriptRoot '../build/quader-reader-current')
)
$ErrorActionPreference = 'Stop'
$nativeRoot = (Resolve-Path -LiteralPath $QuaderRoot).Path
$binaryRoot = [IO.Path]::GetFullPath($BuildRoot)
$cacheFile = Join-Path $binaryRoot 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cacheFile -PathType Leaf)) {
    throw 'Configure the isolated native reader build and dependency cache before using this incremental wrapper.'
}
$override = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot 'QuaderReaderOverride.cmake')).Path
& cmake -S $nativeRoot -B $binaryRoot "-DCMAKE_PROJECT_TOP_LEVEL_INCLUDES=$override"
if ($LASTEXITCODE -ne 0) { throw 'Native reader configuration failed.' }
& cmake --build $binaryRoot --config Release --target quader_bridge_reader --parallel
if ($LASTEXITCODE -ne 0) { throw 'Native reader build failed.' }
& (Join-Path $PSScriptRoot 'Stage-QuaderReader.ps1') -QuaderRoot $nativeRoot -BuildRoot $binaryRoot
