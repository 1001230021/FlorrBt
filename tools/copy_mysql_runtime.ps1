[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MySqlRoot,

    [Parameter(Mandatory = $true)]
    [string]$OutputDir
)

$ErrorActionPreference = "Stop"

$runtimeFiles = @(
    @{ Name = "libmysql.dll"; SourceDirectory = "lib" },
    @{ Name = "libssl-3-x64.dll"; SourceDirectory = "bin" },
    @{ Name = "libcrypto-3-x64.dll"; SourceDirectory = "bin" }
)

try {
    $resolvedOutputDir = [System.IO.Path]::GetFullPath($OutputDir)
    New-Item -ItemType Directory -Force -Path $resolvedOutputDir | Out-Null

    foreach ($runtimeFile in $runtimeFiles) {
        $source = Join-Path (Join-Path $MySqlRoot $runtimeFile.SourceDirectory) $runtimeFile.Name
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            throw "MySQL runtime file not found: $source"
        }

        Copy-Item -LiteralPath $source -Destination (Join-Path $resolvedOutputDir $runtimeFile.Name) -Force
    }

    Write-Host "Copied MySQL, OpenSSL and crypto runtime DLLs to $resolvedOutputDir"
    exit 0
}
catch {
    Write-Error $_.Exception.Message
    exit 1
}
