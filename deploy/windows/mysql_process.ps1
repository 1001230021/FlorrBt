[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$MySqlExe
)

$ErrorActionPreference = "Stop"

# Invoke mysqld through PowerShell's native call operator. MySQL 8.x on
# Windows can lose command-line options when it is launched by another
# process; the managed my.ini in the installation directory carries the
# runtime paths to the actual server process.
& $MySqlExe "--standalone"
exit $LASTEXITCODE
