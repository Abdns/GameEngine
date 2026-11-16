@echo off
setlocal
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%~dp0vs\Sync-Projects.ps1"
if errorlevel 1 exit /b %errorlevel%
echo [vs] Projects and filters are up to date.
exit /b 0
