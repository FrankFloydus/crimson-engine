param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectDirectory "build\$Configuration\bin\crimson-renderer.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build.cmd $Configuration must complete first." }
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$testRoot = Join-Path $projectDirectory "build\verification\models-$runId"
$captureDirectory = Join-Path $projectDirectory "captures\models-$runId"
New-Item -ItemType Directory -Force -Path $testRoot,$captureDirectory | Out-Null
$preserved = @{}
foreach ($relative in @('assets\material.json','assets\environment.json','assets\preview.json','crimson_ui.ini')) {
    $file = Join-Path $projectDirectory $relative
    $preserved[$file] = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { $null }
}
foreach ($scenario in @('model-import','model-reopen','model-sphere','default')) {
    $capture = Join-Path $captureDirectory "$scenario.png"
    $arguments = @('--hidden','--frames','32','--project-root',('"' + $testRoot + '"'),'--capture',('"' + $capture + '"'))
    if ($scenario -eq 'model-import') { $arguments += '--model-smoke' }
    else { $arguments += @('--scenario',$scenario) }
    $process = Start-Process -FilePath $executable -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru -ArgumentList $arguments
    if (-not $process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw "$scenario timed out. See $testRoot\renderer.log." }
    $process.Refresh()
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)) { throw "$scenario failed. See $testRoot\renderer.log." }
    Copy-Item -LiteralPath (Join-Path $testRoot 'renderer.log') -Destination (Join-Path $captureDirectory "$scenario.log")
    Write-Host "PASS: $scenario"
}
foreach ($file in $preserved.Keys) {
    $actual = if (Test-Path -LiteralPath $file) { (Get-FileHash -LiteralPath $file).Hash } else { $null }
    if ($actual -ne $preserved[$file]) { throw "Authored project file changed: $file" }
}
Write-Host 'PASS: authored material/environment/preview/layout preserved.'
Write-Host "Captures: $captureDirectory"
Write-Host "Isolated project: $testRoot"
