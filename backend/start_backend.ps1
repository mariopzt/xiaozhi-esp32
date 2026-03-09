param(
    [int]$Port = 8787
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$python = Join-Path $root ".venv\Scripts\python.exe"
$stdout = Join-Path $root "backend.out.log"
$stderr = Join-Path $root "backend.err.log"

$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -like "*backend\.venv\Scripts\python.exe*app.main*"
}

if ($existing) {
    $existing | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
    Start-Sleep -Seconds 1
}

$proc = Start-Process `
    -FilePath $python `
    -ArgumentList "-m", "app.main" `
    -WorkingDirectory $root `
    -RedirectStandardOutput $stdout `
    -RedirectStandardError $stderr `
    -PassThru

Start-Sleep -Seconds 6

$resp = Invoke-WebRequest -UseBasicParsing "http://127.0.0.1:$Port/health" -TimeoutSec 10

Write-Output "Backend PID: $($proc.Id)"
Write-Output $resp.Content
