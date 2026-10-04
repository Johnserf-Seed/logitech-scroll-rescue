@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scroll-rescue-cli.ps1" %*
exit /b %errorlevel%
