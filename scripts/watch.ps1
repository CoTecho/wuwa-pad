
# Watchdog: re-inject whenever the pet restarts or updates.
$base = $PSScriptRoot
$flag = Join-Path $base 'watch.run'
$log  = Join-Path $base 'watch.log'
function W($m) { "$(Get-Date -Format 'HH:mm:ss') $m" | Add-Content $log }
W "watch started pid=$PID"
while (Test-Path $flag) {
    try { & (Join-Path $base 'apply.ps1') | Out-Null } catch { W ("apply error: " + $_.Exception.Message) }
    Start-Sleep -Seconds 5
}
W 'watch stopped'
