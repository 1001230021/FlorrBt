$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $RepoRoot

. "$PSScriptRoot\web.env.ps1"

$Node = Get-Command node -ErrorAction SilentlyContinue
if (-not $Node) {
    Write-Host "Node.js is not installed or not in PATH." -ForegroundColor Red
    Write-Host "Install Node.js LTS, then run this script again."
    Start-Process "https://nodejs.org/en/download"
    Read-Host "Press Enter to exit"
    exit 1
}

Write-Host "Starting FlorrBt web client..."
Write-Host "URL: http://$($env:WEB_HOST):$($env:WEB_PORT)"
Write-Host "Bridge: ws://$($env:WEB_HOST):$($env:WEB_PORT)/ws -> $($env:GAME_HOST):$($env:GAME_PORT)"
Write-Host ""

node tools\web_client_server.js
