@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0coop.ps1" join
echo.
pause
