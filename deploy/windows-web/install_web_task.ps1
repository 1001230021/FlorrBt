$ErrorActionPreference = "Stop"

$Root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$TaskName = "FlorrBtWeb"
$Node = Get-Command node -ErrorAction SilentlyContinue
if (-not $Node) {
    Write-Host "Node.js is not installed or not in PATH." -ForegroundColor Red
    Start-Process "https://nodejs.org/en/download"
    Read-Host "Press Enter to exit"
    exit 1
}

$StartScript = Join-Path $PSScriptRoot "start_web.ps1"
$Action = New-ScheduledTaskAction `
    -Execute "powershell.exe" `
    -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$StartScript`"" `
    -WorkingDirectory $Root
$Trigger = New-ScheduledTaskTrigger -AtLogOn
$Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)

Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName

Write-Host "Installed and started scheduled task: $TaskName"
Write-Host "Use Task Scheduler or this command to stop it:"
Write-Host "Stop-ScheduledTask -TaskName $TaskName"
Read-Host "Press Enter to exit"
