@echo off
:: Reset Logitech LIGHTSPEED receiver - fixes stuck infinite scrolling
:: Double-click to run (auto-elevates to admin). Equivalent to unplug/replug.

net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo Searching for Logitech USB receiver (VID_046D)...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$ids = Get-PnpDevice -Class USB | Where-Object { $_.InstanceId -like 'USB\VID_046D*' -and $_.InstanceId -notmatch '&MI_' -and $_.Status -eq 'OK' } | Select-Object -ExpandProperty InstanceId; if (-not $ids) { Write-Host 'No Logitech USB device found.'; exit 1 } else { foreach ($id in $ids) { Write-Host \"Restarting: $id\"; pnputil /restart-device \"$id\" } }"

echo.
echo Done. Scrolling should be back to normal.
timeout /t 3 >nul
