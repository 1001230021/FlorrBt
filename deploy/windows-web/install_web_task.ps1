param(
    [string]$Root = "",
    [string]$WebHost = "",
    [int]$WebPort = 0,
    [string]$GameHost = "",
    [int]$GamePort = 0
)

$ErrorActionPreference = "Stop"

$TaskName = "FlorrBtWeb"

function Test-FlorrBtRoot([string]$Path) {
    if (-not $Path) { return $false }
    return (Test-Path (Join-Path $Path "web\index.html")) -and
           (Test-Path (Join-Path $Path "tools\web_client_server.js"))
}

function Resolve-FlorrBtRoot([string]$ExplicitRoot) {
    $seeds = @()
    if ($ExplicitRoot) { $seeds += $ExplicitRoot }
    $seeds += $PSScriptRoot
    $seeds += (Get-Location).Path

    foreach ($seed in $seeds | Where-Object { $_ } | Select-Object -Unique) {
        try {
            $current = (Resolve-Path $seed -ErrorAction Stop).Path
        } catch {
            continue
        }

        while ($current) {
            if (Test-FlorrBtRoot $current) { return $current }
            $parent = Split-Path $current -Parent
            if (-not $parent -or $parent -eq $current) { break }
            $current = $parent
        }
    }

    throw "Cannot find FlorrBt root. Run install_web_task.ps1 -Root D:\VSProject\FlorrBt"
}

function Find-Node {
    $candidates = @()
    if ($env:NODE_EXE) { $candidates += $env:NODE_EXE }

    $nodeCmd = Get-Command node.exe -ErrorAction SilentlyContinue
    if ($nodeCmd) { $candidates += $nodeCmd.Source }

    $candidates += @(
        "C:\Program Files\nodejs\node.exe",
        "C:\Program Files (x86)\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\nodejs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe"
    )

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (-not (Test-Path $candidate)) { continue }
        if ($candidate -like "*\WindowsApps\*") { continue }
        try {
            $version = & $candidate --version 2>$null
            if ($LASTEXITCODE -eq 0 -and $version) { return $candidate }
        } catch {
            continue
        }
    }

    return $null
}

function Quote-Arg([string]$Value) {
    if ($null -eq $Value) { return '""' }
    if ($Value -match '[\s"]') {
        return '"' + ($Value -replace '"', '\"') + '"'
    }
    return $Value
}

function Join-Args([string[]]$Items) {
    return ($Items | ForEach-Object { Quote-Arg $_ }) -join " "
}

try {
    $RepoRoot = Resolve-FlorrBtRoot $Root
    $EnvFile = Join-Path $PSScriptRoot "web.env.ps1"
    if (Test-Path $EnvFile) {
        . $EnvFile
    }

    if (-not $WebHost) { $WebHost = if ($env:WEB_HOST) { $env:WEB_HOST } else { "0.0.0.0" } }
    if ($WebPort -le 0) { $WebPort = if ($env:WEB_PORT) { [int]$env:WEB_PORT } else { 8080 } }
    if (-not $GameHost) { $GameHost = if ($env:GAME_HOST) { $env:GAME_HOST } else { "127.0.0.1" } }
    if ($GamePort -le 0) { $GamePort = if ($env:GAME_PORT) { [int]$env:GAME_PORT } else { 10012 } }

    $Node = Find-Node
    if (-not $Node) {
        Write-Host "Node.js was not found. Install Node.js LTS from https://nodejs.org/." -ForegroundColor Red
        Read-Host "Press Enter to exit"
        exit 1
    }

    $StartScript = Join-Path $PSScriptRoot "start_web.ps1"
    if (-not (Test-Path $StartScript)) {
        throw "Cannot find start script: $StartScript"
    }

    $arguments = Join-Args @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $StartScript,
        "-Root", $RepoRoot,
        "-WebHost", $WebHost,
        "-WebPort", [string]$WebPort,
        "-GameHost", $GameHost,
        "-GamePort", [string]$GamePort,
        "-NoPause"
    )

    $Action = New-ScheduledTaskAction -Execute "powershell.exe" -Argument $arguments -WorkingDirectory $RepoRoot
    $Trigger = New-ScheduledTaskTrigger -AtLogOn
    $Settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)

    Register-ScheduledTask -TaskName $TaskName -Action $Action -Trigger $Trigger -Settings $Settings -Force | Out-Null
    Start-ScheduledTask -TaskName $TaskName

    Write-Host "Installed and started scheduled task: $TaskName"
    Write-Host "Root  : $RepoRoot"
    Write-Host "URL   : http://SERVER_IP:$WebPort"
    Write-Host "Bridge: ws://$WebHost`:$WebPort/ws -> $GameHost`:$GamePort"
    Write-Host "Use Task Scheduler or this command to stop it:"
    Write-Host "Stop-ScheduledTask -TaskName $TaskName"
    Read-Host "Press Enter to exit"
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Read-Host "Press Enter to exit"
    exit 1
}
