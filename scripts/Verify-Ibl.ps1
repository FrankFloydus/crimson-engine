param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectDirectory "build\$Configuration\bin\crimson-renderer.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build.cmd $Configuration must complete first." }
$runId = Get-Date -Format 'yyyyMMdd-HHmmss'
$testRoot = Join-Path $projectDirectory "build\verification\ibl-$runId"
$captureDirectory = Join-Path $projectDirectory "captures\ibl-$runId"
New-Item -ItemType Directory -Force -Path $testRoot,$captureDirectory | Out-Null
$layout = Join-Path $projectDirectory 'crimson_ui.ini'
if (Test-Path -LiteralPath $layout) { Copy-Item -LiteralPath $layout -Destination $testRoot }
$preserved = @{}
foreach ($relative in @('.crimson\material.json','.crimson\environment.json','assets\material.json','assets\environment.json','crimson_ui.ini')) {
    $file = Join-Path $projectDirectory $relative
    if (Test-Path -LiteralPath $file) { $preserved[$file] = (Get-FileHash -LiteralPath $file).Hash }
}
$scenarios = @('ibl-normal','ibl-reopen','ibl-dielectric','ibl-neutral','ibl-zero-normal','ibl-metal','ibl-rough','ibl-extreme','ibl-zero-exposure','unlit')
foreach ($scenario in $scenarios) {
    $capture = Join-Path $captureDirectory "$scenario.png"
    $arguments = @('--hidden','--frames','24','--project-root',('"' + $testRoot + '"'),'--scenario',$scenario,'--capture',('"' + $capture + '"'))
    if ($scenario -eq 'ibl-normal') { $arguments += '--ibl-smoke' }
    $process = Start-Process -FilePath $executable -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru -ArgumentList $arguments
    if (-not $process.WaitForExit(60000)) { Stop-Process -Id $process.Id; throw "$scenario timed out. See $testRoot\renderer.log." }
    $process.Refresh()
    if ($process.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $capture)) { throw "$scenario failed. See $testRoot\renderer.log." }
    Copy-Item -LiteralPath (Join-Path $testRoot 'renderer.log') -Destination (Join-Path $captureDirectory "$scenario.log")
    Write-Host "PASS: $scenario"
}
foreach ($file in $preserved.Keys) {
    if ((Get-FileHash -LiteralPath $file).Hash -ne $preserved[$file]) { throw "User project file changed: $file" }
}
Write-Host "PASS: saved project material/environment/layout preserved."
Write-Host "Captures: $captureDirectory"
Write-Host "Isolated project: $testRoot"
