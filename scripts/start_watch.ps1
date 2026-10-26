
# Ask the elevated agent to launch the watchdog as its own process.
$base = $PSScriptRoot
$out  = Join-Path $base 'out.txt'
if (Test-Path $out) { Remove-Item $out -Force }
$watchPath = Join-Path $base 'watch.ps1'
$code = "Start-Process -FilePath 'powershell.exe' -WindowStyle Hidden -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File','$watchPath')"
Set-Content -Path (Join-Path $base 'cmd.txt') -Value $code -Encoding utf8
for ($i = 0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 300; if (Test-Path $out) { break } }
