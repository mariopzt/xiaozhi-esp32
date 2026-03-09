$ErrorActionPreference = "Stop"

$existing = Get-CimInstance Win32_Process | Where-Object {
    $_.CommandLine -like "*backend\.venv\Scripts\python.exe*app.main*"
}

if (-not $existing) {
    Write-Output "Backend is not running."
    exit 0
}

$existing | ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
Write-Output "Backend stopped."
