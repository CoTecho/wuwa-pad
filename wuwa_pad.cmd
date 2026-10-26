@echo off
setlocal
set "DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%DIR%scripts\start_agent.ps1"
if not exist "%DIR%scripts\watch.run" type nul > "%DIR%scripts\watch.run"
powershell -NoProfile -ExecutionPolicy Bypass -File "%DIR%scripts\start_watch.ps1"
echo.
echo wuwapad is running. Press any gamepad button to test the pet.
timeout /t 4 >nul