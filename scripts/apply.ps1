
# Ensure the pet is running and our DLL is loaded into it.
param([string]$DllPath = (Join-Path (Split-Path $PSScriptRoot -Parent) 'build\\wuwapad.dll'))

$ErrorActionPreference = 'Continue'
$base = $PSScriptRoot
$log  = Join-Path $base 'apply.log'
function W($m) { "$(Get-Date -Format 'HH:mm:ss') $m" | Add-Content $log }

# keep the tuning file alive (it lives in %TEMP% so the injected DLL can read it)
$cfg = Join-Path $env:TEMP 'wuwapad_cfg.txt'
if (-not (Test-Path $cfg)) {
    Set-Content -Path $cfg -Value @('up=1','updelay=60','stick=1','proc=2','debounce=150') -Encoding ascii
    W 'cfg restored'
}

$pet = Get-Process WuwaTappo -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $pet) {
    if ($NoStart) { exit 0 }
    W 'pet not running, starting'
    $exe = 'C:\Program Files\WuwaTappo\WuwaTappo.exe'
    if (-not (Test-Path $exe)) { W "pet exe not found: $exe"; exit 1 }
    Start-Process -FilePath $exe -WorkingDirectory (Split-Path $exe)
    for ($i = 0; $i -lt 40; $i++) {
        Start-Sleep -Milliseconds 500
        $pet = Get-Process WuwaTappo -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($pet) { break }
    }
}
if (-not $pet) { W 'failed to start pet'; exit 1 }

$res  = & (Join-Path $base 'inject.ps1') -Elevated -DllPath $DllPath -ResultFile (Join-Path $base 'result_apply.txt')
$flat = (($res | Out-String) -split "\r?\n" | Where-Object { $_ -ne '' }) -join ' | '
W ("inject(" + $pet.Id + "): " + $flat)