
$base = $PSScriptRoot
$aliveFile = Join-Path $base 'agent.alive'
$fresh = $false
if (Test-Path $aliveFile) {
    $age = (Get-Date) - (Get-Item $aliveFile).LastWriteTime
    if ($age.TotalSeconds -lt 10) { $fresh = $true }
}
if (-not $fresh) {
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -WindowStyle Hidden -ArgumentList @(
        '-NoProfile','-ExecutionPolicy','Bypass','-File',(Join-Path $base 'agent.ps1'))
    for ($i = 0; $i -lt 60; $i++) {
        Start-Sleep -Milliseconds 500
        if ((Test-Path $aliveFile) -and ((Get-Date) - (Get-Item $aliveFile).LastWriteTime).TotalSeconds -lt 10) { $fresh = $true; break }
    }
}
if ($fresh) { "agent ready" } else { "agent NOT running (UAC declined?)" }
