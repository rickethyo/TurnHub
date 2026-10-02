@echo off
rem One-button flash without signing: pulls, builds and flashes every attached board. Options: -NoPull, -DryRun
rem flash-all.cmd does the same but signs the firmware with your local key first.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-all.ps1" %*
echo.
pause
