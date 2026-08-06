@echo off
cd /d "%~dp0"
call deploy\windows\deploy.cmd -Mode start-web
pause
