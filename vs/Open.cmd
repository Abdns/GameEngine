@echo off
setlocal
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0Sync-Projects.ps1"
if errorlevel 1 exit /b 1
start "" "%~dp0Engine.sln"
