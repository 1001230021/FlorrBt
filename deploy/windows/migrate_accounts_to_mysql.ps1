[CmdletBinding()]
param(
    [string]$JsonPath = "",
    [string]$ServerExe = "",
    [string]$MySqlRoot = "",
    [string]$DataDir = "",
    [string]$RootPassword = "",
    [switch]$SkipProvision,
    [switch]$StartMySql
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path

function Resolve-RepoPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $RepoRoot $Path))
}

try {
    $jsonInput = if ($JsonPath) { $JsonPath } else { "data\accounts.json" }
    $json = Resolve-RepoPath $jsonInput
    if (-not (Test-Path -LiteralPath $json -PathType Leaf)) {
        throw "Account JSON file was not found: $json"
    }

    if (-not $ServerExe) {
        $serverCandidates = @(
            (Join-Path $RepoRoot "x64\Release\FlorrBt.Server.exe"),
            (Join-Path $RepoRoot "x64\Debug\FlorrBt.Server.exe"),
            (Join-Path $RepoRoot "build\Release\FlorrBt_Server.exe"),
            (Join-Path $RepoRoot "build\Debug\FlorrBt_Server.exe")
        )
        $ServerExe = $serverCandidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
    } else {
        $ServerExe = Resolve-RepoPath $ServerExe
    }
    if (-not $ServerExe) { throw "FlorrBt.Server.exe was not found. Build the server first or pass -ServerExe <path>." }

    if ($StartMySql) {
        $mysqlScript = Join-Path $PSScriptRoot "mysql.ps1"
        $mysqlArguments = @("-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $mysqlScript, "-Action", "start")
        if ($MySqlRoot) { $mysqlArguments += @("-MySqlRoot", $MySqlRoot) }
        if ($DataDir) { $mysqlArguments += @("-DataDir", $DataDir) }
        if ($RootPassword) { $mysqlArguments += @("-RootPassword", $RootPassword) }
        if ($SkipProvision) { $mysqlArguments += "-SkipProvision" }
        & powershell.exe @mysqlArguments
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    Write-Host "Migrating account JSON to MySQL: $json"
    Push-Location $RepoRoot
    try {
        & $ServerExe "--migrate-accounts-to-mysql" $json
        $migrationExitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    exit $migrationExitCode
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
