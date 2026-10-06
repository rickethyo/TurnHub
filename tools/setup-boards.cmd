@echo off
rem Board setup: identifies every attached board (CH340 = Atlas, CP210x = Sigil, whose firmware
rem reports E-ink or OLED), asks what each new one is, and records it in tools\boards.local.md
rem so flash-all knows which firmware it gets. No pull, build or flash. Option: -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-all.ps1" -Setup %*
echo.
pause
