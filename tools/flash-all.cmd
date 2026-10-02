@echo off
rem One-button signed flash: pulls (when clean), builds, signs with your local key, verifies and
rem flashes every attached board. Options: -NoPull, -DryRun, -Key path
rem For a plain unsigned build and flash, use flash-all-unsigned.cmd.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-all.ps1" -Sign %*
echo.
pause
