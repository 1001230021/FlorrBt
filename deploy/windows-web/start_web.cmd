@echo off
set "SCRIPT_DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%start_web.ps1" %*
if errorlevel 1 (
  echo.
  echo FlorrBt web client failed to start.
)
pause
