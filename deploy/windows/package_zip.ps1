param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputZip = "",
    [switch]$IncludePdb
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ArtifactsDir = Join-Path $RepoRoot "artifacts"
$AppVersion = "1.0.1"
$PackageName = "FlorrBtHotReload-v$AppVersion"
$StageRoot = Join-Path $ArtifactsDir $PackageName
if (-not $OutputZip) {
    $OutputZip = Join-Path $ArtifactsDir "$PackageName.zip"
}

function Copy-Dir([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) {
        throw "Missing directory: $Source"
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Recurse -Force
}

function Copy-File([string]$Source, [string]$Destination) {
    if (-not (Test-Path $Source)) {
        throw "Missing file: $Source"
    }
    New-Item -ItemType Directory -Force -Path (Split-Path $Destination -Parent) | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination -Force
}

function Find-NodeRuntime {
    $candidates = @(
        (Get-Command node -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        "C:\Program Files\nodejs\node.exe",
        "C:\Program Files (x86)\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\nodejs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe"
    )

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (Test-Path $candidate) { return $candidate }
    }

    throw "node.exe was not found; a self-contained START_ALL package cannot be created."
}

function Write-TextFile([string]$Path, [string]$Text) {
    New-Item -ItemType Directory -Force -Path (Split-Path $Path -Parent) | Out-Null
    Set-Content -Path $Path -Value $Text -Encoding ASCII
}

New-Item -ItemType Directory -Force -Path $ArtifactsDir | Out-Null
if (Test-Path $StageRoot) {
    Remove-Item -LiteralPath $StageRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null

$BuildDir = Join-Path $RepoRoot "x64\$Configuration"
$RuntimeDir = Join-Path $StageRoot "x64\$Configuration"
$ServerExe = Join-Path $BuildDir "FlorrBt.Server.exe"
$LauncherExe = Join-Path $BuildDir "FlorrBt.Launcher.exe"
Copy-File $ServerExe (Join-Path $RuntimeDir "FlorrBt.Server.exe")
Copy-File $LauncherExe (Join-Path $RuntimeDir "FlorrBt.Launcher.exe")
$NodeExe = Find-NodeRuntime
Copy-File $NodeExe (Join-Path $StageRoot "runtime\node.exe")

Get-ChildItem $BuildDir -Filter "sfml*.dll" -File -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-File $_.FullName (Join-Path $RuntimeDir $_.Name)
}

if ($IncludePdb) {
    Get-ChildItem $BuildDir -Filter "*.pdb" -File -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-File $_.FullName (Join-Path $RuntimeDir $_.Name)
    }
}

Copy-Dir (Join-Path $RepoRoot "web") (Join-Path $StageRoot "web")
Copy-Dir (Join-Path $RepoRoot "data") (Join-Path $StageRoot "data")
Copy-Dir (Join-Path $RepoRoot "tools") (Join-Path $StageRoot "tools")
Copy-Dir (Join-Path $RepoRoot "deploy\windows") (Join-Path $StageRoot "deploy\windows")
Copy-Dir (Join-Path $RepoRoot "deploy\windows-web") (Join-Path $StageRoot "deploy\windows-web")
$HotReloadData = Join-Path $StageRoot "data\hot_reload"
if (Test-Path $HotReloadData) {
    Remove-Item -LiteralPath $HotReloadData -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $HotReloadData | Out-Null
Get-ChildItem -LiteralPath $StageRoot -Directory -Recurse -Force -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq "__pycache__" } |
    Remove-Item -Recurse -Force
Get-ChildItem -LiteralPath $StageRoot -File -Recurse -Force -Filter "*.pyc" -ErrorAction SilentlyContinue |
    Remove-Item -Force
Get-ChildItem -LiteralPath $StageRoot -Filter "*.log" -File -Recurse -ErrorAction SilentlyContinue | ForEach-Object {
    Remove-Item -LiteralPath $_.FullName -Force
}

$StartAll = @'
@echo off
cd /d "%~dp0"
call deploy\windows\deploy.cmd -Mode start
'@
Write-TextFile (Join-Path $StageRoot "START_ALL.cmd") $StartAll

$StartWeb = @'
@echo off
cd /d "%~dp0"
call deploy\windows\deploy.cmd -Mode start-web
pause
'@
Write-TextFile (Join-Path $StageRoot "START_WEB_ONLY.cmd") $StartWeb

$StartServer = @'
@echo off
cd /d "%~dp0"
call deploy\windows\deploy.cmd -Mode start-server
pause
'@
Write-TextFile (Join-Path $StageRoot "START_SERVER_ONLY.cmd") $StartServer

$OpenFirewall = @'
@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell.exe -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%CD%\deploy\windows\deploy.ps1"" -Mode firewall'"
'@
Write-TextFile (Join-Path $StageRoot "OPEN_FIREWALL_ADMIN.cmd") $OpenFirewall

$Install = @'
@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "Start-Process powershell.exe -Verb RunAs -ArgumentList '-NoProfile -ExecutionPolicy Bypass -File ""%CD%\deploy\windows\deploy.ps1"" -Mode install'"
'@
Write-TextFile (Join-Path $StageRoot "INSTALL_AUTOSTART_ADMIN.cmd") $Install

$Status = @'
@echo off
cd /d "%~dp0"
call deploy\windows\deploy.cmd -Mode status
pause
'@
Write-TextFile (Join-Path $StageRoot "STATUS.cmd") $Status

$Stop = @'
@echo off
cd /d "%~dp0"
call deploy\windows\deploy.cmd -Mode stop
pause
'@
Write-TextFile (Join-Path $StageRoot "STOP_AUTOSTART.cmd") $Stop

$Readme = @"
FlorrBt v$AppVersion one-click deployment package

1. Extract this zip on the Windows server.
2. Install Microsoft Visual C++ Redistributable 2022+ if FlorrBt.Server.exe cannot start.
3. Double-click START_ALL.cmd. The package uses its bundled Node.js runtime.
4. Open http://SERVER_IP:8080 from another computer.

Useful files:
- START_ALL.cmd: starts the hot-reload launcher, game server, and web bridge.
- START_SERVER_ONLY.cmd: starts the server through FlorrBt.Launcher.exe.
- START_WEB_ONLY.cmd: starts only the web client bridge.
- deploy/windows-web/start_web.cmd: standalone web bridge launcher.
- OPEN_FIREWALL_ADMIN.cmd: opens TCP 8080 and 10012 as Administrator.
- INSTALL_AUTOSTART_ADMIN.cmd: installs logon auto-start tasks as Administrator.
- STATUS.cmd: checks task and port status.
- data/server.cfg: created on first start if missing. Edit this for port/API/config commands.

Hot reload:
1. Build the replacement FlorrBt.Server.exe.
2. Put it at FlorrBt.Server.next.exe in this package root, or pass its path to the command.
3. Enter hot_reload in the server console, or hot_reload <candidate-path>.
4. The launcher snapshots the completed tick, starts the candidate, restores the snapshot, and rolls back if startup fails.

Packaged configuration: $Configuration
Version: $AppVersion
Packaged at: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
"@
Write-TextFile (Join-Path $StageRoot "README_DEPLOY.txt") $Readme

if (Test-Path $OutputZip) {
    Remove-Item -LiteralPath $OutputZip -Force
}

Get-ChildItem -LiteralPath $StageRoot -Force | Compress-Archive -DestinationPath $OutputZip -Force

$zipInfo = Get-Item $OutputZip
Write-Host "Created: $($zipInfo.FullName)"
Write-Host "Size: $([math]::Round($zipInfo.Length / 1MB, 2)) MB"
