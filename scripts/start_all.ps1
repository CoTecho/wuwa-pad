
# Entry point for the logon scheduled task (runs elevated, no UAC).
$ErrorActionPreference = 'Continue'
$base = $PSScriptRoot
$log  = Join-Path $base 'autostart.log'
function W($m) { "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $m" | Add-Content $log }
W "start_all begin (elevated=$(([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)))"

# 1) elevated command agent (kept for manual inject / debugging)
$agentPath = Join-Path $base 'agent.ps1'
$agentRunning = $false
foreach ($p in Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue) {
    if ($p.CommandLine -and $p.CommandLine.Contains($agentPath)) { $agentRunning = $true; break }
}
$alive = Join-Path $base 'agent.alive'
if (-not $agentRunning -and (Test-Path $alive)) {
    $agentRunning = ((Get-Date) - (Get-Item $alive).LastWriteTime).TotalSeconds -lt 5
}
if (-not $agentRunning) {
    Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $base 'agent.ps1'))
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 250
        if ((Test-Path $alive) -and ((Get-Date) - (Get-Item $alive).LastWriteTime).TotalSeconds -lt 15) { break }
    }
    W 'agent started'
} else { W 'agent already running' }

# 2) watchdog
if (-not (Test-Path (Join-Path $base 'watch.run'))) { New-Item -ItemType File -Path (Join-Path $base 'watch.run') -Force | Out-Null }
$watchRunning = $false
$watchPath = Join-Path $base 'watch.ps1'
foreach ($p in Get-CimInstance Win32_Process -Filter "Name='powershell.exe'" -ErrorAction SilentlyContinue) {
    if ($p.CommandLine -and $p.CommandLine.Contains($watchPath)) { $watchRunning = $true; break }
}
if (-not $watchRunning) {
    Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $base 'watch.ps1'))
    W 'watchdog started'
} else { W 'watchdog already running' }

# 3) ensure the pet is up and injected right now
Start-Sleep -Seconds 2
& (Join-Path $base 'apply.ps1') | Out-Null
W 'start_all done'