param(
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$InstallDirectory,
    [Parameter(Mandatory=$true)][ValidateSet('Debug','Release')][string]$Configuration
)
$ErrorActionPreference = 'Stop'
$sourceDirectory = Join-Path $PSScriptRoot '../cmake/curl'
$configure = @('-S', $sourceDirectory, '-B', $BuildDirectory, '-A', 'x64',
    ('-DCMAKE_INSTALL_PREFIX=' + $InstallDirectory), '-DCMAKE_INSTALL_LIBDIR=lib')
if ($env:FLORRBT_CURL_SOURCE_DIR) {
    $configure += '-DFETCHCONTENT_SOURCE_DIR_CURL=' + $env:FLORRBT_CURL_SOURCE_DIR
}
& cmake @configure
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $BuildDirectory --config $Configuration --target libcurl_shared --parallel 4
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --install $BuildDirectory --config $Configuration --prefix $InstallDirectory
exit $LASTEXITCODE
