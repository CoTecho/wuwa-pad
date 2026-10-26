
# Elevated command agent: watches cmd.txt, runs it, writes out.txt.
# Started once (UAC), then reused by everything else.
$ErrorActionPreference = 'Continue'
$base = $PSScriptRoot
$cmdFile  = Join-Path $base 'cmd.txt'
$outFile  = Join-Path $base 'out.txt'
$aliveFile = Join-Path $base 'agent.alive'

"agent started pid=$PID at $(Get-Date -Format 'HH:mm:ss')" | Set-Content (Join-Path $base 'agent.log')
while ($true) {
    Set-Content -Path $aliveFile -Value ((Get-Date).ToString('o'))
    if (Test-Path $cmdFile) {
        $code = Get-Content $cmdFile -Raw -ErrorAction SilentlyContinue
        Remove-Item $cmdFile -Force -ErrorAction SilentlyContinue
        if ($code -match '^\s*exit\s*$') { 'stopped' | Set-Content $outFile; break }
        $res = ''
        try { $res = & ([scriptblock]::Create($code)) 2>&1 | Out-String } catch { $res = "ERROR: $($_.Exception.Message)" }
        $res | Set-Content -Path $outFile
        ("[{0}] cmd ok" -f (Get-Date -Format 'HH:mm:ss')) | Add-Content (Join-Path $base 'agent.log')
    }
    Start-Sleep -Milliseconds 250
}
