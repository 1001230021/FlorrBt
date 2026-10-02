@echo off
setlocal
cd /d "%~dp0..\.."
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0mysql.ps1" -Action start %*
exit /b %errorlevel%
