
param([string]$TaskName = '\WuwaTappo\PadBridge')
$isAdmin = ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Start-Process -FilePath 'powershell.exe' -Verb RunAs -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-TaskName',$TaskName)
    return
}
schtasks /delete /tn $TaskName /f
$base = $PSScriptRoot
Remove-Item (Join-Path $base 'watch.run') -Force -ErrorAction SilentlyContinue
Set-Content -Path (Join-Path $base 'cmd.txt') -Value 'exit' -Encoding utf8 -ErrorAction SilentlyContinue
"removed task: $TaskName"
