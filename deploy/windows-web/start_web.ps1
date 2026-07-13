param(
    [string]$Root = "",
    [string]$WebHost = "",
    [int]$WebPort = 0,
    [string]$GameHost = "",
    [int]$GamePort = 0,
    [switch]$NoPause
)

$ErrorActionPreference = "Stop"

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

    throw "Cannot find FlorrBt root. Put this script inside the FlorrBt folder, or run: start_web.ps1 -Root D:\VSProject\FlorrBt"
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

function Get-LocalWebUrls([string]$BindHost, [int]$Port) {
    $urls = New-Object System.Collections.Generic.List[string]
    if ($BindHost -eq "0.0.0.0" -or $BindHost -eq "::") {
        $urls.Add("http://127.0.0.1:$Port")
        try {
            $addresses = Get-NetIPAddress -AddressFamily IPv4 -ErrorAction Stop |
                Where-Object { $_.IPAddress -notlike "169.254.*" -and $_.IPAddress -ne "127.0.0.1" } |
                Select-Object -ExpandProperty IPAddress -Unique
            foreach ($address in $addresses) {
                $urls.Add("http://$address`:$Port")
            }
        } catch {
            $urls.Add("http://SERVER_IP:$Port")
        }
    } else {
        $urls.Add("http://$BindHost`:$Port")
    }
    return $urls
}

function Wait-Exit {
    if (-not $NoPause) {
        Read-Host "Press Enter to exit"
    }
}

try {
    $RepoRoot = Resolve-FlorrBtRoot $Root
    Set-Location $RepoRoot

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
        Write-Host "Node.js was not found." -ForegroundColor Red
        Write-Host "Install Node.js LTS from https://nodejs.org/ and run this again."
        Wait-Exit
        exit 1
    }

    $BridgeScript = Join-Path $RepoRoot "tools\web_client_server.js"
    if (-not (Test-Path $BridgeScript)) {
        Write-Host "Cannot find web bridge script: $BridgeScript" -ForegroundColor Red
        Wait-Exit
        exit 1
    }

    $env:FLORRBT_ROOT = $RepoRoot
    $env:WEB_HOST = $WebHost
    $env:WEB_PORT = [string]$WebPort
    $env:GAME_HOST = $GameHost
    $env:GAME_PORT = [string]$GamePort

    Write-Host "Starting FlorrBt web client..."
    Write-Host "Root  : $RepoRoot"
    Write-Host "Node  : $Node"
    Write-Host "Bind  : $WebHost`:$WebPort"
    Write-Host "Bridge: ws://$WebHost`:$WebPort/ws -> $GameHost`:$GamePort"
    Write-Host "Open:"
    foreach ($url in Get-LocalWebUrls $WebHost $WebPort) {
        Write-Host "  $url"
    }
    Write-Host ""

    & $Node $BridgeScript
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        Write-Host "Web server exited with code $exitCode" -ForegroundColor Red
    }
    Wait-Exit
    exit $exitCode
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    Wait-Exit
    exit 1
}
