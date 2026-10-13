param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$executable = Join-Path $projectDirectory "build\$Configuration\bin\crimson-renderer.exe"
if (-not (Test-Path -LiteralPath $executable)) { throw "Build.cmd $Configuration must complete first." }
$shortcutPath = Join-Path ([Environment]::GetFolderPath('Desktop')) 'Crimson Engine.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
if ((Test-Path -LiteralPath $shortcutPath) -and $shortcut.TargetPath -and
    $shortcut.TargetPath -ne $executable -and
    $shortcut.TargetPath -ne (Join-Path $projectDirectory 'build\Debug\bin\crimson-renderer.exe') -and
    $shortcut.TargetPath -ne (Join-Path $projectDirectory 'build\Release\bin\crimson-renderer.exe')) {
    throw "A different application already uses $shortcutPath; it has not been changed."
}
$shortcut.TargetPath = $executable
$shortcut.WorkingDirectory = $projectDirectory
$shortcut.Arguments = ''
$shortcut.IconLocation = "$executable,0"
$shortcut.Description = 'Crimson Engine renderer and editor'
$shortcut.Save()
& (Join-Path $PSScriptRoot 'Refresh-AppIcon.ps1') -ExecutablePath $executable
Write-Host "Created $shortcutPath -> $executable"
