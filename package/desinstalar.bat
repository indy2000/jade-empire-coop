@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0coop.ps1" uninstall
echo.
pause
