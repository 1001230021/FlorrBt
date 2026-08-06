@echo off
setlocal
cd /d "%~dp0"
if exist "x64\Release\FlorrBt.Launcher.exe" (
  "x64\Release\FlorrBt.Launcher.exe" --server "x64\Release\FlorrBt.Server.exe"
) else (
  "x64\Debug\FlorrBt.Launcher.exe" --server "x64\Debug\FlorrBt.Server.exe"
)
