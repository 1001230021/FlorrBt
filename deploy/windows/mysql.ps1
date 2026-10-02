[CmdletBinding()]
param(
    [ValidateSet("start", "stop", "status")]
    [string]$Action = "start",
    [string]$MySqlRoot = "",
    [string]$DataDir = "",
    [int]$Port = 0,
    [string]$BindAddress = "",
    [string]$RootPassword = "",
    [switch]$SkipProvision,
    [switch]$NoWait
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$RunDir = Join-Path $RepoRoot "mysql\run"
$LogDir = Join-Path $RepoRoot "mysql\logs"
$PidFile = Join-Path $RunDir "florrbt-mysqld.pid"
$ManagedIniMarker = "# FlorrBt managed MySQL runtime configuration"

function Read-ServerConfigValue([string]$Name, [string]$Fallback) {
    $configPath = Join-Path $RepoRoot "data\server.cfg"
    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        return $Fallback
    }

    $namePattern = [regex]::Escape($Name)
    foreach ($line in Get-Content -LiteralPath $configPath -ErrorAction Stop) {
        if ($line -match "^\s*set\s+$namePattern\s+(.+?)\s*$") {
            $value = $Matches[1].Trim()
            if ($value -and $value -notmatch '^//') {
                return $value.Trim('"')
            }
        }
    }
    return $Fallback
}

function Resolve-AbsolutePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        throw "A path cannot be empty."
    }
    if ([IO.Path]::IsPathRooted($Path)) {
        return [IO.Path]::GetFullPath($Path)
    }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

function Resolve-MySqlRoot {
    $candidates = @()
    if ($MySqlRoot) { $candidates += $MySqlRoot }
    if ($env:FLORRBT_MYSQL_ROOT) { $candidates += $env:FLORRBT_MYSQL_ROOT }
    $candidates += @(
        (Join-Path $RepoRoot "mysql"),
        (Join-Path $RepoRoot "mysql-server"),
        "C:\Program Files\MySQL\MySQL Server 8.4",
        "C:\Program Files\MariaDB 11.0"
    )

    # Also discover ZIP installs such as D:\<folder>\mysql-8.x without
    # embedding a non-ASCII developer path in this script. This keeps the
    # package compatible with Windows PowerShell's legacy script decoding.
    foreach ($driveRoot in [IO.Directory]::GetLogicalDrives()) {
        foreach ($pattern in @(
                (Join-Path $driveRoot "mysql-*"),
                (Join-Path $driveRoot "*\mysql-*"))) {
            $candidates += Get-ChildItem -Path $pattern -Directory -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName
        }
    }

    $command = Get-Command mysqld.exe -ErrorAction SilentlyContinue
    if ($command -and $command.Source) {
        $candidates += (Split-Path $command.Source -Parent | Split-Path -Parent)
    }

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        $resolved = Resolve-AbsolutePath $candidate
        if (Test-Path -LiteralPath (Join-Path $resolved "bin\mysqld.exe") -PathType Leaf) {
            return $resolved
        }
        if (Test-Path -LiteralPath (Join-Path $resolved "mysqld.exe") -PathType Leaf) {
            return $resolved
        }
    }

    throw "MySQL server was not found. Extract MySQL 8.x, then run START_MYSQL.cmd -MySqlRoot <directory> or set FLORRBT_MYSQL_ROOT."
}

function Resolve-MySqlExecutable([string]$Root, [string]$Name) {
    $binPath = Join-Path $Root "bin\$Name"
    if (Test-Path -LiteralPath $binPath -PathType Leaf) { return $binPath }

    $rootPath = Join-Path $Root $Name
    if (Test-Path -LiteralPath $rootPath -PathType Leaf) { return $rootPath }

    return $null
}

function Convert-ToMySqlPath([string]$Path) {
    # MySQL's Windows option parser is more reliable with forward slashes,
    # especially when the command line was assembled by PowerShell 5.1.
    return $Path.Replace('\', '/')
}

function Get-MySqlShortPath([string]$Path) {
    if (-not ('FlorrBtNativePath' -as [type])) {
        Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;

public static class FlorrBtNativePath
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern uint GetShortPathName(string longPath, StringBuilder shortPath, int bufferLength);

    public static string GetShortPath(string path)
    {
        var buffer = new StringBuilder(512);
        uint length = GetShortPathName(path, buffer, buffer.Capacity);
        return length == 0 ? path : buffer.ToString();
    }
}
"@
    }
    return [FlorrBtNativePath]::GetShortPath($Path)
}

function Get-OwnedMySqlProcess {
    if (-not (Test-Path -LiteralPath $PidFile -PathType Leaf)) { return $null }
    $pidText = (Get-Content -LiteralPath $PidFile -Raw).Trim()
    $processId = 0
    if (-not [int]::TryParse($pidText, [ref]$processId) -or $processId -le 0) { return $null }
    try {
        $process = Get-Process -Id $processId -ErrorAction Stop
        if ($process.ProcessName -eq "mysqld") { return $process }
        if ($process.ProcessName -match '^(powershell|pwsh)$') {
            $commandLine = (Get-CimInstance Win32_Process -Filter "ProcessId = $processId" -ErrorAction SilentlyContinue).CommandLine
            if ($commandLine -and $commandLine -match 'mysql_process\.ps1') { return $process }
        }
    } catch {
        return $null
    }
    return $null
}

function Invoke-MySqlAdmin([string]$Root, [string[]]$CommandArguments) {
    $mysqlAdmin = Resolve-MySqlExecutable $Root "mysqladmin.exe"
    if (-not $mysqlAdmin) { return $false }

    $args = @(
        "--no-defaults",
        "--protocol=tcp",
        "--host=$script:ResolvedBindAddress",
        "--port=$script:ResolvedPort",
        "--user=root"
    )
    if ($RootPassword) { $args += "--password=$RootPassword" }
    $args += $CommandArguments
    $previousErrorAction = $ErrorActionPreference
    try {
        # mysqladmin writes a diagnostic to stderr for the normal "server is
        # down" probe. Do not let PowerShell's Stop preference turn that
        # expected probe result into a terminating exception.
        $ErrorActionPreference = "Continue"
        & $mysqlAdmin @args 1>$null 2>$null
        return $LASTEXITCODE -eq 0
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
}

function Escape-SqlLiteral([string]$Value) {
    return $Value.Replace('\', '\\').Replace("'", "''")
}

function Initialize-ApplicationDatabase([string]$Root) {
    if ($SkipProvision) { return }

    $mysql = Resolve-MySqlExecutable $Root "mysql.exe"
    if (-not $mysql) { throw "mysql.exe was not found under $Root; cannot provision the FlorrBt database user." }

    $database = Read-ServerConfigValue "mysql_database" "florrbt"
    $user = Read-ServerConfigValue "mysql_user" "florrbt"
    $applicationPassword = Read-ServerConfigValue "mysql_password" ""
    if ($env:FLORRBT_MYSQL_PASSWORD) { $applicationPassword = $env:FLORRBT_MYSQL_PASSWORD }

    if ($database -notmatch '^[A-Za-z0-9_$]+$') {
        throw "mysql_database must contain only letters, digits, '_' or '$'."
    }
    if ($user -notmatch '^[A-Za-z0-9_$]+$') {
        throw "mysql_user must contain only letters, digits, '_' or '$'."
    }

    $quotedDatabase = [string]::Concat('`', $database, '`')
    $quotedPassword = Escape-SqlLiteral $applicationPassword
    $sql = @"
CREATE DATABASE IF NOT EXISTS $quotedDatabase CHARACTER SET utf8mb4 COLLATE utf8mb4_bin;
CREATE USER IF NOT EXISTS '$(Escape-SqlLiteral $user)'@'127.0.0.1' IDENTIFIED BY '$quotedPassword';
CREATE USER IF NOT EXISTS '$(Escape-SqlLiteral $user)'@'localhost' IDENTIFIED BY '$quotedPassword';
GRANT ALL PRIVILEGES ON $quotedDatabase.* TO '$(Escape-SqlLiteral $user)'@'127.0.0.1';
GRANT ALL PRIVILEGES ON $quotedDatabase.* TO '$(Escape-SqlLiteral $user)'@'localhost';
FLUSH PRIVILEGES;
"@

    $args = @(
        "--no-defaults",
        "--protocol=tcp",
        "--host=$script:ResolvedBindAddress",
        "--port=$script:ResolvedPort",
        "--user=root"
    )
    if ($RootPassword) { $args += "--password=$RootPassword" }
    $previousErrorAction = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $sql | & $mysql @args 1>$null 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "Could not provision MySQL database '$database' and user '$user'. Check root credentials or use -SkipProvision."
        }
    }
    finally {
        $ErrorActionPreference = $previousErrorAction
    }
    Write-Host "MySQL database '$database' and application user '$user' are ready."
}

function Show-RecentLog {
    $errorLog = Join-Path $LogDir "mysqld.error.log"
    if (Test-Path -LiteralPath $errorLog -PathType Leaf) {
        Write-Host "Recent MySQL error log:"
        Get-Content -LiteralPath $errorLog -Tail 20
    }
}

function Wait-ForMySql([string]$Root, [System.Diagnostics.Process]$Process) {
    if ($NoWait) { return }
    for ($attempt = 0; $attempt -lt 30; ++$attempt) {
        $Process.Refresh()
        if ($Process.HasExited) {
            Show-RecentLog
            throw "mysqld exited during startup (exit code $($Process.ExitCode))."
        }
        if (Invoke-MySqlAdmin $Root @("ping")) {
            Write-Host "MySQL is ready on $script:ResolvedBindAddress`:$script:ResolvedPort."
            return
        }
        Start-Sleep -Seconds 1
    }

    Show-RecentLog
    throw "MySQL process is running, but the server did not answer ping within 30 seconds. If root has a password, pass -RootPassword."
}

function Start-MySql {
    $existing = Get-OwnedMySqlProcess
    if ($existing) {
        Write-Host "MySQL is already running (PID $($existing.Id))."
        if (-not $NoWait -and (Invoke-MySqlAdmin $script:ResolvedMySqlRoot @("ping"))) {
            Write-Host "MySQL is ready on $script:ResolvedBindAddress`:$script:ResolvedPort."
        }
        Initialize-ApplicationDatabase $script:ResolvedMySqlRoot
        return
    }

    if (Invoke-MySqlAdmin $script:ResolvedMySqlRoot @("ping")) {
        Write-Host "A MySQL server is already answering on $script:ResolvedBindAddress`:$script:ResolvedPort."
        Initialize-ApplicationDatabase $script:ResolvedMySqlRoot
        return
    }

    $mysqld = Resolve-MySqlExecutable $script:ResolvedMySqlRoot "mysqld.exe"
    if (-not $mysqld) { throw "mysqld.exe was not found under $script:ResolvedMySqlRoot." }

    New-Item -ItemType Directory -Force -Path $script:ResolvedDataDir,$RunDir,$LogDir | Out-Null
    $errorLog = Join-Path $LogDir "mysqld.error.log"
    $stdoutLog = Join-Path $LogDir "mysqld.stdout.log"
    $stderrLog = Join-Path $LogDir "mysqld.stderr.log"
    $systemDatabaseDir = Join-Path $script:ResolvedDataDir "mysql"
    $dataEntries = @(Get-ChildItem -LiteralPath $script:ResolvedDataDir -Force -ErrorAction Stop)

    if (-not (Test-Path -LiteralPath $systemDatabaseDir -PathType Container)) {
        if ($dataEntries.Count -gt 0) {
            throw "MySQL data directory is not initialized and is not empty: $script:ResolvedDataDir. Refusing to alter it."
        }
        Write-Host "Initializing a new MySQL data directory: $script:ResolvedDataDir"
        & $mysqld --initialize-insecure "--basedir=$script:ResolvedMySqlRoot" "--datadir=$script:ResolvedDataDir" "--log-error=$errorLog"
        if ($LASTEXITCODE -ne 0) {
            Show-RecentLog
            throw "MySQL initialization failed with exit code $LASTEXITCODE."
        }
    }

    $mysqlBin = Split-Path $mysqld -Parent
    $mysqlRootArgument = Convert-ToMySqlPath (Get-MySqlShortPath $script:ResolvedMySqlRoot)
    $dataDirArgument = Convert-ToMySqlPath (Get-MySqlShortPath $script:ResolvedDataDir)
    $pidFileArgument = Convert-ToMySqlPath (Get-MySqlShortPath $PidFile)
    $errorLogArgument = Convert-ToMySqlPath (Get-MySqlShortPath $errorLog)
    $configPath = Join-Path $script:ResolvedMySqlRoot "my.ini"
    $existingConfig = Test-Path -LiteralPath $configPath -PathType Leaf
    if ($existingConfig) {
        $existingConfigText = Get-Content -LiteralPath $configPath -Raw -ErrorAction Stop
        if ($existingConfigText -notmatch [regex]::Escape($ManagedIniMarker)) {
            throw "MySQL installation already contains my.ini: $configPath. Refusing to overwrite it; move it aside or configure its [mysqld] datadir/port settings manually."
        }
    }
    $config = @(
        $ManagedIniMarker,
        "[mysqld]",
        "basedir=$mysqlRootArgument",
        "datadir=$dataDirArgument",
        "pid-file=$pidFileArgument",
        "log-error=$errorLogArgument",
        "port=$script:ResolvedPort",
        "bind-address=$script:ResolvedBindAddress"
    ) -join [Environment]::NewLine
    # mysqld reads Windows option files using the active system code page on
    # older Windows builds. Use that encoding so non-ASCII installation paths
    # do not become mojibake when MySQL parses the child-process configuration.
    [IO.File]::WriteAllText($configPath, $config, [Text.Encoding]::Default)
    $processHost = Join-Path $PSScriptRoot "mysql_process.ps1"
    $hostArguments = @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $processHost,
        "-MySqlExe",
        $mysqld
    )
    $startParameters = @{
        FilePath = "powershell.exe"
        ArgumentList = $hostArguments
        WorkingDirectory = $mysqlBin
        WindowStyle = "Hidden"
        RedirectStandardOutput = $stdoutLog
        RedirectStandardError = $stderrLog
        PassThru = $true
    }
    $process = Start-Process @startParameters
    Set-Content -LiteralPath $PidFile -Value ([string]$process.Id) -Encoding ASCII
    Write-Host "Started MySQL (PID $($process.Id)). Logs: $LogDir"
    Wait-ForMySql $script:ResolvedMySqlRoot $process
    Initialize-ApplicationDatabase $script:ResolvedMySqlRoot
}

function Stop-MySql {
    $process = Get-OwnedMySqlProcess
    if (-not $process) {
        Write-Host "No MySQL process owned by this deployment was found."
        return
    }

    if (Invoke-MySqlAdmin $script:ResolvedMySqlRoot @("shutdown")) {
        for ($attempt = 0; $attempt -lt 15; ++$attempt) {
            $process.Refresh()
            if ($process.HasExited) { break }
            Start-Sleep -Seconds 1
        }
    }
    $process.Refresh()
    if (-not $process.HasExited) {
        Write-Warning "mysqladmin shutdown did not stop PID $($process.Id); leaving it running to avoid forced termination."
        return
    }
    if (Test-Path -LiteralPath $PidFile -PathType Leaf) {
        Remove-Item -LiteralPath $PidFile -Force
    }
    $managedConfigPath = Join-Path $script:ResolvedMySqlRoot "my.ini"
    if (Test-Path -LiteralPath $managedConfigPath -PathType Leaf) {
        $managedConfigText = Get-Content -LiteralPath $managedConfigPath -Raw -ErrorAction Stop
        if ($managedConfigText -match [regex]::Escape($ManagedIniMarker)) {
            [IO.File]::Delete($managedConfigPath)
        }
    }
    Write-Host "MySQL stopped."
}

function Show-MySqlStatus {
    $process = Get-OwnedMySqlProcess
    if ($process) {
        $ready = Invoke-MySqlAdmin $script:ResolvedMySqlRoot @("ping")
        Write-Host "Managed MySQL: running (PID $($process.Id)); ready=$ready; endpoint=$script:ResolvedBindAddress`:$script:ResolvedPort"
        return
    }
    if (Invoke-MySqlAdmin $script:ResolvedMySqlRoot @("ping")) {
        Write-Host "External MySQL: answering on $script:ResolvedBindAddress`:$script:ResolvedPort"
        return
    }
    Write-Host "MySQL: stopped"
}

try {
    if (-not $RootPassword -and $env:FLORRBT_MYSQL_ROOT_PASSWORD) {
        $RootPassword = $env:FLORRBT_MYSQL_ROOT_PASSWORD
    }
    $script:ResolvedPort = if ($Port -gt 0) { $Port } else { [int](Read-ServerConfigValue "mysql_port" "3306") }
    $script:ResolvedBindAddress = if ($BindAddress) { $BindAddress } else { Read-ServerConfigValue "mysql_host" "127.0.0.1" }
    $script:ResolvedDataDir = if ($DataDir) { Resolve-AbsolutePath $DataDir } else { Resolve-AbsolutePath "mysql\data" }
    $script:ResolvedMySqlRoot = Resolve-MySqlRoot

    switch ($Action) {
        "start" { Start-MySql }
        "stop" { Stop-MySql }
        "status" { Show-MySqlStatus }
    }
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
