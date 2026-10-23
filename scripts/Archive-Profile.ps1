[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Capture,
    [Parameter(Mandatory = $true)][string]$DestinationDirectory,
    [string]$MetadataPath,
    [string]$AnalysisDirectory,
    [string]$PythonExecutable = 'python'
)
$ErrorActionPreference = 'Stop'
$arguments = @((Join-Path $PSScriptRoot 'profiling/analyze_capture.py'),(Resolve-Path -LiteralPath $Capture).Path,
    '--archive-only','--archive',[IO.Path]::GetFullPath($DestinationDirectory))
if ($MetadataPath) { $arguments += @('--metadata',(Resolve-Path -LiteralPath $MetadataPath).Path) }
if ($AnalysisDirectory) { $arguments += @('--analysis-directory',(Resolve-Path -LiteralPath $AnalysisDirectory).Path) }
& $PythonExecutable @arguments
if ($LASTEXITCODE -ne 0) { throw 'Profile evidence archive failed.' }
