param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputZip = "",
    [switch]$IncludePdb,
    [string]$MySqlRoot = "",
    [switch]$BundleMySqlServer
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ArtifactsDir = Join-Path $RepoRoot "artifacts"
$VersionHeader = Join-Path $RepoRoot "src\Shared\version.h"
$VersionText = Get-Content -LiteralPath $VersionHeader -Raw
if ($VersionText -notmatch 'inline\s+constexpr\s+std::string_view\s+version\s*=\s*"([^"]+)"') {
    throw "Could not read the application version from $VersionHeader"
}
$AppVersion = $Matches[1]
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

function Resolve-MySqlDistribution([string]$RequestedRoot) {
    $candidates = @(
        $RequestedRoot,
        $env:FLORRBT_MYSQL_ROOT
    ) | Where-Object { $_ } | Select-Object -Unique

    foreach ($candidate in $candidates) {
        $resolved = [IO.Path]::GetFullPath($candidate)
        if ((Test-Path -LiteralPath (Join-Path $resolved "bin\mysqld.exe") -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $resolved "bin\mysql.exe") -PathType Leaf) -and
            (Test-Path -LiteralPath (Join-Path $resolved "bin\mysqladmin.exe") -PathType Leaf)) {
            return $resolved
        }
    }

    throw "A MySQL 8.x distribution is required for -BundleMySqlServer. Pass -MySqlRoot <directory> or set FLORRBT_MYSQL_ROOT."
}

function Copy-BundledMySqlServer([string]$SourceRoot, [string]$DestinationRoot) {
    $sourceBin = Join-Path $SourceRoot "bin"
    $destinationBin = Join-Path $DestinationRoot "bin"
    New-Item -ItemType Directory -Force -Path $destinationBin | Out-Null

    # The deployment scripts only need these three tools. Keep all release
    # DLLs beside them because MySQL may load optional authentication support
    # dynamically, but omit the very large debug/PDB payload from the ZIP.
    foreach ($name in @("mysqld.exe", "mysql.exe", "mysqladmin.exe")) {
        Copy-File (Join-Path $sourceBin $name) (Join-Path $destinationBin $name)
    }
    Get-ChildItem -LiteralPath $sourceBin -File -Filter "*.dll" |
        Where-Object { $_.Name -notlike "*-debug.dll" } |
        ForEach-Object { Copy-File $_.FullName (Join-Path $destinationBin $_.Name) }

    Copy-Dir (Join-Path $SourceRoot "share") (Join-Path $DestinationRoot "share")
    $pluginRoot = Join-Path $SourceRoot "lib\plugin"
    if (Test-Path -LiteralPath $pluginRoot -PathType Container) {
        $destinationPlugins = Join-Path $DestinationRoot "lib\plugin"
        New-Item -ItemType Directory -Force -Path $destinationPlugins | Out-Null
        Get-ChildItem -LiteralPath $pluginRoot -File |
            ForEach-Object { Copy-File $_.FullName (Join-Path $destinationPlugins $_.Name) }
    }
    $privateRoot = Join-Path $SourceRoot "lib\private"
    if (Test-Path -LiteralPath $privateRoot -PathType Container) {
        Copy-Dir $privateRoot (Join-Path $DestinationRoot "lib\private")
    }
    foreach ($name in @("LICENSE", "README")) {
        $source = Join-Path $SourceRoot $name
        if (Test-Path -LiteralPath $source -PathType Leaf) {
            Copy-File $source (Join-Path $DestinationRoot $name)
        }
    }
}

function Find-VcRuntimeDirectory {
    $roots = @(
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC"
    )
    foreach ($root in $roots) {
        if (-not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $candidate = Get-ChildItem -LiteralPath $root -Directory |
            Sort-Object Name -Descending |
            ForEach-Object {
                Get-ChildItem -LiteralPath (Join-Path $_.FullName "x64") -Directory -Filter "Microsoft.VC*.CRT" -ErrorAction SilentlyContinue |
                    Sort-Object Name -Descending |
                    Select-Object -First 1 -ExpandProperty FullName
            } |
            Select-Object -First 1
        if ($candidate) { return $candidate }
    }
    return $null
}

function Copy-VcRuntime([string]$Destination) {
    $runtime = Find-VcRuntimeDirectory
    if (-not $runtime) {
        Write-Warning "Visual C++ x64 app-local runtime was not found; the target server must install VC++ Redistributable 2022+."
        return
    }
    Get-ChildItem -LiteralPath $runtime -File -Filter "*.dll" | ForEach-Object {
        Copy-File $_.FullName (Join-Path $Destination $_.Name)
    }
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

# Keep the MySQL client and the OpenSSL DLLs it loads together in the
# self-contained package.  New builds place these beside the executable; the
# fallback locations also make packaging an older local build less surprising.
$mysqlRuntimeNames = @("libmysql.dll", "libssl-3-x64.dll", "libcrypto-3-x64.dll")
$mysqlRoots = @($MySqlRoot, $env:FLORRBT_MYSQL_ROOT) |
    Where-Object { $_ } |
    Select-Object -Unique
foreach ($runtimeName in $mysqlRuntimeNames) {
    $runtimeSource = Join-Path $BuildDir $runtimeName
    if (-not (Test-Path $runtimeSource)) {
        foreach ($mysqlRootCandidate in $mysqlRoots) {
            $runtimeCandidates = @(
                (Join-Path $mysqlRootCandidate "lib\$runtimeName"),
                (Join-Path $mysqlRootCandidate "bin\$runtimeName"),
                (Join-Path $mysqlRootCandidate $runtimeName)
            )
            $runtimeSource = $runtimeCandidates |
                Where-Object { Test-Path $_ } |
                Select-Object -First 1
            if ($runtimeSource) { break }
        }
    }
    Copy-File $runtimeSource (Join-Path $RuntimeDir $runtimeName)
}

Copy-VcRuntime $RuntimeDir

if ($BundleMySqlServer) {
    $resolvedMySqlRoot = Resolve-MySqlDistribution $MySqlRoot
    $bundledMySqlRoot = Join-Path $StageRoot "mysql-server"
    Copy-BundledMySqlServer $resolvedMySqlRoot $bundledMySqlRoot
    Copy-VcRuntime (Join-Path $bundledMySqlRoot "bin")
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
call deploy\windows\deploy.cmd -Mode start %*
'@
Write-TextFile (Join-Path $StageRoot "START_ALL.cmd") $StartAll
Write-TextFile (Join-Path $StageRoot "START_ALL.bat") $StartAll

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
call deploy\windows\deploy.cmd -Mode start-server %*
pause
'@
Write-TextFile (Join-Path $StageRoot "START_SERVER_ONLY.cmd") $StartServer

$StartMysql = @'
@echo off
cd /d "%~dp0"
call deploy\windows\start_mysql.cmd %*
pause
'@
Write-TextFile (Join-Path $StageRoot "START_MYSQL.cmd") $StartMysql

$StopMysql = @'
@echo off
cd /d "%~dp0"
call deploy\windows\stop_mysql.cmd %*
pause
'@
Write-TextFile (Join-Path $StageRoot "STOP_MYSQL.cmd") $StopMysql

$MysqlStatus = @'
@echo off
cd /d "%~dp0"
call deploy\windows\mysql_status.cmd %*
pause
'@
Write-TextFile (Join-Path $StageRoot "MYSQL_STATUS.cmd") $MysqlStatus

$MigrateAccounts = @'
@echo off
cd /d "%~dp0"
call deploy\windows\migrate_accounts_to_mysql.cmd %*
pause
'@
Write-TextFile (Join-Path $StageRoot "MIGRATE_ACCOUNTS_TO_MYSQL.cmd") $MigrateAccounts

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

$BundledMySqlNote = if ($BundleMySqlServer) {
    "MySQL 8 is bundled under mysql-server\; START_ALL.cmd discovers it automatically."
} else {
    "MySQL Server is not bundled. Set FLORRBT_MYSQL_ROOT or pass -MySqlRoot <directory>."
}

$Readme = @"
FlorrBt v$AppVersion one-click deployment package

1. Extract this zip on the Windows server.
2. Run OPEN_FIREWALL_ADMIN.cmd once if remote clients cannot reach ports 8080/10012.
3. Double-click START_ALL.cmd. It initializes MySQL on first launch, then starts the game server and web bridge.
4. Open http://SERVER_IP:8080 from another computer.

$BundledMySqlNote

Useful files:
- START_ALL.bat / START_ALL.cmd: starts MySQL, the hot-reload launcher, game server, and web bridge.
- START_SERVER_ONLY.cmd: starts the server through FlorrBt.Launcher.exe.
- START_MYSQL.cmd: initializes/starts the package-managed MySQL data directory.
- MIGRATE_ACCOUNTS_TO_MYSQL.cmd: imports data/accounts.json into MySQL and verifies it.
- STOP_MYSQL.cmd / MYSQL_STATUS.cmd: controls or checks the package-managed MySQL process.
- mysql\: package-managed MySQL data/log files; a generated `my.ini` is removed on clean shutdown.
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
