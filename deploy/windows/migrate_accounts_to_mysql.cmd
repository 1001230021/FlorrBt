@echo off
setlocal
cd /d "%~dp0..\.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0migrate_accounts_to_mysql.ps1" %*
exit /b %errorlevel%
