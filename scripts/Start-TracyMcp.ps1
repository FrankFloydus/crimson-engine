[CmdletBinding()]
param(
    [ValidateSet('Release','Debug')][string]$Configuration = 'Release',
    [ValidateSet('streamable-http','sse')][string]$Transport = 'streamable-http',
    [ValidateRange(1024,65535)][int]$Port = 47380,
    [string]$CapturesDirectory,
    [switch]$Background,
    [switch]$PrintConfiguration
)
$ErrorActionPreference = 'Stop'
$projectDirectory = Split-Path -Parent $PSScriptRoot
$tools = Join-Path $projectDirectory "build/$Configuration/bin/profiling"
if ($PrintConfiguration) {
    $endpoint = if ($Transport -eq 'sse') { 'sse' } else { 'mcp' }
    Write-Output "[mcp_servers.crimson_tracy]`nurl = `"http://127.0.0.1:$Port/$endpoint`""
    return
}
$manifestPath = Join-Path $tools 'tracy-tools.json'
if (-not (Test-Path -LiteralPath $manifestPath)) { throw 'Run scripts/Prepare-Tracy.ps1 -Build first.' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($manifest.tracy_version -ne '0.14.1' -or $manifest.mcp_version -ne '2.3.0') { throw 'The paired Tracy/MCP tool manifest is incompatible.' }
foreach ($entry in $manifest.files.PSObject.Properties) {
    $path = Join-Path $tools $entry.Name
    if (-not (Test-Path -LiteralPath $path) -or (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $entry.Value) {
        throw "Paired profiling tool hash mismatch: $path"
    }
}
function Get-PairedServer([int]$ServerPort) {
    $listeners = @(Get-NetTCPConnection -State Listen -LocalPort $ServerPort -ErrorAction SilentlyContinue |
        Where-Object { $_.LocalAddress -in @('127.0.0.1','0.0.0.0','::','::1') })
    if (-not $listeners.Count) { return $null }
    $owners = @($listeners.OwningProcess | Select-Object -Unique)
    if ($owners.Count -ne 1) { throw "Port $ServerPort has multiple listeners; no process was changed." }
    $owner = Get-CimInstance Win32_Process -Filter "ProcessId=$($owners[0])" -ErrorAction Stop
    $expectedPythons = @([IO.Path]::GetFullPath($manifest.python))
    if ($manifest.base_python) { $expectedPythons += [IO.Path]::GetFullPath($manifest.base_python) }
    $expectedScript = [IO.Path]::GetFullPath($manifest.server)
    if (-not $owner -or -not $owner.ExecutablePath -or
        [IO.Path]::GetFullPath($owner.ExecutablePath) -notin $expectedPythons -or
        $owner.CommandLine -notmatch ('(?i)(?:^|[\s"''])' + [regex]::Escape($expectedScript) + '(?:[\s"'']|$)')) {
        throw "Port $ServerPort is owned by a different process (PID $($owners[0])); no process was changed."
    }
    return $owner
}
function Test-ServerEndpoint([string]$Url) {
    try {
        $request = [Net.HttpWebRequest]::Create($Url)
        $request.Timeout = 2000
        $response = $request.GetResponse()
        $response.Close()
        return $true
    } catch {
        # MCP GET without its session/Accept headers may return an HTTP error.
        # An HTTP response still proves this verified process is serving.
        $failure = $_.Exception
        while ($failure) {
            if ($null -ne $failure.Response) { $failure.Response.Close(); return $true }
            $failure = $failure.InnerException
        }
        return $false
    }
}
$endpoint = if ($Transport -eq 'sse') { 'sse' } else { 'mcp' }
$serverUrl = "http://127.0.0.1:$Port/$endpoint"
$existing = Get-PairedServer $Port
if ($existing) {
    if (-not (Test-ServerEndpoint $serverUrl)) { throw "Paired Tracy MCP PID $($existing.ProcessId) owns port $Port but is not responding; no process was changed." }
    [pscustomobject]@{ ProcessId = $existing.ProcessId; Url = $serverUrl; Transport = $Transport; Reused = $true }
    return
}
$pidFile = Join-Path (Split-Path -Parent $manifest.server) 'tracy_mcp.pid'
if (Test-Path -LiteralPath $pidFile -PathType Leaf) {
    $recordedProcessId = 0
    if ([int]::TryParse((Get-Content -LiteralPath $pidFile -Raw).Trim(),[ref]$recordedProcessId)) {
        $otherListeners = @(Get-NetTCPConnection -State Listen -OwningProcess $recordedProcessId -ErrorAction SilentlyContinue)
        if ($otherListeners.Count) {
            foreach ($otherPort in @($otherListeners.LocalPort | Select-Object -Unique)) {
                $other = Get-PairedServer $otherPort
                if ($other) { throw "This paired Tracy MCP is already listening on port $otherPort (PID $recordedProcessId). Use -Port $otherPort; no process was changed." }
            }
        }
    }
}
if (-not $CapturesDirectory) { $CapturesDirectory = Join-Path $projectDirectory 'captures/profiling' }
$CapturesDirectory = [IO.Path]::GetFullPath($CapturesDirectory)
New-Item -ItemType Directory -Force -Path $CapturesDirectory | Out-Null
$oldPythonPath = $env:PYTHONPATH
$oldCaptureDirectory = $env:TRACY_CAPTURES_DIR
$oldPort = $env:TRACY_MCP_PORT
$oldTransport = $env:TRACY_MCP_TRANSPORT
try {
    $env:PYTHONPATH = if ($oldPythonPath) { $tools + [IO.Path]::PathSeparator + $oldPythonPath } else { $tools }
    $env:TRACY_CAPTURES_DIR = $CapturesDirectory
    $env:TRACY_MCP_PORT = [string]$Port
    $env:TRACY_MCP_TRANSPORT = $Transport
    if ($Background) {
        $stdout = Join-Path $tools 'tracy-mcp.stdout.log'
        $stderr = Join-Path $tools 'tracy-mcp.stderr.log'
        $process = Start-Process -FilePath $manifest.python -ArgumentList @('"' + $manifest.server + '"') `
            -WorkingDirectory $projectDirectory -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
        $deadline = [DateTime]::UtcNow.AddSeconds(15)
        do {
            $listener = Get-PairedServer $Port
            if ($listener -and (Test-ServerEndpoint $serverUrl)) {
                # Windows venv Python may use a redirector process. The actual
                # server's executable and full paired script were verified.
                [pscustomobject]@{ ProcessId = $listener.ProcessId; Url = $serverUrl; Transport = $Transport; Reused = $false; StandardError = $stderr; CapturesDirectory = $CapturesDirectory }
                return
            }
            if ($process.HasExited) { throw "Official Tracy MCP exited with code $($process.ExitCode). See $stderr" }
            Start-Sleep -Milliseconds 100
        } while ([DateTime]::UtcNow -lt $deadline)
        throw "Official Tracy MCP did not become ready at $serverUrl (PID $($process.Id)). See $stderr"
    } else {
        & $manifest.python $manifest.server
        if ($LASTEXITCODE -ne 0) { throw "Official Tracy MCP exited with code $LASTEXITCODE." }
    }
} finally {
    $env:PYTHONPATH = $oldPythonPath
    $env:TRACY_CAPTURES_DIR = $oldCaptureDirectory
    $env:TRACY_MCP_PORT = $oldPort
    $env:TRACY_MCP_TRANSPORT = $oldTransport
}
