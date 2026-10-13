param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectDirectory "build\$Configuration\bin\crimson-renderer.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build.cmd $Configuration must complete first." }
$captureDirectory = Join-Path $projectDirectory 'captures'
New-Item -ItemType Directory -Force -Path $captureDirectory | Out-Null
$capture = Join-Path $captureDirectory 'editor-check.png'
$process = Start-Process -FilePath $executable -WorkingDirectory $projectDirectory -WindowStyle Hidden -PassThru -ArgumentList @('--hidden','--frames','30','--capture',('"' + $capture + '"'))
if (-not $process.WaitForExit(20000)) { Stop-Process -Id $process.Id; throw 'Editor smoke check timed out.' }
$process.Refresh()
if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)) { throw "Editor check failed. See $projectDirectory\renderer.log." }
Write-Host "PASS: native editor capture saved to $capture"
