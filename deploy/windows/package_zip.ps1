param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputZip = "",
    [switch]$IncludePdb,
    [string]$MySqlRoot = "",
    [string]$SfmlRoot = "",
    [string]$BuildDirectory = "",
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
if ($AppVersion -notmatch '^\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?$') { throw "Invalid application version" }
$PackageName = "FlorrBtHotReload-v$AppVersion"
$StageRoot = [IO.Path]::GetFullPath((Join-Path $ArtifactsDir ("package-staging\" + $PackageName + "-" + [guid]::NewGuid().ToString("N"))))
if (-not $StageRoot.StartsWith(([IO.Path]::GetFullPath($ArtifactsDir) + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) {
    throw "Package staging must stay inside artifacts"
}
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

# 只复制将进入源码仓库的文件，不能把本机账号、密钥、日志或技能链接装进发布包。
function Copy-ProjectFiles([string]$Directory) {
    $files = & git -C $RepoRoot -c core.quotepath=false ls-files --cached --others --exclude-standard -- $Directory
    if ($LASTEXITCODE -ne 0) { throw "Cannot obtain package source file list" }
    foreach ($relative in $files | Select-Object -Unique) {
        if ($relative -match '(?i)(?:^|/)(?:node_modules|__pycache__|hot_reload|logs|\.agents|\.claude|\.mirasim)(?:/|$)|\.(?:log|pyc|dmp)$|(?:^|/)\.env(?:$|\.(?!example$))|^data/(?:accounts\.json(?!\.example$)|unique_petals\.json$|server\.cfg$)|^deploy/windows/account_links\.ps1$|^deploy/windows-web/web\.env\.ps1$') { continue }
        $source = Join-Path $RepoRoot $relative
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
        if ((Get-Item -LiteralPath $source -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Cannot package a source link: $relative" }
        $destination = [IO.Path]::GetFullPath((Join-Path $StageRoot $relative))
        if (-not $destination.StartsWith(($StageRoot + [IO.Path]::DirectorySeparatorChar), [StringComparison]::OrdinalIgnoreCase)) { throw "Invalid package path: $relative" }
        Copy-File $source $destination
    }
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
New-Item -ItemType Directory -Force -Path $StageRoot | Out-Null

$BuildDir = if ($BuildDirectory) { (Resolve-Path -LiteralPath $BuildDirectory).Path } else { Join-Path $RepoRoot "x64\$Configuration" }
$RuntimeDir = Join-Path $StageRoot "x64\$Configuration"
$ServerExe = Join-Path $BuildDir "FlorrBt.Server.exe"
$LauncherExe = Join-Path $BuildDir "FlorrBt.Launcher.exe"
Copy-File $ServerExe (Join-Path $RuntimeDir "FlorrBt.Server.exe")
Copy-File $LauncherExe (Join-Path $RuntimeDir "FlorrBt.Launcher.exe")
Copy-File (Join-Path $BuildDir "libcurl.dll") (Join-Path $RuntimeDir "libcurl.dll")
Copy-File (Join-Path $BuildDir "curl-COPYING.txt") (Join-Path $RuntimeDir "curl-COPYING.txt")
$NodeExe = Find-NodeRuntime
Copy-File $NodeExe (Join-Path $StageRoot "runtime\node.exe")
$nodeVersion = (& $NodeExe --version).Trim()
if ($LASTEXITCODE -ne 0 -or $nodeVersion -notmatch '^v\d+\.\d+\.\d+$') { throw "Cannot identify bundled Node.js version" }
$nodeLicense = Join-Path $BuildDir "node-$nodeVersion-LICENSE.txt"
if (-not (Test-Path -LiteralPath $nodeLicense -PathType Leaf)) {
    $licenseResponse = Invoke-RestMethod -Uri "https://api.github.com/repos/nodejs/node/contents/LICENSE?ref=$nodeVersion" -TimeoutSec 30
    if ($licenseResponse.encoding -ne "base64") { throw "Cannot read the Node.js license" }
    [IO.File]::WriteAllBytes($nodeLicense, [Convert]::FromBase64String($licenseResponse.content))
}
Copy-File $nodeLicense (Join-Path $StageRoot "runtime\LICENSE.node.txt")

$sfmlRoots = @($SfmlRoot, $env:FLORRBT_SFML_ROOT, (Join-Path $RepoRoot "dependencies\SFML")) | Where-Object { $_ }
foreach ($component in @("graphics", "window", "network", "system")) {
    $suffix = if ($Configuration -eq "Debug") { "-d" } else { "" }
    $name = "sfml-$component$suffix-3.dll"
    $source = Join-Path $BuildDir $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        $source = $sfmlRoots | ForEach-Object { Join-Path $_ "bin\$name" } |
            Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    }
    if (-not $source) { throw "Missing $name; pass -SfmlRoot <SDK directory>" }
    Copy-File $source (Join-Path $RuntimeDir $name)
}
$sfmlLicense = @((Join-Path $BuildDir "SFML-LICENSE.md")) + @($sfmlRoots | ForEach-Object { Join-Path $_ "license.md" })
$sfmlLicense = $sfmlLicense | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (-not $sfmlLicense) { throw "Missing SFML license; pass -SfmlRoot <SDK directory>" }
Copy-File $sfmlLicense (Join-Path $RuntimeDir "SFML-LICENSE.md")

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

foreach ($directory in @("web", "data", "tools", "deploy/windows", "deploy/windows-web")) { Copy-ProjectFiles $directory }

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

Upgrade: extract into a new directory. Back up and preserve the existing mysql data,
data/server.cfg, account files and local OAuth/web configuration before switching.
This archive includes no production accounts, database or local secrets.
Discord/QQ login remains disabled until the provider settings are configured.
"@
Write-TextFile (Join-Path $StageRoot "README_DEPLOY.txt") $Readme

$releaseNotes = Join-Path $RepoRoot "docs\release-v$AppVersion.md"
if (Test-Path -LiteralPath $releaseNotes -PathType Leaf) {
    Copy-File $releaseNotes (Join-Path $StageRoot "RELEASE_NOTES.md")
}
$sourceCommit = (& git -C $RepoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Cannot determine source revision" }
$sourceDirty = [bool](& git -C $RepoRoot status --porcelain)
$manifest = [ordered]@{
    version = $AppVersion
    configuration = $Configuration
    sourceCommit = $sourceCommit
    sourceDirty = $sourceDirty
    packagedAtUtc = [DateTime]::UtcNow.ToString("o")
    bundledNode = $nodeVersion
    bundledMySqlServer = [bool]$BundleMySqlServer
    serverSha256 = (Get-FileHash -LiteralPath (Join-Path $RuntimeDir "FlorrBt.Server.exe") -Algorithm SHA256).Hash.ToLowerInvariant()
    launcherSha256 = (Get-FileHash -LiteralPath (Join-Path $RuntimeDir "FlorrBt.Launcher.exe") -Algorithm SHA256).Hash.ToLowerInvariant()
}
Write-TextFile (Join-Path $StageRoot "PACKAGE_MANIFEST.json") ($manifest | ConvertTo-Json)

if (Test-Path $OutputZip) {
    if (-not (Test-Path -LiteralPath $OutputZip -PathType Leaf) -or [IO.Path]::GetExtension($OutputZip) -ne '.zip') {
        throw "OutputZip must name a zip file"
    }
    Remove-Item -LiteralPath $OutputZip -Force
}

Get-ChildItem -LiteralPath $StageRoot -Force | Compress-Archive -DestinationPath $OutputZip -Force

$zipInfo = Get-Item $OutputZip
Write-Host "Created: $($zipInfo.FullName)"
Write-Host "Staging: $StageRoot"
Write-Host "Size: $([math]::Round($zipInfo.Length / 1MB, 2)) MB"
