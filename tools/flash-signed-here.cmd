@echo off
rem Temporary-laptop signed flash: builds Include, signs with the key in
rem %USERPROFILE%\TurnHub-Private, verifies and flashes every attached board.
rem Options: -NoPull, -DryRun
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash-signed-here.ps1" %*
echo.
pause
