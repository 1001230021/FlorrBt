param(
    [ValidateSet("start", "start-server", "start-web", "firewall", "install", "uninstall", "stop", "restart", "status")]
    [string]$Mode = "start",
    [string]$WebHost = "0.0.0.0",
    [int]$WebPort = 8080,
    [string]$GameHost = "127.0.0.1",
    [int]$GamePort = 10012,
    [string]$ServerExe = "",
    [switch]$NoServer,
    [switch]$NoWeb
)

$ErrorActionPreference = "Stop"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$LogDir = Join-Path $RepoRoot "logs"
$ServerTaskName = "FlorrBtServer"
$WebTaskName = "FlorrBtWeb"

function Write-Section([string]$Text) {
    Write-Host ""
    Write-Host "== $Text ==" -ForegroundColor Cyan
}

function Test-Admin {
    $principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Require-Admin {
    if (-not (Test-Admin)) {
        throw "This mode needs Administrator. Right-click deploy.cmd and choose Run as administrator, or run PowerShell as Administrator."
    }
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

function Build-SelfArgs([string]$ChildMode) {
    $items = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $PSCommandPath,
        "-Mode", $ChildMode,
        "-WebHost", $WebHost,
        "-WebPort", [string]$WebPort,
        "-GameHost", $GameHost,
        "-GamePort", [string]$GamePort
    )

    if ($ServerExe) {
        $items += @("-ServerExe", $ServerExe)
    }

    return Join-Args $items
}

function Build-InteractiveSelfArgs([string]$ChildMode) {
    return "-NoExit " + (Build-SelfArgs $ChildMode)
}

function Find-Node {
    $candidates = @(
        (Get-Command node -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
        "C:\Program Files\nodejs\node.exe",
        "C:\Program Files (x86)\nodejs\node.exe",
        "$env:LOCALAPPDATA\Programs\nodejs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\18\Insiders\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Microsoft\VisualStudio\NodeJs\node.exe"
    )
    $candidates += Get-ChildItem -Path "$env:LOCALAPPDATA\OpenAI\Codex\bin" -Recurse -Filter node.exe -ErrorAction SilentlyContinue |
        Select-Object -ExpandProperty FullName

    foreach ($candidate in $candidates | Where-Object { $_ } | Select-Object -Unique) {
        if (-not (Test-Path $candidate)) { continue }
        if ($candidate -like "*\WindowsApps\OpenAI.Codex_*") { continue }
        try {
            $version = & $candidate --version 2>$null
            if ($LASTEXITCODE -eq 0 -and $version) { return $candidate }
        } catch {
            continue
        }
    }

    throw "Node.js was not found. Install Node.js LTS or add node.exe to PATH."
}

function Resolve-ServerExe {
    if ($ServerExe) {
        $resolved = Resolve-Path $ServerExe -ErrorAction Stop
        return $resolved.Path
    }

    $candidates = @(
        (Join-Path $RepoRoot "x64\Release\FlorrBt.Server.exe"),
        (Join-Path $RepoRoot "x64\Debug\FlorrBt.Server.exe"),
        (Join-Path $RepoRoot "build\Release\FlorrBt.Server.exe"),
        (Join-Path $RepoRoot "build\Debug\FlorrBt.Server.exe")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }

    throw "FlorrBt.Server.exe was not found. Build the server first, or pass -ServerExe <path>."
}

function Ensure-ServerCfg {
    $cfg = Join-Path $RepoRoot "data\server.cfg"
    if (Test-Path $cfg) {
        Write-Host "Using existing data/server.cfg. It controls the real game port and server config."
        return
    }

    $template = Join-Path $PSScriptRoot "server.cfg.example"
    if (-not (Test-Path $template)) { return }

    $content = Get-Content $template -Raw
    $content = $content -replace "set port 10012", "set port $GamePort"
    Set-Content -Path $cfg -Value $content -Encoding ASCII
    Write-Host "Created data/server.cfg from deploy template."
}

function Open-Port([int]$Port, [string]$Name) {
    $existing = Get-NetFirewallRule -DisplayName $Name -ErrorAction SilentlyContinue
    if ($existing) {
        Write-Host "Firewall rule exists: $Name"
        return
    }

    New-NetFirewallRule -DisplayName $Name -Direction Inbound -Protocol TCP -LocalPort $Port -Action Allow | Out-Null
    Write-Host "Opened TCP port $Port ($Name)."
}

function Start-ServerForeground {
    Set-Location $RepoRoot
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    Ensure-ServerCfg

    $exe = Resolve-ServerExe
    Write-Section "FlorrBt server"
    Write-Host "Root: $RepoRoot"
    Write-Host "Exe : $exe"
    Write-Host "Port: $GamePort unless data/server.cfg overrides it"
    & $exe
}

function Start-WebForeground {
    Set-Location $RepoRoot
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

    $node = Find-Node
    $script = Join-Path $RepoRoot "tools\web_client_server.js"
    if (-not (Test-Path $script)) {
        throw "tools/web_client_server.js was not found."
    }

    $env:WEB_HOST = $WebHost
    $env:WEB_PORT = [string]$WebPort
    $env:GAME_HOST = $GameHost
    $env:GAME_PORT = [string]$GamePort

    Write-Section "FlorrBt web"
    Write-Host "URL   : http://$WebHost`:$WebPort"
    Write-Host "Bridge: ws://$WebHost`:$WebPort/ws -> $GameHost`:$GamePort"
    Write-Host "Node  : $node"
    & $node $script
}

function Start-Interactive {
    Set-Location $RepoRoot

    if (-not $NoServer) {
        Start-Process -FilePath "powershell.exe" -ArgumentList (Build-InteractiveSelfArgs "start-server") -WorkingDirectory $RepoRoot
    }
    if (-not $NoWeb) {
        Start-Process -FilePath "powershell.exe" -ArgumentList (Build-InteractiveSelfArgs "start-web") -WorkingDirectory $RepoRoot
    }

    Write-Section "Started"
    if (-not $NoServer) { Write-Host "Server window launched." }
    if (-not $NoWeb) { Write-Host "Web window launched: http://SERVER_IP:$WebPort" }
}

function Install-Tasks {
    Require-Admin
    Set-Location $RepoRoot

    if (-not $NoServer) {
        Ensure-ServerCfg
        Resolve-ServerExe | Out-Null
    }
    if (-not $NoWeb) {
        Find-Node | Out-Null
    }

    Open-Port $WebPort "FlorrBt Web $WebPort"
    if (-not $NoServer) {
        Open-Port $GamePort "FlorrBt Game $GamePort"
    }

    $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1)
    $trigger = New-ScheduledTaskTrigger -AtLogOn

    if (-not $NoServer) {
        $serverAction = New-ScheduledTaskAction -Execute "powershell.exe" -Argument (Build-SelfArgs "start-server") -WorkingDirectory $RepoRoot
        Register-ScheduledTask -TaskName $ServerTaskName -Action $serverAction -Trigger $trigger -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName $ServerTaskName
        Write-Host "Installed and started scheduled task: $ServerTaskName"
    }

    if (-not $NoWeb) {
        $webAction = New-ScheduledTaskAction -Execute "powershell.exe" -Argument (Build-SelfArgs "start-web") -WorkingDirectory $RepoRoot
        Register-ScheduledTask -TaskName $WebTaskName -Action $webAction -Trigger $trigger -Settings $settings -Force | Out-Null
        Start-ScheduledTask -TaskName $WebTaskName
        Write-Host "Installed and started scheduled task: $WebTaskName"
    }
}

function Stop-Tasks {
    foreach ($taskName in @($ServerTaskName, $WebTaskName)) {
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($task) {
            Stop-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
            Write-Host "Stopped task: $taskName"
        }
    }
}

function Uninstall-Tasks {
    Require-Admin
    Stop-Tasks
    foreach ($taskName in @($ServerTaskName, $WebTaskName)) {
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($task) {
            Unregister-ScheduledTask -TaskName $taskName -Confirm:$false
            Write-Host "Removed task: $taskName"
        }
    }
}

function Show-Status {
    Write-Section "Tasks"
    foreach ($taskName in @($ServerTaskName, $WebTaskName)) {
        $task = Get-ScheduledTask -TaskName $taskName -ErrorAction SilentlyContinue
        if ($task) {
            $info = Get-ScheduledTaskInfo -TaskName $taskName
            Write-Host "${taskName}: $($task.State), last result $($info.LastTaskResult)"
        } else {
            Write-Host "${taskName}: not installed"
        }
    }

    Write-Section "Ports"
    foreach ($port in @($WebPort, $GamePort)) {
        $listening = Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue
        if ($listening) {
            Write-Host "TCP ${port}: listening"
        } else {
            Write-Host "TCP ${port}: not listening"
        }
    }
}

switch ($Mode) {
    "start" { Start-Interactive }
    "start-server" { Start-ServerForeground }
    "start-web" { Start-WebForeground }
    "firewall" {
        Require-Admin
        Open-Port $WebPort "FlorrBt Web $WebPort"
        if (-not $NoServer) { Open-Port $GamePort "FlorrBt Game $GamePort" }
    }
    "install" { Install-Tasks }
    "uninstall" { Uninstall-Tasks }
    "stop" { Stop-Tasks }
    "restart" {
        Stop-Tasks
        if (-not $NoServer) { Start-ScheduledTask -TaskName $ServerTaskName -ErrorAction SilentlyContinue }
        if (-not $NoWeb) { Start-ScheduledTask -TaskName $WebTaskName -ErrorAction SilentlyContinue }
    }
    "status" { Show-Status }
}
