@echo off
setlocal
set "SCRIPT_DIR=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%start_web.ps1" -NoPause %*
set "EXIT_CODE=%ERRORLEVEL%"
if not "%EXIT_CODE%"=="0" (
  echo.
  echo FlorrBt web client failed to start. Exit code: %EXIT_CODE%
)
echo.
pause
exit /b %EXIT_CODE%
