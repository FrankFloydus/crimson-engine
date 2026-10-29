[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Capture,
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [string]$OutputDirectory,
    [string]$MetadataPath,
    [string]$McpUrl,
    [ValidateRange(1,10000)][int]$Top = 40,
    [ValidateRange(0,1000000)][int]$SkipFrames = 0,
    [switch]$ExportEvents,
    [string]$ArchiveDirectory
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $projectDirectory "build/$Configuration/bin/profiling"
$manifestFile = Join-Path $tools 'tracy-tools.json'
if (-not (Test-Path -LiteralPath $manifestFile)) { throw 'Run Prepare-Tracy.ps1 -Build first.' }
$manifest = Get-Content -LiteralPath $manifestFile -Raw | ConvertFrom-Json
$capturePath = (Resolve-Path -LiteralPath $Capture).Path
$arguments = @((Join-Path $PSScriptRoot 'profiling/analyze_capture.py'),$capturePath,'--tools',$tools,'--top',[string]$Top,'--skip-frames',[string]$SkipFrames)
if ($OutputDirectory) { $arguments += @('--output',[IO.Path]::GetFullPath($OutputDirectory)) }
if ($MetadataPath) { $arguments += @('--metadata',(Resolve-Path -LiteralPath $MetadataPath).Path) }
if ($McpUrl) { $arguments += @('--mcp-url',$McpUrl) }
if ($ExportEvents) { $arguments += '--export-events' }
if ($ArchiveDirectory) { $arguments += @('--archive',[IO.Path]::GetFullPath($ArchiveDirectory)) }
& $manifest.python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Tracy capture analysis failed.' }
