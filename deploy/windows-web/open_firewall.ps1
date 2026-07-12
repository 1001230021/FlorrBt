$ErrorActionPreference = "Stop"

. "$PSScriptRoot\web.env.ps1"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$isAdmin = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Host "Please run this script as Administrator." -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}

$ruleName = "FlorrBt Web $($env:WEB_PORT)"
$existing = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Firewall rule already exists: $ruleName"
} else {
    New-NetFirewallRule -DisplayName $ruleName -Direction Inbound -Protocol TCP -LocalPort $env:WEB_PORT -Action Allow | Out-Null
    Write-Host "Firewall opened for TCP port $($env:WEB_PORT)."
}

Read-Host "Press Enter to exit"
