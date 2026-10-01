@echo off
rem One-button flash: double-click, or run from a terminal. Options: -NoPull, -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-all.ps1" %*
echo.
pause
