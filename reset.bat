@echo off
setlocal
set "tool=%~dp0scroll-rescue-cli.exe"
if not exist "%tool%" set "tool=%~dp0target\release\scroll-rescue-cli.exe"
if not exist "%tool%" (
    echo Please extract the full portable package or build the project first.
    echo scroll-rescue-cli.exe was not found.
    pause
    exit /b 2
)
"%tool%" repair %*
set "result=%errorlevel%"
echo.
if "%~1"=="" pause
exit /b %result%
