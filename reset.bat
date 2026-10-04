@echo off
setlocal
call "%~dp0scroll-rescue-cli.cmd" repair %*
set "result=%errorlevel%"
echo.
if "%~1"=="" pause
exit /b %result%
