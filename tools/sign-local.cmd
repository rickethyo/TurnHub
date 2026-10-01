@echo off
rem Build and sign firmware with your local key. Options: -Products atlas,sigil-oled  -Key path
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign-local.ps1" %*
echo.
pause
